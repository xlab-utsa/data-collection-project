// Implementation of Frame and Decoder (decoder.hh): reassembly of Datagrams
// into frames on the receiver, in-order delivery with skip-ahead recovery,
// goodput measurement, and the worker thread that decodes (libvpx VP9),
// records to .y4m and displays.
//
// Threads: the main thread (video_receiver.cc) calls add_datagram(),
// next_frame_complete() and consume_next_frame(); complete frames are moved
// into shared_queue_ under mtx_, and worker_main() takes them from there.

#include <sys/sysinfo.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "common.hh"
#include "decoder.hh"
#include "exception.hh"
#include "conversion.hh"
#include "image.hh"
#include "timestamp.hh"

using namespace std;
using namespace chrono;

Frame::Frame(const uint32_t frame_id,
             const FrameType frame_type,
             const uint16_t frag_cnt)
  : id_(frame_id), type_(frame_type), frags_(frag_cnt), null_frags_(frag_cnt)
{
  if (frag_cnt == 0) {
    throw runtime_error("frame cannot have zero fragments");
  }
}

bool Frame::has_frag(const uint16_t frag_id) const
{
  return frags_.at(frag_id).has_value();
}

Datagram & Frame::get_frag(const uint16_t frag_id)
{
  return frags_.at(frag_id).value();
}

const Datagram & Frame::get_frag(const uint16_t frag_id) const
{
  return frags_.at(frag_id).value();
}

optional<size_t> Frame::frame_size() const
{
  if (not complete()) {
    return nullopt;
  }

  return frame_size_;
}

void Frame::validate_datagram(const Datagram & datagram) const
{
  if (datagram.frame_id != id_ or
      datagram.frame_type != type_ or
      datagram.frag_id >= frags_.size() or
      datagram.frag_cnt != frags_.size()) {
    throw runtime_error("unable to insert an incompatible datagram");
  }
}

void Frame::insert_frag(const Datagram & datagram)
{
  validate_datagram(datagram);

  // insert only if the datagram does not exist yet
  if (not frags_[datagram.frag_id]) {
    frame_size_ += datagram.payload.size();
    null_frags_--;
    frags_[datagram.frag_id] = datagram;
  }
}

void Frame::insert_frag(Datagram && datagram)
{
  validate_datagram(datagram);

  // insert only if the datagram does not exist yet
  if (not frags_[datagram.frag_id]) {
    frame_size_ += datagram.payload.size();
    null_frags_--;
    frags_[datagram.frag_id] = move(datagram);
  }
}

Decoder::Decoder(const uint16_t display_width,
                 const uint16_t display_height,
                 const int lazy_level,
                 const uint16_t frame_rate,
                 const string & output_path,
                 const string & record_dir)
  : display_width_(display_width), display_height_(display_height),
    lazy_level_(), frame_rate_(frame_rate), record_dir_(record_dir),
    output_fd_(), decoder_epoch_(steady_clock::now())
{
  // validate lazy level
  if (lazy_level < DECODE_DISPLAY or lazy_level > NO_DECODE_DISPLAY) {
    throw runtime_error("Invalid lazy level: " + to_string(lazy_level));
  }
  lazy_level_ = static_cast<LazyLevel>(lazy_level);

  // both main and worker threads start from the same time for stats output
  last_stats_time_ = decoder_epoch_;

  // open the output file
  if (not output_path.empty()) {
    output_fd_ = FileDescriptor(check_syscall(
        open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644)));
  }

  // start the worker thread only if we are going to decode frames
  if (lazy_level_ <= DECODE_ONLY) {
    worker_ = thread(&Decoder::worker_main, this);
    cerr << "Spawned a new thread for decoding and displaying frames" << endl;
  }
}

Decoder::~Decoder()
{
  if (worker_.joinable()) {
    {
      lock_guard<mutex> lock(mtx_);
      stopping_ = true;
    }
    cv_.notify_one();
    worker_.join();
  }
}

bool Decoder::add_datagram_common(const Datagram & datagram)
{
  const auto frame_id = datagram.frame_id;
  const auto frame_type = datagram.frame_type;
  const auto frag_cnt = datagram.frag_cnt;

  // ignore any datagrams from the old frames
  if (frame_id < next_frame_) {
    return false;
  }

  if (not frame_buf_.count(frame_id)) {
    // initialize a Frame instance for frame 'frame_id'
    frame_buf_.emplace(piecewise_construct,
                       forward_as_tuple(frame_id),
                       forward_as_tuple(frame_id, frame_type, frag_cnt));
  }

  return true;
}

void Decoder::add_datagram(const Datagram & datagram)
{
  if (not add_datagram_common(datagram)) {
    return;
  }

  // copy the fragment into the frame
  frame_buf_.at(datagram.frame_id).insert_frag(datagram);
}

