// VideoDevice: reads frames from a V4L2 (Video4Linux2) camera such as
// /dev/video0, in YUYV format, and converts them to I420 RawImages.
//
// Used in this project by: the webcam tool (video/webcam.cc) only. The
// sender does NOT use this class: app/capture.cc has its own V4L2 capture
// code (separate thread, frame-rate setting via VIDIOC_S_PARM, libswscale
// conversion into the FrameRing), built on the same mmap streaming flow.
//
// Key concept, the V4L2 mmap streaming flow (what the constructor and
// read_frame() do):
//   1. VIDIOC_QUERYCAP   check the device can capture and stream
//   2. VIDIOC_S_FMT      ask for a pixel format and resolution
//   3. VIDIOC_REQBUFS    ask the driver to allocate N frame buffers
//   4. for each buffer: VIDIOC_QUERYBUF (get its size/offset), mmap() it
//      into our memory, VIDIOC_QBUF (hand it to the driver to fill)
//   5. VIDIOC_STREAMON   start capturing
//   then repeatedly:
//   6. VIDIOC_DQBUF      take a filled buffer (fails with EAGAIN in
//                        non-blocking mode if none is ready yet)
//   7. read its pixels, then VIDIOC_QBUF to give it back to the driver.
// The driver cycles through the buffers, so a buffer must be returned
// quickly or the camera runs out of buffers to fill and drops frames.

#ifndef V4L2_HH
#define V4L2_HH

extern "C" {
#include <linux/videodev2.h>
}

#include <string>
#include <vector>

#include "file_descriptor.hh"
#include "mmap.hh"
#include "video_input.hh"

class VideoDevice : public VideoInput
{
public:
  // open the device and start streaming at exactly this resolution in YUYV;
  // throws if the device cannot do so (steps 1-5 above)
  VideoDevice(const std::string & video_device_path,
              const uint16_t display_width,
              const uint16_t display_height);
  // stops streaming; the buffers are then unmapped and the fd closed by the
  // member destructors
  ~VideoDevice();

  // video device defaults to blocking mode
  void set_blocking(const bool blocking);

  // try to fetch a video frame from webcam into raw_img
  // (returns false if no frame could be dequeued, which in non-blocking mode
  // usually means "not ready yet"; other DQBUF errors also return false)
  bool read_frame(RawImage & raw_img) override;

  // accessors
  // (fd() is what an event loop such as Poller watches for In)
  FileDescriptor & fd() { return fd_; }
  uint16_t display_width() const override { return display_width_; }
  uint16_t display_height() const override { return display_height_; }

private:
  FileDescriptor fd_;
  uint16_t display_width_;
  uint16_t display_height_;

  v4l2_buffer buf_info_ {}; // allocate a buffer info to reuse
  std::vector<MMap> buf_mem_ {}; // memory region for V4L2 buffers
                                 // (indexed by the V4L2 buffer index)

  // constants
  static constexpr uint32_t pixel_format = V4L2_PIX_FMT_YUYV;
  static constexpr uint32_t buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  static constexpr size_t NUM_BUFFERS = 4;
};

#endif /* V4L2_HH */
