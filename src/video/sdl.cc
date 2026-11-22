// Implementation of VideoDisplay (sdl.hh).

#include <stdexcept>
#include "sdl.hh"

using namespace std;

VideoDisplay::VideoDisplay(const uint16_t display_width,
                           const uint16_t display_height)
  : display_width_(display_width), display_height_(display_height)
{
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    throw runtime_error(SDL_GetError());
  }

  window_ = SDL_CreateWindow(
    "Video Display",
    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
    display_width, display_height,
    SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);

  if (window_ == nullptr) {
    throw runtime_error(SDL_GetError());
  }

  // GPU-accelerated renderer; PRESENTVSYNC syncs presenting to the screen
  // refresh, which avoids tearing
  renderer_ = SDL_CreateRenderer(
    window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  if (renderer_ == nullptr) {
    throw runtime_error(SDL_GetError());
  }

  // STREAMING: the texture's contents are replaced every frame
  texture_ = SDL_CreateTexture(
    renderer_, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
    display_width, display_height);

  if (texture_ == nullptr) {
    throw runtime_error(SDL_GetError());
  }

  event_ = make_unique<SDL_Event>();
}

// destroy in the reverse order of creation
VideoDisplay::~VideoDisplay()
{
  SDL_DestroyTexture(texture_);
  SDL_DestroyRenderer(renderer_);
  SDL_DestroyWindow(window_);
  SDL_Quit();
}

void VideoDisplay::show_frame(const RawImage & raw_img)
{
  if (raw_img.display_width() != display_width_ or
      raw_img.display_height() != display_height_) {
    throw runtime_error("VideoDisplay: image dimensions don't match");
  }

  // upload the three planes, each with its own stride, so padded images
  // (e.g. straight from the decoder) display correctly
  SDL_UpdateYUVTexture(texture_, nullptr,
    raw_img.y_plane(), raw_img.y_stride(),
    raw_img.u_plane(), raw_img.u_stride(),
    raw_img.v_plane(), raw_img.v_stride());
  // draw the texture scaled to the whole window, then show it
  SDL_RenderClear(renderer_);
  SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
  SDL_RenderPresent(renderer_);
}

bool VideoDisplay::signal_quit()
{
  // SDL_PollEvent() does not block; it returns 0 once the queue is empty
  while (SDL_PollEvent(event_.get())) {
    if (event_->type == SDL_QUIT) {
      return true;
    }
  }

  return false;
}
