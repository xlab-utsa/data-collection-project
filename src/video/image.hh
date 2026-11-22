// RawImage: an uncompressed video frame in I420 format, wrapping libvpx's
// vpx_image_t so it can be handed straight to the VP9 encoder.
//
// Used in this project by: app/video_sender.cc (one RawImage is filled from
// the capture FrameRing with copy_from_i420() every frame tick),
// app/encoder.hh / encoder.cc (compress_frame() encodes it) and
// app/decoder.cc (wraps each decoded vpx_image, without owning it, to show it
// on the VideoDisplay). In src/video: sdl.cc, v4l2.cc, yuv4mpeg.cc and the
// webcam tool.
//
// Key concepts:
// - I420 (also called YUV420p / IYUV) stores a W x H frame as three separate
//   planes: Y (brightness), W x H bytes; then U (Cb) and V (Cr) (colour),
//   each (W/2) x (H/2) bytes, because one colour sample is shared by a 2x2
//   block of pixels. Total size: W * H * 3 / 2 bytes. W and H are assumed
//   to be even.
// - Stride (also "pitch" or "linesize"): the number of bytes from the start
//   of one row to the start of the next. It can be larger than the visible
//   width because libraries pad rows for alignment. Pixel (x, y) of the Y
//   plane is at y_plane()[y * y_stride() + x]. Code that copies a plane with
//   a single memcpy is only correct when stride == width; copy row by row
//   otherwise, as copy_from_i420() does.
// - Ownership: an image created from (width, height) is allocated here and
//   freed in the destructor. One created from an existing vpx_image_t* (e.g.
//   a frame returned by the decoder) only borrows it.

#ifndef IMAGE_HH
#define IMAGE_HH

extern "C" {
#include <vpx/vpx_image.h>
}

#include <cstdint>
#include <string_view>

// wrapper class for vpx_image of format I420
class RawImage
{
public:
  // allocate a new vpx_image and own it
  RawImage(const uint16_t display_width, const uint16_t display_height);

  // hold a non-owning pointer to an existing vpx_image
  // (throws if it is null or not I420; the vpx_image must outlive this object)
  RawImage(vpx_image_t * const vpx_img);

  // free the vpx_image only if the class owns it
  ~RawImage();

  // return the underlying vpx_image
  vpx_image * get_vpx_image() const { return vpx_img_; }

  // image dimensions
  uint16_t display_width() const { return display_width_; }
  uint16_t display_height() const { return display_height_; }

  // plane size
  // (visible bytes only, i.e. without any stride padding)
  size_t y_size() const { return display_width_ * display_height_; }
  size_t uv_size() const { return display_width_ * display_height_ / 4; }

  // pointers to the top left pixel for each plane
  uint8_t * y_plane() const { return vpx_img_->planes[VPX_PLANE_Y]; }
  uint8_t * u_plane() const { return vpx_img_->planes[VPX_PLANE_U]; }
  uint8_t * v_plane() const { return vpx_img_->planes[VPX_PLANE_V]; }

  // stride between rows for each plane
  int y_stride() const { return vpx_img_->stride[VPX_PLANE_Y]; }
  int u_stride() const { return vpx_img_->stride[VPX_PLANE_U]; }
  int v_stride() const { return vpx_img_->stride[VPX_PLANE_V]; }

  // copy image data from a YUYV-formatted buffer
  // (YUYV is the packed 4:2:2 format many webcams deliver; see image.cc.
  // Assumes stride == width, which holds for images allocated by this class)
  void copy_from_yuyv(const std::string_view src);

  // copy plane data from a buffer
  // (each 'src' must be exactly one tightly packed plane; a single memcpy is
  // used, so these also assume stride == width)
  void copy_y_from(const std::string_view src);
  void copy_u_from(const std::string_view src);
  void copy_v_from(const std::string_view src);

  // copy a tightly packed I420 frame (Y plane, then U, then V; size must be
  // W * H * 3 / 2), e.g. a slot of the sender's FrameRing
  void copy_from_i420(const uint8_t * src, const size_t size);

  // forbid copy and move operators
  RawImage(const RawImage & other) = delete;
  const RawImage & operator=(const RawImage & other) = delete;
  RawImage(RawImage && other) = delete;
  RawImage & operator=(RawImage && other) = delete;

private:
  // underlying vpx_image
  vpx_image * vpx_img_;

  // if the class owns the vpx_image
  bool own_vpx_img_;

  // image display dimensions
  uint16_t display_width_;
  uint16_t display_height_;
};

#endif /* IMAGE_HH */
