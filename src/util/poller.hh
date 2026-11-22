// Poller: a single-threaded event loop built on poll(2). Register a callback
// for "fd is readable" (In) or "fd is writable" (Out); each call to poll()
// sleeps until at least one active event is ready (or the timeout expires)
// and then runs the matching callbacks.
//
// Used in this project by: app/video_sender.cc (frame timer, stats timer,
// UDP socket In for ACKs/config, UDP socket Out to drain the send buffer),
// app/video_receiver.cc (UDP socket In for datagrams) and video/webcam.cc.
//
// Key concepts:
// - Register once, then toggle. A registered event can be deactivated and
//   activated cheaply. The sender keeps Out on the UDP socket deactivated
//   while it has nothing to send; otherwise poll() would return immediately
//   on every call (a UDP socket is almost always writable) and the loop would
//   spin at 100% CPU.
// - Callbacks must not block: use non-blocking fds and treat EWOULDBLOCK
//   (UDPSocket::send() returning false, recv() returning nullopt) as "try
//   again on a later event".
// - Level-triggered: an fd that is still readable after its callback runs is
//   reported again by the next poll(), so a callback may handle just one item.
// - See epoller.hh for the epoll(7) variant and why poll is enough here.

#ifndef POLLER_HH
#define POLLER_HH

#include <poll.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>

#include "file_descriptor.hh"

class Poller
{
public:
  // type definitions
  enum Flag : short {
    In = POLLIN,
    Out = POLLOUT
  };

  using Callback = std::function<void()>;

  // register a single event (flag) on fd to monitor with a callback function
  // (the event starts out active; an fd may have both an In and an Out
  // callback, but registering the same flag twice throws)
  void register_event(const int fd,
                      const Flag flag,
                      const Callback callback);
  void register_event(const FileDescriptor & fd,
                      const Flag flag,
                      const Callback callback);

  // activate an event on fd (safe to be called repeatedly)
  // (throws std::out_of_range if fd was never registered)
  void activate(const int fd, const Flag flag);
  void activate(const FileDescriptor & fd, const Flag flag);

  // deactivate an event on fd (safe to be called repeatedly)
  // (the callback is kept; poll() simply stops watching for this event)
  void deactivate(const int fd, const Flag flag);
  void deactivate(const FileDescriptor & fd, const Flag flag);

  // deregister a fd from the interest list
  // (deferred to the start of the next poll(), so it is safe to call from
  // inside a callback)
  void deregister(const int fd);
  void deregister(const FileDescriptor & fd);

  // execute the callbacks on the ready fds; returns without running any
  // callback if the wait is interrupted by a signal (EINTR)
  // (timeout_ms = -1 waits forever, 0 returns immediately)
  void poll(const int timeout_ms = -1);

private:
  // *actually* deregister fds in fds_to_deregister_
  void do_deregister();

  // fd -> {flag (bit value) -> callback}
  std::unordered_map<int, std::unordered_map<Flag, Callback>> roster_ {};

  // fd -> currently active events (bitmask)
  std::unordered_map<int, short> active_events_ {};

  // fds scheduled to deregister
  std::unordered_set<int> fds_to_deregister_ {};
};

#endif /* POLLER_HH */
