/*
 * capture_to_disk: record a V4L2 camera straight to a .y4m file, with a
 * 640x480 live preview. No encoding and no network; this is the "capture
 * only" tool from pipeline v1, kept for recording raw reference footage.
 *
 * Build (not part of the autotools build):
 *   gcc -O2 -o capture_to_disk capture_to_disk.c \
 *       $(pkg-config --cflags --libs sdl2 libavutil libswscale) -lpthread
 *
 * Usage:
 *   ./capture_to_disk -w 1920 -h 1080 -r 60 [-p 422|420] [-d /dev/video0]
 *
 * Output: YYYYMMDD_HHMMSS.y4m in the current directory, as planar 4:2:2
 * (-p 422, the default, keeps the camera's full chroma) or 4:2:0 (-p 420).
 * Recording stops on Ctrl+C, on 'q' in the preview window, or when less than
 * 1 GiB of disk space is left.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#include <linux/videodev2.h>

#include <SDL2/SDL.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define NUM_BUFFERS 4
#define PREVIEW_W 640
#define PREVIEW_H 480

/* ---------------------- configuration ---------------------- */
static const char *dev_name = "/dev/video0";
static int width = 1920;
static int height = 1080;
static int fps = 30;
static int pixel_mode = 422;   /* 422 or 420: chroma layout of the output */

/* ---------------------- state ---------------------- */
static int fd = -1;
struct mapped_buffer { void *start; size_t length; };
static struct mapped_buffer buffers[NUM_BUFFERS];
static unsigned n_buffers = 0;

static volatile sig_atomic_t run = 1;  /* cleared by Ctrl+C or 'q' */

/* latest preview frame (RGB565), shared with the preview thread */
static pthread_mutex_t preview_mtx = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *preview_rgb = NULL;
static int preview_ready = 0;

static void sigint_handler(int s)
{
  (void)s;
  run = 0;
}

static void die(const char *what)
{
  perror(what);
  exit(1);
}

static int xioctl(int f, unsigned long req, void *arg)
{
  int r;
  do { r = ioctl(f, req, arg); } while (r == -1 && errno == EINTR);
  return r;
}

/* ---------------------- V4L2 setup ---------------------- */

/* request YUYV at width x height, progressive, at 'fps' */
static void set_format(void)
{
  struct v4l2_format fmt;
  CLEAR(fmt);
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = width;
  fmt.fmt.pix.height = height;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;
  if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) die("VIDIOC_S_FMT");

  if ((int)fmt.fmt.pix.width != width || (int)fmt.fmt.pix.height != height ||
      fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
    fprintf(stderr, "Camera does not support YUYV %dx%d (offered %ux%u)\n",
            width, height, fmt.fmt.pix.width, fmt.fmt.pix.height);
    exit(1);
  }

  struct v4l2_streamparm parm;
  CLEAR(parm);
  parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parm.parm.capture.timeperframe.numerator = 1;
  parm.parm.capture.timeperframe.denominator = fps;
  if (xioctl(fd, VIDIOC_S_PARM, &parm) < 0) die("VIDIOC_S_PARM");
}

/* map the driver's capture buffers; each is looked up later by its index */
static void init_mmap(void)
{
  struct v4l2_requestbuffers req;
  CLEAR(req);
  req.count = NUM_BUFFERS;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) die("VIDIOC_REQBUFS");

  n_buffers = req.count < NUM_BUFFERS ? req.count : NUM_BUFFERS;
  for (unsigned i = 0; i < n_buffers; i++) {
    struct v4l2_buffer buf;
    CLEAR(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;
    if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) die("VIDIOC_QUERYBUF");

    buffers[i].length = buf.length;
    buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, buf.m.offset);
    if (buffers[i].start == MAP_FAILED) die("mmap");

    if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) die("VIDIOC_QBUF");
  }
}

/* 1 if less than 1 GiB is free in the current directory */
static int low_space(void)
{
  struct statvfs s;
  if (statvfs(".", &s) < 0) return 0;
  unsigned long long free_b = s.f_bavail * (unsigned long long)s.f_frsize;
  return free_b < (1ULL << 30);
}

