// VideoDisplay: a desktop window (SDL2) that shows I420 frames.
//
// Used in this project by: app/decoder.cc, where the receiver's worker
// thread creates one (unless --lazy disables display) and calls show_frame()
// for every decoded frame and signal_quit() to notice the window being
// closed; and by the webcam tool (video/webcam.cc). The sender's preview
// window in app/capture.cc uses SDL directly, not this class.
//
// Key concepts:
// - The texture is SDL_PIXELFORMAT_IYUV, which is SDL's name for I420 (Y,
//   then U, then V planes; see image.hh), so frames are uploaded without any
//   colour conversion on the CPU; the GPU converts to RGB when drawing.
// - SDL is not thread-safe for windowing: create the display, show frames
//   and poll events all from the same thread.
// - The constructor calls SDL_Init() and the destructor SDL_Quit(), so only
//   one VideoDisplay should exist at a time.

#ifndef DISPLAY_HH
#define DISPLAY_HH

extern "C" {
#include <SDL2/SDL.h>
}

#include <memory>
#include "image.hh"

class VideoDisplay
{
public:
  // open a resizable window of the given size; throws std::runtime_error
  // with SDL's message if any SDL call fails (e.g. no display available)
  VideoDisplay(const uint16_t display_width, const uint16_t display_height);
  ~VideoDisplay();

  // display a frame
  // (dimensions must match the window's; with vsync enabled this may wait
  // for the next screen refresh)
  void show_frame(const RawImage & raw_img);

  // if signaled to quit
  // (drains pending window events; true once the user closes the window.
  // Call it regularly, or the OS may consider the window unresponsive.)
  bool signal_quit();

  // forbid copy and move operators
  VideoDisplay(const VideoDisplay & other) = delete;
  const VideoDisplay & operator=(const VideoDisplay & other) = delete;
  VideoDisplay(VideoDisplay && other) = delete;
  VideoDisplay & operator=(VideoDisplay && other) = delete;

private:
  uint16_t display_width_;
  uint16_t display_height_;

  // window -> renderer (draws into the window) -> texture (frame on the GPU)
  SDL_Window * window_ {nullptr};
  SDL_Renderer * renderer_ {nullptr};
  SDL_Texture * texture_ {nullptr};
  std::unique_ptr<SDL_Event> event_ {nullptr};
};

#endif /* DISPLAY_HH */
