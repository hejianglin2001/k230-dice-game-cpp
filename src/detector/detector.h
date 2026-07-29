// YOLO 骰子检测器 — nncase KPU 推理
//
// 检测流程:
//   1. pre_process:  摄像头 BGR planar → AI2D (pad+resize→320x320)
//   2. inference:    KPU 推理 (约 18ms)
//   3. post_process: 解析输出 → NMS 去重 → 返回 Detection 列表
//
// 模型输出: (1, 7, 2100) = 1batch × 7通道(4bbox+3class) × 2100锚点
// 类别: 0=rock, 1=paper, 2=scissors

#pragma once
#include <vector>
#include <string>
#include "nncase/ai_base.h"       // AIBase: kmodel 加载 + 推理接口
#include "nncase/utils.h"          // FrameSize, Utils::padding_resize

// 检测结果
struct Detection {
    float cx, cy, w, h;    // 边界框中心坐标 + 宽高 (图像坐标系)
    float score;            // 置信度
    int cls_id;             // 类别: 0=rock, 1=paper, 2=scissors
};

class DiceDetector : public AIBase {
public:
    // kmodel_file: 模型路径
    // obj_thresh:  置信度阈值 (低于此值的结果丢弃)
    // nms_thresh:  NMS IoU 阈值
    // frame_size:  输入图像尺寸 (1280, 720)
    // isp_shape:   输入张量形状 (3, 720, 1280)
    DiceDetector(const char *kmodel_file, float obj_thresh, float nms_thresh,
                 FrameSize frame_size, FrameCHWSize isp_shape,
                 uintptr_t vaddr, uintptr_t paddr, int debug_mode);
    ~DiceDetector();

    void pre_process();                        // 使用内部 vaddr 预处理
    void pre_process_from_buffer(void *buf);   // 使用外部 buffer 预处理
    void inference();                           // KPU 推理
    void post_process(std::vector<Detection> &results);  // 解析输出 + NMS

    std::vector<std::string> labels_ = {"rock", "paper", "scissors"};
    static const int NC = 3;                   // 类别数
    static const int N_ANCHORS = 2100;         // YOLO 检测头锚点数 (320x320)

private:
    std::unique_ptr<ai2d_builder> ai2d_builder_;  // AI2D 硬件加速预处理
    runtime_tensor ai2d_in_tensor_;               // AI2D 输入张量
    runtime_tensor ai2d_out_tensor_;              // AI2D 输出张量 (模型输入)
    uintptr_t vaddr_;                             // 旧版 MPP 内存地址 (不再使用)
    FrameCHWSize isp_shape_;                      // 输入图像 CHW

    float obj_thresh_, nms_thresh_;
    FrameSize frame_size_;
    int classes_num_;

    float pad_top_ = 0, pad_left_ = 0;   // Letterbox 上方/左侧填充
    float ratio_ = 1.0;                  // Letterbox 缩放比

    void nms(std::vector<Detection> &input_boxes);  // NMS 去重
};
