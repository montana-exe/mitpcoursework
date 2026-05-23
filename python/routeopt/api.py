from __future__ import annotations

from pathlib import Path

from fastapi import FastAPI
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from routeopt.core import NativeRouteOptimizer
from routeopt.models import OptimizationRequest, OptimizationResponse

app = FastAPI(title="RouteOpt API", version="0.1.0")
optimizer = NativeRouteOptimizer()
static_dir = Path(__file__).parent / "static"
app.mount("/static", StaticFiles(directory=static_dir), name="static")


@app.get("/", include_in_schema=False)
def interface() -> FileResponse:
    return FileResponse(static_dir / "index.html")


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok", "native_library": "loaded" if optimizer.library_path else "not-found"}


@app.post("/optimize", response_model=OptimizationResponse)
def optimize(request: OptimizationRequest) -> OptimizationResponse:
    return optimizer.optimize(request)
