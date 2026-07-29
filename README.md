# Dice Game (骰子猜拳游戏)

## 概述

K230 板端 C++ 骰子猜拳游戏。摄像头采集画面 → YOLO KPU 推理 → 识别石头/剪刀/布 → 结果叠加显示。

## 板端环境

- **硬件**: 01Studio CanMV-K230
- **固件镜像**: `CanMV-K230_01studio_linux_v1.2_nncase_v2.11.0_2dd270f7.img.gz`
- **系统**: Linux (RISC-V 64-bit, glibc)
- **nncase**: v2.11.0

## 项目结构

```
dice_game/
├── CMakeLists.txt              # CMake 构建配置
├── build.sh                    # 一键编译 + ADB 推送
├── README.md
├── assets/ui/                  # UI 图片资源 (PNG)
├── deps/                       # 交叉编译依赖 (从 k230_sdk 抽出)
│   ├── nncase/                 #   nncase 头文件
│   ├── opencv/                 #   OpenCV 交叉编译库 (symlink)
│   ├── mpp/                    #   MPP 静态库 (sys.a)
│   └── v4l2/                   #   板端 .so 文件
├── micopyton_project/          # 旧 MicroPython 版本 (参考)
└── src/
    ├── main.cc                 # 入口 (12 行)
    ├── config.h                # 全局常量 + 状态枚举
    ├── game/
    │   ├── game.h              #   GameController 声明
    │   └── game.cc             #   状态机 + 摄像头 + 推理 + 叠加 (核心)
    ├── camera/
    │   ├── camera.h            #   V4L2 摄像头 (SimpleCamera)
    │   └── camera.cc           #   open/mmap/stream/dqbuf
    ├── display/
    │   ├── display.h           #   DRM 双缓冲显示 (SimpleDisplay)
    │   └── display.cc          #   dumb buffer + page flip
    ├── detector/
    │   ├── detector.h          #   YOLO KPU 检测器 (DiceDetector)
    │   └── detector.cc         #   AI2D 预处理 + 推理 + NMS
    ├── key/
    │   ├── key.h               #   GPIO 按键 (Key)
    │   └── key.cc              #   Linux chardev uAPI
    ├── nncase/
    │   ├── ai_base.h/cc        #   nncase 推理基类 (AIBase)
    │   ├── utils.h/cc          #   图像处理 (AI2D resize/pad)
    │   └── scoped_timing.hpp   #   耗时统计
    └── ui/
        ├── lvgl_dl.h           #   LVGL 动态加载 (LvglDL)
        └── lvgl_dl.cc          #   dlopen/dlsym, HOME 页 UI
```

## 编译

### 前置条件

- K230 SDK 路径: `/home/ubuntu/k230_sdk/` (只需要 toolchain)
- 交叉编译器: Xuantie-900 GCC 10.4.0 glibc (路径在 build.sh 中配置)

### 编译命令

```bash
cd dice_game
./build.sh              # 编译
./build.sh clean        # 清理
./build.sh deploy       # 编译 + adb push 到板端
```

产物: `build/bin/dice_game.elf` (~4.3MB, RISC-V 动态链接)

## 部署与运行

```bash
# 1. 推送资源文件 (首次)
adb shell mkdir -p /root/app/dice_game/assets/ui/star
adb push assets/ui/*.png /root/app/dice_game/assets/ui/
adb push assets/ui/star/*.png /root/app/dice_game/assets/ui/star/

# 2. 推送可执行文件
./build.sh deploy

# 3. 板端运行
cd /root/app/dice_game
ln -sf /usr/lib/python3.13/site-packages/nncaseruntime/_nncaseruntime_k230*.so /usr/lib/libnncaseruntime_k230.so
./dice_game.elf ./DR_yolo8n.kmodel [conf_thresh] [nms_thresh] [debug_mode]
```

### 参数说明

| 参数 | 说明 | 默认值 |
|---|---|---|
| kmodel | YOLOv8 kmodel 路径 | 必填 |
| conf_thresh | 置信度阈值 | 0.7 |
| nms_thresh | NMS IoU 阈值 | 0.3 |
| debug_mode | 调试级别 | 0=安静, 1=耗时, 2=详细 |

## 游戏流程

```
HOME ──按键──▶ CAMERA ──按键──▶ DETECT ──3秒──▶ LOCKED ──按键──▶ HOME
```

| 状态 | 说明 | 摄像头 | 模型 | 显示 |
|---|---|---|---|---|
| HOME | 待机页面 | 延迟关闭 | 未加载 | LVGL UI (背景+星星动画) |
| CAMERA | 预览 | 开启 | 已加载 | 实时画面 |
| DETECT | 推理 | 开启 | 推理中 | 画面+绿框+检测结果 |
| LOCKED | 锁定 | 开启 | 已卸载 | 画面+黄框+大图结果 |

## 技术栈

| 组件 | 方案 |
|---|---|
| 推理框架 | nncase runtime v2.11 (板端 .so, 动态链接) |
| AI2D 预处理 | K230 硬件 AI2D (padding + resize to 320x320) |
| 摄像头 | Linux V4L2 API, /dev/video2, BGR3 1280x720, mmap 零拷贝 |
| 显示 | DRM dumb buffer, 双缓冲, 480x800 竖屏 (旋转显示) |
| UI | LVGL v9, dlopen 动态加载, PNG 通过 OpenCV 解码 |
| 按键 | GPIO21, Linux chardev uAPI (/dev/gpiochip0) |
| 检测框 | OpenCV cv::rectangle + cv::putText |
| 结果图 | OpenCV Alpha 混合 (BGRA PNG → BGR 摄像头帧) |

## 已知限制

- CMA reserved 池偏小，摄像头开关有 warning 但不影响功能
- LVGL 通过 dlopen 加载，编译时不链 liblvgl.so (避免 GCC 版本冲突)
- nncase 库来自板端 `_nncaseruntime_k230*.so`，需要创建 symlink
