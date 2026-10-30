#!/bin/bash

# ==============================
# 综合实验控制脚本 (C00001)
# 功能：运行仿真、重跑失败任务、计算 FCT、绘图、清理 e2elapsplus 文件等
# ==============================

OUTPUT_DIR="/home/super/xiaojinyu/laps_share/outputFiles/C00001"

function run_all_lb_instances() {
    echo "准备运行所有负载均衡实例..."
    read -p "按下 ENTER 正式开始，输入 BACK 返回主菜单: " confirm
    if [[ "$confirm" == "" ]]; then
        echo "正在执行 ./run_all_arg.sh ..."
        cd /file-in-ctr/executableFiles/C00001 && ./run_all_arg.sh
    elif [[ "$confirm" == "BACK" ]]; then
        return
    else
        echo "未识别的输入，已取消操作。"
        return
    fi
}

function run_latest_lapsplus_instance() {
    echo "准备运行 LAPSPLUS 最新负载均衡实例..."
    read -p "按下 ENTER 正式开始，输入 BACK 返回主菜单: " confirm
    if [[ "$confirm" == "" ]]; then
        echo "正在执行 ./run_new_arg.sh ..."
        cd /file-in-ctr/executableFiles/C00001 && ./run_new_arg.sh
    elif [[ "$confirm" == "BACK" ]]; then
        return
    else
        echo "未识别的输入，已取消操作。"
        return
    fi
}

function rerun_failed_experiments() {
    echo "准备查询失败实验并重新运行..."
    read -p "按下 ENTER 正式开始，输入 BACK 返回主菜单: " confirm
    if [[ "$confirm" == "" ]]; then
        echo "正在执行 ./rerun_failed_exp.sh ..."
        cd /file-in-ctr/executableFiles/C00001 && ./rerun_failed_exp.sh
    elif [[ "$confirm" == "BACK" ]]; then
        return
    else
        echo "未识别的输入，已取消操作。"
        return
    fi
}
function plotter_PDF_experiments() {
    while true; do
        echo "请选择要执行的任务（输入数字，多个任务用空格隔开，输入 ALL 运行全部，输入 QUIT 返回主菜单）："
        echo "1) 绘制偏好度表格"
        echo "2) 统计不同算法的各交换机端口平均利用率，并绘制CDF图"
        echo "3) 绘制不同算法的速率改变次数CDF图"
        echo "4) 绘制一个节点对之间八条路径带宽利用率随时间变化图"
        echo "5) 绘制两种测量方式相对误差的CDF图"
        echo "6) /file-in-ctr/plot_path_error_bar.py"
        echo "7) 绘制乱序程度的CDF图"
        echo "8) 绘制各算法平均队列长度的CDF图"
        echo ""
        read -p "请输入选择（例如: 1 3 5 或 ALL 或 QUIT）: " choices

        if [[ "$choices" == "QUIT" ]]; then
            return
        elif [[ "$choices" == "ALL" ]]; then
            # 运行所有任务
            echo "正在执行 绘制偏好度表格.........."
            python3 /file-in-ctr/1234/plotter_PathOveruseStatistics_table.py
            
            echo "正在执行 统计不同算法的各交换机端口平均利用率,并绘制出CDF图.........."
            python3 /file-in-ctr/1234/plot_AVG_utilization_cdf.py
            
            echo "正在执行 绘制不同算法的速率改变次数CDF图.........."
            python3 /file-in-ctr/1234/plotter_rate_change.py
            
            echo "正在执行 绘制一个节点对之间八条路径带宽利用率随时间变化图.........."
            python3 /file-in-ctr/plotter_utilization.py
            
            echo "正在执行 绘制了两种测量方式相对误差的CDF图.........."
            python3 /file-in-ctr/plotter_PDF.py
            
            echo "正在执行 /file-in-ctr/plot_path_error_bar.py .........."
            python3 /file-in-ctr/plot_path_error_bar.py

            echo " 绘制各算法平均队列长度的CDF图"
            python3 /file-in-ctr/1234/plotter_queue_length_CDF.py
            
            echo "所有任务执行完毕！"
            break
        else
            # 解析用户输入的任务编号
            for choice in $choices; do
                case $choice in
                    1)
                        echo "正在执行 绘制偏好度表格.........."
                        python3 /file-in-ctr/1234/plotter_PathOveruseStatistics_table.py
                        ;;
                    2)
                        echo "正在执行 统计不同算法的各交换机端口平均利用率,并绘制出CDF图.........."
                        python3 /file-in-ctr/1234/plot_AVG_utilization_cdf.py
                        ;;
                    3)
                        echo "正在执行 绘制不同算法的速率改变次数CDF图.........."
                        python3 /file-in-ctr/1234/plotter_rate_change.py
                        ;;
                    4)
                        echo "正在执行 绘制一个节点对之间八条路径带宽利用率随时间变化图.........."
                        python3 /file-in-ctr/plotter_utilization.py
                        ;;
                    5)
                        echo "正在执行 绘制了两种测量方式相对误差的CDF图.........."
                        python3 /file-in-ctr/plotter_PDF.py
                        ;;
                    6)
                        echo "正在执行 /file-in-ctr/plot_path_error_bar.py .........."
                        python3 /file-in-ctr/plot_path_error_bar.py
                        ;;
                    7)
                        echo "正在执行 绘制乱序程度的CDF图 .........."
                        python3 /file-in-ctr/1234/plotter_Reordering_cdf.py
                        ;;    
                    8)
                        echo "正在执行绘制各算法平均队列长度的CDF图.........."
                        python3 /file-in-ctr/1234/plotter_queue_length_CDF.py
                        ;;   
                    *)
                        echo "警告: 无效的选择 '$choice'，跳过此项。"
                        ;;
                esac
            done
            echo "所选任务执行完毕！"
            break
        fi
    done
}

