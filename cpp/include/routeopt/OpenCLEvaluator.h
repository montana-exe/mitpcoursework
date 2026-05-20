#pragma once

#include "routeopt/GeneticSolver.h"

#include <string>
#include <vector>

namespace routeopt {

class OpenCLEvaluator {
public:
    bool available() const;
    std::string backend_name() const;

    std::vector<double> evaluate_cycle_distances(
        const Problem& problem,
        const std::vector<std::vector<std::size_t>>& population) const;
};

}  // namespace routeopt
