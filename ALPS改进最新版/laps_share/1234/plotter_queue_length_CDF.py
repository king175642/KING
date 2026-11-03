#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import re
import numpy as np
import matplotlib.pyplot as plt
from collections import defaultdict
from matplotlib.ticker import FuncFormatter

# ==========================
# 全局字体强制 15
# ==========================
plt.rcParams.update({
    'font.size': 15,
    'axes.labelsize': 15,
    'axes.titlesize': 15,
    'xtick.labelsize': 15,
    'ytick.labelsize': 15,
    'legend.fontsize': 15
})

# ==========================
# 配置
# ==========================
INPUT_DIR = "/file-in-ctr/outputFiles/seleted_data/queue_length"
OUTPUT_DIR = "/file-in-ctr/PNG/QUEUE_length"
os.makedirs(OUTPUT_DIR, exist_ok=True)
FIXED_LEGEND_ORDER = ['ECMP', 'LetFlow', 'PLB', 'LAPS', 'ALPS']
FILENAME_PATTERN = re.compile(
    r'^C00001_'
    r'(?P<topo>[^_]+)_'
    r'RPC_CDF_All-lr-'
    r'0\.95-lb-'
    r'(?P<algo>[^-]+)'
    r'-All_port_avg_queue_length\.txt$'
)

X_CUTOFF = 150  # KB 阈值

name_map = {
    'ecmp': 'ECMP',
    'letflow': 'LetFlow',
    'plb': 'PLB',
    'e2elapsorigin': 'LAPS',
    'e2elapsplus003': 'ALPS'
}

COLOR_MAP = {
    'ecmp': '#1f77b4',
    'letflow': '#ff7f0e',
    'plb': '#9467bd',
    'e2elapsorigin': '#8c564b',
    'e2elapsplus003': '#d62728'
}

ALGO_ORDER = ['ecmp', 'letflow', 'plb', 'e2elapsorigin', 'e2elapsplus003']

# ==========================
# 读取队列文件
# ==========================
def load_queue_file(path):
    values = []
    with open(path, 'r') as f:
        next(f)  # skip header
        for line in f:
            parts = line.strip().split()
            if len(parts) < 3:
                continue
            values.append(float(parts[2]))
    return np.array(values)

# ==========================
# 绘制 CDF（带 marker）
# ==========================
ALGO_MARKERS = {
    'ECMP': 'o',
    'LetFlow': 's',
    'PLB': '^',
    'LAPS': 'D',
    'ALPS': 'X'
}

CDF_MARK_POINTS_MAP = {
    'ECMP': [0.1, 0.3, 0.5, 0.7,0.9],
    'LetFlow': [0.15, 0.35, 0.55, 0.75,0.95],
    'PLB': [0.2, 0.4, 0.6, 0.8,1.0],
    'LAPS': [5, 10, 20,30, 40,50,60,70],
    'ALPS': [ 5,10, 20,30, 40,50,60,70]
}


def plot_cdf_on_ax(ax, topo, algo_data):
    for algo in ALGO_ORDER:
        if algo not in algo_data:
            continue

        data = algo_data[algo]
        sorted_data = np.sort(data) / 1024.0  # 转 KB
        N = len(sorted_data)
        cdf = np.arange(1, N + 1) / N

        algo_name = name_map.get(algo, algo)
        marker_style = ALGO_MARKERS.get(algo_name, None)
        CDF_MARK_POINTS = CDF_MARK_POINTS_MAP.get(algo_name, [0.2, 0.4, 0.6, 0.8])

        # 计算 marker 索引
        if algo_name in ["LAPS", "ALPS"]:
            # 横坐标定位 marker
            target_xs = CDF_MARK_POINTS_MAP.get(algo_name, [])
            marker_indices = []
            for tx in target_xs:
                # 找到距离 tx 最近的横坐标索引
                idx = np.argmin(np.abs(sorted_data - tx))
                marker_indices.append(idx)
        else:
            # 原来的 CDF 百分比方式
            target_ps = CDF_MARK_POINTS_MAP.get(algo_name, [0.2, 0.4, 0.6, 0.8])
            marker_indices = [max(0, min(N - 1, int(np.ceil(p * N) - 1))) for p in target_ps]

        ax.plot(
            sorted_data,
            cdf,
            linewidth=1.2,
            label=algo_name,
            color=COLOR_MAP.get(algo),
            marker=marker_style if marker_style else None,
            markevery=marker_indices if marker_style else None,
            markersize=8,
            markeredgewidth=1.8,
            markerfacecolor='white',
            markeredgecolor=COLOR_MAP.get(algo),
            zorder=5
        )

    ax.set_xlim(-1.4, X_CUTOFF)
    ax.set_ylim(0, 1.02)
    ax.set_ylabel("CDF", fontsize=15)
    ax.set_xlabel(
    f"Average Queue Length (KB)\n",
    fontsize=15,
    fontweight='normal'  # 主标题不加粗
)

