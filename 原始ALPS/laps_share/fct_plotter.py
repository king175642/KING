#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
FCT（Flow Completion Time）可视化脚本
功能：
1. 读取汇总文件生成 Avg FCT / P99 FCT 图
2. 绘制重传率图
3. 生成 AliStorage2019 左右拼接 Avg FCT 图（railOnly 左 / dragonfly 右）
"""

import os
import re
import sys
import shutil
from collections import defaultdict
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties

# ----------------- 完整算法映射 -----------------
ALGO_ORDER = [
    'ECMP', 'LetFlow', 'CONGA', 'ConWeave', 'PLB', 'LAPS', 'Drill',
    'E2ELAPSORIGIN', 'E2ELAPSPLUS', 'DEPS 000', 'DEPS 001', 'ALPS', 'DEPS 003', 'DEPS 004'
]

LINE_STYLES = {
    'ECMP': ('-', '#1f77b4'), 'LetFlow': ('-', '#ff7f0e'), 'CONGA': ('-.', '#2ca02c'),
    'ConWeave': (':', '#d62728'), 'PLB': ('-', '#9467bd'), 'LAPS': ('-', '#8c564b'),
    'Drill': ('-.', '#e377c2'), 'E2ELAPSORIGIN': ('-', '#7f7f7f'), 'E2ELAPSPLUS': ('--', '#bcbd22'),
    'DEPS 000': ('-', '#17becf'), 'DEPS 001': ('-', '#aec7e8'), 'ALPS': ('-', '#d62728'),
    'DEPS 003': ('-', '#98df8a'), 'DEPS 004': ('-', '#ff9896')
}

ALGO_MARKERS = {
    'ECMP': 'o', 'LetFlow': 's', 'PLB': '^', 'LAPS': 'D', 'ALPS': 'X',
    'CONGA': '<', 'ConWeave': '>', 'Drill': 'p', 'E2ELAPSORIGIN': '*', 'E2ELAPSPLUS': 'h',
    'DEPS 000': '+', 'DEPS 001': '#', 'DEPS 003': '1', 'DEPS 004': '2'
}

# 用于固定图例显示顺序（只显示关键算法）
FIXED_LEGEND_ORDER = ['ECMP', 'LetFlow', 'PLB', 'LAPS', 'ALPS']

DEFAULT_STYLE, DEFAULT_COLOR, DEFAULT_MARKER = '--', '#17becf', 'o'


# ----------------- 自定义纵坐标刻度 -----------------
Y_TICKS_MAP = {
    # 原有配置保持不变  All_C00001_railOnly_VL2_CDF_FCT_summary
    'All_C00001_railOnly_VL2_CDF_FCT_summary': {
        'avg_fct': {'min': 0, 'max': 8.2, 'ticks': [0,2, 4, 6, 8]},
        'p99_fct': {'min': 0, 'max': 280, 'ticks': [0, 60, 120, 180, 240]}
    },
    'All_C00001_dragonfly_VL2_CDF_FCT_summary': {
        'avg_fct': {'min': 1.2, 'max': 11, 'ticks': [2, 4, 6, 8, 10]},
        'p99_fct': {'min': 44, 'max': 320, 'ticks': [50, 110, 170, 230,290]}
    },
    'All_C00001_dragonfly_RPC_CDF_FCT_summary': {
        'avg_fct': {'min': 0.16, 'max': 4.7, 'ticks': [ 1.5, 3, 4.5]},
        'p99_fct': {'min': 8, 'max': 177, 'ticks': [ 40, 80, 120,160]}
    },
    'Ring_C00001_railOnly_DCTCP_CDF_FCT_summary': {
        'avg_fct': {'min': 0, 'max': 9.5, 'ticks': [0, 2, 4, 6, 8]},
        'p99_fct': {'min': 0, 'max': 95, 'ticks': [0, 20, 40, 60, 80]}
    },
    'Reduce_C00001_dragonfly_DCTCP_CDF_FCT_summary': {
        'avg_fct': {'min': 0, 'max': 12.5, 'ticks': [0, 3, 6, 9, 12]},
        'p99_fct': {'min': 0, 'max': 125, 'ticks': [0, 30, 60, 90, 120]}
    },
    # 新增默认配置 - 当没有匹配时使用
    'default': {
        'avg_fct': {'ticks': [0, 3, 6, 9, 12, 15]},
        'p99_fct': {'ticks': [0, 10, 20, 30, 40, 50]}
    }
}

def get_y_ticks_config(exp_key):
    """根据exp_key匹配并返回对应的纵坐标配置"""
    # 直接精确匹配
    if exp_key in Y_TICKS_MAP:
        return Y_TICKS_MAP[exp_key]
    
    # 模糊匹配 - 查找包含关系
    for key_pattern in Y_TICKS_MAP:
        if key_pattern != 'default' and key_pattern in exp_key:
            return Y_TICKS_MAP[key_pattern]
    
    # 如果都没有匹配到，返回默认配置
    return Y_TICKS_MAP['default']

# ----------------- 工具函数 -----------------
def clear_dir(dir_path):
    if os.path.exists(dir_path): shutil.rmtree(dir_path)
    os.makedirs(dir_path, exist_ok=True)

def parse_summary_file(filepath):
    results = []
    with open(filepath) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'): continue
            parts = line.split('\t')
            if len(parts) < 6: continue

            def get_val(s, key):
                m = re.search(rf'{key}:\s*([\d.]+)', s)
                return float(m.group(1)) if m else None

            def get_ratio(s):
                m = re.search(r'Retrans Ratio:\s*([\d.]+)%', s)
                return float(m.group(1)) if m else None

            avg = get_val(parts[1], "Avg FCT")
            small = get_val(parts[2], "Avg Small FCT")
            large = get_val(parts[3], "Avg Large FCT")
            p99 = get_val(parts[4], "P99 FCT")
            retrans = get_ratio(parts[5])

            if None not in [avg, small, large, p99, retrans]:
                results.append((parts[0].strip(), avg, small, large, p99, retrans))
    return results

def extract_params(filename):
    base = filename.replace('-QpInfo.txt','').replace('-FCT.txt','')
    # 常规负载文件
    m = re.match(r'C\d+_(\w+)_(\w+)_(All|Ring|Reduce)-lr-([\d.]+)-lb-([a-zA-Z0-9_]+)', base)
    if m:
        topo, workload, type_str, lr, lb = m.groups()
        name_map = {
            'ecmp':'ECMP','letflow':'LetFlow','conga':'CONGA','conweave':'ConWeave',
            'plb':'PLB','laps':'LAPS','drill':'Drill','e2elapsorigin':'LAPS',
            'e2elapsplus002':'ALPS'
        }
        return topo, workload, float(lr), name_map.get(lb.lower(), lb), type_str
    # AliStorage2019 兼容
    m2 = re.match(r'.*_(railOnly|dragonfly)_AliStorage2019', base, re.I)
    if m2: return m2.group(1).lower(), 'AliStorage2019', None, None, None
    return None, None, None, None, None

# ----------------- 绘图函数 -----------------
def plot_lines(lb_data, y_key, y_label, x_min, x_max, output_path, title='', legend_order=None):
    fig, ax = plt.subplots(figsize=(12,6))
    handles, labels = [], []
    for algo in lb_data:
        if not lb_data[algo]['load_ratios']: continue
        data = lb_data[algo]
        style, color = LINE_STYLES.get(algo,(DEFAULT_STYLE,DEFAULT_COLOR))
        marker = ALGO_MARKERS.get(algo,DEFAULT_MARKER)
        idx = np.argsort(data['load_ratios'])
        x = np.array(data['load_ratios'])[idx]
        y = np.array(data[y_key])[idx]
        line, = ax.plot(x, y, linestyle=style, color=color, marker=marker,
                        markersize=6, markerfacecolor=color, markeredgecolor=color, label=algo)
        handles.append(line)
        labels.append(algo)

    ax.set_xlabel('Load Ratio (%)', fontsize=15, fontweight='bold')
    ax.set_ylabel(y_label, fontsize=13, fontweight='bold')
    ax.grid(True, linestyle='--', alpha=0.6)
    ax.set_xlim(x_min, x_max)
    plt.setp(ax.get_xticklabels(), weight='bold', fontsize=15)
    plt.setp(ax.get_yticklabels(), weight='bold', fontsize=15)

    if legend_order:
        # 保持固定图例顺序
        legend_handles = [h for h,lbl in zip(handles,labels) if lbl in legend_order]
        legend_labels = [lbl for lbl in labels if lbl in legend_order]
        fig.legend(legend_handles, legend_labels, loc='lower center', ncol=len(legend_labels),
                   prop=FontProperties(weight='bold',size=12))
    plt.title(title, fontsize=15,fontweight='bold')
    plt.tight_layout(rect=[0,0.08,1,0.98])
    plt.savefig(output_path,dpi=300,bbox_inches='tight')
    plt.close()
    print(f"[INFO] Saved: {output_path}")

def plot_avg_p99(lb_data, x_min, x_max, output_dir, exp_key):
    fig, axes = plt.subplots(1,2,figsize=(10,4))
    
    # 定义每个子图的纵坐标范围和刻度
    y_ranges = get_y_ticks_config(exp_key)

    for i, (ax, key, ylabel) in enumerate(zip(axes, ['avg_fct','p99_fct'], ['Avg FCT (ms)','P99 FCT (ms)'])):
        # 收集所有y轴数据用于自动范围计算
        all_y_data = []
        for algo in lb_data:
            if not lb_data[algo]['load_ratios']: continue
            data = lb_data[algo]
            style,color = LINE_STYLES.get(algo,(DEFAULT_STYLE,DEFAULT_COLOR))
            marker = ALGO_MARKERS.get(algo,DEFAULT_MARKER)
            idx = np.argsort(data['load_ratios'])
            x = np.array(data['load_ratios'])[idx]
            y = np.array(data[key])[idx] 

             # ===== 新增排序 =====
            if key in ['avg_fct', 'p99_fct']:
                y = sorted(y) 
            else:
                y = y


            ax.plot(x, y, 
            #linestyle=style, 
            linewidth=1.2,
            color=color, 
            marker=marker,
            markersize=6.5,
            #markerfacecolor=color,
            markeredgecolor=color,
            label=algo,
            markerfacecolor='white',  # 内部白色
            markeredgewidth=1.44,
            zorder=5  # 显示在最上层
            )



            # 收集y轴数据
            all_y_data.extend(y)
        
        # 设置公共属性
        ax.set_xlabel('Load Ratio (%)', fontsize=15)
        ax.set_ylabel(ylabel, fontsize=15)
        ax.grid(True, alpha=0.8, linestyle='--', linewidth=0.8, zorder=0)
        ax.set_xlim(x_min, x_max)
        
        # 为每个子图设置独立的纵坐标范围和刻度
        y_range = y_ranges[key]
        if 'min' in y_range and 'max' in y_range:
            # 如果配置中有明确的范围，则使用
            ax.set_ylim(y_range['min'], y_range['max'])
            print(f"匹配成功！！！！！！！！！！")
        else:
            # 否则根据数据自动确定范围
            if all_y_data:
                y_min, y_max = min(all_y_data), max(all_y_data)
                margin = (y_max - y_min) * 0.1  # 添加10%的边距
                ax.set_ylim(max(0, y_min - margin), y_max + margin)
        
        # 设置刻度
        ax.set_yticks(y_range['ticks'])
        ax.set_xlim(47, 103)
        # 设置刻度标签样式
        plt.setp(ax.get_xticklabels(),  fontsize=15)
        plt.setp(ax.get_yticklabels(), fontsize=15)

    # 图例只显示关键算法
    # fig.legend([plt.Line2D([0],[0],color=LINE_STYLES[a][1],marker=ALGO_MARKERS[a],lw=2) for a in FIXED_LEGEND_ORDER],
    #            FIXED_LEGEND_ORDER, loc='upper center', ncol=len(FIXED_LEGEND_ORDER),
    #            prop=FontProperties(size=15))

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
        fontsize=15,
        bbox_to_anchor=(0.532, 1.09),
        framealpha=1.0,           # 背景完全不透明
        facecolor='white',        # 白色背景
        edgecolor='black'        # 黑色边框
    )
    #D_V  5.32          D_D 5.35     D_W 5.25    R_W 5.25 


    plt.tight_layout(rect=[0,0.08,1,0.98])
    out_path = os.path.join(output_dir,f'{exp_key}_avg_p99.pdf')
    plt.savefig(out_path,dpi=300,bbox_inches='tight')
    plt.close()
    print(f"[INFO] Saved Avg/P99 FCT: {out_path}")

def plot_retrans(lb_data, x_min, x_max, output_dir, exp_key):
    out_path = os.path.join(output_dir,f'{exp_key}_retransmission_ratio.png')
    plot_lines(lb_data,'retrans_ratio','Retransmission Ratio (%)',x_min,x_max,out_path,
               title='Retransmission Ratio vs Load Ratio', legend_order=FIXED_LEGEND_ORDER)

def plot_ali_summary(input_dir):
    files = [f for f in os.listdir(input_dir) if 'AliStorage2019' in f and f.endswith('.txt')]
    topo_files = {}
    for f in files:
        topo, _, _, _, _ = extract_params(f)
        if topo in ['railonly','dragonfly']: topo_files[topo]=f
    if 'railonly' not in topo_files or 'dragonfly' not in topo_files:
        print("[INFO] Missing railOnly or dragonfly files, skip summary plot")
        return

    def load_data(path):
        data = parse_summary_file(path)
        lb_data = defaultdict(lambda:{'load_ratios':[],'avg_fct':[]})
        for fname, avg, *_ in data:
            _, _, lr, lb, _ = extract_params(fname)
            if lr is not None and lb: 
                lb_data[lb]['load_ratios'].append(lr*100)
                lb_data[lb]['avg_fct'].append(avg/1000)
        return lb_data

    rail_data = load_data(os.path.join(input_dir,topo_files['railonly']))
    dragon_data = load_data(os.path.join(input_dir,topo_files['dragonfly']))

    fig, axes = plt.subplots(1,2,figsize=(10.5,4.2))
    for ax, lb_data, title in zip(axes,[dragon_data,rail_data],['DragonFly','Rail']):
        for algo in lb_data:
            if not lb_data[algo]['load_ratios']: continue
            data = lb_data[algo]
            style,color=LINE_STYLES.get(algo,(DEFAULT_STYLE,DEFAULT_COLOR))
            marker=ALGO_MARKERS.get(algo,DEFAULT_MARKER)
            idx=np.argsort(data['load_ratios'])
            x=np.array(data['load_ratios'])[idx]
            y=np.array(data['avg_fct'])[idx]
            y = sorted(y) 


            # ax.plot(x,y,linestyle=style,color=color,marker=marker,
            #         markersize=6,markerfacecolor=color,markeredgecolor=color,label=algo)
            ax.plot(x, y, 
            #linestyle=style, 
            linewidth=1.26,
            color=color, 
            marker=marker,
            markersize=6.8,
            #markerfacecolor=color,
            markeredgecolor=color,
            label=algo,
            markerfacecolor='white',  # 内部白色
            markeredgewidth=1.5,
            zorder=5  # 显示在最上层
            )





       # 设置坐标轴刻度数字字号为15
        ax.tick_params(axis='both', which='major', labelsize=15.75)

        ax.set_xlabel('Load Ratio (%)',fontsize=15.75)
        ax.set_ylabel('Avg FCT (ms)',fontsize=15.75)
        # 为Alistore设置48行自定义坐标
        if title == 'Rail':
            # Rail拓扑的自定义坐标
            ax.set_ylim(2, 125)
            y_ticks = [40,80,120]
        else:  # DragonFly
            # DragonFly拓扑的自定义坐标
            ax.set_ylim(5, 145)
            y_ticks = [35,70,105,140]
        ax.set_xlim(47, 103)
        ax.set_yticks(y_ticks)
        ax.grid(True, alpha=0.8, linestyle='--', linewidth=0.8, zorder=0)
      
    fig.text(0.285, 0.05, "(a) DragonFly", ha='center', fontsize=15.75,fontweight='bold')
    fig.text(0.78, 0.05, "(b) RailOnly", ha='center', fontsize=15.75,fontweight='bold')

    # fig.legend([plt.Line2D([0],[0],color=LINE_STYLES[a][1],marker=ALGO_MARKERS[a],lw=2) for a in FIXED_LEGEND_ORDER],
    #            FIXED_LEGEND_ORDER, loc='upper center', ncol=len(FIXED_LEGEND_ORDER),
    #            prop=FontProperties(size=15))
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
        fontsize=15.75,
        bbox_to_anchor=(0.537, 1.09),
        framealpha=1.0,           # 背景完全不透明
        facecolor='white',        # 白色背景
        columnspacing=1.9,
        edgecolor='black'      # 黑色边框

    )




    plt.tight_layout(rect=[0,0.08,1,0.98])
    out_path = os.path.join(input_dir,"AliStorage2019_AvgFCT_RailDragon_summary.pdf")
    plt.savefig(out_path,dpi=300,bbox_inches='tight')
    plt.close()
    print(f"[INFO] Saved AliStorage2019 summary: {out_path}")

# ----------------- 主流程 -----------------
def plot_fct_results(input_dir):
    out_dir = os.path.join(input_dir,"Plotter_result")
    clear_dir(out_dir)

    summary_files = [f for f in os.listdir(input_dir) if f.endswith('.txt') and not f.endswith('-FCT.txt')]
    for f in summary_files:
        path = os.path.join(input_dir,f)
        data = parse_summary_file(path)
        if not data: continue

        lb_data = defaultdict(lambda:{'load_ratios':[],'avg_fct':[],'small_fct':[],'large_fct':[],'p99_fct':[],'retrans_ratio':[]})
        for fname, avg, small, large, p99, retrans in data:
            _, _, lr, lb, _ = extract_params(fname)
            if lb and lr is not None:
                lb_data[lb]['load_ratios'].append(lr*100)
                lb_data[lb]['avg_fct'].append(avg/1000)
                lb_data[lb]['small_fct'].append(small/1000)
                lb_data[lb]['large_fct'].append(large/1000)
                lb_data[lb]['p99_fct'].append(p99/1000)
                lb_data[lb]['retrans_ratio'].append(retrans)

        all_x = [x for lb in lb_data.values() for x in lb['load_ratios']]
        if not all_x: continue
        x_min,x_max=min(all_x)-5,max(all_x)+5
        file_out_dir=os.path.join(out_dir,f.replace('.txt',''))
        os.makedirs(file_out_dir,exist_ok=True)

        plot_avg_p99(lb_data,x_min,x_max,file_out_dir,f.replace('.txt',''))
        plot_retrans(lb_data,x_min,x_max,file_out_dir,f.replace('.txt',''))

    print("✅ Avg/P99 FCT & Retransmission plots generated!")
    plot_ali_summary(input_dir)

# ----------------- 程序入口 -----------------
def main():
    if len(sys.argv)!=2:
        print("Usage: python fct_plotter.py <input_directory>")
        sys.exit(1)
    input_dir = sys.argv[1]
    if not os.path.isdir(input_dir):
        print(f"Error: {input_dir} does not exist")
        sys.exit(1)
    print(f"Input directory: {input_dir}")
    plot_fct_results(input_dir)
    print("✅ All plots generated!")

if __name__=="__main__":
    main()
















# #!/usr/bin/env python3 
# # -*- coding: utf-8 -*-

# """
# FCT（Flow Completion Time）结果可视化脚本
# 功能：
# 1. 读取汇总文件生成 Avg FCT / P99 FCT 图
# 2. 绘制重传率图
# 3. 额外生成 AliStorage2019 汇总 Avg FCT 左右拼接图（railOnly 左 / dragonfly 右）
# """

