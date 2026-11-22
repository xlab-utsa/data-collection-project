// FileDescriptor: RAII owner of a Unix file descriptor (an int handle the
// kernel gives out for files, sockets, timers, devices, ...), plus simple
// read/write helpers.
//
// Used in this project by: app/encoder.hh and app/decoder.hh (the optional
// per-frame log file, written with write()), and as the base class of
// Socket/UDPSocket (socket.hh) and Timerfd (timerfd.hh), which the sender and
// receiver event loops register with Poller. In src/video, YUV4MPEG and
// VideoDevice hold one each.
//
// Key concepts:
// - RAII ownership: the object owns the fd and close()s it in its destructor,
//   so fds cannot leak on early returns or exceptions. It can be moved (the
//   source is left holding -1, which means "owns nothing") but not copied,
//   because two owners would close the same fd twice.
// - Every fd is created with FD_CLOEXEC, so it is not inherited by programs
//   started with exec().
// - Blocking vs non-blocking: by default read()/write() sleep until they can
//   make progress. After set_blocking(false) they return immediately and fail
//   with EAGAIN/EWOULDBLOCK when nothing can be done right now; that is what
//   an event loop (Poller) needs. The helpers marked "blocking I/O only"
//   below assume blocking mode and throw on EWOULDBLOCK.
// - eof() becomes true after a read returns 0 bytes (end of file, or the peer
//   closed a stream socket).

#ifndef FILE_DESCRIPTOR_HH
#define FILE_DESCRIPTOR_HH

// headers for commonly used system calls: open, close, etc.
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>

#include <string>
#include <string_view>

class FileDescriptor
{
public:
  // take ownership of an already-open fd (e.g. the result of open());
  // throws if 'fd' is not a valid descriptor
  FileDescriptor(const int fd);
  virtual ~FileDescriptor();

  // move constructor and assignment
  FileDescriptor(FileDescriptor && other);
  FileDescriptor & operator=(FileDescriptor && other);

  // forbid copying or assigning
  FileDescriptor(const FileDescriptor & other) = delete;
  const FileDescriptor & operator=(const FileDescriptor & other) = delete;

  // accessors
  // (fd_num() is the raw int, e.g. for ioctl() or Poller::register_event)
  int fd_num() const { return fd_; }
  bool eof() const { return eof_; }

  // close throws exception on failure
  // (closing early is optional; the destructor closes the fd otherwise)
  void close();

  // I/O mode
  // (toggles O_NONBLOCK; see the header comment)
  bool get_blocking() const;
  void set_blocking(const bool blocking);

  // return the actual bytes written; 0 indicates EWOULDBLOCK
  // (may write fewer bytes than data.size(); throws on empty data)
  size_t write(const std::string_view data);

  // read and return data up to 'limit' bytes; empty string indicates EOF
  // (limit is capped at MAX_BUF_SIZE; in non-blocking mode, "no data yet"
  // throws unix_error with EAGAIN rather than returning)
  std::string read(const size_t limit = MAX_BUF_SIZE);

  // blocking I/O only: write exactly N bytes of data
  void writen(const std::string_view data, const size_t n);

  // blocking I/O only: write all the data
  void write_all(const std::string_view data);

  // blocking I/O only: read exactly N bytes of data
  // (on EOF: returns the bytes read so far if allow_partial_read, else throws)
  std::string readn(const size_t n, const bool allow_partial_read = false);

  // blocking I/O only: read one line of data
  // (the '\n' is not included; reads one byte per syscall, fine for short
  // header lines such as those in Y4M files)
  std::string getline();

  // manipulate the file offset
  // (whence is SEEK_SET, SEEK_CUR or SEEK_END as for lseek(); returns the new
  // offset. Only meaningful for regular files, not sockets or devices)
  uint64_t seek(const int64_t offset, const int whence);
  void reset_offset();  // reset file offset to beginning and set EOF to false
  uint64_t file_size(); // get file size without modifying the file offset

protected:
  static constexpr size_t MAX_BUF_SIZE = 1024 * 1024; // 1 MB

private:
  // fd_ is -1 once closed or moved from
  int fd_;
  bool eof_;
};

#endif /* FILE_DESCRIPTOR_HH */
