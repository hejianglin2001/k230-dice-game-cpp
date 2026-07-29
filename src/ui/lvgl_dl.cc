// LVGL 骰子游戏 UI — Python dice_ui.py 对齐
#include "ui/lvgl_dl.h"
#include <iostream>
#include <cstring>
#include <fstream>
#include <dlfcn.h>

// ── lv_image_dsc_t 正确结构 (from lvgl v9.2.2) ──
struct __attribute__((packed)) lv_img_hdr { uint32_t magic_cf_flags, wh, stride_reserved; };
struct lv_img_dsc_t { lv_img_hdr header; uint32_t data_size; const uint8_t *data; const void *reserved; };

#include <chrono>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

static void*  (*p_lv_init)(void);
static void*  (*p_lv_display_create)(int,int);
static void   (*p_lv_display_set_default)(void*);
static void   (*p_lv_display_set_flush_cb)(void*,void(*)(void*,void*,uint8_t*));
static void   (*p_lv_display_flush_ready)(void*);
static void   (*p_lv_display_set_buffers)(void*,void*,void*,uint32_t,int);
static void*  (*p_lv_obj_create)(void*);
static void   (*p_lv_obj_set_style_bg_color)(void*,uint32_t,int);
static void   (*p_lv_obj_set_style_bg_opa)(void*,uint8_t,int);
static void   (*p_lv_obj_set_style_text_color)(void*,uint32_t,int);
static void   (*p_lv_obj_set_style_border_width)(void*,int32_t,int);
static void   (*p_lv_obj_set_style_border_color)(void*,uint32_t,int);
static void   (*p_lv_obj_set_size)(void*,int,int);
static void   (*p_lv_obj_set_pos)(void*,int,int);
static void   (*p_lv_obj_align)(void*,int,int,int);
static void   (*p_lv_obj_center)(void*);
static void*  (*p_lv_label_create)(void*);
static void   (*p_lv_label_set_text)(void*,const char*);
static void*  (*p_lv_image_create)(void*);
static void   (*p_lv_image_set_src)(void*,const void*);
static void   (*p_lv_screen_load)(void*);
static uint32_t (*p_lv_color_hex)(uint32_t);
static uint32_t (*p_lv_color_make)(uint8_t,uint8_t,uint8_t);
static int32_t  (*p_lv_timer_handler)(void);
static void*  (*p_lv_timer_create)(void(*)(void*),uint32_t,void*);
static void   (*p_lv_tick_inc)(uint32_t);

#define DL(fn) p_##fn = reinterpret_cast<decltype(p_##fn)>(dlsym(hdl, #fn))

static uint8_t *g_osd = nullptr;
static int g_w = 0, g_h = 0;
struct lv_area_t { int32_t x1,y1,x2,y2; };
static void flush_cb(void* disp, void* a_v, uint8_t* px) {
    auto* a = (lv_area_t*)a_v; if (!g_osd) return;
    int aw = a->x2-a->x1+1, ah = a->y2-a->y1+1;
    for (int y=0; y<ah; y++) {
        int d=((a->y1+y)*g_w+a->x1)*4, s=y*aw*4;
        if (d+aw*4<=g_w*g_h*4) std::memcpy(g_osd+d, px+s, aw*4);
    }
    p_lv_display_flush_ready(disp);
}

// ── 星星闪烁 (star1底图不动, star2↔star3交替) ──
static void* star_glows[3] = {};
static const void *star_src2 = nullptr, *star_src3 = nullptr;
static int star_toggle = 0;
static bool star_anim = true;

static void twinkle_cb(void*) {
    if (!star_anim) return;
    star_toggle = !star_toggle;
    const void* src = star_toggle ? star_src3 : star_src2;
    for (auto* g : star_glows) if (g) p_lv_image_set_src(g, src);
}

