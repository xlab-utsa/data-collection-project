// Implementation of FrameRing (frame_ring.hh): a fixed array of frame-sized
// slots used as a circular queue between the capture thread (producer) and
// the encoder thread (consumer).
//
// Indices: head_ is where the producer writes next, tail_ is where the
// consumer reads next, count_ is how many committed frames sit in between.
// Both advance modulo the number of slots, so slots are reused forever.

#include <algorithm>
#include <stdexcept>

#include "frame_ring.hh"

using namespace std;

FrameRing::FrameRing(const size_t num_slots, const size_t frame_size)
  : frame_size_(frame_size),
    slots_(num_slots, vector<uint8_t>(frame_size))
{
  if (num_slots == 0 or frame_size == 0) {
    throw runtime_error("FrameRing: slots and frame size must be positive");
  }
}

// e.g. 1080p I420 (~3 MB/frame) gets the full 500 slots, while 4K and 8K
// frames get fewer so the ring stays within the memory budget.
size_t FrameRing::capacity_for(const size_t frame_size,
                               const size_t memory_budget,
                               const size_t max_slots,
                               const size_t min_slots)
{
  const size_t fit = frame_size ? memory_budget / frame_size : max_slots;
  return clamp(fit, min_slots, max_slots);
}

// The producer and consumer never touch the same slot at the same time:
// the producer only writes slots_[head_] while count_ < capacity, and the
// consumer only reads slots_[tail_] while count_ > 0. The mutex only guards
// the indices, so the (large) memcpy into and out of a slot happens unlocked.

uint8_t * FrameRing::begin_write()
{
  lock_guard<mutex> lock(mtx_);
  if (count_ == slots_.size()) {
    dropped_++;
    return nullptr;
  }
  return slots_[head_].data();
}

void FrameRing::commit_write()
{
  lock_guard<mutex> lock(mtx_);
  head_ = (head_ + 1) % slots_.size();
  count_++;
}

const uint8_t * FrameRing::try_begin_read()
{
  lock_guard<mutex> lock(mtx_);
  if (count_ == 0) {
    return nullptr;
  }
  return slots_[tail_].data();
}

void FrameRing::end_read()
{
  lock_guard<mutex> lock(mtx_);
  tail_ = (tail_ + 1) % slots_.size();
  count_--;
}

size_t FrameRing::size() const
{
  lock_guard<mutex> lock(mtx_);
  return count_;
}

size_t FrameRing::dropped() const
{
  lock_guard<mutex> lock(mtx_);
  return dropped_;
}
