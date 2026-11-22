// Wire format (src/app/protocol.*): round trips, sizes, and robustness
// against truncated or random input.
#include <memory>
#include <random>
#include <string>

#include "protocol.hh"
#include "test_util.hh"

using namespace std;

int main()
{
  // Datagram round trip
  Datagram d(7, FrameType::KEY, 2, 5, "hello");
  d.send_ts = 123;
  const string wire = d.serialize_to_string();
  CHECK(wire.size() == Datagram::HEADER_SIZE + 5);
  Datagram p;
  CHECK(p.parse_from_string(wire));
  CHECK(p.frame_id == 7 && p.frame_type == FrameType::KEY && p.frag_id == 2 &&
        p.frag_cnt == 5 && p.send_ts == 123 && p.payload == "hello");

  // AckMsg, with and without bitrate feedback
  const AckMsg plain(d);
  auto pm = dynamic_pointer_cast<AckMsg>(Msg::parse_from_string(plain.serialize_to_string()));
  CHECK(pm && pm->frame_id == 7 && pm->frag_id == 2 && pm->send_ts == 123 &&
        pm->has_bitrate_feedback == 0 && pm->measured_kbps_x100 == 0);

  const AckMsg fb(d, true, 654321);
  const string aw = fb.serialize_to_string();
  CHECK(aw.size() == fb.serialized_size());
  auto am = dynamic_pointer_cast<AckMsg>(Msg::parse_from_string(aw));
  CHECK(am && am->has_bitrate_feedback == 1 && am->measured_kbps_x100 == 654321);

  // ConfigMsg; and a ConfigMsg is never mistaken for a Datagram
  const ConfigMsg c(1920, 1080, 60, 8000);
  const string cw = c.serialize_to_string();
  CHECK(cw.size() == c.serialized_size());
  auto cm = dynamic_pointer_cast<ConfigMsg>(Msg::parse_from_string(cw));
  CHECK(cm && cm->width == 1920 && cm->height == 1080 && cm->frame_rate == 60 &&
        cm->target_bitrate == 8000);
  Datagram junk;
  CHECK(!junk.parse_from_string(cw));

  // MTU handling
  Datagram::set_mtu(1000);
  CHECK(Datagram::max_payload == 1000 - 28 - Datagram::HEADER_SIZE);
  bool threw = false;
  try { Datagram::set_mtu(100); } catch (const exception &) { threw = true; }
  CHECK(threw);
  Datagram::set_mtu(1500);

  // truncated messages are rejected, not crashed on
  for (const string & full : {aw, cw}) {
    for (size_t n = 0; n < full.size(); n++) {
      CHECK(Msg::parse_from_string(full.substr(0, n)) == nullptr);
    }
  }

  // random input never throws
  mt19937 rng(3);
  for (int i = 0; i < 20000; i++) {
    string r(rng() % 64, '\0');
    for (auto & ch : r) ch = static_cast<char>(rng());
    Msg::parse_from_string(r);
    Datagram x;
    x.parse_from_string(r);
  }

  return test_summary("test_protocol");
}