void Decoder::add_datagram(Datagram && datagram)
{
  if (not add_datagram_common(datagram)) {
    return;
  }

  // move the fragment into the frame
  frame_buf_.at(datagram.frame_id).insert_frag(move(datagram));
}

bool Decoder::next_frame_complete()
{
  {
    // check if the next frame to expect is complete
    auto it = as_const(frame_buf_).find(next_frame_);
    if (it != frame_buf_.end() and it->second.complete()) {
      return true;
    }
  }

  // Seek forward if a key frame in the future is already complete. A key
  // frame does not depend on earlier frames, so everything before it can be
  // abandoned. Scanning from the newest frame (rbegin) jumps to the most
  // recent complete key frame, skipping as much stale video as possible.
  for (auto it = frame_buf_.rbegin(); it != frame_buf_.rend(); it++) {
    const auto frame_id = it->first;
    const auto & frame = it->second;

    // found a complete key frame ahead of next_frame_
    if (frame.type() == FrameType::KEY and frame.complete()) {
      assert(frame_id > next_frame_);

      // set next_frame_ to frame_id and clean up old frames
      const auto frame_diff = frame_id - next_frame_;
      advance_next_frame(frame_diff);

      cerr << "* Recovery: skipped " << frame_diff
           << " frames ahead to key frame " << frame_id << endl;

      return true;
    }
  }

  return false;
}

void Decoder::update_bitrate_stats(const size_t frame_size)
{
  num_decodable_frames_++;
  total_decodable_frame_size_ += frame_size;

  const auto stats_now = steady_clock::now();
  while (stats_now >= last_stats_time_ + 1s) {
    cerr << "Decodable frames in the last ~1s: "
         << num_decodable_frames_ << endl;

    const double diff_ms = duration<double, milli>(
                           stats_now - last_stats_time_).count();
    if (diff_ms > 0) {
      // bytes * 8 / ms = kbps; scaled by 100 to keep two decimals in an int
      latest_bitrate_ = static_cast<uint32_t>(
          total_decodable_frame_size_ * 8 * 100 / diff_ms);
      pending_bitrate_ = latest_bitrate_;
      cerr << "  - Bitrate (kbps): "
           << double_to_string(total_decodable_frame_size_ * 8 / diff_ms)
           << endl;
    } else {
      latest_bitrate_.reset();
      pending_bitrate_.reset();
    }

    // reset stats
    num_decodable_frames_ = 0;
    total_decodable_frame_size_ = 0;
    last_stats_time_ += 1s;
  }
}

void Decoder::consume_next_frame()
{
  Frame & frame = frame_buf_.at(next_frame_);
  if (not frame.complete()) {
    throw runtime_error("next frame must be complete before consuming it");
  }

  const size_t frame_size = frame.frame_size().value();
  update_bitrate_stats(frame_size);

  if (lazy_level_ <= DECODE_ONLY) {
    // dispatch the frame to worker thread
    {
      lock_guard<mutex> lock(mtx_);
      shared_queue_.emplace_back(move(frame));
    } // release the lock before notifying the worker thread

    cv_.notify_one();
  } else {
    // main thread outputs frame information if no worker thread
    if (output_fd_) {
      const auto frame_decodable_ts = timestamp_us();

      output_fd_->write(to_string(next_frame_) + "," +
                        to_string(frame_size) + "," +
                        to_string(frame_decodable_ts) + "\n");
    }
  }

  // move onto the next frame
  advance_next_frame();
}

void Decoder::advance_next_frame(const unsigned int n)
{
  next_frame_ += n;

  // clean up state up to next_frame_
  clean_up_to(next_frame_);
}

void Decoder::clean_up_to(const uint32_t frontier)
{
  for (auto it = frame_buf_.begin(); it != frame_buf_.end(); ) {
    if (it->first >= frontier) {
      break;
    }

    it = frame_buf_.erase(it);
  }
}

double Decoder::decode_frame(vpx_codec_ctx_t & context, const Frame & frame,
                             vector<uint8_t> & decode_buf)
{
  if (not frame.complete()) {
    throw runtime_error("frame must be complete before decoding");
  }

  // concatenate the fragments' payloads into one compressed frame; the
  // buffer grows as needed (key frames at 4K/8K easily exceed 1 MB)
  decode_buf.clear();
  for (const auto & datagram : frame.frags()) {
    const string & payload = datagram.value().payload;
    decode_buf.insert(decode_buf.end(), payload.begin(), payload.end());
  }

  const auto decode_start = steady_clock::now();
  check_call(vpx_codec_decode(&context, decode_buf.data(),
                              static_cast<unsigned int>(decode_buf.size()),
                              nullptr, 1),
             VPX_CODEC_OK, "failed to decode a frame");
  const auto decode_end = steady_clock::now();

  return duration<double, milli>(decode_end - decode_start).count();
}

