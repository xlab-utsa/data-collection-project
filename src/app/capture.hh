#ifndef CAPTURE_HH
#define CAPTURE_HH

// Camera capture for the sender.
//
// run_capture() opens a V4L2 camera that produces packed YUYV 4:2:2 frames,
// converts each frame to planar I420 (YUV 4:2:0, the encoder's input format)
// and writes it into a FrameRing for the encoder thread. Optionally it also
// shows a small 640x480 SDL preview window; pressing 'q' in that window
// stops the whole program.

#include <string>

#include "frame_ring.hh"

struct CaptureConfig
{
  std::string device {"/dev/video0"};
  int width {1920};
  int height {1080};
  int fps {60};
  bool preview {true}; // open the SDL preview window
};

// Capture until g_stop_requested (common.hh) becomes true. Intended to run on
// its own thread. Errors are reported on stderr and also request a stop, so
// the encoder thread does not wait forever for frames.
void run_capture(const CaptureConfig & config, FrameRing & ring);

#endif /* CAPTURE_HH */
