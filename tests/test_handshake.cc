// Handshake helper (src/app/common.*) over the loopback interface, including
// a receiver that starts before the sender (ECONNREFUSED is tolerated and the
// request is retried until answered).
#include <chrono>
#include <thread>

#include "common.hh"
#include "test_util.hh"

using namespace std;

int main()
{
  // pick a free port by binding and releasing it
  uint16_t port;
  {
    UDPSocket probe;
    probe.bind({"127.0.0.1", 0});
    port = probe.local_address().port();
  }

  UDPSocket receiver;
  receiver.connect({"127.0.0.1", port});
  const ConfigMsg request(0, 0, 0, 5000);
  receiver.send(request.serialize_to_string()); // nobody listening yet

  thread sender([port] {
    this_thread::sleep_for(chrono::milliseconds(700));
    UDPSocket sock;
    sock.bind({"127.0.0.1", port});
    const auto got = wait_for_config_msg(sock);
    CHECK(got && got->second.target_bitrate == 5000);
    sock.connect(got->first);
    sock.send(ConfigMsg(1280, 720, 120, 5000).serialize_to_string());
  });

  const auto reply = wait_for_config_msg(
      receiver, [&] { receiver.send(request.serialize_to_string()); }, 200);
  sender.join();
  CHECK(reply && reply->second.width == 1280 && reply->second.height == 720 &&
        reply->second.frame_rate == 120);

  // a stop request ends the wait
  g_stop_requested = true;
  UDPSocket idle;
  idle.bind({"127.0.0.1", 0});
  CHECK(!wait_for_config_msg(idle, {}, 50));

  return test_summary("test_handshake");
}
