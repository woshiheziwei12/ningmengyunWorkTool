#include <QtWidgets>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <chrono>
#include "QtWidgetsApplication1.h"
#include "backupaccountsetwidget.h"
#include "httprequesthelper.h"
#include "Common/globaldata.h"

// Runs in a disposable child: success requires the real exit entry point to
// terminate the process, even while the event loop and background work are busy.
int runWorkspaceExitTest(const QString &scenario)
{
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        fprintf(stderr, "FAIL: close request did not terminate the process\n");
        std::fflush(stderr);
        std::_Exit(42);
    }).detach();
    GlobalData data;
    QtWidgetsApplication1 window(&data);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    auto *backup = new BackupAccountSetWidget(&data, &window);
    const bool busy = !scenario.endsWith("idle");
    backup->setProperty("sessionBusyCloudTask", busy);
    QTcpServer server;
    QNetworkReply *pending = nullptr;
    if (busy) {
        if (!server.listen(QHostAddress::LocalHost)) return 43;
        pending = CHttpRequestHelper::networkManager()->get(QNetworkRequest(
            QUrl(QString("http://127.0.0.1:%1/pending-backup").arg(server.serverPort()))));
        if (!window.hasActiveTasks() || pending->isFinished()) return 43;
    }
    QDialog modal(&window);
    modal.setAttribute(Qt::WA_DontShowOnScreen);
    if (scenario.contains("modal")) { modal.setModal(true); modal.show(); }
    QTimer::singleShot(0, &window, [&]() {
        if (scenario.contains("modal") && QApplication::activeModalWidget() != &modal)
            std::_Exit(43);
        fprintf(stdout, "READY: %s\n", qPrintable(scenario));
        std::fflush(stdout);
        QMenu *menu = scenario.startsWith("tray-")
            ? window.findChild<QSystemTrayIcon*>()->contextMenu()
            : window.findChild<QPushButton*>("btnSwitchEnvironment")->menu();
        for (auto *action : menu->actions()) {
            if (action->text() == QStringLiteral("退出工具")) {
                action->trigger();
                std::_Exit(44); // Returning, including qApp->quit(), is not a force exit.
            }
        }
        std::_Exit(44);
    });
    return qApp->exec() == 0 ? 44 : 43;
}
