cmake_minimum_required(VERSION 3.31)
project(rune_fr CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" RUNE_FR_VERSION)
# Numeric x.y.z only (strip any -snapshot/-rc suffix) for CMake's version file,
# which rejects non-numeric version components.
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" RUNE_FR_VERSION_NUMERIC "${RUNE_FR_VERSION}")
if(NOT RUNE_FR_VERSION_NUMERIC)
    set(RUNE_FR_VERSION_NUMERIC "0.0.0")
endif()

# Guard against Version.h drifting from VERSION.txt (the single source CMake/Conan
# read): fail configure if the header's kVersion literal doesn't match.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/rune_fr/Version.h" RUNE_FR_VERSION_HDR)
if(NOT RUNE_FR_VERSION_HDR MATCHES "\"${RUNE_FR_VERSION}\"")
    message(FATAL_ERROR
        "rune_fr: include/rune_fr/Version.h kVersion is out of sync with VERSION.txt "
        "(${RUNE_FR_VERSION}). Update Version.h to match.")
endif()

option(RUNE_FR "Enable rune-fr flight recording (compile-in)" ON)
option(BUILD_TESTING "Build tests" OFF)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)

# rune-fr's per-thread ring is a namespace-isolated copy of Rune's agrona M2O
# ring, built on aeron's header-only AtomicBuffer/RecordDescriptor. On Linux the
# real aeron package provides these. The aeron ConanCenter recipe rejects
# macOS/armv8 (Apple Silicon), so there it is skipped (see conanfile.py) and we
# provide a stub aeron::aeron INTERFACE target so cmake CONFIGURE still succeeds.
# Set -DRUNE_FR_AERON_INCLUDE_DIR=<path to aeron client headers> to actually
# COMPILE on macOS against the cached aeron headers (header-only usage).
set(RUNE_FR_AERON_INCLUDE_DIR "" CACHE PATH "aeron client include dir (macOS header-only fallback)")
find_package(aeron QUIET)
if(NOT TARGET aeron::aeron)
    add_library(rune_fr_aeron_stub INTERFACE)
    if(RUNE_FR_AERON_INCLUDE_DIR)
        target_include_directories(rune_fr_aeron_stub INTERFACE ${RUNE_FR_AERON_INCLUDE_DIR})
        message(STATUS "rune-fr: aeron package not found; using header-only fallback at ${RUNE_FR_AERON_INCLUDE_DIR}")
    else()
        message(WARNING "rune-fr: aeron not found and RUNE_FR_AERON_INCLUDE_DIR unset — "
                        "configure-only stub (build/tests require aeron headers).")
    endif()
    add_library(aeron::aeron ALIAS rune_fr_aeron_stub)
endif()

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
target_link_libraries(rune_fr_core PUBLIC aeron::aeron)
target_compile_definitions(rune_fr_core PUBLIC RUNE_FR=$<BOOL:${RUNE_FR}>)

# Public consumable target: headers + the core lib.
add_library(rune_fr INTERFACE)
target_include_directories(rune_fr INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(rune_fr INTERFACE rune_fr_core aeron::aeron)
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
    target_link_libraries(rune_fr_tests PRIVATE rune_fr GTest::gtest_main nlohmann_json::nlohmann_json)
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
include(CMakePackageConfigHelpers)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
set(RUNE_FR_INSTALL_TARGETS rune_fr rune_fr_core)
if(TARGET rune_fr_aeron_stub)
    # The header-only aeron fallback stub is linked into the exported targets,
    # so it must travel in the same export set (real consumers use find_package(aeron)).
    list(APPEND RUNE_FR_INSTALL_TARGETS rune_fr_aeron_stub)
endif()
install(TARGETS ${RUNE_FR_INSTALL_TARGETS} EXPORT rune_frTargets)
install(EXPORT rune_frTargets NAMESPACE rune_fr:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rune_fr)

# Package config so `find_package(rune_fr CONFIG)` works for raw-CMake consumers.
# (Conan consumers get their own generated config via package_info.)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/rune_frConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/rune_frConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rune_fr)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/rune_frConfigVersion.cmake
    VERSION ${RUNE_FR_VERSION_NUMERIC}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/rune_frConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/rune_frConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rune_fr)
