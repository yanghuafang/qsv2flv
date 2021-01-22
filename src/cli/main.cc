extern "C" {
#include <libavutil/log.h>
}

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "qsv/reader.h"
#include "remux/remuxer.h"

/// \file
/// The headless converter: `qsv2flv-cli [options] file.qsv ...`
///
/// Not a port of anything — the original was GUI-only. It exists for three
/// reasons. A batch of files on a machine with no display was previously
/// impossible; CI can run a real conversion on all three platforms without a
/// window server (docs/Testing.md); and when a conversion fails, comparing this
/// against `ffmpeg -i` on the same segment is the shortest way to tell a bug in
/// this tool from a broken source file.

namespace {

constexpr const char* kUsage =
    "Usage: qsv2flv-cli [options] <file.qsv> ...\n"
    "\n"
    "Remux iQIYI .qsv files to a standard container, without re-encoding.\n"
    "\n"
    "Options:\n"
    "  -o, --output-dir DIR   Write output here (default: beside each input)\n"
    "  -f, --format EXT       Container extension: mp4 (default) or flv\n"
    "  -v, --verbose          Show FFmpeg's own diagnostics\n"
    "  -h, --help             Show this help\n"
    "\n"
    "MP4 is the safe choice. FLV cannot carry H.265, which the higher-quality\n"
    "sources use, and the muxer will refuse the file rather than re-encode "
    "it.\n";

struct Options {
  std::vector<std::string> inputs;
  std::string output_dir;
  std::string format = "mp4";
  bool verbose = false;
};

/// Returns false when the command line is unusable; `exit_code` is 0 for --help
/// and 1 for a real mistake, so main() can hand both to the same return.
bool ParseArguments(int argc, char** argv, Options& options, int& exit_code) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    auto take_value = [&](std::string& target) {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << arg << "\n";
        exit_code = 1;
        return false;
      }
      target = argv[++i];
      return true;
    };

    if (arg == "-h" || arg == "--help") {
      std::cout << kUsage;
      exit_code = 0;
      return false;
    }
    if (arg == "-v" || arg == "--verbose") {
      options.verbose = true;
    } else if (arg == "-o" || arg == "--output-dir") {
      if (!take_value(options.output_dir)) {
        return false;
      }
    } else if (arg == "-f" || arg == "--format") {
      if (!take_value(options.format)) {
        return false;
      }
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Unknown option: " << arg << "\n" << kUsage;
      exit_code = 1;
      return false;
    } else {
      options.inputs.push_back(arg);
    }
  }

  if (options.inputs.empty()) {
    std::cerr << kUsage;
    exit_code = 1;
    return false;
  }
  return true;
}

/// Where one input's output goes: the chosen directory, or the input's own.
std::filesystem::path OutputPathFor(const std::filesystem::path& input,
                                    const Options& options) {
  const std::filesystem::path directory =
      options.output_dir.empty() ? input.parent_path()
                                 : std::filesystem::path(options.output_dir);
  return directory / (input.stem().string() + "." + options.format);
}

/// Progress on one rewritten line, but only for a terminal. Redirected to a
/// file or a CI log, a carriage return produces one enormous line, so those get
/// the summary line at the end and nothing else.
class ProgressLine {
 public:
  explicit ProgressLine(std::string label)
      : label_(std::move(label)), interactive_(isatty(fileno(stderr)) != 0) {}

  bool operator()(double fraction) {
    if (interactive_) {
      std::fprintf(stderr, "\r%s  %3d%%", label_.c_str(),
                   static_cast<int>(fraction * 100.0));
      std::fflush(stderr);
    }
    return true;
  }

  void finish() const {
    if (interactive_) {
      std::fprintf(stderr, "\r\033[K");
    }
  }

 private:
  std::string label_;
  bool interactive_;
};

}  // namespace

int main(int argc, char** argv) {
  Options options;
  int exit_code = 0;
  if (!ParseArguments(argc, argv, options, exit_code)) {
    return exit_code;
  }

  // FFmpeg logs at AV_LOG_INFO by default, which prints a stream dump per
  // segment — one per index entry, so dozens for a feature-length file.
  av_log_set_level(options.verbose ? AV_LOG_INFO : AV_LOG_ERROR);

  int failures = 0;
  for (const std::string& input : options.inputs) {
    const std::filesystem::path input_path(input);
    const std::filesystem::path output = OutputPathFor(input_path, options);

    qsv::Reader reader(input);
    if (!reader.ok()) {
      std::cerr << input << ": " << qsv::Describe(reader.status()) << "\n";
      ++failures;
      continue;
    }

    ProgressLine progress(input_path.filename().string());
    const remux::Result result =
        remux::Remux(reader, output.string(), std::ref(progress));
    progress.finish();

    if (!result.ok) {
      std::cerr << input << ": " << result.message << "\n";
      ++failures;
      continue;
    }
    if (result.dropped_packets > 0) {
      std::cerr
          << input << ": warning: skipped " << result.dropped_packets
          << " packet(s) whose stream is missing from the first segment\n";
    }
    std::cout << output.string() << "\n";
  }

  return failures == 0 ? 0 : 1;
}