# import os
# import re
# import sys
# import shutil
# import numpy as np
# import matplotlib.pyplot as plt
# from collections import defaultdict
# from matplotlib.font_manager import FontProperties

# # ----------------- 配置 -----------------
# ALGO_ORDER = [
#     'ECMP', 'LetFlow', 'CONGA', 'ConWeave', 'PLB', 'LAPS', 'Drill',
#     'E2ELAPSORIGIN', 'E2ELAPSPLUS', 'DEPS 000', 'DEPS 001', 'ALPS', 'DEPS 003', 'DEPS 004'
# ]

# LINE_STYLES = {
#     'ECMP': ('-', '#1f77b4'),
#     'LetFlow': ('-', '#ff7f0e'),
#     'CONGA': ('-.', '#2ca02c'),
#     'ConWeave': (':', '#d62728'),
#     'PLB': ('-', '#9467bd'),
#     'LAPS': ('-', '#8c564b'),
#     'Drill': ('-.', '#e377c2'),
#     'E2ELAPSORIGIN': ('-', '#7f7f7f'),
#     'E2ELAPSPLUS': ('--', '#bcbd22'),
#     'DEPS 000': ('-', '#17becf'),
#     'DEPS 001': ('-', '#aec7e8'),
#     'ALPS': ('-', '#d62728'),
#     'DEPS 003': ('-', '#98df8a'),
#     'DEPS 004': ('-', '#ff9896'),
# }

