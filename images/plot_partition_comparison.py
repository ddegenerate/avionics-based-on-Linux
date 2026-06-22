#!/usr/bin/env python3
"""
plot_partition_comparison.py — 分区串行 (Serial) vs 分区并行 (Parallel) 执行时序对比图
高精度、无重叠、特黑加粗渲染版

用法:
    python3 plot_partition_comparison.py

输出:
    partition_comparison.png — 串行与并行分区对比图
"""

import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
import matplotlib.font_manager as fm
import matplotlib.patheffects as pe

# ==================== 中文字体自动适配 ====================
def setup_chinese_font():
    """自动探测系统中可用的中文字体并设置"""
    for _f in fm.fontManager.ttflist:
        _n = _f.name.lower()
        if any(k in _n for k in ['microsoft yahei', 'pingfang sc', 'noto sans sc',
                                 'simhei', 'source han sans', 'wqy-microhei',
                                 'noto sans cjk', 'wenquanyi']):
            plt.rcParams['font.sans-serif'] = [_f.name]
            plt.rcParams['axes.unicode_minus'] = False
            print(f'[OK] 已加载中文字体: {_f.name}')
            return
    # Fallback: 尝试直接指定常见字体名
    for fallback in ['Microsoft YaHei', 'SimHei', 'Noto Sans SC', 'WenQuanYi Micro Hei']:
        try:
            plt.rcParams['font.sans-serif'] = [fallback]
            plt.rcParams['axes.unicode_minus'] = False
            print(f'[OK] 使用 fallback 字体: {fallback}')
            return
        except Exception:
            continue
    print("[Warn] 未找到优质中文字体，可能出现乱码")

setup_chinese_font()
plt.rcParams['font.weight'] = 'bold'

# ==================== 强制加粗描边特效 ====================
def get_stroke(color, lw=0.7):
    """为文字添加描边效果，增强可读性"""
    return [pe.withStroke(linewidth=lw, foreground=color)]

# ==================== 核心配色 (Tailwind-inspired) ====================
C_P1_BG, C_P1_BD, C_P1_TXT = '#EFF6FF', '#3B82F6', '#1E3A8A'   # 蓝
C_P2_BG, C_P2_BD, C_P2_TXT = '#F0FDF4', '#22C55E', '#14532D'   # 绿
C_P3_BG, C_P3_BD, C_P3_TXT = '#FEF3C7', '#F59E0B', '#78350F'   # 黄
C_P4_BG, C_P4_BD, C_P4_TXT = '#F5F3FF', '#A855F7', '#4C1D95'   # 紫

C_TEXT   = '#0F172A'   # 主文字色 (Slate 900)
C_DESC   = '#64748B'   # 描述文字色 (Slate 500)
C_LINE   = '#CBD5E1'   # 分割线色 (Slate 300)
C_RED    = '#EF4444'   # 强调红色 (Red 500)
C_GREEN  = '#22C55E'   # 强调绿色 (Green 500)

# ==================== 绘制任务块 (完美对齐算法) ====================
def draw_task(ax, x, y, w, h, label, bg_color, bd_color, text_color, fs=16):
    """
    在指定位置绘制一个圆角矩形任务块。

    参数:
        ax:          matplotlib axes 对象
        x, y:        任务块左下角坐标 (数据坐标)
        w, h:        任务块宽度和高度 (数据坐标)
        label:       任务块内显示的文字
        bg_color:    背景色
        bd_color:    边框色
        text_color:  文字色
        fs:          文字大小
    """
    pad = 0.15          # 内边距补偿
    r_size = 0.2        # 圆角半径

    # 逆向补偿外扩，实现像素级对齐
    ax_x = x + pad
    ax_y = y + pad
    ax_w = w - 2 * pad
    ax_h = h - 2 * pad

    # 阴影层 (略微偏移，营造立体感)
    ax.add_patch(FancyBboxPatch(
        (ax_x + 0.15, ax_y - 0.15), ax_w, ax_h,
        boxstyle=f'round,pad={pad},rounding_size={r_size}',
        facecolor='black', alpha=0.1, edgecolor='none', zorder=2))

    # 主框体
    ax.add_patch(FancyBboxPatch(
        (ax_x, ax_y), ax_w, ax_h,
        boxstyle=f'round,pad={pad},rounding_size={r_size}',
        facecolor=bg_color, edgecolor=bd_color, linewidth=2.5, zorder=3))

    # 居中文字
    ax.text(x + w / 2, y + h / 2, label,
            ha='center', va='center',
            fontsize=fs, color=text_color, fontweight='bold',
            path_effects=get_stroke(text_color, 0.4), zorder=4)

# ==================== 画布初始化 ====================
fig, ax = plt.subplots(figsize=(20, 8), dpi=300)
fig.patch.set_facecolor('white')

# 坐标系设置
ax.set_xlim(0, 100)
ax.set_ylim(0, 22)
ax.axis('off')

