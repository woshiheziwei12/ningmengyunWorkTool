#include "QtWidgetsApplication1.h"
#include "startupwindow.h"
#include "Common/globaldata.h"
#include "Common/logger.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    AppLogger::initialize();
    AppLogger::installOperationEventFilter(&app);
    AppLogger::operation(QStringLiteral("程序启动"));

    GlobalData globalData;

    StartUpWindow loginWindow(&globalData);
    loginWindow.exec();

    if (!globalData.m_bLogin)
    {
        AppLogger::operation(QStringLiteral("用户未登录，程序退出"));
        AppLogger::shutdown();
        return 0;
    }

    AppLogger::operation(QStringLiteral("用户登录成功，打开主窗口"));
    QtWidgetsApplication1 window(&globalData);
    window.show();
    const int result = app.exec();
    AppLogger::operation(QStringLiteral("程序退出，exitCode=%1").arg(result));
    AppLogger::shutdown();
    return result;
}