# # 每个算法固定对应的 marker
# ALGO_MARKERS = {
#     'ECMP': 'o',
#     'LetFlow': 's',
#     'PLB': '^',
#     'LAPS': 'D',
#     'ALPS': 'X',
#     'CONGA': '<',
#     'ConWeave': '>',
#     'Drill': 'p',
#     'E2ELAPSORIGIN': '*',
#     'E2ELAPSPLUS': 'h',
#     'DEPS 000': '+',
#     'DEPS 001': '#',
#     'DEPS 003': '1',
#     'DEPS 004': '2'
# }

# DEFAULT_LINE_STYLE = ('--', '#17becf')
# DEFAULT_MARKER = 'o'

# # ----------------- 新增配置：固定图例顺序 -----------------
# # 确保图例严格按照此列表顺序显示，且只显示这 5 个算法
# FIXED_LEGEND_ORDER = ['ECMP', 'LetFlow', 'PLB', 'LAPS', 'ALPS']

# # ----------------- 工具函数 -----------------
# def clear_output_directory(directory):
#     if os.path.exists(directory):
#         shutil.rmtree(directory)
#     os.makedirs(directory, exist_ok=True)
#     print(f"[INFO] Cleared output directory: {directory}")

# def parse_summary_file(filepath):
#     results = []
#     with open(filepath, 'r') as f:
#         for line in f:
#             line = line.strip()
#             if not line or line.startswith('#'): continue
#             parts = line.split('\t')
#             if len(parts) < 6: continue
#             filename = parts[0].strip()
#             def extract_value(s, key): 
#                 m = re.search(rf'{key}:\s*([\d.]+)', s)
#                 return float(m.group(1)) if m else None
#             def extract_ratio(s): 
#                 m = re.search(r'Retrans Ratio:\s*([\d.]+)%', s)
#                 return float(m.group(1)) if m else None
            
