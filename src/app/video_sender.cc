// video_sender: runs on the camera machine (e.g. a Jetson board).
//
//   capture thread:  V4L2 camera --YUYV--> swscale --I420--> FrameRing
//   main thread:     FrameRing --> VP9 encoder --> Datagrams --UDP--> receiver
//                    <-- ACKs (RTT, retransmission, bitrate feedback) <--
//
// Start it first; it waits for a receiver to connect and ask for a bitrate.

#include <getopt.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "capture.hh"
#include "common.hh"
#include "conversion.hh"
#include "encoder.hh"
#include "frame_ring.hh"
#include "poller.hh"
#include "protocol.hh"
#include "timerfd.hh"
#include "timestamp.hh"
#include "udp_socket.hh"

using namespace std;

namespace {

constexpr long BILLION = 1000L * 1000 * 1000;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <port> -w <width> -h <height> -r <fps> [options]\n\n"
  "  -w, --width <W>        capture width\n"
  "  -h, --height <H>       capture height\n"
  "  -r, --fps <FPS>        capture and encoding frame rate\n"
  "  -d, --device <path>    V4L2 camera (default: /dev/video0)\n"
  "  --mtu <bytes>          MTU used to size datagrams (default: 1500)\n"
  "  --no-preview           do not open the 640x480 preview window\n"
  "  -o, --output <file>    write per-frame encoder stats (CSV) to <file>\n"
  "  -v, --verbose          log every datagram and ACK\n\n"
  "Supported modes (camera/V4L2 limits):\n"
  "  up to 1280x720 @ 120, 1920x1080 @ 60, 2000x1500 @ 50,\n"
  "        3840x2160 @ 20, 4000x3000 @ 14, 8000x6000 @ 3\n"
  "  frame rate must be one of 120, 60, 50, 20, 14, 3\n"
  << endl;
}

// The camera this was built for only exposes a few modes; reject anything
// else up front instead of letting the driver silently pick another mode.
bool validate_resolution_and_fps(const int width, const int height,
                                 const int fps)
{
  struct Tier { int w, h, max_fps; };
  static const Tier tiers[] = {
    { 1280,  720, 120 },
    { 1920, 1080,  60 },
    { 2000, 1500,  50 },
    { 3840, 2160,  20 },
    { 4000, 3000,  14 },
    { 8000, 6000,   3 },
  };
  static const int allowed_fps[] = { 120, 60, 50, 20, 14, 3 };

  // smallest tier that contains the requested resolution
  int tier_max = 0;
  for (const auto & tier : tiers) {
    if (width <= tier.w and height <= tier.h) {
      tier_max = tier.max_fps;
      break;
    }
  }

  if (tier_max == 0) {
    cerr << "Unsupported resolution: " << width << "x" << height << endl;
    return false;
  }

  for (const int allowed : allowed_fps) {
    if (fps == allowed and fps <= tier_max) {
      return true;
    }
  }

  cerr << "Unsupported frame rate " << fps << " fps for resolution "
       << width << "x" << height << " (max " << tier_max << " fps)" << endl;
  return false;
}

} // namespace

