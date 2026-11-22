// Implementation of the non-template parts of serialization.hh.

#include "serialization.hh"

using namespace std;

// These dereference the pointer directly, which assumes the platform allows
// unaligned loads (true on x86-64 and 64-bit ARM). get_number() and
// WireParser use memcpy and have no such assumption.
uint8_t get_uint8(const char * data)
{
  return *reinterpret_cast<const uint8_t *>(data);
}

uint16_t get_uint16(const char * data)
{
  return be16toh(*reinterpret_cast<const uint16_t *>(data));
}

uint32_t get_uint32(const char * data)
{
  return be32toh(*reinterpret_cast<const uint32_t *>(data));
}

uint64_t get_uint64(const char * data)
{
  return be64toh(*reinterpret_cast<const uint64_t *>(data));
}

string WireParser::read_string(const size_t len)
{
  if (len > str_.size()) {
    throw out_of_range("WireParser::read_string(): attempted to read past end");
  }

  // copies the bytes, so the returned string stays valid after the buffer
  string ret { str_.data(), len };

  // move the start of string view forward
  str_.remove_prefix(len);

  return ret;
}

void WireParser::skip(const size_t len)
{
  if (len > str_.size()) {
    throw out_of_range("WireParser::skip(): attempted to skip past end");
  }

  str_.remove_prefix(len);
}
