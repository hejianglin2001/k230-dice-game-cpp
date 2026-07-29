// V4L2 摄像头采集 — 直接操作 /dev/video 设备节点
//
// 用法:
//   SimpleCamera cam;
//   cam.open("/dev/video2", 1280, 720);   // 打开 AI 通道, BGR3 1280x720
//   void *frame = cam.get_frame(1000);     // 阻塞取一帧, 1秒超时
//   cam.release_frame();                   // 还给驱动
//   cam.close();                           // 关闭 (析构自动调)
//
// BGR3 格式: 每像素 3 字节 [B][G][R], 无 stride 填充
// 1280x720 = 2.76MB/帧, mmap 零拷贝

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <linux/videodev2.h>   // V4L2 内核接口定义

class SimpleCamera {
public:
    // 打开设备, 请求指定格式和分辨率
    bool open(const char *dev, int w, int h);

    // 阻塞获取一帧 (mmap 零拷贝, 返回内核缓冲指针)
    void* get_frame(int timeout_ms = 1000);

    // 释放当前帧 (还给驱动队列, 否则 buffer 耗尽会丢帧)
    void release_frame();

    // 关闭设备 (停止流 + munmap + close fd)
    void close();
    ~SimpleCamera();

    int width = 0, height = 0;   // 实际分辨率 (驱动可能改)

private:
    int fd = -1;                 // /dev/video 文件描述符
    void *bufs[4] = {};          // mmap 映射的缓冲区指针
    size_t buf_sizes[4] = {};    // 每个缓冲的大小
    int num_bufs = 0;            // 实际分配的缓冲数
    int last_buf = 0;            // 上一次出队的缓冲索引
};
