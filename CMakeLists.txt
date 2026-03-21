cmake_minimum_required(VERSION 4.1)

project(WebServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-Wall -O2 -DNDEBUG)

include_directories(
        ${PROJECT_SOURCE_DIR}/src/http
        ${PROJECT_SOURCE_DIR}/src/log
        ${PROJECT_SOURCE_DIR}/src/socket
        ${PROJECT_SOURCE_DIR}/src/timer
        ${PROJECT_SOURCE_DIR}/src/utils
)

file(GLOB_RECURSE SRC_FILES "${PROJECT_SOURCE_DIR}/src/*.cpp")

add_executable(server main.cpp ${SRC_FILES})

target_link_libraries(server pthread)