// GPIO 按键 — Linux chardev uAPI (替代旧 sysfs 接口)
//
// 旧 sysfs (/sys/class/gpio/*) 在 Linux 5.x+ 已废弃
// 新版使用字符设备 /dev/gpiochip0 + ioctl
//
// 流程: open chip → request line → ioctl 读值 → 检测下降沿

#include "key/key.h"
#include <iostream>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>       // GPIO chardev uAPI 定义

Key::Key(int gpio_pin) : pin_(gpio_pin), last_(1), chip_fd_(-1), line_fd_(-1) {
    // ── 1. 打开 GPIO 芯片 ──
    // K230 有 2 个 gpiochip: gpiochip0 (GPIO0-31), gpiochip1 (GPIO32-63)
    // GPIO21 在 gpiochip0
    chip_fd_ = ::open("/dev/gpiochip0", O_RDONLY);
    if (chip_fd_ < 0) {
        std::cerr << "[KEY] gpiochip0 open failed" << std::endl;
        return;
    }

    // ── 2. 请求 GPIO 线作为输入 ──
    // 返回匿名 fd (line_fd_), 后续用它读取电平
    gpiohandle_request req;
    std::memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = (uint32_t)pin_;            // 引脚号
    req.flags = GPIOHANDLE_REQUEST_INPUT;            // 输入模式
    req.lines = 1;                                   // 只请求一条线
    strncpy(req.consumer_label, "dice_key", sizeof(req.consumer_label) - 1);

    if (ioctl(chip_fd_, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        std::cerr << "[KEY] GPIO" << pin_ << " request failed" << std::endl;
        ::close(chip_fd_); chip_fd_ = -1;
        return;
    }
    line_fd_ = req.fd;  // 保存匿名 fd
    std::cout << "[KEY] GPIO" << pin_ << " ready" << std::endl;
}

bool Key::pressed() {
    if (line_fd_ < 0) return false;

    // ── 3. 读取当前电平 ──
    gpiohandle_data data;
    if (ioctl(line_fd_, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0)
        return false;

    // ── 4. 检测下降沿 ──
    // 未按下: 上拉电阻 → 1 (高电平)
    // 按下:   接地 → 0 (低电平)
    // 下降沿: last=1 → cur=0
    int cur = data.values[0] & 1;
    bool ret = (cur == 0 && last_ == 1);
    last_ = cur;
    return ret;
}

Key::~Key() {
    if (line_fd_ >= 0) ::close(line_fd_);
    if (chip_fd_ >= 0) ::close(chip_fd_);
}
