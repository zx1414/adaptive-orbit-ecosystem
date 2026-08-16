#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase 0.5 验收测试：验证 6 项行为并输出实测数字。"""
import csv
import math
import subprocess
from pathlib import Path

EXE = str(Path(__file__).resolve().parent.parent / "simulator.exe")
TMP = Path(__file__).resolve().parent.parent / ".p05tmp"
NL = chr(10)


def write_env(path: Path, **kv):
    lines = ["visualization = off", "pause_on_exit = off", "render_interval = 0"]
    lines += [f"{k} = {v}" for k, v in kv.items()]
    path.write_text(NL.join(lines) + NL, encoding="utf-8")


def nuc_line(x, y, energy, atk=100.0, ar=200.0, avs=0.0, avr=0.0, msp=10.0, th=9999.0):
    return (f"{x} {y} {energy} 0.33 0.33 0.34 100 100 100 1.0 1.0 1.0 "
            f"1.0 1.0 1.0 {ar} {atk} {avr} {avs} {msp} {th} 0.05")


def write_cfg(path: Path, lines):
    path.write_text(NL.join(lines) + NL, encoding="utf-8")


def run(exe_args, cwd):
    return subprocess.run(exe_args, cwd=str(cwd), capture_output=True, text=True,
                         encoding="utf-8", errors="replace")


def read_nuclei(path: Path):
    rows = []
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append((float(r["x"]), float(r["y"]), float(r["energy"])))
    return rows


def read_balls(path: Path):
    rows = []
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append((float(r["x"]), float(r["y"]), int(r["type"]), int(r["ownerId"])))
    return rows


def dist(p, q):
    return math.hypot(p[0] - q[0], p[1] - q[1])


def main():
    TMP.mkdir(exist_ok=True)
    print("exe:", EXE)

    print()
    print("=== 1) 确定性（同种子两次）===")
    for d in ("detA", "detB"):
        (TMP / d).mkdir(exist_ok=True)
    args0 = [EXE, "--balls", "100", "--nuclei", "5", "--frames", "200",
             "--render-interval", "0", "--sample-interval", "0", "--seed", "42"]
    run(args0, TMP / "detA")
    run(args0, TMP / "detB")
    ha = (TMP / "detA" / "survivors.csv").read_bytes()
    hb = (TMP / "detB" / "survivors.csv").read_bytes()
    print("survivors.csv 一致:", ha == hb)

    print()
    print("=== 2) 新生保护期（强核 vs 弱核，grace=30）===")
    d = TMP / "grace"
    d.mkdir(exist_ok=True)
    write_env(d / "e.txt", balls=0, basal_cost=0, speed_cost_k=0,
              wander_k=0, forage_k=0, newborn_grace_frames=30, sample_interval=10)
    write_cfg(d / "c.txt", [
        nuc_line(500, 500, 500, atk=100.0),
        nuc_line(500, 550, 500, atk=1.0),
    ])
    run([EXE, "--env", "e.txt", "--config", "c.txt", "--nuclei", "0", "--frames", "60"], d)
    for f in (10, 30, 40, 60):
        rows = read_nuclei(d / f"nuclei_{f:06d}.csv")
        strong, weak = (rows[0], rows[1]) if rows[0][2] >= rows[1][2] else (rows[1], rows[0])
        print(f"  frame {f:2d}: 弱核能量={weak[2]:.1f}（强核={strong[2]:.1f}）")

    print()
    print("=== 3) 两核贴脸（min_separation=0 vs 40）===")
    for tag, sep in (("无修复(0)", 0.0), ("修复(40)", 40.0)):
        d = TMP / ("face0" if sep == 0.0 else "face40")
        d.mkdir(exist_ok=True)
        write_env(d / "e.txt", balls=0, basal_cost=0, speed_cost_k=0,
                  wander_k=0, forage_k=0, newborn_grace_frames=0,
                  nucleus_min_separation=sep, nucleus_repel_k=4.0, sample_interval=5)
        write_cfg(d / "c.txt", [
            nuc_line(500, 500, 1000, atk=100.0, ar=200.0, msp=20.0),
            nuc_line(500, 515, 1000, atk=100.0, ar=200.0, msp=20.0),
        ])
        run([EXE, "--env", "e.txt", "--config", "c.txt", "--nuclei", "0", "--frames", "100"], d)
        mind = 1e9
        maxd = 0.0
        overlap = 0
        nframes = 0
        for f in range(5, 101, 5):
            rows = read_nuclei(d / f"nuclei_{f:06d}.csv")
            if len(rows) < 2:
                continue
            dd = dist(rows[0], rows[1])
            nframes += 1
            mind = min(mind, dd)
            maxd = max(maxd, dd)
            if dd < 5.0:
                overlap += 1
        print(f"  {tag}: 最小间距={mind:.1f} 最大间距={maxd:.1f} 重叠帧(<5)={overlap}/{nframes}")

    print()
    print("=== 4) 跟随加速（快核 + 慢球，follow_boost=0 vs 1.5）===")
    for tag, fb in (("boost=0", 0.0), ("boost=1.5", 1.5)):
        d = TMP / ("fb0" if fb == 0.0 else "fb15")
        d.mkdir(exist_ok=True)
        write_env(d / "e.txt", balls=30, basal_cost=0, speed_cost_k=0,
                  wander_k=30, forage_k=0, follow_boost=fb, ball_loss_cost=0,
                  sample_interval=20, max_speed_init="200, 200")
        run([EXE, "--env", "e.txt", "--nuclei", "1", "--frames", "200"], d)
        max_owned = 0.0
        owned_cnt = 0
        sum_owned = 0.0
        for f in range(20, 201, 20):
            nrows = read_nuclei(d / f"nuclei_{f:06d}.csv")
            brows = read_balls(d / f"balls_{f:06d}.csv")
            if not nrows:
                continue
            npos = (nrows[0][0], nrows[0][1])
            for b in brows:
                if b[3] >= 0:
                    owned_cnt += 1
                    dd = dist((b[0], b[1]), npos)
                    sum_owned += dd
                    max_owned = max(max_owned, dd)
        avg = sum_owned / owned_cnt if owned_cnt else 0.0
        print(f"  {tag}: 归属球距核 平均={avg:.1f} 最大={max_owned:.1f}（归属采样点={owned_cnt}）")

    print()
    print("=== 5) 失球惩罚（甩球，ball_loss_cost=0 vs 5）===")
    for tag, cost in (("cost=0", 0.0), ("cost=5", 5.0)):
        d = TMP / ("lc0" if cost == 0.0 else "lc5")
        d.mkdir(exist_ok=True)
        write_env(d / "e.txt", balls=30, basal_cost=0, speed_cost_k=0,
                  wander_k=30, forage_k=0, follow_boost=0, ball_loss_cost=cost,
                  sample_interval=20, max_speed_init="200, 200")
        run([EXE, "--env", "e.txt", "--nuclei", "1", "--frames", "300"], d)
        e0 = read_nuclei(d / "nuclei_000020.csv")[0][2]
        last_f = 20
        last_e = e0
        for f in range(40, 301, 20):
            p = d / f"nuclei_{f:06d}.csv"
            if p.exists():
                rows = read_nuclei(p)
                if rows:
                    last_f = f
                    last_e = rows[0][2]
        print(f"  {tag}: 能量 frame20={e0:.1f} -> frame{last_f}={last_e:.1f}（变化 {last_e-e0:.1f}）")

    print()
    print("完成。临时文件在", TMP)


if __name__ == "__main__":
    main()
