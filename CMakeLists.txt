cmake_minimum_required(VERSION 3.20)
project(route_optimizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ROUTEOPT_ENABLE_OPENCL "Build with OpenCL support when available" ON)

add_library(routeopt_core
    cpp/src/GeneticSolver.cpp
    cpp/src/CApi.cpp
)
target_include_directories(routeopt_core PUBLIC cpp/include)

if(ROUTEOPT_ENABLE_OPENCL)
    find_package(OpenCL QUIET)
    if(OpenCL_FOUND)
        target_compile_definitions(routeopt_core PRIVATE ROUTEOPT_HAS_OPENCL=1)
        target_link_libraries(routeopt_core PRIVATE OpenCL::OpenCL)
    endif()
endif()

add_executable(routeopt_cli cpp/src/cli.cpp)
target_link_libraries(routeopt_cli PRIVATE routeopt_core)

add_executable(routeopt_cpp_tests cpp/tests/test_genetic_solver.cpp)
target_link_libraries(routeopt_cpp_tests PRIVATE routeopt_core)

enable_testing()
add_test(NAME routeopt_cpp_tests COMMAND routeopt_cpp_tests)
