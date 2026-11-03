import matplotlib.pyplot as plt
import numpy as np
import os
import re
import glob

# ====== 配置 ======
input_dir = "/file-in-ctr/outputFiles/C00001"
output_dir = "/file-in-ctr/PNG"
include_origin_point = True  # 是否在每条曲线前加 (0,0)

# ====== 新增：按点数裁剪配置 ======
truncate_by_points = True # 默认关闭按点数裁剪功能
max_points_to_keep = 300    # 保留前 N 个点（可调）

os.makedirs(output_dir, exist_ok=True)

# ====== 查找所有 Reordering Degree 文件（精确匹配固定格式） ======
pattern = os.path.join(input_dir, "C00001_dragonfly_RPC_CDF_All-lr-1.0-lb-*-ReorderDregree.txt")
file_list = glob.glob(pattern)

if not file_list:
    raise FileNotFoundError(f"未在 {input_dir} 中找到任何匹配 'C00001_dragonfly_RPC_CDF_All-lr-1.0-lb*-ReorderDregree.txt' 的文件！")

print(f"找到 {len(file_list)} 个重排序度文件：")
for f in file_list:
    print(f"  - {os.path.basename(f)}")

# ====== 存储所有曲线数据 ======
all_curves = []  # 每项: {"label": str, "x": array, "y": array}

# ====== 复用你原有的解析逻辑（封装成函数）=====
def parse_reordering_file(filepath):
    reorder_count = []
    total_packets_all_flows = 0

    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 4:
                continue
            try:
                total_packets = int(parts[2])
                total_packets_all_flows += total_packets
            except ValueError:
                continue

            match = re.search(r'\[([^\]]+)\]', line)
            if not match:
                continue
            list_str = match.group(1)
            try:
                counts = [int(x.strip()) for x in list_str.split(',')]
            except Exception:
                continue

            needed_len = len(counts)
            if len(reorder_count) < needed_len:
                reorder_count.extend([0] * (needed_len - len(reorder_count)))
            for i, cnt in enumerate(counts):
                reorder_count[i] += cnt

    if total_packets_all_flows == 0:
        raise ValueError(f"文件 {filepath} 未读取到有效流数据！")

    zero_reorder_count = total_packets_all_flows - sum(reorder_count)
    reorder_full = [zero_reorder_count] + reorder_count
    cdf_vals = np.cumsum(reorder_full) / total_packets_all_flows
    x_vals = np.arange(len(cdf_vals))

    if include_origin_point:
        x_plot = np.concatenate([[0], x_vals])
        y_plot = np.concatenate([[0], cdf_vals])
    else:
        x_plot = x_vals
        y_plot = cdf_vals

    return x_plot, y_plot

# ====== 按点数裁剪的函数 ======
def truncate_by_points_if_enabled(x_vals, y_vals):
    if not truncate_by_points:
        return x_vals, y_vals
    
    # 保留前 N 个点
    num_points = min(len(x_vals), max_points_to_keep)
    truncated_x = x_vals[:num_points]
    truncated_y = y_vals[:num_points]
    
    print(f"    按点数裁剪：从 {len(x_vals)} 个点保留前 {num_points} 个点")
    return truncated_x, truncated_y

# ====== 提取算法名并解析每个文件 ======
for filepath in sorted(file_list):  # 排序保证图例顺序稳定
    filename = os.path.basename(filepath)
    
    # 从文件名精确提取算法名，格式为：
    # C00001_dragonfly_RPC_CDF_All-lr-1.0-lb-e2elapsplus002-ReorderDregree.txt
    # 提取 "e2elapsplus002"
    match = re.search(r'C00001_dragonfly_RPC_CDF_All-lr-1.0-lb-(.+?)-ReorderDregree\.txt$', filename)
    if match:
        algo_name = match.group(1)
    else:
        # 理论上不会发生，因为 glob 已匹配格式
        algo_name = filename.replace("-ReorderDregree.txt", "").split('-lb-')[-1]

    print(f"解析 {algo_name} ...")
    try:
        x_raw, y_raw = parse_reordering_file(filepath)
        x_final, y_final = truncate_by_points_if_enabled(x_raw, y_raw)
        all_curves.append({"label": algo_name, "x": x_final, "y": y_final})
    except Exception as e:
        print(f"  跳过 {filename}（解析失败）: {e}")

