// DRM 双缓冲显示 — 双 framebuffer 交替 SETCRTC
#include "display/display.h"
#include <iostream>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#define DRM_MODE_CONNECTED 1
#define MAX_PROPS 64
#define MAX_MODES 64
#define MAX_ENCODERS 4

static int drmIoctl(int fd, unsigned long req, void *arg) {
    int ret;
    do { ret = ioctl(fd, req, arg); } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
    return ret;
}

bool SimpleDisplay::open(const char* mode) {
    is_hdmi_ = (mode && strcmp(mode, "hdmi") == 0);
    std::cout << "[DISP] mode=" << (mode?mode:"auto") << std::endl;
    fd = ::open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { std::cerr << "[DISP] open failed" << std::endl; return false; }

    drm_mode_card_res res; std::memset(&res, 0, sizeof(res));
    if (drmIoctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) { close(); return false; }
    if (res.count_connectors == 0 || res.count_crtcs == 0) { close(); return false; }

    // Stack arrays (K230 has at most a few connectors/crtcs)
    uint32_t conn_ids[4], crtc_ids[4], fb_ids[4], enc_ids[4];
    res.connector_id_ptr = (uint64_t)(uintptr_t)conn_ids;
    res.crtc_id_ptr      = (uint64_t)(uintptr_t)crtc_ids;
    res.fb_id_ptr        = (uint64_t)(uintptr_t)fb_ids;
    res.encoder_id_ptr   = (uint64_t)(uintptr_t)enc_ids;
    if (drmIoctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) { close(); return false; }

    bool found = false;
    drm_mode_get_connector cr;
    for (int i = 0; i < (int)res.count_connectors && !found; i++) {
        std::memset(&cr, 0, sizeof(cr));
        cr.connector_id = conn_ids[i];
        if (drmIoctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &cr) < 0) continue;
        if (cr.connection != DRM_MODE_CONNECTED || cr.count_modes == 0) continue;

        drm_mode_modeinfo modes[MAX_MODES];
        uint32_t encoders[MAX_ENCODERS], props[MAX_PROPS];
        uint64_t prop_vals[MAX_PROPS];
        cr.modes_ptr         = (uint64_t)(uintptr_t)modes;
        cr.encoders_ptr      = (uint64_t)(uintptr_t)encoders;
        cr.props_ptr         = (uint64_t)(uintptr_t)props;
        cr.prop_values_ptr   = (uint64_t)(uintptr_t)prop_vals;
        if (drmIoctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &cr) < 0) continue;
        if (cr.count_encoders == 0 || cr.count_modes == 0) continue;

        conn_id = conn_ids[i];
        width = modes[0].hdisplay;
        height = modes[0].vdisplay;
        portrait = (width < height);

        drm_mode_get_encoder er; std::memset(&er, 0, sizeof(er));
        er.encoder_id = encoders[0];
        if (drmIoctl(fd, DRM_IOCTL_MODE_GETENCODER, &er) < 0) continue;
        crtc_id = er.crtc_id;
        if (!crtc_id && res.count_crtcs > 0) crtc_id = crtc_ids[0];  // HDMI 需手动分配
        std::cerr << "[DISP] crtc_id=" << crtc_id << " (" << width << "x" << height;
        std::cerr << " portrait=" << (width < height) << ")" << std::endl;

        // 创建双缓冲
        bool ok = true;
        for (int b = 0; b < 2; b++) {
            drm_mode_create_dumb dq; std::memset(&dq, 0, sizeof(dq));
            dq.width = width; dq.height = height; dq.bpp = 32;
            if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &dq) < 0) { std::cerr << "[DISP] dumb fail" << std::endl; ok = false; break; }
            handle[b] = dq.handle; pitch[b] = dq.pitch; size[b] = dq.size;

            struct _fb2 { uint32_t fb_id, width, height, pixel_format, flags; uint32_t handles[4], pitches[4], offsets[4]; uint64_t modifier[4]; } fb2;
            std::memset(&fb2, 0, sizeof(fb2));
            fb2.width = width; fb2.height = height;
            fb2.pixel_format = 0x34325258; fb2.handles[0] = handle[b]; fb2.pitches[0] = pitch[b];
            if (drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &fb2) < 0) {
                fb2.pixel_format = 0x34325241;
                if (drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &fb2) < 0) { std::cerr << "[DISP] adf2 fail" << std::endl; ok = false; break; }
            }
            fb_id[b] = fb2.fb_id;

            drm_mode_map_dumb mq; std::memset(&mq, 0, sizeof(mq));
            mq.handle = handle[b];
            if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mq) < 0) { std::cerr << "[DISP] map fail" << std::endl; ok = false; break; }
            map[b] = (uint32_t *)mmap(0, size[b], PROT_READ | PROT_WRITE, MAP_SHARED, fd, mq.offset);
            if (map[b] == MAP_FAILED) { std::cerr << "[DISP] mmap fail" << std::endl; ok = false; break; }
            std::memset(map[b], 0, size[b]);
        }
        if (!ok) { for (int b = 0; b < 2; b++) if (map[b]) { munmap(map[b], size[b]); map[b] = nullptr; } continue; }

        // 初始显示
        drm_mode_crtc rc; std::memset(&rc, 0, sizeof(rc));
        rc.set_connectors_ptr = (uint64_t)(uintptr_t)&conn_id;
        rc.count_connectors = 1; rc.crtc_id = crtc_id; rc.fb_id = fb_id[0];
        rc.mode_valid = 1;
        std::memcpy(&rc.mode, &modes[0], sizeof(modes[0]));
        if (drmIoctl(fd, DRM_IOCTL_MODE_SETCRTC, &rc) < 0) {
            std::cerr << "[DISP] SETCRTC fail: " << strerror(errno) << std::endl;
            for (int b = 0; b < 2; b++) if (map[b]) { munmap(map[b], size[b]); map[b] = nullptr; } continue;
        }
        cur_buf = 0;
        found = true;
    }
    if (!found) { std::cerr << "[DISP] no display" << std::endl; close(); return false; }
    std::cout << "[DISP] " << width << "x" << height << " dualbuf" << std::endl;
    return true;
}

