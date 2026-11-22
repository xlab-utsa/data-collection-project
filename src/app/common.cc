#include <poll.h>
#include <signal.h>

#include <cerrno>
#include <cstring>
#include <memory>

#include "common.hh"
#include "exception.hh"

using namespace std;

atomic<bool> g_stop_requested {false};

namespace {

void stop_signal_handler(int)
{
  g_stop_requested = true;
}

} // namespace

void install_stop_signal_handlers()
{
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = stop_signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0; // deliberately no SA_RESTART

  check_syscall(sigaction(SIGINT, &sa, nullptr), "sigaction(SIGINT)");
  check_syscall(sigaction(SIGTERM, &sa, nullptr), "sigaction(SIGTERM)");
}

optional<pair<Address, ConfigMsg>>
wait_for_config_msg(UDPSocket & sock,
                    const function<void()> & on_timeout,
                    const int resend_interval_ms)
{
  while (not g_stop_requested) {
    pollfd pfd {sock.fd_num(), POLLIN, 0};
    const int ret = ::poll(&pfd, 1, resend_interval_ms);

    if (ret < 0) {
      if (errno == EINTR) {
        continue; // probably SIGINT; the loop condition re-checks the flag
      }
      throw unix_error("poll");
    }

    try {
      if (ret == 0) { // timed out without a reply
        if (on_timeout) {
          on_timeout();
        }
        continue;
      }

      const auto [peer_addr, raw_data] = sock.recvfrom();
      if (not raw_data) {
        continue;
      }

      const shared_ptr<Msg> msg = Msg::parse_from_string(*raw_data);
      const auto config_msg = dynamic_pointer_cast<ConfigMsg>(msg);
      if (config_msg) {
        return make_pair(peer_addr, *config_msg);
      }
    } catch (const unix_error & e) {
      // On a connected UDP socket, an ICMP "port unreachable" (the peer is
      // not running yet) shows up as ECONNREFUSED on the next call. Keep
      // waiting; the next request may get through.
      if (e.code().value() != ECONNREFUSED) {
        throw;
      }
    }
  }

  return nullopt;
}
