cmake_minimum_required(VERSION 3.16)
project(zx_camera_qt_demo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Widgets)
find_path(RGA_INCLUDE_DIR NAMES rga/im2d.h)
find_library(RGA_LIBRARY NAMES rga)

add_executable(zx-camera_qt_demo
    include/frame_worker.h
    include/camera_panel.h
    include/main_window.h
    include/detection_types.h
    include/inference_worker.h
    include/inference_scheduler.h
    include/rknn_yolov5_engine.h
    src/main.cpp
    src/main_window.cpp
    src/frame_worker.cpp
    src/camera_panel.cpp
    src/nv12_converter.cpp
    src/v4l2_frame_source.cpp
    src/inference_worker.cpp
    src/inference_scheduler.cpp
    src/rknn_yolov5_engine.cpp
    third_party/rknn/src/yolo.cc
    third_party/rknn/src/rknn_demo_utils.cc
)

target_include_directories(zx-camera_qt_demo PRIVATE
    include
    third_party/rknn/include
)
target_link_libraries(zx-camera_qt_demo PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    rknnrt
    pthread
    dl
)
target_compile_options(zx-camera_qt_demo PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
)

if(RGA_INCLUDE_DIR AND RGA_LIBRARY)
    target_include_directories(zx-camera_qt_demo PRIVATE ${RGA_INCLUDE_DIR})
    target_link_libraries(zx-camera_qt_demo PRIVATE ${RGA_LIBRARY})
    target_compile_definitions(zx-camera_qt_demo PRIVATE ZX_CAMERA_HAVE_RGA=1)
    message(STATUS "RGA acceleration enabled: ${RGA_LIBRARY}")
else()
    message(STATUS "RGA not found; using CPU image conversion fallback")
endif()
