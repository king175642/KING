#!/bin/bash

# ================== 配置 ==================
SCRIPT="/file-in-ctr/executableFiles/C00001/run.py"
SOFTMAX_OPTS=(2)
WORKLOADS=('RPC_CDF' )
LOADRATIOS=( '0.8' '0.9' '1.0' '0.85' '0.95')
TOPODIRS=('dragonfly' 'railOnly')
LOG_DIR="/file-in-ctr/outputFiles/logs"
mkdir -p "$LOG_DIR"
# 'DCTCP_CDF' 'RPC_CDF'  'VL2_CDF' 'FbHdp2015' 'AliStorage2019'
# 最大并发任务数（每个任务 = 一个 run.py + 其所有子进程）
MAX_JOBS=32

# ================== 初始化 ==================
total=$(( ${#SOFTMAX_OPTS[@]} * ${#WORKLOADS[@]} * ${#LOADRATIOS[@]} * ${#TOPODIRS[@]} ))
echo "Total experiments: ${#WORKLOADS[@]} × ${#SOFTMAX_OPTS[@]} × ${#LOADRATIOS[@]} × ${#TOPODIRS[@]} = $total"
echo "Max concurrent jobs: $MAX_JOBS"
echo "Logs will be saved to: $LOG_DIR"

# 收集所有参数组合
tasks=()
for workload in "${WORKLOADS[@]}"; do
    for softmax in "${SOFTMAX_OPTS[@]}"; do
        for load in "${LOADRATIOS[@]}"; do
            for topo in "${TOPODIRS[@]}"; do
                tasks+=("$workload|$softmax|$load|$topo")
            done
        done
    done
done

# ================== 工具函数 ==================
# 检查进程组是否仍有活动进程
is_pgid_alive() {
    local pgid="$1"
    # 如果存在任意进程的 PGID 等于 $pgid，则返回 0（alive）
    ps -eo pgid= | grep -q "^ *$pgid$"
}

# 清理所有运行中的实验进程组
cleanup() {
    echo -e "\n\n🛑 检测到中断信号（Ctrl+C 或 SIGTERM），正在终止所有实验..."
    
    for pgid in "${pgids[@]}"; do
        if is_pgid_alive "$pgid"; then
            echo "Killing process group: -$pgid"
            # 发送 SIGTERM 先尝试优雅退出
            kill -- "-$pgid" 2>/dev/null
        fi
    done

    # 等待几秒让进程退出
    sleep 3

    # 强制清理残留（保险）
    echo "清理可能残留的进程..."
    pkill -f 'python3.*run\.py.*--workloadName' 2>/dev/null || true
    pkill -f '/main$' 2>/dev/null || true   # 匹配路径结尾为 /main 的二进制

    echo "✅ 所有实验进程已终止。"
    exit 1
}

# 设置信号捕获
trap cleanup SIGINT SIGTERM

# ================== 主循环 ==================
pgids=()  # 存储每个任务的进程组 ID (PGID)

for task in "${tasks[@]}"; do
    IFS='|' read -r workload softmax load topo <<< "$task"
    
    # 并发控制：等待直到有空位
    while (( ${#pgids[@]} >= MAX_JOBS )); do
        sleep 5
        new_pgids=()
        for pgid in "${pgids[@]}"; do
            if is_pgid_alive "$pgid"; then
                new_pgids+=("$pgid")
            fi
        done
        pgids=("${new_pgids[@]}")
    done

    # 构造安全日志文件名
    safe_workload="${workload//[^a-zA-Z0-9._-]/_}"
    log_file="$LOG_DIR/run_${safe_workload}_e2elaps00${softmax}_elptrue_${load}_${topo}.log"

    echo "[$(date +'%H:%M:%S')] Launching: workload=$workload, softmax=$softmax, load=$load, topo=$topo → $(basename "$log_file")"
    
    # ✅ 关键：用 setsid 启动独立进程组
    setsid python3 "$SCRIPT" \
        --choose_softmax="$softmax" \
        --workloadName="$workload" \
        --loadratio="$load" \
        --enable_laps_plus="true" \
        --topoDir="$topo" > "$log_file" 2>&1 &

    pgid=$!  # 获取 setsid 的 PID（即新进程组 ID）
    pgids+=("$pgid")
done

# ================== 等待全部完成 ==================
echo "All ${#tasks[@]} jobs submitted. Waiting for remaining ${#pgids[@]} groups to finish..."

# 轮询等待所有进程组结束
while (( ${#pgids[@]} > 0 )); do
    sleep 10
    new_pgids=()
    for pgid in "${pgids[@]}"; do
        if is_pgid_alive "$pgid"; then
            new_pgids+=("$pgid")
        fi
    done
    pgids=("${new_pgids[@]}")
done

echo "✅ All experiments completed successfully!"