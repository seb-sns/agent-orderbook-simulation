#ifdef ENABLE_VIZ

#include "VizRecorder.h"
#include <cstddef>
#include <cstring>

namespace {
// Fixed-size file header, followed by nAgents VizAgentMeta records, then a
// stream of 32-byte VizEvents. eventCount/droppedEvents are patched on Stop.
struct VizFileHeader {
  char magic[8]; // "MKTVIZ01"
  std::uint32_t version;
  std::uint32_t nAgents;
  std::int64_t initialCash;
  std::int64_t initialUnits;
  double minPrice;
  double tickSize;
  std::uint64_t eventCount;
  std::uint64_t droppedEvents;
};
static_assert(sizeof(VizFileHeader) == 64, "VizFileHeader must stay 64 bytes");
} // namespace

bool VizRecorder::Start(const std::string &path, std::int64_t initialCash,
                        std::int64_t initialUnits, double minPrice,
                        double tickSize) {
  file_ = std::fopen(path.c_str(), "wb");
  if (!file_) {
    std::perror("VizRecorder: failed to open log file");
    return false;
  }
  std::setvbuf(file_, nullptr, _IOFBF, 1 << 20);

  VizFileHeader header{};
  std::memcpy(header.magic, "MKTVIZ01", 8);
  header.version = 1;
  header.nAgents = static_cast<std::uint32_t>(agents_.size());
  header.initialCash = initialCash;
  header.initialUnits = initialUnits;
  header.minPrice = minPrice;
  header.tickSize = tickSize;
  std::fwrite(&header, sizeof(header), 1, file_);
  if (!agents_.empty()) {
    std::fwrite(agents_.data(), sizeof(VizAgentMeta), agents_.size(), file_);
  }

  t0_ = std::chrono::steady_clock::now();
  running_ = true;
  writer_ = std::thread(&VizRecorder::WriterLoop, this);
  return true;
}

void VizRecorder::WriterLoop() {
  VizEvent event;
  while (true) {
    bool drained = false;
    while (events_.Pop(event)) {
      std::fwrite(&event, sizeof(event), 1, file_);
      ++recorded_;
      drained = true;
    }
    if (!drained) {
      if (!running_) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }
}

void VizRecorder::Stop() {
  if (!file_) {
    return;
  }
  running_ = false;
  if (writer_.joinable()) {
    writer_.join();
  }
  // Patch the counts now that the stream is complete.
  std::fseek(file_, offsetof(VizFileHeader, eventCount), SEEK_SET);
  std::fwrite(&recorded_, sizeof(recorded_), 1, file_);
  std::fwrite(&dropped_, sizeof(dropped_), 1, file_);
  std::fclose(file_);
  file_ = nullptr;
}

#endif // ENABLE_VIZ
