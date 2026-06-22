#!/usr/bin/env python3
"""
plot_observability.py — ARINC 653 双通道可观测性体系架构图
高精度、无重叠、特黑加粗渲染版
"""

import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
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
plt.rcParams['font.weight'] = 'bold'

# ==================== 强制加粗描边特效 ====================
def get_stroke(color, lw=0.7):
    return [pe.withStroke(linewidth=lw, foreground=color)]

# ==================== 核心配色 (Tailwind) ====================
C_BG_BASE = '#F8FAFC'
C_BD_BASE = '#94A3B8'

# 左通道 (内核 ftrace 视角 - 橙红)
C_L_BG, C_L_BD, C_L_TXT = '#FFF7ED', '#EA580C', '#9A3412'
# 右通道 (用户态 CSV 视角 - 翡翠绿)
C_R_BG, C_R_BD, C_R_TXT = '#ECFDF5', '#10B981', '#065F46'
# 代码块 (IDE 暗色调)
C_CODE_BG, C_CODE_TXT = '#0F172A', '#38BDF8'

C_TEXT = '#0F172A'
C_DESC = '#64748B'

# ==================== 绘制带精准对齐的方块 ====================
def draw_box(ax, x, y, w, h, bg_color, bd_color, title="", text="", title_color=C_TEXT, text_color=C_DESC):
    pad = 0.2
    r_size = 0.2

    # 逆向补偿外扩，实现像素级对齐
    ax_x = x + pad
    ax_y = y + pad
    ax_w = w - 2 * pad
    ax_h = h - 2 * pad

    # 阴影
    ax.add_patch(FancyBboxPatch((ax_x+0.2, ax_y-0.2), ax_w, ax_h,
                                boxstyle=f'round,pad={pad},rounding_size={r_size}',
                                facecolor='black', alpha=0.1, edgecolor='none', zorder=1))
    # 主框
    ax.add_patch(FancyBboxPatch((ax_x, ax_y), ax_w, ax_h,
                                boxstyle=f'round,pad={pad},rounding_size={r_size}',
                                facecolor=bg_color, edgecolor=bd_color, linewidth=2.5, zorder=2))

    # 填充文字
    if title:
        ax.text(x + w/2, y + h - 1.5, title, ha='center', va='top',
                fontsize=16, color=title_color, fontweight='bold',
                path_effects=get_stroke(title_color, 0.4), zorder=3)
        # 分隔线
        ax.plot([x + 1, x + w - 1], [y + h - 3.5, y + h - 3.5], color=bd_color, alpha=0.3, lw=2, zorder=3)
    if text:
        ax.text(x + w/2, y + h/2 - 1, text, ha='center', va='center',
                fontsize=14, color=text_color, fontweight='bold',
                path_effects=get_stroke(text_color, 0.3), linespacing=1.6, zorder=3)

# ==================== 绘制直角正交箭头 ====================
def draw_ortho_arrow(ax, p1, p2, mid_y, color, lw=3):
    ax.plot([p1[0], p1[0]], [p1[1], mid_y], color=color, lw=lw, zorder=0)
    ax.plot([p1[0], p2[0]], [mid_y, mid_y], color=color, lw=lw, zorder=0)
    ax.annotate('', xy=(p2[0], p2[1]), xytext=(p2[0], mid_y),
                arrowprops=dict(arrowstyle='-|>', color=color, lw=lw, mutation_scale=20))

def draw_straight_arrow(ax, p1, p2, color, lw=3):
    ax.annotate('', xy=p2, xytext=p1,
                arrowprops=dict(arrowstyle='-|>', color=color, lw=lw, mutation_scale=20))

# ==================== 画布初始化 ====================
fig, ax = plt.subplots(figsize=(16, 12), dpi=300)
fig.patch.set_facecolor('white')
ax.set_xlim(0, 100)
ax.set_ylim(0, 100)
ax.axis('off')

