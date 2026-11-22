// Address: an IPv4 socket address (IP + port), i.e. a thin value wrapper
// around the C `sockaddr` struct that the BSD socket calls expect.
//
// Used in this project by: app/common.hh / common.cc (wait_for_config_msg()
// returns the peer's Address), app/video_sender.cc (bind to {"0", port},
// connect to the receiver) and app/video_receiver.cc (connect to the sender).
//
// Key concepts:
// - Only IPv4 is supported: the constructor asks getaddrinfo() for AF_INET,
//   and the storage is a plain `sockaddr` (16 bytes, enough for sockaddr_in
//   but NOT for an IPv6 sockaddr_in6).
// - The IP string may be a dotted quad ("192.168.1.5"), a host name that DNS
//   can resolve, or "0", which getaddrinfo() parses as 0.0.0.0 (INADDR_ANY,
//   "listen on every local interface"); that is how the sender binds.
// - Inside the sockaddr, the port and IP are stored in network byte order
//   (big-endian); use ip_port()/str() rather than reading the fields directly.

#ifndef ADDRESS_HH
#define ADDRESS_HH

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <string>
#include <utility>

class Address
{
public:
  // constructors
  // resolve 'ip' (numeric IPv4 or host name) and 'port' with getaddrinfo();
  // throws std::runtime_error if resolution fails
  Address(const std::string & ip, const uint16_t port);
  // copy a raw sockaddr, e.g. one filled in by recvfrom() or getsockname()
  Address(const sockaddr & addr, const socklen_t size);

  // accessors (what bind()/connect()/sendto() take as arguments)
  const sockaddr & sock_addr() const { return addr_; }
  socklen_t size() const { return size_; }

  // get IP and port (numeric, host byte order) via getnameinfo()
  std::pair<std::string, uint16_t> ip_port() const;
  std::string ip() const { return ip_port().first; }
  uint16_t port() const { return ip_port().second; }
  // "ip:port", handy for log messages
  std::string str() const;

  // comparison operator (byte-wise comparison of the stored sockaddr)
  bool operator==(const Address & other) const;

private:
  sockaddr addr_ {};
  socklen_t size_ {};
};

#endif /* ADDRESS_HH */
