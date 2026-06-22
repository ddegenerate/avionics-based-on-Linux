#!/usr/bin/env python3
"""
plot_slots.py — 读取 trace_log.csv, 画双层 slot 甘特图

用法:
    python3 plot_slots.py [trace_log.csv] [--max-cycles N] [--detail-cycles M]

输出:
    trace_log.png — 双层甘特图:
       上层 (Overview): 聚合视图, 每个 slot 一个堆叠柱状图, 宏观展示分区时间分配
       下层 (Detail):   放大视图, 前 M 个 MAF 周期的逐任务条形图, 验证调度交替逻辑

参数:
    --max-cycles N     只解析前 N 个 MAF 周期的数据 (默认 10, CSV 很大时关键!)
    --detail-cycles M  放大视图显示前 M 个周期 (默认 3)

依赖: pip install matplotlib

示例:
    python3 plot_slots.py trace_log.csv
    python3 plot_slots.py trace_log.csv --max-cycles 20 --detail-cycles 5
"""

import csv
import sys
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.ticker as ticker
from matplotlib.lines import Line2D
from collections import defaultdict

# ==================== 配色 ====================
COLOR_LIST = [
    '#e41a1c', '#377eb8', '#4daf4a', '#984ea3', '#ff7f00',
    '#a65628', '#f781bf', '#999999', '#66c2a5', '#fc8d62',
    '#8dd3c7', '#bebada', '#fb8072', '#80b1d3', '#fdb462',
]
IDLE_COLOR       = '#e8e8e8'
PARTITION_BG     = {1: '#f7f7f7', 2: '#ffffff'}
TASK_COLORS      = {}   # 任务名 → 颜色 (自动分配)


def get_color(name: str) -> str:
    if name not in TASK_COLORS:
        TASK_COLORS[name] = COLOR_LIST[len(TASK_COLORS) % len(COLOR_LIST)]
    return TASK_COLORS[name]


# ==================== CSV 解析 (支持提前截断) ====================

