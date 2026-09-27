cmake_minimum_required(VERSION 3.31)
project(rune_fr CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" RUNE_FR_VERSION)

option(RUNE_FR "Enable rune-fr flight recording (compile-in)" ON)
option(BUILD_TESTING "Build tests" OFF)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)

# Compiled cold-path library (calibration, registry, reporting).
add_library(rune_fr_core STATIC
    src/Calibration.cpp
    src/Enable.cpp
    src/Registry.cpp
    src/Report.cpp
)
target_include_directories(rune_fr_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(rune_fr_core PUBLIC RUNE_FR=$<BOOL:${RUNE_FR}>)

# Public consumable target: headers + the core lib.
add_library(rune_fr INTERFACE)
target_include_directories(rune_fr INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(rune_fr INTERFACE rune_fr_core)
target_compile_definitions(rune_fr INTERFACE RUNE_FR=$<BOOL:${RUNE_FR}>)
add_library(rune_fr::rune_fr ALIAS rune_fr)

if(BUILD_TESTING)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)
    add_executable(rune_fr_tests
        tests/VersionTest.cpp
        tests/CalibrationTest.cpp
        tests/SitesTest.cpp
        tests/RingTest.cpp
        tests/RegistryTest.cpp
        tests/EnableTest.cpp
        tests/EventTest.cpp
        tests/HistogramTest.cpp
        tests/ReportHistogramTest.cpp
    )
    target_link_libraries(rune_fr_tests PRIVATE rune_fr GTest::gtest_main)
    gtest_discover_tests(rune_fr_tests)

    # Compile-out test: built with RUNE_FR=0 and linked ONLY against gtest
    # (deliberately NOT rune_fr) so any accidental reference to a recorder
    # symbol from the no-op Event.h fails to link and catches the regression.
    add_executable(rune_fr_compileout_test tests/CompileOutTest.cpp)
    target_include_directories(rune_fr_compileout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(rune_fr_compileout_test PRIVATE RUNE_FR=0)
    target_link_libraries(rune_fr_compileout_test PRIVATE GTest::gtest_main)
    gtest_discover_tests(rune_fr_compileout_test)
endif()

if(BUILD_BENCHMARKS)
    find_package(benchmark REQUIRED)
    add_executable(rune_fr_bench
        bench/fr_overhead_bench.cpp
    )
    target_link_libraries(rune_fr_bench PRIVATE rune_fr benchmark::benchmark)
endif()

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS rune_fr rune_fr_core EXPORT rune_frTargets)
install(EXPORT rune_frTargets NAMESPACE rune_fr:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rune_fr)
