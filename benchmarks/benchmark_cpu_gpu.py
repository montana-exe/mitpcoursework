from __future__ import annotations

import argparse
import json
import subprocess
import time
from pathlib import Path


def run(cli: Path, backend: str, customers: int, generations: int) -> dict[str, object]:
    started = time.perf_counter()
    completed = subprocess.run(
        [
            str(cli),
            "--backend",
            backend,
            "--customers",
            str(customers),
            "--generations",
            str(generations),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    elapsed = time.perf_counter() - started
    if completed.returncode not in (0, 2):
        raise RuntimeError(completed.stderr)
    payload = json.loads(completed.stdout)
    payload["requested_backend"] = backend
    payload["elapsed_seconds"] = elapsed
    return payload


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", default="build/routeopt_cli")
    parser.add_argument("--customers", type=int, default=250)
    parser.add_argument("--generations", type=int, default=300)
    parser.add_argument("--output", default="benchmarks/results.json")
    args = parser.parse_args()

    cli = Path(args.cli)
    results = [
        run(cli, "cpu", args.customers, args.generations),
        run(cli, "gpu", args.customers, args.generations),
    ]
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(results, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
