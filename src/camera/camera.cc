// V4L2 摄像头采集实现
// 参考: Linux V4L2 API 标准流程 (https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html)
//
// V4L2 采集流程:
//   1. open()         打开设备
//   2. VIDIOC_S_FMT   设置格式 (BGR3, 1280x720)
//   3. VIDIOC_REQBUFS 请求 mmap 缓冲 (4个)
//   4. mmap()         映射到用户空间
//   5. VIDIOC_QBUF    缓冲入队
//   6. VIDIOC_STREAMON 开始采集
//   7. VIDIOC_DQBUF   取一帧 (出队)
//   8. VIDIOC_QBUF    用完还回去 (入队)
//   9. VIDIOC_STREAMOFF 停止
//  10. munmap()       解除映射
//  11. close()        关闭设备

#include "camera/camera.h"
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

bool SimpleCamera::open(const char *dev, int w, int h) {
    // ── 1. 打开设备 ──
    // O_RDWR: 读写 (mmap 需要)
    // O_NONBLOCK: 非阻塞, select 控制超时
    fd = ::open(dev, O_RDWR | O_NONBLOCK);
    if (fd < 0) { std::cerr << "[CAM] open " << dev << " failed" << std::endl; return false; }

    // ── 2. 查询设备能力 ──
    v4l2_capability cap;
    std::memset(&cap, 0, sizeof(cap));
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) { close(); return false; }
    std::cout << "[CAM] " << cap.card << std::endl;  // 驱动名, 如 "vvcam-video.0.1"

    // ── 3. 设置像素格式 ──
    v4l2_format fmt;
    std::memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;     // 视频捕获
    fmt.fmt.pix.width = w;                      // 1280
    fmt.fmt.pix.height = h;                     // 720
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;  // BGR 交错 (每像素3字节)
    fmt.fmt.pix.field = V4L2_FIELD_NONE;        // 逐行扫描
    // 驱动可能修改 width/height/pixelformat, 后面读回实际值
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        // 回退: 如果 BGR3 不支持, 试试默认格式
        fmt.fmt.pix.width = 1280; fmt.fmt.pix.height = 720;
        if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) { close(); return false; }
    }
    width = fmt.fmt.pix.width;
    height = fmt.fmt.pix.height;
    std::cout << "[CAM] " << width << "x" << height
              << " BGR3 stride=" << fmt.fmt.pix.bytesperline << std::endl;

    // ── 4. 请求 mmap 缓冲 ──
    v4l2_requestbuffers req;
    std::memset(&req, 0, sizeof(req));
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;   // 内存映射方式
    req.count = 4;                   // 4个缓冲 (太少会丢帧, 太多延迟大)
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) { close(); return false; }
    num_bufs = req.count;            // 驱动可能分配少于请求

    // ── 5. mmap 每个缓冲到用户空间 ──
    for (int i = 0; i < num_bufs; i++) {
        v4l2_buffer buf;
        std::memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) { close(); return false; }
        buf_sizes[i] = buf.length;   // 缓冲大小 (约 1280*720*3 = 2.7MB)
        // mmap: 内核 DMA 缓冲映射到用户态, 零拷贝
        bufs[i] = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, buf.m.offset);
        if (bufs[i] == MAP_FAILED) { close(); return false; }
    }

    // ── 6. 所有缓冲入队 + 开始采集 ──
    for (int i = 0; i < num_bufs; i++) {
        v4l2_buffer buf;
        std::memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) { close(); return false; }
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) { close(); return false; }
    std::cout << "[CAM] streaming started" << std::endl;
    return true;
}

void* SimpleCamera::get_frame(int timeout_ms) {
    // ── select 等待数据就绪 (带超时) ──
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    timeval tv = {(timeout_ms / 1000), ((timeout_ms % 1000) * 1000)};
    if (select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0)
        return nullptr;  // 超时或出错

    // ── 出队: 从驱动拿到填好的缓冲 ──
    v4l2_buffer buf;
    std::memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) return nullptr;
    last_buf = buf.index;             // 记住索引, release 时需要
    return bufs[last_buf];            // 返回 mmap 指针 (零拷贝)
}

void SimpleCamera::release_frame() {
    // ── 入队: 把用完的缓冲还给驱动, 驱动继续往里写下一帧 ──
    v4l2_buffer buf;
    std::memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = last_buf;
    ioctl(fd, VIDIOC_QBUF, &buf);
}

void SimpleCamera::close() {
    // ── 逆序清理: 停止流 → 解除映射 → 关闭 fd ──
    if (fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(fd, VIDIOC_STREAMOFF, &type);  // 停止采集
        for (int i = 0; i < num_bufs; i++)
            if (bufs[i]) munmap(bufs[i], buf_sizes[i]);
        ::close(fd);
        fd = -1;
    }
}

SimpleCamera::~SimpleCamera() { close(); }
