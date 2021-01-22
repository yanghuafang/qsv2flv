#ifndef QSV2FLV_GUI_MAIN_WINDOW_H_
#define QSV2FLV_GUI_MAIN_WINDOW_H_

#include <QMainWindow>
#include <QString>

/// \file
/// The window: file list, output folder, target container, and one button that
/// starts and stops the run.

class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

namespace gui {

class ConverterThread;
class FileListModel;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

 protected:
  // camelCase: QWidget's event handlers, whose names are its to choose.
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

  /// Stops the worker and waits for it before the window goes away.
  ///
  /// Without this, closing mid-conversion destroys a running QThread, which
  /// Qt turns into a qFatal() — and the worker would still be reading a
  /// half-destroyed window on its way there.
  void closeEvent(QCloseEvent* event) override;

 private slots:
  void ChooseOutputFolder();
  void AddFiles();
  void RemoveSelectedFiles();
  void ClearFiles();
  void ToggleConversion();

  void FileStarted(int row);
  void FileProgress(int row, double progress);
  void FileFinished(int row, bool ok, const QString& error);
  void ConversionFinished();

 private:
  void StartConversion();
  void StopConversion();

  /// Enable or disable everything that must not change mid-run, in one place.
  /// The original guarded three of the four entry points and left Add Files
  /// open, which is the one that could reallocate the list the worker was
  /// reading.
  void SetEditingEnabled(bool enabled);

  [[nodiscard]] QString output_folder() const;
  [[nodiscard]] QString target_extension() const;

  Ui::MainWindow* ui_;
  FileListModel* model_ = nullptr;
  ConverterThread* worker_ = nullptr;
};

}  // namespace gui

#endif  // QSV2FLV_GUI_MAIN_WINDOW_H_
