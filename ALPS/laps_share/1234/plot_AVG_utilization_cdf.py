import os
import re
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

# ================= 配置 =================
INPUT_DIR = "/file-in-ctr/outputFiles/seleted_data/AVG_utilization_CDF/"
OUTPUT_PREFIX = "/file-in-ctr/PNG/Topology_Compare"
FIXED_LEGEND_ORDER = ['ECMP', 'LetFlow', 'PLB', 'LAPS', 'ALPS']
NODE_ID_THRESHOLD = 180
PORT_THRESHOLD = 11

# ================= 新增统计参数 =================
K_PERCENT = 0.40

# ================= 新增：算法 marker =================
ALGO_MARKERS = {
    'ECMP': 'o',
    'LetFlow': 's',
    'PLB': '^',
    'LAPS': 'D',
    'ALPS': 'X'
}

# 需要标记的CDF点
CDF_MARK_POINTS = [0.2, 0.4, 0.6, 0.8]

name_map = {
    'ecmp': 'ECMP',
    'letflow': 'LetFlow',
    'conga': 'CONGA',
    'plb': 'PLB',
    'e2elapsorigin': 'LAPS',
    'e2elapsplus000': 'DEPS 000',
    'e2elapsplus001': 'DEPS 001',
    'e2elapsplus002': 'ALPS',
    'e2elapsplus003': 'ALPS',
    'e2elapsplus004': 'DEPS 004',
}

COLOR_MAP = {
    'ecmp': '#1f77b4',
    'letflow': '#ff7f0e',
    'conga': '#2ca02c',
    'plb': '#9467bd',
    'e2elapsorigin': '#8c564b',
    'e2elapsplus000': '#8c564b',
    'e2elapsplus001': '#e377c2',
    'e2elapsplus002': '#d62728',
    'e2elapsplus003': '#d62728',
    'e2elapsplus004': '#17becf',
}


# ================= 数据读取 =================
def load_topology_data(topology_name):

    pattern = re.compile(
        rf'^C00001_{topology_name}_RPC_CDF_All-lr-1\.0-lb-'
        rf'(?P<algo>[^-]+)'
        rf'-AVGPortUtilization\.txt$'
    )

    algo_data = {}

    for fname in os.listdir(INPUT_DIR):
        match = pattern.match(fname)
        if not match:
            continue

        algo_key = match.group("algo")
        if algo_key not in name_map:
            continue

        file_path = os.path.join(INPUT_DIR, fname)
        utilizations = []

        with open(file_path, "r") as f:
            for line in f:
                parts = line.strip().split()
                if len(parts) < 3:
                    continue

                try:
                    node_id = int(parts[0])
                    port = int(parts[1])
                    util = float(parts[2])

                    if node_id >= NODE_ID_THRESHOLD:
                        continue
                    if port >= PORT_THRESHOLD:
                        continue

                    if 0.0 <= util <= 1.1:
                        utilizations.append(util)
                except:
                    continue

        if utilizations:
            algo_data[algo_key] = sorted(utilizations)

    return algo_data


# ================= 利用率统计 =================
def print_top_k_utilization(algo_data, topology_name):

    print(f"\n===== {topology_name} Top {int(K_PERCENT*100)}% Port Utilization Threshold =====")

    for algo_key in sorted(algo_data.keys()):
        data = algo_data[algo_key]
        if not data:
            continue

        threshold = np.percentile(
            data,
            (1 - K_PERCENT) * 100,
            method="linear"
        )

        print(f"{name_map[algo_key]:<10} : {threshold:.6f}")


# ================= CDF 绘图 =================
def plot_cdf(ax, algo_data):

    for algo_key in sorted(algo_data.keys()):
        data = algo_data[algo_key]
        N = len(data)
        cdf_y = [(i + 1) / N for i in range(N)]

        algo_name = name_map[algo_key]
        marker_style = ALGO_MARKERS.get(algo_name, None)

        # 根据不同算法设置不同的CDF标记点
        if algo_name == "ECMP":
            CDF_MARK_POINTS = [0.1,0.3, 0.5, 0.7, 0.9]
        elif algo_name == "LetFlow": 
            CDF_MARK_POINTS = [0.2, 0.4, 0.6, 0.8,0.97]
        else:    
            CDF_MARK_POINTS = [0.2, 0.4, 0.6, 0.8,0.3, 0.5, 0.7, 0.9,0.95]


        # 计算需要打 marker 的 index
        marker_indices = []
        for target in CDF_MARK_POINTS:
            idx = int(np.ceil(target * N)) - 1
            idx = max(0, min(idx, N - 1))
            marker_indices.append(idx)

        ax.plot(
    data,
    cdf_y,
    linewidth=1.2,
    color=COLOR_MAP.get(algo_key),
    label=algo_name,  # legend
    marker=marker_style if marker_style else None,
    markevery=marker_indices if marker_style else None,
    markersize=8,
    markeredgewidth=1.8,
    markerfacecolor='white',  # ← 内部填充白色
    markeredgecolor=COLOR_MAP.get(algo_key),
    zorder=5  # ← 提升到最上层
)

    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)

    ticks = [i / 5 for i in range(6)]
    ax.set_xticks(ticks)
    ax.set_yticks(ticks)

    def x_format(x, pos):
        if abs(x) < 1e-9:
            return "0"
        if abs(x - 1) < 1e-9:
            return "1"
        return f"{x:.1f}"

    def y_format(y, pos):
        if abs(y) < 1e-9:
            return ""
        if abs(y - 1) < 1e-9:
            return "1"
        return f"{y:.1f}"

    ax.xaxis.set_major_formatter(FuncFormatter(x_format))
    ax.yaxis.set_major_formatter(FuncFormatter(y_format))
    
    ax.tick_params(labelsize=15)
    ax.set_xlabel("Port Bandwidth Utilization", fontsize=15)
    ax.set_ylabel("CDF", fontsize=15)
    ax.set_ylim(0, 1.02)
    ax.grid(True, alpha=1.0, linestyle='--', linewidth=0.8, zorder=0)


