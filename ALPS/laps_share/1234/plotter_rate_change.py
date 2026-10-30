#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import re
import matplotlib.pyplot as plt
import numpy as np
# ================= 全局字体 =================
plt.rcParams.update({
    "font.size": 15,
    "axes.titlesize": 15,
    "axes.labelsize": 15,
    "xtick.labelsize": 15,
    "ytick.labelsize": 15,
    "legend.fontsize": 15
})

# ================= 基本配置 =================
#INPUT_DIR = "/file-in-ctr/outputFiles/C00001-2.13/"
INPUT_DIR = "/file-in-ctr/outputFiles/C00001/"
OUTPUT_DIR = "/file-in-ctr/PNG/RateChange/"
os.makedirs(OUTPUT_DIR, exist_ok=True)
FIXED_LEGEND_ORDER = ['ECMP', 'LetFlow', 'PLB', 'LAPS', 'ALPS']
WORKLOADS = ["DCTCP_CDF", "RPC_CDF"]
TOPOLOGIES = ["dragonfly", "railOnly"]

name_map = {
    'ecmp': 'ECMP',
    'letflow': 'LetFlow',
    'conga': 'CONGA',
    'plb': 'PLB',
    'e2elapsorigin': 'LAPS',
    'e2elapsplus000': 'DEPS 000',
    'e2elapsplus001': 'DEPS 001',
    'e2elapsplus002': 'ALPS',
    'e2elapsplus003': 'DEPS 003',
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
    'e2elapsplus003': '#bcbd22',
    'e2elapsplus004': '#17becf',
}

# ================= 正则匹配 =================
FILENAME_PATTERN = re.compile(
    r'^C00001_'
    r'(?P<topo>dragonfly|railOnly)_'
    r'(?P<workload>DCTCP_CDF|RPC_CDF)_'
    r'All-lr-1\.0+-lb-'
    r'(?P<algo>[^-]+)-'
    r'Rate'
)

# ================= 数据解析 =================
def parse_line(line):
    pattern = r'\[\s*([\d\.]+)\s*,\s*([\d\.]+)\s*,\s*[\d\.]+\s*\]'
    matches = re.findall(pattern, line)
    return [(float(rate), float(time)) for rate, time in matches]

def count_rate_changes(seq):
    return sum(1 for i in range(1, len(seq)) if seq[i][0] != seq[i-1][0])

# ================= 加载数据 =================
def load_data():
    data = {}
    for fname in os.listdir(INPUT_DIR):
        match = FILENAME_PATTERN.match(fname)
        if not match:
            continue
        workload = match.group("workload")
        topo = match.group("topo")
        algo = match.group("algo")
        if algo not in name_map:
            continue
        file_path = os.path.join(INPUT_DIR, fname)
        rate_change_counts = []
        with open(file_path, "r") as f:
            for line in f:
                rt = parse_line(line)
                rt.sort(key=lambda x: x[1])
                rate_change_counts.append(count_rate_changes(rt))
        if not rate_change_counts:
            continue
        data.setdefault(workload, {})
        data[workload].setdefault(topo, {})
        data[workload][topo][algo] = sorted(rate_change_counts)
        print(f"Loaded {workload:<10} | {topo:<10} | {name_map[algo]}")
    return data

# ================= 绘图 =================
ALGO_MARKERS = {
     'ECMP': 'o',
    'LetFlow': 's',
    'PLB': '^',
    'LAPS': 'D',
    'ALPS': 'X'
}

