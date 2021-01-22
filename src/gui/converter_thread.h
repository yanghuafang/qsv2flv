#ifndef QSV2FLV_GUI_CONVERTER_THREAD_H_
#define QSV2FLV_GUI_CONVERTER_THREAD_H_

#include <atomic>

#include <QList>
#include <QString>
#include <QThread>

/// \file
/// The worker that runs conversions off the GUI thread.

namespace gui {

/// One queued conversion. Both paths are resolved on the GUI thread before the
/// worker starts, so it never reads a widget or the model — changing either
/// mid-run cannot affect or race with a conversion under way.
struct ConversionJob {
  int row = -1;
  QString input_path;
  QString output_path;
};

class ConverterThread : public QThread {
  Q_OBJECT

 public:
  ConverterThread(QList<ConversionJob> jobs, QObject* parent = nullptr);

  /// Ask the run to stop at the next progress callback, which is at most 1% of
  /// one file away. Safe from any thread.
  void RequestStop();

 signals:
  /// `row` indexes the FileListModel, not this thread's job list. Every one of
  /// these crosses to the GUI thread over a queued connection, which is the
  /// only reason it is safe for the slot to touch the model.
  void FileStarted(int row);
  void FileProgress(int row, double progress);
  void FileFinished(int row, bool ok, const QString& error);

 protected:
  // camelCase: QThread::run() is the one Qt calls on the new thread.
  void run() override;

 private:
  QList<ConversionJob> jobs_;
  std::atomic<bool> stop_requested_{false};
};

}  // namespace gui

#endif  // QSV2FLV_GUI_CONVERTER_THREAD_H_
