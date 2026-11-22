// Socket: common base class of UDPSocket (udp_socket.hh) and TCPSocket
// (tcp_socket.hh). It is a FileDescriptor (a socket is just an fd) plus the
// operations every socket type shares: bind, connect, address queries and
// socket options.
//
// Used in this project through UDPSocket, by app/video_sender.cc,
// app/video_receiver.cc and app/common.cc. It is never used directly; the
// constructors are protected so only the subclasses can create one.
//
// Key concepts:
// - bind(addr) chooses the *local* address/port this socket receives on. The
//   sender binds to {"0", port} so it can be reached on that port on any
//   interface.
// - connect(addr) on a UDP socket sends no packets. It only records a
//   default peer: afterwards send()/recv() can be used without an address,
//   datagrams from other hosts are filtered out, and ICMP errors from the
//   peer (e.g. "port unreachable") are reported as ECONNREFUSED on a later
//   call (see wait_for_config_msg() in app/common.cc).

#ifndef SOCKET_HH
#define SOCKET_HH

#include <sys/types.h>
#include <sys/socket.h>

#include "address.hh"
#include "file_descriptor.hh"

class Socket : public FileDescriptor
{
protected:
  // constructors
  // create a new socket, e.g. (AF_INET, SOCK_DGRAM) for UDP over IPv4
  Socket(const int domain, const int type);
  // wrap an existing socket fd (e.g. from accept()), checking that its
  // domain and type are what the subclass expects
  Socket(FileDescriptor && fd, const int domain, const int type);

  // manipulate socket options
  // (thin wrappers over getsockopt(2)/setsockopt(2). They are defined in
  // socket.cc, so they can only be instantiated there; to add a new option,
  // add a member function to Socket in socket.cc as set_reuseaddr() does.)
  template<typename OptionType>
  socklen_t getsockopt(const int level, const int option_name,
                       OptionType & option_value) const;

  template<typename OptionType>
  void setsockopt(const int level, const int option_name,
                  const OptionType & option_value);

public:
  // bind socket to a local address
  void bind(const Address & local_addr);

  // connect socket to a peer address
  void connect(const Address & peer_addr);

  // get local and peer addresses
  // (peer_address() throws ENOTCONN if the socket is not connected)
  Address local_address() const;
  Address peer_address() const;

  // allow local address to be reused sooner
  // (SO_REUSEADDR; mainly matters for TCP servers that restart quickly)
  void set_reuseaddr();
};

#endif /* SOCKET_HH */