def plot_combined(workload, workload_data):
    fig, axes = plt.subplots(1, 2, figsize=(11, 4), sharey=True)
        # 确保输出目录存在
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    for idx, topo in enumerate(TOPOLOGIES):
        ax = axes[idx]
        if topo not in workload_data:
            continue
        for algo_key, data in workload_data[topo].items():
            N = len(data)
            # 截取最大不超过200的值
            cutoff_idx = next((i for i, val in enumerate(data) if val > 200), N)
            data_left = data[:cutoff_idx]
            cdf_y = [(i + 1) / N for i in range(len(data_left))]

            # ===== 新增 marker 设置 =====
            algo_name = name_map[algo_key]
            marker_style = ALGO_MARKERS.get(algo_name, 'o')

            # 设置要打 marker 的横坐标值
            if algo_name in ["ECMP", "ALPS"]:
                MARK_X_VALUES = [5,20, 40, 60, 80]   # 横坐标值，可根据需要调整
            else:
                MARK_X_VALUES = [10,30, 50, 70,90]

            # 根据横坐标找到最接近的 index
            marker_indices = []
            for x_val in MARK_X_VALUES:
                idx_marker = min(range(len(data_left)), key=lambda i: abs(data_left[i]-x_val))
                marker_indices.append(idx_marker)

            ax.plot(
                data_left,
                cdf_y,
                linewidth=1.2,
                label=algo_name,
                color=COLOR_MAP.get(algo_key, 'black'),
                marker=marker_style,
                markersize=8,
                markeredgewidth=1.8,
                markerfacecolor='white',  # 内部白色
                markeredgecolor=COLOR_MAP.get(algo_key, 'black'),
                markevery=marker_indices,
                zorder=5  # 显示在最上层
            )

        # 坐标轴原点对齐
        ax.set_xlim(0, 100)
       # ax.set_ylim(0.4, 1.05) 
        ax.spines['left'].set_position(('data', 0))
        #ax.spines['bottom'].set_position(('data', 0))
        ax.spines['right'].set_visible(False)
        ax.spines['top'].set_visible(False)
        # 纵坐标刻度稀疏显示
        ylim_min = 0.5
        ylim_max = 1.02
        ax.set_ylim(ylim_min, ylim_max)
    
        # 纵坐标刻度设置
        yticks = [0.4, 0.6, 0.8, 1.0]
        ytick_labels = [f"{y:.1f}" for y in yticks]
        ax.set_yticks(yticks)
        ax.set_yticklabels(ytick_labels)
        ax.tick_params(axis='y', labelleft=True)
        ax.set_xlabel("Number of Rate Changes per Flow")
        ax.set_ylabel("CDF")  # 左右图都显示y轴标签
        ax.grid(True, alpha=1.0, linestyle='--', linewidth=0.8, zorder=0)
    # ===== 合并两图 legend，去重，保证右图曲线文字显示 =====
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
        loc="upper center",
        ncol=6,
        frameon=True,
        bbox_to_anchor=(0.52, 1.05),
        framealpha=1.0,           # 背景完全不透明
        facecolor='white',        # 白色背景
        #columnspacing=1.9,
        edgecolor='black'      # 黑色边框
    )
    # ===== 拓扑标签上移，添加 (a)/(b) =====
    fig.text(0.285, 0.04, "(a) DragonFly", ha='center', fontsize=15,fontweight='bold')
    fig.text(0.77, 0.04, "(b) RailOnly", ha='center', fontsize=15,fontweight='bold')
    plt.tight_layout(rect=[0, 0.05, 1, 0.95])
    outfile = os.path.join(
        OUTPUT_DIR, f"{workload}_CDF_rate_changed_combined.pdf"
    )
    plt.savefig(outfile, dpi=300, bbox_inches="tight")
    plt.close()
    print(f"Saved: {outfile}")
# ================= 主程序 =================
if __name__ == "__main__":
    all_data = load_data()

    # ===== 统计平均速率变化次数 =====
    print("\n=== 平均速率变化次数统计 ===")
    for workload in WORKLOADS:
        if workload not in all_data:
            continue
        print(f"\nWorkload: {workload}")
        for topo in TOPOLOGIES:
            if topo not in all_data[workload]:
                continue
            print(f" Topology: {topo}")
            
            # 设置阈值
            K = 10   # 小于K的速率变化次数阈值
            M = 50  # 大于M的速率变化次数阈值
            
            for algo_key, data in all_data[workload][topo].items():
                avg_rate_changes = sum(data) / len(data)
                print(f" {name_map[algo_key]:<10}: AVG {avg_rate_changes:.2f} rate changes per flow")
                
                # ===== 新增：统计 <K 和 >M 的比例 =====
                count_lt_K = sum(1 for v in data if v < K)
                count_gt_M = sum(1 for v in data if v > M)
                total = len(data)
                pct_lt_K = count_lt_K / total * 100
                pct_gt_M = count_gt_M / total * 100
                #print(f"   <{K}: {pct_lt_K:.1f}%  ; >{M}: {pct_gt_M:.1f}%")

    # ===== 绘图 =====
    for workload in WORKLOADS:
        if workload in all_data:
            plot_combined(workload, all_data[workload])

    print("\n所选任务执行完毕！")











# import os
# import re
# import matplotlib.pyplot as plt
# from collections import Counter

# # ================= 配置 =================
# INPUT_DIR = "/file-in-ctr/outputFiles/C00001/"
# OUTPUT_PREFIX = "/file-in-ctr/PNG/"

# # 算法名 → 图例名
# name_map = {
#     'ecmp': 'ECMP',
#     'letflow': 'LetFlow',
#     'conga': 'CONGA',
#     'plb': 'PLB',
#     'e2elapsorigin': 'LAPS',
#     'e2elapsplus000': 'DEPS 000',
#     'e2elapsplus001': 'DEPS 001',
#     'e2elapsplus002': 'ALPS',
#     'e2elapsplus003': 'DEPS 003',
#     'e2elapsplus004': 'DEPS 004',
# }

