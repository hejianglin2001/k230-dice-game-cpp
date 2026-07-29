cmake_minimum_required(VERSION 3.2)
project(dice_game C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DEPS_DIR    "${PROJECT_SOURCE_DIR}/deps")
set(NNCASE_DIR  "${DEPS_DIR}/nncase")
set(OPENCV_DIR  "${DEPS_DIR}/opencv")
set(V4L2_DIR    "${DEPS_DIR}/v4l2")
set(TOOLCHAIN   "/home/ubuntu/k230_sdk/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1")

set(CMAKE_EXE_LINKER_FLAGS "")

set(src
    src/main.cc
    src/game/game.cc
    src/camera/camera.cc
    src/display/display.cc
    src/detector/detector.cc
    src/key/key.cc
    src/nncase/ai_base.cc
    src/nncase/utils.cc
    src/ui/lvgl_dl.cc
)
set(bin dice_game.elf)

include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${NNCASE_DIR}/include)
include_directories(${NNCASE_DIR}/include/nncase/runtime)
include_directories(${NNCASE_DIR}/rvvlib/include)
include_directories(${OPENCV_DIR}/include/opencv4)
include_directories(${TOOLCHAIN}/sysroot/usr/include)

link_directories(${OPENCV_DIR}/lib)
link_directories(${V4L2_DIR})
link_directories(${OPENCV_DIR}/lib/opencv4/3rdparty)

add_executable(${bin} ${src})

target_link_libraries(${bin}
    nncaseruntime_k230  python3.13
    opencv_imgproc  opencv_imgcodecs  opencv_core
    zlib  jpeg-turbo  openjp2  png  tiff  webp  csi_cv
    dl  rt  pthread)

install(TARGETS ${bin} DESTINATION bin)
