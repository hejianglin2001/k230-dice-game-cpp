// 骰子猜拳游戏控制器 — 全部业务逻辑
#include "game/game.h"
#include "camera/camera.h"       // V4L2 摄像头 (SimpleCamera)
#include "display/display.h"     // DRM 双缓冲显示 (SimpleDisplay)
#include "detector/detector.h"   // YOLO KPU 检测器 (DiceDetector)
#include "ui/lvgl_dl.h"          // LVGL UI 动态加载 (LvglDL)
#include "key/key.h"             // GPIO 按键 (Key)
#include <iostream>
#include <opencv2/core.hpp>      // cv::Mat 图像容器
#include <opencv2/imgcodecs.hpp>  // cv::imread 读 PNG
#include <opencv2/imgproc.hpp>    // cv::resize, cv::rectangle, cv::putText

// ══════════════════════════════════════════════════════════════════
// 构造函数: 解析命令行参数, 存下来后面加载模型时用
// ══════════════════════════════════════════════════════════════════
GameController::GameController(int argc, char *argv[])
    : kmodel_path_(argv[1]),                                       // 模型文件路径 (必填)
      conf_thresh_((argc > 2) ? std::stof(argv[2]) : CONFIDENCE_THRESHOLD),  // 置信度阈值, 默认 0.5
      nms_thresh_ ((argc > 3) ? std::stof(argv[3]) : NMS_THRESHOLD),        // NMS 阈值, 默认 0.3
      debug_mode_ ((argc > 4) ? std::stoi(argv[4]) : 0) {}                  // 调试级别, 默认 0(安静)

GameController::~GameController() = default;

// ══════════════════════════════════════════════════════════════════
// 初始化: 打开显示 + 创建 LVGL UI + 初始化按键 + 预加载结果图
// 相机和模型不在这里加载 — HOME 待机状态不需要
// ══════════════════════════════════════════════════════════════════
bool GameController::init() {
    // ── 打开 DRM 显示器 ──
    disp_.reset(new SimpleDisplay());
    if (!disp_->open()) return false;  // 打不开 /dev/dri/card0 就退出

    // ── 创建 LVGL UI ──
    // DRM 输出是 480x800 竖屏, LVGL 内部按 800x480 横屏设计
    // 显示时旋转 90° 适配竖屏
    int ow = std::max(disp_->width, disp_->height);   // 800
    int oh = std::min(disp_->width, disp_->height);   // 480
    ui_.reset(new LvglDL());
    ui_->init(ow, oh);  // LVGL 内部用 800x480 横屏坐标系

    // ── 初始化按键 (GPIO21, 低电平触发) ──
    key_.reset(new Key(21));

    // ── 预加载石头/剪刀/布结果图 ──
    // 提前读 PNG 到内存, 检测出结果直接叠加, 不用每次都读文件
    const char *names[] = {"rock", "paper", "scissors"};
    for (int i = 0; i < 3; i++)
        rps_[i] = cv::imread("/root/app/dice_game/assets/ui/"
                             + std::string(names[i]) + ".png",
                             cv::IMREAD_UNCHANGED);  // 保留 alpha 通道

    // 时间戳初始化
    mode_start_ = close_cam_at_ = fps_time_ = std::chrono::steady_clock::now();
    std::cout << "[MAIN] HOME" << std::endl;
    return true;
}