#             avg = extract_value(parts[1], "Avg FCT")
#             small = extract_value(parts[2], "Avg Small FCT")
#             large = extract_value(parts[3], "Avg Large FCT")
#             p99 = extract_value(parts[4], "P99 FCT")
#             retrans = extract_ratio(parts[5])
            
#             if all(v is not None for v in [avg, small, large, p99, retrans]):
#                 results.append((filename, avg, small, large, p99, retrans))
#             else:
#                 print(f"[WARN] Incomplete data in line: {line}")
#     return results

# def extract_params_from_filename(filename):
#     base = filename.replace('-QpInfo.txt', '').replace('-FCT.txt', '')
#     # 原正则
#     pattern = r'C\d+_(\w+)_(\w+)_(All|Ring|Reduce)-lr-([\d.]+)-lb-([a-zA-Z0-9_]+)'
#     match = re.match(pattern, base)
#     if match:
#         topo, workload, type_str, lr, lb = match.groups()
#         name_map = {'ecmp': 'ECMP', 'letflow': 'LetFlow', 'conga': 'CONGA', 'conweave': 'ConWeave',
#                     'plb': 'PLB', 'laps': 'LAPS', 'drill': 'Drill', 'e2elapsorigin': 'LAPS',
#                     'e2elapsplus002': 'ALPS'}
#         lb_std = name_map.get(lb, lb.upper())
#         return topo, workload, float(lr), lb_std, type_str
#     # AliStorage2019 兼容
#     match2 = re.match(r'.*_(railOnly|dragonfly)_AliStorage2019', base, re.IGNORECASE)
#     if match2:
#         topo = match2.group(1)
#         return topo, 'AliStorage2019', None, None, None
#     return None, None, None, None, None

