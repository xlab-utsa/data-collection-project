// Safe conversions between strings and numbers, and between integer types.
//
// Used in this project by: app/video_sender.cc and app/video_receiver.cc
// (parsing command-line options with strict_stoi + narrow_cast),
// app/encoder.cc and app/decoder.cc (double_to_string for stats output,
// narrow_cast for fragment counts), util/timerfd.cc, and video/webcam.cc.
//
// Key concept: "strict" means the whole string must be a number. Plain
// std::stoi("30fps") happily returns 30; strict_stoi("30fps") throws. Likewise
// narrow_cast throws instead of silently wrapping when a value does not fit
// in the smaller type (e.g. narrow_cast<uint16_t>(70000) throws), so a typo on
// the command line fails loudly instead of producing a strange resolution.

#ifndef CONVERSION_HH
#define CONVERSION_HH

#include <stdexcept>
#include <string>
#include <type_traits>
#include <iomanip>
#include <sstream>

// parse an integer; throws if 'str' has trailing characters after the number
// (std::invalid_argument / std::out_of_range from std::stoi also propagate)
int strict_stoi(const std::string & str, const int base = 10);
// same for long long (see the note in conversion.cc about its return value)
long long strict_stoll(const std::string & str, const int base = 10);
// format with a fixed number of digits after the decimal point, e.g. "3.14"
std::string double_to_string(const double input, const int precision = 2);

// cast two integral types: Source to Target, and assert no precision is lost
// (the check is a round trip: cast to Target and back, and compare; this
// also catches most sign changes, e.g. narrow_cast<uint8_t>(-1) throws)
template<typename Target, typename Source>
Target narrow_cast(const Source & s)
{
  static_assert(std::is_integral<Source>::value, "Source: integral required");
  static_assert(std::is_integral<Target>::value, "Target: integral required");

  Target t = static_cast<Target>(s);

  if (static_cast<Source>(t) != s) {
    throw std::runtime_error("narrow_cast: " + std::to_string(s)
                             + " != " + std::to_string(t));
  }

  return t;
}

#endif /* CONVERSION_HH */
