// Implementation of Address (address.hh): name resolution with
// getaddrinfo() and conversion back to text with getnameinfo().

#include <cstring>
#include <stdexcept>

#include "address.hh"

using namespace std;

Address::Address(const std::string & ip, const uint16_t port)
{
  // hints restrict what getaddrinfo() may return: IPv4 only
  addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;

  // resolved address
  // (a linked list allocated by getaddrinfo(); only the first entry is used.
  // Note: it is never released with freeaddrinfo(), so each call leaks a
  // small allocation. Harmless here because only a few Addresses are built.)
  addrinfo * result;

  const int ret_code = getaddrinfo(ip.c_str(), to_string(port).c_str(),
                                   &hints, &result);
  if (ret_code != 0) {
    // getaddrinfo() reports errors via its return code, not errno
    throw runtime_error(gai_strerror(ret_code));
  }

  size_ = result->ai_addrlen;
  memcpy(&addr_, result->ai_addr, size_);
}

Address::Address(const sockaddr & addr, const socklen_t size)
{
  // refuse addresses that do not fit in our sockaddr storage (e.g. IPv6)
  if (size > sizeof(addr_)) {
    throw runtime_error("invalid sockaddr size");
  }

  size_ = size;
  memcpy(&addr_, &addr, size_);
}

pair<string, uint16_t> Address::ip_port() const
{
  char ip[NI_MAXHOST], port[NI_MAXSERV];

  // NI_NUMERIC*: return "10.0.0.1" and "9090" instead of doing a reverse
  // DNS lookup or a service-name lookup
  const int ret_code = getnameinfo(&addr_, size_,
                                   ip, sizeof(ip), port, sizeof(port),
                                   NI_NUMERICHOST | NI_NUMERICSERV);
  if (ret_code != 0) {
    throw runtime_error(gai_strerror(ret_code));
  }

  return {ip, stoi(port)};
}

string Address::str() const
{
  const auto & [ip, port] = ip_port();
  return ip + ":" + to_string(port);
}

bool Address::operator==(const Address & other) const
{
  return size_ == other.size_ and memcmp(&addr_, &other.addr_, size_) == 0;
}
