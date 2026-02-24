# Core library CMakeLists.txt

# Core library source files
set(CORE_SOURCES
    config_loader.cpp
)

# Create static library
add_library(sdcore STATIC ${CORE_SOURCES})

# Include directories
target_include_directories(sdcore
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
)


# ── ADD THIS BLOCK ───────────────────────────────────────────
target_link_libraries(sdcore
    PUBLIC
    http_utils
)
# ─────────────────────────────────────────────────────────────

# Set properties
set_target_properties(sdcore PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Installation
install(TARGETS sdcore
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

message(STATUS "Core library configured: sdcore")
