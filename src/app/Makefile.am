AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util -I$(srcdir)/../video \
	$(VPX_CFLAGS) $(SDL_CFLAGS)
AM_CXXFLAGS = $(PICKY_CXXFLAGS)
COMMON_LDADD = ../video/libvideo.a ../util/libutil.a \
	$(VPX_LIBS) $(SDL_LIBS) -lpthread

bin_PROGRAMS = video_sender video_receiver

# Sender: camera capture thread + VP9 encoder + UDP transport.
video_sender_SOURCES = video_sender.cc \
	common.hh common.cc protocol.hh protocol.cc \
	encoder.hh encoder.cc \
	frame_ring.hh frame_ring.cc \
	capture.hh capture.cc
video_sender_CPPFLAGS = $(AM_CPPFLAGS) $(AV_CFLAGS)
video_sender_LDADD = $(COMMON_LDADD) $(AV_LIBS)

# Receiver: UDP transport + frame reassembly + VP9 decoder + Y4M recording.
video_receiver_SOURCES = video_receiver.cc \
	common.hh common.cc protocol.hh protocol.cc \
	decoder.hh decoder.cc
video_receiver_LDADD = $(COMMON_LDADD)
