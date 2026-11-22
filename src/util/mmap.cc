// Implementation of MMap (mmap.hh).

#include <iostream>

#include "mmap.hh"
#include "exception.hh"

using namespace std;

MMap::MMap(const size_t length, const int prot, const int flags,
           const int fd, const off_t offset)
  : addr_(static_cast<uint8_t *>(mmap(nullptr, length, prot, flags, fd, offset))),
    length_(length)
{
  // mmap() signals failure with MAP_FAILED ((void *) -1), not nullptr
  if (addr_ == MAP_FAILED) {
    throw runtime_error("mmap failed");
  }
}

MMap::~MMap()
{
  // skip moved-from objects; never throw from a destructor
  if (addr_ and length_ > 0 and munmap(addr_, length_) != 0) {
    cerr << "munmap error" << endl;
  }
}

MMap::MMap(MMap && other)
  : addr_(other.addr_), length_(other.length_)
{
  other.addr_ = nullptr;
  other.length_ = 0;
}

// Note: like FileDescriptor's move assignment, this does not unmap a region
// *this may already hold (std::vector<MMap> in v4l2.hh only needs the move
// constructor, so this is not exercised in this project)
MMap & MMap::operator=(MMap && other)
{
  addr_ = other.addr_;
  length_ = other.length_;

  other.addr_ = nullptr;
  other.length_ = 0;

  return *this;
}
