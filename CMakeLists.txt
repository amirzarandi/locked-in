cmake_minimum_required(VERSION 3.10)
project(LockedInQueue)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(lockedin INTERFACE)
target_include_directories(lockedin INTERFACE include)

find_package(Threads REQUIRED)
include(CTest)

if(PROJECT_IS_TOP_LEVEL)
    add_compile_options(-Wall -Wpedantic -Wextra)
endif()

file(GLOB EXAMPLE_SOURCES "examples/*.cpp")

foreach(sourcefile ${EXAMPLE_SOURCES})
    get_filename_component(name ${sourcefile} NAME_WE)
    add_executable(${name} ${sourcefile})
    target_link_libraries(${name} 
        PRIVATE 
        lockedin 
        Threads::Threads
    )
endforeach()

if(BUILD_TESTING)
    add_executable(abstract_queue_tests test/abstract_queue_tests.cpp)
    add_executable(spmc_queue_tests test/spmc_queue_tests.cpp)
    add_executable(latency_benchmark perf/latency_benchmark.cpp)
    add_executable(throughput_benchmark perf/throughput_benchmark.cpp)
    target_link_libraries(abstract_queue_tests
        PRIVATE
        lockedin
        Threads::Threads
    )
    target_link_libraries(spmc_queue_tests
        PRIVATE
        lockedin
        Threads::Threads
    )
    target_link_libraries(latency_benchmark
        PRIVATE
        lockedin
        Threads::Threads
    )
    target_link_libraries(throughput_benchmark
        PRIVATE
        lockedin
        Threads::Threads
    )
    add_test(NAME abstract_queue_stub COMMAND abstract_queue_tests)
    add_test(NAME spmc_queue_tests COMMAND spmc_queue_tests)
endif()
