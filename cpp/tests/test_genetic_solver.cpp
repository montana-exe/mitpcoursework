#include "routeopt/GeneticSolver.h"

#include <cassert>
#include <set>

int main() {
    routeopt::Problem problem;
    problem.depot_index = 0;
    problem.vehicle_capacity = 10.0;
    problem.max_route_time = 100.0;
    problem.average_speed_kmh = 40.0;
    problem.nodes = {
        {0.0, 0.0, 0.0, 0.0},
        {1.0, 0.0, 2.0, 0.0},
        {2.0, 0.0, 2.0, 0.0},
        {0.0, 2.0, 3.0, 0.0},
        {2.0, 2.0, 3.0, 0.0},
    };

    routeopt::SolverConfig config;
    config.population_size = 32;
    config.generations = 60;
    routeopt::GeneticSolver solver(config);
    const auto solution = solver.solve(problem);

    assert(solution.feasible);
    assert(solution.total_distance > 0.0);

    std::set<std::size_t> visited;
    for (const auto& route : solution.routes) {
        assert(route.load <= problem.vehicle_capacity);
        for (const auto node : route.nodes) {
            visited.insert(node);
        }
    }
    assert(visited.size() == problem.nodes.size() - 1);
    return 0;
}
