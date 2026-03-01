import json
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec

with open("message.json") as f:
    data = json.load(f)

cfg     = data["config"]
results = data["results"]
ts      = data["time_series"]


def unpack(samples):
    x = [s["time_offset_sec"] for s in samples]
    y = [s["value"]           for s in samples]
    return x, y

mem_x,   mem_y   = unpack(ts["memory_samples"])
thr_x,   thr_y   = unpack(ts["thread_count_samples"])
queue_x, queue_y = unpack(ts["queue_depth_samples"])

fig = plt.figure(figsize=(14, 10))
fig.suptitle(
    f"Benchmark: {cfg['experiment_name']}\n"
    f"pool={cfg['pool_type']}  threads={cfg['threads']}  "
    f"matrix={cfg['matrix_size']}×{cfg['matrix_size']}  "
    f"rate={cfg['traffic_rate']:.0f} tasks/s  duration={cfg['duration_sec']:.0f}s",
    fontsize=13, fontweight="bold", y=0.98
)

gs = gridspec.GridSpec(3, 2, figure=fig, hspace=0.45, wspace=0.35)


ax1 = fig.add_subplot(gs[0, :])          # full-width top row
ax1.plot(mem_x, mem_y, color="#2196F3", linewidth=1.2)
ax1.fill_between(mem_x, mem_y, alpha=0.15, color="#2196F3")
ax1.axhline(results["memory_kb"]["mean"], color="red",
            linestyle="--", linewidth=0.9, label=f'mean={results["memory_kb"]["mean"]:.0f} KB')
ax1.set_title("Memory Usage over Time")
ax1.set_xlabel("Time (s)")
ax1.set_ylabel("Memory (KB)")
ax1.legend(fontsize=8)
ax1.grid(True, alpha=0.3)


ax2 = fig.add_subplot(gs[1, 0])
ax2.step(thr_x, thr_y, color="#4CAF50", linewidth=1.2, where="post")
ax2.set_title("Thread Count over Time")
ax2.set_xlabel("Time (s)")
ax2.set_ylabel("Threads")
ax2.set_ylim(0, results["thread_count"]["max"] + 2)
ax2.grid(True, alpha=0.3)


ax3 = fig.add_subplot(gs[1, 1])
ax3.bar(queue_x, queue_y, width=0.15, color="#FF9800", alpha=0.8)
ax3.set_title("Queue Depth over Time")
ax3.set_xlabel("Time (s)")
ax3.set_ylabel("Queue Depth")
ax3.grid(True, alpha=0.3, axis="y")


ax4 = fig.add_subplot(gs[2, 0])
lat = results["latency_ms"]
labels = ["min", "mean", "p50", "p95", "p99", "max"]
values = [lat["min"], lat["mean"], lat["p50"], lat["p95"], lat["p99"], lat["max"]]
colors = ["#81C784", "#4CAF50", "#2196F3", "#FF9800", "#F44336", "#9C27B0"]
bars = ax4.bar(labels, values, color=colors, edgecolor="white", linewidth=0.5)
for bar, val in zip(bars, values):
    ax4.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.001,
             f"{val:.3f}", ha="center", va="bottom", fontsize=8)
ax4.set_title("Latency Distribution (ms)")
ax4.set_ylabel("Latency (ms)")
ax4.grid(True, alpha=0.3, axis="y")


ax5 = fig.add_subplot(gs[2, 1])
ax5.axis("off")
rows = [
    ["Tasks submitted",  f"{results['tasks_submitted']:,}"],
    ["Tasks completed",  f"{results['tasks_completed']:,}"],
    ["Elapsed",          f"{results['elapsed_sec']:.2f} s"],
    ["Throughput",       f"{results['throughput_tasks_per_sec']:.2f} tasks/s"],
    ["Memory (mean)",    f"{results['memory_kb']['mean']:.0f} KB"],
    ["Memory (max)",     f"{results['memory_kb']['max']:,} KB"],
    ["Latency p50",      f"{lat['p50']:.3f} ms"],
    ["Latency p99",      f"{lat['p99']:.3f} ms"],
]
table = ax5.table(
    cellText=rows,
    colLabels=["Metric", "Value"],
    loc="center",
    cellLoc="left",
)
table.auto_set_font_size(False)
table.set_fontsize(9)
table.scale(1, 1.4)
ax5.set_title("Summary", pad=12)

plt.savefig("benchmark_report.png", dpi=150, bbox_inches="tight")
print("Saved → benchmark_report.png")
plt.show()