# ================= 主程序 =================
def main():

    dragonfly_data = load_topology_data("dragonfly")
    rail_data = load_topology_data("railOnly")

    if not dragonfly_data or not rail_data:
        print("Missing data.")
        return

    print_top_k_utilization(dragonfly_data, "DragonFly")
    print_top_k_utilization(rail_data, "RailOnly")

    fig, axes = plt.subplots(1, 2, figsize=(11, 5))

    plot_cdf(axes[0], dragonfly_data)
    plot_cdf(axes[1], rail_data)

    axes[1].tick_params(labelleft=True)

    axes[0].text(
        0.5, -0.25,
        "(a) DragonFly",
        transform=axes[0].transAxes,
        ha='center',
        fontsize=15,
        fontweight='bold'  # 加粗
    )

    axes[1].text(
        0.5, -0.25,
        "(b) RailOnly",
        transform=axes[1].transAxes,
        ha='center',
        fontsize=15,
        fontweight='bold'  # 加粗
    )

     # 取出当前轴上的 handles 和 labels
    handles, labels = axes[0].get_legend_handles_labels()

    # 创建字典方便映射
    label_to_handle = dict(zip(labels, handles))

    # 按固定顺序构建新的 handles 和 labels
    fixed_handles = []
    fixed_labels = []
    for key in FIXED_LEGEND_ORDER:
        if key in label_to_handle:
            fixed_handles.append(label_to_handle[key])
            fixed_labels.append(key)
            
    fig.legend(
        fixed_handles,
        fixed_labels,
        loc='upper center',
        ncol=6,
        frameon=True,
        fontsize=15,
        bbox_to_anchor=(0.53, 1.022),
        framealpha=1.0,           # 背景完全不透明
        facecolor='white',        # 白色背景
        #columnspacing=1.9,
        edgecolor='black'      # 黑色边框
    )

    plt.tight_layout(rect=[0, 0, 1, 0.93])

    output_path = f"{OUTPUT_PREFIX}_AVG_utilization_CDF_compare.pdf"
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    plt.savefig(output_path, dpi=300,bbox_inches='tight')
    plt.close()

    print("\n✅ Plot saved:")
    print(f"  → {output_path}")


if __name__ == "__main__":
    main()






# import os 
# import re
# import numpy as np
# import matplotlib.pyplot as plt
# from matplotlib.ticker import FuncFormatter

# # ================= 配置 =================
# INPUT_DIR = "/file-in-ctr/outputFiles/seleted_data/AVG_utilization_CDF/"
# OUTPUT_PREFIX = "/file-in-ctr/PNG/Topology_Compare"

# NODE_ID_THRESHOLD = 180
# PORT_THRESHOLD = 11

# # ================= 新增统计参数 =================
# K_PERCENT = 0.40   # 表示统计“前10%端口利用率大于多少”

# name_map = {
#     'ecmp': 'ECMP',
#     'letflow': 'LetFlow',
#     'conga': 'CONGA',
#     'plb': 'PLB',
#     'e2elapsorigin': 'LAPS',
#     'e2elapsplus000': 'DEPS 000',
#     'e2elapsplus001': 'DEPS 001',
#     'e2elapsplus002': 'ALPS',
#     'e2elapsplus003': 'ALPS',
#     'e2elapsplus004': 'DEPS 004',
# }

# COLOR_MAP = {
#     'ecmp': '#1f77b4',
#     'letflow': '#ff7f0e',
#     'conga': '#2ca02c',
#     'plb': '#9467bd',
#     'e2elapsorigin': '#8c564b',
#     'e2elapsplus000': '#8c564b',
#     'e2elapsplus001': '#e377c2',
#     'e2elapsplus002': '#d62728',
#     'e2elapsplus003': '#d62728',
#     'e2elapsplus004': '#17becf',
# }


# # ================= 数据读取 =================
# def load_topology_data(topology_name):