// ══════════════════════════════════════════════════════════════════
// 按键处理 → 状态切换
//
// 状态机流程:
//   HOME ──按键──▶ CAMERA ──按键──▶ DETECT ──3秒稳定──▶ LOCKED ──按键──▶ HOME
//
// HOME:    待机, 只显示 LVGL UI, 无摄像头无模型
// CAMERA:  摄像头预览, 模型已加载但不推理
// DETECT:  每帧 AI 推理, 画检测框, 连续3帧同类别→锁定
// LOCKED:  显示最终结果(黄框+底部大图), 按键返回 HOME
// ══════════════════════════════════════════════════════════════════
void GameController::on_key() {
    // 跳过前 10 帧: GPIO 初始化时电平可能抖动, 避免误触发
    if (--skip_frames_ > 0) return;
    if (!key_->pressed()) return;  // 没按就不处理

    switch (mode_) {

    case ST_HOME:  // ── 待机 → 启动摄像头 ──
        // 1. 打开摄像头 (/dev/video2, 1280x720, BGR3 格式)
        cam_.reset(new SimpleCamera());
        cam_->open("/dev/video2", 1280, 720);
        // 2. 加载骰子识别模型 (KPU 约 2ms)
        det_.reset(new DiceDetector(kmodel_path_.c_str(), conf_thresh_,
                    nms_thresh_, {cam_->width, cam_->height},
                    {3, cam_->height, cam_->width}, 0, 0, debug_mode_));
        // 3. 切换状态 + LVGL 页面
        mode_ = ST_CAMERA;
        mode_start_ = std::chrono::steady_clock::now();
        ui_->switch_page(1);  // CAM 页 (透明, 叠加在摄像头上)
        break;

    case ST_CAMERA:  // ── 预览 → 开始识别 ──
        mode_ = ST_DETECT;
        mode_start_ = std::chrono::steady_clock::now();
        det_buf_.clear(); stable_dets_.clear(); stable_vals_.clear();
        break;

    case ST_LOCKED:  // ── 结果页 → 返回待机 ──
        mode_ = ST_HOME;
        mode_start_ = std::chrono::steady_clock::now();
        // 摄像头延迟 1.5 秒再关 — 先让 LVGL 刷到屏幕上,
        // ISP 复位不会出现白屏闪烁
        close_cam_at_ = mode_start_ + std::chrono::milliseconds(1500);
        det_.reset();                         // 卸载模型释放内存
        stable_dets_.clear();
        stable_vals_.clear();
        ui_->switch_page(0);                  // 切回 HOME 页
        // 连刷两帧 LVGL: 清空 DRM 双缓冲, 两个缓冲都是 LVGL 画面
        ui_->tick();
        disp_->show_rgba_landscape(ui_->osd(), ui_->width, ui_->height);
        ui_->tick();
        disp_->show_rgba_landscape(ui_->osd(), ui_->width, ui_->height);
        break;

    default: break;
    }
}

// ══════════════════════════════════════════════════════════════════
// HOME 模式: 纯 LVGL UI 显示
// - 不取摄像头帧 (不耗 CPU)
// - 每 30ms 刷新一次 LVGL
// - 检查是否需要延迟关闭摄像头
// ══════════════════════════════════════════════════════════════════
void GameController::do_home() {
    ui_->tick();   // 驱动 LVGL 内部时钟 + 渲染
    disp_->show_rgba_landscape(ui_->osd(), ui_->width, ui_->height);

    // 延迟关摄像头: 切回 HOME 1.5 秒后才关, 避免 ISP 复位闪屏
    auto now = std::chrono::steady_clock::now();
    if (cam_ && now >= close_cam_at_) {
        cam_.reset();
        std::cout << "[HOME] cam off" << std::endl;
    }
}

