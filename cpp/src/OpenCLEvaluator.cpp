#include "routeopt/OpenCLEvaluator.h"

#include <stdexcept>

#if defined(ROUTEOPT_HAS_OPENCL)
#include <CL/cl.h>
#include <cstring>
#endif

namespace routeopt {

#if defined(ROUTEOPT_HAS_OPENCL)
namespace {

constexpr const char* kFitnessKernel = R"CLC(
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
)CLC";

struct Point {
    double x;
    double y;
};

}  // namespace
#endif

bool OpenCLEvaluator::available() const {
#if defined(ROUTEOPT_HAS_OPENCL)
    cl_uint platform_count = 0;
    return clGetPlatformIDs(0, nullptr, &platform_count) == CL_SUCCESS && platform_count > 0;
#else
    return false;
#endif
}

std::string OpenCLEvaluator::backend_name() const {
    return available() ? "opencl-fitness" : "cpu-opencl-unavailable";
}

std::vector<double> OpenCLEvaluator::evaluate_fitness(
    const Problem& problem,
    const std::vector<std::vector<std::size_t>>& population) const {
    if (population.empty()) {
        return {};
    }
#if !defined(ROUTEOPT_HAS_OPENCL)
    throw std::runtime_error("OpenCL support is not built");
#else
    cl_int err = CL_SUCCESS;
    cl_uint platform_count = 0;
    if (clGetPlatformIDs(0, nullptr, &platform_count) != CL_SUCCESS || platform_count == 0) {
        throw std::runtime_error("OpenCL platform is not available");
    }
    std::vector<cl_platform_id> platforms(platform_count);
    clGetPlatformIDs(platform_count, platforms.data(), nullptr);

    cl_device_id device = nullptr;
    for (const auto platform : platforms) {
        if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, nullptr) == CL_SUCCESS) {
            break;
        }
    }
    if (device == nullptr &&
        clGetDeviceIDs(platforms.front(), CL_DEVICE_TYPE_DEFAULT, 1, &device, nullptr) != CL_SUCCESS) {
        throw std::runtime_error("OpenCL device is not available");
    }

    cl_context context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) {
        throw std::runtime_error("OpenCL context creation failed");
    }
    cl_command_queue queue = clCreateCommandQueue(context, device, 0, &err);
    if (err != CL_SUCCESS) {
        clReleaseContext(context);
        throw std::runtime_error("OpenCL queue creation failed");
    }
    const char* source = kFitnessKernel;
    const auto source_size = std::strlen(kFitnessKernel);
    cl_program program = clCreateProgramWithSource(context, 1, &source, &source_size, &err);
    if (err != CL_SUCCESS || clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr) != CL_SUCCESS) {
        if (program) {
            clReleaseProgram(program);
        }
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        throw std::runtime_error("OpenCL kernel build failed");
    }
    cl_kernel kernel = clCreateKernel(program, "route_fitness_kernel", &err);
    if (err != CL_SUCCESS) {
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        throw std::runtime_error("OpenCL kernel creation failed");
    }

    std::vector<Point> points;
    std::vector<double> demand;
    std::vector<double> service_time;
    for (const auto& node : problem.nodes) {
        points.push_back({node.x, node.y});
        demand.push_back(node.demand);
        service_time.push_back(node.service_time);
    }
    std::vector<int> chromosomes;
    for (const auto& chromosome : population) {
        for (const auto gene : chromosome) {
            chromosomes.push_back(static_cast<int>(gene));
        }
    }
    std::vector<double> fitness(population.size(), 0.0);
    cl_mem points_buffer = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                          sizeof(Point) * points.size(), points.data(), &err);
    cl_mem demand_buffer = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                          sizeof(double) * demand.size(), demand.data(), &err);
    cl_mem service_buffer = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                           sizeof(double) * service_time.size(), service_time.data(), &err);
    cl_mem chromosomes_buffer = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                               sizeof(int) * chromosomes.size(), chromosomes.data(), &err);
    cl_mem fitness_buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(double) * fitness.size(), nullptr, &err);
    if (err != CL_SUCCESS) {
        throw std::runtime_error("OpenCL buffer creation failed");
    }

    const auto chromosome_size = static_cast<int>(population.front().size());
    const auto depot = static_cast<int>(problem.depot_index);
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &points_buffer);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &demand_buffer);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &service_buffer);
    clSetKernelArg(kernel, 3, sizeof(cl_mem), &chromosomes_buffer);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &fitness_buffer);
    clSetKernelArg(kernel, 5, sizeof(int), &chromosome_size);
    clSetKernelArg(kernel, 6, sizeof(int), &depot);
    clSetKernelArg(kernel, 7, sizeof(double), &problem.vehicle_capacity);
    clSetKernelArg(kernel, 8, sizeof(double), &problem.max_route_time);
    const std::size_t global_size = population.size();
    err = clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global_size, nullptr, 0, nullptr, nullptr);
    if (err == CL_SUCCESS) {
        err = clEnqueueReadBuffer(queue, fitness_buffer, CL_TRUE, 0, sizeof(double) * fitness.size(),
                                  fitness.data(), 0, nullptr, nullptr);
    }

    clReleaseMemObject(fitness_buffer);
    clReleaseMemObject(chromosomes_buffer);
    clReleaseMemObject(service_buffer);
    clReleaseMemObject(demand_buffer);
    clReleaseMemObject(points_buffer);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    if (err != CL_SUCCESS) {
        throw std::runtime_error("OpenCL fitness evaluation failed");
    }
    return fitness;
#endif
}

}  // namespace routeopt
