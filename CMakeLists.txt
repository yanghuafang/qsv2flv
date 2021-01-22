cmake_minimum_required(VERSION 3.22)

project(qsv2flv VERSION 1.0.0 LANGUAGES CXX)

# All three: without REQUIRED the standard is only a preference and an older
# one is chosen silently; without EXTENSIONS OFF the flag is -std=gnu++17, so a
# GNU extension compiles here and fails elsewhere.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# clang-tidy and clangd both need the compile database to know the flags each
# file is built with; scripts/tidy.sh points at it.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Separable so the sanitizer job runs without Qt in the process, and so this
# builds on a headless machine.
option(QSV2FLV_BUILD_GUI "Build the Qt desktop application" ON)
option(QSV2FLV_BUILD_TESTS "Build the test suite" ON)

# ---------------------------------------------------------------------------
# FFmpeg
# ---------------------------------------------------------------------------

# pkg-config, not find_library: it takes the headers and the libraries from one
# install. Vendored headers beside a system library link cleanly and then read
# every struct field at the wrong offset, since AVFormatContext's layout moves
# between majors.
find_package(PkgConfig REQUIRED)

# Homebrew bakes the versioned Cellar path into its .pc files, so
# `brew upgrade ffmpeg` leaves every cached include path pointing at a
# directory that no longer exists. --define-prefix recomputes the prefix from
# where the .pc file was found, which is the stable opt/ symlink Homebrew
# re-points on upgrade. Apple only: elsewhere the .pc prefix is already stable.
if(APPLE)
  set(PKG_CONFIG_ARGN --define-prefix)
endif()

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
  libavformat>=60
  libavcodec>=60
  libavutil>=58)
message(STATUS "FFmpeg: libavformat ${FFMPEG_libavformat_VERSION}")

# 60 is FFmpeg 6.0, and it is a support floor rather than an API one: the
# sources compile clean against 4.4 headers. It is the oldest FFmpeg CI
# exercises, and 4.x and 5.x are end-of-life — which matters for a program
# whose input is an untrusted file. Lower it only alongside a CI job that
# actually runs the older version.
#
# Three libraries only; a remux decodes nothing, scales nothing, resamples
# nothing.

# ---------------------------------------------------------------------------
# Shared target settings
# ---------------------------------------------------------------------------

# Opt-in: a compiler upgrade can add a warning at any time, and a clone should
# still build. CI turns it on.
option(QSV2FLV_WERROR "Treat compiler warnings as errors" OFF)

# AddressSanitizer: -DQSV2FLV_ASAN=ON, or scripts/build.sh --asan.
#
# Here rather than in a private build because every length in a .qsv header is
# attacker-controlled, and reader_test.cc's garbage pass asserts only "did not
# crash". The frame-pointer flag and -g are what make its traces readable.
option(QSV2FLV_ASAN "Build with AddressSanitizer" OFF)

# UBSan: the shift-and-rotate arithmetic in qsv/crypto.cc and the 64-bit offset
# checks in qsv/reader.cc are the shapes it reports on. -fno-sanitize-recover
# aborts on the first finding, so CI fails rather than printing and carrying on.
option(QSV2FLV_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)

