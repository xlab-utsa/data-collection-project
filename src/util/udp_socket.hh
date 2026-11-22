// UDPSocket: a UDP (SOCK_DGRAM) socket over IPv4. This is the only network
// transport in the project.
//
// Used in this project by: app/video_sender.cc (bind, wait for the
// receiver's request, connect, then send datagrams and receive ACKs),
// app/video_receiver.cc (connect to the sender, send requests/ACKs, receive
// datagrams) and app/common.cc (wait_for_config_msg() uses recvfrom()).
// tests/test_handshake.cc runs the handshake over loopback.
//
// Key concepts:
// - Datagrams: each send() is one packet that arrives whole, or not at all;
//   UDP adds no retransmission or ordering (the app's ACKs do that job).
//   That is why send() returns bool rather than a byte count.
// - Why EWOULDBLOCK gives false/nullopt instead of an exception: the apps
//   run their sockets in non-blocking mode inside a Poller loop. "The send
//   buffer is full" or "no datagram has arrived yet" are normal, expected
//   situations there, not errors; the caller just stops and waits for the
//   next Poller event. Real errors (e.g. ECONNREFUSED) still throw
//   unix_error.

#ifndef UDP_SOCKET_HH
#define UDP_SOCKET_HH

#include <string>
#include <string_view>
#include <utility>
#include <optional>

#include "socket.hh"
#include "address.hh"

class UDPSocket : public Socket
{
public:
  // constructor
  UDPSocket() : Socket(AF_INET, SOCK_DGRAM) {};

  // return true if data is sent in its entirety
  // return false to indicate EWOULDBLOCK in nonblocking I/O mode
  // (send() needs a prior connect(); sendto() sends to 'dst_addr'. Throws on
  // empty data and on any other error.)
  bool send(const std::string_view data);
  bool sendto(const Address & dst_addr, const std::string_view data);

  // receive a datagram (*supposedly* from a connected address)
  // return nullopt to indicate EWOULDBLOCK in nonblocking I/O mode
  // (after connect(), the kernel only delivers datagrams from the peer)
  std::optional<std::string> recv();

  // receive a datagram and its source address
  // (on EWOULDBLOCK the second element is nullopt and the Address is
  // meaningless, so check the payload first)
  std::pair<Address, std::optional<std::string>> recvfrom();

private:
  // turn the raw return value of send()/sendto() into true/false/exception
  bool check_bytes_sent(const ssize_t bytes_sent, const size_t target) const;
  // turn the raw return value of recv()/recvfrom() into true/false/exception
  bool check_bytes_received(const ssize_t bytes_received) const;

  // receive buffer size; no UDP datagram can be larger (the maximum IPv4
  // UDP payload is 65507 bytes). Not to be confused with the network MTU
  // (~1500 bytes) that the app's Datagram::set_mtu() is about.
  static constexpr size_t UDP_MTU = 65536; // bytes
};

#endif /* UDP_SOCKET_HH */
