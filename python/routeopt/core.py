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
        service_time = np.array([node.service_time for node in nodes], dtype=np.float64)

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
            service_time.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            len(nodes),
            0,
            request.vehicle.capacity,
            request.vehicle.max_route_time,
            request.vehicle.average_speed_kmh,
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
            routes.append(_route_result(request, customer_ids))

        backend = "native-cpu"
        if request.settings.prefer_gpu:
            backend = (
                "native-opencl-fitness"
                if self._opencl_available()
                else "native-cpu-opencl-unavailable"
            )
        return OptimizationResponse(
            routes=routes,
            total_distance=total_distance.value,
            total_duration=sum(route.duration for route in routes),
            feasible=bool(feasible.value),
            backend=backend,
        )

    def _fallback_optimize(self, request: OptimizationRequest) -> OptimizationResponse:
        routes: list[RouteResult] = []
        current: list[int] = []
        feasible = True
        for customer in sorted(request.customers, key=lambda item: (item.x, item.y)):
            candidate = _route_result(request, [*current, customer.id])
            exceeds_capacity = candidate.load > request.vehicle.capacity
            exceeds_time = _exceeds_time(request, candidate)
            if current and (exceeds_capacity or exceeds_time):
                routes.append(_route_result(request, current))
                current = []
            current.append(customer.id)
        if current:
            routes.append(_route_result(request, current))
        for route in routes:
            if route.load > request.vehicle.capacity or _exceeds_time(request, route):
                feasible = False
        return OptimizationResponse(
            routes=routes,
            total_distance=sum(route.distance for route in routes),
            total_duration=sum(route.duration for route in routes),
            feasible=feasible,
            backend="python-fallback",
        )

    def _configure_signatures(self) -> None:
        self._lib.routeopt_optimize.argtypes = [
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_double,
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


def _route_result(request: OptimizationRequest, customer_ids: list[int]) -> RouteResult:
    customers = {customer.id: customer for customer in request.customers}
    points = [request.depot, *[customers[customer_id] for customer_id in customer_ids], request.depot]
    distance = sum(_distance(points[index], points[index + 1]) for index in range(len(points) - 1))
    load = sum(customers[customer_id].demand for customer_id in customer_ids)
    service_time = sum(customers[customer_id].service_time for customer_id in customer_ids)
    duration = distance / request.vehicle.average_speed_kmh + service_time
    return RouteResult(
        customer_ids=customer_ids,
        load=load,
        distance=distance,
        duration=duration,
        capacity_utilization=load / request.vehicle.capacity * 100,
    )


def _exceeds_time(request: OptimizationRequest, route: RouteResult) -> bool:
    return request.vehicle.max_route_time > 0 and route.duration > request.vehicle.max_route_time
