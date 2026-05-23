from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt

from routeopt.models import OptimizationRequest, OptimizationResponse


def render_routes(request: OptimizationRequest, response: OptimizationResponse, output: str | Path) -> Path:
    output_path = Path(output)
    nodes = {request.depot.id: request.depot, **{customer.id: customer for customer in request.customers}}

    fig, ax = plt.subplots(figsize=(9, 7))
    ax.scatter([request.depot.x], [request.depot.y], marker="s", s=120, label="depot")
    ax.scatter([c.x for c in request.customers], [c.y for c in request.customers], s=40, label="customers")

    for route in response.routes:
        xs = [request.depot.x]
        ys = [request.depot.y]
        for customer_id in route.customer_ids:
            xs.append(nodes[customer_id].x)
            ys.append(nodes[customer_id].y)
        xs.append(request.depot.x)
        ys.append(request.depot.y)
        ax.plot(xs, ys, linewidth=1.5)

    ax.set_title(f"RouteOpt: distance={response.total_distance:.2f} km, routes={len(response.routes)}")
    ax.set_xlabel("x, km")
    ax.set_ylabel("y, km")
    ax.legend()
    ax.grid(True, alpha=0.3)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(output_path, dpi=160)
    plt.close(fig)
    return output_path
