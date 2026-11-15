#include <QApplication>

#include "gui_types.h"
#include "main_window.h"
#include "theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PressPNG"));
    QApplication::setApplicationName(QStringLiteral("PressPNG"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    presspng::gui::registerMetaTypes();
    presspng::gui::applyTheme(app);

    presspng::gui::MainWindow window;
    window.show();

    if (argc > 1) {
        window.openPath(QString::fromLocal8Bit(argv[1]));
    }

    return app.exec();
}