# 单独设置子标题加粗
    ax.text(
        0.5, -0.25,  # 调整位置参数
        topo.replace('railOnly', '(b) RailOnly').replace('dragonfly', '(a) DragonFly'),
        transform=ax.transAxes,
        ha='center',
        fontsize=15,
        fontweight='bold'  # 只加粗这部分
    )

    ax.tick_params(axis='both', labelsize=15)
    ax.grid(True, alpha=1.0, linestyle='--', linewidth=0.8, zorder=0)

    # 隐藏 y=0.0
    def y_format(y, pos):
        if abs(y) < 1e-9:
            return ""
        return f"{y:.1f}"

    ax.yaxis.set_major_formatter(FuncFormatter(y_format))

# ==========================
# 主函数
# ==========================
def main():

    groups = defaultdict(dict)

    # 读取所有文件
    for filename in os.listdir(INPUT_DIR):
        match = FILENAME_PATTERN.match(filename)
        if not match:
            continue

        topo = match.group("topo")
        algo = match.group("algo")

        full_path = os.path.join(INPUT_DIR, filename)
        data = load_queue_file(full_path)

        groups[topo][algo] = data

    topo_list = sorted(groups.keys())

    # ===== 输出队列长度统计到控制台 =====
    print("\n=== 队列长度统计 (KB) ===")
    for topo in topo_list:
        print(f"\nTopology: {topo}")
        for algo in ALGO_ORDER:
            if algo not in groups[topo]:
                continue
            data_kb = groups[topo][algo] / 1024.0
            median = np.median(data_kb)
            p99 = np.percentile(data_kb, 99)
            pct_gt_X = np.sum(data_kb > X_CUTOFF) / len(data_kb) * 100
            print(f" {name_map[algo]:<10}: median={median:.2f} KB, P99={p99:.2f} KB, >{X_CUTOFF} KB={pct_gt_X:.1f}%")

    # ===== 原绘图逻辑保持不变 =====
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))

    for i, topo in enumerate(topo_list):
        plot_cdf_on_ax(axes[i], topo, groups[topo])

    

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
        bbox_to_anchor=(0.527, 1.03),
        framealpha=1.0,           # 背景完全不透明
        facecolor='white',        # 白色背景
        #columnspacing=1.9,
        edgecolor='black'      # 黑色边框
    )

    plt.tight_layout(rect=[0, 0, 1, 0.95])

    outfile = os.path.join(
        OUTPUT_DIR,
        "QUEUE_length_RPC_CDF_combined_horizontal.pdf"
    )

    plt.savefig(outfile, dpi=300,bbox_inches="tight")
    plt.close()

    print("\n✅ 最终版已生成:")
    print(outfile)


if __name__ == "__main__":
    main()




# #!/usr/bin/env python3
# # -*- coding: utf-8 -*-

# import os
# import re
# import numpy as np
# import matplotlib.pyplot as plt
# from collections import defaultdict

# # ==========================
# # 配置
# # ==========================