// ══════════════════════════════════════════════════════════════════
// DETECT 模式: AI 推理流水线
//
// 每帧流程:
//   摄像头 BGR3 → BGR planar (NCHW) → AI2D 预处理 → KPU 推理 → NMS
//
// 稳定性判定: 连续 3 帧检测到同一类别 → 锁定结果
// 定时锁定: 进入 DETECT 超过 3 秒 + 已有稳定结果 → 自动 LOCKED
// ══════════════════════════════════════════════════════════════════
void GameController::do_detect(void *frame) {
    if (!det_) return;

    // ── 1. BGR3 interleaved → BGR planar (NCHW) ──
    //     摄像头输出: [B,G,R,B,G,R,...] 交错排列
    //     AI2D 需要:  [B平面][G平面][R平面] 分离排列
    int ps = cam_->width * cam_->height;       // 1280*720 = 921600 像素
    planar_buf_.resize(ps * 3);                // 分配 3 个平面
    uint8_t *bp = planar_buf_.data(),          // B 平面
            *gp = bp + ps,                     // G 平面
            *rp = gp + ps,                     // R 平面
            *s  = (uint8_t*)frame;             // 源数据 (BGR 交错)
    for (int i = 0; i < ps; i++) {
        bp[i] = s[i*3];        // B
        gp[i] = s[i*3 + 1];    // G
        rp[i] = s[i*3 + 2];    // R
    }

    // ── 2. AI2D 预处理 (pad+resize) + KPU 推理 + 后处理(NMS) ──
    det_->pre_process_from_buffer(planar_buf_.data());  // ~3ms
    det_->inference();                                   // ~18ms (KPU)
    dets_.clear();
    det_->post_process(dets_);                           // ~0.01ms (NMS)

    // ── 3. 稳定性判定: 连续 3 帧同一类别 ──
    if (!dets_.empty()) {
        int cur = dets_[0].cls_id + 1;          // cls_id: 0=rock, 1=paper, 2=scissors
        det_buf_.push_back(cur);                // 记录最近 3 帧的检测结果
        if (det_buf_.size() > 3)                // 只保留最近 3 帧
            det_buf_.erase(det_buf_.begin());

        if (det_buf_.size() == 3) {             // 够 3 帧了, 检查是否都一致
            bool same = true;
            for (int v : det_buf_)
                if (v != cur) { same = false; break; }
            if (same) {                         // 3 帧一致 → 稳定检测!
                stable_dets_ = dets_;
                stable_vals_.clear();
                for (auto &d : stable_dets_)
                    stable_vals_.push_back(d.cls_id + 1);
                std::cout << "[DET] " << det_->labels_[cur-1] << std::endl;
            }
        }
    } else {
        det_buf_.clear();  // 这帧没检测到 → 清空缓冲重新累计
    }

    // ── 4. 自动锁定: 进入 DETECT 超过 SETTLE_SEC(3秒) 且有稳定结果 ──
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<float>(now - mode_start_).count() >= SETTLE_SEC
        && !stable_vals_.empty()) {
        mode_ = ST_LOCKED;
        mode_start_ = now;
        std::cout << "[RESULT] " << det_->labels_[stable_vals_[0]-1] << std::endl;
    }
}

