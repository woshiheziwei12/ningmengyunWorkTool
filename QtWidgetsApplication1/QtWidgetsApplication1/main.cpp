#include "QtWidgetsApplication1.h"
#include "startupwindow.h"
#include "pwdloginwidget.h"
#include "lemoncofighelper.h"
#include "Common/globaldata.h"
#include "Common/logger.h"
#include <QtWidgets/QApplication>
#include <QComboBox>
#include <QEvent>
#include <QMessageBox>
#include <memory>
#include <functional>

namespace
{
class ComboBoxWheelFilter : public QObject
{
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Wheel && qobject_cast<QComboBox *>(watched)) {
            // Ignore wheel selection even when focused; popup lists keep normal scrolling.
            event->ignore();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
};
}

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    ComboBoxWheelFilter comboBoxWheelFilter;
    app.installEventFilter(&comboBoxWheelFilter);
    AppLogger::initialize();
    AppLogger::installOperationEventFilter(&app);
    AppLogger::operation(QStringLiteral("程序启动"));

    auto data = std::make_unique<GlobalData>();
    auto login = std::make_unique<StartUpWindow>(data.get());
    login->exec();
    if (!data->m_bLogin) {
        AppLogger::shutdown();
        return 0;
    }

    app.setQuitOnLastWindowClosed(false);
    std::unique_ptr<QtWidgetsApplication1> window;
    bool switching = false;
    std::function<void()> openWorkspace;
    openWorkspace = [&]() {
        window = std::make_unique<QtWidgetsApplication1>(data.get());
        auto *session = login->sessionLogin();
        QObject::connect(session, &PwdLoginWidget::sessionStatusChanged,
            window.get(), &QtWidgetsApplication1::updateSessionStatus);
        QObject::connect(window.get(), &QtWidgetsApplication1::refreshSessionRequested,
            session, &PwdLoginWidget::refreshSession);
        QObject::connect(window.get(), &QtWidgetsApplication1::switchEnvironmentRequested, &app, [&]() {
            if (switching || window->hasActiveTasks() || login->sessionLogin()->isBusy()) return;
            switching = true;
            const QString previousEnvironment = lemoncofighelper::CurrentHttpEnvironment();
            const QString nextEnvironment = previousEnvironment == "Pro" ? "Dev2" : "Pro";
            const bool wasHidden = !window->isVisible();
            login->sessionLogin()->setAutoRefreshEnabled(false);
            window->setEnabled(false);
            lemoncofighelper::SetHttpEnvironment(nextEnvironment);
            auto nextData = std::make_unique<GlobalData>();
            auto nextLogin = std::make_unique<StartUpWindow>(nextData.get());
            nextLogin->prepareEnvironmentSwitch();
            nextLogin->exec();
            // Destroy failed/pending replies before restoring the original environment.
            if (!nextData->m_bLogin) {
                nextLogin.reset();
                lemoncofighelper::SetHttpEnvironment(previousEnvironment);
                window->setEnabled(true);
                login->sessionLogin()->setAutoRefreshEnabled(true);
                if (!wasHidden) window->restoreFromTray();
            } else {
                window.reset();
                login.reset();
                data = std::move(nextData);
                login = std::move(nextLogin);
                openWorkspace();
            }
            switching = false;
        }, Qt::QueuedConnection);
        window->show();
        window->updateSessionStatus(session->sessionStatusMessage(), !session->nextRefreshAt().isValid());
        session->setAutoRefreshEnabled(true);
    };
    openWorkspace();
    const int result = app.exec();
    window.reset();
    login.reset();
    AppLogger::operation(QStringLiteral("程序退出，exitCode=%1").arg(result));
    AppLogger::shutdown();
    return result;
}
