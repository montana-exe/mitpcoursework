import pytest
from pydantic import ValidationError

from routeopt.models import Customer, OptimizationRequest, Vehicle


def test_request_rejects_duplicate_ids() -> None:
    with pytest.raises(ValidationError):
        OptimizationRequest(
            depot=Customer(id=1, x=0, y=0, demand=0),
            customers=[Customer(id=1, x=1, y=1, demand=2)],
            vehicle=Vehicle(capacity=10),
        )


def test_vehicle_capacity_must_be_positive() -> None:
    with pytest.raises(ValidationError):
        Vehicle(capacity=0)
