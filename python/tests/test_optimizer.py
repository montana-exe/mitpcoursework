from routeopt.core import NativeRouteOptimizer
from routeopt.models import Customer, OptimizationRequest, Vehicle


def test_optimizer_visits_all_customers() -> None:
    request = OptimizationRequest(
        depot=Customer(id=0, x=0, y=0, demand=0),
        customers=[
            Customer(id=10, x=1, y=0, demand=2),
            Customer(id=11, x=2, y=0, demand=2),
            Customer(id=12, x=0, y=2, demand=2),
        ],
        vehicle=Vehicle(capacity=4),
    )

    response = NativeRouteOptimizer().optimize(request)

    visited = sorted(customer_id for route in response.routes for customer_id in route.customer_ids)
    assert visited == [10, 11, 12]
    assert all(route.load <= request.vehicle.capacity for route in response.routes)
    assert response.total_distance > 0