function run_single_instance() {
    echo "准备运行单个实例（编译并执行）..."
    read -p "按下 ENTER 正式开始，输入 BACK 返回主菜单: " confirm
    if [[ "$confirm" == "" ]]; then
        echo "正在执行 run.py ..."
        python3 /file-in-ctr/executableFiles/C00001/run.py
    elif [[ "$confirm" == "BACK" ]]; then
        return
    else
        echo "未识别的输入，已取消操作。"
        return
    fi
}
function run_instance_one_by_one() {
    echo "准备串行运行e2elapsplus001所有实例（编译并执行）..."
    read -p "按下 ENTER 正式开始，输入 BACK 返回主菜单: " confirm
    if [[ "$confirm" == "" ]]; then
        echo "正在执行 run.py ..."
        python3 /file-in-ctr/executableFiles/C00001/run_one_by_one.py
    elif [[ "$confirm" == "BACK" ]]; then
        return
    else
        echo "未识别的输入，已取消操作。"
        return
    fi
}
function calculate_and_plot_fct() {
    echo "准备计算 FCT 并生成图表..."
    read -p "按下 ENTER 正式开始，输入 BACK 返回主菜单: " confirm
    if [[ "$confirm" == "" ]]; then
        echo "正在执行 FCT 计算与绘图流程..."
        python3 /file-in-ctr/calculate_fct_and_sort_results.py /file-in-ctr/outputFiles/C00001 /file-in-ctr/calculate_result
        python3 /file-in-ctr/fct_plotter.py /file-in-ctr/calculate_result
    elif [[ "$confirm" == "BACK" ]]; then
        return
    else
        echo "未识别的输入，已取消操作。"
        return
    fi
}

