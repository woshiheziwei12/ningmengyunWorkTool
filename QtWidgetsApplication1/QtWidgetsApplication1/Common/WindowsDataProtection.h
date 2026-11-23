#pragma once

#include <QByteArray>
#include <Windows.h>
#include <wincrypt.h>
#pragma comment(lib, "Crypt32.lib")

namespace WindowsDataProtection {
// Uses the current Windows user as the DPAPI scope for encryption and recovery.
inline bool transform(const QByteArray &input, QByteArray *output, bool encrypt, const wchar_t *description = nullptr)
{
    DATA_BLOB source{DWORD(input.size()), reinterpret_cast<BYTE *>(const_cast<char *>(input.constData()))};
    DATA_BLOB result{};
    const BOOL ok = encrypt
        ? CryptProtectData(&source, description, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result)
        : CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result);
    if (!ok) return false;
    *output = QByteArray(reinterpret_cast<const char *>(result.pbData), int(result.cbData));
    SecureZeroMemory(result.pbData, result.cbData);
    LocalFree(result.pbData);
    return true;
}
}
