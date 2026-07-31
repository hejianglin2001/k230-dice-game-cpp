"""骰子 YOLO 检测器"""
from libs.AIBase import AIBase
from libs.AI2D import Ai2d
from libs.Utils import *
from project.config import *
import nncase_runtime as nn
import ulab.numpy as np

CLASS_NAMES = ["rock", "paper", "scissors"]
NC = 3


class DiceDetectionApp(AIBase):
    def __init__(self, kmodel_path, model_input_size, confidence_threshold=0.5,
                 nms_threshold=0.45, rgb888p_size=[1920,1080], display_size=[1920,1080], debug_mode=0):
        super().__init__(kmodel_path, model_input_size, rgb888p_size, debug_mode)
        self.model_input_size = model_input_size
        self.confidence_threshold = confidence_threshold
        self.nms_threshold = nms_threshold
        self.rgb888p_size = [ALIGN_UP(rgb888p_size[0], 16), rgb888p_size[1]]
        self.display_size = [ALIGN_UP(display_size[0], 16), display_size[1]]
        self.debug_mode = debug_mode
        self.ai2d = Ai2d(debug_mode)
        self.ai2d.set_ai2d_dtype(nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT, np.uint8, np.uint8)

    def config_preprocess(self, input_image_size=None):
        ai2d_input_size = input_image_size if input_image_size else self.rgb888p_size
        top, bottom, left, right = self._padding_param()
        self.ai2d.pad([0,0,0,0, top,bottom,left,right], 0, [104,117,123])
        self.ai2d.resize(nn.interp_method.tf_bilinear, nn.interp_mode.half_pixel)
        self.ai2d.build([1,3,ai2d_input_size[1],ai2d_input_size[0]],
                        [1,3,self.model_input_size[1],self.model_input_size[0]])

    def postprocess(self, results):
        output = results[0][0]
        scale = max(self.rgb888p_size) / max(self.model_input_size)
        candidates = []
        for i in range(2100):
            best_score, best_cls = 0.0, 0
            for c in range(NC):
                sc = float(output[4+c][i])
                if sc > best_score: best_score, best_cls = sc, c
            if best_score < self.confidence_threshold: continue
            cx_m, cy_m = float(output[0][i]), float(output[1][i])
            w_m, h_m = float(output[2][i]), float(output[3][i])
            candidates.append([cx_m*scale, cy_m*scale, w_m*scale, h_m*scale, best_score, best_cls])
        return self._nms(candidates) if candidates else []

    def _nms(self, dets):
        keep = []
        for c in range(NC):
            cls_dets = sorted([d for d in dets if d[5]==c], key=lambda x: x[4], reverse=True)
            while cls_dets:
                best = cls_dets.pop(0); keep.append(best)
                bx1,bx2 = best[0]-best[2]/2, best[0]+best[2]/2
                by1,by2 = best[1]-best[3]/2, best[1]+best[3]/2
                area_b = best[2]*best[3]
                remaining = []
                for d in cls_dets:
                    rx1,rx2 = d[0]-d[2]/2, d[0]+d[2]/2
                    ry1,ry2 = d[1]-d[3]/2, d[1]+d[3]/2
                    ix1,ix2 = max(bx1,rx1), min(bx2,rx2)
                    iy1,iy2 = max(by1,ry1), min(by2,ry2)
                    iw,ih = ix2-ix1, iy2-iy1
                    if iw<=0 or ih<=0: remaining.append(d); continue
                    if (iw*ih)/(area_b+d[2]*d[3]-iw*ih+1e-16) <= self.nms_threshold:
                        remaining.append(d)
                cls_dets = remaining
        return keep

    def get_dice_values(self, dets):
        """返回所有检测到的类别"""
        if not dets: return []
        dets = sorted(dets, key=lambda x: x[4], reverse=True)
        return [int(d[5]) + 1 for d in dets]

    def draw_result(self, img, dets):
        img.clear()
        if not dets: return
        for det in dets:
            cx,cy,w,h = det[0],det[1],det[2],det[3]
            score, cid = det[4], int(det[5])
            x1,y1 = int(cx-w/2), int(cy-h/2)
            x2,y2 = int(cx+w/2), int(cy+h/2)
            sx = self.display_size[0]/self.rgb888p_size[0]
            sy = self.display_size[1]/self.rgb888p_size[1]
            img.draw_rectangle(int(x1*sx), int(y1*sy), int((x2-x1)*sx), int((y2-y1)*sy),
                               (0,255,0), thickness=2)
            img.draw_string_advanced(int(x1*sx), max(0,int(y1*sy)-20), 32,
                "{0} {1:.1f}".format(CLASS_NAMES[cid], score), (0,255,0))

    def _padding_param(self):
        dst_w, dst_h = self.model_input_size
        ratio = min(dst_w/self.rgb888p_size[0], dst_h/self.rgb888p_size[1])
        dw = (dst_w - int(ratio*self.rgb888p_size[0]))/2
        dh = (dst_h - int(ratio*self.rgb888p_size[1]))/2
        return 0, int(round(dh*2+0.1)), 0, int(round(dw*2-0.1))