# ✅ 新增功能：删除 e2elapsplus 文件（支持保留指定数字后缀）
function delete_e2elapsplus_files() {
    local output_dir="/file-in-ctr/outputFiles/C00001"
    echo "🗑️ 即将删除 $output_dir 下所有文件名包含 'e2elapsplus' 的文件。"
    echo "📌 你可以输入要保留的 e2elapsplus 数字后缀（例如：003 105）"

    # Step 1: 获取所有匹配文件
    all_files=()
    while IFS= read -r -d '' file; do
        all_files+=("$file")
    done < <(find "$output_dir" -maxdepth 1 -type f -name "*e2elapsplus*" -print0)

    if [ ${#all_files[@]} -eq 0 ]; then
        echo -e "\n✅ 未找到任何 e2elapsplus 相关文件。"
        read -p "按 ENTER 返回主菜单..."
        return
    fi

    echo -e "\n📋 共找到 ${#all_files[@]} 个 e2elapsplus 文件。"

    # Step 2: 用户输入要保留的数字后缀
    echo ""
    echo "❓ 请输入你想保留的 e2elapsplus 数字后缀（例如：003 105）"
    echo "   - 多个数字用空格分隔（如：001 003 105）"
    echo "   - 若不保留任何类型，直接按 ENTER"
    read -p "保留数字后缀（留空表示全部删除）: " num_input

    # 构建排除模式列表
    declare -a exclude_patterns=()
    if [[ -n "$num_input" ]]; then
        read -ra nums <<< "$num_input"
        for n in "${nums[@]}"; do
            n=$(echo "$n" | tr -d ' ')
            if [[ "$n" =~ ^[0-9]+$ ]]; then
                exclude_patterns+=("e2elapsplus$n")
            else
                echo "⚠️ 警告：'$n' 不是有效数字，已忽略。"
            fi
        done
    fi

    # Step 3: 过滤出将被删除的文件
    to_delete=()
    for file in "${all_files[@]}"; do
        basename_file=$(basename "$file")
        should_exclude=false
        for pat in "${exclude_patterns[@]}"; do
            if [[ "$basename_file" == *"$pat"* ]]; then
                should_exclude=true
                break
            fi
        done
        if [[ "$should_exclude" == false ]]; then
            to_delete+=("$file")
        fi
    done

    # Step 4: 预览并确认
    if [ ${#to_delete[@]} -eq 0 ]; then
        echo -e "\nℹ️  根据你的保留设置，没有文件需要删除。"
        read -p "按 ENTER 返回主菜单..."
        return
    fi

    echo -e "\n🔍 将被删除的文件（已排除你指定的数字后缀）："
    for file in "${to_delete[@]}"; do
        echo "  $(basename "$file")"
    done

    echo -e "\n⚠️ 共 ${#to_delete[@]} 个文件将被永久删除。"
    read -p "确认删除？输入 YES 立即执行，其他任意键取消: " confirm

    if [[ "$confirm" == "YES" ]]; then
        echo -e "\n🗑️ 正在删除..."
        for file in "${to_delete[@]}"; do
            rm -f "$file"
        done
        echo "✅ 删除完成。"
    else
        echo "❌ 操作已取消。"
    fi
    read -p "按 ENTER 返回主菜单..."
}

# ========== 主菜单 ==========
function main_menu() {
    while true; do
        clear
        echo "========================================"
        echo "         实验控制中心 (C00001)"
        echo "========================================"
        echo "1) 运行所有负载均衡实例"
        echo "2) 运行 LAPSPLUS 最新负载均衡实例"
        echo "3) 绘制实验测量相对误差概率分布图"
        echo "4) 跑一个实例（编译代码）"
        echo "5) 计算 FCT 并画图"
        echo "6) 删除 e2elapsplus 相关输出文件（可保留指定类型）"
        echo "7) 串行运行e2elapsplus001的数据"
        echo "8) 重新跑失败的例子"
        echo "9) 退出脚本"
        echo "上传代码至GitHub: 新开一个页面键入  /home/jyxiao/GitHub/copy-src-from-container.sh"
        echo "----------------------------------------"
        read -p "请选择操作 [1-7]: " choice

        case $choice in
            1)
                run_all_lb_instances
                ;;
            2)
                run_latest_lapsplus_instance
                ;;
            3)
                plotter_PDF_experiments
                ;;
            4)
                run_single_instance
                ;;
            5)
                calculate_and_plot_fct
                ;;
            6)
                delete_e2elapsplus_files
                ;;
            7)
                run_instance_one_by_one
                ;;
            8)
                rerun_failed_experiments
                ;;
            9)
                echo "再见！"
                exit 0
                ;;
            *)
                echo "❌ 无效选项，请输入 1-9 之间的数字。"
                sleep 1.5
                ;;
        esac
    done
}

# 启动主菜单
main_menu