#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
批量运行模拟 -> 收集存活核参数 -> k-means 聚类角色原型（仅依赖 numpy，无 sklearn）。

用法示例：
    python scripts/batch_cluster.py --exe simulator.exe --runs 6 \
        --balls 1000 --nuclei 50 --frames 5000 --trend-interval 50 \
        --out output/batch --k 4

输出：
    <out>/run_<seed>/trend.csv        每场的时间序列（frame + 核参数）
    <out>/run_<seed>/survivors.csv    每场最终存活核
    <out>/all_survivors.csv           汇总所有场次的存活核（含 seed 列）
    <out>/archetypes.csv              k 个角色原型（聚类中心，原始量纲）
    <out>/assignments.csv             每个存活核的聚类归属
"""
import argparse
import csv
import subprocess
import sys
from pathlib import Path

import numpy as np

# 参与聚类的 22 个遗传参数（与 nuclei_*.csv / trend.csv 列名一致）
PARAM_COLS = [
    "affinity0", "affinity1", "affinity2",
    "orbitRadius0", "orbitRadius1", "orbitRadius2",
    "absorbPreference0", "absorbPreference1", "absorbPreference2",
    "repelStrength0", "repelStrength1", "repelStrength2",
    "attackRange", "attackStrength", "avoidRange", "avoidStrength",
    "maxSpeed", "energyThreshold", "mutationRate",
]


def run_one(exe: str, out_dir: Path, seed: int, balls: int, nuclei: int,
            frames: int, trend_interval: int, config: str) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        exe,
        "--balls", str(balls),
        "--nuclei", str(nuclei),
        "--frames", str(frames),
        "--render-interval", "0",
        "--sample-interval", "0",
        "--max-fps", "0",
        "--seed", str(seed),
        "--trend-csv", "trend.csv",
        "--trend-interval", str(trend_interval),
    ]
    if config:
        cmd += ["--config", str(config)]
    proc = subprocess.run(cmd, cwd=str(out_dir), capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(f"[batch] seed={seed} 运行失败:\n{proc.stderr}", file=sys.stderr)
        return
    print(f"[batch] seed={seed} 完成")


def load_survivors(path: Path):
    """读取 survivors.csv，返回 (rows, params) —— rows 为带原始字段的字典列表。"""
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    params = np.array([[float(r[c]) for c in PARAM_COLS] for r in rows])
    return rows, params


def zscore(X):
    mu = X.mean(axis=0)
    sd = X.std(axis=0)
    sd[sd < 1e-12] = 1.0
    return (X - mu) / sd, mu, sd


def kmeans(X, k, seed=0, iters=200):
    """k-means++ 初始化的标准 k-means。返回 (labels, centers_standardized)。"""
    rng = np.random.default_rng(seed)
    n = X.shape[0]
    centers = [X[int(rng.integers(n))]]
    for _ in range(1, k):
        d2 = np.min([((X - c) ** 2).sum(axis=1) for c in centers], axis=0)
        d2 = np.maximum(d2, 1e-12)
        probs = d2 / d2.sum()
        centers.append(X[int(rng.choice(n, p=probs))])
    centers = np.array(centers)
    labels = np.zeros(n, dtype=int)
    for _ in range(iters):
        d2 = ((X[:, None, :] - centers[None, :, :]) ** 2).sum(axis=2)
        labels = d2.argmin(axis=1)
        new_centers = np.array([
            X[labels == j].mean(axis=0) if (labels == j).sum() > 0 else centers[j]
            for j in range(k)
        ])
        if np.allclose(centers, new_centers):
            centers = new_centers
            break
        centers = new_centers
    d2 = ((X[:, None, :] - centers[None, :, :]) ** 2).sum(axis=2)
    labels = d2.argmin(axis=1)
    return labels, centers


def archetype_label(c: dict) -> str:
    """根据聚类中心给角色起一个可读的启发式标签（主型 + 副特征）。"""
    atk = c["attackStrength"]
    avd = c["avoidStrength"]
    orb = max(c["orbitRadius0"], c["orbitRadius1"], c["orbitRadius2"])
    absorb = max(c["absorbPreference0"], c["absorbPreference1"], c["absorbPreference2"])
    spd = c["maxSpeed"]

    if atk < 60:
        base = "和平型"
    elif atk >= 200:
        base = "捕食型"
    else:
        base = "竞争型"

    traits = []
    if orb >= 150:
        traits.append("远轨")
    elif orb <= 70:
        traits.append("近距")
    if absorb >= 1.5:
        traits.append("高吸收")
    if avd >= 50:
        traits.append("高避让")
    if spd >= 130:
        traits.append("高速")
    if not traits:
        traits.append("均衡")
    return base + "-" + "-".join(traits)


def main():
    ap = argparse.ArgumentParser(description="批量运行 + 聚类角色原型")
    ap.add_argument("--exe", default="simulator.exe")
    ap.add_argument("--runs", type=int, default=6)
    ap.add_argument("--seed-start", type=int, default=100)
    ap.add_argument("--balls", type=int, default=1000)
    ap.add_argument("--nuclei", type=int, default=50)
    ap.add_argument("--frames", type=int, default=5000)
    ap.add_argument("--trend-interval", type=int, default=50)
    ap.add_argument("--k", type=int, default=4)
    ap.add_argument("--config", default="")
    ap.add_argument("--out", default="output/batch")
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    all_rows = []
    for i in range(args.runs):
        seed = args.seed_start + i
        run_dir = out / f"run_{seed}"
        run_one(args.exe, run_dir, seed, args.balls, args.nuclei,
                args.frames, args.trend_interval, args.config)
        surv = run_dir / "survivors.csv"
        if surv.exists():
            rows, _ = load_survivors(surv)
            for r in rows:
                r["seed"] = str(seed)
                all_rows.append(r)

    if len(all_rows) == 0:
        print("没有收集到任何存活核，请检查运行参数。", file=sys.stderr)
        sys.exit(1)

    # 汇总所有场次
    fieldnames = ["seed"] + PARAM_COLS
    with open(out / "all_survivors.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in all_rows:
            w.writerow({k: r[k] for k in fieldnames})

    X = np.array([[float(r[c]) for c in PARAM_COLS] for r in all_rows])
    Z, mu, sd = zscore(X)
    k = min(args.k, X.shape[0])
    labels, centers_z = kmeans(Z, k, seed=42)

    # 中心还原到原始量纲
    centers = centers_z * sd + mu

    # 输出角色原型
    with open(out / "archetypes.csv", "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(["cluster", "count", "fraction", "label"] + PARAM_COLS)
        for j in range(k):
            cnt = int((labels == j).sum())
            frac = cnt / len(all_rows)
            row = dict(zip(PARAM_COLS, centers[j]))
            w.writerow([j, cnt, f"{frac:.4f}", archetype_label(row)] +
                       [f"{centers[j, t]:.6f}" for t in range(len(PARAM_COLS))])

    # 输出每个存活核的聚类归属
    with open(out / "assignments.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["seed", "cluster"] + PARAM_COLS)
        for idx, r in enumerate(all_rows):
            w.writerow([r["seed"], int(labels[idx])] +
                       [r[c] for c in PARAM_COLS])

    print(f"\n共收集 {len(all_rows)} 个存活核，聚类为 {k} 个角色原型：")
    for j in range(k):
        row = dict(zip(PARAM_COLS, centers[j]))
        cnt = int((labels == j).sum())
        print(f"  原型 {j} ({archetype_label(row)}): {cnt} 个核 "
              f"({cnt/len(all_rows)*100:.1f}%)  "
              f"attackStrength={row['attackStrength']:.1f} "
              f"orbitRadius.max={max(row['orbitRadius0'],row['orbitRadius1'],row['orbitRadius2']):.1f} "
              f"avoidStrength={row['avoidStrength']:.1f} "
              f"maxSpeed={row['maxSpeed']:.1f}")


if __name__ == "__main__":
    main()
