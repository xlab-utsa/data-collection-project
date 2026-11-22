#ifndef DECODER_HH
#define DECODER_HH

extern "C" {
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>
}

#include <map>
#include <vector>
#include <deque>
#include <optional>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <string>
#include <thread>

#include "protocol.hh"
#include "sdl.hh"
#include "file_descriptor.hh"

// A video frame being reassembled on the receiver from its fragments.
class Frame
{
public:
  Frame(const uint32_t frame_id,
        const FrameType frame_type,
        const uint16_t frag_cnt);

  // if the frame has fragment 'frag_id'
  bool has_frag(const uint16_t frag_id) const;

  // get fragment 'frag_id'
  Datagram & get_frag(const uint16_t frag_id);
  const Datagram & get_frag(const uint16_t frag_id) const;

  // insert a fragment into the frame
  void insert_frag(const Datagram & datagram);
  void insert_frag(Datagram && datagram);

  // if the frame has received all fragments
  bool complete() const { return null_frags_ == 0; }
  std::optional<size_t> frame_size() const;

  // accessors
  uint32_t id() const { return id_; }
  FrameType type() const { return type_; }

  std::vector<std::optional<Datagram>> & frags() { return frags_; }
  const std::vector<std::optional<Datagram>> & frags() const { return frags_; }

  unsigned int null_frags() const { return null_frags_; }

private:
  uint32_t id_;    // frame ID
  FrameType type_; // frame type

  std::vector<std::optional<Datagram>> frags_; // fragments of this frame
  unsigned int null_frags_; // number of uninitialized fragments
  size_t frame_size_ {0}; // frame size so far

  // validate if a datagram belongs to this frame
  void validate_datagram(const Datagram & datagram) const;
};

// Receiver side: reassembles Datagrams into frames, measures goodput, and
// hands complete frames to a worker thread that decodes them (VP9), records
// them to a .y4m file and optionally displays them.
//
// Frames are consumed strictly in order. If the next frame cannot be
// completed but a later key frame is complete, the decoder skips ahead to that
// key frame (the sender forces one after giving up on retransmissions).
class Decoder
{
public:
  enum LazyLevel : int {
    DECODE_DISPLAY = 0,   // decode, record to .y4m and display
    DECODE_ONLY = 1,      // decode and record to .y4m, no window
    NO_DECODE_DISPLAY = 2 // only reassemble frames (no decoding, no recording)
  };

  // 'record_dir': where the decoded .y4m recording is written
  // 'output_path': optional per-frame CSV log (empty = none)
  Decoder(const uint16_t display_width,
          const uint16_t display_height,
          const int lazy_level = 0,
          const uint16_t frame_rate = 30,
          const std::string & output_path = "",
          const std::string & record_dir = "./data");

  // stops the worker after it has decoded and written every queued frame
  ~Decoder();

  // add a received datagram
  void add_datagram(const Datagram & datagram);
  void add_datagram(Datagram && datagram);

  // is next frame complete; might skip to a complete key frame ahead
  bool next_frame_complete();

  // account for the next (complete) frame and pass it to the worker
  void consume_next_frame();

  // accessors
  uint32_t next_frame() const { return next_frame_; }

  // Goodput measured over the last ~1 s, in kbps * 100 (the AckMsg unit).
  // take_pending_bitrate() returns each new measurement once, so it is sent
  // to the sender in exactly one ACK.
  std::optional<uint32_t> latest_bitrate() const { return latest_bitrate_; }
  std::optional<uint32_t> take_pending_bitrate()
  {
    auto val = pending_bitrate_;
    pending_bitrate_.reset();
    return val;
  }

  // mutators
  void set_verbose(const bool verbose) { verbose_ = verbose; }

  // forbid copying and moving
  Decoder(const Decoder & other) = delete;
  const Decoder & operator=(const Decoder & other) = delete;
  Decoder(Decoder && other) = delete;
  Decoder & operator=(Decoder && other) = delete;

private:
  // initialized before worker thread starts and won't be modified again
  uint16_t display_width_;
  uint16_t display_height_;
  LazyLevel lazy_level_;
  uint16_t frame_rate_;
  std::string record_dir_;
  std::optional<FileDescriptor> output_fd_; // only one thread should output
  std::chrono::time_point<std::chrono::steady_clock> decoder_epoch_;

  // print debugging info
  bool verbose_ {false};

  // next frame ID to decode
  uint32_t next_frame_ {0};

  // frame ID => class Frame
  std::map<uint32_t, Frame> frame_buf_ {};

  // goodput stats (main thread)
  unsigned int num_decodable_frames_ {0};
  size_t total_decodable_frame_size_ {0}; // bytes
  std::chrono::time_point<std::chrono::steady_clock> last_stats_time_ {};
  std::optional<uint32_t> latest_bitrate_ {};  // kbps * 100
  std::optional<uint32_t> pending_bitrate_ {}; // kbps * 100, not yet sent

  // shared between main (Decoder) and worker threads
  std::mutex mtx_ {};
  std::condition_variable cv_ {};
  std::deque<Frame> shared_queue_ {};
  bool stopping_ {false};

  // worker thread for decoding and displaying frames
  std::thread worker_ {};

  // common code between the two versions of add_datagram()
  bool add_datagram_common(const Datagram & datagram);

  // advance next frame ID by 'n'
  void advance_next_frame(const unsigned int n = 1);

  // clean up states (such as frame_buf_) up to frame 'frontier'
  void clean_up_to(const uint32_t frontier);

  // update and print the once-per-second goodput measurement
  void update_bitrate_stats(const size_t frame_size);

  // worker thread calls the functions below
  double decode_frame(vpx_codec_ctx_t & context, const Frame & frame,
                      std::vector<uint8_t> & decode_buf);
  void worker_main(); // thread entry point; reports errors
  void worker_loop();
};

#endif /* DECODER_HH */