/* ---------------------- preview thread ---------------------- */
static void *preview_thread(void *arg)
{
  (void)arg;
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "Preview disabled: %s\n", SDL_GetError());
    return NULL;
  }
  SDL_Window *win = SDL_CreateWindow("Preview", SDL_WINDOWPOS_UNDEFINED,
                                     SDL_WINDOWPOS_UNDEFINED,
                                     PREVIEW_W, PREVIEW_H, 0);
  SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, 0) : NULL;
  SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             PREVIEW_W, PREVIEW_H) : NULL;
  if (!tex) {
    fprintf(stderr, "Preview disabled: %s\n", SDL_GetError());
  }

  SDL_Event ev;
  while (tex && run) {
    while (SDL_PollEvent(&ev)) {
      if ((ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_q) ||
          ev.type == SDL_QUIT) {
        run = 0;
      }
    }

    int draw = 0;
    pthread_mutex_lock(&preview_mtx);
    if (preview_ready) {
      SDL_UpdateTexture(tex, NULL, preview_rgb, PREVIEW_W * 2);
      preview_ready = 0;
      draw = 1;
    }
    pthread_mutex_unlock(&preview_mtx);

    if (draw) {
      SDL_RenderClear(ren);
      SDL_RenderCopy(ren, tex, NULL, NULL);
      SDL_RenderPresent(ren);
    } else {
      SDL_Delay(10);
    }
  }

  if (tex) SDL_DestroyTexture(tex);
  if (ren) SDL_DestroyRenderer(ren);
  if (win) SDL_DestroyWindow(win);
  SDL_Quit();
  return NULL;
}

/* ---------------------- capture loop ---------------------- */
static void capture_loop(const char *fname)
{
  FILE *out = fopen(fname, "wb");
  if (!out) die("fopen");

  /* YUYV -> planar output (same size) for the file */
  const enum AVPixelFormat out_fmt =
      (pixel_mode == 420) ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV422P;
  fprintf(out, "YUV4MPEG2 W%d H%d F%d:1 Ip A1:1 %s\n", width, height, fps,
          pixel_mode == 420 ? "C420jpeg" : "C422");

  const size_t y_sz = (size_t)width * height;
  const size_t c_sz = (pixel_mode == 420) ? y_sz / 4 : y_sz / 2;
  uint8_t *frame = av_malloc(y_sz + 2 * c_sz);
  uint8_t *planes[3] = { frame, frame + y_sz, frame + y_sz + c_sz };
  int strides[3] = { width, width / 2, width / 2 };
  struct SwsContext *to_file = sws_getContext(width, height, AV_PIX_FMT_YUYV422,
      width, height, out_fmt, SWS_BILINEAR, NULL, NULL, NULL);

  /* YUYV -> downscaled RGB565 for the preview */
  preview_rgb = malloc(PREVIEW_W * PREVIEW_H * 2);
  uint8_t *prev_buf = av_malloc(PREVIEW_W * PREVIEW_H * 2);
  struct SwsContext *to_preview = sws_getContext(width, height,
      AV_PIX_FMT_YUYV422, PREVIEW_W, PREVIEW_H, AV_PIX_FMT_RGB565,
      SWS_BILINEAR, NULL, NULL, NULL);

  if (!frame || !preview_rgb || !prev_buf || !to_file || !to_preview) {
    fprintf(stderr, "Out of memory / swscale init failed\n");
    exit(1);
  }

  pthread_t tid;
  pthread_create(&tid, NULL, preview_thread, NULL);

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) die("VIDIOC_STREAMON");

  struct pollfd pfd = { fd, POLLIN, 0 };
  time_t last_flush = time(NULL);
  unsigned long frames = 0;

  while (run) {
    if (low_space()) {
      fprintf(stderr, "Disk <1GiB, stopping.\n");
      break;
    }

    int r = poll(&pfd, 1, 1000);
    if (r < 0) {
      if (errno == EINTR) continue;
      perror("poll");
      break;
    }
    if (r == 0) continue; /* no frame within 1 s */

    struct v4l2_buffer buf;
    CLEAR(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
      if (errno == EAGAIN) continue;
      perror("VIDIOC_DQBUF");
      break;
    }

    const uint8_t *src[1] = { buffers[buf.index].start };
    const int src_stride[1] = { width * 2 };

    /* preview */
    uint8_t *pdst[1] = { prev_buf };
    const int pstride[1] = { PREVIEW_W * 2 };
    sws_scale(to_preview, src, src_stride, 0, height, pdst, pstride);
    pthread_mutex_lock(&preview_mtx);
    memcpy(preview_rgb, prev_buf, PREVIEW_W * PREVIEW_H * 2);
    preview_ready = 1;
    pthread_mutex_unlock(&preview_mtx);

    /* file */
    sws_scale(to_file, src, src_stride, 0, height, planes, strides);
    fputs("FRAME\n", out);
    fwrite(planes[0], 1, y_sz, out);
    fwrite(planes[1], 1, c_sz, out);
    fwrite(planes[2], 1, c_sz, out);
    frames++;

    if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) die("VIDIOC_QBUF");

    if (difftime(time(NULL), last_flush) >= 5.0) {
      fflush(out);
      last_flush = time(NULL);
    }
  }

  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  xioctl(fd, VIDIOC_STREAMOFF, &type);
  run = 0;
  pthread_join(tid, NULL);
  fclose(out);
  fprintf(stderr, "Wrote %lu frames to %s\n", frames, fname);

  sws_freeContext(to_file);
  sws_freeContext(to_preview);
  av_free(frame);
  av_free(prev_buf);
  free(preview_rgb);
}

