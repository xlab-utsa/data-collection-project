#ifndef COMMON_HH
#define COMMON_HH

// Small helpers shared by video_sender and video_receiver.

#include <atomic>
#include <functional>
#include <optional>
#include <utility>

#include "address.hh"
#include "protocol.hh"
#include "udp_socket.hh"

// Set to true by SIGINT/SIGTERM (or by pressing 'q' in a preview window).
// Every long-running loop in the programs checks this flag.
extern std::atomic<bool> g_stop_requested;

// Install SIGINT/SIGTERM handlers that set g_stop_requested. The handlers are
// installed without SA_RESTART so that blocking poll()/recv() calls return
// with EINTR and the loops get a chance to notice the flag.
void install_stop_signal_handlers();

// Wait for a ConfigMsg on 'sock'. Non-config datagrams are ignored.
//
// Every 'resend_interval_ms' without a reply, 'on_timeout' (if given) is
// called; the receiver uses it to re-send its request, so a single lost
// handshake packet does not hang both ends.
//
// Returns nullopt if g_stop_requested became true while waiting.
std::optional<std::pair<Address, ConfigMsg>>
wait_for_config_msg(UDPSocket & sock,
                    const std::function<void()> & on_timeout = {},
                    const int resend_interval_ms = 500);

#endif /* COMMON_HH */
