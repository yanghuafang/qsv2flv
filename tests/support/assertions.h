#ifndef QSV2FLV_TESTS_SUPPORT_ASSERTIONS_H_
#define QSV2FLV_TESTS_SUPPORT_ASSERTIONS_H_

#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>

/// \file
/// The whole test harness: two macros, a counter, and a summary line.
///
/// Deliberately not GoogleTest or Catch2. Both would be fetched at configure
/// time, which turns `cmake ..` into a network operation and makes a CI job's
/// first failure mode a download rather than a compile; and the sanitizer job
/// would then be reporting on their allocations as well as this project's. What
/// the suite here needs is a comparison that prints both sides — everything
/// past that is a framework paying for itself in a codebase larger than this
/// one.
///
/// Failures do not stop the run. One `ctest` invocation should tell you every
/// case that broke, not the first.

namespace test {

inline int g_checks = 0;
inline int g_failures = 0;
inline const char* g_case = "";

/// Names the group that later failures belong to. Cheap enough to call per
/// scenario, which is what makes a failure line locatable without a debugger.
inline void BeginCase(const char* name) { g_case = name; }

inline void RecordFailure(const char* file, int line, const std::string& what) {
  ++g_failures;
  std::cerr << file << ":" << line << ": [" << g_case << "] " << what << "\n";
}

/// Render a value for a failure message.
///
/// Enums go through their underlying type because qsv::Status and friends have
/// no operator<<, and single-byte integers are widened because ostream would
/// otherwise print them as characters — the difference between "expected 10"
/// and an invisible control code in the log.
template <typename T>
std::string Show(const T& value) {
  std::ostringstream out;
  if constexpr (std::is_enum_v<T>) {
    out << static_cast<long long>(
        static_cast<std::underlying_type_t<T>>(value));
  } else if constexpr (std::is_integral_v<T> && sizeof(T) == 1) {
    out << static_cast<long long>(value);
  } else {
    out << value;
  }
  return out.str();
}

/// Print the summary and return the process exit code, in lcc's `NAME PASS`
/// shape so a `ctest` log reads the same as that project's.
inline int Finish(const char* suite) {
  if (g_failures == 0) {
    std::cout << suite << " PASS (" << g_checks << " checks)\n";
    return 0;
  }
  std::cout << suite << " FAIL (" << g_failures << " of " << g_checks
            << " checks)\n";
  return 1;
}

}  // namespace test

#define CHECK(condition)                                                  \
  do {                                                                    \
    ++::test::g_checks;                                                   \
    if (!(condition)) {                                                   \
      ::test::RecordFailure(__FILE__, __LINE__, "CHECK(" #condition ")"); \
    }                                                                     \
  } while (false)

#define CHECK_EQ(actual, expected)                                    \
  do {                                                                \
    ++::test::g_checks;                                               \
    const auto& check_actual = (actual);                              \
    const auto& check_expected = (expected);                          \
    if (!(check_actual == check_expected)) {                          \
      std::ostringstream check_message;                               \
      check_message << "CHECK_EQ(" #actual ", " #expected "): "       \
                    << ::test::Show(check_actual)                     \
                    << " != " << ::test::Show(check_expected);        \
      ::test::RecordFailure(__FILE__, __LINE__, check_message.str()); \
    }                                                                 \
  } while (false)

#endif  // QSV2FLV_TESTS_SUPPORT_ASSERTIONS_H_
