#include "LocalToolEndpoint.h"
#include <QByteArray>
#include <Windows.h>
#include <sddl.h>
#pragma comment(lib, "Advapi32.lib")

QString LocalToolEndpoint::name(const QString &service)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray buffer(int(size), '\0');
    const BOOL ok = GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!ok || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) return {};
    const QString name = QStringLiteral("LemonWorkTool.%1.v1.%2").arg(service, QString::fromWCharArray(sid));
    LocalFree(sid);
    return name;
}
