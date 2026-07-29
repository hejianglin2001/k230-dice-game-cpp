#!/bin/bash
# 去掉静态库中所有 .o 文件的 .riscv.attributes section（处理重名文件）
set -e

OBJCOPY="/home/ubuntu/k230_sdk/toolchain/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-objcopy"

strip_one_archive() {
    local lib="$1"
    local name=$(basename "$lib")
    local tmpdir=$(mktemp -d)

    echo "  $name ..."

    local members=$(ar t "$lib" 2>/dev/null)
    local count=0

    # 用关联数组记录重名
    declare -A seen

    while IFS= read -r member; do
        [ -z "$member" ] && continue
        count=$((count + 1))

        # 处理重名：用计数器做后缀
        local out_name="$member"
        local seen_cnt="${seen[$member]:-0}"
        if [ "$seen_cnt" -gt 0 ]; then
            local base="${member%.o}"
            out_name="${base}_dup${seen_cnt}.o"
        fi
        seen[$member]=$(( seen_cnt + 1 ))

        # 提取单个 .o
        (cd "$tmpdir" && ar x "$lib" "$member" 2>/dev/null)

        # 如果 member 名含路径，展平到 tmpdir
        if [ -f "$tmpdir/$member" ]; then
            if [ "$out_name" != "$member" ]; then
                mv "$tmpdir/$member" "$tmpdir/$out_name"
            fi
        fi

        # Strip .riscv.attributes
        "$OBJCOPY" --remove-section=.riscv.attributes "$tmpdir/$out_name" 2>/dev/null || true

    done <<< "$members"

    # 重新打包
    rm -f "$lib"
    (cd "$tmpdir" && ar rcs "$lib" *.o 2>/dev/null)
    rm -rf "$tmpdir"
    echo "    $count members processed"
}

echo "Stripping nncase libs..."
for lib in /home/ubuntu/hjl/dice_game/deps/nncase/lib/*.a /home/ubuntu/hjl/dice_game/deps/nncase/rvvlib/*.a; do
    strip_one_archive "$lib"
done

echo "Stripping MPP libs..."
for lib in /home/ubuntu/hjl/dice_game/deps/mpp/userapps/lib/*.a; do
    strip_one_archive "$lib"
done

echo "Stripping OpenCV libs..."
for lib in /home/ubuntu/hjl/dice_game/deps/opencv/lib/*.a; do
    strip_one_archive "$lib"
done

echo "=== All stripped ==="