bool LvglDL::init(int w, int h) {
    width = w; height = h; g_w = w; g_h = h;
    void* hdl = dlopen("liblvgl.so", RTLD_LAZY);
    if (!hdl) { std::cerr << "[LVGL] dlopen failed" << std::endl; return false; }
    handle_ = hdl;
    DL(lv_init); DL(lv_display_create); DL(lv_display_set_default);
    DL(lv_display_set_flush_cb); DL(lv_display_flush_ready); DL(lv_display_set_buffers);
    DL(lv_obj_create); DL(lv_obj_set_style_bg_color); DL(lv_obj_set_style_bg_opa);
    DL(lv_obj_set_style_text_color); DL(lv_obj_set_style_border_width); DL(lv_obj_set_style_border_color);
    DL(lv_obj_set_size); DL(lv_obj_set_pos); DL(lv_obj_align); DL(lv_obj_center);
    DL(lv_label_create); DL(lv_label_set_text);
    DL(lv_image_create); DL(lv_image_set_src);
    DL(lv_screen_load); DL(lv_color_hex); DL(lv_color_make);
    DL(lv_timer_handler); DL(lv_timer_create); DL(lv_tick_inc);

    p_lv_init();
    auto* disp = p_lv_display_create(w, h);
    p_lv_display_set_default(disp);

    buf1_.resize(w*h*4); buf2_.resize(w*h*4); osd_buf_.resize(w*h*4);
    std::memset(buf1_.data(),0,buf1_.size()); std::memset(buf2_.data(),0,buf2_.size()); std::memset(osd_buf_.data(),0,osd_buf_.size());
    g_osd = osd_buf_.data();
    p_lv_display_set_buffers(disp, buf1_.data(), buf2_.data(), w*h*4, 0);
    p_lv_display_set_flush_cb(disp, flush_cb);

    // ── HOME 页 ──
    // ── HOME 页 ──
    auto* home = p_lv_obj_create(nullptr);
    p_lv_obj_set_style_bg_color(home, p_lv_color_hex(0x000000), 0);
    p_lv_obj_set_style_bg_opa(home, 255, 0);
    p_lv_obj_set_size(home, w, h);

    // dp: 设计稿 1366x938 居中于 1920x1080, 等比缩放到当前屏幕 w×h
    // HDMI 坐标 = (277+x, 121+y) → 任意屏幕等比
    // 等比例缩放，用 HDMI 偏移等比缩确保位置一致
    // Python: dp(x,y) = (OX+x, OY+y) on 1920x1080
    // LCD: 等比缩 + 居中黑边
    float s = std::min((float)w / 1920, (float)h / 1080);
    int ox = (w - 1920 * s) / 2;
    int oy = (h - 1080 * s) / 2;
    auto dp = [=](int x, int y) {
        return std::make_pair(ox + (int)((277+x) * s), oy + (int)((121+y) * s));
    };
    float img_scale = s;
    std::string A = "/root/app/dice_game/assets/ui";
    static std::vector<std::vector<uint8_t>> img_bufs;

    // 背景图 OpenCV 缩放
    auto load_img = [=](const std::string& path, bool fullscreen) -> const lv_img_dsc_t* {
        cv::Mat img = cv::imread(path, cv::IMREAD_UNCHANGED);
        if (img.empty()) return nullptr;
        int nw = fullscreen ? w : (int)(img.cols * img_scale);
        int nh = fullscreen ? h : (int)(img.rows * img_scale);
        if (nw<1)nw=1;if(nh<1)nh=1;
        cv::resize(img,img,cv::Size(nw,nh));
        if(img.channels()==3)cv::cvtColor(img,img,cv::COLOR_BGR2BGRA);
        int iw=img.cols,ih=img.rows;
        std::vector<uint8_t> raw(iw*ih*4);
        std::memcpy(raw.data(),img.data,raw.size());
        size_t hs=sizeof(lv_img_dsc_t);
        std::vector<uint8_t> b(hs+raw.size());
        lv_img_dsc_t dsc;
        dsc.header.magic_cf_flags=0x19|(0x10<<8);dsc.header.wh=(ih<<16)|iw;
        dsc.header.stride_reserved=iw*4;dsc.data_size=raw.size();dsc.data=nullptr;dsc.reserved=nullptr;
        std::memcpy(b.data(),&dsc,hs);
        ((lv_img_dsc_t*)b.data())->data=(const uint8_t*)(uintptr_t)(b.data()+hs);
        std::memcpy(b.data()+hs,raw.data(),raw.size());
        img_bufs.push_back(std::move(b));
        return (const lv_img_dsc_t*)img_bufs.back().data();
    };

    auto load_s  = [&](const std::string& p) { return load_img(p, false); };

    // UI 元素: 加载 + 定位
    auto make_img = [&](const lv_img_dsc_t* d, int x, int y, bool center) -> void* {
        if (!d) return nullptr;
        auto* img = p_lv_image_create(home);
        p_lv_image_set_src(img, d);
        p_lv_obj_set_style_bg_opa(img, 0, 0);  // 透明背景, 无填充
        if (center) p_lv_obj_center(img);
        else p_lv_obj_set_pos(img, x, y);
        return img;
    };

    // 背景: 等比例缩放居中
    if (auto* d = load_s(A + "/bg.png")) {
        auto* bg = p_lv_image_create(home); p_lv_image_set_src(bg, d);
        p_lv_obj_set_style_bg_opa(bg, 0, 0);
        p_lv_obj_center(bg);
    }
    auto [px1, py1] = dp(663, 63);
    if (auto* d = load_s(A+"/price1.png")) {
        auto* img = p_lv_image_create(home); p_lv_image_set_src(img, d);
        p_lv_obj_set_style_bg_opa(img, 0, 0);
        p_lv_obj_set_pos(img, px1, py1);
    }
    auto place = [&](const lv_img_dsc_t* d, int x, int y) {
        if (!d) return;
        auto* img = p_lv_image_create(home); p_lv_image_set_src(img, d);
        p_lv_obj_set_style_bg_opa(img, 0, 0); p_lv_obj_set_pos(img, x, y);
    };

    auto [px2, py2] = dp(77, 144);  place(load_s(A+"/Step.png"), px2, py2);

    struct { int y; const char* f; } items[] = {{317,"Prize_l"},{409,"Prize_m"},{469,"Prompt"},{533,"Prize_h"}};
    for (auto& it : items) {
        auto [px, py] = dp(215, it.y);
        place(load_s(A+"/"+std::string(it.f)+".png"), px, py);
    }

    auto [px3, py3] = dp(86, 52);  place(load_s(A+"/Game_Rules.png"), px3, py3);

    if (auto* d1 = load_s(A+"/star/star1.png"))
    if (auto* d2 = load_s(A+"/star/star2.png"))
    if (auto* d3 = load_s(A+"/star/star3.png"))
    {
        star_src2 = d2; star_src3 = d3;
        int sy_coords[] = {322, 414, 538};
        for (int i=0; i<3; i++) {
            auto [sx, sy] = dp(157, sy_coords[i]);
            // 底图 star1 不动
            auto* b = p_lv_image_create(home); p_lv_image_set_src(b, d1);
            p_lv_obj_set_style_bg_opa(b, 0, 0); p_lv_obj_set_pos(b, sx, sy);
            // glow 层 star2/star3 交替
            auto* g = p_lv_image_create(home); p_lv_image_set_src(g, d2);
            p_lv_obj_set_style_bg_opa(g, 0, 0); p_lv_obj_set_pos(g, sx, sy);
            star_glows[i] = g;
        }
    }
    p_lv_timer_create(twinkle_cb, 400, nullptr);
    star_anim = true;

    home_scr_ = home;

    // ── CAM 页 ──
    auto* cam = p_lv_obj_create(nullptr);
    p_lv_obj_set_style_bg_opa(cam, 0, 0);
    p_lv_obj_set_size(cam, w, h);
    // 结果图片 (底部居中)
    result_label_ = p_lv_image_create(cam);
    p_lv_obj_set_style_bg_opa(result_label_, 0, 0);
    p_lv_obj_align(result_label_, 9, 0, -20); // BOTTOM_MID
    cam_scr_ = cam;

    // 预加载石头剪刀布结果图
    result_imgs_[0] = (void*)load_s(A + "/rock.png");
    result_imgs_[1] = (void*)load_s(A + "/paper.png");
    result_imgs_[2] = (void*)load_s(A + "/scissors.png");

    p_lv_screen_load(home);
    ok = true;
    std::cout << "[LVGL] ready " << w << "x" << h << std::endl;
    return true;
}

void LvglDL::tick() {
    if (!ok) return;
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    int ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
    if (ms < 1) ms = 1;
    last = now;
    p_lv_tick_inc(ms);
    p_lv_timer_handler();
}

void LvglDL::switch_page(int p) {
    if (!ok) return;
    star_anim = (p == 0);  // HOME 开动画, 其他模式关
    p_lv_screen_load(p == 0 ? home_scr_ : cam_scr_);
}

void LvglDL::show_result(const char* name) {
    if (!ok || !result_label_) return;
    int idx = -1;
    if      (strstr(name,"rock")||strstr(name,"Rock")) idx = 0;
    else if (strstr(name,"paper")||strstr(name,"Paper")) idx = 1;
    else if (strstr(name,"scissors")||strstr(name,"Scissors")) idx = 2;
    if (idx >= 0 && result_imgs_[idx])
        p_lv_image_set_src(result_label_, result_imgs_[idx]);
}

uint8_t* LvglDL::osd() { return osd_buf_.data(); }
