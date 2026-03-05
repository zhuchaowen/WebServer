cmake_minimum_required(VERSION 3.16)

project(WebServer LANGUAGES CXX)

# 使用 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

# =========================
# base 模块
# =========================
add_library(base
    base/util.cpp
)

target_include_directories(base
    PUBLIC ${PROJECT_SOURCE_DIR}/base
)

# =========================
# http 模块
# =========================
add_library(http
    http/http.cpp
)

target_include_directories(http
    PUBLIC ${PROJECT_SOURCE_DIR}/http
)

target_link_libraries(http
    PUBLIC base
)

# =========================
# 主程序
# =========================
add_executable(server
    main.cpp
)

target_link_libraries(server
    PRIVATE http base
)

# Linux 需要 pthread
target_link_libraries(server PRIVATE pthread)