// ══════════════════════════════════════════════════════════════════
// 画面叠加: 在摄像头帧上画检测框 + 底部结果图
//
// 注意: 直接修改 frame 指向的 V4L2 buffer,
//       后续 show_frame 会把修改后的画面送显
// ══════════════════════════════════════════════════════════════════
void GameController::draw_overlay(void *frame) {
    // 把 V4L2 buffer 包装成 OpenCV Mat 方便画图 (零拷贝, 不额外分配内存)
    cv::Mat img(cam_->height, cam_->width, CV_8UC3, frame);

    // ── 检测框 ──
    // DETECT 模式: 绿色框    LOCKED 模式: 黄色框 (显示稳定结果)
    if ((mode_ == ST_DETECT || mode_ == ST_LOCKED) && !dets_.empty()) {
        auto &draw = (mode_ == ST_LOCKED && !stable_dets_.empty())
                     ? stable_dets_ : dets_;         // LOCKED 画稳定框
        auto c = (mode_ == ST_LOCKED)
                 ? cv::Scalar(0, 255, 255)           // BGR 黄色
                 : cv::Scalar(0, 255, 0);            // BGR 绿色

        for (auto &d : draw) {
            // 中心坐标+宽高 → 左上右下角
            int x1 = std::max(0, (int)(d.cx - d.w/2));
            int y1 = std::max(0, (int)(d.cy - d.h/2));
            int x2 = std::min(cam_->width  - 1, (int)(d.cx + d.w/2));
            int y2 = std::min(cam_->height - 1, (int)(d.cy + d.h/2));
            cv::rectangle(img, cv::Rect(x1, y1, x2-x1, y2-y1), c, 3);

            // 框上方标签: "rock 0.85"
            char lb[32];
            snprintf(lb, 32, "%s %.2f",
                     det_->labels_[d.cls_id].c_str(), d.score);
            cv::putText(img, lb, cv::Point(x1, std::max(20, y1 - 5)),
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, c, 2);
        }
    }

    // ── 底部结果图 ──
    // 有稳定检测结果时, 在画面底部居中叠加 rock/paper/scissors PNG
    if ((mode_ == ST_DETECT || mode_ == ST_LOCKED) && !stable_vals_.empty()) {
        int idx = stable_vals_[0] - 1;          // 1→rock, 2→paper, 3→scissors
        if (idx >= 0 && idx < 3 && !rps_[idx].empty()) {
            cv::Mat &r = rps_[idx];
            // 缩放到画面 25% 宽度 (不要太大挡住画面)
            float scl = std::min(cam_->width  * 0.25f / r.cols,
                                 cam_->height * 0.18f / r.rows);
            int dw = r.cols * scl;
            int dh = r.rows * scl;
            // 底部居中
            int dx = (cam_->width - dw) / 2;
            int dy = cam_->height - dh - 10;    // 离底边 10px

            cv::Mat roi = img(cv::Rect(dx, dy, dw, dh));  // 目标区域
            cv::Mat rz;
            cv::resize(r, rz, cv::Size(dw, dh));           // 缩放 PNG到目标尺寸

            // Alpha 混合: PNG 透明部分露出摄像头背景
            if (rz.channels() == 4) {           // BGRA 4 通道
                for (int y = 0; y < dh; y++)
                    for (int x = 0; x < dw; x++) {
                        auto &p = rz.at<cv::Vec4b>(y, x);  // BGRA 像素
                        float a = p[3] / 255.f;             // alpha (0~1)
                        auto &d = roi.at<cv::Vec3b>(y, x);  // 目标 BGR 像素
                        // dst = src*(1-alpha) + png*alpha
                        d[0] = d[0] * (1-a) + p[0] * a;    // B
                        d[1] = d[1] * (1-a) + p[1] * a;    // G
                        d[2] = d[2] * (1-a) + p[2] * a;    // R
                    }
            }
        }
    }
}

// ══════════════════════════════════════════════════════════════════
// 主循环: 每帧执行
//
//   1. 检查按键 → 切换状态
//   2. HOME: 显示 LVGL, sleep 30ms
//   3. 非 HOME: 取摄像头帧 → 推理(可选) → 画叠加 → 送显
// ══════════════════════════════════════════════════════════════════
void GameController::run() {
    while (true) {
        // ── 1. 按键检测 + 状态切换 ──
        on_key();

        // ── 2. HOME 模式: 只显示 LVGL, 不碰摄像头 ──
        if (mode_ == ST_HOME) {
            do_home();
            usleep(30000);  // ~30fps, LVGL 不需要更快
            continue;       // 跳过后面的摄像头逻辑
        }

        // ── 3. 取摄像头帧 (阻塞 1 秒超时) ──
        if (!cam_) continue;
        void *frame = cam_->get_frame(1000);
        if (!frame) continue;  // 超时了就跳过这帧

        // ── 4. DETECT 模式: AI 推理 ──
        if (mode_ == ST_DETECT) do_detect(frame);

        // ── 5. 画检测框 + 结果图 ──
        draw_overlay(frame);

        // ── 6. 送显 (BGR3→DRM 旋转 90° + 缩放) ──
        disp_->show_frame((uint8_t*)frame, cam_->width, cam_->height);

        // ── 7. 释放摄像头缓冲 (还给 V4L2 驱动) ──
        cam_->release_frame();

        // ── 8. FPS 统计 (每秒打印一次) ──
        fps_cnt_++;
        auto t2 = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(t2 - fps_time_).count() >= 1.0f) {
            std::cout << "[FPS] " << fps_cnt_
                      << " mode=" << mode_ << std::endl;
            fps_cnt_ = 0;
            fps_time_ = t2;
        }
    }
}
