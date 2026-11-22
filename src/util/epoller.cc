// Implementation of Epoller (epoller.hh). Unused by the apps; see the header
// for how it differs from Poller.

#include <cstdio>
#include <iostream>

#include "epoller.hh"
#include "exception.hh"

using namespace std;

// EPOLL_CLOEXEC: do not leak the epoll fd into child processes (exec)
Epoller::Epoller()
  : epfd_(check_syscall(epoll_create1(EPOLL_CLOEXEC)))
{}

Epoller::~Epoller()
{
  // don't throw from destructor
  if (close(epfd_) < 0) {
    perror("failed to close epoll instance");
  }
}

void Epoller::register_event(const int fd,
                             const Flag flag,
                             const Callback callback)
{
  if (not roster_.count(fd)) { // fd is not registered yet
    roster_[fd][flag] = callback;
    active_events_[fd] = flag;
    epoll_add(fd, flag);
  }
  else { // fd is registered but flag should not be registered yet
    if (roster_[fd].count(flag)) {
      throw runtime_error("attempted to register the same event");
    }

    roster_[fd][flag] = callback;
    active_events_[fd] |= flag;
    epoll_mod(fd, active_events_[fd]);
  }
}

void Epoller::register_event(const FileDescriptor & fd,
                             const Flag flag,
                             const Callback callback)
{
  register_event(fd.fd_num(), flag, callback);
}

// unlike Poller, every change must be pushed to the kernel with epoll_ctl(),
// so activate/deactivate only make the syscall when the bitmask changes
void Epoller::activate(const int fd, const Flag flag)
{
  uint32_t & active_events = active_events_.at(fd);

  // activate only if not activated yet
  if (not (active_events & flag)) {
    active_events |= flag;
    epoll_mod(fd, active_events);
  }
}

void Epoller::activate(const FileDescriptor & fd, const Flag flag)
{
  activate(fd.fd_num(), flag);
}

void Epoller::deactivate(const int fd, const Flag flag)
{
  uint32_t & active_events = active_events_.at(fd);

  // deactivate only if activated before
  if (active_events & flag) {
    active_events &= ~flag;
    epoll_mod(fd, active_events);
  }
}

void Epoller::deactivate(const FileDescriptor & fd, const Flag flag)
{
  deactivate(fd.fd_num(), flag);
}

// Deregistration is deferred because a callback may ask to deregister an fd
// while poll() is iterating over roster_; erasing right away would invalidate
// that iteration.
void Epoller::deregister(const int fd)
{
  fds_to_deregister_.emplace(fd);
}

void Epoller::deregister(const FileDescriptor & fd)
{
  deregister(fd.fd_num());
}

void Epoller::do_deregister()
{
  for (const int fd : fds_to_deregister_) {
    roster_.erase(fd);
    active_events_.erase(fd);
    // throws (EBADF) if the fd was already closed: deregister before closing
    check_syscall(epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr));
  }

  fds_to_deregister_.clear();
}

void Epoller::poll(const int timeout_ms)
{
  // at most this many ready fds are handled per call; any others stay ready
  // (level-triggered) and are returned by the next call.
  // Note: 'static' means the buffer is shared by all Epollers, so poll() must
  // not be called from two threads at once.
  static constexpr size_t MAX_EVENTS = 32;
  static epoll_event event_list[MAX_EVENTS];

  // first, deregister the fds that have been scheduled to deregister
  do_deregister();

  // returns the number of ready fds (0 on timeout). Unlike Poller::poll(),
  // EINTR (interrupted by a signal) is not special-cased and will throw.
  int nfds = check_syscall(
    epoll_wait(epfd_, event_list, MAX_EVENTS, timeout_ms));

  for (int i = 0; i < nfds; i++) {
    int fd = event_list[i].data.fd;
    uint32_t revents = event_list[i].events;

    // run the callback of every registered flag the kernel reported
    // (EPOLLERR/EPOLLHUP are not dispatched to any callback)
    for (const auto & [flag, callback] : roster_.at(fd)) {
      if (revents & flag) {
        callback(); // execute the callback function
      }
    }
  }
}

void Epoller::epoll_add(const int fd, const uint32_t events)
{
  // data.fd is handed back to us by epoll_wait() to identify the ready fd
  epoll_event ev;
  ev.data.fd = fd;
  ev.events = events;

  check_syscall(epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev));
}

void Epoller::epoll_mod(const int fd, const uint32_t events)
{
  epoll_event ev;
  ev.data.fd = fd;
  ev.events = events;

  check_syscall(epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev));
}
