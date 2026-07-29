// 骰子游戏控制器 — 封装全部业务逻辑
//
// 职责: 状态机 + 摄像头管理 + 模型加载/卸载 + 推理 + 画面叠加 + FPS统计
// main.cc 只做 init() + run(), 所有逻辑在这里
//
// 状态机:  HOME ──key──▶ CAMERA ──key──▶ DETECT ──3s──▶ LOCKED ──key──▶ HOME

#pragma once
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <opencv2/core.hpp>    // cv::Mat (结果图缓存)
#include "config.h"

class SimpleCamera;
class SimpleDisplay;
class DiceDetector;
class LvglDL;
class Key;
struct Detection;

class GameController {
public:
    GameController(int argc, char *argv[]);
    ~GameController();
    bool init();              // 初始化: 显示 + LVGL + 按键 + 预加载结果图
    void run();               // 主循环 (阻塞, 直到程序退出)

private:
    void on_key();            // 按键处理 → 状态切换
    void do_home();           // HOME: LVGL UI + 延迟关摄像头
    void do_detect(void *f);  // DETECT: AI 推理 + 稳定性判定
    void draw_overlay(void *f); // 画检测框 + 底部结果图

    // 硬件
    std::unique_ptr<SimpleCamera> cam_;       // V4L2 摄像头
    std::unique_ptr<DiceDetector> det_;       // YOLO KPU 检测器
    std::unique_ptr<SimpleDisplay> disp_;     // DRM 显示器
    std::unique_ptr<LvglDL> ui_;              // LVGL UI
    std::unique_ptr<Key> key_;                // GPIO 按键

    // 状态
    State mode_ = ST_HOME;
    std::vector<Detection> dets_, stable_dets_;
    std::vector<int> det_buf_, stable_vals_;
    std::vector<uint8_t> planar_buf_;         // BGR3→planar 转换缓冲

    // 时间戳
    std::chrono::steady_clock::time_point mode_start_;    // 当前状态开始时间
    std::chrono::steady_clock::time_point close_cam_at_;  // 延迟关摄像头时间
    std::chrono::steady_clock::time_point fps_time_;      // FPS 统计起点
    int fps_cnt_ = 0, skip_frames_ = 10;     // 跳过前 N 帧防 GPIO 误触发

    // 参数 (从命令行解析)
    std::string kmodel_path_;
    float conf_thresh_, nms_thresh_;
    int debug_mode_;

    cv::Mat rps_[3];  // 预加载 rock.png, paper.png, scissors.png
};
