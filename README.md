# data-collect-system

A real-time **camera → VP9 → UDP → recorder** pipeline for collecting video
datasets. A sender (for example a Jetson board with a USB/MIPI camera)
captures raw frames, encodes them with VP9 at a target bitrate, and streams
them over UDP. A receiver (a server) reassembles and decodes them and writes
the decoded video to disk as raw `.y4m`.

The code started as a copy of
[YuxinLiu2023/video_capture-stream_v2](https://github.com/YuxinLiu2023/video_capture-stream_v2)
("pipeline v2"), which builds on Microsoft's
[Ringmaster](https://github.com/microsoft/ringmaster) video-streaming research
tool. Ringmaster streams a pre-recorded `.y4m` file. This project adds live
V4L2 capture on a separate thread, on-disk recording on the receiver, and
bitrate feedback in the ACKs, as groundwork for an adaptive bitrate controller.

## How it works

```
 SENDER (camera machine)                          RECEIVER (server)
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
   converted to planar I420 directly into a free slot of the `FrameRing`.
   If the encoder falls behind and the ring fills, new frames are dropped
   and counted.
3. **Encode and send** (`video_sender.cc`, `encoder.cc`). A timer fires once
   per frame interval. On each tick the oldest frame in the ring is encoded
   (libvpx VP9, real-time single-pass CBR with no automatic key frames) and
   split into datagrams of at most MTU − 28 − 17 bytes.
4. **Reliability.** The receiver ACKs every datagram. When an ACK arrives,
   unacked datagrams sent before it are queued for retransmission, up to 3
   times and at most once per RTT. If the oldest unacked datagram is more
   than 1 s old, the sender gives up, clears its queues and forces a key frame.
5. **Feedback.** About once a second the receiver piggy-backs its measured
   goodput (kbps × 100) on an ACK. The sender currently only logs it, as
   input for the planned adaptive bitrate controller.
6. **Decode and record** (`decoder.cc`). Complete frames go in order to a
   worker thread. The worker decodes them, appends them to
   `data/output_YYYYmmdd_HHMMSS.y4m` and, with `--lazy 0`, also displays
   them. On Ctrl+C the worker finishes the queued frames and closes the file.

## Repository layout

| Path | Contents |
|------|----------|
| `src/app/video_sender.cc` | sender `main`: options, handshake, event loop |
| `src/app/capture.{hh,cc}` | V4L2 capture thread and preview window |
| `src/app/frame_ring.{hh,cc}` | single-producer/single-consumer ring of raw frames |
| `src/app/encoder.{hh,cc}` | VP9 encoder, packetization, ACK/RTT/retransmission logic |
| `src/app/video_receiver.cc` | receiver `main`: options, handshake, receive loop |
| `src/app/decoder.{hh,cc}` | frame reassembly, goodput measurement, decode/record/display worker |
| `src/app/protocol.{hh,cc}` | wire format: `Datagram`, `AckMsg`, `ConfigMsg` |
| `src/app/common.{hh,cc}` | stop-signal handling, handshake helper |
| `src/video/` | `RawImage` (vpx_image wrapper), SDL display, Y4M reader, V4L2 class, `webcam` viewer (from Ringmaster) |
| `src/util/` | sockets, poller, timerfd, serialization and other helpers (from Ringmaster) |
| `tools/capture_to_disk.c` | standalone recorder: camera → `.y4m` with preview, no network (pipeline v1) |

## Building

Dependencies (Ubuntu / JetPack names):

```bash
sudo apt install build-essential autoconf automake pkg-config \
     libvpx-dev libsdl2-dev libavutil-dev libswscale-dev
```

Build:

```bash
./autogen.sh
./configure
make -j
```

This produces `src/app/video_sender`, `src/app/video_receiver` and
`src/video/webcam`. The standalone recorder is built separately; see the
comment at the top of `tools/capture_to_disk.c`.

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

`<port>` must match on both ends. Stop either side with Ctrl+C. On the
sender, pressing `q` in the preview window also stops it.

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

- **Bitrate is fixed.** The goodput feedback and RTT samples are collected
  but not yet used to change the encoder bitrate.
- **Latency can grow under load.** The ring is FIFO and holds up to 500
  frames (fewer at 4K/8K, to stay within about 2 GiB). If the encoder is
  slower than the camera, queued frames age before they are encoded. Watch
  the "Frames queued in ring" line in the sender's stats.
- **Display rate.** The receiver window uses vsync. With `--lazy 0` at fps
  above the monitor's refresh rate, decoding is held back by the display, so
  use `--lazy 1` for recording.
- **No reorder tolerance.** Any datagram older than one that was acked is
  treated as lost, which is fine on most LAN paths.

## Roadmap (pipeline v3)

- [ ] ACK-based adaptive bitrate from measured throughput and RTT
- [ ] Multi-threaded and GPU (NVENC on Jetson) encoding
- [ ] Store the encoded stream on the receiver without decoding

## Changes from video_capture-stream_v2

The full history of the original repository is preserved in this repository.
On top of it:

- **Builds again.** Upstream `encoder.cc` referenced an undeclared member
  (`reset_rtt_sample_buffer_`), so `main` did not compile.
- **Camera buffers.** Every V4L2 buffer is now mapped and looked up by
  index. Before, only the last mapping was kept and frames were read at
  `last_mapping + offset`, outside the mapped region for most buffers.
- **Shutdown.** Ctrl+C used to crash the sender (`poll` threw on EINTR). On
  the receiver it killed the process with up to 32 MiB of video still in the
  write buffer, and the decoder thread was never joined. Both programs now
  stop cleanly, and the receiver finishes and closes the recording.
- **Display while recording.** With `--lazy 0` the window used to stay empty:
  each decoded frame was taken from libvpx for the file and was then gone. The
  same image is now used for both.
- **Large frames.** The decode buffer was a fixed 1 MB, which 4K/8K key
  frames could exceed. It now grows to fit.
- **Handshake.** The receiver re-sends its request until the sender replies,
  and stray or duplicate config packets are ignored instead of crashing the
  receiver. Malformed or truncated messages are dropped.
- **Capture path.** Frames are converted straight into ring slots with no
  intermediate copies. The preview is rate-limited to about 30 Hz. The ring
  has one lock instead of a global lock plus one per slot. Pressing `q` now
  stops the sender; before, it stalled it.
- **Y4M header.** The aspect ratio is now `A1:1` (square pixels) instead of
  `A128:117`, which made players stretch the image.
- **Standalone recorder.** `tools/capture_to_disk.c` writes valid planar
  4:2:2 (`C422`) Y4M. It used to write packed YUYV under a 4:2:0 header.
- **Options.** `-d`, `--mtu`, `--no-preview`, `-o`, `-v`, `--record-dir` and
  `--help` were added, and usage texts match the real options.
- **Repository.** Generated autotools files, object files, libraries and
  editor settings were removed from git and added to `.gitignore`. Dead code
  and commented-out blocks were removed.
