#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QStringList>

class QCoreApplication;

namespace AppLogger
{
bool initialize();
void shutdown();
QString logFilePath();
void installOperationEventFilter(QCoreApplication *app);

void info(const QString &message);
void warning(const QString &message);
void error(const QString &message);
void operation(const QString &message);
void command(const QString &program, const QStringList &arguments);
}

#endif // LOGGER_H
