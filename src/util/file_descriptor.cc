// Implementation of FileDescriptor (file_descriptor.hh).

#include <cstdio>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <algorithm>

#include "file_descriptor.hh"
#include "exception.hh"

using namespace std;

FileDescriptor::FileDescriptor(const int fd)
  : fd_(fd), eof_(false)
{
  // set close-on-exec flag by default to prevent fds from leaking
  // (this also validates 'fd': fcntl() fails with EBADF on a bad descriptor)
  check_syscall(fcntl(fd_, F_SETFD, FD_CLOEXEC));
}

FileDescriptor::~FileDescriptor()
{
  // don't throw from destructor
  if (fd_ >= 0 and ::close(fd_) < 0) {
    perror("failed to close file descriptor");
  }
}

FileDescriptor::FileDescriptor(FileDescriptor && other)
  : fd_(other.fd_), eof_(other.eof_)
{
  other.fd_ = -1; // mark other file descriptor as inactive
}

// Note: if *this already owns an fd, it is overwritten without being closed
// (that fd leaks). Nothing in this project relies on it: assigning to an
// empty std::optional<FileDescriptor>, as encoder.cc and decoder.cc do,
// uses the move constructor instead.
FileDescriptor & FileDescriptor::operator=(FileDescriptor && other)
{
  fd_ = other.fd_;
  eof_ = other.eof_;
  other.fd_ = -1; // mark other file descriptor as inactive

  return *this;
}

void FileDescriptor::close()
{
  if (fd_ < 0) { // has already been moved away or closed
    return;
  }

  check_syscall(::close(fd_));

  fd_ = -1;
}

// the file status flags (F_GETFL/F_SETFL) hold O_NONBLOCK; read-modify-write
// them so that other flags (e.g. O_APPEND) are preserved
bool FileDescriptor::get_blocking() const
{
  int flags = check_syscall(fcntl(fd_, F_GETFL));
  return !(flags & O_NONBLOCK);
}

void FileDescriptor::set_blocking(const bool blocking)
{
  int flags = check_syscall(fcntl(fd_, F_GETFL));

  if (blocking) {
    flags &= ~O_NONBLOCK;
  } else {
    flags |= O_NONBLOCK;
  }

  check_syscall(fcntl(fd_, F_SETFL, flags));
}

size_t FileDescriptor::write(const string_view data)
{
  if (data.empty()) {
    throw runtime_error("attempted to write empty data");
  }

  // a single write(): may be partial; the caller handles the remainder
  const ssize_t bytes_written = ::write(fd_, data.data(), data.size());

  if (bytes_written <= 0) {
    // in non-blocking mode "would block" is not an error, just "try later"
    if (bytes_written == -1 and errno == EWOULDBLOCK) {
      return 0; // return 0 to indicate EWOULDBLOCK
    }

    throw unix_error("FileDescriptor::write()");
  }

  return bytes_written;
}

void FileDescriptor::writen(const string_view data, const size_t n)
{
  if (data.empty() or n == 0) {
    throw runtime_error("attempted to write empty data");
  }

  if (data.size() < n) {
    throw runtime_error("data size is smaller than n");
  }

  const char * it = data.data();
  const char * end = it + n;

  // keep calling write() until all n bytes are out (each call may be partial)
  while (it != end) {
    const ssize_t bytes_written = ::write(fd_, it, end - it);

    if (bytes_written <= 0) {
      throw unix_error("FileDescriptor::writen()");
    }

    it += bytes_written;
  }
}

void FileDescriptor::write_all(const string_view data)
{
  writen(data, data.size());
}

string FileDescriptor::read(const size_t limit)
{
  vector<char> buf(min(MAX_BUF_SIZE, limit));

  // throws on any error, including EAGAIN in non-blocking mode
  const size_t bytes_read = check_syscall(::read(fd_, buf.data(), buf.size()));

  if (bytes_read == 0) {
    eof_ = true;
  }

  return {buf.data(), bytes_read};
}

string FileDescriptor::readn(const size_t n, const bool allow_partial_read)
{
  if (n == 0) {
    throw runtime_error("attempted to read 0 bytes");
  }

  vector<char> buf(n);
  size_t total_read = 0;

  // read() may return fewer bytes than asked for; loop until we have n
  while (total_read != n) {
    const size_t bytes_read = check_syscall(
      ::read(fd_, buf.data() + total_read, n - total_read));

    if (bytes_read == 0) {
      eof_ = true;

      if (allow_partial_read) {
        return {buf.data(), total_read};
      } else {
        throw runtime_error("FileDescriptor::readn(): unexpected EOF");
      }
    }

    total_read += bytes_read;
  }

  return {buf.data(), total_read};
}

string FileDescriptor::getline()
{
  string ret;

  // one byte at a time so that nothing past the '\n' is consumed
  while (true) {
    const string char_read = read(1);

    if (eof_ or char_read == "\n") {
      break;
    }

    ret += char_read;
  }

  return ret;
}

// Note: check_syscall() takes an int, so offsets beyond 2 GiB do not survive
// this round trip; only small files (such as Y4M test clips) should rely on it
uint64_t FileDescriptor::seek(const int64_t offset, const int whence)
{
  return check_syscall(lseek(fd_, offset, whence));
}

void FileDescriptor::reset_offset()
{
  seek(0, SEEK_SET);
  eof_ = false;
}

uint64_t FileDescriptor::file_size()
{
  // save the current offset
  const uint64_t saved_offset = seek(0, SEEK_CUR);

  // seek to the end of file to get file size
  const uint64_t ret = seek(0, SEEK_END);

  // seek back to the original offset
  seek(saved_offset, SEEK_SET);

  return ret;
}