int main(int argc, char * argv[])
{
  CaptureConfig cap;
  cap.width = cap.height = cap.fps = 0;
  string output_path;
  bool verbose = false;

  // ===== argument parsing =====
  // note: -h is height (kept for compatibility); use --help for help
  const option cmd_line_opts[] = {
    {"width",      required_argument, nullptr, 'w'},
    {"height",     required_argument, nullptr, 'h'},
    {"fps",        required_argument, nullptr, 'r'},
    {"device",     required_argument, nullptr, 'd'},
    {"mtu",        required_argument, nullptr, 'M'},
    {"no-preview", no_argument,       nullptr, 'P'},
    {"output",     required_argument, nullptr, 'o'},
    {"verbose",    no_argument,       nullptr, 'v'},
    {"help",       no_argument,       nullptr, 'H'},
    {nullptr,      0,                 nullptr,  0 },
  };

  try {
    while (true) {
      const int opt = getopt_long(argc, argv, "w:h:r:d:o:v",
                                  cmd_line_opts, nullptr);
      if (opt == -1) {
        break;
      }

      switch (opt) {
        case 'w': cap.width = strict_stoi(optarg); break;
        case 'h': cap.height = strict_stoi(optarg); break;
        case 'r': cap.fps = strict_stoi(optarg); break;
        case 'd': cap.device = optarg; break;
        case 'M': Datagram::set_mtu(strict_stoi(optarg)); break;
        case 'P': cap.preview = false; break;
        case 'o': output_path = optarg; break;
        case 'v': verbose = true; break;
        case 'H': print_usage(argv[0]); return EXIT_SUCCESS;
        default:  print_usage(argv[0]); return EXIT_FAILURE;
      }
    }
  } catch (const exception & e) {
    cerr << "Invalid argument: " << e.what() << endl;
    return EXIT_FAILURE;
  }

  // getopt_long permutes argv, so the port can come before or after options
  if (optind != argc - 1) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }
  const auto port = narrow_cast<uint16_t>(strict_stoi(argv[optind]));

  if (cap.width <= 0 or cap.height <= 0 or cap.fps <= 0) {
    cerr << "-w, -h and -r are required and must be > 0" << endl;
    return EXIT_FAILURE;
  }
  if (cap.width % 2 or cap.height % 2) {
    cerr << "width and height must be even (I420 chroma subsampling)" << endl;
    return EXIT_FAILURE;
  }
  if (not validate_resolution_and_fps(cap.width, cap.height, cap.fps)) {
    return EXIT_FAILURE;
  }

  const auto width = narrow_cast<uint16_t>(cap.width);
  const auto height = narrow_cast<uint16_t>(cap.height);
  const auto fps = narrow_cast<uint16_t>(cap.fps);
  cerr << "Sender: port " << port << ", " << width << "x" << height
       << " @ " << fps << " fps" << endl;

  install_stop_signal_handlers();

  // ===== handshake: wait for a receiver to ask for a bitrate =====
  UDPSocket udp_sock;
  udp_sock.bind({"0", port});
  cerr << "Local address: " << udp_sock.local_address().str()
       << "; waiting for receiver..." << endl;

  const auto request = wait_for_config_msg(udp_sock);
  if (not request) {
    return EXIT_SUCCESS; // interrupted while waiting
  }
  const auto & [peer_addr, request_msg] = *request;
  const uint32_t target_bitrate = request_msg.target_bitrate;
  cerr << "Receiver " << peer_addr.str() << " requested "
       << target_bitrate << " kbps" << endl;

  udp_sock.connect(peer_addr);

  // reply with the full stream configuration
  const ConfigMsg stream_config(width, height, fps, target_bitrate);
  udp_sock.send(stream_config.serialize_to_string());
  udp_sock.set_blocking(false);

  // ===== encoder =====
  Encoder encoder(width, height, fps, output_path);
  encoder.set_target_bitrate(target_bitrate);
  encoder.set_verbose(verbose);
  RawImage raw_img(width, height);

  // ===== capture thread =====
  const size_t frame_size = static_cast<size_t>(width) * height * 3 / 2;
  FrameRing ring(FrameRing::capacity_for(frame_size), frame_size);
  cerr << "Frame ring: " << ring.capacity() << " slots of "
       << frame_size << " bytes" << endl;

  thread capture_thread(run_capture, cref(cap), ref(ring));

  // ===== event loop =====
  Poller poller;

  // encode one frame per frame interval
  Timerfd fps_timer;
  const timespec frame_interval {0, BILLION / fps};
  fps_timer.set_time(frame_interval, frame_interval);

  poller.register_event(fps_timer, Poller::In,
    [&]()
    {
      const auto num_exp = fps_timer.read_expirations();
      if (num_exp > 1 and verbose) {
        cerr << "Warning: timer fell behind by " << num_exp - 1
             << " frame intervals" << endl;
      }

      // never block the event loop waiting for the camera; if no frame is
      // ready, try again on the next tick
      const uint8_t * frame = ring.try_begin_read();
      if (not frame) {
        return;
      }
      raw_img.copy_from_i420(frame, ring.frame_size());
      ring.end_read();

      // encode and packetize into encoder.send_buf()
      encoder.compress_frame(raw_img);

      if (not encoder.send_buf().empty()) {
        poller.activate(udp_sock, Poller::Out);
      }
    }
  );

  // socket writable: flush queued datagrams (retransmissions come first)
  poller.register_event(udp_sock, Poller::Out,
    [&]()
    {
      deque<Datagram> & send_buf = encoder.send_buf();

      while (not send_buf.empty()) {
        auto & datagram = send_buf.front();
        datagram.send_ts = timestamp_us();

        if (not udp_sock.send(datagram.serialize_to_string())) {
          datagram.send_ts = 0; // EWOULDBLOCK: not sent, retry when writable
          break;
        }

        if (verbose) {
          cerr << "Sent datagram: frame_id=" << datagram.frame_id
               << " frag_id=" << datagram.frag_id
               << " frag_cnt=" << datagram.frag_cnt
               << " rtx=" << datagram.num_rtx << endl;
        }

        // first transmissions are tracked until acked; retransmissions are
        // already tracked
        if (datagram.num_rtx == 0) {
          encoder.add_unacked(move(datagram));
        }
        send_buf.pop_front();
      }

      if (send_buf.empty()) {
        poller.deactivate(udp_sock, Poller::Out);
      }
    }
  );

  // socket readable: ACKs from the receiver
  poller.register_event(udp_sock, Poller::In,
    [&]()
    {
      while (true) {
        const auto raw_data = udp_sock.recv();
        if (not raw_data) {
          break; // EWOULDBLOCK: drained
        }

        const shared_ptr<Msg> msg = Msg::parse_from_string(*raw_data);
        if (not msg) {
          continue;
        }

        // the receiver re-sends its request if it missed our reply
        if (msg->type == Msg::Type::CONFIG) {
          udp_sock.send(stream_config.serialize_to_string());
          continue;
        }

        const auto ack = dynamic_pointer_cast<AckMsg>(msg);
        if (not ack) {
          continue;
        }

        if (verbose) {
          cerr << "Received ACK: frame_id=" << ack->frame_id
               << " frag_id=" << ack->frag_id << endl;
        }

        if (ack->has_bitrate_feedback) {
          // reported only; the adaptive bitrate controller is not in place
          // yet (see README, "Roadmap")
          cerr << "[Feedback] receiver goodput (kbps): "
               << ack->measured_kbps_x100 / 100.0 << endl;
        }

        // RTT estimation and loss-triggered retransmission
        encoder.handle_ack(ack);

        if (not encoder.send_buf().empty()) {
          poller.activate(udp_sock, Poller::Out);
        }
      }
    }
  );

  // print encoder stats every second
  Timerfd stats_timer;
  const timespec stats_interval {1, 0};
  stats_timer.set_time(stats_interval, stats_interval);

  poller.register_event(stats_timer, Poller::In,
    [&]()
    {
      if (stats_timer.read_expirations() == 0) {
        return;
      }
      encoder.output_periodic_stats();
      cerr << "  - Frames queued in ring: " << ring.size()
           << " (dropped so far: " << ring.dropped() << ")" << endl;
    }
  );

  int exit_code = EXIT_SUCCESS;
  try {
    while (not g_stop_requested) {
      poller.poll(-1); // returns early on SIGINT (EINTR)
    }
  } catch (const exception & e) {
    cerr << "Sender error: " << e.what() << endl;
    g_stop_requested = true;
    exit_code = EXIT_FAILURE;
  }

  cerr << "Stopping..." << endl;
  capture_thread.join();
  return exit_code;
}
