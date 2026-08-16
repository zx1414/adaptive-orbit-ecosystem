#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""追击甩球测试：A 先慢后快地追击 B（加速阶段甩球），看归属球是否跟得住。"""
import csv
import math
import subprocess
import shutil
from pathlib import Path

root = Path(__file__).resolve().parent.parent
EXE = str(root / "simulator.exe")
TMP = root / ".chase"
NL = chr(10)
shutil.rmtree(TMP, ignore_errors=True)
TMP.mkdir()


def run(fc, seed):
    d = TMP / ("fc" + str(fc).replace(".", "_") + "_s" + str(seed))
    d.mkdir()
    env = ["visualization = off", "pause_on_exit = off", "render_interval = 0",
           "balls = 40", "basal_cost = 0", "speed_cost_k = 0", "wander_k = 0",
           "forage_k = 0", "ball_loss_cost = 0", "follow_boost = 1.5",
           "follow_coupling = " + str(fc), "sample_interval = 10"]
    (d / "e.txt").write_text(NL.join(env) + NL, encoding="utf-8")
    # A: 追击者，attackRange=1200，maxSpeed=200；B: 静止靶
    cfg = ["500 500 2000 0.33 0.33 0.34 100 100 100 1.0 1.0 1.0 1.0 1.0 1.0 "
           "1200 100 0 0 200 9999 0.05",
           "500 1500 2000 0.33 0.33 0.34 100 100 100 1.0 1.0 1.0 1.0 1.0 1.0 "
           "0 0 0 0 0 9999 0.05"]
    (d / "c.txt").write_text(NL.join(cfg) + NL, encoding="utf-8")
    subprocess.run([EXE, "--env", "e.txt", "--config", "c.txt", "--nuclei", "0",
                    "--frames", "150", "--seed", str(seed)],
                   cwd=str(d), capture_output=True)
    # 追 A 的归属球最大距离（A 是 attackStrength=100 的那个，ownerId=0 是第一个注入的核）
    maxd = 0.0
    for f in range(10, 151, 10):
        np_ = d / f"nuclei_{f:06d}.csv"
        bp = d / f"balls_{f:06d}.csv"
        if not np_.exists() or not bp.exists():
            continue
        with open(np_, newline="", encoding="utf-8") as fh:
            nucs = [(float(r["x"]), float(r["y"])) for r in csv.DictReader(fh)]
        if len(nucs) < 2:
            continue
        with open(bp, newline="", encoding="utf-8") as fh:
            for row in csv.DictReader(fh):
                oid = int(row["ownerId"])
                if 0 <= oid < len(nucs):
                    dd = math.hypot(float(row["x"]) - nucs[oid][0],
                                    float(row["y"]) - nucs[oid][1])
                    maxd = max(maxd, dd)
    return maxd


for fc in (0.0, 15.0, 30.0, 50.0):
    vals = [run(fc, s) for s in (1, 2, 3)]
    print(f"follow_coupling={fc}: 归属球距各自核的最大距离(3种子) = "
          + ", ".join(f"{v:.0f}" for v in vals))
