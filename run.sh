#!/bin/sh
# 骰子游戏启动脚本
# 用法: ./run.sh [conf_thresh] [nms_thresh] [debug_mode]

cd "$(dirname "$0")"
KMODEL="./DR_yolo8n.kmodel"

if [ ! -f "$KMODEL" ]; then
    echo "Error: $KMODEL not found!"
    echo "Please put your model file here: $(pwd)/DR_yolo8n.kmodel"
    exit 1
fi

# 确保 nncase 库能找到
export LD_LIBRARY_PATH=/usr/lib/python3.13/site-packages/nncaseruntime:$LD_LIBRARY_PATH

exec ./dice_game.elf "$KMODEL" "$@"
