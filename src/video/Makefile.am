AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util $(VPX_CFLAGS) $(SDL_CFLAGS)
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

noinst_LIBRARIES = libvideo.a

libvideo_a_SOURCES = \
	image.hh image.cc \
	video_input.hh \
	yuv4mpeg.hh yuv4mpeg.cc \
	v4l2.hh v4l2.cc \
	sdl.hh sdl.cc

# Small webcam viewer (V4L2 -> SDL), handy for checking a camera works.
bin_PROGRAMS = webcam

webcam_SOURCES = webcam.cc
webcam_LDADD = libvideo.a ../util/libutil.a $(VPX_LIBS) $(SDL_LIBS) -lpthread
