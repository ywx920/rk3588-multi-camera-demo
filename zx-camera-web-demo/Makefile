CXX ?= c++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic -pthread
TARGET := zx-camera_web
QT_PROJECT := ../zx-camera-qt-demo
SOURCES := src/main.cpp src/v4l2_mjpeg_camera.cpp src/jpeg_encoder.cpp \
	src/hardware_jpeg_encoder.cpp \
	$(QT_PROJECT)/src/rknn_yolov5_engine.cpp \
	$(QT_PROJECT)/src/nv12_converter.cpp \
	$(QT_PROJECT)/third_party/rknn/src/yolo.cc \
	$(QT_PROJECT)/third_party/rknn/src/rknn_demo_utils.cc
QT_CFLAGS := $(shell pkg-config --cflags Qt5Core Qt5Gui)
QT_LIBS := $(shell pkg-config --libs Qt5Core Qt5Gui)
CXXFLAGS += $(QT_CFLAGS) -fPIC -DZX_CAMERA_HAVE_RGA=1
LDLIBS := -ljpeg -lrknnrt -lrga $(QT_LIBS) -ldl
ifneq ($(shell pkg-config --exists rockchip_mpp && echo yes),)
CXXFLAGS += $(shell pkg-config --cflags rockchip_mpp) -DZX_CAMERA_HAVE_MPP=1
LDLIBS += $(shell pkg-config --libs rockchip_mpp)
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES) include/v4l2_mjpeg_camera.h include/http_security.h
	$(CXX) $(CXXFLAGS) -Iinclude -I$(QT_PROJECT)/include \
		-I$(QT_PROJECT)/third_party/rknn/include $(SOURCES) -o $@ $(LDLIBS)

clean:
	$(RM) $(TARGET)
