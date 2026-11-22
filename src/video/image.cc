// Implementation of RawImage (image.hh): allocation/ownership of the
// vpx_image and copying pixels into it from other layouts.

#include <cstring>
#include <stdexcept>

#include "image.hh"

using namespace std;

// constructor that allocates and owns the vpx_image
// (vpx_img_alloc(nullptr, ...) allocates both the struct and the pixel
// buffer; the last argument is the row alignment. With 1, no extra padding
// is requested, so for even widths the strides equal the plane widths, which
// the single-memcpy copy functions below rely on.)
RawImage::RawImage(const uint16_t display_width, const uint16_t display_height)
  : vpx_img_(vpx_img_alloc(nullptr, VPX_IMG_FMT_I420,
                           display_width, display_height, 1)),
    own_vpx_img_(true),
    display_width_(display_width),
    display_height_(display_height)
{}

// constructor with a non-owning pointer to vpx_image
RawImage::RawImage(vpx_image_t * const vpx_img)
  : vpx_img_(vpx_img),
    own_vpx_img_(false),
    display_width_(),
    display_height_()
{
  if (not vpx_img) {
    throw runtime_error("RawImage: unable to construct from a null vpx_img");
  }

  if (vpx_img->fmt != VPX_IMG_FMT_I420) {
    throw runtime_error("RawImage: only supports I420");
  }

  // d_w/d_h are the displayed (visible) dimensions
  display_width_ = vpx_img->d_w;
  display_height_ = vpx_img->d_h;
}

RawImage::~RawImage()
{
  // free vpx_image only if the class owns it
  if (own_vpx_img_) {
    vpx_img_free(vpx_img_);
  }
}

// YUYV (a.k.a. YUY2) is packed 4:2:2: every 2 horizontal pixels take 4 bytes,
//   Y0 U Y1 V
// i.e. each pixel has its own Y, and each pair of pixels in a row shares one
// U and one V. I420 shares U/V over 2x2 blocks instead, so the conversion
// keeps every Y, and takes U/V from the even rows only (odd rows' colour is
// dropped).
void RawImage::copy_from_yuyv(const string_view src)
{
  // expects YUYV to have size of 2 * W * H
  if (src.size() != y_size() * 2) {
    throw runtime_error("RawImage: invalid YUYV size");
  }

  uint8_t * dst_y = y_plane();
  uint8_t * dst_u = u_plane();
  uint8_t * dst_v = v_plane();

  // copy Y plane
  // (Y samples are every other byte: offsets 0, 2, 4, ...)
  const uint8_t * p = reinterpret_cast<const uint8_t *>(src.data());
  for (unsigned i = 0; i < y_size(); i++, p += 2) {
    *dst_y++ = *p;
  }

  // copy U and V planes
  // (the inner loop walks one full row, 2 * W bytes, in 4-byte steps reading
  // U at p[1] and V at p[3]; the outer increment then skips the next row)
  p = reinterpret_cast<const uint8_t *>(src.data());
  for (unsigned i = 0; i < display_height_ / 2; i++, p += 2 * display_width_) {
    for (unsigned j = 0; j < display_width_ / 2; j++, p += 4) {
      *dst_u++ = p[1];
      *dst_v++ = p[3];
    }
  }
}

void RawImage::copy_from_i420(const uint8_t * src, const size_t size)
{
  if (size != y_size() + 2 * uv_size()) {
    throw runtime_error("RawImage::copy_from_i420: invalid frame size");
  }

  // the source is tightly packed: its row length equals the plane width
  const size_t w = display_width_;
  const size_t h = display_height_;
  const uint8_t * src_y = src;
  const uint8_t * src_u = src_y + y_size();
  const uint8_t * src_v = src_u + uv_size();

  // vpx_image rows may be padded (stride >= width), so copy row by row
  for (size_t row = 0; row < h; row++) {
    memcpy(y_plane() + row * y_stride(), src_y + row * w, w);
  }
  // chroma planes have half the rows and half the width
  for (size_t row = 0; row < h / 2; row++) {
    memcpy(u_plane() + row * u_stride(), src_u + row * w / 2, w / 2);
    memcpy(v_plane() + row * v_stride(), src_v + row * w / 2, w / 2);
  }
}

// the three functions below copy one whole plane at once (see image.hh)
void RawImage::copy_y_from(const string_view src)
{
  if (src.size() != y_size()) {
    throw runtime_error("RawImage: invalid size for Y plane");
  }

  memcpy(y_plane(), src.data(), src.size());
}

void RawImage::copy_u_from(const string_view src)
{
  if (src.size() != uv_size()) {
    throw runtime_error("RawImage: invalid size for U plane");
  }

  memcpy(u_plane(), src.data(), src.size());
}

void RawImage::copy_v_from(const string_view src)
{
  if (src.size() != uv_size()) {
    throw runtime_error("RawImage: invalid size for V plane");
  }

  memcpy(v_plane(), src.data(), src.size());
}
