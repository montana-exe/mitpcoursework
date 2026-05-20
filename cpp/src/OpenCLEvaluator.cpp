#include "routeopt/OpenCLEvaluator.h"

#include <cmath>
#include <stdexcept>

#if defined(ROUTEOPT_HAS_OPENCL)
#include <CL/cl.h>
#include <algorithm>
#include <cstring>
#endif

namespace routeopt {

namespace {

double cpu_cycle_distance(const Problem& problem, const std::vector<std::size_t>& chromosome) {
    if (chromosome.empty()) {
        return 0.0;
    }
    double distance = 0.0;
    auto prev = problem.depot_index;
    for (const auto node : chromosome) {
        distance += euclidean_distance(problem.nodes[prev], problem.nodes[node]);
        prev = node;
    }
    distance += euclidean_distance(problem.nodes[prev], problem.nodes[problem.depot_index]);
    return distance;
}

#if defined(ROUTEOPT_HAS_OPENCL)
constexpr const char* kFitnessKernel = R"CLC(
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
)CLC";
#endif

}  // namespace

bool OpenCLEvaluator::available() const {
#if defined(ROUTEOPT_HAS_OPENCL)
    cl_uint platform_count = 0;
    return clGetPlatformIDs(0, nullptr, &platform_count) == CL_SUCCESS && platform_count > 0;
#else
    return false;
#endif
}

std::string OpenCLEvaluator::backend_name() const {
    return available() ? "opencl-distance-cpu-constraints" : "cpu-opencl-unavailable";
}

std::vector<double> OpenCLEvaluator::evaluate_cycle_distances(
    const Problem& problem,
    const std::vector<std::vector<std::size_t>>& population) const {
    if (population.empty()) {
        return {};
    }

#if defined(ROUTEOPT_HAS_OPENCL)
    if (!available()) {
        std::vector<double> fallback;
        fallback.reserve(population.size());
        for (const auto& chromosome : population) {
            fallback.push_back(cpu_cycle_distance(problem, chromosome));
        }
        return fallback;
    }

    cl_int err = CL_SUCCESS;
    cl_uint platform_count = 0;
    err = clGetPlatformIDs(0, nullptr, &platform_count);
    if (err != CL_SUCCESS || platform_count == 0) {
        throw std::runtime_error("OpenCL platform is not available");
    }

    std::vector<cl_platform_id> platforms(platform_count);
    clGetPlatformIDs(platform_count, platforms.data(), nullptr);

    cl_device_id device = nullptr;
    for (const auto platform : platforms) {
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, nullptr);
        if (err == CL_SUCCESS) {
            break;
        }
    }
    if (device == nullptr) {
        err = clGetDeviceIDs(platforms.front(), CL_DEVICE_TYPE_DEFAULT, 1, &device, nullptr);
    }
    if (err != CL_SUCCESS || device == nullptr) {
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
    if (err != CL_SUCCESS) {
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        throw std::runtime_error("OpenCL program creation failed");
    }
    err = clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        throw std::runtime_error("OpenCL kernel build failed");
    }

    cl_kernel kernel = clCreateKernel(program, "route_distance_kernel", &err);
    if (err != CL_SUCCESS) {
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        throw std::runtime_error("OpenCL kernel creation failed");
    }

    struct Point {
        double x;
        double y;
    };
    std::vector<Point> points;
    points.reserve(problem.nodes.size());
    for (const auto& node : problem.nodes) {
        points.push_back({node.x, node.y});
    }

    const auto chromosome_size = static_cast<int>(population.front().size());
    std::vector<int> chromosomes;
    chromosomes.reserve(population.size() * population.front().size());
    for (const auto& chromosome : population) {
        for (const auto gene : chromosome) {
            chromosomes.push_back(static_cast<int>(gene));
        }
    }
    std::vector<double> distances(population.size(), 0.0);

    auto release_all = [&]() {
        clReleaseKernel(kernel);
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
    };

    cl_mem point_buffer = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                        sizeof(Point) * points.size(), points.data(), &err);
    if (err != CL_SUCCESS) {
        release_all();
        throw std::runtime_error("OpenCL point buffer creation failed");
    }
    cl_mem chromosome_buffer = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                             sizeof(int) * chromosomes.size(), chromosomes.data(), &err);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(point_buffer);
        release_all();
        throw std::runtime_error("OpenCL chromosome buffer creation failed");
    }
    cl_mem distance_buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY,
                                           sizeof(double) * distances.size(), nullptr, &err);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(chromosome_buffer);
        clReleaseMemObject(point_buffer);
        release_all();
        throw std::runtime_error("OpenCL distance buffer creation failed");
    }

    const auto depot = static_cast<int>(problem.depot_index);
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &point_buffer);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &chromosome_buffer);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &distance_buffer);
    clSetKernelArg(kernel, 3, sizeof(int), &chromosome_size);
    clSetKernelArg(kernel, 4, sizeof(int), &depot);

    const std::size_t global_size = population.size();
    err = clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global_size, nullptr, 0, nullptr, nullptr);
    if (err == CL_SUCCESS) {
        err = clEnqueueReadBuffer(queue, distance_buffer, CL_TRUE, 0,
                                  sizeof(double) * distances.size(), distances.data(), 0, nullptr, nullptr);
    }

    clReleaseMemObject(distance_buffer);
    clReleaseMemObject(chromosome_buffer);
    clReleaseMemObject(point_buffer);
    release_all();

    if (err != CL_SUCCESS) {
        throw std::runtime_error("OpenCL distance evaluation failed");
    }
    return distances;
#else
    std::vector<double> fallback;
    fallback.reserve(population.size());
    for (const auto& chromosome : population) {
        fallback.push_back(cpu_cycle_distance(problem, chromosome));
    }
    return fallback;
#endif
}

}  // namespace routeopt
