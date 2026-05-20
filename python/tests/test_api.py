from fastapi.testclient import TestClient

from routeopt.api import app


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
        "vehicle": {"capacity": 2},
        "settings": {"population_size": 16, "generations": 10, "seed": 7},
    }
    response = TestClient(app).post("/optimize", json=payload)
    assert response.status_code == 200
    assert response.json()["feasible"] is True
