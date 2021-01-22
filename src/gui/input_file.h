#ifndef QSV2FLV_GUI_INPUT_FILE_H_
#define QSV2FLV_GUI_INPUT_FILE_H_

#include <QString>

/// \file
/// One row of the conversion list.

namespace gui {

/// A queued input file and what has happened to it so far.
///
/// Plain data, owned by FileListModel and only ever touched on the GUI thread.
/// The worker never sees one: it is handed a snapshot of paths and reports back
/// by row number, which is what keeps the model out of a data race. The
/// original passed the worker a pointer into the model's own vector, so
/// enqueueing another file mid-conversion reallocated the vector under it.
struct InputFile {
  enum class State { kWaiting, kConverting, kDone, kFailed };

  QString path;
  State state = State::kWaiting;
  double progress = 0.0;
  /// Set only when `state` is Failed.
  QString error;

  [[nodiscard]] QString file_name() const;

  /// The file name with its extension removed — the stem of the output name.
  [[nodiscard]] QString base_name() const;

  /// What the Status column shows.
  [[nodiscard]] QString status_text() const;
};

}  // namespace gui

#endif  // QSV2FLV_GUI_INPUT_FILE_H_
