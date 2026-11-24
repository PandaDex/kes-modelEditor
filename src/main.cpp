#include "KesStyle.h"
#include "MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QSurfaceFormat>

int main(int argc, char** argv)
{
    // has to happen before QApplication, otherwise floating a dock breaks its GL context
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // Qt's native Windows file dialogs keep the main thread blocked for seconds after closing.
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QApplication::setApplicationName("kesModelEditor");
    QApplication::setOrganizationName("KES Engine");
    QApplication::setWindowIcon(QIcon(":/icons/icon.png"));
    kes::style::apply(app);

    kme::MainWindow w;
    w.show();
    if (argc > 1) w.openPath(QString::fromLocal8Bit(argv[1]));
    return app.exec();
}
