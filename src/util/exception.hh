// Error-checking helpers that turn C-style error returns into C++ exceptions.
//
// Used in this project by: almost every file. In src/app: common.cc
// (sigaction, poll errors, catching ECONNREFUSED), capture.cc (V4L2 ioctls,
// mmap), encoder.cc / encoder.hh and decoder.cc (check_call on libvpx return
// codes). In src/util and src/video: every wrapper around a syscall.
//
// Key concepts:
// - Linux syscalls return -1 on failure and store the reason in the global
//   'errno'. check_syscall(ret) returns 'ret' unchanged when it is >= 0 and
//   throws unix_error otherwise, so code reads as
//     const int fd = check_syscall(open(path, O_RDONLY));
// - unix_error is a std::system_error: e.what() includes the strerror()
//   text (e.g. "Connection refused") and e.code().value() is the errno value,
//   which callers can test (see the ECONNREFUSED check in app/common.cc).
//   errno is read when the exception is constructed, so throw immediately
//   after the failing call, before anything else can overwrite errno.
// - check_call(actual, expected, msg) is for libraries that return an error
//   code instead of -1/errno, e.g. libvpx's VPX_CODEC_OK.

#ifndef EXCEPTION_HH
#define EXCEPTION_HH

#include <system_error>
#include <stdexcept>

// exception carrying the current errno; 'tag' says which call failed
class unix_error : public std::system_error
{
public:
  unix_error()
    : system_error(errno, std::system_category()) {}

  unix_error(const std::string & tag)
    : system_error(errno, std::system_category(), tag) {}
};

// return 'return_value' if it is non-negative, else throw unix_error
// (note: the parameter is an int, so 64-bit results such as lseek()'s off_t
// or read()'s ssize_t are converted to int on the way in)
inline int check_syscall(const int return_value)
{
  if (return_value >= 0) {
    return return_value;
  }
  throw unix_error();
}

// same, with a tag naming the call in the error message
inline int check_syscall(const int return_value, const std::string & tag)
{
  if (return_value >= 0) {
    return return_value;
  }
  throw unix_error(tag);
}

// throw std::runtime_error(error_msg) unless actual_return == expected_return
template<typename T>
inline void check_call(const T & actual_return, const T & expected_return,
                       const std::string & error_msg = "check_call")
{
  if (actual_return != expected_return) {
    throw std::runtime_error(error_msg);
  }
}

#endif /* EXCEPTION_HH */
