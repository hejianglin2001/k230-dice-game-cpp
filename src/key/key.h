// GPIO 按键 — 使用 Linux GPIO chardev uAPI (/dev/gpiochip0)
//
// K230 板: KEY 接 GPIO21, 按下低电平 (GND), 松开高电平 (上拉)
// 检测下降沿触发: cur=0 && last=1 → 按下瞬间返回 true

#pragma once

class Key {
public:
    Key(int gpio_pin);        // gpio_pin: 引脚号 (如 21)
    ~Key();
    bool pressed();           // 返回 true 表示检测到一次下降沿

private:
    int pin_;
    int last_;                // 上一次的电平值 (0 或 1)
    int chip_fd_ = -1;        // /dev/gpiochip0 的 fd
    int line_fd_ = -1;        // GPIO 线的匿名 fd (用于读取电平)
};
