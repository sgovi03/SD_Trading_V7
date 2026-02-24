# Source directory CMakeLists.txt

# Add subdirectories
add_subdirectory(core)
add_subdirectory(zones)

add_subdirectory(analysis)
add_subdirectory(utils/http)        # ← ADD THIS LINE — builds http_utils.lib

# Collect all sources
set(ALL_LIB_SOURCES
    ${CORE_SOURCES}
    ${ZONES_SOURCES}
    ${ANALYSIS_SOURCES}
)

# Prepend paths
set(FULL_SOURCES "")
foreach(src ${CORE_SOURCES})
    list(APPEND FULL_SOURCES "core/${src}")
endforeach()
foreach(src ${ZONES_SOURCES})
    list(APPEND FULL_SOURCES "zones/${src}")
endforeach()
foreach(src ${ANALYSIS_SOURCES})
    list(APPEND FULL_SOURCES "analysis/${src}")
endforeach()

# Update parent library
set(LIB_SOURCES ${FULL_SOURCES} PARENT_SCOPE)

message(STATUS "Source modules configured:")
message(STATUS "  - Core: ${CORE_SOURCES}")
message(STATUS "  - Zones: ${ZONES_SOURCES}")
message(STATUS "  - Analysis: ${ANALYSIS_SOURCES}")
