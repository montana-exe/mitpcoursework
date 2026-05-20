from __future__ import annotations

from fastapi import FastAPI

from routeopt.core import NativeRouteOptimizer
from routeopt.models import OptimizationRequest, OptimizationResponse

app = FastAPI(title="RouteOpt API", version="0.1.0")
optimizer = NativeRouteOptimizer()


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok", "native_library": "loaded" if optimizer.library_path else "not-found"}


@app.post("/optimize", response_model=OptimizationResponse)
def optimize(request: OptimizationRequest) -> OptimizationResponse:
    return optimizer.optimize(request)
