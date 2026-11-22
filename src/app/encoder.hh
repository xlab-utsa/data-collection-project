#ifndef ENCODER_HH
#define ENCODER_HH

extern "C" {
#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>
}

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "exception.hh"
#include "image.hh"
#include "protocol.hh"
#include "file_descriptor.hh"

// VP9 encoder plus the sender half of the transport: it packetizes encoded
// frames into MTU-sized Datagrams, keeps unacked datagrams, retransmits on
// loss and forces a key frame when retransmission gives up.
class Encoder
{
public:
  // initialize a VP9 encoder
  Encoder(const uint16_t display_width,
          const uint16_t display_height,
          const uint16_t frame_rate,
          const std::string & output_path = "");
  ~Encoder();

  // encode raw_img and packetize into datagrams
  void compress_frame(const RawImage & raw_img);

  // add a transmitted but unacked datagram (except retransmissions) to unacked
  void add_unacked(const Datagram & datagram);
  void add_unacked(Datagram && datagram);

  // handle ACK
  void handle_ack(const std::shared_ptr<AckMsg> & ack);

  // output stats every second and reset some of them
  void output_periodic_stats();

  // set target bitrate (kbps); can be changed while streaming
  void set_target_bitrate(const unsigned int bitrate_kbps);

  // accessors
  uint32_t frame_id() const { return frame_id_; }
  std::deque<Datagram> & send_buf() { return send_buf_; }
  std::map<SeqNum, Datagram> & unacked() { return unacked_; }

  // mutators
  void set_verbose(const bool verbose) { verbose_ = verbose; }

  // forbid copying and moving
  Encoder(const Encoder & other) = delete;
  const Encoder & operator=(const Encoder & other) = delete;
  Encoder(Encoder && other) = delete;
  Encoder & operator=(Encoder && other) = delete;

private:
  uint16_t display_width_;
  uint16_t display_height_;
  uint16_t frame_rate_;
  std::optional<FileDescriptor> output_fd_;

  // print debugging info
  bool verbose_ {false};

  // current target bitrate
  unsigned int target_bitrate_ {0};

  // VPX encoding configuration and context
  vpx_codec_enc_cfg_t cfg_ {};
  vpx_codec_ctx_t context_ {};

  // frame ID to encode
  uint32_t frame_id_ {0};

  // queue of datagrams (packetized video frames) to send
  std::deque<Datagram> send_buf_ {};

  // unacked datagrams
  std::map<SeqNum, Datagram> unacked_ {};

  // RTT estimation from ACKs (send_ts is echoed back by the receiver)
  std::optional<unsigned int> min_rtt_us_ {};
  std::optional<double> ewma_rtt_us_ {};
  static constexpr double ALPHA = 0.2; // EWMA weight of the newest sample

  // RTT samples collected since the last bitrate report from the receiver.
  // Not consumed yet: kept as input for the planned ACK-based adaptive
  // bitrate controller (see README, "Roadmap").
  std::vector<unsigned int> rtt_samples_ {};
  bool reset_rtt_samples_ {false};

  // performance stats
  unsigned int num_encoded_frames_ {0};
  double total_encode_time_ms_ {0.0};
  double max_encode_time_ms_ {0.0};

  // constants
  static constexpr unsigned int MAX_NUM_RTX = 3;
  static constexpr uint64_t MAX_UNACKED_US = 1000 * 1000; // 1 second

  // track RTT
  void add_rtt_sample(const unsigned int rtt_us);

  // encode the raw frame stored in 'raw_img'
  void encode_frame(const RawImage & raw_img);

  // packetize the just encoded frame (stored in context_) and return its size
  size_t packetize_encoded_frame();

  // VPX API wrappers
  template <typename ... Args>
  inline void codec_control(Args && ... args)
  {
    check_call(vpx_codec_control_(std::forward<Args>(args)...),
               VPX_CODEC_OK, "vpx_codec_control_");
  }
};

#endif /* ENCODER_HH */
