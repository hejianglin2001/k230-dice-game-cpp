// LVGL UI — 运行时动态加载 liblvgl.so (dlopen/dlsym)
//
// 不用编译时链接 liblvgl.so, 避免 GCC 版本冲突 (板端 GCC14, 我们 GCC10)
// 所有 lv_* 函数通过 dlsym 获取指针, 运行时调用
//
// 页面:
//   HOME: 深色背景 + PNG 背景图 + 价格牌 + 星星闪烁动画
//   CAM:  全透明叠加层 + 底部结果图 (rock/paper/scissors)

#pragma once
#include <cstdint>
#include <vector>
#include <functional>

class LvglDL {
public:
    bool init(int w, int h);              // 初始化 LVGL, w×h 横屏坐标系
    void tick();                           // 每帧调用: 驱动 LVGL 时钟 + 渲染
    void switch_page(int p);              // 0=HOME, 1=CAM/DETECT/LOCKED
    void show_result(const char* name);    // 显示检测结果图 (rock/paper/scissors)
    uint8_t* osd();                       // 返回 LVGL 渲染的 OSD buffer (ARGB8888)

    int width = 0, height = 0;
    bool ok = false;                      // 初始化是否成功

private:
    void* handle_ = nullptr;              // dlopen 返回的库句柄
    std::vector<uint8_t> buf1_, buf2_;    // LVGL 双缓冲
    std::vector<uint8_t> osd_buf_;        // flush 目标 buffer (ARGB8888)
    void* home_scr_ = nullptr;            // HOME 页面
    void* cam_scr_ = nullptr;             // CAMERA 页面
    void* result_label_ = nullptr;        // 结果图控件
    void* result_imgs_[3] = {};           // 预加载 rock, paper, scissors 图
};
