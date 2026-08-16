#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
读取 trend.csv（frame + 核参数时间序列），输出逐帧统计摘要，便于观察参数演化趋势。

用法：
    python scripts/trend_summary.py --input output/trend.csv --out output/trend_summary.csv

输出列：frame, alive_count, mean_<param>, std_<param> （22 个参数各一组均值/标准差）。
"""
import argparse
import csv
from collections import defaultdict

import numpy as np

PARAM_COLS = [
    "affinity0", "affinity1", "affinity2",
    "orbitRadius0", "orbitRadius1", "orbitRadius2",
    "absorbPreference0", "absorbPreference1", "absorbPreference2",
    "repelStrength0", "repelStrength1", "repelStrength2",
    "attackRange", "attackStrength", "avoidRange", "avoidStrength",
    "maxSpeed", "energyThreshold", "mutationRate",
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", default="output/trend.csv")
    ap.add_argument("--out", default="output/trend_summary.csv")
    args = ap.parse_args()

    by_frame = defaultdict(list)
    with open(args.input, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            frame = int(row["frame"])
            by_frame[frame].append([float(row[c]) for c in PARAM_COLS])

    frames = sorted(by_frame.keys())
    with open(args.out, "w", newline="", encoding="utf-8") as f:
        cols = ["frame", "alive_count"]
        for c in PARAM_COLS:
            cols += [f"mean_{c}", f"std_{c}"]
        w = csv.writer(f)
        w.writerow(cols)
        for frame in frames:
            X = np.array(by_frame[frame])
            row = [frame, X.shape[0]]
            for t in range(len(PARAM_COLS)):
                row += [f"{X[:, t].mean():.6f}", f"{X[:, t].std():.6f}"]
            w.writerow(row)

    print(f"已写出 {len(frames)} 个采样帧的统计摘要到 {args.out}")


if __name__ == "__main__":
    main()