namespace {

// write one decoded I420 image as a Y4M "FRAME" (planes without row padding)
void write_y4m_frame(ofstream & out, const vpx_image & img)
{
  out << "FRAME\n";
  for (int plane = 0; plane < 3; plane++) {
    const uint8_t * data = img.planes[plane];
    const int stride = img.stride[plane];
    const unsigned int h = (plane == 0) ? img.d_h : (img.d_h + 1) / 2;
    const unsigned int w = (plane == 0) ? img.d_w : (img.d_w + 1) / 2;

    for (unsigned int y = 0; y < h; y++) {
      out.write(reinterpret_cast<const char *>(data), w);
      data += stride;
    }
  }
}

// ./data/output_YYYYmmdd_HHMMSS.y4m
string timestamped_y4m_path(const string & dir)
{
  const time_t now = system_clock::to_time_t(system_clock::now());
  tm now_tm;
  localtime_r(&now, &now_tm);

  char name[64];
  strftime(name, sizeof(name), "output_%Y%m%d_%H%M%S.y4m", &now_tm);
  return (filesystem::path(dir) / name).string();
}

} // namespace

void Decoder::worker_loop()
{
  // initialize a VP9 decoding context
  const unsigned int max_threads = min(get_nprocs(), 4);
  vpx_codec_dec_cfg_t cfg {max_threads, display_width_, display_height_};

  vpx_codec_ctx_t context;
  check_call(vpx_codec_dec_init(&context, &vpx_codec_vp9_dx_algo, &cfg, 0),
             VPX_CODEC_OK, "vpx_codec_dec_init");

  cerr << "[worker] Initialized decoder (max threads: "
       << max_threads << ")" << endl;

  // video display
  unique_ptr<VideoDisplay> display;
  if (lazy_level_ == DECODE_DISPLAY) {
    display = make_unique<VideoDisplay>(display_width_, display_height_);
  }

  // ---- recording: every decoded frame goes into a raw .y4m file ----
  filesystem::create_directories(record_dir_);
  const string y4m_path = timestamped_y4m_path(record_dir_);

  // a large stream buffer keeps the disk writes big and sequential; it has to
  // be installed before open() to take effect
  vector<char> y4m_buffer(32 * 1024 * 1024); // 32 MiB
  ofstream y4m_file;
  y4m_file.rdbuf()->pubsetbuf(y4m_buffer.data(), y4m_buffer.size());
  y4m_file.open(y4m_path, ios::binary);
  if (not y4m_file.is_open()) {
    throw runtime_error("Failed to open " + y4m_path + " for writing");
  }
  cerr << "[worker] Recording decoded video to " << y4m_path << endl;

  // Y4M header: 4:2:0 (the default colour space), progressive, square pixels
  y4m_file << "YUV4MPEG2 W" << display_width_ << " H" << display_height_
           << " F" << frame_rate_ << ":1 Ip A1:1\n";

  deque<Frame> local_queue;
  vector<uint8_t> decode_buf;

  while (true) {
    // destroy display if it has been signalled to quit
    if (display and display->signal_quit()) {
      display.reset(nullptr);
    }

    {
      // wait until there is work, or we are asked to stop
      unique_lock<mutex> lock(mtx_);
      cv_.wait(lock, [this] { return stopping_ or not shared_queue_.empty(); });

      if (shared_queue_.empty()) {
        break; // stopping_, and nothing left to decode
      }

      // grab everything quickly and release the lock for the main thread
      while (not shared_queue_.empty()) {
        local_queue.emplace_back(move(shared_queue_.front()));
        shared_queue_.pop_front();
      }
    }

    while (not local_queue.empty()) {
      const Frame & frame = local_queue.front();
      decode_frame(context, frame, decode_buf);

      if (output_fd_) {
        output_fd_->write(to_string(frame.id()) + "," +
                          to_string(frame.frame_size().value()) + "," +
                          to_string(timestamp_us()) + "\n");
      }

      // fetch the decoded image once and use it for both recording and
      // display (libvpx returns each decoded frame only once)
      vpx_codec_iter_t iter = nullptr;
      unsigned int frames_decoded = 0;
      while (vpx_image * img = vpx_codec_get_frame(&context, &iter)) {
        if (++frames_decoded > 1) {
          throw runtime_error("Multiple frames were decoded at once");
        }

        write_y4m_frame(y4m_file, *img);

        if (display) {
          // temporary RawImage that does not own 'img'
          display->show_frame(RawImage(img));
        }
      }

      local_queue.pop_front();
    }
  }

  y4m_file.close();
  cerr << "[worker] Closed " << y4m_path << endl;

  check_call(vpx_codec_destroy(&context), VPX_CODEC_OK, "vpx_codec_destroy");
}

void Decoder::worker_main()
{
  try {
    worker_loop();
  } catch (const exception & e) {
    // without a worker nobody drains shared_queue_, so stop the receiver
    cerr << "[worker] Error: " << e.what() << endl;
    g_stop_requested = true;
  }
}