// 翻页: HDMI 直接用 SETCRTC, LCD 用 PAGE_FLIP
static void do_flip(int fd, uint32_t crtc, uint32_t fb, uint32_t *conn, bool hdmi) {
    if (hdmi) {
        // HDMI: PAGE_FLIP + 16ms vsync 等待, 防止 buffer 争抢噪点
        struct _pf { uint32_t crtc_id, fb_id, flags, reserved; uint64_t user_data; } pf;
        std::memset(&pf, 0, sizeof(pf));
        pf.crtc_id = crtc; pf.fb_id = fb; pf.flags = 1;
        if (drmIoctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf) == 0) {
            fd_set fs; FD_ZERO(&fs); FD_SET(fd, &fs);
            timeval tv = {0, 16666}; // 16.6ms = 60Hz
            if (select(fd+1, &fs, nullptr, nullptr, &tv) > 0) { char b[128]; read(fd,b,sizeof(b)); return; }
        }
        // fallback SETCRTC
        drm_mode_crtc rc; std::memset(&rc, 0, sizeof(rc));
        rc.set_connectors_ptr = (uint64_t)(uintptr_t)conn;
        rc.count_connectors = 1; rc.crtc_id = crtc; rc.fb_id = fb; rc.mode_valid = 0;
        drmIoctl(fd, DRM_IOCTL_MODE_SETCRTC, &rc);
        return;
    }
    // drain 旧事件
    fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
    timeval tv = {0, 0};
    while (select(fd+1, &fds, nullptr, nullptr, &tv) > 0) { char b[128]; read(fd,b,sizeof(b)); FD_ZERO(&fds); FD_SET(fd,&fds); }

    struct _pf { uint32_t crtc_id, fb_id, flags, reserved; uint64_t user_data; } pf;
    std::memset(&pf, 0, sizeof(pf));
    pf.crtc_id = crtc; pf.fb_id = fb; pf.flags = 1;
    if (drmIoctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf) == 0) {
        // 等 8ms 让 flip 完成 (非阻塞, 短超时)
        tv.tv_usec = 8000; FD_ZERO(&fds); FD_SET(fd, &fds);
        if (select(fd+1, &fds, nullptr, nullptr, &tv) > 0) { char b[128]; read(fd,b,sizeof(b)); }
        return;
    }
    // fallback
    drm_mode_crtc rc; std::memset(&rc, 0, sizeof(rc));
    rc.set_connectors_ptr = (uint64_t)(uintptr_t)conn;
    rc.count_connectors = 1; rc.crtc_id = crtc; rc.fb_id = fb; rc.mode_valid = 0;
    drmIoctl(fd, DRM_IOCTL_MODE_SETCRTC, &rc);
}

