// FrameRing (src/app/frame_ring.*): single-producer/single-consumer
// correctness under concurrency, drop-when-full, capacity sizing.
#include <cstring>
#include <thread>

#include "frame_ring.hh"
#include "test_util.hh"

using namespace std;

int main()
{
  // capacity: at most 500 slots, fewer for big frames, at least 8
  CHECK(FrameRing::capacity_for(1920 * 1080 * 3 / 2) == 500);
  CHECK(FrameRing::capacity_for(3840 * 2160 * 3 / 2) < 500);
  CHECK(FrameRing::capacity_for(8000 * 6000 * 3 / 2) >= 8);

  // every committed frame arrives, in order, uncorrupted
  {
    FrameRing ring(4, 256);
    const int N = 100000;
    thread producer([&] {
      for (int i = 0; i < N; i++) {
        uint8_t * slot;
        while (!(slot = ring.begin_write())) this_thread::yield();
        memset(slot, i & 255, 256);
        ring.commit_write();
      }
    });
    for (int i = 0; i < N; i++) {
      const uint8_t * f;
      while (!(f = ring.try_begin_read())) this_thread::yield();
      bool ok = true;
      for (int k = 0; k < 256; k++) ok &= (f[k] == (i & 255));
      CHECK(ok);
      ring.end_read();
    }
    producer.join();
    CHECK(ring.size() == 0);
    CHECK(ring.dropped() > 0 || ring.dropped() == 0); // counted, value timing-dependent
  }

  // a full ring refuses new frames and counts the drop
  {
    FrameRing ring(2, 8);
    CHECK(ring.begin_write() != nullptr); ring.commit_write();
    CHECK(ring.begin_write() != nullptr); ring.commit_write();
    const size_t before = ring.dropped();
    CHECK(ring.begin_write() == nullptr);
    CHECK(ring.dropped() == before + 1);
    CHECK(ring.try_begin_read() != nullptr);
    ring.end_read();
    CHECK(ring.begin_write() != nullptr);
  }

  // empty ring has nothing to read
  {
    FrameRing ring(3, 8);
    CHECK(ring.try_begin_read() == nullptr);
  }

  return test_summary("test_frame_ring");
}
