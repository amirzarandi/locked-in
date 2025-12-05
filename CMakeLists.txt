cmake_minimum_required(VERSION 3.10)
project(LockedInQueue)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LOCKEDIN_BUILD_BENCHMARKS "Build performance benchmarks" ON)
option(LOCKEDIN_BUILD_TESTS      "Build unit tests"             OFF)
option(LOCKEDIN_BUILD_EXAMPLES   "Build example executables"    OFF)

if(PROJECT_IS_TOP_LEVEL)
    add_compile_options(-Wall -Wpedantic -Wextra)
endif()

add_library(lockedin INTERFACE)
target_include_directories(lockedin INTERFACE include)
find_package(Threads REQUIRED)

if(LOCKEDIN_BUILD_BENCHMARKS)
    include(FetchContent)

    find_package(Boost QUIET)
    if(Boost_FOUND)
        if(NOT TARGET Boost::headers)
            add_library(Boost::headers INTERFACE IMPORTED)
            set_target_properties(Boost::headers PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIRS}"
            )
        endif()
    else()
        message(STATUS "Boost not found, fetching headers via FetchContent")
        set(LOCKEDIN_BOOST_VERSION 1.84.0)
        string(REPLACE "." "_" LOCKEDIN_BOOST_VERSION_UNDERSCORE ${LOCKEDIN_BOOST_VERSION})
        FetchContent_Declare(
            boost_headers
            URL https://boostorg.jfrog.io/artifactory/main/release/${LOCKEDIN_BOOST_VERSION}/source/boost_${LOCKEDIN_BOOST_VERSION_UNDERSCORE}.tar.gz
        )
        FetchContent_MakeAvailable(boost_headers)
        add_library(boost_headers INTERFACE)
        target_include_directories(boost_headers INTERFACE ${boost_headers_SOURCE_DIR})
        add_library(Boost::headers ALIAS boost_headers)
    endif()

    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.4
    )
    FetchContent_MakeAvailable(benchmark)

    add_executable(google_benchmarks perf/google_benchmarks.cpp)
    target_link_libraries(google_benchmarks 
        PRIVATE 
        lockedin 
        Threads::Threads
        Boost::headers
        benchmark::benchmark 
        benchmark::benchmark_main
    )
endif()

if(LOCKEDIN_BUILD_EXAMPLES)    
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
endif()

if(LOCKEDIN_BUILD_TESTS)
    enable_testing()
    include(CTest)

    function(add_lockedin_test test_name source_file)
        add_executable(${test_name} ${source_file})
        target_link_libraries(${test_name} PRIVATE lockedin Threads::Threads)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endfunction()

    add_lockedin_test(abstract_queue_tests test/abstract_queue_tests.cpp)
    add_lockedin_test(spmc_queue_tests test/spmc_queue_tests.cpp)
    add_lockedin_test(latency_benchmark perf/latency_benchmark.cpp)
    add_lockedin_test(throughput_benchmark perf/throughput_benchmark.cpp)
endif()
