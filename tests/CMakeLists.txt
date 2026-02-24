# Tests CMakeLists.txt

# Fetch Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)

# CRITICAL: Force static runtime for Google Test
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Unit test sources
set(UNIT_TEST_SOURCES
    unit/test_config.cpp
    unit/test_logger.cpp
)

# Integration test sources (empty for now)
set(INTEGRATION_TEST_SOURCES
    # Will add in later milestones
)

# Create test executable
add_executable(run_tests
    ${UNIT_TEST_SOURCES}
    ${INTEGRATION_TEST_SOURCES}
)

# CRITICAL: Force static runtime on test executable
if(MSVC)
    set_property(TARGET run_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    
    # Force /MTd and /MT flags directly
    target_compile_options(run_tests PRIVATE
        $<$<CONFIG:Debug>:/MTd>
        $<$<CONFIG:Release>:/MT>
    )
endif()

# Link libraries
target_link_libraries(run_tests
    PRIVATE
    sdcore
    gtest
    gtest_main
)

# Include directories
target_include_directories(run_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
)

# Register with CTest
add_test(NAME AllTests COMMAND run_tests)

message(STATUS "Tests configured: run_tests")
message(STATUS "  - Unit tests: ${UNIT_TEST_SOURCES}")
message(STATUS "  - Run with: ctest or run_tests.exe directly")
