cmake_minimum_required(VERSION 3.31)
project(flight_recorder_sample CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(flight_recorder CONFIG REQUIRED)

add_executable(flight_recorder_sample main.cpp)
target_link_libraries(flight_recorder_sample PRIVATE flight_recorder::flight_recorder)
