#include "routeopt/GeneticSolver.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

routeopt::Problem sample_problem(std::size_t customers) {
    routeopt::Problem problem;
    problem.depot_index = 0;
    problem.vehicle_capacity = 25.0;
    problem.max_route_time = 220.0;
    problem.nodes.push_back({50.0, 50.0, 0.0, 0.0});
    for (std::size_t i = 1; i <= customers; ++i) {
        const auto x = static_cast<double>((i * 37) % 100);
        const auto y = static_cast<double>((i * 53) % 100);
        const auto demand = 1.0 + static_cast<double>((i * 7) % 8);
        problem.nodes.push_back({x, y, demand, 0.0});
    }
    return problem;
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t customers = 80;
    std::size_t generations = 200;
    bool prefer_gpu = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--customers" && i + 1 < argc) {
            customers = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--generations" && i + 1 < argc) {
            generations = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--backend" && i + 1 < argc) {
            prefer_gpu = std::string(argv[++i]) == "gpu";
        }
    }

    routeopt::SolverConfig config;
    config.generations = generations;
    config.prefer_gpu = prefer_gpu;
    routeopt::GeneticSolver solver(config);
    const auto solution = solver.solve(sample_problem(customers));

    std::cout << "{";
    std::cout << "\"backend\":\"" << solution.backend << "\",";
    std::cout << "\"routes\":" << solution.routes.size() << ",";
    std::cout << "\"distance\":" << solution.total_distance << ",";
    std::cout << "\"feasible\":" << (solution.feasible ? "true" : "false");
    std::cout << "}\n";
    return solution.feasible ? 0 : 2;
}
