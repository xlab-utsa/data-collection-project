# libutil.a: OS/network helpers shared by src/video and src/app
# (file descriptors, sockets, event loops, timers, serialization).
# epoller, tcp_socket and split are built but not used by the apps; mmap is
# used only by the webcam tool (via src/video/v4l2.cc).
AM_CPPFLAGS = $(CXX17_FLAGS)
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

noinst_LIBRARIES = libutil.a

libutil_a_SOURCES = \
	exception.hh \
	conversion.hh conversion.cc \
	split.hh split.cc \
	mmap.hh mmap.cc \
	timestamp.hh timestamp.cc \
	timerfd.hh timerfd.cc \
	address.hh address.cc \
	serialization.hh serialization.cc \
	poller.hh poller.cc \
	epoller.hh epoller.cc \
	file_descriptor.hh file_descriptor.cc \
	socket.hh socket.cc \
	udp_socket.hh udp_socket.cc \
	tcp_socket.hh tcp_socket.cc
