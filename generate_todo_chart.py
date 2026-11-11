"""
Generates a "Now / Next / Later" Kanban-style board for the remaining project work.
Different chart type from progress_chart.png (which shows the full roadmap) -
this one focuses only on what's left to do, grouped by priority.

Run: /home/aslam/Minor_Project/.venv_plots/bin/python generate_todo_chart.py
Output: /home/aslam/Minor_Project/todo_chart.png
"""

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
import matplotlib.patheffects as pe

# ---- data: matches deck's locked facts (blocker.html / promise.html) ----
columns = [
    ("NOW", "#E0562B", [
        ("Resolve HMAC blocker", "Root-cause the stack/allocator issue, or document a final resolution"),
    ]),
    ("NEXT", "#D9A441", [
        ("Rate & flood detection", "Not started - defends against a bus-flood DoS"),
        ("Tamper-evident logging", "Not started - RPi3 has no RPMB, storage design still open"),
        ("Key provisioning", "Move off the hardcoded prototype key"),
    ]),
    ("LATER", "#4F7CFF", [
        ("Performance measurements", "No latency / throughput / CPU-overhead numbers yet"),
        ("End-to-end hardware demo", "Full pipeline demonstration for final presentation"),
    ]),
]

fig, ax = plt.subplots(figsize=(13, 7.5), dpi=200)
fig.patch.set_facecolor("#12161F")
ax.set_facecolor("#12161F")
ax.set_xlim(0, 3)
ax.set_ylim(0, 10)
ax.axis("off")

col_w = 0.92
gap = 0.12

for ci, (title, color, items) in enumerate(columns):
    x0 = ci + (1 - col_w) / 2
    # column header pill
    header = FancyBboxPatch((x0, 9.15), col_w, 0.55, boxstyle="round,pad=0.02,rounding_size=0.12",
                             facecolor=color, edgecolor="none", zorder=3)
    ax.add_patch(header)
    ax.text(x0 + col_w / 2, 9.15 + 0.27, title, ha="center", va="center", fontsize=15,
            fontweight="bold", color="#12161F", zorder=4)

    # column background track
    track = FancyBboxPatch((x0, 0.3), col_w, 8.65, boxstyle="round,pad=0.02,rounding_size=0.15",
                            facecolor="#1B2130", edgecolor=color, linewidth=1.4, alpha=0.55, zorder=1)
    ax.add_patch(track)

    y = 8.75
    for label, desc in items:
        card_h = 1.55
        y -= card_h + 0.18
        card = FancyBboxPatch((x0 + 0.08, y), col_w - 0.16, card_h, boxstyle="round,pad=0.02,rounding_size=0.10",
                               facecolor="#242B3D", edgecolor=color, linewidth=1.2, zorder=2)
        card.set_path_effects([pe.withSimplePatchShadow(offset=(0, -3), shadow_rgbFace="#000000", alpha=0.4)])
        ax.add_patch(card)
        # priority dot
        ax.scatter([x0 + 0.28], [y + card_h - 0.28], s=90, color=color, zorder=5)
        ax.text(x0 + 0.42, y + card_h - 0.28, label, ha="left", va="center", fontsize=11.5,
                fontweight="bold", color="#F2F2F2", zorder=5)
        ax.text(x0 + 0.2, y + card_h - 0.68, desc, ha="left", va="top", fontsize=9,
                color="#B7BCC8", zorder=5, wrap=True, linespacing=1.35,
                bbox=None)
        # manual wrap since matplotlib wrap is unreliable at this width
        import textwrap
        wrapped = "\n".join(textwrap.wrap(desc, width=34))
        ax.texts[-1].set_text(wrapped)

fig.text(0.045, 0.965, "NEXT STEPS", fontsize=12, fontweight="bold", color="#4FA8FF", family="monospace")
fig.text(0.045, 0.925, "What's Left, By Priority", fontsize=24, fontweight="bold", color="#FFFFFF")
fig.text(0.5, 0.02, "Ordered by what blocks the rest of the pipeline first, not by estimated effort.",
          ha="center", fontsize=10.5, color="#9AA0AE", style="italic")

plt.tight_layout(rect=[0, 0.04, 1, 0.88])
plt.savefig("/home/aslam/Minor_Project/todo_chart.png", facecolor=fig.get_facecolor(), bbox_inches="tight")
print("Saved: /home/aslam/Minor_Project/todo_chart.png")
