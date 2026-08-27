#include "cliarguments.h"

CliArguments CliArguments::parse(const QStringList &arguments)
{
    CliArguments parsed;

    for (int i = 1; i < arguments.size(); ++i)
    {
        const QString token = arguments.at(i);
        if (!token.startsWith("--") || token == "--")
        {
            parsed.m_positionals.append(token);
            continue;
        }

        QString name = token.mid(2);
        QString value = QStringLiteral("true");

        const int eqIndex = name.indexOf('=');
        if (eqIndex >= 0)
        {
            value = name.mid(eqIndex + 1);
            name = name.left(eqIndex);
        }
        else if (i + 1 < arguments.size() && !arguments.at(i + 1).startsWith("--"))
        {
            value = arguments.at(++i);
        }

        parsed.m_options[name].append(value);
    }

    return parsed;
}

QStringList CliArguments::positionals() const
{
    return m_positionals;
}

QString CliArguments::commandPath(int maxParts) const
{
    QStringList parts;
    for (int i = 0; i < m_positionals.size() && i < maxParts; ++i)
        parts.append(m_positionals.at(i));
    return parts.join('.');
}

bool CliArguments::has(const QString &name) const
{
    return m_options.contains(name);
}

QString CliArguments::value(const QString &name, const QString &defaultValue) const
{
    const QStringList vals = values(name);
    return vals.isEmpty() ? defaultValue : vals.last();
}

QStringList CliArguments::values(const QString &name) const
{
    return m_options.value(name);
}
