// YUV4MPEG: reads raw I420 frames from a .y4m (YUV4MPEG2) video file, as a
// VideoInput (video_input.hh). Useful for feeding a recorded clip instead of
// a live camera, optionally looping forever.
//
// Currently UNUSED by the apps. (The receiver writes .y4m recordings itself
// in app/decoder.cc with an ofstream; this reader can read those files back,
// since they use the same 4:2:0 layout.)
//
// Key concept, the Y4M file format: a single text header line, e.g.
//   YUV4MPEG2 W1280 H720 F30:1 Ip A1:1 C420\n
// (W = width, H = height, F = frame rate, I = interlacing, A = pixel aspect
// ratio, C = colour space; C is optional and defaults to 4:2:0), followed by
// frames, each a text line starting with "FRAME" and then exactly
// W * H * 3 / 2 bytes of raw I420 data (Y plane, U plane, V plane; see
// image.hh). There is no compression and no index, so frame k starts at a
// computable offset, and files are large (about 1.4 MB per 1280x720 frame).

#ifndef YUV4MPEG_HH
#define YUV4MPEG_HH

#include <string>

#include "file_descriptor.hh"
#include "video_input.hh"

class YUV4MPEG : public VideoInput
{
public:
  // open the file and validate its header against the expected dimensions
  // (throws if the signature, W, H or a C token other than C420* mismatch)
  YUV4MPEG(const std::string & video_file_path,
           const uint16_t display_width,
           const uint16_t display_height,
           const bool loop = false); // Yuxin: stop the loop of video streaming

  // byte sizes of one frame and of its planes
  size_t frame_size() const { return display_width_ * display_height_ * 3 / 2; }
  size_t y_size() const { return display_width_ * display_height_; }
  size_t uv_size() const { return display_width_ * display_height_ / 4; }

  // try to fetch a video frame from video file into raw_img
  // (returns false at end of file unless 'loop' was set, in which case it
  // rewinds to the first frame)
  bool read_frame(RawImage & raw_img) override;

  // accessors
  FileDescriptor & fd() { return fd_; }
  uint16_t display_width() const override { return display_width_; }
  uint16_t display_height() const override { return display_height_; }

private:
  FileDescriptor fd_;
  uint16_t display_width_;
  uint16_t display_height_;

  // loop over the file infinitely
  bool loop_;
};

#endif /* YUV4MPEG_HH */
