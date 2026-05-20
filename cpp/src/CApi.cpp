#include "routeopt/CApi.h"

#include "routeopt/GeneticSolver.h"

#include <exception>
#include <vector>

extern "C" {

int routeopt_optimize(
    const double* xy,
    const double* demand,
    int node_count,
    int depot_index,
    double vehicle_capacity,
    double max_route_time,
    int population_size,
    int generations,
    unsigned long long seed,
    int prefer_gpu,
    int* route_nodes,
    int route_nodes_capacity,
    int* route_offsets,
    int route_offsets_capacity,
    int* out_route_count,
    int* out_node_count,
    double* out_total_distance,
    int* out_feasible) {
    try {
        if (!xy || !demand || !route_nodes || !route_offsets || node_count <= 0) {
            return -1;
        }
        routeopt::Problem problem;
        problem.depot_index = static_cast<std::size_t>(depot_index);
        problem.vehicle_capacity = vehicle_capacity;
        problem.max_route_time = max_route_time;
        problem.nodes.reserve(static_cast<std::size_t>(node_count));
        for (int i = 0; i < node_count; ++i) {
            problem.nodes.push_back(routeopt::Customer{
                xy[i * 2],
                xy[i * 2 + 1],
                demand[i],
                0.0,
            });
        }

        routeopt::SolverConfig config;
        config.population_size = static_cast<std::size_t>(population_size > 0 ? population_size : 96);
        config.generations = static_cast<std::size_t>(generations > 0 ? generations : 250);
        config.seed = seed;
        config.prefer_gpu = prefer_gpu != 0;
        routeopt::GeneticSolver solver(config);
        const auto solution = solver.solve(problem);

        int node_cursor = 0;
        int route_cursor = 0;
        for (const auto& route : solution.routes) {
            if (route_cursor + 1 >= route_offsets_capacity) {
                return -2;
            }
            route_offsets[route_cursor++] = node_cursor;
            for (const auto node : route.nodes) {
                if (node_cursor >= route_nodes_capacity) {
                    return -3;
                }
                route_nodes[node_cursor++] = static_cast<int>(node);
            }
        }
        if (route_cursor >= route_offsets_capacity) {
            return -2;
        }
        route_offsets[route_cursor] = node_cursor;

        if (out_route_count) {
            *out_route_count = route_cursor;
        }
        if (out_node_count) {
            *out_node_count = node_cursor;
        }
        if (out_total_distance) {
            *out_total_distance = solution.total_distance;
        }
        if (out_feasible) {
            *out_feasible = solution.feasible ? 1 : 0;
        }
        return 0;
    } catch (const std::exception&) {
        return -10;
    }
}

const char* routeopt_version() {
    return "0.1.0";
}

}
