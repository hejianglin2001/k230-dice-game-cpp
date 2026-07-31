"""骰子游戏 — LVGL UI：显示驱动 + PNG 加载 + 所有页面"""

from media.display import *
from media.media import *
import lvgl as lv
import image
from project.config import *

# LVGL 双缓冲全局引用（flush callback 需要）
disp_img1 = None
disp_img2 = None


# ════════════════════════════════════════════
# 显示驱动
# ════════════════════════════════════════════

def disp_drv_flush_cb(disp_drv, area, color):
    """LVGL flush 回调：将渲染好的缓冲区推送到 OSD2 层显示"""
    global disp_img1, disp_img2
    buf = color.__dereference__()
    addr = uctypes.addressof(buf)
    if addr == disp_img1.virtaddr():
        Display.show_image(disp_img1, layer=Display.LAYER_OSD2)
    else:
        Display.show_image(disp_img2, layer=Display.LAYER_OSD2)
    disp_drv.flush_ready()


def lvgl_init():
    """初始化 LVGL：创建 display、双缓冲、flush 回调，返回 display driver"""
    global disp_img1, disp_img2
    lv.init()
    d = lv.disp_create(DISPLAY_W, DISPLAY_H)
    d.set_flush_cb(disp_drv_flush_cb)
    d.set_color_format(lv.COLOR_FORMAT.ARGB8888)
    disp_img1 = image.Image(DISPLAY_W, DISPLAY_H, image.BGRA8888)
    disp_img2 = image.Image(DISPLAY_W, DISPLAY_H, image.BGRA8888)
    disp_img1.clear(); disp_img2.clear()
    d.set_draw_buffers(disp_img1.bytearray(), disp_img2.bytearray(),
                        disp_img1.size(), lv.DISP_RENDER_MODE.FULL)
    return d


# ════════════════════════════════════════════
# PNG 加载
# ════════════════════════════════════════════

def load_png(path):
    """读取 PNG 文件，返回 LVGL 图片描述符（LVGL 内置解码）"""
    with open(path, 'rb') as f:
        data = f.read()
    return lv.img_dsc_t({'data_size': len(data), 'data': data})


# ════════════════════════════════════════════
# UI 页面
# ════════════════════════════════════════════

class DiceUI:
    """骰子游戏主 UI：首页 + 摄像头页"""

    def __init__(self):
        """初始化所有页面 + 摄像头遮罩"""
        self._build()
        # 黑屏遮罩（盖住摄像头，首页用）
        self._black = image.Image(DISPLAY_W, DISPLAY_H, image.ARGB8888)
        self._black.clear()
        self._black.draw_rectangle(0, 0, DISPLAY_W, DISPLAY_H, (0, 0, 0), thickness=-1)

    def _build(self):
        """构建所有 LVGL 页面对象"""
        self.page_home = self._home()
        self.page_cam  = self._cam()

    # ── 首页 ──────────────────────────────

    def _home(self):
        """首页：背景图 + 价格牌 + 规则按钮 + 3 颗闪烁星星"""
        scr = lv.obj()
        scr.set_style_bg_color(lv.color_hex(0x000000), lv.PART.MAIN)
        scr.set_style_bg_opa(255, lv.PART.MAIN)

        # 背景图
        dsc = load_png(UI_DIR + "/bg.png")
        if dsc:
            img = lv.img(scr); img.set_src(dsc); img.center()

        # 价格牌
        dsc_price = load_png(UI_DIR + "/price1.png")
        if dsc_price:
            w = lv.img(scr); w.set_src(dsc_price); w.set_pos(*dp(663, 63))
            self._price_widget = w

        # Step 说明图
        d_step = load_png(UI_DIR + "/Step.png")
        if d_step:
            w = lv.img(scr); w.set_src(d_step); w.set_pos(*dp(77, 144))

        # 奖品标签 + 提示
        for name, y in [("Prize_l", 317), ("Prize_m", 409), ("Prompt", 469), ("Prize_h", 533)]:
            d = load_png(UI_DIR + "/%s.png" % name)
            if d:
                w = lv.img(scr); w.set_src(d); w.set_pos(*dp(215, y))

        # 规则按钮
        dsc_rules = load_png(UI_DIR + "/Game_Rules.png")
        if dsc_rules:
            w = lv.img(scr); w.set_src(dsc_rules); w.set_pos(*dp(86, 52))
            self._rules_widget = w

        # 3 颗星星：star1 底图不动，star2↔star3 交替闪烁
        d1 = load_png(UI_DIR + "/star/star1.png")
        d2 = load_png(UI_DIR + "/star/star2.png")
        d3 = load_png(UI_DIR + "/star/star3.png")
        self._star_imgs = [d2, d3]
        self._star_toggle = 0
        self._star_glows = []
        self._star_bases = []   # 必须存引用防止 GC

        for sx, sy in [dp(157, 322), dp(157, 414), dp(157, 538)]:
            b = lv.img(scr); b.set_src(d1); b.set_pos(sx, sy)   # base
            g = lv.img(scr); g.set_src(d2); g.set_pos(sx, sy)   # glow
            self._star_bases.append(b)
            self._star_glows.append(g)

        lv.timer_create(lambda t: self._twinkle(), 400, None)
        return scr

    _anims_stopped = False

    def stop_animations(self):
        """停止星星闪烁"""
        DiceUI._anims_stopped = True

    def resume_animations(self):
        """恢复星星闪烁"""
        DiceUI._anims_stopped = False

    def _twinkle(self):
        if self._anims_stopped:
            return
        self._star_toggle ^= 1
        img = self._star_imgs[self._star_toggle]
        for g in self._star_glows:
            g.set_src(img)

    # ── 摄像头页（YOLO 检测时使用）───────

    def _cam(self):
        scr = lv.obj()
        scr.set_style_bg_opa(lv.OPA.TRANSP, lv.PART.MAIN)

        # 结果图片（底部居中）
        self._rps_imgs = {
            1: load_png(UI_DIR + "/rock.png"),
            2: load_png(UI_DIR + "/paper.png"),
            3: load_png(UI_DIR + "/scissors.png"),
        }
        self._rps_widget = lv.img(scr)
        self._rps_widget.align(lv.ALIGN.BOTTOM_MID, 0, -20)
        return scr

    def update_status(self, vals):
        if vals and vals[0] in self._rps_imgs:
            self._rps_widget.set_src(self._rps_imgs[vals[0]])
