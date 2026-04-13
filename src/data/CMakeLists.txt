# ==================================================================
# SD Trading V8 — Data Ingestion Library CMakeLists.txt
# src/data/CMakeLists.txt
# Milestones M2.2, M2.3, M2.6
# ==================================================================

add_library(sd_data STATIC
    bar_validator.cpp        # M2.3
    pipe_listener.cpp        # M2.2
    shadow_mode.cpp          # M2.6
)

target_include_directories(sd_data PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sd_data PUBLIC sd_core)

target_compile_features(sd_data PUBLIC cxx_std_17)

if(WIN32)
    target_link_libraries(sd_data PUBLIC ws2_32)
endif()

message(STATUS "[Data] bar_validator + pipe_listener + shadow_mode")
