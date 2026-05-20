from __future__ import annotations

import ctypes
import os
from pathlib import Path

import numpy as np

from routeopt.models import OptimizationRequest, OptimizationResponse, RouteResult


class NativeRouteOptimizer:
    def __init__(self, library_path: str | None = None) -> None:
        self.library_path = library_path or self._discover_library()
        self._lib = ctypes.CDLL(self.library_path) if self.library_path else None
        if self._lib is not None:
            self._configure_signatures()

    def optimize(self, request: OptimizationRequest) -> OptimizationResponse:
        if self._lib is None:
            return self._fallback_optimize(request)
        return self._native_optimize(request)

    def _native_optimize(self, request: OptimizationRequest) -> OptimizationResponse:
        nodes = [request.depot, *request.customers]
        ids = [node.id for node in nodes]
        xy = np.array([[node.x, node.y] for node in nodes], dtype=np.float64).reshape(-1)
        demand = np.array([node.demand for node in nodes], dtype=np.float64)

        max_nodes = len(nodes) + len(request.customers)
        max_routes = len(request.customers) + 1
        route_nodes = (ctypes.c_int * max_nodes)()
        route_offsets = (ctypes.c_int * (max_routes + 1))()
        route_count = ctypes.c_int()
        out_node_count = ctypes.c_int()
        total_distance = ctypes.c_double()
        feasible = ctypes.c_int()

        rc = self._lib.routeopt_optimize(
            xy.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            demand.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            len(nodes),
            0,
            request.vehicle.capacity,
            request.vehicle.max_route_time,
            request.settings.population_size,
            request.settings.generations,
            request.settings.seed,
            int(request.settings.prefer_gpu),
            route_nodes,
            max_nodes,
            route_offsets,
            max_routes + 1,
            ctypes.byref(route_count),
            ctypes.byref(out_node_count),
            ctypes.byref(total_distance),
            ctypes.byref(feasible),
        )
        if rc != 0:
            raise RuntimeError(f"native optimizer returned error code {rc}")

        routes: list[RouteResult] = []
        for route_idx in range(route_count.value):
            start = route_offsets[route_idx]
            end = route_offsets[route_idx + 1]
            node_indexes = [route_nodes[i] for i in range(start, end)]
            customer_ids = [ids[index] for index in node_indexes]
            load = sum(nodes[index].demand for index in node_indexes)
            routes.append(RouteResult(customer_ids=customer_ids, load=load))

        backend = "native-cpu"
        if request.settings.prefer_gpu:
            backend = (
                "native-opencl-distance-cpu-constraints"
                if self._opencl_available()
                else "native-cpu-opencl-unavailable"
            )
        return OptimizationResponse(
            routes=routes,
            total_distance=total_distance.value,
            feasible=bool(feasible.value),
            backend=backend,
        )

    def _fallback_optimize(self, request: OptimizationRequest) -> OptimizationResponse:
        routes: list[RouteResult] = []
        current: list[int] = []
        load = 0.0
        total = 0.0
        prev = request.depot
        for customer in sorted(request.customers, key=lambda item: (item.x, item.y)):
            if current and load + customer.demand > request.vehicle.capacity:
                total += _distance(prev, request.depot)
                routes.append(RouteResult(customer_ids=current, load=load))
                current = []
                load = 0.0
                prev = request.depot
            total += _distance(prev, customer)
            current.append(customer.id)
            load += customer.demand
            prev = customer
        if current:
            total += _distance(prev, request.depot)
            routes.append(RouteResult(customer_ids=current, load=load))
        return OptimizationResponse(routes=routes, total_distance=total, feasible=True, backend="python-fallback")

    def _configure_signatures(self) -> None:
        self._lib.routeopt_optimize.argtypes = [
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_double,
            ctypes.c_double,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_ulonglong,
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_int),
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_int),
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_int),
        ]
        self._lib.routeopt_optimize.restype = ctypes.c_int
        self._lib.routeopt_opencl_available.argtypes = []
        self._lib.routeopt_opencl_available.restype = ctypes.c_int

    def _opencl_available(self) -> bool:
        if self._lib is None:
            return False
        return bool(self._lib.routeopt_opencl_available())

    @staticmethod
    def _discover_library() -> str | None:
        explicit = os.getenv("ROUTEOPT_LIB")
        if explicit:
            return explicit
        root = Path(__file__).resolve().parents[2]
        candidates = [
            root / "build" / "librouteopt_core.so",
            root / "build" / "routeopt_core.dll",
            root / "build" / "Debug" / "routeopt_core.dll",
            root / "build" / "Release" / "routeopt_core.dll",
        ]
        return next((str(path) for path in candidates if path.exists()), None)


def _distance(lhs, rhs) -> float:
    return float(((lhs.x - rhs.x) ** 2 + (lhs.y - rhs.y) ** 2) ** 0.5)
