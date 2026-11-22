// Implementation of the sender's capture thread (capture.hh).
//
// V4L2 streaming in mmap mode, in short: ask the driver for a format
// (VIDIOC_S_FMT, VIDIOC_S_PARM), request a few kernel buffers
// (VIDIOC_REQBUFS), map each one into our address space (mmap) and hand them
// to the driver (VIDIOC_QBUF), start streaming (VIDIOC_STREAMON). Then loop:
// wait until a buffer is filled (poll), take it (VIDIOC_DQBUF), use the
// pixels, and give it back (VIDIOC_QBUF). The driver cycles through the
// buffers, so a buffer must be returned quickly or the camera stalls.

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev2.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <SDL2/SDL.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include "capture.hh"
#include "common.hh"
#include "exception.hh"

using namespace std;
using namespace std::chrono;

namespace {

constexpr int PREVIEW_W = 640;
constexpr int PREVIEW_H = 480;
constexpr int PREVIEW_BYTES_PER_PIXEL = 2; // RGB565
constexpr auto PREVIEW_INTERVAL = milliseconds(33); // refresh preview at ~30 Hz
constexpr unsigned int NUM_V4L2_BUFFERS = 4;

// ioctl() that retries on EINTR, as recommended by the V4L2 docs
int xioctl(const int fd, const unsigned long request, void * arg)
{
  int ret;
  do {
    ret = ioctl(fd, request, arg);
  } while (ret == -1 and errno == EINTR);
  return ret;
}

// ---------------------------------------------------------------------------
// V4L2 camera in memory-mapped streaming mode
// ---------------------------------------------------------------------------
class V4L2Camera
{
public:
  V4L2Camera(const CaptureConfig & config)
    : fd_(check_syscall(open(config.device.c_str(), O_RDWR | O_NONBLOCK),
                        "open " + config.device))
  {
    try {
      set_format(config);
      map_buffers();
      start_streaming();
    } catch (...) {
      release();
      throw;
    }
  }

  ~V4L2Camera() { release(); }

  int fd() const { return fd_; }
  size_t bytes_per_line() const { return bytes_per_line_; }

  // Dequeue a filled buffer; returns its index, or -1 if none is ready yet.
  int dequeue()
  {
    v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
      if (errno == EAGAIN) {
        return -1;
      }
      throw unix_error("VIDIOC_DQBUF");
    }
    return static_cast<int>(buf.index);
  }

  const uint8_t * data(const int index) const
  {
    return static_cast<const uint8_t *>(buffers_.at(index).start);
  }

  // Hand a buffer back to the driver so it can be filled again.
  void requeue(const int index)
  {
    v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = static_cast<unsigned int>(index);
    check_syscall(xioctl(fd_, VIDIOC_QBUF, &buf), "VIDIOC_QBUF");
  }

  V4L2Camera(const V4L2Camera &) = delete;
  V4L2Camera & operator=(const V4L2Camera &) = delete;

private:
  struct MappedBuffer {
    void * start;
    size_t length;
  };

  int fd_;
  size_t bytes_per_line_ {0};
  vector<MappedBuffer> buffers_ {};
  bool streaming_ {false};

  void set_format(const CaptureConfig & config)
  {
    v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = config.width;
    fmt.fmt.pix.height = config.height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE; // progressive
    check_syscall(xioctl(fd_, VIDIOC_S_FMT, &fmt), "VIDIOC_S_FMT");

    // the driver may silently pick a different mode; refuse to continue,
    // since the encoder and the receiver were told the requested size
    if (fmt.fmt.pix.width != static_cast<unsigned>(config.width) or
        fmt.fmt.pix.height != static_cast<unsigned>(config.height) or
        fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
      throw runtime_error("camera does not support YUYV " +
                          to_string(config.width) + "x" +
                          to_string(config.height) + " (driver offered " +
                          to_string(fmt.fmt.pix.width) + "x" +
                          to_string(fmt.fmt.pix.height) + ")");
    }
    bytes_per_line_ = fmt.fmt.pix.bytesperline
                      ? fmt.fmt.pix.bytesperline : config.width * 2;

    v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = config.fps;
    check_syscall(xioctl(fd_, VIDIOC_S_PARM, &parm), "VIDIOC_S_PARM");
  }

