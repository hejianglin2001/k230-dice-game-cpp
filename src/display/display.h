// DRM 双缓冲显示 — 直接操作 /dev/dri/card0 帧缓冲
//
// LCD 物理 800x480 横屏, DRM 报告 480x800 竖屏 (显示控制器旋转)
// show_frame:  摄像头 BGR 1280x720 → 旋转90°+缩放 → DRM 480x800
// show_rgba_landscape: LVGL ARGB 800x480 横屏 → 旋转90° → DRM 480x800
// show_frame_with_osd: 摄像头 + LVGL OSD 合成 → DRM (一次 Flip)

#pragma once
#include <cstdint>

class SimpleDisplay {
public:
    bool open(int fb_id = 0);

    // 显示摄像头帧 (BGR3 1280x720 → 旋转+缩放 → 竖屏 DRM)
    void show_frame(uint8_t *bgr888, int w, int h);

    // 直接显示 RGBA (无旋转, 尺寸必须匹配 DRM)
    void show_rgba(uint8_t *rgba, int w, int h);

    // 横屏 LVGL OSD 旋转 90° 写入竖屏 DRM
    void show_rgba_landscape(uint8_t *rgba, int sw, int sh);

    // 摄像头 + LVGL OSD 合成到同一帧 (暂未使用)
    void show_frame_with_osd(uint8_t *bgr, int cw, int ch,
                             uint8_t *osd, int ow, int oh);

    void close();
    ~SimpleDisplay();

    int width = 0, height = 0;    // DRM 帧缓冲尺寸 (480x800)

private:  // 与 display.cc 变量名保持一致
    int fd = -1;                  // /dev/dri/card0 文件描述符
    uint32_t crtc_id = 0;        // CRTC 硬件 ID
    uint32_t conn_id = 0;        // Connector 硬件 ID

    // 双缓冲: 渲染到 cur_buf^1, 翻页后成为 cur_buf
    // 交替渲染+翻页防止画面撕裂 (Double Buffering)
    uint32_t fb_id[2] = {};      // DRM framebuffer ID
    uint32_t handle[2] = {};     // GEM 缓冲句柄
    uint32_t pitch[2] = {};      // 每行字节数 (stride)
    uint32_t size[2] = {};       // 缓冲总大小
    uint32_t *map[2] = {};       // mmap 映射到用户空间
    int cur_buf = 0;             // 当前显示的缓冲索引 (0 或 1)
};
