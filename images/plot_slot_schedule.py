#!/usr/bin/env python3
"""
plot_slot_schedule.py — ARINC 653 单时间片 (Slot) 内两阶段弹性接力调度原理图
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
C_CRIT_BG, C_CRIT_BD = '#FEF3C7', '#F59E0B' # 琥珀色 (关键任务)
C_ELAS_BG, C_ELAS_BD = '#D1FAE5', '#10B981' # 翠绿色 (弹性任务)
C_IDLE_BG, C_IDLE_BD = '#F8FAFC', '#94A3B8' # 灰白色 (余量)

C_CRIT_TXT = '#B45309'
C_ELAS_TXT = '#047857'
C_IDLE_TXT = '#475569'

# ==================== 绘制任务块 (完美对齐算法) ====================
def draw_task(ax, x, y, w, h, label, bg_color, bd_color, text_color, fs=15, ls='-'):
    pad = 0.15
    r_size = 0.15

    # 逆向补偿外扩，实现像素级对齐
    ax_x = x + pad
    ax_y = y + pad
    ax_w = w - 2 * pad
    ax_h = h - 2 * pad

    # 阴影 (轻微右下偏移)
    ax.add_patch(FancyBboxPatch((ax_x+0.15, ax_y-0.15), ax_w, ax_h,
                                boxstyle=f'round,pad={pad},rounding_size={r_size}',
                                facecolor='black', alpha=0.1, edgecolor='none', zorder=1))
    # 主框
    ax.add_patch(FancyBboxPatch((ax_x, ax_y), ax_w, ax_h,
                                boxstyle=f'round,pad={pad},rounding_size={r_size}',
                                facecolor=bg_color, edgecolor=bd_color, linewidth=2.5,
                                linestyle=ls, zorder=2))
    # 居中文字
    if label:
        ax.text(x + w/2, y + h/2, label, ha='center', va='center',
                fontsize=fs, color=text_color, fontweight='bold',
                path_effects=get_stroke(text_color, 0.4), zorder=3)

# ==================== 画布初始化 ====================
fig, ax = plt.subplots(figsize=(18, 6), dpi=300)
fig.patch.set_facecolor('white')
# X轴表示百分比 0~100 (代表整个 Slot 的可用空间)
ax.set_xlim(-3, 103)
ax.set_ylim(-1.5, 17)
ax.axis('off')

Y_TASK = 5
H_TASK = 4

# ==================== 绘制背景垂直辅助线 ====================
# 分界点：0 (起点), 66 (阶段1结束), 90 (阶段2结束), 100 (终点)
for vx in [0, 66, 90, 100]:
    ax.plot([vx, vx], [1.5, 13.5], color='#CBD5E1', linestyle='--', lw=2, zorder=0)

# ==================== 顶部尺寸标注 ====================
def draw_dim(x1, x2, text, color, y_arr=10.2, y_txt=10.6, fs=16):
    ax.annotate('', xy=(x2, y_arr), xytext=(x1, y_arr),
                arrowprops=dict(arrowstyle='<|-|>', color=color, lw=2.5, mutation_scale=15))
    ax.text((x1+x2)/2, y_txt, text, ha='center', va='bottom', fontsize=fs,
            color=color, fontweight='bold', path_effects=get_stroke(color, 0.6))

# 整体 Slot 窗口线
draw_dim(0, 100, '完整 Slot 时间窗口 (例如 10ms)', '#3B82F6', y_arr=12.5, y_txt=13.0, fs=18)

# 三个阶段细分线
draw_dim(0, 66, '阶段 1: 关键 + 弹性交织', '#EA580C')
draw_dim(66, 90, '阶段 2: 弹性接棒', '#059669')
draw_dim(90, 100, '安全余量', '#64748B')

# ==================== 绘制任务方块流 ====================
# [阶段 1] 总宽 66
draw_task(ax, 0,  Y_TASK, 14, H_TASK, 'nav',    C_CRIT_BG, C_CRIT_BD, C_CRIT_TXT)
draw_task(ax, 14, Y_TASK, 12, H_TASK, 'sensor', C_CRIT_BG, C_CRIT_BD, C_CRIT_TXT)
draw_task(ax, 26, Y_TASK, 8,  H_TASK, '弹性',   C_ELAS_BG, C_ELAS_BD, C_ELAS_TXT)
draw_task(ax, 34, Y_TASK, 12, H_TASK, 'ctrl',   C_CRIT_BG, C_CRIT_BD, C_CRIT_TXT)
draw_task(ax, 46, Y_TASK, 12, H_TASK, 'nav',    C_CRIT_BG, C_CRIT_BD, C_CRIT_TXT)
draw_task(ax, 58, Y_TASK, 8,  H_TASK, '...',    C_IDLE_BG, C_IDLE_BD, C_IDLE_TXT, fs=20)

# [阶段 2] 总宽 24
draw_task(ax, 66, Y_TASK, 8, H_TASK, '弹性', C_ELAS_BG, C_ELAS_BD, C_ELAS_TXT)
draw_task(ax, 74, Y_TASK, 8, H_TASK, '弹性', C_ELAS_BG, C_ELAS_BD, C_ELAS_TXT)
draw_task(ax, 82, Y_TASK, 8, H_TASK, '弹性', C_ELAS_BG, C_ELAS_BD, C_ELAS_TXT)

# [余量] 总宽 10
draw_task(ax, 90, Y_TASK, 10, H_TASK, 'Margin', C_IDLE_BG, '#CBD5E1', C_IDLE_TXT, fs=14, ls='--')


# ==================== 底部触发条件标注 ====================
def draw_bottom_trigger(x, title, subtitle):
    # 向上红箭头
    ax.plot([x, x], [Y_TASK, 2.5], color='#EF4444', linestyle=':', lw=2.5, zorder=0)
    ax.annotate('', xy=(x, 4.5), xytext=(x, 2.5),
                arrowprops=dict(arrowstyle='-|>', color='#EF4444', lw=2.5, mutation_scale=18))
    # 触发条件文字
    ax.text(x, 1.8, title, ha='center', va='top', fontsize=16, color='#EF4444',
            fontweight='bold', path_effects=get_stroke('#EF4444', 0.6))
    ax.text(x, 0.4, subtitle, ha='center', va='top', fontsize=14, color='#EF4444',
            fontweight='bold', path_effects=get_stroke('#EF4444', 0.4))

draw_bottom_trigger(66, '↑ 关键任务停止触发', '(剩余可用时间 < 关键预算 + 余量)')
draw_bottom_trigger(90, '↑ margin 枯竭触发', '(触及 EWMA 动态安全红线)')


# ==================== 全局大标题 ====================
ax.text(50, 15.5, '单时间片 (Slot) 内的两阶段弹性接力调度原理', ha='center', va='center',
        fontsize=24, color='#0F172A', fontweight='bold', path_effects=get_stroke('#0F172A', 1.0))

# ==================== 渲染与保存 ====================
plt.tight_layout()
plt.savefig('slot_schedule_optimized.png', dpi=300, bbox_inches='tight', facecolor='white')
print("[完成] 单 Slot 调度原理图已保存为: slot_schedule_optimized.png")