  void map_buffers()
  {
    v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = NUM_V4L2_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    check_syscall(xioctl(fd_, VIDIOC_REQBUFS, &req), "VIDIOC_REQBUFS");

    // map every buffer separately and remember where each one lives;
    // DQBUF reports buffers by index, so frames are found via buffers_[index]
    for (unsigned int i = 0; i < req.count; i++) {
      v4l2_buffer buf;
      memset(&buf, 0, sizeof(buf));
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index = i;
      check_syscall(xioctl(fd_, VIDIOC_QUERYBUF, &buf), "VIDIOC_QUERYBUF");

      void * start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd_, buf.m.offset);
      if (start == MAP_FAILED) {
        throw unix_error("mmap");
      }
      buffers_.push_back({start, buf.length});

      check_syscall(xioctl(fd_, VIDIOC_QBUF, &buf), "VIDIOC_QBUF");
    }
  }

  void start_streaming()
  {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    check_syscall(xioctl(fd_, VIDIOC_STREAMON, &type), "VIDIOC_STREAMON");
    streaming_ = true;
  }

  void release()
  {
    if (streaming_) {
      int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      xioctl(fd_, VIDIOC_STREAMOFF, &type);
      streaming_ = false;
    }
    for (const auto & b : buffers_) {
      munmap(b.start, b.length);
    }
    buffers_.clear();
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
  }
};

// ---------------------------------------------------------------------------
// Preview window: shows the most recent RGB565 frame on its own thread
// ---------------------------------------------------------------------------
class PreviewWindow
{
public:
  PreviewWindow() : thread_(&PreviewWindow::run, this) {}

  ~PreviewWindow()
  {
    {
      lock_guard<mutex> lock(mtx_);
      closing_ = true;
    }
    thread_.join();
  }

  // copy the latest preview frame (PREVIEW_W x PREVIEW_H, RGB565)
  void update(const uint8_t * rgb565)
  {
    lock_guard<mutex> lock(mtx_);
    memcpy(pixels_.data(), rgb565, pixels_.size());
    has_frame_ = true;
  }

  PreviewWindow(const PreviewWindow &) = delete;
  PreviewWindow & operator=(const PreviewWindow &) = delete;

private:
  mutex mtx_ {};
  vector<uint8_t> pixels_ =
      vector<uint8_t>(PREVIEW_W * PREVIEW_H * PREVIEW_BYTES_PER_PIXEL);
  bool has_frame_ {false};
  bool closing_ {false};
  thread thread_;

  bool closing()
  {
    lock_guard<mutex> lock(mtx_);
    return closing_ or g_stop_requested;
  }

  void run()
  {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      cerr << "Preview disabled: SDL_Init failed: " << SDL_GetError() << endl;
      return;
    }

    SDL_Window * win = SDL_CreateWindow("Sender preview",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        PREVIEW_W, PREVIEW_H, 0);
    SDL_Renderer * ren = win ? SDL_CreateRenderer(win, -1, 0) : nullptr;
    SDL_Texture * tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, PREVIEW_W, PREVIEW_H) : nullptr;

    if (not tex) {
      cerr << "Preview disabled: " << SDL_GetError() << endl;
    } else {
      SDL_Event ev;
      while (not closing()) {
        while (SDL_PollEvent(&ev)) {
          if ((ev.type == SDL_KEYDOWN and ev.key.keysym.sym == SDLK_q) or
              ev.type == SDL_QUIT) {
            cerr << "Preview window closed: stopping." << endl;
            g_stop_requested = true;
          }
        }

        bool drew = false;
        {
          lock_guard<mutex> lock(mtx_);
          if (has_frame_) {
            SDL_UpdateTexture(tex, nullptr, pixels_.data(),
                              PREVIEW_W * PREVIEW_BYTES_PER_PIXEL);
            has_frame_ = false;
            drew = true;
          }
        }

        if (drew) {
          SDL_RenderClear(ren);
          SDL_RenderCopy(ren, tex, nullptr, nullptr);
          SDL_RenderPresent(ren);
        } else {
          SDL_Delay(10);
        }
      }
    }

    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
  }
};

