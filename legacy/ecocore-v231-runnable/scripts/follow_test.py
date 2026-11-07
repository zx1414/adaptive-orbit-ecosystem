#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""follow_coupling 验证：小世界快核狂奔，多种子平均，看归属球被甩多远。"""
import csv
import math
import subprocess
import shutil
from pathlib import Path

root = Path(__file__).resolve().parent.parent
EXE = str(root / "simulator.exe")
TMP = root / ".fbtest"
NL = chr(10)
shutil.rmtree(TMP, ignore_errors=True)
TMP.mkdir()


def run(fc, seed):
    d = TMP / ("fc" + str(fc).replace(".", "_") + "_s" + str(seed))
    d.mkdir()
    env = ["visualization = off", "pause_on_exit = off", "render_interval = 0",
           "width = 200", "height = 200", "balls = 5", "nuclei = 1",
           "frames = 150", "basal_cost = 0", "speed_cost_k = 0", "wander_k = 30",
           "forage_k = 0", "ball_loss_cost = 0", "follow_boost = 1.5",
           "follow_coupling = " + str(fc), "sample_interval = 10"]
    (d / "e.txt").write_text(NL.join(env) + NL, encoding="utf-8")
    subprocess.run([EXE, "--env", "e.txt", "--seed", str(seed)],
                   cwd=str(d), capture_output=True)
    np_ = d / "nuclei_000150.csv"
    bp = d / "balls_000150.csv"
    with open(np_, newline="", encoding="utf-8") as fh:
        nucs = [(float(r["x"]), float(r["y"])) for r in csv.DictReader(fh)]
    if not nucs:
        return None, None
    nx, ny = nucs[0]
    owned = []
    free = []
    with open(bp, newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            dd = math.hypot(float(row["x"]) - nx, float(row["y"]) - ny)
            if int(row["ownerId"]) >= 0:
                owned.append(dd)
            else:
                free.append(dd)
    avg_o = sum(owned) / len(owned) if owned else 0.0
    return avg_o, len(owned)


for fc in (0.0, 15.0):
    avgs = []
    counts = []
    for seed in (1, 2, 3):
        a, c = run(fc, seed)
        avgs.append(a)
        counts.append(c)
    print(f"follow_coupling={fc}: 归属球平均距离={sum(avgs)/len(avgs):.1f} "
          f"（3种子，归属数 {min(counts)}~{max(counts)}）")
