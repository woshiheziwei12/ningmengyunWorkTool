#include "logger.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QMessageLogContext>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QPushButton>
#include <QTextStream>
#include <QThread>

namespace
{
QMutex g_logMutex;
QFile *g_logFile = nullptr;
QString g_logFilePath;
QtMessageHandler g_previousMessageHandler = nullptr;

QString levelText(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("INFO");
}

QString nowText()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
}

QString threadText()
{
    return QString("tid=%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
}

void writeLine(const QString &level, const QString &message)
{
    QMutexLocker locker(&g_logMutex);
    if (!g_logFile || !g_logFile->isOpen())
        return;

    QTextStream stream(g_logFile);
    stream.setCodec("UTF-8");
    stream << nowText() << " [" << level << "] [" << threadText() << "] " << message << "\n";
    stream.flush();
    g_logFile->flush();
}

QString quoteArg(const QString &arg)
{
    QString escaped = arg;
    escaped.replace("\"", "\\\"");
    if (escaped.contains(' ') || escaped.contains('\t') || escaped.contains('"'))
        return "\"" + escaped + "\"";
    return escaped;
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QString line = message;
    if (context.file)
    {
        line += QStringLiteral(" (%1:%2)").arg(QString::fromLocal8Bit(context.file)).arg(context.line);
    }

    writeLine(levelText(type), line);

    if (g_previousMessageHandler)
        g_previousMessageHandler(type, context, message);
}

class OperationEventFilter : public QObject
{
public:
    explicit OperationEventFilter(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonRelease)
        {
            QPushButton *button = qobject_cast<QPushButton *>(watched);
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (button && button->isEnabled() && button->rect().contains(mouseEvent->pos()))
            {
                AppLogger::operation(QStringLiteral("按钮点击 object=%1 text=%2")
                    .arg(button->objectName(), button->text().replace('\n', ' ')));
            }
        }
        else if (event->type() == QEvent::Drop)
        {
            AppLogger::operation(QStringLiteral("拖放操作 target=%1")
                .arg(watched ? watched->objectName() : QString()));
        }

        return QObject::eventFilter(watched, event);
    }
};
}

bool AppLogger::initialize()
{
    QMutexLocker locker(&g_logMutex);
    if (g_logFile && g_logFile->isOpen())
        return true;

    const QString logDirPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/log");
    QDir logDir(logDirPath);
    if (!logDir.exists() && !logDir.mkpath("."))
        return false;

    g_logFilePath = logDir.absoluteFilePath(
        QString("worktool_%1.log").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz")));

    g_logFile = new QFile(g_logFilePath);
    if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        delete g_logFile;
        g_logFile = nullptr;
        g_logFilePath.clear();
        return false;
    }

    g_previousMessageHandler = qInstallMessageHandler(messageHandler);

    QTextStream stream(g_logFile);
    stream.setCodec("UTF-8");
    stream << nowText() << " [INFO] [" << threadText() << "] 日志启动 path="
           << QDir::toNativeSeparators(g_logFilePath) << "\n";
    stream.flush();
    g_logFile->flush();
    return true;
}

void AppLogger::shutdown()
{
    QMutexLocker locker(&g_logMutex);
    if (g_previousMessageHandler)
    {
        qInstallMessageHandler(g_previousMessageHandler);
        g_previousMessageHandler = nullptr;
    }

    if (g_logFile)
    {
        QTextStream stream(g_logFile);
        stream.setCodec("UTF-8");
        stream << nowText() << " [INFO] [" << threadText() << "] 日志关闭\n";
        stream.flush();
        g_logFile->flush();
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
}

QString AppLogger::logFilePath()
{
    QMutexLocker locker(&g_logMutex);
    return g_logFilePath;
}

void AppLogger::installOperationEventFilter(QCoreApplication *app)
{
    if (!app)
        return;
    app->installEventFilter(new OperationEventFilter(app));
    operation(QStringLiteral("已安装全局操作日志过滤器"));
}

void AppLogger::info(const QString &message)
{
    writeLine(QStringLiteral("INFO"), message);
}

void AppLogger::warning(const QString &message)
{
    writeLine(QStringLiteral("WARN"), message);
}

void AppLogger::error(const QString &message)
{
    writeLine(QStringLiteral("ERROR"), message);
}

void AppLogger::operation(const QString &message)
{
    writeLine(QStringLiteral("OP"), message);
}

void AppLogger::command(const QString &program, const QStringList &arguments)
{
    QStringList parts;
    parts.append(quoteArg(program));
    for (const QString &argument : arguments)
        parts.append(quoteArg(argument));
    writeLine(QStringLiteral("CMD"), parts.join(' '));
}