# ==================== 绘制全局框架 ====================
# 中间分割线 (虚线)
ax.plot([50, 50], [1, 20], color=C_LINE, linestyle=':', lw=3, zorder=1)

# CPU 时间轴底线 (Track line)
for y_track in [7, 13]:
    ax.plot([10, 48], [y_track, y_track], color='#E2E8F0', lw=2, zorder=1)
    ax.plot([58, 96], [y_track, y_track], color='#E2E8F0', lw=2, zorder=1)

# === 左侧 CPU 标签 ===
ax.text(8, 13, 'CPU 10', ha='right', va='center',
        fontsize=16, fontweight='bold', color=C_TEXT,
        path_effects=get_stroke(C_TEXT, 0.5))
ax.text(8, 7,  'CPU 11', ha='right', va='center',
        fontsize=16, fontweight='bold', color=C_TEXT,
        path_effects=get_stroke(C_TEXT, 0.5))

# === 右侧 CPU 标签 ===
ax.text(56, 13, 'CPU 10', ha='right', va='center',
        fontsize=16, fontweight='bold', color=C_TEXT,
        path_effects=get_stroke(C_TEXT, 0.5))
ax.text(56, 7,  'CPU 11', ha='right', va='center',
        fontsize=16, fontweight='bold', color=C_TEXT,
        path_effects=get_stroke(C_TEXT, 0.5))

# ============================================================
# 左半部分: 【分区串行 (Serial)】
# ============================================================
ax.text(28, 20, '【分区串行 (Serial)】',
        ha='center', va='bottom', fontsize=20,
        color=C_TEXT, fontweight='bold',
        path_effects=get_stroke(C_TEXT, 0.8))
ax.text(28, 18, '独立调度，交错执行',
        ha='center', va='bottom', fontsize=15,
        color=C_DESC, fontweight='bold',
        path_effects=get_stroke(C_DESC, 0.4))

# 故意错开、交错排布的任务块 — 模拟各核心独立运行不同分区
# CPU 10: P1 (较宽) → P3
draw_task(ax, 11, 11.5, 15, 3, 'P1', C_P1_BG, C_P1_BD, C_P1_TXT)
draw_task(ax, 28, 11.5, 12, 3, 'P3', C_P3_BG, C_P3_BD, C_P3_TXT)
# CPU 11: P2 (更宽、错位起始) → P4
draw_task(ax, 16, 5.5, 18, 3, 'P2', C_P2_BG, C_P2_BD, C_P2_TXT)
draw_task(ax, 36, 5.5, 12, 3, 'P4', C_P4_BG, C_P4_BD, C_P4_TXT)

# ============================================================
# 右半部分: 【分区并行 (Parallel)】
# ============================================================
ax.text(76, 20, '【分区并行 (Parallel)】',
        ha='center', va='bottom', fontsize=20,
        color=C_TEXT, fontweight='bold',
        path_effects=get_stroke(C_TEXT, 0.8))
ax.text(76, 18, '全局时钟，垂直对齐',
        ha='center', va='bottom', fontsize=15,
        color=C_GREEN, fontweight='bold',
        path_effects=get_stroke(C_GREEN, 0.5))

# 绝对对齐的任务块 — 多核同步切换，并发执行同一分区
# CPU 10: P1 → P2 (严格对齐)
draw_task(ax, 60, 11.5, 15, 3, 'P1', C_P1_BG, C_P1_BD, C_P1_TXT)
draw_task(ax, 77, 11.5, 15, 3, 'P2', C_P2_BG, C_P2_BD, C_P2_TXT)
# CPU 11: P1 → P2 (与 CPU 10 垂直完全对齐)
draw_task(ax, 60, 5.5, 15, 3, 'P1', C_P1_BG, C_P1_BD, C_P1_TXT)
draw_task(ax, 77, 5.5, 15, 3, 'P2', C_P2_BG, C_P2_BD, C_P2_TXT)

# ★ 核心亮点：垂直对齐的虚线切面 (同步边界)
sync_points = [60, 75, 77, 92]
for sx in sync_points:
    ax.plot([sx, sx], [4, 16],
            color=C_RED, linestyle='--', lw=2.5, alpha=0.7, zorder=0)

# 强调垂直对齐的箭头与文字
ax.annotate('', xy=(75, 16.5), xytext=(60, 16.5),
            arrowprops=dict(arrowstyle='<|-|>', color=C_RED, lw=2))
ax.text(67.5, 16.8, '严格的时间隔离边界',
        ha='center', va='bottom', fontsize=12,
        color=C_RED, fontweight='bold',
        path_effects=get_stroke(C_RED, 0.5))

# ==================== 渲染与保存 ====================
plt.tight_layout()
output_path = 'partition_comparison.png'
plt.savefig(output_path, dpi=300, bbox_inches='tight', facecolor='white')
print(f"[完成] 串行与并行分区对比图已保存为: {output_path}")
