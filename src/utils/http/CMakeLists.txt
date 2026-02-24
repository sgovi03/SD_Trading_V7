# HTTP Utilities CMakeLists.txt
# For order submission to Java Spring Boot microservice

cmake_minimum_required(VERSION 3.10)

# Find libcurl (required for HTTP client)
find_package(CURL REQUIRED)

# HTTP utilities library
add_library(http_utils
    http_client.cpp
    order_submitter.cpp
)

target_include_directories(http_utils
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CURL_INCLUDE_DIRS}
)

target_link_libraries(http_utils
    PUBLIC
    ${CURL_LIBRARIES}
)

# Installation
install(TARGETS http_utils
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES
    http_client.h
    order_submitter.h
    DESTINATION include/utils/http
)
