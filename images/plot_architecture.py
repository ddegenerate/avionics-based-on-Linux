#!/usr/bin/env python3
"""
plot_architecture.py — ARINC 653 多核并行分区实验框架 (三层架构总览)
全局特黑加粗版：引入 path_effects 描边技术，强制突破系统字体字重限制，实现真加粗。
"""

import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
from matplotlib.lines import Line2D
import matplotlib.font_manager as fm
import matplotlib.patheffects as pe

# ==================== 中文字体自动适配 ====================
def setup_chinese_font():
    for _f in fm.fontManager.ttflist:
        _n = _f.name.lower()
        if any(k in _n for k in ['microsoft yahei', 'pingfang sc', 'noto sans sc', 
                                 'simhei', 'source han sans', 'wqy-microhei']):
            plt.rcParams['font.sans-serif'] = [_f.name]
            plt.rcParams['axes.unicode_minus'] = False
            print(f'[OK] 已加载中文字体: {_f.name}')
            return
    print("[Warn] 未找到优质中文字体，可能出现乱码")

setup_chinese_font()

# 全局设置默认加粗
plt.rcParams['font.weight'] = 'bold'

# ==================== 全局配色方案 ====================
C_BG_L1, C_BD_L1 = '#EFF6FF', '#3B82F6'  
C_BG_L2, C_BD_L2 = '#F0FDF4', '#22C55E'  
C_BG_L3, C_BD_L3 = '#FEF2F2', '#EF4444'  

C_CRIT = '#F59E0B'  
C_ELAS = '#10B981'  
C_IDLE = '#E2E8F0'  
C_SL0  = '#60A5FA'  
C_SL1  = '#34D399'  
C_TEXT = '#1E293B'  
C_DESC = '#64748B'  

# ==================== 强制加粗辅助函数 ====================
def get_stroke(color, lw=0.7):
    """利用路径描边特效，强制放大字体字重，突破系统字体限制"""
    return [pe.withStroke(linewidth=lw, foreground=color)]

# ==================== 核心绘图函数 ====================
def draw_box(ax, x, y, w, h, bg_color, bd_color, lw=2.0, alpha=1.0):
    ax.add_patch(FancyBboxPatch(
        (x, y), w, h, boxstyle='round,pad=0.5,rounding_size=0.5',
        facecolor=bg_color, edgecolor=bd_color, linewidth=lw, alpha=alpha, zorder=1
    ))

def draw_task(ax, x, y, w, h, label, bg_color, text_color='white', fs=13):
    if w <= 0: return
    # 阴影
    ax.add_patch(FancyBboxPatch(
        (x+0.2, y-0.2), w, h, boxstyle='round,pad=0.2,rounding_size=0.2',
        facecolor='black', alpha=0.1, edgecolor='none', zorder=2
    ))
    # 主体
    ax.add_patch(FancyBboxPatch(
        (x, y), w, h, boxstyle='round,pad=0.2,rounding_size=0.2',
        facecolor=bg_color, edgecolor='white', linewidth=1.0, zorder=3
    ))
    
    fs_use = fs
    if w < 1.8: fs_use = 0       
    elif w < 3.8: fs_use = fs - 3  
        
    if fs_use > 0:
        # 任务标签强制加粗描边
        ax.text(x + w/2, y + h/2, label, ha='center', va='center', 
                fontsize=fs_use, color=text_color, fontweight='bold', zorder=4, linespacing=1.4,
                path_effects=get_stroke(text_color, 0.7))

# ==================== 初始化画布 ====================
fig, ax = plt.subplots(figsize=(25, 13), dpi=300)
fig.patch.set_facecolor('white')
ax.set_xlim(0, 126)
ax.set_ylim(-2, 112)
ax.axis('off')

# 时间轴映射
X_START = 8
W_SLOT0 = 32
GAP = 2
X_SLOT1 = X_START + W_SLOT0 + GAP
W_SLOT1 = 46

# ============================================================
# Layer 1 — MAF 全局基准时钟 
# ============================================================
draw_box(ax, 2, 2, 89, 20, C_BG_L1, C_BD_L1)
ax.text(4, 21.5, 'Layer 1 — MAF 全局基准时钟 (MafClock)', fontsize=16, fontweight='bold', color=C_BD_L1, va='top', path_effects=get_stroke(C_BD_L1, 0.8))
ax.text(4, 18.2, 'CLOCK_MONOTONIC 单调时钟  |  纪元对齐 (epoch)  |  TIMER_ABSTIME 绝对睡眠 → 消除相位漂移', fontsize=12, fontweight='bold', color=C_DESC, va='top')

