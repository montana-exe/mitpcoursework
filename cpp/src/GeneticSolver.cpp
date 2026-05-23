#include "routeopt/GeneticSolver.h"

#include "routeopt/OpenCLEvaluator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_set>

namespace routeopt {

namespace {

constexpr double kPenalty = 1'000'000.0;

double route_duration_with_return(const Problem& problem, const std::vector<std::size_t>& route) {
    if (route.empty()) {
        return 0.0;
    }
    double duration = 0.0;
    std::size_t prev = problem.depot_index;
    for (const auto node : route) {
        duration += euclidean_distance(problem.nodes[prev], problem.nodes[node]) / problem.average_speed_kmh;
        duration += problem.nodes[node].service_time;
        prev = node;
    }
    duration += euclidean_distance(problem.nodes[prev], problem.nodes[problem.depot_index]) / problem.average_speed_kmh;
    return duration;
}

}  // namespace

double euclidean_distance(const Customer& lhs, const Customer& rhs) {
    const auto dx = lhs.x - rhs.x;
    const auto dy = lhs.y - rhs.y;
    return std::sqrt(dx * dx + dy * dy);
}

GeneticSolver::GeneticSolver(SolverConfig config) : config_(config), rng_(config.seed) {
    if (config_.population_size < 8) {
        config_.population_size = 8;
    }
    if (config_.elite_fraction <= 0.0 || config_.elite_fraction > 0.5) {
        config_.elite_fraction = 0.08;
    }
}

std::vector<std::size_t> GeneticSolver::make_base_chromosome(const Problem& problem) const {
    std::vector<std::size_t> chromosome;
    chromosome.reserve(problem.nodes.size() > 0 ? problem.nodes.size() - 1 : 0);
    for (std::size_t i = 0; i < problem.nodes.size(); ++i) {
        if (i != problem.depot_index) {
            chromosome.push_back(i);
        }
    }
    return chromosome;
}

double GeneticSolver::route_distance(const Problem& problem, const std::vector<std::size_t>& route_nodes) const {
    if (route_nodes.empty()) {
        return 0.0;
    }
    double distance = 0.0;
    std::size_t prev = problem.depot_index;
    for (const auto node : route_nodes) {
        distance += euclidean_distance(problem.nodes[prev], problem.nodes[node]);
        prev = node;
    }
    distance += euclidean_distance(problem.nodes[prev], problem.nodes[problem.depot_index]);
    return distance;
}

Solution GeneticSolver::decode(const Problem& problem, const std::vector<std::size_t>& chromosome) const {
    if (problem.nodes.empty() || problem.depot_index >= problem.nodes.size()) {
        throw std::invalid_argument("invalid problem: depot index is out of range");
    }
    if (problem.vehicle_capacity <= 0.0) {
        throw std::invalid_argument("invalid problem: vehicle capacity must be positive");
    }
    if (problem.average_speed_kmh <= 0.0) {
        throw std::invalid_argument("invalid problem: average speed must be positive");
    }

    Solution solution;
    solution.backend = config_.prefer_gpu ? OpenCLEvaluator{}.backend_name() : "cpu";

    Route current;
    for (const auto node : chromosome) {
        if (node >= problem.nodes.size() || node == problem.depot_index) {
            solution.penalty += kPenalty;
            continue;
        }

        const auto candidate_load = current.load + problem.nodes[node].demand;
        auto candidate_nodes = current.nodes;
        candidate_nodes.push_back(node);
        const auto candidate_duration = route_duration_with_return(problem, candidate_nodes);

        const bool capacity_ok = candidate_load <= problem.vehicle_capacity;
        const bool time_ok = problem.max_route_time <= 0.0 || candidate_duration <= problem.max_route_time;

        if (!current.nodes.empty() && (!capacity_ok || !time_ok)) {
            current.distance = route_distance(problem, current.nodes);
            current.duration = route_duration_with_return(problem, current.nodes);
            solution.total_distance += current.distance;
            solution.routes.push_back(current);
            current = Route{};
        }

        current.nodes.push_back(node);
        current.load += problem.nodes[node].demand;

        if (problem.nodes[node].demand > problem.vehicle_capacity) {
            solution.penalty += kPenalty + problem.nodes[node].demand - problem.vehicle_capacity;
        }
    }

    if (!current.nodes.empty()) {
        current.distance = route_distance(problem, current.nodes);
        current.duration = route_duration_with_return(problem, current.nodes);
        solution.total_distance += current.distance;
        solution.routes.push_back(current);
    }

    solution.feasible = solution.penalty == 0.0;
    for (const auto& route : solution.routes) {
        if (route.load > problem.vehicle_capacity) {
            solution.feasible = false;
            solution.penalty += kPenalty + route.load - problem.vehicle_capacity;
        }
        if (problem.max_route_time > 0.0 && route.duration > problem.max_route_time) {
            solution.feasible = false;
            solution.penalty += kPenalty + route.duration - problem.max_route_time;
        }
    }
    return solution;
}

std::size_t GeneticSolver::tournament(const std::vector<std::vector<std::size_t>>& population,
                                      const std::vector<double>& fitness) {
    std::uniform_int_distribution<std::size_t> pick(0, population.size() - 1);
    auto best = pick(rng_);
    for (int i = 0; i < 2; ++i) {
        const auto candidate = pick(rng_);
        if (fitness[candidate] < fitness[best]) {
            best = candidate;
        }
    }
    return best;
}

std::vector<std::size_t> GeneticSolver::ordered_crossover(const std::vector<std::size_t>& a,
                                                          const std::vector<std::size_t>& b) {
    if (a.size() < 2) {
        return a;
    }
    std::uniform_int_distribution<std::size_t> pick(0, a.size() - 1);
    auto left = pick(rng_);
    auto right = pick(rng_);
    if (left > right) {
        std::swap(left, right);
    }

    std::vector<std::size_t> child(a.size(), std::numeric_limits<std::size_t>::max());
    std::unordered_set<std::size_t> used;
    for (std::size_t i = left; i <= right; ++i) {
        child[i] = a[i];
        used.insert(a[i]);
    }

    std::size_t cursor = (right + 1) % child.size();
    for (std::size_t i = 0; i < b.size(); ++i) {
        const auto gene = b[(right + 1 + i) % b.size()];
        if (used.contains(gene)) {
            continue;
        }
        child[cursor] = gene;
        cursor = (cursor + 1) % child.size();
    }
    return child;
}

void GeneticSolver::mutate(std::vector<std::size_t>& chromosome) {
    if (chromosome.size() < 2) {
        return;
    }
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    if (chance(rng_) > config_.mutation_rate) {
        return;
    }
    std::uniform_int_distribution<std::size_t> pick(0, chromosome.size() - 1);
    const auto a = pick(rng_);
    const auto b = pick(rng_);
    std::swap(chromosome[a], chromosome[b]);
}

Solution GeneticSolver::solve(const Problem& problem) {
    auto base = make_base_chromosome(problem);
    if (base.empty()) {
        return Solution{.feasible = true, .backend = "cpu"};
    }

    std::vector<std::vector<std::size_t>> population;
    population.reserve(config_.population_size);
    population.push_back(base);
    for (std::size_t i = 1; i < config_.population_size; ++i) {
        auto chromosome = base;
        std::shuffle(chromosome.begin(), chromosome.end(), rng_);
        population.push_back(std::move(chromosome));
    }

    std::vector<double> fitness(population.size(), 0.0);
    std::vector<std::size_t> order(population.size(), 0);
    std::iota(order.begin(), order.end(), 0);

    const auto elite_count = std::max<std::size_t>(1, static_cast<std::size_t>(config_.population_size * config_.elite_fraction));

    OpenCLEvaluator opencl;
    const auto use_opencl_fitness = config_.prefer_gpu && opencl.available();

    for (std::size_t generation = 0; generation < config_.generations; ++generation) {
        std::vector<double> gpu_fitness;
        if (use_opencl_fitness) {
            gpu_fitness = opencl.evaluate_fitness(problem, population);
        }
        for (std::size_t i = 0; i < population.size(); ++i) {
            const auto solution = decode(problem, population[i]);
            fitness[i] = use_opencl_fitness ? gpu_fitness[i] : solution.total_distance + solution.penalty;
        }
        std::sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
            return fitness[lhs] < fitness[rhs];
        });

        std::vector<std::vector<std::size_t>> next;
        next.reserve(config_.population_size);
        for (std::size_t i = 0; i < elite_count; ++i) {
            next.push_back(population[order[i]]);
        }
        while (next.size() < config_.population_size) {
            const auto parent_a = tournament(population, fitness);
            const auto parent_b = tournament(population, fitness);
            auto child = ordered_crossover(population[parent_a], population[parent_b]);
            mutate(child);
            next.push_back(std::move(child));
        }
        population = std::move(next);
    }

    for (std::size_t i = 0; i < population.size(); ++i) {
        const auto solution = decode(problem, population[i]);
        fitness[i] = solution.total_distance + solution.penalty;
    }
    auto best = static_cast<std::size_t>(std::distance(fitness.begin(), std::min_element(fitness.begin(), fitness.end())));
    auto solution = decode(problem, population[best]);
    solution.backend = config_.prefer_gpu ? opencl.backend_name() : "cpu";
    return solution;
}

}  // namespace routeopt
