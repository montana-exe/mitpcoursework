from __future__ import annotations

from pydantic import BaseModel, Field, model_validator


class Customer(BaseModel):
    id: int = Field(ge=0)
    x: float = Field(ge=0, le=100)
    y: float = Field(ge=0, le=100)
    demand: float = Field(ge=0)
    service_time: float = Field(default=0, ge=0)


class Vehicle(BaseModel):
    capacity: float = Field(gt=0)
    max_route_time: float = Field(default=0, ge=0)
    average_speed_kmh: float = Field(default=40, gt=0)


class SolverSettings(BaseModel):
    population_size: int = Field(default=96, ge=8, le=4096)
    generations: int = Field(default=250, ge=1, le=20000)
    seed: int = Field(default=42, ge=0)
    prefer_gpu: bool = False


class OptimizationRequest(BaseModel):
    depot: Customer
    customers: list[Customer] = Field(min_length=1)
    vehicle: Vehicle
    settings: SolverSettings = Field(default_factory=SolverSettings)

    @model_validator(mode="after")
    def unique_customer_ids(self) -> "OptimizationRequest":
        ids = [self.depot.id, *[customer.id for customer in self.customers]]
        if len(ids) != len(set(ids)):
            raise ValueError("depot and customer ids must be unique")
        return self


class RouteResult(BaseModel):
    customer_ids: list[int]
    load: float
    distance: float
    duration: float
    capacity_utilization: float


class OptimizationResponse(BaseModel):
    routes: list[RouteResult]
    total_distance: float
    total_duration: float
    feasible: bool
    backend: str