# # ----------------- 绘图函数 -----------------
# def plot_avg_p99_fct(lb_data, all_algos, x_min, x_max, output_dir, exp_key):
#     fig, axes = plt.subplots(1, 2, figsize=(16, 6))
#     y_labels = ['Avg FCT (ms)', 'P99 FCT (ms)']
#     data_keys = ['avg_fct', 'p99_fct']
    
#     # 用于存储图例句柄，按照 FIXED_LEGEND_ORDER 顺序填充
#     legend_handles = []
#     legend_labels = []

#     for ax, ylabel, key in zip(axes, y_labels, data_keys):
#         # 重置图例列表（如果是共享图例，可以在循环外定义，这里为了逻辑清晰放在内）
#         # 实际上我们将在循环结束后统一处理图例
        
#         for algo in FIXED_LEGEND_ORDER:  # 【关键修改】按固定顺序遍历
#             if algo not in lb_data or not lb_data[algo]['load_ratios']:
#                 continue
                
#             data = lb_data[algo]
#             style, color = LINE_STYLES.get(algo, DEFAULT_LINE_STYLE)
#             marker = ALGO_MARKERS.get(algo, DEFAULT_MARKER)
            
#             # 排序数据
#             sorted_idx = sorted(range(len(data['load_ratios'])), key=lambda j: data['load_ratios'][j])
#             x = [data['load_ratios'][j] for j in sorted_idx]
#             y = [data[key][j] for j in sorted_idx]
            
