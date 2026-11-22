// Timerfd: a Linux timer that is also a file descriptor (timerfd_create(2)).
// When the timer expires, the fd becomes readable, so a timer can be watched
// by the same Poller as the sockets and no separate thread or signal handler
// is needed.
//
// Used in this project by: app/video_sender.cc, for the frame timer (one
// tick per 1/fps seconds: grab a frame from the ring and encode it) and the
// once-a-second stats timer.
//
// Key concepts:
// - set_time(initial, interval): first expiry after 'initial', then every
//   'interval' (periodic). A zero interval makes a one-shot timer; a zero
//   'initial' disarms the timer.
// - Expirations: reading the fd returns how many times the timer has expired
//   since the last read, and resets that count to 0. More than 1 means the
//   event loop fell behind (the sender warns about this). You MUST read the
//   fd in the callback; otherwise it stays readable and poll() keeps firing.
// - CLOCK_MONOTONIC (the default) is not affected by wall-clock changes such
//   as NTP adjustments, which is what you want for a frame rate.

#ifndef TIMERFD_HH
#define TIMERFD_HH

#include <time.h>
#include <sys/timerfd.h>

#include "file_descriptor.hh"

class Timerfd : public FileDescriptor
{
public:
  // the timer starts disarmed; call set_time() to start it. TFD_NONBLOCK
  // makes reads return EAGAIN instead of sleeping when nothing has expired.
  Timerfd(int clockid = CLOCK_MONOTONIC, int flags = TFD_NONBLOCK);

  // arm (or re-arm) the timer; both values are relative to now
  void set_time(const timespec & initial_expiration,
                const timespec & interval);

  // number of expirations since the last call. Call it when the fd is
  // readable: with TFD_NONBLOCK and nothing expired, the read fails with
  // EAGAIN and this throws unix_error.
  unsigned int read_expirations();
};

#endif /* TIMERFD_HH */
