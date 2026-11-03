import os
import re
import numpy as np
import matplotlib.pyplot as plt

# ================= 配置 =================
INPUT_DIR = "/file-in-ctr/outputFiles/C00001/"

FILENAME_PATTERN = re.compile(
    r'^C00001_dragonfly_RPC_CDF_All-lr-1\.0-lb-'
    r'(?P<algo>[^-]+)'
    r'-QpInfo\.txt$'
)

OUTPUT_PREFIX = "/file-in-ctr/PNG/PFC/pfc"

# ================= 日志解析 =================
def parse_qpinfo_file(filepath):
    pfc, fct, pause = [], [], []
    kv_pattern = re.compile(r'(\w+)=([^,]+)')

    with open(filepath, 'r') as f:
        for line in f:
            kvs = dict(kv_pattern.findall(line))

            if 'pfcDuration' not in kvs or 'FCT' not in kvs or 'pauseCount' not in kvs:
                continue

            try:
                pfc_dur = float(kvs['pfcDuration'])
                fct_val = float(kvs['FCT'])
                pause_cnt = float(kvs['pauseCount'])

                if fct_val <= 0:
                    continue

                pfc.append(pfc_dur)
                fct.append(fct_val)
                pause.append(pause_cnt)

            except ValueError:
                continue

    return pfc, fct, pause


# ================= CDF 绘制 =================
def plot_multi_cdf(data_dict, xlabel, title, output_path):
    plt.figure(figsize=(6.5, 4.5))

    for algo, data in data_dict.items():
        if len(data) == 0:
            continue
        x = np.sort(np.array(data))
        y = np.arange(1, len(x) + 1) / len(x)
        plt.plot(x, y, label=algo)

    plt.xlabel(xlabel)
    plt.ylabel("CDF")
    plt.title(title)
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()


# ================= 主流程 =================
def main():
    pfc_map = {}
    ratio_map = {}
    pause_map = {}

    for fname in os.listdir(INPUT_DIR):
        match = FILENAME_PATTERN.match(fname)
        if not match:
            continue

        algo = match.group("algo")
        filepath = os.path.join(INPUT_DIR, fname)

        pfc, fct, pause = parse_qpinfo_file(filepath)

        pfc_map[algo] = pfc
        ratio_map[algo] = [pfc[i] / fct[i] for i in range(len(pfc))]
        pause_map[algo] = pause

        print(f"[INFO] {algo}: {len(pfc)} valid flows")

    os.makedirs(os.path.dirname(OUTPUT_PREFIX), exist_ok=True)

    plot_multi_cdf(
        pfc_map,
        xlabel="pfcDuration",
        title="CDF of PFC Duration",
        output_path=OUTPUT_PREFIX + "_pfcDuration_cdf.pdf"
    )

    plot_multi_cdf(
        ratio_map,
        xlabel="pfcDuration / FCT",
        title="CDF of PFC Impact Ratio",
        output_path=OUTPUT_PREFIX + "_pfcRatio_cdf.pdf"
    )

    plot_multi_cdf(
        pause_map,
        xlabel="pauseCount",
        title="CDF of Pause Count",
        output_path=OUTPUT_PREFIX + "_pauseCount_cdf.pdf"
    )

    print("[INFO] All CDF figures generated.")


if __name__ == "__main__":
    main()
