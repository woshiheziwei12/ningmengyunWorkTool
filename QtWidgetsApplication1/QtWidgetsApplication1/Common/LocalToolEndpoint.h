#pragma once
#include <QString>

namespace LocalToolEndpoint {
constexpr int ProtocolVersion = 1;
constexpr int MaxRequestBytes = 8 * 1024;
constexpr int MaxResponseBytes = 64 * 1024 * 1024;

// Names a tool pipe for the current Windows user.
QString name(const QString &service);
}
