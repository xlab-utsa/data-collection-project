// VideoInput: abstract interface for "something that produces raw frames",
// implemented by VideoDevice (v4l2.hh, a live camera) and YUV4MPEG
// (yuv4mpeg.hh, a .y4m file). Code written against VideoInput can switch
// between a camera and a recorded clip without other changes.
//
// Currently UNUSED by the apps: the sender captures with app/capture.cc
// instead. It is kept from the Ringmaster code base.

#ifndef VIDEO_INPUT_HH
#define VIDEO_INPUT_HH

#include "image.hh"

class VideoInput
{
public:
  VideoInput() {}
  virtual ~VideoInput() {}

  virtual uint16_t display_width() const = 0;
  virtual uint16_t display_height() const = 0;

  // read a raw frame into raw_img
  // (returns false if no frame is available now; raw_img must have the
  // same dimensions as the input, or implementations throw)
  virtual bool read_frame(RawImage & raw_img) = 0;
};

#endif /* VIDEO_INPUT_HH */
