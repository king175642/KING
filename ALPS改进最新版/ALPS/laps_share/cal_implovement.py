import os
import re
from collections import defaultdict

# 配置
INPUT_DIR = "/file-in-ctr/calculate_result"
TARGET_ALGO = "e2elapsplus002"
BASELINE_ALGOS = ["ecmp", "letflow", "plb", "e2elapsorigin"]  # 四个对比算法
OUTPUT_FILE = os.path.join(INPUT_DIR, "lapsplus002_improvement_summary.txt")
TABLE_OUTPUT_FILE = os.path.join(INPUT_DIR, "lapsplus002_improvement_summary_table.txt")

def parse_summary_file(filepath):
    """
    解析文件，返回字典：algo -> list of (load_ratio, avg_fct)
    只保留 load_ratio in [0.5, 1.0]
    """
    data = defaultdict(list)
    with open(filepath, 'r') as f:
        for line in f:
            match = re.search(r'lr-(\d+\.\d+)-lb-([a-zA-Z0-9_]+).*?Avg FCT:\s*([\d.]+)', line)
            if match:
                load_ratio = float(match.group(1))
                algo = match.group(2).lower()
                avg_fct = float(match.group(3))
                if 0.5 <= load_ratio <= 1.0:
                    data[algo].append((load_ratio, avg_fct))
    return data

def format_table(data_rows, headers):
    """格式化为精确对齐的表格形式"""
    # 计算每列的最大宽度
    all_rows = [headers] + data_rows
    col_widths = []
    for i in range(len(headers)):
        width = max(len(str(row[i])) for row in all_rows)
        # 设置最小宽度，确保美观
        col_widths.append(max(width, 12))
    
    # 生成表格行
    table_lines = []
    # 表头
    header_parts = []
    for i in range(len(headers)):
        content = str(headers[i])
        padding = col_widths[i] - len(content)
        left_pad = padding // 2
        right_pad = padding - left_pad
        padded_content = " " * left_pad + content + " " * right_pad
        header_parts.append(padded_content)
    header_line = " | ".join(header_parts)
    separator = "+".join("-" * (w + 2) for w in col_widths)
    separator = "+" + separator[1:-1] + "+"
    table_lines.append("|" + header_line + "|")
    table_lines.append(separator)
    
    # 数据行
    for row in data_rows:
        row_parts = []
        for i in range(len(row)):
            content = str(row[i])
            padding = col_widths[i] - len(content)
            left_pad = padding // 2
            right_pad = padding - left_pad
            padded_content = " " * left_pad + content + " " * right_pad
            row_parts.append(padded_content)
        row_line = " | ".join(row_parts)
        table_lines.append("|" + row_line + "|")
    
    return "\n".join(table_lines)

def main():
    summary_files = [
        f for f in os.listdir(INPUT_DIR)
        if f.endswith('_FCT_summary.txt')
    ]

    results_data = []

    for fname in sorted(summary_files):
        print(f"处理: {fname}")
        filepath = os.path.join(INPUT_DIR, fname)
        data = parse_summary_file(filepath)

        # 提取实验名称：C00001_dragonfly_AliStorage2019_FCT_summary.txt → dragonfly_AliStorage2019
        parts = fname.replace('.txt', '').split('_')
        if len(parts) >= 4 and parts[-2] == 'FCT' and parts[-1] == 'summary':
            exp_name = '_'.join(parts[1:-2])  # 排除 Cxxxx, FCT, summary
        else:
            exp_name = fname.replace('_FCT_summary.txt', '')

        # 获取 e2elapsplus002 的最大 Avg FCT
        if TARGET_ALGO not in data or not data[TARGET_ALGO]:
            print(f"  -> 跳过：{TARGET_ALGO} 无数据")
            continue

        laps_max = max(avg_fct for _, avg_fct in data[TARGET_ALGO])

        improvements = []
        valid = True
        for baseline in BASELINE_ALGOS:
            if baseline not in data or not data[baseline]:
                print(f"  -> 警告：{baseline} 无数据，跳过该对比")
                improvements.append("N/A")
                continue
            baseline_max = max(avg_fct for _, avg_fct in data[baseline])
            if baseline_max == 0:
                imp = 0.0
            else:
                imp = (1 - laps_max / baseline_max) * 100
            improvements.append(f"{imp:.2f}")

        # 添加到数据列表
        results_data.append([exp_name] + improvements)

    # 创建表头
    baseline_names = {
        "ecmp": "ECMP",
        "letflow": "LetFlow",
        "plb": "PLB",
        "e2elapsorigin": "E2ELAPSorigin"
    }
    headers = ["Experiment"] + [f"vs_{baseline_names[algo]}(%)" for algo in BASELINE_ALGOS]

    # 格式化为表格
    table_content = format_table(results_data, headers)
    
    # 保存原始制表符分隔格式到文件
    with open(OUTPUT_FILE, 'w') as out_f:
        header_line = "\t".join(headers) + "\n"
        out_f.write(header_line)
        for row in results_data:
            out_f.write("\t".join(row) + "\n")

    # 保存表格格式到另一个文件
    with open(TABLE_OUTPUT_FILE, 'w') as table_out_f:
        table_out_f.write(table_content)

    print(f"\n✅ 制表符分隔格式结果已保存至: {OUTPUT_FILE}")
    print(f"✅ 表格格式结果已保存至: {TABLE_OUTPUT_FILE}")
    print("\n📊 表格形式输出预览:")
    print(table_content)

if __name__ == "__main__":
    main()