#pragma once

#ifdef _WIN32
#define ROUTEOPT_API __declspec(dllexport)
#else
#define ROUTEOPT_API
#endif

extern "C" {

ROUTEOPT_API int routeopt_optimize(
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
    int* out_feasible);

ROUTEOPT_API const char* routeopt_version();

ROUTEOPT_API int routeopt_opencl_available();

}
