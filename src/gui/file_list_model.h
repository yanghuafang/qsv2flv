#ifndef QSV2FLV_GUI_FILE_LIST_MODEL_H_
#define QSV2FLV_GUI_FILE_LIST_MODEL_H_

#include <vector>

#include <QAbstractTableModel>
#include <QStringList>

#include "gui/input_file.h"

/// \file
/// The table behind the conversion list.

namespace gui {

/// A real QAbstractTableModel over a vector of InputFile.
///
/// The original wrapped a QStandardItemModel and rebuilt every row from scratch
/// on each add, remove, and clear — which reset the model, so column widths and
/// the user's selection went with it, and made adding N files O(N^2). More to
/// the point, a reset invalidates the QModelIndex values a caller is holding,
/// which is what made "select three rows and press Remove" delete the wrong
/// ones.
class FileListModel : public QAbstractTableModel {
  Q_OBJECT

 public:
  enum Column { kNameColumn = 0, kStatusColumn = 1, kColumnCount = 2 };

  explicit FileListModel(QObject* parent = nullptr);

  // camelCase, against this project's Google naming, because these four
  // override QAbstractTableModel and a rename would silently stop overriding
  // anything. See CONTRIBUTING.md, "Coding style".
  [[nodiscard]] int rowCount(const QModelIndex& parent) const override;
  [[nodiscard]] int columnCount(const QModelIndex& parent) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index,
                              int role) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                    int role) const override;

  /// Append every path that is not already queued. Returns how many were added,
  /// so the caller can tell a duplicate drop from an empty one.
  int AddFiles(const QStringList& paths);

  /// Remove the given rows. Sorts and walks them high to low internally, so a
  /// caller can pass a selection in any order — removing row 2 first would
  /// otherwise renumber row 5 out from under the next removal.
  void RemoveFiles(QList<int> rows);

  void ClearFiles();

  [[nodiscard]] const InputFile& FileAt(int row) const;
  [[nodiscard]] int file_count() const {
    return static_cast<int>(files_.size());
  }

  /// Rows still waiting to be converted, in order.
  [[nodiscard]] QList<int> PendingRows() const;

  void SetState(int row, InputFile::State state, const QString& error = {});
  void SetProgress(int row, double progress);

  /// Put every Done or Failed row back to Waiting, so a second run retries
  /// them instead of skipping straight past.
  void ResetStates();

 private:
  void StatusChanged(int row);

  std::vector<InputFile> files_;
};

}  // namespace gui

#endif  // QSV2FLV_GUI_FILE_LIST_MODEL_H_