# ============================================================
# 顶部: WorkerThread 运行循环 (暗色代码风格)
# ============================================================
WT_X, WT_Y, WT_W, WT_H = 20, 68, 60, 24
draw_box(ax, WT_X, WT_Y, WT_W, WT_H, C_BG_BASE, C_BD_BASE,
         title="WorkerThread 运行时执行循环 (两阶段交织)", title_color=C_TEXT)

# 代码终端块
CODE_X, CODE_Y, CODE_W, CODE_H = 24, 70, 52, 14
draw_box(ax, CODE_X, CODE_Y, CODE_W, CODE_H, C_CODE_BG, C_CODE_BG)

code_lines = [
    "▶ trace_marker::slot_event(\"WAKE/SLEEP\");",
    "▶ trace_marker::task_begin(\"task_name\");",
    "▶ trace_marker::task_end(\"task_name\", elapsed_us);",
    "▶ slot_log::event(core, pid, \"TASK\", dur_us);"
]

for i, line in enumerate(code_lines):
    ax.text(CODE_X + 2, CODE_Y + CODE_H - 2 - i*3, line, ha='left', va='top',
            fontsize=15, color=C_CODE_TXT, fontweight='bold', fontfamily='monospace',
            path_effects=get_stroke(C_CODE_TXT, 0.4), zorder=4)

# ============================================================
# 中间路由: 左右通道分发箭头
# ============================================================
SPLIT_Y = 62
# 到 ftrace
draw_ortho_arrow(ax, (50, 68), (25, 54), SPLIT_Y, C_L_BD)
# 到 CSV
draw_ortho_arrow(ax, (50, 68), (75, 54), SPLIT_Y, C_R_BD)

# ============================================================
# 左通道 (ftrace 视角)
# ============================================================
L_X, L_W = 7, 36
# Data node
draw_box(ax, L_X, 42, L_W, 12, C_L_BG, C_L_BD,
         title="通道 1: ftrace 内核通道", title_color=C_L_TXT,
         text="/sys/kernel/tracing/trace_marker\n(用户态打点注入内核缓冲区)", text_color=C_L_TXT)

# Arrow
draw_straight_arrow(ax, (25, 42), (25, 34), C_L_BD)

# Analysis node
draw_box(ax, L_X, 10, L_W, 24, C_L_BD, C_L_BD,
         title="trace-cmd + KernelShark", title_color='white',
         text="【 内核调度视角 】\n\n结合 sched_switch 追踪\n验证 CPU 物理核绝对时间对齐\n排查 CFS 抢占与定时器漂移", text_color='white')

# ============================================================
# 右通道 (CSV 视角)
# ============================================================
R_X, R_W = 57, 36
# Data node
draw_box(ax, R_X, 42, R_W, 12, C_R_BG, C_R_BD,
         title="通道 2: CSV 日志通道", title_color=C_R_TXT,
         text="trace_log.csv\n(标准 std::fprintf 直写落盘)", text_color=C_R_TXT)

# Arrow
draw_straight_arrow(ax, (75, 42), (75, 34), C_R_BD)

# Analysis node
draw_box(ax, R_X, 10, R_W, 24, C_R_BD, C_R_BD,
         title="分析脚本 (Python)", title_color='white',
         text="【 分区任务视角 】\n\nplot_slots.py 绘制多核甘特图\n精确统计时间片溢出与执行抖动\n观察弹性任务 Margin 利用率", text_color='white')

# ============================================================
# 全局大标题
# ============================================================
ax.text(50, 96, '框架双通道可观测性 (Observability) 体系', ha='center', va='center',
        fontsize=26, color='#0F172A', fontweight='bold', path_effects=get_stroke('#0F172A', 1.0))

# ==================== 渲染与保存 ====================
plt.tight_layout()
plt.savefig('observability_architecture.png', dpi=300, bbox_inches='tight', facecolor='white')
print("[完成] 双通道可观测性架构图已保存为: observability_architecture.png")
