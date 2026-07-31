// 游戏全局常量配置
#pragma once

// ── 检测参数 ──
#define CONFIDENCE_THRESHOLD 0.7f   // 检测置信度阈值, 低于此值的结果丢弃
#define NMS_THRESHOLD 0.3f          // NMS IoU 阈值, 重叠超此值合并
#define SETTLE_SEC 3                // 进入 DETECT 后几秒自动锁定结果

// ── 状态机 ──
// HOME ──按键──▶ CAMERA ──按键──▶ DETECT ──3秒──▶ LOCKED ──按键──▶ HOME
enum State { ST_HOME, ST_CAMERA, ST_DETECT, ST_LOCKED };
