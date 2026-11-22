// MMap: RAII owner of a memory mapping created with mmap(2), unmapped with
// munmap(2) in the destructor.
//
// Used in this project by: video/v4l2.cc (VideoDevice maps the camera's
// kernel capture buffers), i.e. only by the `webcam` tool. The sender's
// capture thread (app/capture.cc) calls mmap()/munmap() directly instead.
//
// Key concept: mmap() makes a region of a file or device appear as ordinary
// memory in our address space. For a V4L2 camera this means the driver
// writes each frame straight into memory we can read, with no copy through
// read(). Like FileDescriptor, an MMap can be moved but not copied, so the
// region is unmapped exactly once; this also lets MMaps live in a
// std::vector.

#ifndef MMAP_HH
#define MMAP_HH

#include <sys/mman.h>
#include <cstdint>

class MMap
{
public:
  // same arguments as mmap(2) (the start address is chosen by the kernel);
  // throws std::runtime_error if the mapping fails
  MMap(const size_t length, const int prot, const int flags,
       const int fd, const off_t offset);
  ~MMap();

  // move constructor and assignment
  MMap(MMap && other);
  MMap & operator=(MMap && other);

  // forbid copying or assigning
  MMap(const MMap & other) = delete;
  const MMap & operator=(const MMap & other) = delete;

  // accessors
  uint8_t * addr() const { return addr_; }
  size_t length() const { return length_; }

private:
  // addr_ is nullptr once moved from
  uint8_t * addr_;
  size_t length_;
};

#endif /* MMAP_HH */