# INPUT_DIR = "/file-in-ctr/outputFiles/C00001/"
# OUTPUT_DIR = "/file-in-ctr/PNG/QUEUE_length"
# os.makedirs(OUTPUT_DIR, exist_ok=True)

# FILENAME_PATTERN = re.compile(
#     r'^C00001_'
#     r'(?P<topo>[^_]+)_'
#     r'(?P<workload>.+?)_All-lr-'
#     r'0\.95-lb-'
#     r'(?P<algo>[^-]+)'
#     r'-All_port_avg_queue_length\.txt$'
# )

# # ==========================
# # 算法显示名
# # ==========================

# name_map = {
#     'ecmp': 'ECMP',
#     'letflow': 'LetFlow',
#     'plb': 'PLB',
#     'e2elapsorigin': 'LAPS',
#     'e2elapsplus003': 'ALPS'
# }

# # ==========================
# # 固定颜色
# # ==========================

# COLOR_MAP = {
#     'ecmp': '#1f77b4',
#     'letflow': '#ff7f0e',
#     'plb': '#9467bd',
#     'e2elapsorigin': '#8c564b',
#     'e2elapsplus003': '#d62728'
# }

# ALGO_ORDER = ['ecmp', 'letflow', 'plb', 'e2elapsorigin', 'e2elapsplus003']


# # ==========================
# # 读取文件
# # ==========================

# def load_queue_file(path):
#     values = []

#     with open(path, 'r') as f:
#         next(f)
#         for line in f:
#             parts = line.strip().split()
#             if len(parts) < 3:
#                 continue
#             values.append(float(parts[2]))

#     return np.array(values)


# # ==========================
# # 绘制CDF
# # ==========================

# def plot_cdf(group_key, algo_data):

#     topo, workload = group_key

#     print(f"\n开始绘图: topo={topo}, workload={workload}")
#     print("包含算法:", list(algo_data.keys()))

#     plt.figure(figsize=(7, 5))

#     for algo in ALGO_ORDER:
#         if algo not in algo_data:
#             continue

#         data = algo_data[algo]

#         print(f"  算法 {algo} 数据量: {len(data)}")

#         # Byte → KB
#         sorted_data = np.sort(data) / 1024.0
#         cdf = np.arange(1, len(sorted_data) + 1) / len(sorted_data)

#         plt.plot(
#             sorted_data,
#             cdf,
#             linewidth=2,
#             label=name_map.get(algo, algo),
#             color=COLOR_MAP.get(algo)
#         )

#     plt.xlabel("Average Queue Length (KB)")
#     plt.ylabel("CDF")
#     plt.title(f"{topo} | {workload} | lr=0.95")
#     plt.grid(alpha=0.3)
#     plt.legend()

#     outfile = os.path.join(
#         OUTPUT_DIR,
#         f"QUEUE_length_{topo}_{workload}_lr0.95.png"
#     )

#     plt.tight_layout()
#     plt.savefig(outfile, dpi=300)
#     plt.close()

#     print(f"图已保存: {outfile}")


# # ==========================
# # 主函数
# # ==========================

# def main():

#     groups = defaultdict(dict)

#     # 文件扫描（不打印匹配调试信息）
#     for filename in os.listdir(INPUT_DIR):

#         match = FILENAME_PATTERN.match(filename)
#         if not match:
#             continue

#         topo = match.group("topo")
#         workload = match.group("workload")
#         algo = match.group("algo")

#         full_path = os.path.join(INPUT_DIR, filename)
#         data = load_queue_file(full_path)

#         groups[(topo, workload)][algo] = data

#     # ===== 打印最终分组情况 =====
#     print("\n最终分组情况:")
#     for key in groups:
#         print(f"{key} 包含算法: {list(groups[key].keys())}")

#     # ===== 开始绘图 =====
#     for key, algo_data in groups.items():
#         plot_cdf(key, algo_data)


# if __name__ == "__main__":
#     main()
