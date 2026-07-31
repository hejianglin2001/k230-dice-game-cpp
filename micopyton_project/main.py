"""骰子游戏 — 入口 + 主循环"""
from libs.PipeLine import PipeLine
from project.config import *
from project.detector import CLASS_NAMES, DiceDetectionApp
from project.game import DiceGame
from project.dice_ui import DiceUI, lvgl_init
from media.display import *
from media.media import *
from machine import Pin, FPIOA
import time, sys, gc
import lvgl as lv

# 硬件按键：GPIO21, 引脚 L3, 按下低电平
fpioa = FPIOA()
fpioa.set_function(21, FPIOA.GPIO21)
KEY = Pin(21, Pin.IN, pull=Pin.PULL_UP)
_key_last = 1  # 上次状态（上拉=1=未按下）

print("[INIT] PipeLine...")
display_mode="hdmi"
# k230保持不变，k230d可调整为[640,360]
rgb888p_size = [1920, 1080]

if display_mode=="hdmi":
    display_size=[1920,1080]
else:
    display_size=[800,480]
pl = PipeLine(rgb888p_size=rgb888p_size, display_size=[DISPLAY_W, DISPLAY_H],
              display_mode=display_mode, osd_layer_num=3)
pl.create()

print("[INIT] YOLO...")
app = DiceDetectionApp("/sdcard/examples/kmodel/DR_yolo8n.kmodel",
                       model_input_size=[320,320],
                       confidence_threshold=0.7, nms_threshold=0.3,
                       rgb888p_size=rgb888p_size, display_size=[DISPLAY_W, DISPLAY_H])

app.config_preprocess()

game = DiceGame()

print("[INIT] LVGL...")
lvgl_init()

print("[INIT] UI...")
ui = DiceUI()
ui._black.draw_rectangle(0,0,DISPLAY_W,DISPLAY_H, (0,0,0), thickness=-1)
Display.show_image(ui._black, 0, 0, Display.LAYER_OSD1)
lv.scr_load(ui.page_home)
print("[INIT] Ready — home 5s")

STATE_HOME, STATE_CAMERA, STATE_DETECT, STATE_LOCKED = 0, 1, 2, 3
_mode = STATE_HOME
_det_buf = []
_stable_dets = None
_stable_vals = None
_detect_start = 0
_lock_time = 0
_frame_cnt = 0
_fps_time = time.time()
_fps_cnt = 0

def go_camera():
    global _mode
    pl.sensor.run()
    _mode = STATE_CAMERA
    ui.stop_animations()
    ui._black.clear()
    Display.show_image(ui._black, 0, 0, Display.LAYER_OSD1)
    lv.scr_load(ui.page_cam)
    print("[MODE] -> Camera (rolling)")

def go_detect():
    global _mode, _detect_start, _det_buf, _stable_dets, _stable_vals
    _mode = STATE_DETECT
    _detect_start = time.time()
    _det_buf.clear()
    _stable_dets = None
    _stable_vals = None
    print("[MODE] -> Detect (start)")

def go_locked():
    global _mode, _lock_time
    _mode = STATE_LOCKED
    _lock_time = time.time()
    if _stable_vals is not None:
        names = [CLASS_NAMES[v - 1] for v in _stable_vals]
        print("[RESULT] %s" % ", ".join(names))
    print("[MODE] -> Locked")

def go_home():
    global _mode, _stable_dets, _stable_vals
    pl.sensor.stop()
    _mode = STATE_HOME
    _stable_dets = None
    _stable_vals = None
    ui.resume_animations()
    ui._black.draw_rectangle(0,0,DISPLAY_W,DISPLAY_H, (0,0,0), thickness=-1)
    Display.show_image(ui._black, 0, 0, Display.LAYER_OSD1)
    if hasattr(ui, '_rps_widget'):
        ui._rps_widget.set_src(None)
    
    lv.scr_load(ui.page_home)
    print("[MODE] -> Home")

SETTLE_SEC = 3  # 检测几秒后自动锁定

try:
    while True:
        lv.task_handler()
        time.sleep_ms(5)
        _fps_cnt += 1
        if time.time() - _fps_time >= 1:
            print("[FPS] %d" % _fps_cnt)
            _fps_cnt = 0
            _fps_time = time.time()

        k = KEY.value()
        if k == 0 and _key_last == 1:
            if _mode == STATE_HOME:
                go_camera()
            elif _mode == STATE_CAMERA:
                go_detect()
            elif _mode == STATE_LOCKED:
                go_home()
        _key_last = k

        # ── 摄像头（只看不检测）──
        if _mode == STATE_CAMERA:
            img_np = pl.get_frame()
            pl.osd_img.clear()

        # ── 检测 ──
        elif _mode == STATE_DETECT:
            img_np = pl.get_frame()
            _frame_cnt += 1
            # 每帧推理
            dets = app.run(img_np)
            dice_vals = app.get_dice_values(dets)
            if dice_vals:
                cur = tuple(sorted(dice_vals))
                _det_buf.append(cur)
                if len(_det_buf) > 2: _det_buf.pop(0)
                if len(_det_buf) == 2 and all(v == cur for v in _det_buf):
                    _stable_dets = dets
                    _stable_vals = dice_vals
                    ui.update_status(dice_vals)
            else:
                _det_buf.clear()
            if _stable_dets is not None:
                app.draw_result(pl.osd_img, _stable_dets)
            # 自动锁定
            if time.time() - _detect_start >= SETTLE_SEC and _stable_vals is not None:
                go_locked()
            if _frame_cnt % 30 == 0:
                gc.collect()

        # ── 锁定 ──
        elif _mode == STATE_LOCKED:
            pl.osd_img.clear()

        # ── 首页 ──
        else:
            pl.osd_img.clear()
            pl.show_image()
            time.sleep_ms(16)
            continue

        pl.show_image()

except Exception as e:
    print("FATAL:", e)
    sys.print_exception(e)
finally:
    app.deinit()
    pl.destroy()
    lv.deinit()
    Display.deinit()
