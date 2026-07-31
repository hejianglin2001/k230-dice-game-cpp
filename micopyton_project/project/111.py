from libs.PipeLine import PipeLine, ScopedTiming
from libs.AIBase import AIBase
from libs.AI2D import Ai2d
import os, gc, time
import ujson
from media.media import *
import nncase_runtime as nn
import ulab.numpy as np
import image
import aidemo

# 骰子类别名
DICE_NAMES = ["1", "2", "3", "4", "5", "6"]


class DiceDetectionApp(AIBase):
    def __init__(self, kmodel_path, model_input_size, confidence_threshold=0.5,
                 nms_threshold=0.45, rgb888p_size=[1920,1080], display_size=[800,480], debug_mode=0):
        super().__init__(kmodel_path, model_input_size, rgb888p_size, debug_mode)
        self.kmodel_path = kmodel_path
        self.model_input_size = model_input_size          # [320, 320]
        self.confidence_threshold = confidence_threshold
        self.nms_threshold = nms_threshold
        self.rgb888p_size = [ALIGN_UP(rgb888p_size[0], 16), rgb888p_size[1]]
        self.display_size = [ALIGN_UP(display_size[0], 16), display_size[1]]
        self.debug_mode = debug_mode

        self.ai2d = Ai2d(debug_mode)
        self.ai2d.set_ai2d_dtype(nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT, np.uint8, np.uint8)

        # 预计算 anchor 网格点 (320x320, strides [8,16,32])
        self._anchors = self._build_anchors()

    # ---- 预计算 anchor 点 (只跑一次) ----
    def _build_anchors(self):
        imgsz = self.model_input_size[0]
        strides = [8, 16, 32]
        anchors = []   # (N, 2) 网格坐标
        strd     = []   # (N,)  对应 stride
        for s in strides:
            h = w = imgsz // s
            for y in range(h):
                for x in range(w):
                    anchors.append([x + 0.5, y + 0.5])
                    strd.append(s)
        return np.array(anchors), np.array(strd).reshape((-1, 1))

    # ---- 预处理 ----
    def config_preprocess(self, input_image_size=None):
        with ScopedTiming("set preprocess config", self.debug_mode > 0):
            ai2d_input_size = input_image_size if input_image_size else self.rgb888p_size
            top, bottom, left, right = self.get_padding_param()
            self.ai2d.pad([0, 0, 0, 0, top, bottom, left, right], 0, [104, 117, 123])
            self.ai2d.resize(nn.interp_method.tf_bilinear, nn.interp_mode.half_pixel)
            self.ai2d.build([1, 3, ai2d_input_size[1], ai2d_input_size[0]],
                            [1, 3, self.model_input_size[1], self.model_input_size[0]])

    # ---- ══════════════ 后处理 ══════════════ ----
    def postprocess(self, results):
        with ScopedTiming("postprocess", self.debug_mode > 0):
            output    = results[0][0]                       # (10, 2100)
            nc        = 6
            n_anchors = 2100

            # ---- DEBUG: 打印前几列的原始值 ----
            if self.debug_mode:
                print("DEBUG cls[0:5]:", [float(output[4+c][0]) for c in range(nc)])
                print("DEBUG cls max:", float(max(output[4+c][0] for c in range(nc))))
                print("DEBUG bbox[0]:", [float(output[j][0]) for j in range(4)])
                # 打印所有 anchor 的最大置信度
                all_max = []
                for i in range(min(100, n_anchors)):
                    ms = max(float(output[4+c][i]) for c in range(nc))
                    all_max.append(ms)
                print("DEBUG top5 scores:", sorted(all_max, reverse=True)[:5])

            # 缩放因子: 模型空间 → 原始图像空间
            scale = max(self.rgb888p_size) / max(self.model_input_size)

            # ---- 逐 anchor 处理 ----
            candidates = []
            for i in range(n_anchors):
                # bbox: [cx, cy, w, h] 在模型输入空间
                cx_m = float(output[0][i])
                cy_m = float(output[1][i])
                w_m  = float(output[2][i])
                h_m  = float(output[3][i])

                # 找最大置信度 (原始值, 不用 sigmoid)
                best_score = 0.0
                best_cls   = 0
                for c in range(nc):
                    sc = float(output[4 + c][i])
                    if sc > best_score:
                        best_score = sc
                        best_cls   = c

                if best_score < self.confidence_threshold:
                    continue

                # 缩放到原始图像空间
                cx = cx_m * scale
                cy = cy_m * scale
                w  = w_m * scale
                h  = h_m * scale

                candidates.append([cx, cy, w, h, best_score, best_cls])

            print("candidates:", len(candidates))

            if not candidates:
                return []

            # ---- NMS (纯 Python) ----
            dets = self._nms_py(candidates)
            print("after nms:", len(dets))
            if dets:
                d = dets[0]
                print("DET[0]: cx=%.1f cy=%.1f w=%.1f h=%.1f score=%.3f cls=%d" %
                      (d[0], d[1], d[2], d[3], d[4], d[5]))
                print("DET[0] draw: x1=%d y1=%d x2=%d y2=%d" %
                      (int(d[0]-d[2]/2), int(d[1]-d[3]/2),
                       int(d[0]+d[2]/2), int(d[1]+d[3]/2)))
            return dets

    # ---- NMS (纯 Python) ----
    def _nms_py(self, dets):
        keep = []
        for c in range(6):
            cls_dets = [d for d in dets if d[5] == c]
            cls_dets.sort(key=lambda x: x[4], reverse=True)

            while cls_dets:
                best = cls_dets.pop(0)
                keep.append(best)
                bx, by, bw, bh = best[0], best[1], best[2], best[3]
                bx1 = bx - bw / 2.0
                by1 = by - bh / 2.0
                bx2 = bx + bw / 2.0
                by2 = by + bh / 2.0
                area_b = bw * bh

                remaining = []
                for d in cls_dets:
                    rx, ry, rw, rh = d[0], d[1], d[2], d[3]
                    rx1 = rx - rw / 2.0
                    ry1 = ry - rh / 2.0
                    rx2 = rx + rw / 2.0
                    ry2 = ry + rh / 2.0

                    ix1 = bx1 if bx1 > rx1 else rx1
                    iy1 = by1 if by1 > ry1 else ry1
                    ix2 = bx2 if bx2 < rx2 else rx2
                    iy2 = by2 if by2 < ry2 else ry2
                    iw = ix2 - ix1
                    ih = iy2 - iy1
                    if iw <= 0 or ih <= 0:
                        remaining.append(d)
                        continue

                    inter = iw * ih
                    area_r = rw * rh
                    iou = inter / (area_b + area_r - inter + 1e-16)
                    if iou <= self.nms_threshold:
                        remaining.append(d)

                cls_dets = remaining
        return keep

    # ---- 绘制 ----
    def draw_result(self, pl, dets):
        with ScopedTiming("display_draw", self.debug_mode > 0):
            pl.osd_img.clear()
            if dets:
                for det in dets:
                    cx, cy, w, h = det[0], det[1], det[2], det[3]
                    score   = det[4]
                    cls_id  = det[5]
                    label   = DICE_NAMES[cls_id]

                    x1 = int(cx - w / 2)
                    y1 = int(cy - h / 2)
                    x2 = int(cx + w / 2)
                    y2 = int(cy + h / 2)

                    # 缩放到显示尺寸
                    sx = self.display_size[0] / self.rgb888p_size[0]
                    sy = self.display_size[1] / self.rgb888p_size[1]
                    x1d = int(x1 * sx)
                    y1d = int(y1 * sy)
                    x2d = int(x2 * sx)
                    y2d = int(y2 * sy)

                    pl.osd_img.draw_rectangle(x1d, y1d, x2d - x1d, y2d - y1d,
                                              (0, 255, 0), thickness=2)
                    pl.osd_img.draw_string_advanced(x1d, max(0, y1d - 20), 32,
                                                     f"{label} {score:.1f}",
                                                     (0, 255, 0))
            else:
                pl.osd_img.clear()

    def get_padding_param(self):
        dst_w = self.model_input_size[0]
        dst_h = self.model_input_size[1]
        ratio_w = dst_w / self.rgb888p_size[0]
        ratio_h = dst_h / self.rgb888p_size[1]
        ratio = min(ratio_w, ratio_h)
        new_w = int(ratio * self.rgb888p_size[0])
        new_h = int(ratio * self.rgb888p_size[1])
        dw = (dst_w - new_w) / 2
        dh = (dst_h - new_h) / 2
        top    = int(round(0))
        bottom = int(round(dh * 2 + 0.1))
        left   = int(round(0))
        right  = int(round(dw * 2 - 0.1))
        return top, bottom, left, right


if __name__ == "__main__":
    display_mode = "hdmi"
    rgb888p_size = [1920, 1080]
    display_size = [800, 480] if display_mode != "hdmi" else [1920, 1080]

    kmodel_path    = "/sdcard/examples/kmodel/DR_yolo12n.kmodel"
    confidence_thr = 0.7
    nms_thr        = 0.3

    pl = PipeLine(rgb888p_size=rgb888p_size, display_size=display_size, display_mode=display_mode)
    pl.create()

    app = DiceDetectionApp(
        kmodel_path,
        model_input_size=[320, 320],
        confidence_threshold=confidence_thr,
        nms_threshold=nms_thr,
        rgb888p_size=rgb888p_size,
        display_size=display_size,
        debug_mode=1,
    )
    app.config_preprocess()

    try:
        while True:
            os.exitpoint()
            with ScopedTiming("total", 1):
                img = pl.get_frame()
                res = app.run(img)
                app.draw_result(pl, res)
                pl.show_image()
                gc.collect()
    except Exception as e:
        print(e)
    finally:
        app.deinit()
        pl.destroy()