struct SwsDeleter {
  void operator()(SwsContext * ctx) const { sws_freeContext(ctx); }
};
using SwsPtr = unique_ptr<SwsContext, SwsDeleter>;

SwsPtr make_sws(const int src_w, const int src_h, const AVPixelFormat src_fmt,
                const int dst_w, const int dst_h, const AVPixelFormat dst_fmt)
{
  SwsContext * ctx = sws_getContext(src_w, src_h, src_fmt, dst_w, dst_h,
                                    dst_fmt, SWS_BILINEAR,
                                    nullptr, nullptr, nullptr);
  if (not ctx) {
    throw runtime_error("sws_getContext failed");
  }
  return SwsPtr(ctx);
}

void capture_loop(const CaptureConfig & config, FrameRing & ring)
{
  const int w = config.width;
  const int h = config.height;

  V4L2Camera camera(config);
  cerr << "Capturing from " << config.device << ": " << w << "x" << h
       << " YUYV @ " << config.fps << " fps" << endl;

  // YUYV 4:2:2 (packed) -> I420 (planar Y, U, V; chroma at half resolution)
  const SwsPtr to_i420 = make_sws(w, h, AV_PIX_FMT_YUYV422,
                                  w, h, AV_PIX_FMT_YUV420P);
  const size_t y_size = static_cast<size_t>(w) * h;
  const size_t uv_size = y_size / 4;
  int i420_strides[3] = {w, w / 2, w / 2};

  // YUYV -> downscaled RGB565 for the preview window
  unique_ptr<PreviewWindow> preview;
  SwsPtr to_preview;
  vector<uint8_t> preview_rgb;
  if (config.preview) {
    preview = make_unique<PreviewWindow>();
    to_preview = make_sws(w, h, AV_PIX_FMT_YUYV422,
                          PREVIEW_W, PREVIEW_H, AV_PIX_FMT_RGB565);
    preview_rgb.resize(PREVIEW_W * PREVIEW_H * PREVIEW_BYTES_PER_PIXEL);
  }
  auto last_preview = steady_clock::now() - PREVIEW_INTERVAL;

  const int src_stride[1] = {static_cast<int>(camera.bytes_per_line())};
  size_t last_reported_drops = 0;

  while (not g_stop_requested) {
    pollfd pfd {camera.fd(), POLLIN, 0};
    const int ret = ::poll(&pfd, 1, 1000);
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw unix_error("poll");
    }
    if (ret == 0) {
      cerr << "Warning: no frame from camera for 1 s" << endl;
      continue;
    }

    const int index = camera.dequeue();
    if (index < 0) {
      continue;
    }
    const uint8_t * src_planes[1] = {camera.data(index)};

    // preview (rate-limited; the scaling is not free at 4K and above)
    const auto now = steady_clock::now();
    if (preview and now - last_preview >= PREVIEW_INTERVAL) {
      uint8_t * dst[1] = {preview_rgb.data()};
      const int dst_stride[1] = {PREVIEW_W * PREVIEW_BYTES_PER_PIXEL};
      sws_scale(to_preview.get(), src_planes, src_stride, 0, h,
                dst, dst_stride);
      preview->update(preview_rgb.data());
      last_preview = now;
    }

    // convert straight into a free ring slot (or drop the frame if the
    // encoder is so far behind that the ring is full)
    if (uint8_t * slot = ring.begin_write()) {
      uint8_t * dst[3] = {slot, slot + y_size, slot + y_size + uv_size};
      sws_scale(to_i420.get(), src_planes, src_stride, 0, h,
                dst, i420_strides);
      ring.commit_write();
    } else if (ring.dropped() >= last_reported_drops + 100) {
      last_reported_drops = ring.dropped();
      cerr << "Warning: frame ring full; dropped " << last_reported_drops
           << " captured frames so far" << endl;
    }

    camera.requeue(index);
  }
}

} // namespace

void run_capture(const CaptureConfig & config, FrameRing & ring)
{
  try {
    capture_loop(config, ring);
  } catch (const exception & e) {
    cerr << "Capture thread error: " << e.what() << endl;
    g_stop_requested = true;
  }
}
