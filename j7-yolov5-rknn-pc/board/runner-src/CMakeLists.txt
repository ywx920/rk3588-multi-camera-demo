cmake_minimum_required(VERSION 3.10)
project(j7_yolov5_runner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-O2 -Wall -D_FILE_OFFSET_BITS=64)

include_directories(
    include
    yolo_utils
    common
    common/rkdemo_utils
    rknn/include
)

add_executable(yolov5_rknn
    src/main.cc
    src/yolo.cc
    yolo_utils/resize_function.cc
    common/rkdemo_utils/rknn_demo_utils.cc
)

target_link_libraries(yolov5_rknn
    rknnrt jpeg png z dl pthread
)
