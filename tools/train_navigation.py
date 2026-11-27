"""Train a single-layer model to imitate local nearest-food steering."""

from __future__ import annotations

import argparse
import math
from pathlib import Path
from random import Random

from agentsim_env import AgentSimEnv, export_perceptron


def train(env: AgentSimEnv, seed: int, scenario: str, ticks: int):
    random = Random(seed)
    weights = [[random.uniform(-0.01, 0.01) for _ in range(24)] for _ in range(2)]
    biases = [0.0, 0.0]
    sample_count = 0
    frame = env.reset(seed, scenario, ticks=ticks)
    while not (frame["terminated"] or frame["truncated"]):
        actions = {}
        for agent in frame["observations"]:
            sensors = agent["sensors"]
            if agent["food_ids"][0] is None:
                angle = random.uniform(0.0, math.tau)
                actions[agent["id"]] = (math.cos(angle), math.sin(angle), 0.25, 0.0)
                continue
            direction = sensors[:2]
            for axis in range(2):
                prediction = biases[axis] + sum(w * s for w, s in zip(weights[axis], sensors))
                gradient = 0.015 * (prediction - direction[axis])
                biases[axis] -= gradient
                for i, sensor in enumerate(sensors):
                    weights[axis][i] -= gradient * sensor
            near_food = agent["food_distances"][0] <= 0.85 * agent["bite_radius"]
            actions[agent["id"]] = (*direction, 0.0 if near_food else 0.65, 0.0)
            sample_count += 1
        frame = env.step(actions)
    if sample_count == 0:
        raise RuntimeError("No food observations were collected. Use more ticks or another seed.")
    return weights, biases, sample_count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default="build/agentsim.exe")
    parser.add_argument("--extensions-dir")
    parser.add_argument("--mod", default="standard")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--ticks", type=int, default=300)
    parser.add_argument("--output", type=Path, default=Path("models/nearest_food.txt"))
    args = parser.parse_args()
    with AgentSimEnv(args.exe, args.extensions_dir) as env:
        weights, biases, samples = train(env, args.seed, args.mod, args.ticks)
    export_perceptron(args.output, weights, biases)
    print(f"{args.output}: trained on {samples} food observations")


if __name__ == "__main__":
    main()
