from __future__ import annotations

import argparse
import json
import random
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--customers", type=int, default=100)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--output", default="data/scenario.json")
    args = parser.parse_args()

    rng = random.Random(args.seed)
    payload = {
        "depot": {"id": 0, "x": 50.0, "y": 50.0, "demand": 0.0},
        "customers": [
            {
                "id": index,
                "x": rng.uniform(0, 100),
                "y": rng.uniform(0, 100),
                "demand": rng.randint(1, 8),
            }
            for index in range(1, args.customers + 1)
        ],
        "vehicle": {"capacity": 35.0, "max_route_time": 260.0},
        "settings": {"population_size": 128, "generations": 350, "seed": args.seed},
    }
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