void SimpleDisplay::show_frame(uint8_t *src, int sw, int sh) {
    if (!map[0] || !map[1]) return;
    int next = cur_buf ^ 1;
    uint8_t *dst = (uint8_t *)map[next];
    uint32_t p = pitch[next];

    if (portrait) {
        // LCD 竖屏: 旋转 90° + 缩放
        for (int dy = 0; dy < height; dy++) {
            uint32_t *row = (uint32_t *)(dst + dy * p);
            for (int dx = 0; dx < width; dx++) {
                int sx = dy * sw / height;
                int sy = (sh - 1) - (dx * sh / width);
                if (sx < 0) sx = 0; if (sx >= sw) sx = sw - 1;
                if (sy < 0) sy = 0; if (sy >= sh) sy = sh - 1;
                uint8_t *px = src + (sy * sw + sx) * 3;
                row[dx] = 0xFF000000 | ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2];
            }
        }
    } else {
        // HDMI 横屏: 逐字节写, 避免 ARGB/XRGB 字节序混淆
        float scl = std::min((float)width / sw, (float)height / sh);
        int iw = sw * scl, ih = sh * scl;
        int ox = (width - iw) / 2, oy = (height - ih) / 2;
        std::memset(dst, 0, (size_t)height * p);
        for (int y = 0; y < ih; y++) {
            uint8_t *row = dst + (oy + y) * p;
            int sy = y * sh / ih;
            uint8_t *srow = src + sy * sw * 3;
            for (int x = 0; x < iw; x++) {
                int sx = x * sw / iw;
                uint8_t *px = srow + sx * 3;
                // 写 BGRA 字节 (ARGB8888 = BGRA in memory on little-endian)
                row[(ox+x)*4+0] = px[2];  // R(=B)
                row[(ox+x)*4+1] = px[1];  // G
                row[(ox+x)*4+2] = px[0];  // B(=R)
                row[(ox+x)*4+3] = 0xFF;   // A
            }
        }
    }
    do_flip(fd, crtc_id, fb_id[next], &conn_id, is_hdmi_);
    cur_buf = next;
}

void SimpleDisplay::show_rgba(uint8_t *src, int sw, int sh) {
    if (!map[0] || !map[1]) return;
    int next = cur_buf ^ 1;
    uint8_t *dst = (uint8_t *)map[next];
    uint32_t p = pitch[next];
    for (int y = 0; y < height; y++) {
        int sy = y * sh / height, d_off = y * p, s_off = sy * sw * 4;
        int len = width * 4;
        if (len > (int)(size[next] - d_off)) len = size[next] - d_off;
        if (len > sw * sh * 4 - s_off) len = sw * sh * 4 - s_off;
        if (len > 0) std::memcpy(dst + d_off, src + s_off, len);
    }
    do_flip(fd, crtc_id, fb_id[next], &conn_id, is_hdmi_);
    cur_buf = next;
}

