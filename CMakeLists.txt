cmake_minimum_required(VERSION 3.31)
project(flight_recorder CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" FLIGHT_RECORDER_VERSION)
# Numeric x.y.z only (strip any -snapshot/-rc suffix) for CMake's version file,
# which rejects non-numeric version components.
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" FLIGHT_RECORDER_VERSION_NUMERIC "${FLIGHT_RECORDER_VERSION}")
if(NOT FLIGHT_RECORDER_VERSION_NUMERIC)
    set(FLIGHT_RECORDER_VERSION_NUMERIC "0.0.0")
endif()

# Guard against Version.h drifting from VERSION.txt (the single source CMake/Conan
# read): fail configure if the header's kVersion literal doesn't match.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/flight_recorder/Version.h" FLIGHT_RECORDER_VERSION_HDR)
if(NOT FLIGHT_RECORDER_VERSION_HDR MATCHES "\"${FLIGHT_RECORDER_VERSION}\"")
    message(FATAL_ERROR
        "flight_recorder: include/flight_recorder/Version.h kVersion is out of sync with VERSION.txt "
        "(${FLIGHT_RECORDER_VERSION}). Update Version.h to match.")
endif()

option(FLIGHT_RECORDER "Enable flight-recorder flight recording (compile-in)" ON)
option(BUILD_TESTING "Build tests" OFF)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)

# flight-recorder's per-thread ring is a namespace-isolated copy of Rune's agrona M2O
# ring, built on aeron's header-only AtomicBuffer/RecordDescriptor. On Linux the
# real aeron package provides these. The aeron ConanCenter recipe rejects
# macOS/armv8 (Apple Silicon), so there it is skipped (see conanfile.py) and we
# provide a stub aeron::aeron INTERFACE target so cmake CONFIGURE still succeeds.
# Set -DFLIGHT_RECORDER_AERON_INCLUDE_DIR=<path to aeron client headers> to actually
# COMPILE on macOS against the cached aeron headers (header-only usage).
set(FLIGHT_RECORDER_AERON_INCLUDE_DIR "" CACHE PATH "aeron client include dir (macOS header-only fallback)")
find_package(aeron QUIET)
if(NOT TARGET aeron::aeron)
    add_library(flight_recorder_aeron_stub INTERFACE)
    if(FLIGHT_RECORDER_AERON_INCLUDE_DIR)
        target_include_directories(flight_recorder_aeron_stub INTERFACE ${FLIGHT_RECORDER_AERON_INCLUDE_DIR})
        message(STATUS "flight-recorder: aeron package not found; using header-only fallback at ${FLIGHT_RECORDER_AERON_INCLUDE_DIR}")
    else()
        message(WARNING "flight-recorder: aeron not found and FLIGHT_RECORDER_AERON_INCLUDE_DIR unset — "
                        "configure-only stub (build/tests require aeron headers).")
    endif()
    add_library(aeron::aeron ALIAS flight_recorder_aeron_stub)
endif()

# Compiled cold-path library (calibration, registry, reporting).
add_library(flight_recorder_core STATIC
    src/Calibration.cpp
    src/Enable.cpp
    src/Registry.cpp
    src/Report.cpp
)
target_include_directories(flight_recorder_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(flight_recorder_core PUBLIC aeron::aeron)
target_compile_definitions(flight_recorder_core PUBLIC FLIGHT_RECORDER=$<BOOL:${FLIGHT_RECORDER}>)

# Public consumable target: headers + the core lib.
add_library(flight_recorder INTERFACE)
target_include_directories(flight_recorder INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(flight_recorder INTERFACE flight_recorder_core aeron::aeron)
target_compile_definitions(flight_recorder INTERFACE FLIGHT_RECORDER=$<BOOL:${FLIGHT_RECORDER}>)
add_library(flight_recorder::flight_recorder ALIAS flight_recorder)

if(BUILD_TESTING)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)
    add_executable(flight_recorder_tests
        tests/VersionTest.cpp
        tests/CalibrationTest.cpp
        tests/SitesTest.cpp
        tests/M2ORingTest.cpp
        tests/RegistryTest.cpp
        tests/EnableTest.cpp
        tests/EventTest.cpp
        tests/HistogramTest.cpp
        tests/ReportHistogramTest.cpp
        tests/ChromeTraceTest.cpp
        tests/TapeTest.cpp
        tests/ThroughputTest.cpp
        tests/HopInstrumentationTest.cpp
    )
    find_package(nlohmann_json REQUIRED)
    target_link_libraries(flight_recorder_tests PRIVATE flight_recorder GTest::gtest_main nlohmann_json::nlohmann_json)
    gtest_discover_tests(flight_recorder_tests)

    # Compile-out test: built with FLIGHT_RECORDER=0 and linked ONLY against gtest
    # (deliberately NOT flight_recorder) so any accidental reference to a recorder
    # symbol from the no-op Event.h fails to link and catches the regression.
    add_executable(flight_recorder_compileout_test tests/CompileOutTest.cpp)
    target_include_directories(flight_recorder_compileout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(flight_recorder_compileout_test PRIVATE FLIGHT_RECORDER=0)
    target_link_libraries(flight_recorder_compileout_test PRIVATE GTest::gtest_main)
    gtest_discover_tests(flight_recorder_compileout_test)
endif()

if(BUILD_BENCHMARKS)
    find_package(benchmark REQUIRED)
    add_executable(flight_recorder_bench
        bench/fr_overhead_bench.cpp
    )
    target_link_libraries(flight_recorder_bench PRIVATE flight_recorder benchmark::benchmark)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
set(FLIGHT_RECORDER_INSTALL_TARGETS flight_recorder flight_recorder_core)
if(TARGET flight_recorder_aeron_stub)
    # The header-only aeron fallback stub is linked into the exported targets,
    # so it must travel in the same export set (real consumers use find_package(aeron)).
    list(APPEND FLIGHT_RECORDER_INSTALL_TARGETS flight_recorder_aeron_stub)
endif()
install(TARGETS ${FLIGHT_RECORDER_INSTALL_TARGETS} EXPORT flight_recorderTargets)
install(EXPORT flight_recorderTargets NAMESPACE flight_recorder:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/flight_recorder)

# Package config so `find_package(flight_recorder CONFIG)` works for raw-CMake consumers.
# (Conan consumers get their own generated config via package_info.)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/flight_recorderConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/flight_recorderConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/flight_recorder)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/flight_recorderConfigVersion.cmake
    VERSION ${FLIGHT_RECORDER_VERSION_NUMERIC}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/flight_recorderConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/flight_recorderConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/flight_recorder)
