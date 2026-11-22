// Epoller: an event loop built on Linux epoll(7). It has the same interface
// as Poller (poller.hh): register a callback for "fd is readable" (In) or
// "fd is writable" (Out), then call poll() in a loop to run the callbacks of
// the fds that are ready.
//
// Currently UNUSED by the apps (src/app uses Poller); it is kept from the
// Ringmaster code base as an alternative.
//
// Key concept, epoll vs poll: with poll(2) the whole list of fds is passed to
// the kernel on every call (O(number of fds) per call). With epoll the kernel
// keeps the interest list itself: fds are added/changed once with
// epoll_ctl(), and epoll_wait() only returns the ready ones. That scales to
// thousands of fds; for the two or three fds this project watches, poll is
// just as fast and simpler, which is why the apps use Poller.
// Events here are level-triggered (EPOLLET is not used): an fd keeps being
// reported as long as it stays readable/writable, exactly like poll.

#ifndef EPOLLER_HH
#define EPOLLER_HH

#include <sys/epoll.h>

#include <unordered_map>
#include <unordered_set>
#include <functional>

#include "file_descriptor.hh"

class Epoller
{
public:
  // type definitions
  enum Flag : uint32_t {
    In = EPOLLIN,
    Out = EPOLLOUT
  };

  using Callback = std::function<void()>;

  // create the epoll instance (epoll_create1); the destructor closes it
  Epoller();
  ~Epoller();

  // accessors
  int epfd() const { return epfd_; }

  // register a single event (flag) on fd to monitor with a callback function
  // (the event starts out active; throws if the same flag is registered twice)
  void register_event(const int fd,
                      const Flag flag,
                      const Callback callback);
  void register_event(const FileDescriptor & fd,
                      const Flag flag,
                      const Callback callback);

  // activate an event on fd (safe to be called repeatedly)
  void activate(const int fd, const Flag flag);
  void activate(const FileDescriptor & fd, const Flag flag);

  // deactivate an event on fd (safe to be called repeatedly)
  // (the callback stays registered; it just will not fire until reactivated)
  void deactivate(const int fd, const Flag flag);
  void deactivate(const FileDescriptor & fd, const Flag flag);

  // deregister a fd from the interest list
  // (deferred: takes effect at the start of the next poll(), so it is safe to
  // call from inside a callback; must be called before the fd is closed)
  void deregister(const int fd);
  void deregister(const FileDescriptor & fd);

  // execute the callbacks on the ready fds
  // (timeout_ms = -1 waits forever, 0 returns immediately)
  void poll(const int timeout_ms = -1);

private:
  // add fd to the interest list and monitor provided events
  void epoll_add(const int fd, const uint32_t events);

  // modify the events setting to 'events' for fd
  void epoll_mod(const int fd, const uint32_t events);

  // *actually* deregister fds in fds_to_deregister_
  void do_deregister();

  // data members
  int epfd_;

  // fd -> {flag (bit value) -> callback}
  std::unordered_map<int, std::unordered_map<Flag, Callback>> roster_ {};

  // fd -> currently active events (bitmask)
  std::unordered_map<int, uint32_t> active_events_ {};

  // fds scheduled to deregister
  std::unordered_set<int> fds_to_deregister_ {};
};

#endif /* EPOLLER_HH */
