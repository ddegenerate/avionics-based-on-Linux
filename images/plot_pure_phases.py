#!/usr/bin/env python3
"""
plot_pure_phases.py — 单时间片 (Slot) 纯两阶段接力调度示意图
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
C_IDLE_BG, C_IDLE_BD = '#F1F5F9', '#94A3B8' # 灰白色 (余量)

C_CRIT_TXT = '#B45309'
C_ELAS_TXT = '#047857'
C_IDLE_TXT = '#475569'
C_LINE     = '#CBD5E1'

# ==================== 绘制任务块 (完美对齐算法) ====================
def draw_task(ax, x, y, w, h, label, bg_color, bd_color, text_color, fs=16, ls='-'):
    pad = 0.15
    r_size = 0.15

    # 逆向补偿外扩，实现像素级对齐
    ax_x, ax_y, ax_w, ax_h = x + pad, y + pad, w - 2 * pad, h - 2 * pad

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
                path_effects=get_stroke(text_color, 0.4) if text_color != C_IDLE_TXT else None, zorder=3)

# ==================== 画布初始化 ====================
# 专门为这种单行长条图定制了扁平化比例 (18x5)
fig, ax = plt.subplots(figsize=(18, 5), dpi=300)
fig.patch.set_facecolor('white')
# X轴表示比例 0~100
ax.set_xlim(-2, 102)
ax.set_ylim(0, 12)
ax.axis('off')

Y_TASK = 5
H_TASK = 4

# ==================== 绘制任务方块流 ====================
# [阶段 1] 纯关键任务 (总宽 62)
draw_task(ax, 0,  Y_TASK, 14, H_TASK, 'nav',  C_CRIT_BG, C_CRIT_BD, C_CRIT_TXT)
draw_task(ax, 14, Y_TASK, 13, H_TASK, 'ctrl', C_CRIT_BG, C_CRIT_BD, C_CRIT_TXT)
draw_task(ax, 27, Y_TASK, 14, H_TASK, 'nav',  C_CRIT_BG, C_CRIT_BD, C_CRIT_TXT)
draw_task(ax, 41, Y_TASK, 13, H_TASK, 'ctrl', C_CRIT_BG, C_CRIT_BD, C_CRIT_TXT)
draw_task(ax, 54, Y_TASK, 8,  H_TASK, '...',  C_IDLE_BG, C_IDLE_BD, C_IDLE_TXT, fs=24)

# [阶段 2] 纯弹性接棒 (总宽 28)
draw_task(ax, 62, Y_TASK, 10, H_TASK, '弹性', C_ELAS_BG, C_ELAS_BD, C_ELAS_TXT)
draw_task(ax, 72, Y_TASK, 9,  H_TASK, '弹性', C_ELAS_BG, C_ELAS_BD, C_ELAS_TXT)
draw_task(ax, 81, Y_TASK, 9,  H_TASK, '弹性', C_ELAS_BG, C_ELAS_BD, C_ELAS_TXT)

# [余量] (总宽 10)
draw_task(ax, 90, Y_TASK, 10, H_TASK, 'margin', C_IDLE_BG, C_LINE, C_IDLE_TXT, fs=14, ls='--')

# ==================== 垂直分割辅助线 ====================
for vx in [0, 62, 90, 100]:
    ax.plot([vx, vx], [1, 10], color=C_LINE, linestyle=':', lw=2.5, zorder=0)

# ==================== 底部尺寸标注 (阶段说明) ====================
def draw_dim(x1, x2, text, color):
    y_arr = 2.5
    # 绘制双向箭头
    ax.annotate('', xy=(x2, y_arr), xytext=(x1, y_arr),
                arrowprops=dict(arrowstyle='<|-|>', color=color, lw=2.5, mutation_scale=15))
    # 绘制居中文字
    ax.text((x1+x2)/2, y_arr - 0.6, text, ha='center', va='top', fontsize=16,
            color=color, fontweight='bold', path_effects=get_stroke(color, 0.5))

draw_dim(0, 62, '阶段 1: 纯关键任务', '#EA580C')
draw_dim(62, 90, '阶段 2: 纯弹性接棒', '#059669')

# ==================== 顶部标题 ====================
ax.text(50, 10.5, 'Slot 时间窗口：纯粹的两阶段调度模型', ha='center', va='bottom',
        fontsize=22, color='#0F172A', fontweight='bold', path_effects=get_stroke('#0F172A', 1.0))

# ==================== 渲染与保存 ====================
plt.tight_layout()
plt.savefig('pure_phases_schedule.png', dpi=300, bbox_inches='tight', facecolor='white')
print("[完成] 纯阶段调度示意图已保存为: pure_phases_schedule.png")
