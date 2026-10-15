#pragma once
#include <QJsonArray>
#include <QString>

// Only this feature's document cookies; encrypted for the current Windows user.
namespace WecomSessionStore {
bool save(const QString &path, const QJsonArray &cookies, QString *error);
bool load(const QString &path, QJsonArray *cookies, QString *error);
QJsonArray usableCookies(const QJsonArray &cookies, double now);
}
