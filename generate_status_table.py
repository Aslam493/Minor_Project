"""
Generates the project status table (six-tier vocabulary) as a clean image,
for embedding into the "Project Plan & Progress" slide.

Run: /home/aslam/Minor_Project/.venv_plots/bin/python generate_status_table.py
Output: /home/aslam/Minor_Project/midterm_deck_v2/html/status_table.png
"""

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

rows = [
    ("Boot & Core Pinning",            "HARDWARE-PROVEN", "Core1 excluded from Linux SMP - confirmed via dmesg"),
    ("Secure CAN Ownership",           "HARDWARE-PROVEN", "SPI0 + GPIO25 owned by OP-TEE; CAN interrupt reaches Secure World as FIQ"),
    ("CAN ID Check",                   "HARDWARE-PROVEN", "Single allowed ID 0x123, validated on Core1"),
    ("Replay / Counter Check",         "HARDWARE-PROVEN", "Validated on real captured frames"),
    ("Timestamp Check",                "HARDWARE-PROVEN", "Freshness-continuity tested only; no stale-frame rejection demonstrated"),
    ("HMAC Authentication",            "BLOCKED",          "Unresolved Secure World runtime issue - hypothesis, not a diagnosis"),
    ("Rate & Flood Detection",         "NOT STARTED",      "-"),
    ("Tamper-Evident Logging",         "NOT STARTED",      "RPi3 has no RPMB; storage/integrity design still open"),
    ("Performance Measurements",       "NOT STARTED",      "No latency / throughput / CPU-overhead numbers yet"),
    ("Final Demo & End-to-End",        "NOT STARTED",      "-"),
]

STATUS_COLOR = {
    "HARDWARE-PROVEN": "#1E8E3E",
    "BLOCKED":          "#D9730D",
    "NOT STARTED":      "#8A8F98",
}
STATUS_BG = {
    "HARDWARE-PROVEN": "#E6F4EA",
    "BLOCKED":          "#FDECD9",
    "NOT STARTED":      "#EDEEF0",
}

# two-column layout (5 rows each) so the whole table is wide-and-short, not tall
left_rows = rows[:5]
right_rows = rows[5:]
n = 5
fig, ax = plt.subplots(figsize=(19, 5.6), dpi=170)
fig.patch.set_facecolor("#FFFFFF")
ax.set_xlim(0, 19)
ax.set_ylim(0, n + 1)
ax.axis("off")

def draw_table(rows_subset, x_off, col_w):
    col_x = [x_off + 0.15, x_off + col_w * 0.42, x_off + col_w * 0.68]
    header_y = n + 0.35
    ax.text(col_x[0], header_y, "PIPELINE STAGE", fontsize=11.5, fontweight="bold", color="#0B1F3A")
    ax.text(col_x[1], header_y, "STATUS", fontsize=11.5, fontweight="bold", color="#0B1F3A")
    ax.text(col_x[2], header_y, "EVIDENCE", fontsize=11.5, fontweight="bold", color="#0B1F3A")
    ax.plot([x_off, x_off + col_w - 0.15], [n + 0.05, n + 0.05], color="#0B1F3A", linewidth=1.6)
    for i, (stage, status, evidence) in enumerate(rows_subset):
        y = n - i - 0.5
        if i % 2 == 0:
            ax.add_patch(plt.Rectangle((x_off - 0.05, y - 0.32), col_w - 0.05, 0.64, facecolor="#F7F8FA", edgecolor="none", zorder=0))
        ax.text(col_x[0], y, stage, fontsize=10.6, fontweight="600", color="#141821", va="center", zorder=2)
        badge_w = 1.55
        badge = FancyBboxPatch((col_x[1], y - 0.18), badge_w, 0.36,
                                boxstyle="round,pad=0.02,rounding_size=0.10",
                                facecolor=STATUS_BG[status], edgecolor=STATUS_COLOR[status], linewidth=1.0, zorder=2)
        ax.add_patch(badge)
        ax.text(col_x[1] + badge_w / 2, y, status, fontsize=8.0, fontweight="bold",
                color=STATUS_COLOR[status], ha="center", va="center", zorder=3)
        # wrap evidence text
        import textwrap
        wrapped = "\n".join(textwrap.wrap(evidence, width=26))
        ax.text(col_x[2], y, wrapped, fontsize=8.6, color="#4A4F58", va="center", zorder=2, linespacing=1.2)
    ax.plot([x_off, x_off + col_w - 0.15], [0.05, 0.05], color="#D8DADF", linewidth=1.2)

draw_table(left_rows, 0.1, 9.25)
ax.plot([9.5, 9.5], [0.05, n + 0.35], color="#E3E5EA", linewidth=1.2)
draw_table(right_rows, 9.75, 9.15)

plt.tight_layout(pad=0.4)
plt.savefig("/home/aslam/Minor_Project/midterm_deck_v2/html/status_table.png",
            facecolor=fig.get_facecolor(), bbox_inches="tight", dpi=200)
print("Saved: status_table.png")
