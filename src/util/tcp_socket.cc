// Implementation of TCPSocket (tcp_socket.hh). Unused by the apps.

#include "tcp_socket.hh"
#include "exception.hh"

using namespace std;

void TCPSocket::listen(const int backlog)
{
  check_syscall(::listen(fd_num(), backlog));
}

// ::accept() returns a new fd for the connection (the listening socket keeps
// listening). The braces turn that int into a FileDescriptor and then into a
// TCPSocket via the private constructor, which verifies it is a TCP socket.
TCPSocket TCPSocket::accept()
{
  return { check_syscall(::accept(fd_num(), nullptr, nullptr)) };
}
