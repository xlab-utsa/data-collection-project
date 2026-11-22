// Implementation of timestamp.hh.

#include <chrono>
#include "timestamp.hh"

using namespace std;
using namespace chrono;

// dividing a duration by 1ns / 1us / 1ms yields a plain count of those units
uint64_t timestamp_ns()
{
  return system_clock::now().time_since_epoch() / 1ns;
}

uint64_t timestamp_us()
{
  return system_clock::now().time_since_epoch() / 1us;
}

uint64_t timestamp_ms()
{
  return system_clock::now().time_since_epoch() / 1ms;
}
