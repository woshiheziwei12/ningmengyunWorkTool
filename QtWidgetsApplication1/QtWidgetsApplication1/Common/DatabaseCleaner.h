#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace DatabaseCleaner {
enum class Target { Local, Server211 };
struct Database {
    int id = 0;
    QString name;
    QString created; // SQL Server ISO 8601 datetime, including milliseconds.
    QString state;
    int sourceId = 0;
    bool distributor = false;
};
struct Catalog {
    Target target = Target::Local;
    QString server;
    QString localInstance = QStringLiteral("."); // Endpoint captured when the catalog was read.
    QVector<Database> databases;
    QString error;
};
struct ItemResult {
    QString name;
    bool success = false;
    QString error;
};
struct Result {
    QVector<ItemResult> items;
    QString error;
    QStringList remaining;
    bool stopped = false;
};
using Cancel = std::function<bool()>;
using Progress = std::function<void(const ItemResult&)>;

// A catalog and deletion always use the same session. This boundary also permits
// exercising the destructive workflow with an in-memory session in offline tests.
class Session {
public:
    virtual ~Session() = default;
    virtual Catalog read() = 0;
    virtual QString drop(const Database& database) = 0;
};
QString targetLabel(Target target, const QString& localInstance = QStringLiteral("."));
bool isUserDatabase(const Database& database);
QString dropSql(const Database& database);
Catalog inspect(Target target, const QString& localInstance = QStringLiteral("."));
// confirmed.databases contains only the explicitly selected databases.
Result removeSelected(const Catalog& confirmed, const Cancel& cancel = {}, const Progress& progress = {});
Result removeSelected(Session& session, const Catalog& confirmed, const Cancel& cancel = {}, const Progress& progress = {});
}