// 摄像头 BGR + LVGL OSD 合成到 DRM (一次 flip)
void SimpleDisplay::show_frame_with_osd(uint8_t *bgr, int cw, int ch,
                                         uint8_t *osd, int ow, int oh) {
    if (!map[0] || !map[1]) return;
    int next = cur_buf ^ 1;
    uint8_t *dst = (uint8_t *)map[next];
    uint32_t p = pitch[next];

    // 1. 摄像头 BGR → DRM (旋转 90° + 缩放)
    for (int dy = 0; dy < height; dy++) {
        uint32_t *row = (uint32_t *)(dst + dy * p);
        for (int dx = 0; dx < width; dx++) {
            int sx = dy * cw / height;
            int sy = (ch - 1) - (dx * ch / width);
            if (sx < 0) sx = 0; if (sx >= cw) sx = cw - 1;
            if (sy < 0) sy = 0; if (sy >= ch) sy = ch - 1;
            uint8_t *px = bgr + (sy * cw + sx) * 3;
            row[dx] = 0xFF000000 | ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2];
        }
    }

    // 2. LVGL OSD 旋转 90° 叠加 (alpha blending)
    if (osd) {
        for (int dy = 0; dy < height; dy++) {
            uint32_t *row = (uint32_t *)(dst + dy * p);
            for (int dx = 0; dx < width; dx++) {
                int sx = dy * ow / height;
                int sy = (oh - 1) - (dx * oh / width);
                if (sx < 0) sx = 0; if (sx >= ow) sx = ow - 1;
                if (sy < 0) sy = 0; if (sy >= oh) sy = oh - 1;
                uint32_t osd_px = ((uint32_t*)osd)[sy * ow + sx];
                uint8_t a = (osd_px >> 24) & 0xFF;
                if (a > 0) row[dx] = osd_px;  // 不透明直接覆盖
            }
        }
    }

    do_flip(fd, crtc_id, fb_id[next], &conn_id, is_hdmi_);
    cur_buf = next;
}

// LVGL OSD → DRM (竖屏旋转90°, 横屏直接缩放居中)
void SimpleDisplay::show_rgba_landscape(uint8_t *src, int sw, int sh) {
    if (!map[0] || !map[1]) return;
    int next = cur_buf ^ 1;
    uint8_t *dst = (uint8_t *)map[next];
    uint32_t p = pitch[next];

    if (portrait) {
        for (int dy = 0; dy < height; dy++) {
            uint32_t *row = (uint32_t *)(dst + dy * p);
            for (int dx = 0; dx < width; dx++) {
                int sx = dy * sw / height;
                int sy = (sh - 1) - (dx * sh / width);
                if (sx<0) sx=0; if (sx>=sw) sx=sw-1;
                if (sy<0) sy=0; if (sy>=sh) sy=sh-1;
                row[dx] = ((uint32_t*)src)[sy * sw + sx];
            }
        }
    } else {
        // HDMI: Lvgl ARGB→DRM ARGB 逐字节拷贝
        float scl = std::min((float)width / sw, (float)height / sh);
        int iw = sw * scl, ih = sh * scl;
        int ox = (width - iw) / 2, oy = (height - ih) / 2;
        std::memset(dst, 0, (size_t)height * p);
        for (int y = 0; y < ih; y++) {
            int sy = y * sh / ih;
            uint8_t *srow = ((uint8_t*)src) + sy * sw * 4;
            uint8_t *drow = dst + (oy + y) * p;
            for (int x = 0; x < iw; x++) {
                int sx = x * sw / iw;
                // BGRA byte order
                drow[(ox+x)*4+0] = srow[sx*4+0];
                drow[(ox+x)*4+1] = srow[sx*4+1];
                drow[(ox+x)*4+2] = srow[sx*4+2];
                drow[(ox+x)*4+3] = srow[sx*4+3];
            }
        }
    }
    do_flip(fd, crtc_id, fb_id[next], &conn_id, is_hdmi_);
    cur_buf = next;
}

void SimpleDisplay::close() {
    for (int b = 0; b < 2; b++) {
        if (map[b]) { munmap(map[b], size[b]); map[b] = nullptr; }
        if (handle[b] && fd >= 0) {
            drm_mode_destroy_dumb dq; std::memset(&dq, 0, sizeof(dq));
            dq.handle = handle[b]; drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dq);
        }
    }
    if (fd >= 0) { ::close(fd); fd = -1; }
}
SimpleDisplay::~SimpleDisplay() { close(); }
