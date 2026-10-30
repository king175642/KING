#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
功能说明：
1. 原有功能（完全保留）：
   - 批量读取 QpInfo.txt
   - 提取末尾 FCT 指标
   - 计算全局重传比例
   - 按 prefix 分组、lb / lr 排序
   - 输出 FCT 汇总文件

2. 新增功能（旁路）：
   - 仅处理包含 e2elapsplus 的文件
   - 提取 Path Length Statistics
   - 按 Case + Topo + CC + Workload + Algorithm 聚合
   - 不同 lr 合并输出
   - 输出至 {output_dir}/path_result/
   - 增加每个 Load Ratio 的总发包数和总重传数
"""

import sys
import os
import re
import shutil
from collections import defaultdict

# =========================
# 原有功能（完全不变）
# =========================

def extract_metrics_from_qpinfo(file_path):
    metrics = {
        'avg_fct': None,
        'avg_fct_small': None,
        'avg_fct_large': None,
        '99_fct': None
    }

    with open(file_path, 'r') as f:
        lines = f.readlines()

    for line in reversed(lines[-30:]):
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 2:
            continue
        key, value_str = parts[0], parts[1]
        if key in metrics:
            try:
                metrics[key] = float(value_str)
            except ValueError:
                continue

    if any(v is None for v in metrics.values()):
        print(f"[WARN] Incomplete metrics in {file_path}: {metrics}")

    return metrics


def compute_retrans_ratio(file_path):
    total_sent = 0
    total_rece = 0

    with open(file_path, 'r') as f:
        for line in f:
            if "flowId=" in line and "sentPkt=" in line and "recePkt=" in line:
                sent_match = re.search(r'sentPkt=(\d+)', line)
                rece_match = re.search(r'recePkt=(\d+)', line)
                if sent_match and rece_match:
                    total_sent += int(sent_match.group(1))
                    total_rece += int(rece_match.group(1))

    if total_rece == 0:
        return 0.0
    return (total_sent - total_rece) / total_rece


def compute_total_packets(file_path):
    """
    返回：总发包数、总丢包数、总重传数
    """
    total_sent = 0
    total_lost = 0
    total_retrans = 0

    with open(file_path, 'r') as f:
        for line in f:
            if "flowId=" in line and "sentPkt=" in line and "recePkt=" in line:
                sent_match = re.search(r'sentPkt=(\d+)', line)
                rece_match = re.search(r'recePkt=(\d+)', line)
                lost_match = re.search(r'lostPkt=(\d+)', line)  # 如果有 lostPkt
                if sent_match and rece_match:
                    sent = int(sent_match.group(1))
                    rece = int(rece_match.group(1))
                    lost = int(lost_match.group(1)) if lost_match else 0

                    total_sent += sent
                    total_lost += lost
                    total_retrans += (sent - rece)

    return total_sent, total_lost, total_retrans


def extract_prefix_and_lr_lb(filename):
    """
    返回：
    - prefix: 文件前缀，例如 C00001_dragonfly_DCTCP_CDF
    - type_str: All / Ring / Reduce
    - lr: float
    - lb: 算法名
    """
    type_match = re.search(r'_(All|Ring|Reduce)-lr', filename)
    type_str = type_match.group(1) if type_match else "unknown"

    if type_str != "unknown":
        prefix = filename.split(f"_{type_str}-lr")[0]
    else:
        prefix = os.path.splitext(filename)[0]

    lr_match = re.search(r'lr-(\d+\.\d+)', filename)
    lb_match = re.search(r'lb-([a-zA-Z0-9_]+)', filename)

    lr = float(lr_match.group(1)) if lr_match else 0.0
    lb = lb_match.group(1) if lb_match else "unknown"

    return prefix, type_str, lr, lb

def sort_lines_by_lb_lr(lines):
    groups = defaultdict(list)
    for line in lines:
        if not line.strip():
            continue
        filename = line.split('\t')[0]
        # 原来: _, lr, lb = extract_prefix_and_lr_lb(filename)
        _, _, lr, lb = extract_prefix_and_lr_lb(filename)  # 最小改动
        groups[lb].append((lr, line))

    sorted_lines = []
    for i, lb in enumerate(sorted(groups.keys())):
        for _, line in sorted(groups[lb], key=lambda x: x[0]):
            sorted_lines.append(line)
        if i < len(groups) - 1:
            sorted_lines.append("")
    return sorted_lines

def clear_output_directory(directory):
    if os.path.exists(directory):
        shutil.rmtree(directory)
    os.makedirs(directory, exist_ok=True)


# =========================
# 新增功能：Path 解析
# =========================

def extract_path_prefix_lr_algorithm(filename):
    """
    返回：
    - path_prefix: C00001_dragonfly_DCTCP_CDF_e2elapsplus002
    - lr: float
    - algorithm: e2elapsplus002
    """
    lr_match = re.search(r'lr-(\d+\.\d+)', filename)
    lb_match = re.search(r'lb-([^-]+)', filename)

    if not lr_match or not lb_match:
        return None, None, None

    lr = float(lr_match.group(1))
    algorithm = lb_match.group(1)

    base = filename.split("_All-lr")[0]
    path_prefix = f"{base}_{algorithm}"

    return path_prefix, lr, algorithm


def extract_path_length_statistics(file_path):
    results = []
    start = False

    with open(file_path, 'r') as f:
        for line in f:
            line = line.strip()

            if line == "=== Path Length Statistics ===":
                start = True
                continue

            if start:
                if line.startswith("Path Length"):
                    results.append(line)
                elif line == "":
                    break

    return results


# =========================
# main
# =========================

def main():
    if len(sys.argv) != 3:
        print("Usage: python calculate_fct_and_sort_results.py <input_dir> <output_dir>")
        sys.exit(1)

    input_dir = sys.argv[1]
    output_dir = sys.argv[2]

    if not os.path.isdir(input_dir):
        print(f"Error: {input_dir} is not a directory")
        sys.exit(1)

    clear_output_directory(output_dir)
    print(f"[INFO] Cleared output directory: {output_dir}")

    files = [f for f in os.listdir(input_dir) if f.endswith("QpInfo.txt")]
    if not files:
        print("No QpInfo.txt files found!")
        sys.exit(1)

    summary_data = defaultdict(list)
    path_group_data = defaultdict(lambda: defaultdict(list))
    fct_total_packets = {}  # 保存每个文件总发包数和重传数

    for filename in files:
        full_path = os.path.join(input_dir, filename)

        # ===== 原有 FCT 汇总逻辑 =====
        metrics = extract_metrics_from_qpinfo(full_path)
        if metrics['avg_fct'] is None:
            continue

        retrans_ratio = compute_retrans_ratio(full_path)
        total_sent, total_lost, total_retrans = compute_total_packets(full_path)
        fct_total_packets[filename] = (total_sent, total_retrans)

        prefix, type_str, _, _ = extract_prefix_and_lr_lb(filename)

        line = (
            f"{filename}\t"
            f"Avg FCT: {metrics['avg_fct']:.6f}\t"
            f"Avg Small FCT: {metrics['avg_fct_small']:.6f}\t"
            f"Avg Large FCT: {metrics['avg_fct_large']:.6f}\t"
            f"P99 FCT: {metrics['99_fct']:.6f}\t"
            f"Retrans Ratio: {retrans_ratio:.4%}\t"
            f"Total packets sent: {total_sent}\t"
            f"Total packets retransmitted: {total_retrans}"
        )
        summary_data[prefix].append(line)

        # ===== 新增 Path 统计（旁路）=====
        if "e2elapsplus" not in filename:
            continue

        path_prefix, lr, _ = extract_path_prefix_lr_algorithm(filename)
        if path_prefix is None:
            continue

        path_lines = extract_path_length_statistics(full_path)
        if path_lines:
            path_group_data[path_prefix][lr].extend(path_lines)

    # ===== 输出 FCT 汇总 =====
    for prefix, lines in summary_data.items():
        output_path = os.path.join(output_dir, f"{type_str}_{prefix}_FCT_summary.txt")
        with open(output_path, 'w') as f:
            for line in sort_lines_by_lb_lr(lines):
                f.write(line + "\n")
        print(f"[INFO] Saved: {output_path}")

    # ===== 输出 Path 统计 =====
    path_output_dir = os.path.join(output_dir, "path_result")
    os.makedirs(path_output_dir, exist_ok=True)

    for prefix, lr_dict in path_group_data.items():
        output_path = os.path.join(path_output_dir, f"{prefix}_pathinfo.txt")
        with open(output_path, 'w') as f:
            for lr in sorted(lr_dict.keys()):
                f.write(f"[Load Ratio: {lr:.4f}]\n")

                total_sent_all = 0
                total_retrans_all = 0

                for line in lr_dict[lr]:
                    f.write(line + "\n")
                    # 如果 Path Length line 中已有 Total packets sent/retransmitted，则累加
                    sent_match = re.search(r'Total packets sent: (\d+)', line)
                    retrans_match = re.search(r'Total packets retransmitted: (\d+)', line)
                    if sent_match:
                        total_sent_all += int(sent_match.group(1))
                    if retrans_match:
                        total_retrans_all += int(retrans_match.group(1))

                f.write(f"Total packets sent (all paths): {total_sent_all}, Total packets retransmitted (all paths): {total_retrans_all}\n\n")
        print(f"[INFO] Saved path info: {output_path}")


if __name__ == "__main__":
    main()
