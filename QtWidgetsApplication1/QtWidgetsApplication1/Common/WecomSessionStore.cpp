#include "WecomSessionStore.h"
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>
#include <Windows.h>
#include <wincrypt.h>
#pragma comment(lib, "Crypt32.lib")

namespace {
constexpr int MaxBytes = 1024 * 1024;
bool fail(QString *error) {
    if (error) *error = QStringLiteral("本地登录记录无法保存或恢复；可继续使用官方扫码登录。");
    return false;
}
bool crypt(const QByteArray &input, QByteArray *output, bool encrypt) {
    DATA_BLOB source{DWORD(input.size()), reinterpret_cast<BYTE *>(const_cast<char *>(input.constData()))};
    DATA_BLOB result{};
    const BOOL ok = encrypt
        ? CryptProtectData(&source, L"LemonWorkTool Wecom session v1", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result)
        : CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result);
    if (!ok) return false;
    *output = QByteArray(reinterpret_cast<const char *>(result.pbData), int(result.cbData));
    SecureZeroMemory(result.pbData, result.cbData); LocalFree(result.pbData);
    return true;
}
}

QJsonArray WecomSessionStore::usableCookies(const QJsonArray &cookies, double now)
{
    QJsonArray result;
    if (cookies.size() > 256) return result;
    for (const auto &value : cookies) {
        const auto cookie = value.toObject();
        const auto domain = cookie.value("domain").toString().toLower();
        const bool allowed = domain == "doc.weixin.qq.com" || domain == ".doc.weixin.qq.com"
            || domain == ".weixin.qq.com" || domain == ".qq.com";
        const auto expires = cookie.value("expires");
        const auto sameSite = cookie.value("sameSite");
        if (!allowed || cookie.value("name").toString().isEmpty() || !cookie.value("value").isString()
            || !cookie.value("path").toString().startsWith('/') || !expires.isDouble()
            || !cookie.value("secure").isBool() || !cookie.value("httpOnly").isBool()
            || !sameSite.isDouble() || sameSite.toDouble() != sameSite.toInt(-1)
            || sameSite.toInt() < 0 || sameSite.toInt() > 2) continue;
        const double expiry = expires.toDouble();
        // -1 preserves session-cookie semantics. Never extend a server-issued expiry.
        if (!std::isfinite(expiry) || (expiry != -1.0 && expiry <= now)) continue;
        result.append(cookie);
    }
    return result;
}

bool WecomSessionStore::save(const QString &path, const QJsonArray &cookies, QString *error)
{
    if (error) error->clear();
    auto plain = QJsonDocument(QJsonObject{{"version", 1}, {"cookies", usableCookies(cookies, QDateTime::currentMSecsSinceEpoch() / 1000.0)}}).toJson(QJsonDocument::Compact);
    QByteArray encrypted;
    const bool ok = plain.size() <= MaxBytes && crypt(plain, &encrypted, true);
    plain.fill('\0');
    if (!ok) return fail(error);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(encrypted) != encrypted.size() || !file.commit()) return fail(error);
    return true;
}

bool WecomSessionStore::load(const QString &path, QJsonArray *cookies, QString *error)
{
    *cookies = QJsonArray(); if (error) error->clear();
    QFile file(path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > MaxBytes + 4096) return fail(error);
    QByteArray plain;
    if (!crypt(file.readAll(), &plain, false)) return fail(error);
    const auto document = QJsonDocument::fromJson(plain); plain.fill('\0');
    const auto object = document.object();
    if (object.value("version").toInt() != 1 || !object.value("cookies").isArray()) return fail(error);
    *cookies = usableCookies(object.value("cookies").toArray(), QDateTime::currentMSecsSinceEpoch() / 1000.0);
    return true;
}
