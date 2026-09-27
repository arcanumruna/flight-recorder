cmake_minimum_required(VERSION 3.31)
project(rune_fr_sample CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(rune_fr CONFIG REQUIRED)

add_executable(rune_fr_sample main.cpp)
target_link_libraries(rune_fr_sample PRIVATE rune_fr::rune_fr)
