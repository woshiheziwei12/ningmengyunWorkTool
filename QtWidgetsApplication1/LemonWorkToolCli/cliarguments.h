#ifndef CLIARGUMENTS_H
#define CLIARGUMENTS_H

#include <QMap>
#include <QString>
#include <QStringList>

class CliArguments
{
public:
    static CliArguments parse(const QStringList &arguments);

    QStringList positionals() const;
    QString commandPath(int maxParts = 2) const;

    bool has(const QString &name) const;
    QString value(const QString &name, const QString &defaultValue = QString()) const;
    QStringList values(const QString &name) const;

private:
    QStringList m_positionals;
    QMap<QString, QStringList> m_options;
};

#endif // CLIARGUMENTS_H