#     pattern = re.compile(
#         rf'^C00001_{topology_name}_RPC_CDF_All-lr-1\.0-lb-'
#         rf'(?P<algo>[^-]+)'
#         rf'-AVGPortUtilization\.txt$'
#     )

#     algo_data = {}

#     for fname in os.listdir(INPUT_DIR):
#         match = pattern.match(fname)
#         if not match:
#             continue

#         algo_key = match.group("algo")
#         if algo_key not in name_map:
#             continue

#         file_path = os.path.join(INPUT_DIR, fname)
#         utilizations = []

#         with open(file_path, "r") as f:
#             for line in f:
#                 parts = line.strip().split()
#                 if len(parts) < 3:
#                     continue

#                 try:
#                     node_id = int(parts[0])
#                     port = int(parts[1])
#                     util = float(parts[2])

#                     if node_id >= NODE_ID_THRESHOLD:
#                         continue
#                     if port >= PORT_THRESHOLD:
#                         continue

#                     if 0.0 <= util <= 1.1:
#                         utilizations.append(util)
#                 except:
#                     continue

#         if utilizations:
#             algo_data[algo_key] = sorted(utilizations)

#     return algo_data


# # ================= 新增：利用率统计功能（精确分位数） =================
# def print_top_k_utilization(algo_data, topology_name):

#     print(f"\n===== {topology_name} Top {int(K_PERCENT*100)}% Port Utilization Threshold =====")

#     for algo_key in sorted(algo_data.keys()):
#         data = algo_data[algo_key]
#         if not data:
#             continue

#         threshold = np.percentile(
#             data,
#             (1 - K_PERCENT) * 100,
#             method="linear"
#         )

#         print(f"{name_map[algo_key]:<10} : {threshold:.6f}")


# # ================= CDF 绘图 =================
# def plot_cdf(ax, algo_data):

#     for algo_key in sorted(algo_data.keys()):
#         data = algo_data[algo_key]
#         N = len(data)
#         cdf_y = [(i + 1) / N for i in range(N)]

#         ax.plot(
#             data,
#             cdf_y,
#             linewidth=2.2,
#             color=COLOR_MAP.get(algo_key),
#             label=name_map[algo_key]
#         )

#     ax.set_xlim(0, 1)
#     ax.set_ylim(0, 1)

#     ticks = [i / 5 for i in range(6)]
#     ax.set_xticks(ticks)
#     ax.set_yticks(ticks)

#     def x_format(x, pos):
#         if abs(x) < 1e-9:
#             return "0"
#         if abs(x - 1) < 1e-9:
#             return "1"
#         return f"{x:.1f}"

#     def y_format(y, pos):
#         if abs(y) < 1e-9:
#             return ""
#         if abs(y - 1) < 1e-9:
#             return "1"
#         return f"{y:.1f}"

#     ax.xaxis.set_major_formatter(FuncFormatter(x_format))
#     ax.yaxis.set_major_formatter(FuncFormatter(y_format))

#     ax.tick_params(labelsize=15)
#     ax.set_xlabel("Port Bandwidth Utilization", fontsize=15)
#     ax.set_ylabel("CDF", fontsize=15)

#     ax.grid(True, linestyle='--', alpha=0.6)


# # ================= 主程序 =================
# def main():

#     dragonfly_data = load_topology_data("dragonfly")
#     rail_data = load_topology_data("railOnly")

#     if not dragonfly_data or not rail_data:
#         print("Missing data.")
#         return

#     # ===== 新增统计输出 =====
#     print_top_k_utilization(dragonfly_data, "Dragonfly")
#     print_top_k_utilization(rail_data, "Rail-Only")

#     fig, axes = plt.subplots(1, 2, figsize=(11, 5))

#     plot_cdf(axes[0], dragonfly_data)
#     plot_cdf(axes[1], rail_data)

#     axes[1].tick_params(labelleft=True)

#     axes[0].text(
#         0.5, -0.28,
#         "(a) Dragonfly",
#         transform=axes[0].transAxes,
#         ha='center',
#         fontsize=15
#     )

#     axes[1].text(
#         0.5, -0.28,
#         "(b) Rail-Only",
#         transform=axes[1].transAxes,
#         ha='center',
#         fontsize=15
#     )

#     handles, labels = axes[0].get_legend_handles_labels()
#     fig.legend(
#         handles,
#         labels,
#         loc='upper center',
#         bbox_to_anchor=(0.5, 1.0),
#         ncol=6,
#         fontsize=10,
#         frameon=True
#     )

#     plt.tight_layout(rect=[0, 0, 1, 0.93])

#     output_path = f"{OUTPUT_PREFIX}_AVG_utilization_CDF_compare.pdf"
#     os.makedirs(os.path.dirname(output_path), exist_ok=True)

#     plt.savefig(output_path, dpi=300)
#     plt.close()

#     print("\n✅ Plot saved:")
#     print(f"  → {output_path}")


# if __name__ == "__main__":
#     main()