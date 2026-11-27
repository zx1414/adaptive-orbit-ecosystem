"""Python client for the agentsim navigation research protocol."""

from __future__ import annotations

import json
import math
import subprocess
from collections.abc import Mapping, Sequence
from pathlib import Path


class AgentSimEnv:
    def __init__(self, executable: str | Path, extensions_dir: str | Path | None = None):
        self.executable = Path(executable).resolve()
        self.extensions_dir = Path(extensions_dir).resolve() if extensions_dir else None
        self.process = None
        self.frame = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def reset(
        self, seed: int, scenario: str = "standard", *, ticks: int = 1000,
        nuclei: int = 12, cyano: int = 48, audit: bool = False,
    ) -> dict:
        self.close()
        args = [
            str(self.executable), "--research", "--seed", str(seed), "--mod", scenario,
            "--ticks", str(ticks), "--nuclei", str(nuclei), "--cyano-nodes", str(cyano),
        ]
        if self.extensions_dir:
            args.extend(("--extensions-dir", str(self.extensions_dir)))
        if audit:
            args.append("--audit")
        self.process = subprocess.Popen(
            args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8",
        )
        try:
            self.frame = self._read()
        except Exception:
            self.close()
            raise
        return self.frame

    def step(self, actions: Mapping[str, Sequence[float]]) -> dict:
        """Submit (direction x, direction y, speed scale, boost) by observed ID."""
        frame = self.frame
        if not frame or frame["terminated"] or frame["truncated"] or not frame["audit_ok"]:
            raise RuntimeError("Call reset before stepping an episode.")
        ids = {agent["id"] for agent in frame["observations"]}
        fields = ["step", str(len(actions))]
        for agent_id, action in actions.items():
            if agent_id not in ids:
                raise ValueError(f"Unknown observed agent ID: {agent_id}")
            x, y, speed, boost = action
            if not (-1 <= x <= 1 and -1 <= y <= 1 and 0 <= speed <= 1 and 0 <= boost <= 1):
                raise ValueError("Direction coordinates must be in [-1,1], speed and boost in [0,1].")
            fields.extend((agent_id, *(format(value, ".17g") for value in action)))
        self._send(" ".join(fields))
        self.frame = self._read()
        return self.frame

    def close(self) -> None:
        process = self.process
        self.frame = None
        if process is None:
            return
        try:
            if process.poll() is None:
                try:
                    self._send("close")
                except BrokenPipeError:
                    pass
            process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
        finally:
            self.process = None

    def _send(self, command: str) -> None:
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def _read(self) -> dict:
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError(f"Simulation closed the research stream (exit {self.process.wait()}).")
        frame = json.loads(line)
        if "error" in frame:
            raise ValueError(frame["error"])
        if frame.get("version") != 1:
            raise RuntimeError("Unsupported research protocol version.")
        return frame


def export_perceptron(path: str | Path, weights: Sequence[Sequence[float]],
                      biases: Sequence[float]) -> None:
    """Write a trained 24-input, 2-output linear model for the game menu."""
    if len(weights) != 2 or len(biases) != 2 or any(len(row) != 24 for row in weights):
        raise ValueError("Expected two rows of 24 weights and two biases.")
    lines = ["perceptron 2 24"]
    for bias, row in zip(biases, weights):
        values = [bias, *row]
        if not all(math.isfinite(value) for value in values):
            raise ValueError("The model contains non-finite parameters.")
        lines.append(" ".join(format(value, ".17g") for value in values))
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("\n".join(lines) + "\n", encoding="utf-8")
