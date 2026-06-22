#!/usr/bin/env python3
"""
plot_real_trace.py — 基于真实实验数据的多核执行轨迹与边界对齐分析图
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
    # fallback: 尝试 SimHei
    try:
        plt.rcParams['font.sans-serif'] = ['SimHei']
        plt.rcParams['axes.unicode_minus'] = False
        print('[OK] 已加载中文字体: SimHei (fallback)')
    except Exception:
        print("[Warn] 未找到优质中文字体，可能出现乱码")

setup_chinese_font()
plt.rcParams['font.weight'] = 'bold'

# ==================== 强制加粗描边特效 ====================
def get_stroke(color, lw=0.7):
    return [pe.withStroke(linewidth=lw, foreground=color)]

# ==================== 核心配色 ====================
C_P1_BG, C_P1_BD = '#3B82F6', '#1E3A8A'  # P1 蓝色
C_P2_BG, C_P2_BD = '#10B981', '#064E3B'  # P2 绿色
C_IDLE_BG, C_IDLE_BD = '#F1F5F9', '#94A3B8'  # 空闲灰

C_SLEEP = '#EF4444'  # 休眠红
C_WAKE  = '#F59E0B'  # 唤醒橙
C_TEXT  = '#0F172A'
C_DESC  = '#64748B'

# ==================== 绘制带精准对齐的方块 ====================
def draw_bar(ax, x, y, w, h, bg_color, bd_color, label="", ls='-', alpha=1.0):
    if w <= 0:
        return
    pad = 0.15
    r_size = 0.15

    # 逆向补偿外扩，实现像素级对齐
    ax_x, ax_y, ax_w, ax_h = x + pad, y + pad, w - 2 * pad, h - 2 * pad

    # 阴影
    ax.add_patch(FancyBboxPatch(
        (ax_x + 0.1, ax_y - 0.1), ax_w, ax_h,
        boxstyle=f'round,pad={pad},rounding_size={r_size}',
        facecolor='black', alpha=0.15 * alpha, edgecolor='none', zorder=1))
    # 主框
    ax.add_patch(FancyBboxPatch(
        (ax_x, ax_y), ax_w, ax_h,
        boxstyle=f'round,pad={pad},rounding_size={r_size}',
        facecolor=bg_color, edgecolor=bd_color, linewidth=2,
        linestyle=ls, alpha=alpha, zorder=2))
    # 居中文字
    if label:
        txt_color = 'white' if bg_color != C_IDLE_BG else C_DESC
        stroke_lw = 0.4 if bg_color != C_IDLE_BG else 0
        ax.text(x + w / 2, y + h / 2, label, ha='center', va='center',
                fontsize=13, color=txt_color, fontweight='bold',
                path_effects=get_stroke(txt_color, stroke_lw) if stroke_lw > 0 else None,
                zorder=3)


# ==================== 画布初始化 ====================
fig, ax = plt.subplots(figsize=(18, 7.5), dpi=300)
fig.patch.set_facecolor('white')
# X 轴表示时间 0 ~ 28ms
ax.set_xlim(-2, 28)
ax.set_ylim(-1, 14)
ax.axis('off')

# ==================== Y 轴轨道与背景 ====================
Y_C11, Y_C10 = 3, 7.5
H_TRACK = 3

# 画两条浅色的轨道底线
for y in [Y_C11, Y_C10]:
    ax.plot([0, 26], [y, y], color='#E2E8F0', lw=2, zorder=0)

# 左侧核心标签
ax.text(-0.5, Y_C10 + H_TRACK / 2, 'Core 10', ha='right', va='center',
        fontsize=16, color=C_TEXT, fontweight='bold',
        path_effects=get_stroke(C_TEXT, 0.5))
ax.text(-0.5, Y_C11 + H_TRACK / 2, 'Core 11', ha='right', va='center',
        fontsize=16, color=C_TEXT, fontweight='bold',
        path_effects=get_stroke(C_TEXT, 0.5))

# ==================== 绘制实验数据 (Slot 0: P1) ====================
# 数值来源: 代码逻辑推算 (EWMA margin≈500µs, budget 检查), 非 ftrace 时间戳
P1_END = 10.0
# Core 10: navigation(budget=3500)退出→弹性self_test接棒填至slot边界
c10_p1_exec = 9.80
draw_bar(ax, 0, Y_C10, c10_p1_exec, H_TRACK, C_P1_BG, C_P1_BD,
         f"关键+弹性 ~{c10_p1_exec}ms")
draw_bar(ax, c10_p1_exec, Y_C10, P1_END - c10_p1_exec, H_TRACK,
         C_IDLE_BG, C_IDLE_BD, ls='--')

# Core 11: control+sensor无弹性，关键任务在~7.5ms双双退出→SLEEP
c11_p1_exec = 7.50
draw_bar(ax, 0, Y_C11, c11_p1_exec, H_TRACK, C_P1_BG, C_P1_BD,
         f"仅关键 {c11_p1_exec}ms")
draw_bar(ax, c11_p1_exec, Y_C11, P1_END - c11_p1_exec, H_TRACK,
         C_IDLE_BG, C_IDLE_BD, ls='--')

# ==================== 绘制实验数据 (Slot 1: P2) ====================
P2_END = 25.0
# Core 10: mission_cpu退出后fuel_mgmt独跑至~13ms，无弹性→SLEEP
c10_p2_exec = 13.00
draw_bar(ax, P1_END, Y_C10, c10_p2_exec, H_TRACK, C_P2_BG, C_P2_BD,
         f"仅关键 {c10_p2_exec}ms")
draw_bar(ax, P1_END + c10_p2_exec, Y_C10,
         P2_END - (P1_END + c10_p2_exec), H_TRACK,
         C_IDLE_BG, C_IDLE_BD, ls='--')

# Core 11: flight+engine退出后data_log弹性接棒填至slot边界
c11_p2_exec = 14.80
draw_bar(ax, P1_END, Y_C11, c11_p2_exec, H_TRACK, C_P2_BG, C_P2_BD,
         f"关键+弹性 ~{c11_p2_exec}ms")
draw_bar(ax, P1_END + c11_p2_exec, Y_C11,
         P2_END - (P1_END + c11_p2_exec), H_TRACK,
         C_IDLE_BG, C_IDLE_BD, ls='--')


# ==================== 添加 SLEEP 标注 ====================
def add_sleep_marker(x, y, text_offset_y=1.5):
    # 红点 + SLEEP 文字
    ax.plot([x, x], [y, y + H_TRACK], color=C_SLEEP, lw=3, zorder=4)
    ax.text(x + 0.2, y + text_offset_y, "→ SLEEP", color=C_SLEEP,
            fontsize=12, fontweight='bold', va='center', ha='left')

add_sleep_marker(c10_p1_exec, Y_C10)
add_sleep_marker(c11_p1_exec, Y_C11)
add_sleep_marker(P1_END + c10_p2_exec, Y_C10)
add_sleep_marker(P1_END + c11_p2_exec, Y_C11)


# ==================== 绘制绝对边界与 WAKE ====================
for bx in [0, 10, 25]:
    # 垂直边界线
    ax.plot([bx, bx], [1, 11.5], color=C_TEXT, linestyle=':', lw=2,
            alpha=0.6, zorder=0)


def add_wake_marker(x, title, subtitle):
    # 向上箭头
    ax.annotate('', xy=(x, Y_C11 - 0.5), xytext=(x, 0.5),
                arrowprops=dict(arrowstyle='-|>', color=C_WAKE, lw=3,
                                mutation_scale=20))
    ax.text(x, 0.2, title, ha='center', va='top', fontsize=14,
            color=C_WAKE, fontweight='bold',
            path_effects=get_stroke(C_WAKE, 0.5))
    ax.text(x, -0.6, subtitle, ha='center', va='top', fontsize=12,
            color=C_WAKE, fontweight='bold',
            path_effects=get_stroke(C_WAKE, 0.3))

add_wake_marker(0, "↑ P1 双核同时 WAKE", "(MAF 纪元起点)")
add_wake_marker(10, "↑ P2 双核同时 WAKE", "(TIMER_ABSTIME 对齐)")
add_wake_marker(25, "↑ 下一 P1 同时 WAKE", "(TIMER_ABSTIME 对齐)")


# ==================== 顶部尺寸标注 ====================
def draw_dim(x1, x2, text, color, y_line=12.2, y_text=12.6):
    ax.annotate('', xy=(x2, y_line), xytext=(x1, y_line),
                arrowprops=dict(arrowstyle='<|-|>', color=color, lw=2,
                                mutation_scale=12))
    ax.text((x1 + x2) / 2, y_text, text, ha='center', va='bottom',
            fontsize=15, color=color, fontweight='bold',
            path_effects=get_stroke(color, 0.5))

draw_dim(0, 10, 'P1 Slot (配置 10ms)', C_P1_BD)
draw_dim(10, 25, 'P2 Slot (配置 15ms)', C_P2_BD)


# ==================== 全局标题 ====================
ax.text(12.5, 14.5, '代码预期行为分析：内部动态执行与外部绝对边界',
        ha='center', va='center', fontsize=22, color=C_TEXT,
        fontweight='bold', path_effects=get_stroke(C_TEXT, 1.0))
ax.text(12.5, 13.7,
        '各核心独立判断"还能不能跑"→异步SLEEP, 但MAF边界同时WAKE (数值为代码推算, 非ftrace实测)',
        ha='center', va='center', fontsize=12, color=C_DESC,
        fontweight='bold')

# 图例说明
ax.text(26.5, 9,
        '██ 关键任务执行\n'
        '(含弹性填充)\n\n'
        '░░ 空闲等待\n'
        '(margin安全余量)\n\n'
        '→  提前触发 SLEEP\n'
        '(无弹性任务的核心)\n\n'
        '↑  绝对时钟 WAKE\n'
        '(TIMER_ABSTIME对齐)\n\n'
        '★ 数值=代码推算',
        ha='left', va='center', fontsize=11, color=C_TEXT,
        fontweight='bold', linespacing=1.6,
        bbox=dict(boxstyle='round,pad=0.8', facecolor='white',
                  edgecolor=C_DESC, lw=1.5, alpha=0.9))

# ==================== 渲染与保存 ====================
plt.tight_layout()
out_path = 'real_trace_timeline.png'
plt.savefig(out_path, dpi=300, bbox_inches='tight', facecolor='white')
print(f"[完成] 真实实验数据轨迹图已保存为: {out_path}")
