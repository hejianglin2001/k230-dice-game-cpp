"""骰子游戏 — 常量 & 坐标转换"""
def ALIGN_UP(x, a): return (x + a - 1) // a * a

DISPLAY_W = ALIGN_UP(1920, 16)
DISPLAY_H = 1080
UI_DIR = "/sdcard/project/ui"

# 设计稿 1366×938，HDMI 1920×1080，主图居中不缩放
DX, DY = 1366, 938
OX = (1920 - DX) // 2  # 277
OY = (1080 - DY) // 2 + 50  # 121

def dp(x, y):
    """设计稿坐标 → 屏幕坐标"""
    return OX + x, OY + y

# 游戏参数
DICE_COUNT   = 2
WIN_SUM      = 7
ROLL_TIME    = 2
SETTLE_TIME  = 1
JUDGE_FRAMES = 5

# 状态
(STATE_HOME, STATE_ROLLING, STATE_STOPPING,
 STATE_JUDGING, STATE_WIN, STATE_LOSE, STATE_GIFT) = range(7)
