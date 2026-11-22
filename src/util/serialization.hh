// Binary (de)serialization of integers for network messages.
//
// Used in this project by: app/protocol.cc, which builds every wire message
// (Datagram, AckMsg, ConfigMsg, ...) with put_number() and parses it with
// WireParser; tests/test_protocol.cc exercises that round trip. get_bits()
// and the unchecked get_uint*() helpers are not used by the apps.
//
// Key concept, byte order: a multi-byte integer can be stored most
// significant byte first (big-endian) or least significant byte first
// (little-endian). x86 and ARM (Jetson) CPUs are little-endian, but by
// convention network protocols are big-endian ("network byte order"). So
// every integer is converted with hton() ("host to network") before it is
// written, and with ntoh() after it is read. Example: the uint16_t 0x1234 is
// sent as the bytes 0x12 0x34 regardless of the CPU. Because both ends agree
// on this, the sender and receiver need not have the same architecture.
// (be16toh/htobe16 etc. come from glibc's <endian.h>.)

#ifndef SERIALIZATION_HH
#define SERIALIZATION_HH

#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

// convert values from network to host byte order
// (overloaded on the width; a single byte has no byte order)
inline uint8_t ntoh(uint8_t net) { return net; }
inline uint16_t ntoh(uint16_t net) { return be16toh(net); }
inline uint32_t ntoh(uint32_t net) { return be32toh(net); }
inline uint64_t ntoh(uint64_t net) { return be64toh(net); }

// convert values from host to network byte order
inline uint8_t hton(uint8_t host) { return host; }
inline uint16_t hton(uint16_t host) { return htobe16(host); }
inline uint32_t hton(uint32_t host) { return htobe32(host); }
inline uint64_t hton(uint64_t host) { return htobe64(host); }

// serialize a number in host byte order to binary data on wire
// (returns a string of exactly sizeof(T) bytes; T must be one of the
// unsigned types above, so cast enums and bools first, as protocol.cc does)
template<typename T>
std::string put_number(const T host)
{
  const T net = hton(host);
  return {reinterpret_cast<const char *>(&net), sizeof(net)};
}

// deserialize binary data received on wire to a number in host byte order
// (reads the first sizeof(T) bytes of 'net'; throws std::out_of_range if
// there are fewer)
template<typename T>
T get_number(const std::string_view net)
{
  if (sizeof(T) > net.size()) {
    throw std::out_of_range("get_number(): read past end");
  }

  // memcpy instead of a pointer cast: the bytes may not be suitably aligned
  T ret;
  memcpy(&ret, net.data(), sizeof(T));

  return ntoh(ret);
}

// similar to get_number except with *no* bounds check
// (the caller must guarantee enough bytes are available)
uint8_t get_uint8(const char * net);
uint16_t get_uint16(const char * net);
uint32_t get_uint32(const char * net);
uint64_t get_uint64(const char * net);

// get the value of a bit range in the number
// bit numbering is *MSB 0*, e.g., 0x01 is represented as:
// binary: 0 0 0 0 0 0 0 1
// index:  0 1 2 3 4 5 6 7
// e.g. get_bits<uint8_t>(0xB4 /* 1011 0100 */, 2, 3) == 0b110 == 6
// (the mask is computed with an int, so bit_len must be less than 31)
template<typename T>
T get_bits(const T number, const size_t bit_offset, const size_t bit_len)
{
  const size_t total_bits = sizeof(T) * 8;

   if (bit_offset + bit_len > total_bits) {
    throw std::out_of_range("get_bits(): read past end");
  }

  // shift the wanted bits down to the bottom, then mask off the rest
  T ret = number >> (total_bits - bit_offset - bit_len);
  ret &= (1 << bit_len) - 1;

  return ret;
}

// WireParser: reads fields one after another from a received message.
// It holds a string_view (a non-owning pointer + length) into the buffer and
// advances it past each field read, so the buffer must outlive the parser.
// Every read is bounds-checked and throws std::out_of_range on a truncated
// message; protocol.cc catches that and treats the message as invalid.
class WireParser
{
public:
  WireParser(const std::string_view str) : str_(str) {}

  // read the next big-endian integer of the given width
  uint8_t read_uint8() { return read<uint8_t>(); }
  uint16_t read_uint16() { return read<uint16_t>(); }
  uint32_t read_uint32() { return read<uint32_t>(); }
  uint64_t read_uint64() { return read<uint64_t>(); }

  // read the next 'len' bytes as a string
  std::string read_string(const size_t len);
  // read everything that is left (e.g. a datagram's payload at the end)
  std::string read_string() { return read_string(str_.size()); }

  // skip 'len' bytes ahead
  void skip(const size_t len);

private:
  std::string_view str_;

  template<typename T>
  T read()
  {
    if (sizeof(T) > str_.size()) {
      throw std::out_of_range("WireParser::read(): read past end");
    }

    T ret;
    memcpy(&ret, str_.data(), sizeof(T));

    // move the start of string view forward
    str_.remove_prefix(sizeof(T));

    return ntoh(ret);
  }
};

#endif /* SERIALIZATION_HH */
