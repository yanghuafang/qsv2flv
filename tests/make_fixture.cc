#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "support/media.h"
#include "support/qsv_builder.h"

/// \file
/// Writes a synthetic `.qsv` file to a path on disk.
///
/// The same fixture the unit tests build in memory, made durable so the things
/// that cannot link against tests/support can use it too: scripts/check-cli.sh
/// runs a real conversion through the installed binary, and anyone who wants to
/// try the GUI without owning an iQIYI download can produce something to drop
/// on it.
///
/// Not installed, and not a converter — it only ever emits the synthetic
/// MPEG-2 clip from tests/support/media.cc wrapped as a version 1 container.

namespace {

constexpr const char* kUsage =
    "Usage: qsv2flv-make-fixture <out.qsv> [segments] [frames-per-segment]\n"
    "\n"
    "Write a synthetic version 1 .qsv holding MPEG-2 video in MPEG-TS\n"
    "segments. Defaults: 2 segments of 20 frames.\n";

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || std::string(argv[1]) == "--help") {
    std::cerr << kUsage;
    return argc < 2 ? 1 : 0;
  }

  const std::string path = argv[1];
  const int segments = argc > 2 ? std::atoi(argv[2]) : 2;
  const int frames = argc > 3 ? std::atoi(argv[3]) : 20;
  if (segments < 1 || frames < 1) {
    std::cerr << kUsage;
    return 1;
  }

  test::SilenceFfmpegLog();

  test::QsvBuilder builder(1);
  for (int i = 0; i < segments; ++i) {
    const test::Clip clip =
        test::MakeMpegTsClip(frames, static_cast<std::uint32_t>(i * 7));
    if (!clip.valid()) {
      std::cerr << "This FFmpeg build has no MPEG-2 encoder; cannot build a "
                   "fixture.\n";
      return 2;
    }
    builder.AddSegment(clip.bytes);
  }

  const std::vector<std::uint8_t> bytes = builder.Build();
  std::FILE* out = std::fopen(path.c_str(), "wb");
  if (out == nullptr) {
    std::cerr << "Cannot write " << path << "\n";
    return 1;
  }
  const std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), out);
  std::fclose(out);
  if (written != bytes.size()) {
    std::cerr << "Short write to " << path << "\n";
    return 1;
  }

  std::cout << path << " (" << segments << " segments, " << bytes.size()
            << " bytes)\n";
  return 0;
}