def parse_csv(path: str, max_cycles: int):
    """
    读取 trace_log.csv, 边读边构建 slot, 达到 max_cycles 个完整 slot
    (每个 core) 后停止解析, 避免大 CSV 撑爆内存.

    返回:
      slots = [(core, pid, t_start_us, t_end_us, [(t_us, task, dur_us, event_type)])]
    """
    # open_slots: (core, pid) → (t_wake_us, [task_events])
    open_slots = {}
    closed_slots = []       # 已完成的 slot

    # 统计每个 core 已完成多少个 slot, 用于截断判断
    core_slot_count = defaultdict(int)
    # 目标: 每个 core 至少收集 max_cycles 个 slot
    # 但不同 core 的 slot 可能不同时到达, 用最慢的 core 达到 max_cycles 即停止
    max_per_core = max_cycles

    with open(path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            t     = int(row['timestamp_us'])
            core  = int(row['core'])
            pid   = int(row['partition_id'])
            etype = row['event'].strip()
            task  = row['task'].strip()
            dur   = int(row['duration_us'])

            key = (core, pid)

            if etype == 'WAKE':
                open_slots[key] = (t, [])
            elif etype == 'SLEEP' and key in open_slots:
                t_wake, task_events = open_slots.pop(key)
                closed_slots.append((core, pid, t_wake, t, task_events))
                core_slot_count[core] += 1
            elif etype in ('TASK', 'ELASTIC') and key in open_slots:
                open_slots[key][1].append((t, task, dur, etype))

            # 所有已出现的 core 都收集够了就停止
            if core_slot_count and all(
                    core_slot_count.get(c, 0) >= max_per_core
                    for c in core_slot_count):
                break

    if not closed_slots:
        print("错误: 未找到完整的 WAKE-SLEEP 对")
        sys.exit(1)

    # 如果某些 core 超过了 max_cycles, 截掉多余的, 保持各 core 数量一致
    # (按 core 分组, 每组取前 max_cycles)
    by_core = defaultdict(list)
    for s in closed_slots:
        by_core[s[0]].append(s)
    trimmed = []
    for core, lst in by_core.items():
        trimmed.extend(lst[:max_cycles])
    trimmed.sort(key=lambda s: s[2])  # 按开始时间排序

    print(f'解析完成: {len(trimmed)} 个 slot, '
          f'每个 core 最多 {max_cycles} 个')
    return trimmed


# ==================== 辅助 ====================

def guess_maf_period(slots) -> float:
    """从连续同分区 WAKE 间隔推算 MAF 周期 (微秒)."""
    pid_wakes = defaultdict(list)
    for s in slots:
        pid_wakes[s[1]].append(s[2])
    for pid, wakes in pid_wakes.items():
        if len(wakes) >= 2:
            return wakes[1] - wakes[0]
    # 回退: 第一轮所有 slot 时长之和
    cores = sorted(set(s[0] for s in slots))
    return sum(s[3] - s[2] for s in slots[:len(cores)])


def pick_tick_interval(maf_ms: float) -> tuple:
    """根据 MAF 周期长度选择主/次刻度间距 (ms)."""
    if maf_ms <= 10:
        return 2.0, 0.5
    elif maf_ms <= 30:
        return 5.0, 1.0
    elif maf_ms <= 60:
        return 10.0, 2.0
    else:
        return 25.0, 5.0


def setup_ticks(ax, major_ms: float, minor_ms: float):
    """在 x 轴上设置毫秒级主/次刻度."""
    ax.xaxis.set_major_locator(ticker.MultipleLocator(major_ms))
    ax.xaxis.set_minor_locator(ticker.MultipleLocator(minor_ms))
    ax.tick_params(axis='x', which='major', labelsize=9, length=6)
    ax.tick_params(axis='x', which='minor', length=3)
    ax.grid(axis='x', alpha=0.25, which='major')
    ax.grid(axis='x', alpha=0.10, which='minor')


# ==================== 画图函数 ====================

def draw_overview(ax, core_slots, core_id: int,
                  t_min_us: int, t_max_us: int,
                  major_ms: float, minor_ms: float):
    """上层: 每个 slot 一个堆叠柱状图, 各段宽度 ∝ 任务累计耗时."""
    ax.set_ylabel(f'Core {core_id}', fontsize=11, fontweight='bold')
    ax.set_ylim(-0.3, 1.3)
    ax.set_yticks([0.5])
    ax.set_yticklabels([''])
    ax.set_xlim(t_min_us / 1000, t_max_us / 1000)

    for _, pid, t0, t1, task_events in core_slots:
        slot_ms = (t1 - t0) / 1000.0

        # 分区交替背景
        bg = PARTITION_BG.get(pid, '#ffffff')
        ax.axvspan(t0 / 1000, t1 / 1000, alpha=0.30, color=bg, linewidth=0)

        # 累计每个任务的耗时
        task_total = defaultdict(int)
        for _, task, dur, _ in task_events:
            task_total[task] += dur

        # 保持首次出现顺序
        seen_order = []
        for _, task, _, _ in task_events:
            if task not in seen_order:
                seen_order.append(task)

        total_task_us = sum(task_total.values())
        idle_us = max(0, (t1 - t0) - total_task_us)

        # 逐段堆叠
        cum_us = t0
        bar_h = 0.55

        for task in seen_order:
            dur_us = task_total[task]
            if dur_us <= 0:
                continue
            color = get_color(task)
            ax.barh(0.5, dur_us / 1000, bar_h,
                    left=cum_us / 1000,
                    color=color, alpha=0.88,
                    edgecolor='white', linewidth=0.5)

            seg_ms = dur_us / 1000.0
            if seg_ms > slot_ms * 0.05:
                short = task.split('-')[-1][:8]
                ax.text((cum_us + dur_us / 2) / 1000, 0.5,
                        short, ha='center', va='center',
                        fontsize=6.5, color='white', fontweight='bold')
            cum_us += dur_us

        # 空闲段
        if idle_us > 0:
            ax.barh(0.5, idle_us / 1000, bar_h,
                    left=cum_us / 1000,
                    color=IDLE_COLOR, alpha=0.55,
                    edgecolor='white', linewidth=0.5)

        # Slot 边界
        ax.axvline(t0 / 1000, color='#2ca02c', linewidth=2.0, alpha=0.85, linestyle='--')
        ax.axvline(t1 / 1000, color='#d62728', linewidth=2.0, alpha=0.85, linestyle='--')

        # 分区标签
        mid_ms = (t0 + t1) / 2 / 1000
        ax.text(mid_ms, 1.08, f'P{pid}\n{slot_ms:.0f}ms',
                ha='center', va='bottom', fontsize=7.5,
                fontweight='bold', color='#555555')

    setup_ticks(ax, major_ms, minor_ms)


def draw_detail(ax, core_slots, core_id: int,
                t_min_us: float, t_max_us: float,
                major_ms: float, minor_ms: float):
    """下层: 逐条任务 bar, 保留阶段1/阶段2 交替细节."""
    ax.set_ylabel(f'Core {core_id}', fontsize=11, fontweight='bold')
    ax.set_ylim(0, 1)
    ax.set_yticks([])
    padding = (t_max_us - t_min_us) * 0.02  # 2% 留白
    ax.set_xlim((t_min_us - padding) / 1000, (t_max_us + padding) / 1000)

    for _, pid, t0, t1, task_events in core_slots:
        slot_ms = (t1 - t0) / 1000.0

        # 分区背景
        bg = PARTITION_BG.get(pid, '#ffffff')
        ax.axvspan(t0 / 1000, t1 / 1000, alpha=0.30, color=bg, linewidth=0)

        # 逐条 bar
        for t, task, dur, etype in task_events:
            if dur <= 0:
                continue
            color = get_color(task)
            alpha = 0.90 if etype == 'TASK' else 0.50
            hatch = '' if etype == 'TASK' else '///'
            dur_ms = max(dur / 1000.0, 0.003)  # 保证至少可见

            ax.barh(0.5, dur_ms, height=0.60,
                    left=t / 1000, color=color,
                    alpha=alpha, edgecolor='black',
                    linewidth=0.15, hatch=hatch)

            if dur > 500:
                ax.text((t + dur / 2) / 1000, 0.5,
                        task.split('-')[-1][:6],
                        ha='center', va='center',
                        fontsize=5.5, fontweight='bold')

        # Slot 边界
        ax.axvline(t0 / 1000, color='#2ca02c', linewidth=2.0, alpha=0.85, linestyle='--')
        ax.axvline(t1 / 1000, color='#d62728', linewidth=2.0, alpha=0.85, linestyle='--')

        # 标注
        mid = (t0 + t1) / 2 / 1000
        ax.text(mid, 0.88, f'P{pid} {slot_ms:.0f}ms',
                ha='center', va='top', fontsize=7.5,
                fontweight='bold', color='#333333',
                bbox=dict(boxstyle='round,pad=0.25', facecolor='white',
                          edgecolor='#cccccc', alpha=0.85))

    setup_ticks(ax, major_ms, minor_ms)


def build_legend():
    """构建图例句柄."""
    items = []
    for name, color in TASK_COLORS.items():
        items.append(mpatches.Patch(color=color, label=name))
    items.append(mpatches.Patch(color=IDLE_COLOR, label='Idle / margin'))
    items.append(Line2D([0], [0], color='#2ca02c', linestyle='--', linewidth=2,
                        label='WAKE (slot start)'))
    items.append(Line2D([0], [0], color='#d62728', linestyle='--', linewidth=2,
                        label='SLEEP (slot end)'))
    return items


# ==================== main ====================

def main():
    csv_path      = sys.argv[1] if len(sys.argv) > 1 else 'trace_log.csv'
    max_cycles    = 10      # 默认只解析前 10 个 MAF 周期 → 不会卡死
    detail_cycles = 3       # 放大视图显示前 3 个

    args = sys.argv[2:]
    i = 0
    while i < len(args):
        if args[i] in ('--max-cycles',) and i + 1 < len(args):
            max_cycles = int(args[i + 1]); i += 2
        elif args[i] in ('--detail-cycles',) and i + 1 < len(args):
            detail_cycles = int(args[i + 1]); i += 2
        else:
            i += 1

    # 确保 detail_cycles ≤ max_cycles
    detail_cycles = min(detail_cycles, max_cycles)

    # ---- 解析 (自动截断) ----
    slots = parse_csv(csv_path, max_cycles)

    cores   = sorted(set(s[0] for s in slots))
    n_cores = len(cores)
    t_min_us = min(s[2] for s in slots)
    t_max_us = max(s[3] for s in slots)
    maf_period_us = guess_maf_period(slots)
    maf_ms = maf_period_us / 1000.0
    major_ms, minor_ms = pick_tick_interval(maf_ms)

    print(f'核心数:     {n_cores}')
    print(f'总槽位数:   {len(slots)}')
    print(f'MAF 周期:   {maf_ms:.1f} ms')
    print(f'时间跨度:   {(t_max_us - t_min_us) / 1000:.1f} ms')
    print(f'刻度:       主={major_ms}ms  次={minor_ms}ms')
    print(f'Overview:   {max_cycles} 周期')
    print(f'Detail:     {detail_cycles} 周期')

    # ---- 注册任务颜色 ----
    for _, _, _, _, task_events in slots:
        for _, task, _, _ in task_events:
            get_color(task)

    # ---- 拆分 detail 数据 (前 detail_cycles 个 slot) ----
    detail_slots_by_core = {}
    detail_t_min = float('inf')
    detail_t_max = 0.0
    for core in cores:
        core_slots = [s for s in slots if s[0] == core]
        chosen = core_slots[:detail_cycles]
        detail_slots_by_core[core] = chosen
        if chosen:
            detail_t_min = min(detail_t_min, chosen[0][2])
            detail_t_max = max(detail_t_max, chosen[-1][3])

    # ---- 创建图形 ----
    fig = plt.figure(figsize=(22, 3.8 * n_cores * 2))

    gs = fig.add_gridspec(2, 1, height_ratios=[1, 1.25], hspace=0.18,
                          left=0.06, right=0.93, top=0.93, bottom=0.06)
    gs_ov = gs[0].subgridspec(n_cores, 1, hspace=0.10)
    gs_dt = gs[1].subgridspec(n_cores, 1, hspace=0.10)

    axes_ov = [fig.add_subplot(gs_ov[i]) for i in range(n_cores)]
    axes_dt = [fig.add_subplot(gs_dt[i]) for i in range(n_cores)]

    # ---- 上层: Overview ----
    for ax, core in zip(axes_ov, cores):
        core_slots = [s for s in slots if s[0] == core]
        draw_overview(ax, core_slots, core, t_min_us, t_max_us,
                      major_ms, minor_ms)

    n_shown = min(max_cycles, len([s for s in slots if s[0] == cores[0]]))
    axes_ov[0].set_title(
        f'Overview — {n_shown} MAF cycles '
        f'(each bar = one slot, segment width ∝ cumulative task time)',
        fontsize=11, fontweight='bold', loc='left')

    # ---- 下层: Detail ----
    for ax, core in zip(axes_dt, cores):
        draw_detail(ax, detail_slots_by_core[core], core,
                    detail_t_min, detail_t_max,
                    major_ms, minor_ms)

    axes_dt[0].set_title(
        f'Detail — First {detail_cycles} MAF cycle(s) '
        f'(task-level interleaving, stage 1/2 pattern)',
        fontsize=11, fontweight='bold', loc='left')
    axes_dt[-1].set_xlabel('Time (ms)', fontsize=11)

    # ---- 图例 ----
    legend_items = build_legend()
    fig.legend(handles=legend_items, loc='upper center',
               ncol=min(len(legend_items), 9),
               fontsize=8, bbox_to_anchor=(0.5, 0.98))

    fig.suptitle('ARINC 653 Parallel Partition — Slot Gantt Chart',
                 fontsize=14, fontweight='bold', y=0.997)

    # ---- 保存 ----
    out_path = csv_path.replace('.csv', '.png')
    plt.savefig(out_path, dpi=150, bbox_inches='tight')
    print(f'\n已生成: {out_path}')
    plt.close()


if __name__ == '__main__':
    main()
