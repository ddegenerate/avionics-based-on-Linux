#!/usr/bin/env python3
"""
plot_maf_timeline.py — ARINC 653 MAF 周期与多核并发时序原理图
修复版：加入 pad 逆向补偿算法，实现边框、辅助线、箭头的 100% 像素级严格对齐。
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
C_SLOT0_BG, C_SLOT0_BD = '#EFF6FF', '#3B82F6'  # 浅蓝
C_SLOT1_BG, C_SLOT1_BD = '#F0FDF4', '#22C55E'  # 浅绿
C_TEXT = '#1E293B'  
C_DESC = '#64748B'  
C_BOUND = '#EF4444' # 边界红色

# ==================== 辅助绘图函数 ====================
def draw_box(ax, x, y, w, h, bg_color, bd_color, text1, text2, alpha=1.0, ls='-'):
    # 设置固定的圆角膨胀系数
    pad = 0.2
    r_size = 0.2
    
    # ★ 核心修复：逆向补偿外扩。
    # 因为 FancyBboxPatch 会自动在四周外扩 pad 大小，
    # 为了让最终画出来的外边缘严格对齐指定的 [x, x+w] 和 [y, y+h]，
    # 我们需要在传入参数时，把起始点往里收缩 pad，把宽高减去 2*pad。
    ax_x = x + pad
    ax_y = y + pad
    ax_w = w - 2 * pad
    ax_h = h - 2 * pad

    # 阴影 (轻微右下偏移)
    ax.add_patch(FancyBboxPatch((ax_x+0.15, ax_y-0.15), ax_w, ax_h, 
                                boxstyle=f'round,pad={pad},rounding_size={r_size}',
                                facecolor='black', alpha=0.1*alpha, edgecolor='none', zorder=1))
    # 主框
    ax.add_patch(FancyBboxPatch((ax_x, ax_y), ax_w, ax_h, 
                                boxstyle=f'round,pad={pad},rounding_size={r_size}',
                                facecolor=bg_color, edgecolor=bd_color, linewidth=2.5, 
                                alpha=alpha, linestyle=ls, zorder=2))
                                
    # 内部文字 (使用最初未缩放的 x, y, w, h 寻找绝对中心)
    if text1:
        ax.text(x + w/2, y + h/2 + 0.6, text1, ha='center', va='center', fontsize=16, 
                color=C_TEXT, fontweight='bold', path_effects=get_stroke(C_TEXT, 0.5), alpha=alpha, zorder=3)
        ax.text(x + w/2, y + h/2 - 0.8, text2, ha='center', va='center', fontsize=13, 
                color=C_DESC, fontweight='bold', path_effects=get_stroke(C_DESC, 0.4), alpha=alpha, zorder=3)

# ==================== 画布初始化 ====================
fig, ax = plt.subplots(figsize=(16, 6.5), dpi=300)
fig.patch.set_facecolor('white')
# X 轴表示时间 0 ~ 33ms
ax.set_xlim(-2, 34)
ax.set_ylim(-2, 12.5)
ax.axis('off')

# ==================== 绘制主时间块 ====================
Y_BASE = 3
H_BOX = 4

# Slot 0 (10ms)
draw_box(ax, 0, Y_BASE, 10, H_BOX, C_SLOT0_BG, C_SLOT0_BD, 
         "分区 1 运行", "Core 10 + Core 11 并行")

# Slot 1 (15ms)
draw_box(ax, 10, Y_BASE, 15, H_BOX, C_SLOT1_BG, C_SLOT1_BD, 
         "分区 2 运行", "Core 10 + Core 11 并行")

# Slot 0 (下周期, 截断展示)
draw_box(ax, 25, Y_BASE, 8, H_BOX, C_SLOT0_BG, C_SLOT0_BD, 
         "分区 1 运行\n(下一周期) ...", "", alpha=0.6, ls='--')

# ==================== 绘制顶部尺寸标注线 ====================
# MAF 周期总括
ax.annotate('', xy=(25, 10.5), xytext=(0, 10.5), arrowprops=dict(arrowstyle='<|-|>', color=C_TEXT, lw=2.5, mutation_scale=15))
ax.text(12.5, 10.8, 'MAF 周期 (25ms)', ha='center', va='bottom', fontsize=18, fontweight='bold', color=C_TEXT, path_effects=get_stroke(C_TEXT, 0.8))

# Slot 0 尺寸
ax.annotate('', xy=(10, 8.5), xytext=(0, 8.5), arrowprops=dict(arrowstyle='<|-|>', color=C_SLOT0_BD, lw=2, mutation_scale=12))
ax.text(5, 8.8, 'Slot 0: 10ms', ha='center', va='bottom', fontsize=14, fontweight='bold', color=C_SLOT0_BD, path_effects=get_stroke(C_SLOT0_BD, 0.6))

# Slot 1 尺寸
ax.annotate('', xy=(25, 8.5), xytext=(10, 8.5), arrowprops=dict(arrowstyle='<|-|>', color=C_SLOT1_BD, lw=2, mutation_scale=12))
ax.text(17.5, 8.8, 'Slot 1: 15ms', ha='center', va='bottom', fontsize=14, fontweight='bold', color=C_SLOT1_BD, path_effects=get_stroke(C_SLOT1_BD, 0.6))

# ==================== 绘制垂直边界线 ====================
for x in [0, 10, 25]:
    ax.plot([x, x], [2, 11.5], color=C_BOUND, linestyle='--', lw=2.5, alpha=0.7, zorder=0)

# ==================== 绘制底部同步触发说明 ====================
def draw_bottom_annotation(x, text, is_first=False):
    # 向上指的箭头
    ax.annotate('', xy=(x, 2.5), xytext=(x, 0.8), arrowprops=dict(arrowstyle='-|>', color=C_BOUND, lw=2.5, mutation_scale=15))
    
    if is_first:
        ax.text(x, 0.3, 'MAF 纪元起点\n(Epoch)', ha='center', va='top', fontsize=14, fontweight='bold', 
                color=C_BOUND, path_effects=get_stroke(C_BOUND, 0.6), linespacing=1.5)
    else:
        ax.text(x, 0.3, '↑ TIMER_ABSTIME 边界\n绝对时间唤醒，无相位漂移', ha='center', va='top', fontsize=14, fontweight='bold', 
                color=C_BOUND, path_effects=get_stroke(C_BOUND, 0.6), linespacing=1.5)

draw_bottom_annotation(0, "", True)
draw_bottom_annotation(10, "")
draw_bottom_annotation(25, "")

# ==================== 渲染与保存 ====================
plt.tight_layout()
plt.savefig('maf_timeline_optimized.png', dpi=300, bbox_inches='tight', facecolor='white')
print("[完成] 严格对齐版 MAF 周期时序原理图已保存为: maf_timeline_optimized.png")