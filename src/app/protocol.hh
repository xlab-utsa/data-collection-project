#ifndef PROTOCOL_HH
#define PROTOCOL_HH

// Wire format between video_sender and video_receiver (all integers are
// big-endian, see serialization.hh).
//
//   sender -> receiver   Datagram    one fragment of an encoded VP9 frame
//   receiver -> sender   AckMsg      per-datagram ACK (+ optional bitrate feedback)
//   both directions      ConfigMsg   handshake: receiver asks for a bitrate,
//                                    sender answers with width/height/fps/bitrate
//
// Datagrams carry no message-type byte; the receiver tells them apart from a
// ConfigMsg by size (a Datagram is always at least HEADER_SIZE bytes).

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

enum class FrameType : uint8_t {
  UNKNOWN = 0,
  KEY = 1,    // decodable on its own
  NONKEY = 2, // depends on earlier frames
};

// (frame_id, frag_id) uniquely identifies a datagram and orders them
using SeqNum = std::pair<uint32_t, uint16_t>;

struct Datagram
{
  Datagram() {}
  Datagram(const uint32_t _frame_id,
           const FrameType _frame_type,
           const uint16_t _frag_id,
           const uint16_t _frag_cnt,
           const std::string_view _payload);

  // ---- fields on the wire, in order ----
  uint32_t frame_id {};    // which video frame this fragment belongs to
  FrameType frame_type {}; // key / non-key
  uint16_t frag_id {};     // index of this fragment within the frame
  uint16_t frag_cnt {};    // total number of fragments in the frame
  uint64_t send_ts {};     // sender clock (us) at transmission; echoed in ACK
  std::string payload {};  // slice of the encoded frame

  // ---- sender-side bookkeeping, not serialized ----
  unsigned int num_rtx {0};   // how many times this datagram was retransmitted
  uint64_t last_send_ts {0};  // when it was last (re)transmitted

  static constexpr size_t HEADER_SIZE = sizeof(uint32_t) +
      sizeof(FrameType) + 2 * sizeof(uint16_t) + sizeof(uint64_t);

  // largest payload that keeps the IP packet within the MTU
  static size_t max_payload;
  static void set_mtu(const size_t mtu);

  // returns false if 'binary' is too short to be a datagram
  bool parse_from_string(const std::string & binary);
  std::string serialize_to_string() const;
};

// Control messages start with a one-byte type tag.
struct Msg
{
  enum class Type : uint8_t {
    INVALID = 0,
    ACK = 1,
    CONFIG = 2
  };

  Type type {Type::INVALID};

  Msg() {}
  Msg(const Type _type) : type(_type) {}
  virtual ~Msg() {}

  // returns nullptr for unknown, truncated or malformed messages
  static std::shared_ptr<Msg> parse_from_string(const std::string & binary);

private:
  static std::shared_ptr<Msg> parse_body(const std::string & binary);

public:

  virtual size_t serialized_size() const;
  virtual std::string serialize_to_string() const;
};

struct AckMsg : Msg
{
  AckMsg() : Msg(Type::ACK) {}

  // ACK 'datagram'; optionally piggy-back the receiver's measured bitrate
  AckMsg(const Datagram & datagram,
         const bool has_bitrate_feedback = false,
         const uint32_t measured_kbps_x100 = 0);

  uint32_t frame_id {}; // the acked datagram ...
  uint16_t frag_id {};  // ... and fragment
  uint64_t send_ts {};  // echoed from the datagram, for RTT measurement

  // Receiver-measured throughput of decodable frames over the last ~1 s,
  // in units of 0.01 kbps (i.e. kbps * 100). Only valid when
  // has_bitrate_feedback is set, which happens about once per second.
  uint8_t has_bitrate_feedback {0};
  uint32_t measured_kbps_x100 {0};

  size_t serialized_size() const override;
  std::string serialize_to_string() const override;
};

struct ConfigMsg : Msg
{
  ConfigMsg() : Msg(Type::CONFIG) {}
  ConfigMsg(const uint16_t _width, const uint16_t _height,
            const uint16_t _frame_rate, const uint32_t _target_bitrate);

  // The receiver's request leaves width/height/frame_rate at 0 and only sets
  // target_bitrate; the sender's reply fills in all four fields.
  uint16_t width {};
  uint16_t height {};
  uint16_t frame_rate {};     // frames per second
  uint32_t target_bitrate {}; // kbps

  size_t serialized_size() const override;
  std::string serialize_to_string() const override;
};

#endif /* PROTOCOL_HH */
