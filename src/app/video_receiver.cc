// video_receiver: runs on the server that collects the video.
//
//   1. asks the sender for a target bitrate and learns width/height/fps;
//   2. receives Datagrams, ACKs every one of them (piggy-backing a goodput
//      measurement about once per second) and reassembles frames;
//   3. a worker thread decodes the frames (VP9), appends them to
//      <record dir>/output_<timestamp>.y4m and optionally shows them.
//
// Stop with Ctrl+C; the recording is flushed and closed on exit.

#include <getopt.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "common.hh"
#include "conversion.hh"
#include "decoder.hh"
#include "poller.hh"
#include "protocol.hh"
#include "udp_socket.hh"

using namespace std;

namespace {

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <sender host> <port> --cbr <kbps> [options]\n\n"
  "  --cbr <kbps>             target bitrate requested from the sender (required)\n"
  "  --lazy <level>           0: decode, record and display frames (default)\n"
  "                           1: decode and record frames, no display window\n"
  "                           2: only reassemble frames (no decoding/recording)\n"
  "  --record-dir <dir>       where to write the .y4m recording (default: ./data)\n"
  "  -o, --output <file>      write per-frame decoder stats (CSV) to <file>\n"
  "  -v, --verbose            log every datagram\n"
  << endl;
}

} // namespace

int main(int argc, char * argv[])
{
  unsigned int target_bitrate = 0; // kbps
  int lazy_level = Decoder::DECODE_DISPLAY;
  string record_dir = "./data";
  string output_path;
  bool verbose = false;

  // ===== argument parsing =====
  const option cmd_line_opts[] = {
    {"cbr",        required_argument, nullptr, 'C'},
    {"lazy",       required_argument, nullptr, 'L'},
    {"record-dir", required_argument, nullptr, 'R'},
    {"output",     required_argument, nullptr, 'o'},
    {"verbose",    no_argument,       nullptr, 'v'},
    {"help",       no_argument,       nullptr, 'h'},
    {nullptr,      0,                 nullptr,  0 },
  };

  try {
    while (true) {
      const int opt = getopt_long(argc, argv, "o:vh", cmd_line_opts, nullptr);
      if (opt == -1) {
        break;
      }

      switch (opt) {
        case 'C': target_bitrate = strict_stoi(optarg); break;
        case 'L': lazy_level = strict_stoi(optarg); break;
        case 'R': record_dir = optarg; break;
        case 'o': output_path = optarg; break;
        case 'v': verbose = true; break;
        case 'h': print_usage(argv[0]); return EXIT_SUCCESS;
        default:  print_usage(argv[0]); return EXIT_FAILURE;
      }
    }
  } catch (const exception & e) {
    cerr << "Invalid argument: " << e.what() << endl;
    return EXIT_FAILURE;
  }

  if (optind != argc - 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }
  if (target_bitrate == 0) {
    cerr << "--cbr <kbps> is required" << endl;
    return EXIT_FAILURE;
  }

  const string host = argv[optind];
  const auto port = narrow_cast<uint16_t>(strict_stoi(argv[optind + 1]));

  install_stop_signal_handlers();

  // ===== handshake =====
  const Address peer_addr {host, port};
  UDPSocket udp_sock;
  udp_sock.connect(peer_addr);
  cerr << "Sender: " << peer_addr.str()
       << ", local address: " << udp_sock.local_address().str() << endl;

  // ask for 'target_bitrate'; repeat the request until the sender answers
  // (it may not be running yet, or a packet may get lost)
  const ConfigMsg request(0, 0, 0, target_bitrate);
  const auto send_request = [&] {
    udp_sock.send(request.serialize_to_string());
  };
  send_request();

  const auto reply = wait_for_config_msg(udp_sock, send_request);
  if (not reply) {
    return EXIT_SUCCESS; // interrupted while waiting
  }
  const ConfigMsg & config = reply->second;
  cerr << "Stream config: " << config.width << "x" << config.height
       << " @ " << config.frame_rate << " fps, "
       << config.target_bitrate << " kbps" << endl;

  int exit_code = EXIT_SUCCESS;
  {
    Decoder decoder(config.width, config.height, lazy_level,
                    config.frame_rate, output_path, record_dir);
    decoder.set_verbose(verbose);

    udp_sock.set_blocking(false);

    Poller poller;
    poller.register_event(udp_sock, Poller::In,
      [&]()
      {
        while (true) {
          const auto raw_data = udp_sock.recv();
          if (not raw_data) {
            break; // EWOULDBLOCK: drained
          }

          Datagram datagram;
          if (not datagram.parse_from_string(*raw_data)) {
            continue; // e.g. a duplicate ConfigMsg reply; not a datagram
          }

          // ACK every datagram; about once a second also report goodput
          const auto bitrate = decoder.take_pending_bitrate();
          const AckMsg ack(datagram, bitrate.has_value(), bitrate.value_or(0));
          udp_sock.send(ack.serialize_to_string());

          if (verbose) {
            cerr << "Acked datagram: frame_id=" << datagram.frame_id
                 << " frag_id=" << datagram.frag_id << endl;
          }

          decoder.add_datagram(move(datagram));

          // hand over every frame that is now complete, in order
          while (decoder.next_frame_complete()) {
            decoder.consume_next_frame();
          }
        }
      }
    );

    try {
      while (not g_stop_requested) {
        poller.poll(200); // wake up regularly to notice a stop request
      }
    } catch (const exception & e) {
      cerr << "Receiver error: " << e.what() << endl;
      exit_code = EXIT_FAILURE;
    }

    cerr << "Stopping; finishing queued frames..." << endl;
  } // ~Decoder: worker drains its queue and closes the .y4m file

  return exit_code;
}
