#pragma OPENCL EXTENSION cl_khr_fp64 : enable

__kernel void route_distance_kernel(
    __global const double2* points,
    __global const int* chromosomes,
    __global double* distances,
    const int chromosome_size,
    const int depot_index) {
    const int gid = get_global_id(0);
    const int offset = gid * chromosome_size;
    double distance = 0.0;
    int prev = depot_index;

    for (int i = 0; i < chromosome_size; ++i) {
        const int node = chromosomes[offset + i];
        const double dx = points[prev].x - points[node].x;
        const double dy = points[prev].y - points[node].y;
        distance += sqrt(dx * dx + dy * dy);
        prev = node;
    }
    const double dx = points[prev].x - points[depot_index].x;
    const double dy = points[prev].y - points[depot_index].y;
    distances[gid] = distance + sqrt(dx * dx + dy * dy);
}