# # 🔒 固定颜色映射（按算法 key）
# COLOR_MAP = {
#     'ecmp': '#1f77b4',        # 蓝色
#     'letflow': '#ff7f0e',     # 橙色
#     'conga': '#2ca02c',       # 绿色
#     'plb': '#9467bd',         # 红色
#     'e2elapsorigin': '#8c564b',  # 紫色
#     'e2elapsplus000': '#8c564b', # 棕色
#     'e2elapsplus001': '#e377c2', # 粉色
#     'e2elapsplus002': '#d62728', # 灰色（或可改为青色 '#17becf'）
#     'e2elapsplus003': '#bcbd22', # 橄榄绿
#     'e2elapsplus004': '#17becf', # 青色
# }

# # 🔒 锁定的文件名正则（只允许算法变化）
# FILENAME_PATTERN = re.compile(
#     r'^C00001_dragonfly_RPC_CDF_All-lr-1\.0-lb-'
#     r'(?P<algo>[^-]+)'
#     r'-Rate\.txt$'
# )

# # ================= 解析与统计 =================
# def parse_line(line):
#     pattern = r'\[\s*([\d\.]+)\s*,\s*([\d\.]+)\s*,\s*[\d\.]+\s*\]'
#     matches = re.findall(pattern, line)
#     return [(float(rate), float(time)) for rate, time in matches]

# def count_rate_changes(seq):
#     return sum(1 for i in range(1, len(seq)) if seq[i][0] != seq[i-1][0])

# # ================= 扫描并加载文件 =================
# algo_data = {}

# for fname in os.listdir(INPUT_DIR):
#     match = FILENAME_PATTERN.match(fname)
#     if not match:
#         continue

#     algo_key = match.group("algo")
#     if algo_key not in name_map:
#         continue

#     file_path = os.path.join(INPUT_DIR, fname)
#     rate_change_counts = []

#     with open(file_path, "r") as f:
#         for line in f:
#             rt = parse_line(line)
#             rt.sort(key=lambda x: x[1])
#             rate_change_counts.append(count_rate_changes(rt))

#     if rate_change_counts:
#         algo_data[algo_key] = sorted(rate_change_counts)
#         print(f"Loaded {name_map[algo_key]:<10} | {len(rate_change_counts)} flows")

# # ================= CDF（左 98%） =================
# plt.figure(figsize=(6, 4))

# for algo_key, data in algo_data.items():
#     N = len(data)
#     cutoff_idx = next((i for i, val in enumerate(data) if val > 100), len(data))
#     data_left = data[:cutoff_idx]
#     cdf_y = [(i + 1) / len(data) for i in range(len(data_left))]

#     plt.plot(
#         data_left,
#         cdf_y[:len(data_left)],
#         linewidth=2.0,
#         label=name_map[algo_key],
#         color=COLOR_MAP.get(algo_key, 'black')  # ← 关键：固定颜色
#     )

# plt.xlabel("Number of Rate Changes per Flow")
# plt.ylabel("CDF")
# plt.legend()
# plt.grid(True)
# plt.tight_layout()
# plt.savefig(f"{OUTPUT_PREFIX}_CDF_Number_of_Rate_Changes_per_Flow.png", dpi=300)
# plt.close()

# # ================= PMF / PDF（左 98%） =================
# plt.figure(figsize=(8, 6))

# for algo_key, data in algo_data.items():
#     data_left = [x for x in data if x <= 100]
#     counter = Counter(data_left)
#     x_vals = sorted(counter.keys())
#     y_vals = [counter[x] / len(data_left) for x in x_vals]

#     plt.plot(
#         x_vals,
#         y_vals,
#         marker='o',
#         linewidth=2.0,
#         label=name_map[algo_key],
#         color=COLOR_MAP.get(algo_key, 'black'),  # ← 关键：固定颜色
#         markersize=4
#     )

# plt.xlabel("Number of Rate Changes per Flow (<= 100)")
# plt.ylabel("Probability")
# plt.legend()
# plt.grid(True)
# plt.tight_layout()
# plt.savefig(f"{OUTPUT_PREFIX}_PDF_left.png", dpi=300)
# plt.close()

# print("Saved:")
# print(f"  {OUTPUT_PREFIX}_CDF_Number_of_Rate_Changes_per_Flow.png")
# print(f"  {OUTPUT_PREFIX}_PDF_left.png")