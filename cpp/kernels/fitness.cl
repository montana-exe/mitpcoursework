#pragma OPENCL EXTENSION cl_khr_fp64 : enable

__kernel void route_fitness_kernel(
    __global const double2* points,
    __global const double* demand,
    __global const double* service_time,
    __global const int* chromosomes,
    __global double* fitness,
    const int chromosome_size,
    const int depot_index,
    const double vehicle_capacity,
    const double max_route_time) {
    const int gid = get_global_id(0);
    const int offset = gid * chromosome_size;
    double distance = 0.0;
    double penalty = 0.0;
    double route_load = 0.0;
    double route_elapsed = 0.0;
    int prev = depot_index;
    int has_nodes = 0;

    for (int i = 0; i < chromosome_size; ++i) {
        const int node = chromosomes[offset + i];
        double dx = points[prev].x - points[node].x;
        double dy = points[prev].y - points[node].y;
        double leg = sqrt(dx * dx + dy * dy);
        dx = points[node].x - points[depot_index].x;
        dy = points[node].y - points[depot_index].y;
        const double return_leg = sqrt(dx * dx + dy * dy);
        const double candidate_load = route_load + demand[node];
        const double candidate_duration = route_elapsed + leg + service_time[node] + return_leg;

        if (has_nodes && (candidate_load > vehicle_capacity ||
            (max_route_time > 0.0 && candidate_duration > max_route_time))) {
            dx = points[prev].x - points[depot_index].x;
            dy = points[prev].y - points[depot_index].y;
            const double close_leg = sqrt(dx * dx + dy * dy);
            distance += close_leg;
            if (max_route_time > 0.0 && route_elapsed + close_leg > max_route_time) {
                penalty += 1000000.0 + route_elapsed + close_leg - max_route_time;
            }
            prev = depot_index;
            route_load = 0.0;
            route_elapsed = 0.0;
            dx = points[prev].x - points[node].x;
            dy = points[prev].y - points[node].y;
            leg = sqrt(dx * dx + dy * dy);
        }

        distance += leg;
        route_elapsed += leg + service_time[node];
        route_load += demand[node];
        prev = node;
        has_nodes = 1;
        if (demand[node] > vehicle_capacity) {
            penalty += 1000000.0 + demand[node] - vehicle_capacity;
        }
    }
    if (has_nodes) {
        const double dx = points[prev].x - points[depot_index].x;
        const double dy = points[prev].y - points[depot_index].y;
        const double return_leg = sqrt(dx * dx + dy * dy);
        distance += return_leg;
        const double duration = route_elapsed + return_leg;
        if (max_route_time > 0.0 && duration > max_route_time) {
            penalty += 1000000.0 + duration - max_route_time;
        }
    }
    fitness[gid] = distance + penalty;
}