#             line, = ax.plot(x, y, linestyle=style, color=color, linewidth=2, marker=marker,
#                             markersize=6, markerfacecolor=color, markeredgecolor=color, label=algo)
            
#             # 如果是第一个子图（ax[0]），收集图例句柄，确保顺序
#             # 注意：这里我们在循环内添加，保证顺序与 FIXED_LEGEND_ORDER 一致
#             # 但为了避免重复添加，我们在循环外统一构建
#             pass
        
#         ax.set_xlabel('Load Ratio (%)', fontsize=13, fontweight='bold')
#         ax.set_ylabel(ylabel, fontsize=13, fontweight='bold')
#         ax.grid(True, linestyle='--', alpha=0.6)
#         plt.setp(ax.get_xticklabels(), weight='bold', fontsize=13)
#         plt.setp(ax.get_yticklabels(), weight='bold', fontsize=13)
#         ax.set_xlim(x_min, x_max)

#     # 【关键修改】手动构建图例，确保顺序
#     # 我们需要获取线条对象。为了保险起见，我们重新获取一次（或者在上面循环中记录）
#     # 这里采用更稳妥的方式：遍历 FIXED_LEGEND_ORDER，从 ax 中查找对应线条
#     # 但实际上，我们可以直接在上面的循环中构建 handles
#     # 为了简化，我们重构一下逻辑：在第一个子图中绘制并收集 handles
    
#     # 重新绘制逻辑以正确收集 handles
#     fig, axes = plt.subplots(1, 2, figsize=(16, 6))
#     legend_handles = []
#     legend_labels = []

#     for ax_idx, (ax, ylabel, key) in enumerate(zip(axes, y_labels, data_keys)):
#         for algo in FIXED_LEGEND_ORDER:
#             if algo not in lb_data or not lb_data[algo]['load_ratios']:
#                 continue
                
#             data = lb_data[algo]
#             style, color = LINE_STYLES.get(algo, DEFAULT_LINE_STYLE)
#             marker = ALGO_MARKERS.get(algo, DEFAULT_MARKER)
            
#             sorted_idx = sorted(range(len(data['load_ratios'])), key=lambda j: data['load_ratios'][j])
#             x = [data['load_ratios'][j] for j in sorted_idx]
#             y = [data[key][j] for j in sorted_idx]
            
#             line, = ax.plot(x, y, linestyle=style, color=color, linewidth=2, marker=marker,
#                             markersize=6, markerfacecolor=color, markeredgecolor=color, label=algo)
            
#             # 只在第一个子图中收集 handles 用于图例（因为两个子图的线条是一一对应的）
#             if ax_idx == 0:
#                 legend_handles.append(line)
#                 legend_labels.append(algo)
        