H_BAR = 6
Y_BAR = 6
draw_task(ax, X_START, Y_BAR, W_SLOT0, H_BAR, 'Slot 0 (10ms)\n分区 1 运行窗口', C_SL0, fs=13)
draw_task(ax, X_SLOT1, Y_BAR, W_SLOT1, H_BAR, 'Slot 1 (15ms)\n分区 2 运行窗口', C_SL1, fs=13)
ax.text((X_START + X_SLOT1 + W_SLOT1)/2, Y_BAR - 3.5, 'MAF 总周期 = 10ms + 15ms = 25ms → 循环往复', ha='center', fontsize=13, fontweight='bold', color=C_TEXT, path_effects=get_stroke(C_TEXT, 0.5))

# ============================================================
# Layer 2 — 分区配置 & 任务分配 
# ============================================================
draw_box(ax, 2, 26, 89, 24, C_BG_L2, C_BD_L2)
ax.text(4, 48.5, 'Layer 2 — 分区配置与任务分配 (PartitionTable / TaskAssigner)', fontsize=16, fontweight='bold', color=C_BD_L2, va='top', path_effects=get_stroke(C_BD_L2, 0.8))
ax.text(4, 45.2, '静态配置任务列表  |  WCET 感知贪心负载均衡  |  零跨核状态共享', fontsize=12, fontweight='bold', color=C_DESC, va='top')

draw_box(ax, X_START-1, 29, W_SLOT0+2, 13, 'white', C_BD_L2, lw=1.5)
ax.text(X_START + W_SLOT0/2, 40, '分区 1 (P1)', ha='center', fontsize=14, fontweight='bold', color=C_BD_L2, path_effects=get_stroke(C_BD_L2, 0.6))
ax.text(X_START + W_SLOT0/2, 37.2, 'Core 10–11 | 10ms | PARALLEL ★', ha='center', fontsize=11, fontweight='bold', color=C_DESC)
draw_task(ax, X_START, 30, 8, 5, 'nav\n3.5ms', C_CRIT, fs=13)
draw_task(ax, X_START+9, 30, 7, 5, 'ctrl\n3ms', C_CRIT, fs=13)
draw_task(ax, X_START+17, 30, 6, 5, 'sensor\n2ms', C_CRIT, fs=13)
draw_task(ax, X_START+24, 30, 7, 5, 'test\n0.5ms', C_ELAS, fs=13)

draw_box(ax, X_SLOT1-1, 29, W_SLOT1+2, 13, 'white', C_BD_L2, lw=1.5)
ax.text(X_SLOT1 + W_SLOT1/2, 40, '分区 2 (P2)', ha='center', fontsize=14, fontweight='bold', color=C_BD_L2, path_effects=get_stroke(C_BD_L2, 0.6))
ax.text(X_SLOT1 + W_SLOT1/2, 37.2, 'Core 10–11 | 15ms | PARALLEL ★', ha='center', fontsize=11, fontweight='bold', color=C_DESC)
draw_task(ax, X_SLOT1, 30, 9, 5, 'mission\n4.5ms', C_CRIT, fs=13)
draw_task(ax, X_SLOT1+10, 30, 8, 5, 'flight\n3ms', C_CRIT, fs=13)
draw_task(ax, X_SLOT1+19, 30, 8, 5, 'engine\n2.5ms', C_CRIT, fs=13)
draw_task(ax, X_SLOT1+28, 30, 8, 5, 'fuel\n1.5ms', C_CRIT, fs=13)
draw_task(ax, X_SLOT1+37, 30, 8, 5, 'log\n0.5ms', C_ELAS, fs=13)

# ============================================================
# Layer 3 — 运行时调度 
# ============================================================
draw_box(ax, 2, 54, 89, 40, C_BG_L3, C_BD_L3)
ax.text(4, 92.5, 'Layer 3 — 运行时调度 (WorkerThread)', fontsize=16, fontweight='bold', color=C_BD_L3, va='top', path_effects=get_stroke(C_BD_L3, 0.8))
ax.text(4, 89.2, 'SCHED_FIFO(99) 物理绑核  |  EWMA+2σ 自适应余量  |  关键/弹性双阶段接力', fontsize=12, fontweight='bold', color=C_DESC, va='top')

