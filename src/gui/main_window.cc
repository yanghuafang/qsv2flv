#include "gui/main_window.h"

#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QMimeData>
#include <QStandardPaths>
#include <QUrl>

#include "gui/converter_thread.h"
#include "gui/file_list_model.h"
#include "ui_main_window.h"

namespace gui {
namespace {

/// Only ever queue what the reader can open. Checked on both entry paths,
/// because a drop bypasses the file dialog's filter.
bool LooksLikeQsv(const QString& path) {
  return QFileInfo(path).suffix().compare("qsv", Qt::CaseInsensitive) == 0;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui_(new Ui::MainWindow) {
  ui_->setupUi(this);

  model_ = new FileListModel(this);
  ui_->tbvInputList->setModel(model_);
  ui_->tbvInputList->horizontalHeader()->setSectionResizeMode(
      FileListModel::kNameColumn, QHeaderView::Stretch);
  ui_->tbvInputList->horizontalHeader()->setSectionResizeMode(
      FileListModel::kStatusColumn, QHeaderView::ResizeToContents);

  // Videos on Linux, Movies on macOS; home if the platform has neither.
  QString default_folder =
      QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
  if (default_folder.isEmpty()) {
    default_folder = QDir::homePath();
  }
  ui_->txtOutputPath->setText(QDir::toNativeSeparators(default_folder));

  connect(ui_->btnSelectOutputPath, &QAbstractButton::clicked, this,
          &MainWindow::ChooseOutputFolder);
  connect(ui_->btnAppendFiles, &QAbstractButton::clicked, this,
          &MainWindow::AddFiles);
  connect(ui_->btnRemoveFiles, &QAbstractButton::clicked, this,
          &MainWindow::RemoveSelectedFiles);
  connect(ui_->btnClearList, &QAbstractButton::clicked, this,
          &MainWindow::ClearFiles);
  connect(ui_->btnConvert, &QAbstractButton::clicked, this,
          &MainWindow::ToggleConversion);
}

MainWindow::~MainWindow() {
  // closeEvent() is the normal path and has already done this. Repeated here
  // for the ones that do not go through it, such as QApplication::quit().
  StopConversion();
  if (worker_ != nullptr) {
    worker_->wait();
  }
  delete ui_;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (worker_ == nullptr && event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
  }
}

void MainWindow::dropEvent(QDropEvent* event) {
  if (worker_ != nullptr) {
    return;
  }
  QStringList paths;
  for (const QUrl& url : event->mimeData()->urls()) {
    const QString path = url.toLocalFile();
    if (LooksLikeQsv(path)) {
      paths.append(path);
    }
  }
  model_->AddFiles(paths);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (worker_ != nullptr) {
    const auto answer = QMessageBox::question(
        this, tr("Quit"),
        tr("A conversion is still running. Stop it and quit?"));
    if (answer != QMessageBox::Yes) {
      event->ignore();
      return;
    }
    StopConversion();
    worker_->wait();
  }
  event->accept();
}

void MainWindow::ChooseOutputFolder() {
  const QString folder = QFileDialog::getExistingDirectory(
      this, tr("Choose output folder"), output_folder());
  if (!folder.isEmpty()) {
    ui_->txtOutputPath->setText(QDir::toNativeSeparators(folder));
  }
}

void MainWindow::AddFiles() {
  const QStringList chosen = QFileDialog::getOpenFileNames(
      this, tr("Add QSV files"), QString(), tr("iQIYI video (*.qsv)"));
  model_->AddFiles(chosen);
}

void MainWindow::RemoveSelectedFiles() {
  QList<int> rows;
  for (const QModelIndex& index :
       ui_->tbvInputList->selectionModel()->selectedRows()) {
    rows.append(index.row());
  }
  model_->RemoveFiles(rows);
}

void MainWindow::ClearFiles() { model_->ClearFiles(); }

void MainWindow::ToggleConversion() {
  if (worker_ != nullptr) {
    StopConversion();
  } else {
    StartConversion();
  }
}

void MainWindow::StartConversion() {
  if (model_->file_count() == 0) {
    QMessageBox::information(this, tr("Nothing to convert"),
                             tr("Add one or more .qsv files first."));
    return;
  }

  QList<int> rows = model_->PendingRows();
  if (rows.isEmpty()) {
    const auto answer = QMessageBox::question(
        this, tr("Convert again"),
        tr("Every file in the list has already been converted. "
           "Convert them again, overwriting the previous output?"));
    if (answer != QMessageBox::Yes) {
      return;
    }
    model_->ResetStates();
    rows = model_->PendingRows();
  }

  const QDir folder(output_folder());
  if (!folder.exists()) {
    QMessageBox::warning(this, tr("Output folder"),
                         tr("%1 does not exist.").arg(folder.path()));
    return;
  }

  const QString extension = target_extension();
  QList<ConversionJob> jobs;
  jobs.reserve(rows.size());
  for (int row : rows) {
    const InputFile& file = model_->FileAt(row);
    jobs.append(ConversionJob{
        row, file.path,
        folder.absoluteFilePath(file.base_name() + "." + extension)});
  }

  worker_ = new ConverterThread(jobs);
  connect(worker_, &ConverterThread::FileStarted, this,
          &MainWindow::FileStarted);
  connect(worker_, &ConverterThread::FileProgress, this,
          &MainWindow::FileProgress);
  connect(worker_, &ConverterThread::FileFinished, this,
          &MainWindow::FileFinished);
  // ConversionFinished() before deleteLater(), so worker_ is cleared while the
  // object is still alive; slots fire in the order they were connected.
  connect(worker_, &QThread::finished, this, &MainWindow::ConversionFinished);
  connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);

  SetEditingEnabled(false);
  ui_->btnConvert->setText(tr("Stop"));
  worker_->start();
}

void MainWindow::StopConversion() {
  if (worker_ == nullptr) {
    return;
  }
  worker_->RequestStop();
  ui_->btnConvert->setEnabled(false);
  ui_->btnConvert->setText(tr("Stopping..."));
}

void MainWindow::ConversionFinished() {
  worker_ = nullptr;

  // A file interrupted mid-conversion reports nothing, so put any row still
  // showing Converting back to Waiting — its partial output has already been
  // removed by remux().
  for (int row = 0; row < model_->file_count(); ++row) {
    if (model_->FileAt(row).state == InputFile::State::kConverting) {
      model_->SetState(row, InputFile::State::kWaiting);
      model_->SetProgress(row, 0.0);
    }
  }

  SetEditingEnabled(true);
  ui_->btnConvert->setEnabled(true);
  ui_->btnConvert->setText(tr("Convert"));
}

void MainWindow::FileStarted(int row) {
  model_->SetState(row, InputFile::State::kConverting);
}

void MainWindow::FileProgress(int row, double progress) {
  model_->SetProgress(row, progress);
}

void MainWindow::FileFinished(int row, bool ok, const QString& error) {
  model_->SetState(
      row, ok ? InputFile::State::kDone : InputFile::State::kFailed, error);
}

void MainWindow::SetEditingEnabled(bool enabled) {
  ui_->btnAppendFiles->setEnabled(enabled);
  ui_->btnRemoveFiles->setEnabled(enabled);
  ui_->btnClearList->setEnabled(enabled);
  ui_->btnSelectOutputPath->setEnabled(enabled);
  ui_->txtOutputPath->setEnabled(enabled);
  ui_->cbxTargetFormat->setEnabled(enabled);
}

QString MainWindow::output_folder() const { return ui_->txtOutputPath->text(); }

QString MainWindow::target_extension() const {
  return ui_->cbxTargetFormat->currentText();
}

}  // namespace gui