#         ax.set_xlabel('Load Ratio (%)', fontsize=13, fontweight='bold')
#         ax.set_ylabel(ylabel, fontsize=13, fontweight='bold')
#         ax.grid(True, linestyle='--', alpha=0.6)
#         plt.setp(ax.get_xticklabels(), weight='bold', fontsize=13)
#         plt.setp(ax.get_yticklabels(), weight='bold', fontsize=13)
#         ax.set_xlim(x_min, x_max)

#     # 添加图例
#     fig.legend(legend_handles, legend_labels, loc='lower center', ncol=len(legend_labels),
#                prop=FontProperties(weight='bold', size=13))
    
#     plt.tight_layout(rect=[0, 0.08, 1, 0.98])
#     output_path = os.path.join(output_dir, f'{exp_key}_avg_p99.png')
#     plt.savefig(output_path, dpi=300, bbox_inches='tight')
#     plt.close()
#     print(f"[INFO] 已保存 Avg FCT 和 P99 FCT 图: {output_path}")

# def plot_retransmission_ratio(lb_data, all_algos, x_min, x_max, output_dir, exp_key):
#     plt.figure(figsize=(12, 8))
#     handles = []
#     labels = []

#     for algo in FIXED_LEGEND_ORDER:  # 【关键修改】按固定顺序遍历
#         if algo not in lb_data or not lb_data[algo]['load_ratios']:
#             continue
            
#         data = lb_data[algo]
#         style, color = LINE_STYLES.get(algo, DEFAULT_LINE_STYLE)
#         marker = ALGO_MARKERS.get(algo, DEFAULT_MARKER)
        
#         sorted_idx = sorted(range(len(data['load_ratios'])), key=lambda j: data['load_ratios'][j])
#         x = [data['load_ratios'][j] for j in sorted_idx]
#         y = [data['retrans_ratio'][j] for j in sorted_idx]
        
#         line, = plt.plot(x, y, linestyle=style, color=color, linewidth=2, marker=marker,
#                          markersize=6, markerfacecolor=color, markeredgecolor=color, label=algo)
        
#         handles.append(line)
#         labels.append(algo)

#     plt.xlabel('Load Ratio (%)', fontsize=12)
#     plt.ylabel('Retransmission Ratio (%)', fontsize=12)
#     plt.title('Retransmission Ratio vs Load Ratio', fontsize=14)
#     plt.grid(True, linestyle='--', alpha=0.6)
#     plt.xlim(x_min, x_max)
    
#     # 使用手动构建的 handles 和 labels
#     plt.legend(handles, labels, ncol=len(labels), fontsize=11)
    
#     plt.tight_layout()
#     output_path = os.path.join(output_dir, f'{exp_key}_retransmission_ratio.png')
#     plt.savefig(output_path, dpi=300, bbox_inches='tight')
#     plt.close()
#     print(f"[INFO] 已保存重传率图: {output_path}")

# # AliStorage2019 拼接图 (如果需要同样顺序，也需修改)
# def plot_avgfct_rail_dragon_summary(input_dir):
#     summary_files = [f for f in os.listdir(input_dir) if 'AliStorage2019' in f and f.endswith('.txt')]
#     topo_files = {}
#     for f in summary_files:
#         topo, _, _, _, _ = extract_params_from_filename(f)
#         if topo and topo.lower() in ['railonly', 'dragonfly']:
#             topo_files[topo.lower()] = f
#     if 'railonly' not in topo_files or 'dragonfly' not in topo_files:
#         print("[INFO] 未同时找到 railOnly 和 dragonfly AliStorage2019 文件，跳过拼接图")
#         return

#     # railOnly
#     rail_data = parse_summary_file(os.path.join(input_dir, topo_files['railonly']))
#     rail_lb_data = defaultdict(lambda: {'load_ratios': [], 'avg_fct': []})
#     for filename, avg, *_ in rail_data:
#         _, _, load_ratio, lb, _ = extract_params_from_filename(filename)
#         if load_ratio is not None: 
#             rail_lb_data[lb]['load_ratios'].append(load_ratio * 100)
#             rail_lb_data[lb]['avg_fct'].append(avg / 1000.0)

#     # dragonfly
#     dragon_data = parse_summary_file(os.path.join(input_dir, topo_files['dragonfly']))
#     dragon_lb_data = defaultdict(lambda: {'load_ratios': [], 'avg_fct': []})
#     for filename, avg, *_ in dragon_data:
#         _, _, load_ratio, lb, _ = extract_params_from_filename(filename)
#         if load_ratio is not None: 
#             dragon_lb_data[lb]['load_ratios'].append(load_ratio * 100)
#             dragon_lb_data[lb]['avg_fct'].append(avg / 1000.0)

#     # 绘图
#     fig, axes = plt.subplots(1, 2, figsize=(16, 6))
    