H_CORE = 6
Y_C10 = 76
Y_C11 = 62

ax.text(4, Y_C10+2.5, 'Core 10', fontsize=14, fontweight='bold', color=C_TEXT, path_effects=get_stroke(C_TEXT, 0.7))
ax.text(4, Y_C11+2.5, 'Core 11', fontsize=14, fontweight='bold', color=C_TEXT, path_effects=get_stroke(C_TEXT, 0.7))

ax.text((X_START+24)/2 + 8, Y_C10+8.5, '▼ 阶段 1: 关键任务快速轮转', ha='center', fontsize=13, color=C_CRIT, fontweight='bold', path_effects=get_stroke(C_CRIT, 0.5))
ax.text(X_START+28, Y_C10+8.5, '▼ 阶段 2: 弹性接棒 (榨干)', ha='center', fontsize=13, color=C_ELAS, fontweight='bold', path_effects=get_stroke(C_ELAS, 0.5))

draw_task(ax, X_START, Y_C10, 10, H_CORE, 'P1-nav', C_CRIT, fs=13)
draw_task(ax, X_START+11, Y_C10, 10, H_CORE, 'P1-nav', C_CRIT, fs=13)
draw_task(ax, X_START+22, Y_C10, 4, H_CORE, 'test', C_ELAS, fs=13)
draw_task(ax, X_START+27, Y_C10, 4, H_CORE, 'test', C_ELAS, fs=13)
draw_task(ax, X_START+31.5, Y_C10, 0.5, H_CORE, '', C_IDLE) 

draw_task(ax, X_SLOT1, Y_C10, 12, H_CORE, 'P2-mission', C_CRIT, fs=13)
draw_task(ax, X_SLOT1+13, Y_C10, 8, H_CORE, 'P2-fuel', C_CRIT, fs=13)
draw_task(ax, X_SLOT1+22, Y_C10, 12, H_CORE, 'P2-mission', C_CRIT, fs=13)
draw_task(ax, X_SLOT1+35, Y_C10, 5, H_CORE, 'log', C_ELAS, fs=13)
draw_task(ax, X_SLOT1+41, Y_C10, 4, H_CORE, 'log', C_ELAS, fs=13)
draw_task(ax, X_SLOT1+45.5, Y_C10, 0.5, H_CORE, '', C_IDLE)

draw_task(ax, X_START, Y_C11, 8, H_CORE, 'P1-ctrl', C_CRIT, fs=13)
draw_task(ax, X_START+9, Y_C11, 6, H_CORE, 'P1-sensor', C_CRIT, fs=13)
draw_task(ax, X_START+16, Y_C11, 8, H_CORE, 'P1-ctrl', C_CRIT, fs=13)
draw_task(ax, X_START+25, Y_C11, 4, H_CORE, 'test', C_ELAS, fs=13)
draw_task(ax, X_START+30, Y_C11, 2, H_CORE, 'test', C_ELAS, fs=13)

draw_task(ax, X_SLOT1, Y_C11, 9, H_CORE, 'P2-flight', C_CRIT, fs=13)
draw_task(ax, X_SLOT1+10, Y_C11, 8, H_CORE, 'P2-engine', C_CRIT, fs=13)
draw_task(ax, X_SLOT1+19, Y_C11, 9, H_CORE, 'P2-flight', C_CRIT, fs=13)
draw_task(ax, X_SLOT1+29, Y_C11, 8, H_CORE, 'P2-engine', C_CRIT, fs=13)
draw_task(ax, X_SLOT1+38, Y_C11, 5, H_CORE, 'log', C_ELAS, fs=13)
draw_task(ax, X_SLOT1+44, Y_C11, 2, H_CORE, 'log', C_ELAS, fs=13)

ax.text(X_SLOT1 + W_SLOT1/2, 57, '决策流: 若 (剩余时间 < 余量 Margin + 任务预算 Budget) → 主动放弃，进入下阶段或休眠', 
        ha='center', fontsize=13, fontweight='bold', color=C_BD_L3,
        bbox=dict(boxstyle='round,pad=0.6', facecolor='white', edgecolor=C_BD_L3, lw=1.5),
        path_effects=get_stroke(C_BD_L3, 0.4))

# ============================================================
# 同步边界线 
# ============================================================
for x_line in [X_START, X_START + W_SLOT0, X_SLOT1, X_SLOT1 + W_SLOT1]:
    ax.axvline(x=x_line, ymin=0.03, ymax=0.86, color=C_BD_L1, lw=2.5, linestyle='--', alpha=0.6, zorder=0)

