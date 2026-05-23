from fastapi.testclient import TestClient

from routeopt.api import app


def test_interface_is_served_in_russian() -> None:
    response = TestClient(app).get("/")
    assert response.status_code == 200
    assert "Оптимизация маршрутов доставки" in response.text


def test_health_endpoint() -> None:
    response = TestClient(app).get("/health")
    assert response.status_code == 200
    assert response.json()["status"] == "ok"


def test_optimize_endpoint() -> None:
    payload = {
        "depot": {"id": 0, "x": 0, "y": 0, "demand": 0},
        "customers": [
            {"id": 1, "x": 1, "y": 0, "demand": 1},
            {"id": 2, "x": 0, "y": 1, "demand": 1},
        ],
        "vehicle": {"capacity": 2, "average_speed_kmh": 40},
        "settings": {"population_size": 16, "generations": 10, "seed": 7},
    }
    response = TestClient(app).post("/optimize", json=payload)
    assert response.status_code == 200
    assert response.json()["feasible"] is True
    assert response.json()["routes"][0]["distance"] > 0
    assert response.json()["total_duration"] > 0
