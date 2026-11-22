// Implementation of Poller (poller.hh). The registered state lives entirely
// in user space; the pollfd array handed to the kernel is rebuilt from
// active_events_ on every poll() call.

#include <cassert>
#include <cerrno>
#include <iostream>

#include "poller.hh"
#include "exception.hh"

using namespace std;

void Poller::register_event(const int fd,
                            const Flag flag,
                            const Callback callback)
{
  if (not roster_.count(fd)) { // fd is not registered yet
    roster_[fd][flag] = callback;
    active_events_[fd] = flag;
  }
  else { // fd is registered but flag should not be registered yet
    if (roster_[fd].count(flag)) {
      throw runtime_error("attempted to register the same event");
    }

    roster_[fd][flag] = callback;
    active_events_[fd] |= flag;
  }
}

void Poller::register_event(const FileDescriptor & fd,
                            const Flag flag,
                            const Callback callback)
{
  register_event(fd.fd_num(), flag, callback);
}

// activate/deactivate just flip a bit; the change is picked up by the next
// poll() call, so they cost no syscall
void Poller::activate(const int fd, const Flag flag)
{
  active_events_.at(fd) |= flag;
}

void Poller::activate(const FileDescriptor & fd, const Flag flag)
{
  activate(fd.fd_num(), flag);
}

void Poller::deactivate(const int fd, const Flag flag)
{
  active_events_.at(fd) &= ~flag;
}

void Poller::deactivate(const FileDescriptor & fd, const Flag flag)
{
  deactivate(fd.fd_num(), flag);
}

// Deregistration is deferred because a callback may deregister an fd while
// poll() is iterating over roster_; erasing right away would invalidate it.
void Poller::deregister(const int fd)
{
  fds_to_deregister_.emplace(fd);
}

void Poller::deregister(const FileDescriptor & fd)
{
  deregister(fd.fd_num());
}

void Poller::do_deregister()
{
  for (const int fd : fds_to_deregister_) {
    roster_.erase(fd);
    active_events_.erase(fd);
  }

  fds_to_deregister_.clear();
}

void Poller::poll(const int timeout_ms)
{
  // first, deregister the fds that have been scheduled to deregister
  do_deregister();

  // construct a list of fds to poll
  // (fds whose events are all deactivated are left out entirely; if every fd
  // is inactive and timeout_ms is -1, ::poll() sleeps until a signal)
  vector<pollfd> fds_to_poll;
  for (const auto [fd, events] : active_events_) {
    if (events != 0) {
      fds_to_poll.push_back({fd, events, 0});
    }
  }

  // ::poll() fills in each entry's 'revents' with the events that are ready
  if (::poll(fds_to_poll.data(), fds_to_poll.size(), timeout_ms) < 0) {
    if (errno == EINTR) {
      return; // interrupted by a signal: let the caller re-check its state
    }
    throw unix_error("poll");
  }

  // run the callback of every registered flag that is ready. Only POLLIN and
  // POLLOUT have callbacks; POLLERR/POLLHUP/POLLNVAL in revents are ignored.
  for (const auto & it : fds_to_poll) {
    for (const auto & [flag, callback] : roster_.at(it.fd)) {
      if (it.revents & flag) {
        assert(it.events & flag); // returned event must have been requested
        callback(); // execute the callback function
      }
    }
  }
}
