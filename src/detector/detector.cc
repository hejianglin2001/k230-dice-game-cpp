#include "detector/detector.h"
#include <algorithm>
#include <cmath>

DiceDetector::DiceDetector(const char *kmodel_file, float obj_thresh, float nms_thresh,
                           FrameSize frame_size, FrameCHWSize isp_shape,
                           uintptr_t vaddr, uintptr_t paddr, int debug_mode)
    : obj_thresh_(obj_thresh), nms_thresh_(nms_thresh),
      frame_size_(frame_size), AIBase(kmodel_file, "DiceDet", debug_mode)
{
    model_name_ = "DiceDet";
    classes_num_ = NC;
    vaddr_ = vaddr;
    isp_shape_ = isp_shape;

    dims_t in_shape{1, isp_shape.channel, isp_shape.height, isp_shape.width};
    ai2d_in_tensor_ = hrt::create(typecode_t::dt_uint8, in_shape, hrt::pool_shared)
                          .expect("create ai2d input tensor failed");
    ai2d_out_tensor_ = get_input_tensor(0);
    Utils::padding_resize(isp_shape, {input_shapes_[0][3], input_shapes_[0][2]},
                          ai2d_builder_, ai2d_in_tensor_, ai2d_out_tensor_,
                          cv::Scalar(104, 117, 123));  // YOLO BGR mean

    // 计算 letterbox 参数 (用于 post_process 坐标映射)
    int dst_w = input_shapes_[0][3], dst_h = input_shapes_[0][2];
    float ori_w = (float)frame_size_.width, ori_h = (float)frame_size_.height;
    ratio_ = std::min(dst_w / ori_w, dst_h / ori_h);
    pad_left_ = (dst_w - ratio_ * ori_w) / 2.0f;
    pad_top_  = (dst_h - ratio_ * ori_h) / 2.0f;
}

DiceDetector::~DiceDetector() {}

void DiceDetector::pre_process() {
    pre_process_from_buffer((void *)vaddr_);
}

void DiceDetector::pre_process_from_buffer(void *buf) {
    ScopedTiming st(model_name_ + " pre_process", debug_mode_);
    size_t isp_size = isp_shape_.channel * isp_shape_.height * isp_shape_.width;
    auto ai2d_buf = ai2d_in_tensor_.impl()->to_host().unwrap()->buffer().as_host().unwrap()
                        .map(map_access_::map_write).unwrap().buffer();
    memcpy(reinterpret_cast<char *>(ai2d_buf.data()), buf, isp_size);
    hrt::sync(ai2d_in_tensor_, sync_op_t::sync_write_back, true).expect("sync failed");
    ai2d_builder_->invoke(ai2d_in_tensor_, ai2d_out_tensor_).expect("ai2d run failed");
}

void DiceDetector::inference() {
    this->run();
    this->get_output();
}

void DiceDetector::post_process(std::vector<Detection> &results) {
    float *output = p_outputs_[0];
    // nncase v2.11 output: {1, 7, 2100} — 3D, 用 .back() 取 anchor 数
    auto &oshape = output_shapes_[0];
    int n_anchors = oshape.back();

    // Letterbox 逆变换: 模型坐标 → 原始图像坐标
    // cx_ori = (cx_model - pad_left) / ratio
    // cy_ori = (cy_model - pad_top)  / ratio
    // w_ori  = w_model / ratio
    // h_ori  = h_model / ratio
    float inv_ratio = 1.0f / ratio_;

    for (int i = 0; i < n_anchors; i++) {
        float best_score = 0;
        int best_cls = 0;
        for (int c = 0; c < classes_num_; c++) {
            float sc = output[(4 + c) * n_anchors + i];
            if (sc > best_score) { best_score = sc; best_cls = c; }
        }
        if (best_score < obj_thresh_) continue;

        Detection d;
        d.cx = (output[0 * n_anchors + i] - pad_left_) * inv_ratio;
        d.cy = (output[1 * n_anchors + i] - pad_top_)  * inv_ratio;
        d.w  = output[2 * n_anchors + i] * inv_ratio;
        d.h  = output[3 * n_anchors + i] * inv_ratio;
        d.score = best_score;
        d.cls_id = best_cls;
        results.push_back(d);
    }
    if (!results.empty()) nms(results);
}

void DiceDetector::nms(std::vector<Detection> &input_boxes) {
    std::sort(input_boxes.begin(), input_boxes.end(),
              [](Detection a, Detection b) { return a.score > b.score; });
    std::vector<float> vArea(input_boxes.size());
    for (size_t i = 0; i < input_boxes.size(); ++i) {
        vArea[i] = input_boxes[i].w * input_boxes[i].h;
    }
    for (size_t i = 0; i < input_boxes.size(); ++i) {
        for (size_t j = i + 1; j < input_boxes.size();) {
            if (input_boxes[i].cls_id != input_boxes[j].cls_id) { j++; continue; }
            float xx1 = std::max(input_boxes[i].cx - input_boxes[i].w/2,
                                 input_boxes[j].cx - input_boxes[j].w/2);
            float yy1 = std::max(input_boxes[i].cy - input_boxes[i].h/2,
                                 input_boxes[j].cy - input_boxes[j].h/2);
            float xx2 = std::min(input_boxes[i].cx + input_boxes[i].w/2,
                                 input_boxes[j].cx + input_boxes[j].w/2);
            float yy2 = std::min(input_boxes[i].cy + input_boxes[i].h/2,
                                 input_boxes[j].cy + input_boxes[j].h/2);
            float iw = std::max(0.f, xx2 - xx1);
            float ih = std::max(0.f, yy2 - yy1);
            float inter = iw * ih;
            float ovr = inter / (vArea[i] + vArea[j] - inter + 1e-16f);
            if (ovr >= nms_thresh_) {
                input_boxes.erase(input_boxes.begin() + j);
                vArea.erase(vArea.begin() + j);
            } else {
                j++;
            }
        }
    }
}