function(qsv2flv_configure target)
  target_compile_options(${target} PRIVATE -Wall -Wextra)
  if(QSV2FLV_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
  if(QSV2FLV_ASAN)
    target_compile_options(${target} PRIVATE
      -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(${target} PRIVATE -fsanitize=address)
  endif()
  if(QSV2FLV_UBSAN)
    # vptr is off because it needs RTTI on both sides of a cast and packaged
    # FFmpeg builds are C; leaving it on reports nothing useful here.
    target_compile_options(${target} PRIVATE
      -fsanitize=undefined -fno-sanitize=vptr -fno-sanitize-recover=all
      -fno-omit-frame-pointer -g)
    target_link_options(${target} PRIVATE
      -fsanitize=undefined -fno-sanitize=vptr)
  endif()
endfunction()

if(QSV2FLV_ASAN)
  message(STATUS "AddressSanitizer enabled")
endif()
if(QSV2FLV_UBSAN)
  message(STATUS "UndefinedBehaviorSanitizer enabled")
endif()

# ---------------------------------------------------------------------------
# Core: everything that is not a user interface
# ---------------------------------------------------------------------------

# Qt-free on purpose: <fstream> and std::string are enough, which is what lets
# the suite and the sanitizer job run with no Qt installed.
add_library(qsv2flv_core STATIC
  src/qsv/crypto.cc
  src/qsv/reader.cc
  src/qsv/types.cc
  src/remux/remuxer.cc)

# src/ is the only include root, so every include in this tree names its
# directory ("qsv/reader.h", not "reader.h"). The layer a header belongs to
# is then visible at each use site.
target_include_directories(qsv2flv_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qsv2flv_core PUBLIC PkgConfig::FFMPEG)

# libavutil/common.h refuses to compile as C++ unless one of these is defined,
# so that INT64_C and friends are visible. Without it a translation unit only
# builds by accident — if some earlier include happened to pull in <cstdint>
# first, the guard sees UINT64_C already defined and lets it through. PUBLIC,
# because every target that links the core also includes FFmpeg headers.
target_compile_definitions(qsv2flv_core PUBLIC
  __STDC_CONSTANT_MACROS
  __STDC_LIMIT_MACROS)

qsv2flv_configure(qsv2flv_core)

# ---------------------------------------------------------------------------
# Command-line converter
# ---------------------------------------------------------------------------

add_executable(qsv2flv-cli src/cli/main.cc)
target_link_libraries(qsv2flv-cli PRIVATE qsv2flv_core)
qsv2flv_configure(qsv2flv-cli)

# ---------------------------------------------------------------------------
# Qt desktop application
# ---------------------------------------------------------------------------

if(QSV2FLV_BUILD_GUI)
  # Qt 6 only — the model uses Qt 6 container and QAbstractTableModel API, so a
  # Qt 5 fallback would configure and then fail to compile.
  find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

  add_executable(qsv2flv
    src/gui/converter_thread.cc
    src/gui/file_list_model.cc
    src/gui/input_file.cc
    src/gui/main_window.cc
    src/gui/main_window.ui
    src/gui/main.cc)

  set_target_properties(qsv2flv PROPERTIES AUTOMOC ON AUTOUIC ON)
  target_link_libraries(qsv2flv PRIVATE qsv2flv_core Qt6::Widgets)
  qsv2flv_configure(qsv2flv)

  if(APPLE)
    # A bundle, so the app gets a Dock icon, a menu bar, and the .qsv document
    # association in cmake/Info.plist.in. Not signed and not deployed: the
    # binary here still refers to Qt and FFmpeg where Homebrew put them, so it
    # runs on this machine and not on a stranger's. docs/Install.md says what
    # macdeployqt would add.
    set_target_properties(qsv2flv PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_INFO_PLIST
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in
      MACOSX_BUNDLE_BUNDLE_NAME "qsv2flv"
      MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.yanghuafang.qsv2flv"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
  endif()

  install(TARGETS qsv2flv BUNDLE DESTINATION . RUNTIME DESTINATION bin)
endif()

install(TARGETS qsv2flv-cli RUNTIME DESTINATION bin)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(QSV2FLV_BUILD_TESTS)
  enable_testing()

  add_library(qsv2flv_test_support STATIC
    tests/support/media.cc
    tests/support/qsv_builder.cc
    tests/support/temp_dir.cc)
  target_include_directories(qsv2flv_test_support
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_link_libraries(qsv2flv_test_support PUBLIC qsv2flv_core)
  qsv2flv_configure(qsv2flv_test_support)

  # Target name and file name deliberately differ. Google's convention governs
  # the file (crypto_test.cc); the target is what CTest prints in a failure
  # report and what scripts/run-tests.sh takes as an argument, so it stays the
  # readable CryptoTest. Neither is a C++ identifier, so the style guide has
  # nothing to say about the second one.
  foreach(suite Crypto Reader Remux)
    string(TOLOWER "${suite}" suite_file)
    add_executable(${suite}Test tests/${suite_file}_test.cc)
    target_link_libraries(${suite}Test PRIVATE qsv2flv_test_support)
    qsv2flv_configure(${suite}Test)
    add_test(NAME ${suite}Test COMMAND ${suite}Test)
  endforeach()

  # Writes the in-memory fixture out to a file, so scripts/check-cli.sh can run
  # a real conversion through the shipped binary rather than through the test
  # support library. Built with the tests, never installed.
  add_executable(qsv2flv-make-fixture tests/make_fixture.cc)
  target_link_libraries(qsv2flv-make-fixture PRIVATE qsv2flv_test_support)
  qsv2flv_configure(qsv2flv-make-fixture)

  # The suites above call remux() in process. This one runs the executable a
  # user actually runs, which is what catches a link or runtime-loader problem
  # that no in-process assertion can see.
  #
  # A test rather than a step in a workflow file: it was the latter, and the
  # cost was that `ctest` locally and CI checked different things. Generator
  # expressions for the paths, so this keeps working under a multi-config
  # generator and under the per-preset build directories, neither of which puts
  # the binaries where a hand-written path would look.
  add_test(NAME CliSmoke
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-cli.sh
                   $<TARGET_FILE:qsv2flv-cli>
                   $<TARGET_FILE:qsv2flv-make-fixture>)
  # Labelled so `ctest -L smoke` runs it alone, and `ctest -LE smoke` skips it
  # when only the unit suites are wanted.
  set_tests_properties(CliSmoke PROPERTIES LABELS smoke)
endif()
