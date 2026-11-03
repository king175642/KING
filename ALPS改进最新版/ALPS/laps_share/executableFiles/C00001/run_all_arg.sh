#!/bin/bash

# ================== 配置 ==================
SCRIPT="/file-in-ctr/executableFiles/C00001/run.py"
LBS_NAMES=('e2elaps' 'plb' 'letflow' 'ecmp')
WORKLOADS=('DCTCP_CDF' 'RPC_CDF'  'VL2_CDF' 'AliStorage2019')
LOADRATIOS=('0.5' '0.6' '0.7' '0.8' '0.9' '1.0' '0.65' '0.85' '0.95' '0.75' '0.55')
#WORKLOADS=('RPC_CDF'  )
#LOADRATIOS=( '1.0')
TOPODIRS=('railOnly' 'dragonfly')
LOG_DIR="/file-in-ctr/outputFiles/logs"
mkdir -p "$LOG_DIR"
# 'railOnly' 'dragonfly'       '0.65' '0.85' '0.95' '0.75' '0.55'
MAX_JOBS=48

# ================== 计算总实验数 ==================
total=0
for lbs in "${LBS_NAMES[@]}"; do
    if [ "$lbs" = "e2elaps" ]; then
        total=$((total + ${#WORKLOADS[@]} * ${#LOADRATIOS[@]} * ${#TOPODIRS[@]}))
    else
        total=$((total + ${#WORKLOADS[@]} * ${#LOADRATIOS[@]} * ${#TOPODIRS[@]}))
    fi
done

echo "Total experiments: $total"
echo "Max concurrent jobs: $MAX_JOBS"
echo "Logs will be saved to: $LOG_DIR"

# ================== 生成任务列表 ==================
tasks=()
for workload in "${WORKLOADS[@]}"; do
    for lbs in "${LBS_NAMES[@]}"; do
        if [ "$lbs" = "e2elaps" ]; then
            # ✅ 只跑 laps_plus 
            for load in "${LOADRATIOS[@]}"; do
                for topo in "${TOPODIRS[@]}"; do
                    tasks+=("$workload|$lbs|false|$load|$topo")
                done
            done
        else
            for load in "${LOADRATIOS[@]}"; do
                for topo in "${TOPODIRS[@]}"; do
                    tasks+=("$workload|$lbs|false|$load|$topo")
                done
            done
        fi
    done
done

# ================== 工具函数 ==================
is_pgid_alive() {
    local pgid="$1"
    ps -eo pgid= | grep -q "^ *$pgid$"
}

cleanup() {
    echo -e "\n\n🛑 检测到中断信号，正在终止所有实验..."

    for pgid in "${pgids[@]}"; do
        if is_pgid_alive "$pgid"; then
            kill -- "-$pgid" 2>/dev/null
        fi
    done

    sleep 3
    pkill -f 'python3.*run\.py.*--lbsName' 2>/dev/null || true
    pkill -f '/main$' 2>/dev/null || true

    echo "✅ 所有实验进程已终止。"
    exit 1
}

trap cleanup SIGINT SIGTERM

# ================== 主循环 ==================
pgids=()

for task in "${tasks[@]}"; do
    IFS='|' read -r workload lbs enable_laps_plus load topo <<< "$task"

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

    safe_workload="${workload//[^a-zA-Z0-9._-]/_}"
    safe_lbs="${lbs//[^a-zA-Z0-9._-]/_}"
    log_file="$LOG_DIR/run_${safe_workload}_${safe_lbs}_elp${enable_laps_plus}_${load}_${topo}.log"

    # choose_softmax 逻辑
    if [ "$lbs" = "e2elaps" ]; then
        choose_softmax="0"
    else
        choose_softmax="0"
    fi

    echo "[$(date +'%H:%M:%S')] Launching: workload=$workload, lbs=$lbs, enable_laps_plus=$enable_laps_plus, choose_softmax=$choose_softmax, load=$load, topo=$topo"

    setsid python3 "$SCRIPT" \
        --lbsName="$lbs" \
        --enable_laps_plus="$enable_laps_plus" \
        --choose_softmax="$choose_softmax" \
        --workloadName="$workload" \
        --loadratio="$load" \
        --topoDir="$topo" > "$log_file" 2>&1 &

    pgid=$!
    pgids+=("$pgid")
done

# ================== 等待完成 ==================
echo "All ${#tasks[@]} jobs submitted. Waiting..."

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