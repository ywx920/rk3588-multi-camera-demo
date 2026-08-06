cmake_minimum_required(VERSION 3.10)
project(zx_camera_minimal VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(zx-camera_minimal
    src/main.cpp
    src/camera_config.cpp
    src/v4l2_camera.cpp
)

target_include_directories(zx-camera_minimal PRIVATE include)
target_compile_options(zx-camera_minimal PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
)

install(TARGETS zx-camera_minimal RUNTIME DESTINATION bin)

