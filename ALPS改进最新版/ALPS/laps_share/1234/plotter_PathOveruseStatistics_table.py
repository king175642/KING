#!/usr/bin/env python3
import os
import re
import matplotlib.pyplot as plt
import numpy as np

#统计发送端对所有候选路径中长度越短的是不是偏好度越高

# ================= Configuration =================
INPUT_DIR = "/file-in-ctr/outputFiles/seleted_data/PathOveruseStatistics/"
OUTPUT_PREFIX = "/file-in-ctr/PNG/PathSelectionPreference_table"

# Regex pattern to extract learning rate values from filenames
FILENAME_PATTERN = re.compile(
    r'^C00001_dragonfly_RPC_CDF_All-lr-(?P<lr>[0-9.]+)-lb-[^-]+-PathOveruseStatistics\.txt$'
)

results = {}  # {learning_rate: [shortest_path, second_shortest, third_shortest, fourth_shortest]}

# Scan directory to extract statistics under different load conditions
for filename in os.listdir(INPUT_DIR):
    match = FILENAME_PATTERN.match(filename)
    if not match:
        continue

    lr = float(match.group('lr'))  # Learning Rate
    filepath = os.path.join(INPUT_DIR, filename)

    rank_values = [None, None, None, None]  # Corresponding to path ranks 1~4

    try:
        with open(filepath, 'r') as f:
            lines = f.readlines()
            # Skip header line if exists
            start_idx = 1 if lines and lines[0].strip().split()[0].lower() in ('lengthrank', 'rank') else 0
            for line in lines[start_idx:]:
                parts = line.strip().split()
                if len(parts) < 2:
                    continue
                try:
                    rank = int(float(parts[0]))
                    preference = float(parts[1])  # Path Selection Preference
                    if 1 <= rank <= 4:
                        rank_values[rank - 1] = preference
                except (ValueError, IndexError):
                    continue
    except Exception as e:
        print(f"⚠️ Read failed: {filename} - {e}")
        continue

    results[lr] = rank_values

if not results:
    print("❌ No matching PathOveruseStatistics files found")
else:
    # Sort by learning rate
    sorted_lrs = sorted(results.keys())
    
    # Table header with clear English labels
    header = ["Length\LR"] + [f"{lr}" for lr in sorted_lrs]
    
    # Build table rows with descriptive names
    rows = []
    path_descriptions = ["Shortest Path", "2nd Shortest", "3rd Shortest", "4th Shortest"]
    
    for i, path_desc in enumerate(path_descriptions):
        row = [path_desc]
        for lr in sorted_lrs:
            val = results[lr][i]
            if val is not None:
                row.append(f"{val:.6f}")
            else:
                row.append("N/A")
        rows.append(row)
    
    # Create figure and axis for PNG output
    fig, ax = plt.subplots(figsize=(12, 8))
    ax.axis('tight')
    ax.axis('off')
    
    # Create table data
    table_data = [header] + rows
    
    # Create table
    table = ax.table(cellText=table_data,
                     cellLoc='center',
                     loc='center',
                     bbox=[0, 0, 1, 1])
    
    # Style the table
    table.auto_set_font_size(False)
    table.set_fontsize(12)
    table.scale(1.2, 2)
    
    # Apply styles to each cell
    for i in range(len(table_data)):
        for j in range(len(table_data[i])):
            cell = table[(i, j)]
            
            # Left-top corner cell: special handling
            if i == 0 and j == 0:
                cell.set_facecolor('#E8F5E8')
                cell.set_text_props(weight='bold', color='black')
                continue
            
            # First row (original header): white background
            if i == 0:
                cell.set_facecolor('#E8F5E8')
                cell.set_text_props(weight='bold', color='black')
                continue
            
            # First column (path descriptions): light green background
            if j == 0:
                cell.set_facecolor('#E8F5E8')
                cell.set_text_props(weight='bold')
                continue
            
            # Regular data cells: white background
            cell.set_facecolor('white')

    # Add title
    plt.suptitle('Path Selection Preference Statistics', 
                 fontsize=16, fontweight='bold', y=0.95)
    
    # Add subtitle with explanation
    explanation = ("LR: Network Load Ratio  |  "
                   "Preference > 1.0: Path Preference  |  "
                   "Preference < 1.0: Path Avoidance  |  "
                   "Random = 1.0")
    
    plt.figtext(0.5, 0.02, explanation, ha='center', fontsize=10, 
                bbox=dict(boxstyle="round,pad=0.3", facecolor="lightblue", alpha=0.7))
    
    # Save as PNG
    output_file = f"{OUTPUT_PREFIX}.png"
    plt.savefig(output_file, dpi=300, bbox_inches='tight', 
                facecolor='white', edgecolor='none')
    plt.close()
    
    print(f"✅ Table image saved to: {output_file}")
    
    # Also save as text file for reference
    table_lines = []
    table_width = max(len(str(item)) for row in table_data for item in row)
    
    title = "Path Selection Preference Statistics"
    table_lines.append("=" * 60)
    table_lines.append(f"{title:^60}")
    table_lines.append("=" * 60)
    
    # Format table content
    for i, row in enumerate(table_data):
        if i == 0:  # Header
            formatted_row = " | ".join(f"{str(item):^{table_width}}" for item in row)
            table_lines.append(formatted_row)
            table_lines.append("-" * len(formatted_row))
        else:
            formatted_row = " | ".join(f"{str(item):^{table_width}}" for item in row)
            table_lines.append(formatted_row)
    
    table_lines.append("=" * 60)
    table_lines.append("\nExplanation:")
    table_lines.append("- LR: network load ratio")
    table_lines.append("- Path Selection Preference = (Traffic sent on path K) / Total traffic × Number of candidate paths")
    table_lines.append("- Random spraying results in preference value of 1.0 for all paths")
    table_lines.append("- Values > 1.0 indicate path preference, < 1.0 indicate path avoidance")
    table_lines.append("- N/A indicates path does not exist or data missing")
    
    text_output_file = f"{OUTPUT_PREFIX}_data.txt"
    try:
        with open(text_output_file, 'w', encoding='utf-8') as f:
            f.write("\n".join(table_lines))
        print(f"✅ Data table saved to: {text_output_file}")
    except Exception as e:
        print(f"❌ Text file save failed: {e}")