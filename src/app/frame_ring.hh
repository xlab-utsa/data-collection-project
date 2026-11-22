#ifndef FRAME_RING_HH
#define FRAME_RING_HH

// Fixed-size ring of raw I420 frames handed from the capture thread
// (single producer) to the encoder/streaming thread (single consumer).
//
// Slots are preallocated, and the producer writes straight into a slot
// (begin_write / commit_write) and the consumer reads straight out of one
// (try_begin_read / end_read), so a frame is not copied an extra time.
// If the ring is full the producer drops the new frame; frames already queued
// are never overwritten.

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

class FrameRing
{
public:
  // 'frame_size' is the size in bytes of one I420 frame (W * H * 3 / 2).
  FrameRing(const size_t num_slots, const size_t frame_size);

  // Choose a capacity that keeps the ring under 'memory_budget' bytes, but
  // never more than 'max_slots' or fewer than 'min_slots'.
  static size_t capacity_for(const size_t frame_size,
                             const size_t memory_budget = 2ULL << 30,
                             const size_t max_slots = 500,
                             const size_t min_slots = 8);

  // ---- producer side ----
  // Returns the slot to fill, or nullptr if the ring is full (caller drops
  // the frame). Each non-null result must be followed by commit_write().
  uint8_t * begin_write();
  void commit_write();

  // ---- consumer side ----
  // Returns the oldest queued frame, or nullptr if none is queued. Each
  // non-null result must be followed by end_read() once the data is copied.
  const uint8_t * try_begin_read();
  void end_read();

  size_t frame_size() const { return frame_size_; }
  size_t capacity() const { return slots_.size(); }
  size_t size() const;

  // number of frames the producer has dropped because the ring was full
  size_t dropped() const;

  FrameRing(const FrameRing &) = delete;
  FrameRing & operator=(const FrameRing &) = delete;

private:
  size_t frame_size_;
  std::vector<std::vector<uint8_t>> slots_;

  mutable std::mutex mtx_ {};
  size_t head_ {0};    // next slot the producer writes
  size_t tail_ {0};    // next slot the consumer reads
  size_t count_ {0};   // committed frames waiting to be read
  size_t dropped_ {0};
};

#endif /* FRAME_RING_HH */