ax.text(X_START, 96, 'MAF 周期起点\n(绝对对齐)', ha='center', va='bottom', fontsize=13, color=C_BD_L1, fontweight='bold', path_effects=get_stroke(C_BD_L1, 0.6))
ax.text((X_START + W_SLOT0 + X_SLOT1) / 2, 96, '多核同步切换点\n(Slot 0 → Slot 1)', ha='center', va='bottom', fontsize=13, color=C_BD_L1, fontweight='bold', path_effects=get_stroke(C_BD_L1, 0.6))
ax.text(X_SLOT1 + W_SLOT1, 96, 'MAF 周期终点\n(绝对对齐)', ha='center', va='bottom', fontsize=13, color=C_BD_L1, fontweight='bold', path_effects=get_stroke(C_BD_L1, 0.6))

# ============================================================
# 右侧说明面板
# ============================================================
def draw_note(ax, x, y, w, h, title, text, bd_color):
    draw_box(ax, x, y, w, h, 'white', bd_color, lw=1.5)
    ax.text(x + 1.2, y + h - 1.5, f"▎{title}", fontsize=14, fontweight='bold', color=bd_color, va='top', path_effects=get_stroke(bd_color, 0.7))
    ax.plot([x+1, x+w-1], [y+h-4.0, y+h-4.0], color=bd_color, alpha=0.3, lw=1)
    ax.text(x + 1.2, y + h - 6.0, text, fontsize=12, fontweight='bold', color=C_TEXT, va='top', linespacing=1.8)

txt_l3 = (
    "阶段 1: 关键任务循环\n"
    "  只要剩余时间充裕，各核心循环消费分配\n"
    "  给自己的关键任务。\n\n"
    "阶段 2: 弹性接棒\n"
    "  时间不足以跑关键任务时，切换跑轻量级\n"
    "  的弹性任务，直至触及动态安全红线。\n\n"
    "休眠 (Sleep):\n"
    "  调用 clock_nanosleep 主动交出 CPU。"
)
draw_note(ax, 93, 54, 31, 40, '运行调度算法', txt_l3, C_BD_L3)

txt_l2 = (
    "WCET 感知贪心 ★\n"
    "编译/配置期按\n"
    "budget降序，\n"
    "将任务分发到当前\n"
    "负载最小的物理核。\n\n"
    "零跨核通讯:\n"
    "因为在 L2 层静态\n"
    "分好，L3 运行时\n"
    "各核只读本地队列，\n"
    "无需锁或屏障。"
)
draw_note(ax, 93, 2, 14.5, 49, '任务 → 核心分配', txt_l2, C_BD_L2)

txt_l1 = (
    "纪元对齐 (Epoch):\n"
    "主线程确立绝对\n"
    "起点，各核以此\n"
    "为锚计算时间。\n\n"
    "TIMER_ABSTIME:\n"
    "Linux底层hrtimer\n"
    "绝对时间中断唤醒，\n"
    "彻底消除微秒级\n"
    "相位漂移。"
)
draw_note(ax, 109.5, 2, 14.5, 49, '多核时间同步', txt_l1, C_BD_L1)

# ============================================================
# 全局图例与标题 
# ============================================================
ax.text(63, 109, 'ARINC 653 并行分区实验框架 — 三层架构总览', ha='center', va='top', fontsize=26, fontweight='bold', color='#0F172A', path_effects=get_stroke('#0F172A', 1.5))

legend_items = [
    Line2D([0], [0], marker='s', color='w', markerfacecolor=C_CRIT, markersize=14, label='关键任务 (必须保障)'),
    Line2D([0], [0], marker='s', color='w', markerfacecolor=C_ELAS, markersize=14, label='弹性任务 (回收碎片)'),
    Line2D([0], [0], marker='s', color='w', markerfacecolor=C_IDLE, markersize=14, label='安全余量 (主动休眠)'),
]
leg = ax.legend(handles=legend_items, loc='upper left', fontsize=13, ncol=3, frameon=False, bbox_to_anchor=(0.02, 0.94))
for text in leg.get_texts():
    text.set_fontweight('bold')

# ============================================================
plt.tight_layout()
plt.savefig('architecture_timeline_optimized.png', dpi=300, bbox_inches='tight', facecolor='white')
print("[完成] 真加粗架构图已保存为: architecture_timeline_optimized.png")