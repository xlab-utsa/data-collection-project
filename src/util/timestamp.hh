// Wall-clock timestamps as plain integers.
//
// Used in this project by: app/video_sender.cc (Datagram send_ts),
// app/encoder.cc (encode times, RTT from ACKs, per-frame log) and
// app/decoder.cc (per-frame log, goodput timing). Only timestamp_us() is
// used by the apps.
//
// Key concept: these read std::chrono::system_clock, i.e. the wall clock
// (time since 1970-01-01 UTC). That makes timestamps from the sender and the
// receiver logs comparable, but only as well as the two machines' clocks are
// synchronized (e.g. by NTP/PTP), and the clock may jump when it is adjusted.
// A difference of two timestamps taken on the *same* machine, such as the
// RTT the sender computes from send_ts echoed back in an ACK, does not
// depend on clock synchronization.

#ifndef TIMESTAMP_HH
#define TIMESTAMP_HH

#include <cstdint>

/* nanoseconds since epoch */
uint64_t timestamp_ns();

/* microseconds since epoch */
uint64_t timestamp_us();

/* milliseconds since epoch */
uint64_t timestamp_ms();

#endif /* TIMESTAMP_HH */
