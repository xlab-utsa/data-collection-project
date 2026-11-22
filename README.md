# data-collect-system

A real-time video data-collection system: **edge devices** (NVIDIA Jetson
boards with cameras) capture high-quality video, compress it, packetize it
and stream it over the network to a central **storage server**, which
reassembles, decodes and records it.

Project site: <https://utkrshkmr.github.io/data-collect-system/>

- [Goal](#goal)
- [How the current code works](#how-the-current-code-works)
- [Codebase guide](#codebase-guide)
- [Setting up the environment](#setting-up-the-environment)
- [Building](#building)
- [Testing](#testing)
- [Running](#running)
- [Known limitations](#known-limitations)
- [Roadmap](#roadmap)
- [History](#history)

## Goal

The finished system should:

1. **Capture** frames continuously from a camera on an edge device. Some CPU
   cores pick frames up from the camera into a ring buffer, which is written,
   read and freed continuously.
2. **Encode on the GPU.** The Jetson boards this targets have no hardware
   video encoder (NVENC), so compression work has to run on the GPU (CUDA)
   rather than on dedicated encoder hardware.
3. **Package and transmit** encoded frames on other CPU cores.
4. **Stagger the stages** so they overlap. While the GPU encodes frame N, the
   capture cores already grab frame N+1 and the transmit cores send frame
   N−1.
5. **Share the server fairly.** Many devices stream to one storage server and
   compete for its bandwidth, so congestion happens. Congestion has to be
   measured and reported between server and devices.
6. **Adapt.** Each device adjusts everything that can be adjusted (bitrate,
   frame rate, resolution, or dropping frames) to fit what the network and
   server can take, while losing as little useful data as possible.

The current code is a working single-device baseline. It captures, encodes
with VP9 on the CPU (libvpx), streams over UDP with ACKs and retransmission,
and records on the server. The [roadmap](#roadmap) lists the steps from here
to the goal.

## How the current code works

```
 SENDER (edge device)                             RECEIVER (storage server)
 ─────────────────────────                        ─────────────────────────
 capture thread                                   main thread
   V4L2 camera (YUYV 4:2:2, mmap)                   UDP recv ──► ACK every datagram
     │  swscale                                       │          (+ goodput once/s)
     ├──► 640x480 RGB565 preview window               ▼
     ▼                                              reassemble frames
   FrameRing  (I420 slots, drop if full)            (skip ahead to a key frame
     │                                               if a frame cannot be completed)
 main thread (event loop)                             │
   timer @ fps ─► VP9 encode (libvpx, CBR)            ▼
     ▼                                            worker thread
   split into MTU-sized Datagrams ──── UDP ─────►   VP9 decode
   unacked map ◄── ACKs ◄──────────────────────     ├──► data/output_<time>.y4m
     ├─ RTT (min / EWMA)                            └──► SDL window (--lazy 0)
     ├─ retransmit datagrams older than an ACK
     └─ give up after 1 s ─► force a key frame
```

1. **Handshake.** The receiver sends a `ConfigMsg` that carries only the
   target bitrate, and re-sends it every 500 ms until the sender answers. The
   sender replies with width, height, fps and bitrate.
2. **Capture** (`capture.cc`). The camera delivers packed YUYV. Each frame is
   converted to planar I420 (with libswscale) directly into a free slot of
   the `FrameRing`. If the encoder falls behind and the ring is full, the new
   frame is dropped and counted.
3. **Encode and send** (`video_sender.cc`, `encoder.cc`). A timer fires once
   per frame interval. Each tick takes the oldest frame from the ring and
   encodes it, which also happens on the main thread: libvpx VP9, real-time
   single-pass CBR with key frames only on demand. The encoded frame is then
   split into datagrams of at most MTU − 28 − 17 bytes.
4. **Reliability.** The receiver ACKs every datagram. When an ACK arrives,
   unacked datagrams sent before it are queued for retransmission, up to 3
   times and at most once per RTT. If the oldest unacked datagram is more
   than 1 s old, the sender gives up, clears its queues and forces a key frame.
5. **Feedback.** About once a second the receiver piggy-backs its measured
   goodput (kbps × 100) on an ACK. The sender only logs it: no adaptation
   exists yet.
6. **Decode and record** (`decoder.cc`). Complete frames go in order to a
   worker thread. The worker decodes them, appends them to
   `data/output_YYYYmmdd_HHMMSS.y4m` and, with `--lazy 0`, also displays
   them. On Ctrl+C it finishes the queued frames and closes the file.

Threads today: the sender has a capture thread, a preview thread, and one
main thread that encodes and does all network I/O. The receiver has a main
(network) thread and a decode/record worker.

## Codebase guide

Every source file starts with a comment explaining its role, and the
functions are documented in place. Here is where to start.

| Path | What it is |
|------|-----------|
| `src/app/video_sender.cc` | sender `main`: option parsing, handshake, the event loop (frame timer, socket readable/writable, stats timer) |
| `src/app/capture.{hh,cc}` | camera capture thread (V4L2 mmap streaming), YUYV→I420 conversion, preview window |
| `src/app/frame_ring.{hh,cc}` | ring of raw frames between the capture thread and the encoder |
| `src/app/encoder.{hh,cc}` | VP9 encoder (libvpx), packetization, unacked tracking, RTT, retransmission, key-frame recovery |
| `src/app/video_receiver.cc` | receiver `main`: option parsing, handshake, receive/ACK loop |
| `src/app/decoder.{hh,cc}` | frame reassembly, skip-ahead recovery, goodput measurement, decode/record/display worker |
| `src/app/protocol.{hh,cc}` | wire format: `Datagram`, `AckMsg`, `ConfigMsg` |
| `src/app/common.{hh,cc}` | stop-signal handling, handshake helper |
| `src/video/` | `RawImage` (vpx_image wrapper), SDL display, Y4M reader, V4L2 class and a `webcam` viewer |
| `src/util/` | sockets, poller, timerfd, big-endian serialization, file descriptors, mmap and other helpers |
| `tools/capture_to_disk.c` | standalone recorder: camera → `.y4m` with preview, no network |
| `tests/` | unit tests that need only a C++17 compiler |
| `docs/` | the project website (GitHub Pages) |

Suggested reading order: `protocol.hh`, then `video_sender.cc` with
`capture.cc`, `frame_ring.cc` and `encoder.cc`, then `video_receiver.cc` with
`decoder.cc`. `src/util` and `src/video` are the plumbing underneath.

**Wire protocol** (all integers big-endian; see `protocol.hh`):

| Message | Direction | Fields |
|---------|-----------|--------|
| `Datagram` | sender → receiver | frame_id, frame_type, frag_id, frag_cnt, send_ts, payload |
| `AckMsg` | receiver → sender | type, frame_id, frag_id, send_ts (echoed), has_bitrate_feedback, measured_kbps_x100 |
| `ConfigMsg` | both | type, width, height, frame_rate, target_bitrate |

## Setting up the environment

Ubuntu 22.04 or later, or JetPack on a Jetson:

```bash
sudo apt install build-essential autoconf automake pkg-config \
     libvpx-dev libsdl2-dev libavutil-dev libswscale-dev
```

Optional tools:

- **No camera?** Use a virtual V4L2 camera (`v4l2loopback`) fed by FFmpeg.
  See [Testing](#testing).
- **For roadmap work:** the CUDA toolkit (part of JetPack on Jetson; on a PC
  from NVIDIA's site), and Rust via [rustup](https://rustup.rs) for the Rust
  items.
- **For viewing recordings:** `sudo apt install ffmpeg`.

## Building

```bash
./autogen.sh
./configure
make -j
```

This produces `src/app/video_sender`, `src/app/video_receiver` and
`src/video/webcam`. The standalone recorder is built separately; see the
comment at the top of `tools/capture_to_disk.c`. The code is compiled with
`-Wall -Wextra -Werror -Weffc++`, so warnings fail the build.

## Testing

**Unit tests.** These need no camera and no media libraries:

```bash
make -C tests run
```

They cover the wire protocol (round trips, truncated and random input), the
frame ring (ordering under two threads, drop-when-full) and the handshake
over loopback (including a receiver that starts before the sender). Add a
test here for every module you change.

**End to end on one machine with a virtual camera:**

```bash
sudo apt install v4l2loopback-dkms
```

```bash
sudo modprobe v4l2loopback devices=1 video_nr=10 exclusive_caps=1
```

```bash
ffmpeg -re -f lavfi -i testsrc2=size=1280x720:rate=60 -pix_fmt yuyv422 -f v4l2 /dev/video10
```

Then, in two other terminals:

```bash
./src/app/video_sender 9000 -w 1280 -h 720 -r 60 -d /dev/video10 --no-preview
```

```bash
./src/app/video_receiver 127.0.0.1 9000 --cbr 6000 --lazy 1
```

Stop both with Ctrl+C. Check that a `.y4m` file appeared in `data/` and that
it plays (see [Viewing a recording](#viewing-a-recording)).

**Network conditions.** To see loss recovery, add loss or delay on the
loopback interface with `tc netem`, and remove it when done:

```bash
sudo tc qdisc add dev lo root netem loss 2% delay 20ms
```

```bash
sudo tc qdisc del dev lo root
```

**On a Jetson,** use the real camera (`/dev/video0`) and run the receiver on
another machine on the same network.

## Running

Start the sender first, on the camera machine:

```bash
./src/app/video_sender <port> -w <width> -h <height> -r <fps> [options]
```

| Option | Meaning |
|--------|---------|
| `-w`, `-h`, `-r` | capture width, height, fps (required; see table below) |
| `-d <path>` | camera device (default `/dev/video0`) |
| `--mtu <bytes>` | MTU for sizing datagrams (512–1500, default 1500) |
| `--no-preview` | do not open the preview window (for headless boards) |
| `-o <file>` | per-frame CSV: `frame_id,target_kbps,size_bytes,encode_start_us,encode_end_us` |
| `-v` | log every datagram and ACK |
| `--help` | usage (note that `-h` means height) |

Then start the receiver on the server:

```bash
./src/app/video_receiver <sender ip> <port> --cbr <kbps> [options]
```

| Option | Meaning |
|--------|---------|
| `--cbr <kbps>` | target bitrate to ask the sender for (required) |
| `--lazy 0` | decode, record and display (default) |
| `--lazy 1` | decode and record, no window (recommended on servers) |
| `--lazy 2` | only reassemble frames; nothing is decoded or recorded |
| `--record-dir <dir>` | where the `.y4m` is written (default `./data`, created if missing) |
| `-o <file>` | per-frame CSV: `frame_id,size_bytes,decoded_us` |
| `-v` | log every datagram |

### Supported modes (sender, limited by the camera / V4L2)

The frame rate must be one of {120, 60, 50, 20, 14, 3} and must not exceed
the limit for the resolution:

| Width | Height | FPS   | Remark |
|-------|--------|-------|--------|
| 1280  | 720    | ≤ 120 | 720p   |
| 1920  | 1080   | ≤ 60  | 1080p  |
| 2000  | 1500   | ≤ 50  | 2K     |
| 3840  | 2160   | ≤ 20  | 4K     |
| 4000  | 3000   | ≤ 14  | 4:3 4K |
| 8000  | 6000   | ≤ 3   | 8K     |

### Practical bitrate ranges (receiver `--cbr`)

| Width | Height | FPS | Bitrate range |
|-------|--------|-----|---------------|
| 1280  | 720    | 60  | 4–8 Mbps      |
| 1280  | 720    | 120 | 6–12 Mbps     |
| 1920  | 1080   | 50  | 5–10 Mbps     |
| 1920  | 1080   | 60  | 6–12 Mbps     |
| 2000  | 1500   | 50  | 8–14 Mbps     |
| 3840  | 2160   | 20  | 12–24 Mbps    |
| 4000  | 3000   | 14  | 12–24 Mbps    |
| 8000  | 6000   | 3   | 20–40 Mbps    |

### Viewing a recording

The `.y4m` files are uncompressed I420 (for example, 1080p60 is about 186 MB/s). To
compress and play one:

```bash
ffmpeg -i data/output_20250611_120000.y4m -c:v libx264 -preset fast -crf 23 output.mp4
```

```bash
ffplay output.mp4
```

## Known limitations

These are the starting points for the roadmap:

- **One thread for encoding and sending.** The sender encodes and does all
  network I/O on the same thread, so a slow encode delays ACK processing and
  sending, and nothing overlaps.
- **CPU-only encoding.** Encoding runs on the CPU through libvpx.
- **The ring uses a mutex and is FIFO.** If the encoder is slower than the
  camera, frames queue up and age, so latency grows until the ring fills.
- **Bitrate, fps and resolution are fixed.** The goodput feedback is only
  logged.
- **One device per receiver process.**
- **No per-stage latency measurement.** There are only coarse per-frame logs
  (`-o`).
- **No reorder tolerance.** Any datagram older than one that was acked is
  treated as lost.
- **The receiver display uses vsync.** With `--lazy 0` at fps above the
  monitor's refresh rate, use `--lazy 1` for recording.
- **Not yet validated on Jetson hardware.**

## Roadmap

Each item says what to build, why, how we know it is done, and what to read
up on. Items roughly build on each other, but several can proceed in
parallel. Measure before and after every change; item 1 gives you the tools
for that.

### 1. Per-stage latency benchmarking

- **Goal:** know where the time goes. Timestamp every frame at each stage on
  both machines: capture, ring wait, conversion, encode, send queue,
  transmit, network, reassembly, decode and write. Report per-stage and
  end-to-end latency distributions.
- **Done when:** one command turns a sender log and a server log into a
  per-stage table of p50, p95 and p99, and end-to-end latency is reported
  correctly when the two machines' clocks are synchronized.
- **Pointers:** `clock_gettime` (`CLOCK_MONOTONIC` vs `CLOCK_REALTIME`),
  chrony and PTP clock synchronization, percentiles vs averages, tail
  latency.

### 2. Staged pipeline and latency hiding

- **Goal:** split the sender into separate capture, encode and
  packetize/transmit stages on separate threads and cores, so the stages
  overlap. Then use the measurements from item 1 to find the slowest stage
  and break it up further, so more of the work hides behind other work.
- **Done when:** sustained throughput is limited by the slowest stage rather
  than the sum of all stages, the event loop never blocks on encoding, and
  the per-stage numbers show the overlap. The before/after comparison is
  documented.
- **Pointers:** pipeline parallelism, Amdahl's law, `pthread_setaffinity_np`
  and `isolcpus`, `eventfd`, VP9 tiles and row multi-threading,
  double-buffering, packet pacing.

### 3. A better ring buffer

- **Goal:** a ring between capture and encode that never blocks either side,
  keeps latency bounded when the consumer falls behind, and avoids extra
  copies of frame data.
- **Done when:** a stress test with two threads passes, latency stays
  bounded when the encoder is artificially slowed, and the ring's overhead is
  measured.
- **Pointers:** single-producer/single-consumer queues, `std::atomic`
  memory ordering (acquire/release), false sharing and cache-line alignment,
  drop policies (drop newest vs drop oldest).

### 4. GPU offload on Jetson (no NVENC)

- **Goal:** move per-frame pixel work (colour conversion, scaling) onto the
  GPU first, then the compression work itself, so the CPU cores are free for
  capture and networking.
- **Done when:** GPU output matches a CPU reference (bit-exact for
  conversion), the encode stage keeps up at the target modes on a Jetson, and
  CPU load drops measurably.
- **Pointers:** CUDA streams and asynchronous copies, pinned and unified
  memory on Jetson, NVIDIA NPP and VPI, nvJPEG, which Jetson modules lack
  NVENC, motion estimation on GPUs.

### 5. Congestion feedback and adaptation

- **Goal:** the server measures what it receives (goodput, frame loss,
  queuing delay) and reports it to the device. The device reports its own
  state to the server. The device then adapts bitrate, frame rate and
  resolution, and drops frames when needed, so it backs off under
  congestion and recovers afterwards.
- **Done when:** under emulated congestion (`tc netem`, bandwidth limits) the
  stream adapts within a bounded time without oscillating, and recordings
  stay decodable. The receiver starts a new recording file when the
  resolution changes.
- **Pointers:** AIMD, Google Congestion Control (GCC), RFC 8698 (NADA),
  RFC 8888 (RTP congestion-control feedback), libvpx
  `vpx_codec_enc_config_set`, spatial and temporal scalability (SVC).

### 6. Multi-device storage server

- **Goal:** one server process that receives from many devices at once and
  shares its bandwidth fairly among them, telling each device its budget.
- **Done when:** several senders stream to one server at the same time, each
  is recorded separately, and the bandwidth split follows the chosen
  fairness policy as devices join and leave.
- **Pointers:** max-min fairness, `epoll`, per-connection session state,
  device identity and registration.

### 7. Storage without decoding

- **Goal:** let the server store the compressed stream directly, with
  decoding optional, so it can serve many devices cheaply.
- **Done when:** recordings play back in standard players and contain
  timestamps.
- **Pointers:** the IVF container, WebM/Matroska (libwebm).

### 8. Building parts in Rust

- **Goal:** move the components that benefit most from memory safety and
  safe concurrency to Rust, while keeping V4L2, CUDA and libvpx code in
  C/C++. Candidates: parsing of untrusted network input (the wire protocol),
  the storage server, the congestion and fairness logic, and queues shared
  between threads.
- **Done when:** a Rust crate builds with `cargo`, links into the existing
  binaries through a C ABI (or the server is a Rust binary speaking the same
  wire format), and has parity tests that check it against the C++
  implementation.
- **Pointers:** `extern "C"` and `#[no_mangle]`, `cbindgen`, the `cxx`
  crate, `bindgen` and the `cc` crate in `build.rs`, the `staticlib` crate
  type, fuzzing with `cargo fuzz`, and `tokio` for the server.

### 9. Hardening and operations

- **Goal:** run reliably for long periods on real devices.
- **Done when:** a long soak test on a Jetson passes, metrics are exported,
  and the transport is encrypted and authenticated.
- **Pointers:** DTLS or QUIC, Prometheus exporters, CI with GitHub Actions,
  sanitizers (ASan, TSan, UBSan).

## History

This project started as a copy of
[YuxinLiu2023/video_capture-stream_v2](https://github.com/YuxinLiu2023/video_capture-stream_v2),
which builds on Microsoft's [Ringmaster](https://github.com/microsoft/ringmaster)
(VP9 over UDP with per-datagram ACKs). The original commit history is kept
in this repository. On top of it, the code was refactored, commented and
fixed:

- **Builds again.** Upstream `encoder.cc` referenced an undeclared member,
  so it did not compile.
- **Camera buffers.** V4L2 used to keep only the last buffer mapping and read
  frames outside it. Every buffer is now mapped and looked up by index.
- **Large frames.** The fragment count was wrong for frames larger than
  about 2 MB, so the last datagram exceeded the MTU. Large key frames at
  4K/8K are affected.
- **Shutdown.** Ctrl+C used to crash the sender. On the receiver it lost the
  buffered recording and never joined the decoder thread. Both programs now
  shut down cleanly.
- **Display while recording.** With `--lazy 0` the window used to stay
  empty: each decoded frame was used for the file and was then gone.
- **Decode buffer.** The fixed 1 MB buffer overflowed on large key frames.
- **Handshake.** It now retries, and malformed or stray packets no longer
  crash either side.
- **Y4M output.** Recordings now have square-pixel aspect, and the
  standalone recorder writes valid 4:2:2 Y4M.
- **Repository.** Generated build files were removed from git, a
  `.gitignore` was added, and unit tests were added.
