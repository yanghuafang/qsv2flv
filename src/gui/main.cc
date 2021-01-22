extern "C" {
#include <libavutil/log.h>
}

#include <QApplication>

#include "gui/main_window.h"

int main(int argc, char* argv[]) {
  QApplication application(argc, argv);
  QApplication::setApplicationName("qsv2flv");
  QApplication::setOrganizationName("qsv2flv");

  // FFmpeg writes a stream dump per segment at its default level, and a GUI has
  // no console to absorb it. Errors still reach stderr, which is where the
  // useful half of a failed conversion ends up.
  av_log_set_level(AV_LOG_ERROR);

  gui::MainWindow window;
  window.show();
  return QApplication::exec();
}
