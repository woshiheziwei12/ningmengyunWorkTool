#include "Common/SqlServerCredentials.h"
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

static int checks = 0;
static void check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary configuration directory");
    const QByteArray configPath = (temporary.path() + "/SqlServer211.local.json").toUtf8();
    qputenv("LEMON_SQL211_CONFIG", configPath);
    for (const auto* name : {"LEMON_SQL211_USER", "LEMON_SQL211_PASSWORD", "LEMON_SMB211_USER", "LEMON_SMB211_PASSWORD"}) qunsetenv(name);
    T3SqlConnection options;
    SqlServerCredentials::apply211(options);
    check(options.user == "sa" && options.password.isEmpty(), "missing file has no embedded password");
    check(SqlServerCredentials::share211().first.isEmpty(), "missing file has no embedded share account");
    QFile file(QString::fromUtf8(configPath));
    check(file.open(QIODevice::WriteOnly), "write synthetic configuration");
    file.write("{\"user\":\"local-user\",\"password\":\"synthetic;{}\\\"\",\"shareUser\":\"local-share\",\"sharePassword\":\"synthetic-share\"}");
    file.close();
    SqlServerCredentials::apply211(options);
    check(options.user == "local-user" && options.password == "synthetic;{}\"", "file values preserve special characters");
    check(SqlServerCredentials::share211() == qMakePair(QString("local-share"), QString("synthetic-share")), "independent share credentials");
    qputenv("LEMON_SQL211_USER", "environment-user");
    qputenv("LEMON_SQL211_PASSWORD", "synthetic-environment");
    qputenv("LEMON_SMB211_USER", "environment-share");
    qputenv("LEMON_SMB211_PASSWORD", "synthetic-share-environment");
    SqlServerCredentials::apply211(options);
    check(options.user == "environment-user" && options.password == "synthetic-environment", "environment overrides local SQL configuration");
    check(SqlServerCredentials::share211().second == "synthetic-share-environment", "environment overrides share configuration");
    // An absent or malformed file must clear earlier credentials rather than retain them.
    qunsetenv("LEMON_SQL211_USER"); qunsetenv("LEMON_SQL211_PASSWORD");
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "write malformed configuration");
    file.write("not json"); file.close();
    SqlServerCredentials::apply211(options);
    check(options.user == "sa" && options.password.isEmpty(), "invalid file cannot reuse previous password");
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "write oversized configuration");
    file.write(QByteArray(16385, ' ')); file.close();
    SqlServerCredentials::apply211(options);
    check(options.password.isEmpty(), "oversized configuration rejected");
    std::printf("PASS: %d local credential checks; no network or real credentials used.\n", checks);
    return 0;
}
