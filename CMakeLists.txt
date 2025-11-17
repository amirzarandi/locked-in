cmake_minimum_required(VERSION 3.10)
project(LockedInQueue)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(lockedin INTERFACE)
target_include_directories(lockedin INTERFACE include)

find_package(Threads REQUIRED)

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