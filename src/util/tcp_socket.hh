// TCPSocket: a TCP (SOCK_STREAM) socket over IPv4.
//
// Currently UNUSED by the apps: all traffic in this project is UDP (see
// udp_socket.hh). It is kept from the Ringmaster code base.
//
// Key concept, stream vs datagram: TCP delivers a reliable, ordered *byte
// stream* with no message boundaries, so one send() of 100 bytes may arrive
// as two recv()s of 60 and 40 bytes; that is why send() may be partial and
// the sendn/recvn helpers exist. UDP instead delivers whole datagrams, or
// nothing.
//
// Typical server use: bind(), listen(), then accept() returns a new
// TCPSocket for each client. Typical client use: connect(), then send/recv.

#ifndef TCP_SOCKET_HH
#define TCP_SOCKET_HH

#include <string>
#include <string_view>

#include "socket.hh"

class TCPSocket : public Socket
{
public:
  // constructor
  TCPSocket() : Socket(AF_INET, SOCK_STREAM) {};

  // listen for incoming connections
  // (backlog = how many not-yet-accepted connections the kernel may queue)
  void listen(const int backlog = 16);

  // accept a new incoming connection
  // (blocks until a client connects, unless the socket is non-blocking)
  TCPSocket accept();

  // return the actual bytes sent; 0 indicates EWOULDBLOCK
  inline size_t send(const std::string_view data) { return write(data); }

  // receive and return data up to 'limit' bytes; empty string indicates EOF
  // (EOF here means the peer closed the connection)
  inline std::string recv(const size_t limit = MAX_BUF_SIZE) { return read(limit); }

  // blocking I/O mode only: send exactly N bytes or all of data
  inline void sendn(const std::string_view data, const size_t n) { writen(data, n); }
  inline void send_all(const std::string_view data) { write_all(data); }

  // blocking I/O mode only: *attempt* to receive N bytes of data
  // (with the default allow_partial_read = true, returns fewer than N bytes
  // if the peer closes the connection first)
  inline std::string recvn(const size_t n, const bool allow_partial_read = true)
    { return readn(n, allow_partial_read); }

private:
  // internal constructor from file descriptor (used by accept())
  TCPSocket(FileDescriptor && fd)
    : Socket(std::move(fd), AF_INET, SOCK_STREAM) {};
};

#endif /* TCP_SOCKET_HH */
