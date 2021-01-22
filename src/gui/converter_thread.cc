#include "gui/converter_thread.h"

#include "qsv/reader.h"
#include "remux/remuxer.h"

namespace gui {

ConverterThread::ConverterThread(QList<ConversionJob> jobs, QObject* parent)
    : QThread(parent), jobs_(std::move(jobs)) {}

void ConverterThread::RequestStop() { stop_requested_.store(true); }

void ConverterThread::run() {
  for (const ConversionJob& job : jobs_) {
    if (stop_requested_.load()) {
      return;
    }

    emit FileStarted(job.row);

    qsv::Reader reader(job.input_path.toStdString());
    if (!reader.ok()) {
      emit FileFinished(job.row, false,
                        QString::fromUtf8(qsv::Describe(reader.status())));
      continue;
    }

    const remux::Result result = remux::Remux(
        reader, job.output_path.toStdString(), [&](double progress) {
          emit FileProgress(job.row, progress);
          return !stop_requested_.load();
        });

    if (result.cancelled) {
      // Nothing is emitted for the file that was interrupted: the row goes back
      // to Waiting when the run ends, so pressing Convert again retries it.
      return;
    }
    emit FileFinished(job.row, result.ok,
                      QString::fromStdString(result.message));
  }
}

}  // namespace gui