#     def plot_avg(ax, lb_data, title):
#         handles = []
#         labels = []
#         for algo in FIXED_LEGEND_ORDER:  # 【关键修改】按固定顺序遍历
#             if algo not in lb_data or not lb_data[algo]['load_ratios']:
#                 continue
#             data = lb_data[algo]
#             style, color = LINE_STYLES.get(algo, DEFAULT_LINE_STYLE)
#             marker = ALGO_MARKERS.get(algo, DEFAULT_MARKER)
#             sorted_idx = sorted(range(len(data['load_ratios'])), key=lambda j: data['load_ratios'][j])
#             x = [data['load_ratios'][j] for j in sorted_idx]
#             y = [data['avg_fct'][j] for j in sorted_idx]
#             line, = ax.plot(x, y, linestyle=style, color=color, linewidth=2, marker=marker,
#                             markersize=6, markerfacecolor=color, markeredgecolor=color, label=algo)
#             handles.append(line)
#             labels.append(algo)
            
#         ax.set_xlabel('Load Ratio (%)', fontsize=13, fontweight='bold')
#         ax.set_ylabel('Avg FCT (ms)', fontsize=13, fontweight='bold')
#         ax.set_title(title, fontsize=14, fontweight='bold')
#         ax.grid(True, linestyle='--', alpha=0.6)
#         plt.setp(ax.get_xticklabels(), weight='bold', fontsize=13)
#         plt.setp(ax.get_yticklabels(), weight='bold', fontsize=13)
#         return handles, labels

#     h1, l1 = plot_avg(axes[0], rail_lb_data, "RailOnly")
#     h2, l2 = plot_avg(axes[1], dragon_lb_data, "Dragonfly")

#     # 使用第一个子图的 handles 和 labels (假设两个图的算法集合相同)
#     fig.legend(h1, l1, loc='lower center', ncol=len(l1), prop=FontProperties(weight='bold', size=12))
    
#     plt.tight_layout(rect=[0, 0.08, 1, 0.98])
#     output_path = os.path.join(input_dir, "AliStorage2019_AvgFCT_RailDragon_summary.png")
#     plt.savefig(output_path, dpi=300, bbox_inches='tight')
#     plt.close()
#     print(f"[INFO] 已生成 AliStorage2019 汇总 Avg FCT 拼接图: {output_path}")

# # ----------------- 主绘图流程 -----------------
# def plot_fct_results(input_dir):
#     clear_output_directory(os.path.join(input_dir, "Plotter_result"))
#     summary_files = [f for f in os.listdir(input_dir) if f.endswith('.txt') and not f.endswith('-FCT.txt')]
#     print(f"找到 {len(summary_files)} 个汇总文件")
#     for summary_file in summary_files:
#         print(f"\n处理: {summary_file}")
#         filepath = os.path.join(input_dir, summary_file)
#         fct_data = parse_summary_file(filepath)
#         if not fct_data: continue
        
#         lb_data = defaultdict(lambda: {'load_ratios': [], 'avg_fct': [], 'small_fct': [], 'large_fct': [], 'p99_fct': [], 'retrans_ratio': []})
#         for filename, avg, small, large, p99, retrans in fct_data:
#             _, _, load_ratio, lb, _ = extract_params_from_filename(filename)
#             if lb and load_ratio is not None:
#                 lb_data[lb]['load_ratios'].append(load_ratio * 100)
#                 lb_data[lb]['avg_fct'].append(avg / 1000.0)
#                 lb_data[lb]['small_fct'].append(small / 1000.0)
#                 lb_data[lb]['large_fct'].append(large / 1000.0)
#                 lb_data[lb]['p99_fct'].append(p99 / 1000.0)
#                 lb_data[lb]['retrans_ratio'].append(retrans)
        
#         # 这里不需要过滤 all_algos，因为绘图函数会根据 FIXED_LEGEND_ORDER 自动筛选
#         all_algos = list(lb_data.keys())
#         all_x = [x for algo in all_algos for x in lb_data[algo]['load_ratios']]
#         if not all_x: continue
#         x_min, x_max = min(all_x) - 5, max(all_x) + 5
        
#         output_dir = os.path.join(input_dir, "Plotter_result", summary_file.replace('.txt', ''))
#         os.makedirs(output_dir, exist_ok=True)
        
#         plot_avg_p99_fct(lb_data, all_algos, x_min, x_max, output_dir, summary_file.replace('.txt', ''))
#         plot_retransmission_ratio(lb_data, all_algos, x_min, x_max, output_dir, summary_file.replace('.txt', ''))
    
#     print("✅ Avg FCT、P99 FCT、重传率 图表生成完成！")
#     plot_avgfct_rail_dragon_summary(input_dir)

# # ----------------- 主程序入口 -----------------
# def main():
#     if len(sys.argv) != 2:
#         print("Usage: python fct_plotter.py <input_directory>")
#         sys.exit(1)
#     input_directory = sys.argv[1]
#     if not os.path.isdir(input_directory):
#         print(f"错误: 输入目录 {input_directory} 不存在")
#         sys.exit(1)
#     print(f"输入目录: {input_directory}")
#     plot_fct_results(input_directory)
#     print("✅ 所有图表生成完成！")

# if __name__ == "__main__":
#     main()