if not all_curves:
    raise RuntimeError("没有成功解析任何文件！")

# ====== 绘图 ======
plt.figure(figsize=(10, 6))
max_x = 0

for curve in all_curves:
    plt.plot(curve["x"], curve["y"], linewidth=1.5, marker='.', markersize=3, label=curve["label"])
    max_x = max(max_x, curve["x"][-1])

plt.xlabel('Reordering Degree (k)')
plt.ylabel('CDF: P(Reordering Degree ≤ k)')
plt.title('CDF of Packet Reordering Degree (Multiple Algorithms)')
plt.grid(True, linestyle='--', alpha=0.6)
plt.legend(title="Algorithm", loc='lower right')
plt.xlim(0, max_x)
plt.ylim(0, 1.0)
plt.tight_layout()

# ====== 保存 ======
suffix = "_with_origin" if include_origin_point else "_true_cdf"
if truncate_by_points:
    suffix += f"_first_{max_points_to_keep}_points"
output_path = os.path.join(output_dir, f"reordering_degree_cdf_comparison{suffix}.png")
plt.savefig(output_path, dpi=300)
print(f"\n✅ 图像已保存至: {output_path}")

# plt.show()  # 如需交互显示，取消注释







# import matplotlib.pyplot as plt
# import numpy as np
# import os
# import re

# filename = "/file-in-ctr/outputFiles/C00001/C00001_dragonfly_RPC_CDF_All-lr-1.0-lb-e2elapsplus002-ReorderDregree.txt"
# output_dir = "/file-in-ctr/PNG"
# include_origin_point = True  # 切换是否加 (0,0)

# os.makedirs(output_dir, exist_ok=True)

# # --- 数据解析 ---
# reorder_count = []
# total_packets_all_flows = 0

# with open(filename, 'r') as f:
#     for line in f:
#         line = line.strip()
#         if not line:
#             continue
#         parts = line.split()
#         if len(parts) < 4:
#             continue
#         try:
#             total_packets = int(parts[2])
#             total_packets_all_flows += total_packets
#         except ValueError:
#             continue

#         match = re.search(r'\[([^\]]+)\]', line)
#         if not match:
#             continue
#         list_str = match.group(1)
#         try:
#             counts = [int(x.strip()) for x in list_str.split(',')]
#         except Exception:
#             continue

#         needed_len = len(counts)
#         if len(reorder_count) < needed_len:
#             reorder_count.extend([0] * (needed_len - len(reorder_count)))
#         for i, cnt in enumerate(counts):
#             reorder_count[i] += cnt

# if total_packets_all_flows == 0:
#     raise ValueError("未读取到有效的流数据！")

# zero_reorder_count = total_packets_all_flows - sum(reorder_count)
# reorder_full = [zero_reorder_count] + reorder_count
# cdf_vals = np.cumsum(reorder_full) / total_packets_all_flows
# x_vals = np.arange(len(cdf_vals))

# # --- 可选添加 (0,0) ---
# if include_origin_point:
#     x_plot = np.concatenate([[0], x_vals])
#     y_plot = np.concatenate([[0], cdf_vals])
# else:
#     x_plot = x_vals
#     y_plot = cdf_vals

# # --- 绘图（确保坐标轴从0开始）---
# fig, ax = plt.subplots(figsize=(10, 6))
# ax.plot(x_plot, y_plot, linewidth=1.5, marker='.', markersize=3, color='tab:blue')
# ax.set_xlabel('Reordering Degree')
# ax.set_ylabel('CDF: P(Reordering Degree ≤ k)')
# title = 'CDF of Packet Reordering Degree '
# if include_origin_point:
#     title += ""
# ax.set_title(title)
# ax.grid(True, linestyle='--', alpha=0.6)

# # ✅ 关键：强制坐标轴范围
# ax.set_xlim(0, x_vals[-1])
# ax.set_ylim(0, 1.0)

# plt.tight_layout()

# # --- 保存 ---
# suffix = "_with_origin" if include_origin_point else "_true_cdf"
# output_path = os.path.join(output_dir, f"reordering_degree_cdf{suffix}.png")
# plt.savefig(output_path, dpi=300)
# print(f"图像已保存至: {output_path}")



