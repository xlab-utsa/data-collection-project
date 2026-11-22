// Implementation of Timerfd (timerfd.hh).

#include "timerfd.hh"
#include "exception.hh"
#include "conversion.hh"

using namespace std;

Timerfd::Timerfd(int clockid, int flags)
  : FileDescriptor(check_syscall(timerfd_create(clockid, flags)))
{}

void Timerfd::set_time(const timespec & initial_expiration,
                       const timespec & interval)
{
  // it_value = time until the first expiry, it_interval = period afterwards
  itimerspec its;
  its.it_value = initial_expiration;
  its.it_interval = interval;

  // flags 0: it_value is relative to now (not an absolute time)
  check_syscall(timerfd_settime(fd_num(), 0, &its, nullptr));
}

unsigned int Timerfd::read_expirations()
{
  // a timerfd read always yields exactly one 8-byte counter
  uint64_t num_exp = 0;

  if (check_syscall(::read(fd_num(), &num_exp, sizeof(num_exp)))
      != sizeof(num_exp)) {
    throw runtime_error("read error in timerfd");
  }

  return narrow_cast<unsigned int>(num_exp);
}
