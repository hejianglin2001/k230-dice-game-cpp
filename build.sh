#!/bin/bash
# 骰子游戏 C++ 编译 / 部署脚本
#
# 用法:
#   ./build.sh               # 编译
#   ./build.sh clean         # 清理
#   ./build.sh deploy        # 编译 + adb 推送到板端
#   ./build.sh run           # 编译 + 推送 + 板端运行

set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOLCHAIN_PATH="/home/ubuntu/k230_sdk/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/bin"
export PATH="${TOOLCHAIN_PATH}:${PATH}"
BUILD_DIR="${PROJECT_DIR}/build"
ELF="${BUILD_DIR}/bin/dice_game.elf"

# 板端路径 — 放在 /root/app/dice_game/ 下，跟官方 demo 风格一致
BOARD_DIR="/root/app/dice_game"
BOARD_ELF="${BOARD_DIR}/dice_game.elf"

# kmodel — 把你的骰子模型放到项目根目录，部署时一起推上去
KMODEL_SRC="${PROJECT_DIR}/DR_yolo8n.kmodel"
KMODEL_DST="${BOARD_DIR}/DR_yolo8n.kmodel"

# ── 清理 ──────────────────────────────────────────
if [ "$1" = "clean" ]; then
    echo "Cleaning..."
    rm -rf "${BUILD_DIR}"
    echo "Done."
    exit 0
fi

# ── 编译 ──────────────────────────────────────────
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
pushd "${BUILD_DIR}"

cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}" \
      -DCMAKE_TOOLCHAIN_FILE="${PROJECT_DIR}/deps/cmake/Riscv64_glibc.cmake" \
      -DDISPLAY_MODE="${DISPLAY_MODE:-lcd}" \
      "${PROJECT_DIR}"

make -j$(nproc) && make install
popd

echo "============================================"
echo "Output: ${ELF}"
echo "============================================"

# ── 部署 ──────────────────────────────────────────
if [ "$1" = "deploy" ] || [ "$1" = "run" ]; then
    echo "Pushing to board..."
    adb shell "mkdir -p ${BOARD_DIR}"
    adb push "${ELF}" "${BOARD_ELF}"
    adb push "${PROJECT_DIR}/run.sh" "${BOARD_DIR}/run.sh"
    adb push "${PROJECT_DIR}/S99dice" /etc/init.d/S99dice
    adb shell "chmod +x ${BOARD_DIR}/run.sh ${BOARD_ELF}"
    adb shell "chmod +x /etc/init.d/S99dice"
    # 推送 assets (首次)
    adb shell "mkdir -p ${BOARD_DIR}/assets/ui/star"
    adb push assets/ui/*.png ${BOARD_DIR}/assets/ui/ 2>/dev/null
    adb push assets/ui/star/*.png ${BOARD_DIR}/assets/ui/star/ 2>/dev/null
    if [ -f "${KMODEL_SRC}" ]; then
        adb push "${KMODEL_SRC}" "${KMODEL_DST}"
        echo "  kmodel: ${KMODEL_DST}"
    else
        echo "  (kmodel not found at ${KMODEL_SRC}, skipped)"
    fi
    adb shell sync
    echo "OK: ${BOARD_ELF}"
fi

# ── 运行 ──────────────────────────────────────────
if [ "$1" = "run" ]; then
    echo "Running on board..."
    adb shell "cd ${BOARD_DIR} && ./run.sh"
fi
