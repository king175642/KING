#!/bin/bash

# ================== 配置 ==================
LOG_DIR="/file-in-ctr/outputFiles/C00001"
SCRIPT="/file-in-ctr/executableFiles/C00001/run.py"

LBS_NAMES=('plb' 'letflow' 'ecmp' 'e2elaps')
WORKLOADS=('DCTCP_CDF' )
LOADRATIOS=('0.5' '0.6' '0.7' '0.8' '0.9' '1.0' '0.55' '0.65' '0.85' '0.95' '0.75')
TOPODIRS=('railOnly' 'dragonfly')
#'DCTCP_CDF' 'RPC_CDF' 'VL2_CDF' 'AliStorage2019'
MAX_JOBS=48

echo "🔍 Scanning for missing -QpInfo.txt files in: $LOG_DIR"

# ================== 收集失败任务 ==================
failed_tasks=()

for workload in "${WORKLOADS[@]}"; do
    for lbs in "${LBS_NAMES[@]}"; do
        if [ "$lbs" = "e2elaps" ]; then
            for enable_laps_plus in "false" "true"; do
                for load in "${LOADRATIOS[@]}"; do
                    for topo in "${TOPODIRS[@]}"; do
                        log_prefix="C00001_${topo}_${workload}_Reduce-lr-${load}-lb-${lbs}"
                        if [ "$enable_laps_plus" = "true" ]; then
                            suffix="plus002"
                        else
                            suffix="origin"
                        fi
                        qpinfo_file="$LOG_DIR/${log_prefix}${suffix}-QpInfo.txt"
                        if [[ ! -f "$qpinfo_file" ]]; then
                            failed_tasks+=("$workload|$lbs|$enable_laps_plus|$load|$topo|$qpinfo_file")
                        fi
                    done
                done
            done
        else
            for load in "${LOADRATIOS[@]}"; do
                for topo in "${TOPODIRS[@]}"; do
                    log_prefix="C00001_${topo}_${workload}_Reduce-lr-${load}-lb-${lbs}"
                    qpinfo_file="$LOG_DIR/${log_prefix}-QpInfo.txt"
                    if [[ ! -f "$qpinfo_file" ]]; then
                        failed_tasks+=("$workload|$lbs|false|$load|$topo|$qpinfo_file")
                    fi
                done
            done
        fi
    done
done

# ================== 报告结果 ==================
if (( ${#failed_tasks[@]} == 0 )); then
    echo "🎉 All experiments have completed successfully! Nothing to rerun."
    exit 0
fi

echo ""
echo "⚠️  Found ${#failed_tasks[@]} incomplete experiments (missing -QpInfo.txt):"
echo "=================================================="
for item in "${failed_tasks[@]}"; do
    IFS='|' read -r _ _ _ _ _ qpinfo_file <<< "$item"
    echo "  ❌ $qpinfo_file"
done
echo "=================================================="
echo ""

# ================== 交互确认 ==================
read -p "❓ Do you want to rerun these ${#failed_tasks[@]} tasks? (Y/N): " -n 1 -r
echo

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "🛑 Rerun cancelled by user."
    exit 0
fi

echo "🔄 Starting to rerun ${#failed_tasks[@]} tasks..."

# ================== 实际重跑 ==================
pgids=()

for item in "${failed_tasks[@]}"; do
    IFS='|' read -r workload lbs enable_laps_plus load topo qpinfo_file <<< "$item"

    # 并发控制
    while (( ${#pgids[@]} >= MAX_JOBS )); do
        sleep 5
        new_pgids=()
        for pgid in "${pgids[@]}"; do
            if ps -eo pgid= | grep -q "^ *$pgid$"; then
                new_pgids+=("$pgid")
            fi
        done
        pgids=("${new_pgids[@]}")
    done

    # 日志文件
    safe_workload="${workload//[^a-zA-Z0-9._-]/_}"
    safe_lbs="${lbs//[^a-zA-Z0-9._-]/_}"
    log_file="$LOG_DIR/run_w${safe_workload}_l${safe_lbs}_elp${enable_laps_plus}_l${load}_t${topo}.log"

    # choose_softmax
    if [ "$lbs" = "e2elaps" ]; then
        choose_softmax=$(( enable_laps_plus == "true" ? 2 : 0 ))
    else
        choose_softmax=0
    fi

    echo "[$(date +'%H:%M:%S')] Launching: workload=$workload, lbs=$lbs, enable_laps_plus=$enable_laps_plus, load=$load, topo=$topo"

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
echo "⏳ Waiting for all rerun jobs to finish..."
while (( ${#pgids[@]} > 0 )); do
    sleep 10
    new_pgids=()
    for pgid in "${pgids[@]}"; do
        if ps -eo pgid= | grep -q "^ *$pgid$"; then
            new_pgids+=("$pgid")
        fi
    done
    pgids=("${new_pgids[@]}")
done

echo "✅ All rerun tasks completed successfully!"