/* ---------------------- argument validation ---------------------- */

/* the camera's supported modes; see README "Parameter settings" */
static int validate_mode(void)
{
  static const struct { int w, h, max_fps; } tiers[] = {
    { 1280,  720, 120 }, { 1920, 1080, 60 }, { 2000, 1500, 50 },
    { 3840, 2160,  20 }, { 4000, 3000, 14 }, { 8000, 6000,  3 },
  };
  static const int allowed_fps[] = { 120, 60, 50, 20, 14, 3 };

  int tier_max = 0;
  for (size_t i = 0; i < sizeof(tiers) / sizeof(*tiers); i++) {
    if (width <= tiers[i].w && height <= tiers[i].h) {
      tier_max = tiers[i].max_fps;
      break;
    }
  }
  if (!tier_max) {
    fprintf(stderr, "Unsupported resolution %dx%d\n", width, height);
    return 0;
  }
  for (size_t i = 0; i < sizeof(allowed_fps) / sizeof(*allowed_fps); i++) {
    if (fps == allowed_fps[i] && fps <= tier_max) return 1;
  }
  fprintf(stderr, "Unsupported frame rate %dfps for %dx%d (max %dfps)\n",
          fps, width, height, tier_max);
  return 0;
}

int main(int argc, char **argv)
{
  int opt;
  while ((opt = getopt(argc, argv, "w:h:r:p:d:")) != -1) {
    switch (opt) {
      case 'w': width = atoi(optarg); break;
      case 'h': height = atoi(optarg); break;
      case 'r': fps = atoi(optarg); break;
      case 'p': pixel_mode = (strcmp(optarg, "420") == 0) ? 420 : 422; break;
      case 'd': dev_name = optarg; break;
      default:
        fprintf(stderr, "Usage: %s -w width -h height -r fps "
                        "[-p 422|420] [-d device]\n", argv[0]);
        return 1;
    }
  }
  if (width <= 0 || height <= 0 || width % 2 || height % 2) {
    fprintf(stderr, "width/height must be positive and even\n");
    return 1;
  }
  if (!validate_mode()) return 1;

  /* output file named after the start time: YYYYMMDD_HHMMSS.y4m */
  char fname[64];
  time_t t0 = time(NULL);
  strftime(fname, sizeof(fname), "%Y%m%d_%H%M%S.y4m", localtime(&t0));

  struct sigaction sa;
  CLEAR(sa);
  sa.sa_handler = sigint_handler;
  sigaction(SIGINT, &sa, NULL);

  fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
  if (fd < 0) die("open");

  set_format();
  init_mmap();
  capture_loop(fname);

  for (unsigned i = 0; i < n_buffers; i++) munmap(buffers[i].start, buffers[i].length);
  close(fd);
  return 0;
}
