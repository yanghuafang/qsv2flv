#include "gui/file_list_model.h"

#include <algorithm>

namespace gui {

FileListModel::FileListModel(QObject* parent) : QAbstractTableModel(parent) {}

int FileListModel::rowCount(const QModelIndex& parent) const {
  // A table model has no children below the top level, and returning a count
  // for a valid parent is how a QTreeView-shaped bug gets in.
  return parent.isValid() ? 0 : static_cast<int>(files_.size());
}

int FileListModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : kColumnCount;
}

QVariant FileListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= static_cast<int>(files_.size())) {
    return {};
  }
  const InputFile& file = files_[static_cast<std::size_t>(index.row())];

  if (role == Qt::DisplayRole) {
    return index.column() == kNameColumn ? file.file_name()
                                         : file.status_text();
  }
  if (role == Qt::ToolTipRole) {
    // The full path, because the Name column shows the file name alone and two
    // downloads of the same episode are told apart only by their folder.
    return file.path;
  }
  return {};
}

QVariant FileListModel::headerData(int section, Qt::Orientation orientation,
                                   int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return {};
  }
  return section == kNameColumn ? tr("File") : tr("Status");
}

int FileListModel::AddFiles(const QStringList& paths) {
  QStringList fresh;
  for (const QString& path : paths) {
    const bool queued =
        std::any_of(files_.begin(), files_.end(),
                    [&](const InputFile& file) { return file.path == path; });
    if (!queued && !fresh.contains(path)) {
      fresh.append(path);
    }
  }
  if (fresh.isEmpty()) {
    return 0;
  }

  const int first = static_cast<int>(files_.size());
  beginInsertRows(QModelIndex(), first, first + fresh.size() - 1);
  for (const QString& path : fresh) {
    files_.push_back(InputFile{path, InputFile::State::kWaiting, 0.0, {}});
  }
  endInsertRows();
  return static_cast<int>(fresh.size());
}

void FileListModel::RemoveFiles(QList<int> rows) {
  std::sort(rows.begin(), rows.end(), std::greater<>());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

  for (int row : rows) {
    if (row < 0 || row >= static_cast<int>(files_.size())) {
      continue;
    }
    beginRemoveRows(QModelIndex(), row, row);
    files_.erase(files_.begin() + row);
    endRemoveRows();
  }
}

void FileListModel::ClearFiles() {
  if (files_.empty()) {
    return;
  }
  beginRemoveRows(QModelIndex(), 0, static_cast<int>(files_.size()) - 1);
  files_.clear();
  endRemoveRows();
}

const InputFile& FileListModel::FileAt(int row) const {
  return files_[static_cast<std::size_t>(row)];
}

QList<int> FileListModel::PendingRows() const {
  QList<int> rows;
  for (std::size_t i = 0; i < files_.size(); ++i) {
    if (files_[i].state == InputFile::State::kWaiting) {
      rows.append(static_cast<int>(i));
    }
  }
  return rows;
}

void FileListModel::SetState(int row, InputFile::State state,
                             const QString& error) {
  if (row < 0 || row >= static_cast<int>(files_.size())) {
    return;
  }
  InputFile& file = files_[static_cast<std::size_t>(row)];
  file.state = state;
  file.error = error;
  if (state == InputFile::State::kDone) {
    file.progress = 1.0;
  }
  StatusChanged(row);
}

void FileListModel::SetProgress(int row, double progress) {
  if (row < 0 || row >= static_cast<int>(files_.size())) {
    return;
  }
  files_[static_cast<std::size_t>(row)].progress = progress;
  StatusChanged(row);
}

void FileListModel::ResetStates() {
  for (std::size_t i = 0; i < files_.size(); ++i) {
    InputFile& file = files_[i];
    if (file.state == InputFile::State::kWaiting) {
      continue;
    }
    file.state = InputFile::State::kWaiting;
    file.progress = 0.0;
    file.error.clear();
    StatusChanged(static_cast<int>(i));
  }
}

void FileListModel::StatusChanged(int row) {
  const QModelIndex cell = index(row, kStatusColumn);
  emit dataChanged(cell, cell, {Qt::DisplayRole});
}

}  // namespace gui
