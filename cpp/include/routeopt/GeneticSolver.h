#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace routeopt {

struct Customer {
    double x{};
    double y{};
    double demand{};
    double service_time{};
};

struct Problem {
    std::vector<Customer> nodes;
    std::size_t depot_index{0};
    double vehicle_capacity{0.0};
    double max_route_time{0.0};
};

struct Route {
    std::vector<std::size_t> nodes;
    double load{0.0};
    double duration{0.0};
    double distance{0.0};
};

struct Solution {
    std::vector<Route> routes;
    double total_distance{0.0};
    double penalty{0.0};
    bool feasible{false};
    std::string backend{"cpu"};
};

struct SolverConfig {
    std::size_t population_size{96};
    std::size_t generations{250};
    double elite_fraction{0.08};
    double mutation_rate{0.18};
    std::uint64_t seed{42};
    bool prefer_gpu{false};
};

class GeneticSolver {
public:
    explicit GeneticSolver(SolverConfig config = {});

    Solution solve(const Problem& problem);
    Solution decode(const Problem& problem, const std::vector<std::size_t>& chromosome) const;
    double route_distance(const Problem& problem, const std::vector<std::size_t>& route_nodes) const;

private:
    SolverConfig config_;
    mutable std::mt19937_64 rng_;

    std::vector<std::size_t> make_base_chromosome(const Problem& problem) const;
    std::vector<std::size_t> ordered_crossover(const std::vector<std::size_t>& a,
                                               const std::vector<std::size_t>& b);
    void mutate(std::vector<std::size_t>& chromosome);
    std::size_t tournament(const std::vector<std::vector<std::size_t>>& population,
                           const std::vector<double>& fitness);
};

double euclidean_distance(const Customer& lhs, const Customer& rhs);

}  // namespace routeopt
