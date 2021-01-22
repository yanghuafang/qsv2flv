#include "gui/input_file.h"

#include <QFileInfo>

namespace gui {

QString InputFile::file_name() const { return QFileInfo(path).fileName(); }

QString InputFile::base_name() const {
  // completeBaseName(), not baseName(): the latter stops at the *first* dot, so
  // "Episode 1.1080p.qsv" would come out as "Episode 1" and two episodes of a
  // series could collide on one output name.
  return QFileInfo(path).completeBaseName();
}

QString InputFile::status_text() const {
  switch (state) {
    case State::kWaiting:
      return QObject::tr("Waiting");
    case State::kConverting:
      return QObject::tr("Converting %1%").arg(progress * 100.0, 0, 'f', 0);
    case State::kDone:
      return QObject::tr("Done");
    case State::kFailed:
      return QObject::tr("Failed: %1").arg(error);
  }
  return QString();
}

}  // namespace gui
