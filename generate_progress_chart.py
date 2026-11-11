"""
Generates the "Where We Stand" project-status roadmap graphic for the midterm slide deck.

Node colors carry the meaning (green = done/proven, orange = not done yet / stuck,
grey = not started); status is shown as an icon, not a text label like "NOT STARTED".

Run: /home/aslam/Minor_Project/.venv_plots/bin/python generate_progress_chart.py
Output: /home/aslam/Minor_Project/progress_chart.png
"""

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patheffects as pe
from matplotlib.patches import FancyBboxPatch, Circle
from matplotlib.lines import Line2D
import numpy as np

# ---- data ----
# kind: "done" (hardware-proven), "stuck" (HMAC - not done), "todo" (not started)
nodes = [
    ("Boot &\nCore Pinning",             "done"),
    ("Secure CAN\nOwnership",            "done"),
    ("ID / Replay /\nTimestamp Checks",  "done"),
    ("HMAC\nAuthentication",             "stuck"),
    ("Rate & Flood\nDetection",          "todo"),
    ("Tamper-Evident\nLogging",          "todo"),
    ("Final Demo &\nMeasurements",       "todo"),
]

COLORS = {
    "done":  "#0CA30C",
    "stuck": "#E0562B",
    "todo":  "#D8D6CC",
}
ICON = {"done": "✓", "stuck": "●", "todo": ""}

n = len(nodes)
xs = np.arange(n)
amp = 0.55
ys = amp * np.sin(xs * np.pi / 2.4 + 0.4)

fig, ax = plt.subplots(figsize=(15, 6), dpi=200)
fig.patch.set_facecolor("#12161F")
ax.set_facecolor("#12161F")

# faint background grid dots for texture
gx, gy = np.meshgrid(np.linspace(-0.8, n - 0.2, 40), np.linspace(-1.6, 1.6, 14))
ax.scatter(gx, gy, s=1.2, color="#232838", zorder=0)

# smooth connecting path
t = np.linspace(0, n - 1, 400)
y_smooth = amp * np.sin(t * np.pi / 2.4 + 0.4)
for i in range(len(t) - 1):
    seg_kind = nodes[min(int(t[i]), n - 1)][1]
    c = "#3A4150" if seg_kind == "todo" else ("#0CA30C" if seg_kind == "done" else "#E0562B")
    ax.plot(t[i:i+2], y_smooth[i:i+2], color=c, linewidth=4, solid_capstyle="round", alpha=0.85, zorder=1)

# nodes
r = 0.30
for i, (label, kind) in enumerate(nodes):
    x, y = xs[i], ys[i]
    color = COLORS[kind]
    glow = Circle((x, y), r + 0.09, facecolor=color, alpha=0.18, zorder=2)
    ax.add_patch(glow)
    circ = Circle((x, y), r, facecolor=color, edgecolor="#12161F", linewidth=3, zorder=3)
    circ.set_path_effects([pe.withSimplePatchShadow(offset=(0, -2), shadow_rgbFace="#000000", alpha=0.35)])
    ax.add_patch(circ)
    ax.text(x, y, ICON[kind], ha="center", va="center", fontsize=15 if kind != "stuck" else 11,
            fontweight="bold", color="#12161F" if kind == "todo" else "white", zorder=4)

    label_y = y + (0.62 if y < 0 else -0.62)
    va = "bottom" if y < 0 else "top"
    ax.text(x, label_y, label, ha="center", va=va, fontsize=11.5, fontweight="bold",
            color="#F2F2F2", zorder=4, linespacing=1.3)

ax.set_xlim(-0.7, n - 0.3)
ax.set_ylim(-1.7, 1.7)
ax.axis("off")

fig.text(0.045, 0.93, "PLAN, AND WHERE WE ARE IN IT", fontsize=12, fontweight="bold",
          color="#4FA8FF", family="monospace")
fig.text(0.045, 0.86, "Where We Stand, and What's Next", fontsize=25, fontweight="bold", color="#FFFFFF")

legend_elems = [
    Line2D([0], [0], marker='o', color='w', markerfacecolor=COLORS["done"], markersize=13, label='Hardware-proven'),
    Line2D([0], [0], marker='o', color='w', markerfacecolor=COLORS["stuck"], markersize=13, label='Not done yet'),
    Line2D([0], [0], marker='o', color='w', markerfacecolor=COLORS["todo"], markersize=13, label='Not started'),
]
leg = ax.legend(handles=legend_elems, loc="lower center", bbox_to_anchor=(0.5, -0.18), ncol=3,
                 frameon=False, fontsize=11, labelcolor="#D8D6CC")

fig.text(0.5, 0.035, "Three milestones hardware-proven, one not yet done, three not started.",
          ha="center", fontsize=12, color="#9AA0AE", style="italic")

plt.tight_layout(rect=[0, 0.06, 1, 0.83])
plt.savefig("/home/aslam/Minor_Project/progress_chart.png", facecolor=fig.get_facecolor(), bbox_inches="tight")
print("Saved: /home/aslam/Minor_Project/progress_chart.png")
