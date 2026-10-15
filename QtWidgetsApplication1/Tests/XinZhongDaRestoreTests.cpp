#include <QtTest>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSslSocket>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QUuid>
#include "XinZhongDaRestoreWidget.h"

static QByteArray literalLzw(const QByteArray& data)
{
    QVector<int> codes;
    int n = 0;
    for (unsigned char ch : data) {
        codes.append(ch);
        if (++n % 100 == 0)
            codes.append(258);
    }
    codes.append(256);
    QByteArray result;
    int bits = 0, value = 0;
    for (int code : codes)
        for (int i = 8; i >= 0; --i) {
            value = (value << 1) | ((code >> i) & 1);
            if (++bits == 8) {
                result.append(char(value));
                bits = 0;
                value = 0;
            }
        }
    if (bits)
        result.append(char(value << (8 - bits)));
    return result;
}
static void save(const QString& path, const QByteArray& data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        qFatal("Cannot write fixture");
}
static void fixture(const QString& dir, int year, const QString& name = {}, bool voucher = false)
{
    QDir().mkpath(dir);
    save(dir + "/ZWDATA.VAL", QString("010\r\nZWB188 %1\r\nVer 3.1\r\nMessage=%2\r\n").arg(year).arg(name).toUtf8());
    save(dir + "/km.tx_", literalLzw("1001\tcash\t\t1\t1\t\t0\t0\t\r\n"));
    QStringList hz;
    for (int i = 0; i < 32; ++i)
        hz.append("");
    hz[0] = "1001";
    hz[1] = "cash";
    hz[2] = "1";
    hz[3] = "100.25";
    hz[30] = "1";
    save(dir + "/hz.tx_", literalLzw((hz.join('\t') + "\r\n").toUtf8()));
    for (int m = 1; m <= 12; ++m)
        save(dir + QString("/z_pz%1.tx_").arg(m, 2, 10, QChar('0')), QByteArray::fromHex("804000"));
    if (voucher) {
        QStringList row;
        for (int i = 0; i < 39; ++i)
            row.append("");
        row[0] = "1";
        row[2] = QString::number(year) + "-12-31 00:00:00";
        row[3] = "1";
        row[4] = "0001";
        row[7] = "1001";
        row[8] = "100.25";
        row[14] = "1";
        save(dir + "/z_pz12.tx_", literalLzw((row.join('\t') + "\r\n").toUtf8()));
    }
}
static bool sql(QSqlDatabase& db, const QString& text)
{
    QSqlQuery q(db);
    if (!q.exec(text)) {
        qWarning().noquote() << q.lastError().text();
        return false;
    }
    do {
        while (q.next()) { }
    } while (q.nextResult());
    return !q.lastError().isValid();
}
struct TestDatabases {
    QString connection = "XzdTest_" + QUuid::createUuid().toString(QUuid::Id128);
    QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", connection);
    QStringList names;
    ~TestDatabases()
    {
        if (db.isOpen())
            for (const auto& name : names) {
                const QString lit = T3BackupRestorer::quoteLiteral(name);
                // Unique test names plus restorer physical-file token. No force disconnect.
                sql(db,
                    "USE master; IF DB_ID(" + lit
                        + ") IS NOT NULL AND NOT EXISTS(SELECT 1 FROM sys.master_files WHERE database_id=DB_ID(" + lit
                        + ") AND physical_name NOT LIKE '%xzd[_]%') DROP DATABASE "
                        + T3BackupRestorer::quoteIdentifier(name));
            }
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection);
    }
};
class XinZhongDaRestoreTests : public QObject {
    Q_OBJECT
private slots:
    void packagedSsl()
    {
        if (!qEnvironmentVariableIsSet("XZD_CHECK_SSL"))
            QSKIP("Set XZD_CHECK_SSL for standalone package validation");
        QVERIFY(QSslSocket::supportsSsl());
        qInfo() << "SSL runtime" << QSslSocket::sslLibraryVersionString();
    }
    void metadataAndNames()
    {
        XinZhongDaBackupAnalysis a;
        QVERIFY(XinZhongDaBackupRestorer::readMetadata("010\r\nZWB188 2026\r\nVer 3.1\r\nMessage= \r\n", "folder", &a));
        QCOMPARE(a.accountName, QString("folder"));
        QCOMPARE(a.accountCode, QString("B188"));
        QVERIFY(!a.nameFromBackup);
        const QString val = QString::fromUtf8(u8"010\r\nZW1000 2026\r\nVer 3.1\r\nMessage=公共服务有限责任公司\r\n");
        for (const auto& bytes : { val.toUtf8(), QTextCodec::codecForName("GB18030")->fromUnicode(val),
                 QTextCodec::codecForName("UTF-16LE")->fromUnicode(val) }) {
            QVERIFY(XinZhongDaBackupRestorer::readMetadata(bytes, "other", &a));
            QCOMPARE(a.accountName, QString::fromUtf8(u8"公共服务有限责任公司"));
            QVERIFY(a.nameFromBackup);
        }
        QCOMPARE(XinZhongDaBackupRestorer::targetDatabase("B176", 2023), QString("USERB1762023"));
        QVERIFY(XinZhongDaBackupRestorer::targetDatabase("x]; DROP", 2023).isEmpty());
        QVERIFY(!XinZhongDaBackupRestorer::readMetadata("ZW01 2026\r\nVer 9.0\r\n", "x", &a));
    }
    void decodeBoundaries()
    {
        QByteArray out;
        QString error;
        const QByteArray text = QByteArray(1000, 'a') + "\t123.4\r\n";
        QVERIFY(XinZhongDaBackupRestorer::decode(literalLzw(text), &out, &error));
        QCOMPARE(out, text);
        QVERIFY(XinZhongDaBackupRestorer::decode(QByteArray::fromHex("804000"), &out, &error));
        QVERIFY(out.isEmpty());
        QVERIFY(!XinZhongDaBackupRestorer::decode(literalLzw(text).chopped(1), &out, &error));
        QVERIFY(!XinZhongDaBackupRestorer::decode(QByteArray::fromHex("ffff"), &out, &error));
        QVERIFY(!XinZhongDaBackupRestorer::decode(literalLzw(text), &out, &error, [] { return true; }));
        QVERIFY(!XinZhongDaBackupRestorer::decode(literalLzw(text) + "bad", &out, &error));
    }
    void snapshotsAndDuplicates()
    {
        QTemporaryDir dir;
        fixture(dir.path() + "/2023/01", 2023);
        fixture(dir.path() + "/2023/12", 2023, {}, true);
        fixture(dir.path() + "/2024/01", 2024);
        QString error;
        const auto catalog = XinZhongDaBackupRestorer::discover(dir.path(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(catalog.size(), 3);
        QCOMPARE(catalog[0].year, 2023);
        QCOMPARE(catalog[0].voucherRows, qint64(1));
        QCOMPARE(catalog[0].accountName, QFileInfo(dir.path()).fileName());
        QVERIFY(catalog[0].tables.isEmpty());
        T3SqlConnection unused;
        const auto duplicate = XinZhongDaBackupRestorer::restoreMany(unused, { catalog[0], catalog[1] }, "B188");
        QVERIFY(!duplicate.success);
        QVERIFY(duplicate.error.contains(QString::fromUtf8(u8"同一年度")));
        QFile file(dir.path() + "/2023/12/km.tx_");
        QVERIFY(file.open(QIODevice::Append));
        file.write("bad");
        file.close();
        const auto changed = XinZhongDaBackupRestorer::restore(unused, catalog[0], "B188");
        QVERIFY(!changed.success);
        QVERIFY(changed.error.contains(QString::fromUtf8(u8"已改变")));
    }
    void uiRead()
    {
        QTemporaryDir dir;
        fixture(dir.path() + "/2023/01", 2023);
        fixture(dir.path() + "/2023/12", 2023, {}, true);
        fixture(dir.path() + "/2024/01", 2024);
        XinZhongDaRestoreWidget page;
        auto* source = page.findChild<QLineEdit*>("xzdSource");
        auto* button = page.findChild<QPushButton*>("xzdAnalyze");
        auto* years = page.findChild<QTableWidget*>("xzdYears");
        QVERIFY(source && button && years);
        source->setText(dir.path());
        QSignalSpy finished(&page, &XinZhongDaRestoreWidget::operationFinished);
        button->click();
        QVERIFY(page.property("sessionBusyXzdRestore").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
        QCOMPARE(years->rowCount(), 2);
        auto* combo = qobject_cast<QComboBox*>(years->cellWidget(0, 2));
        QCOMPARE(combo->count(), 2);
        QVERIFY(combo->currentText().startsWith("12"));
        source->setText("changed");
        QCOMPARE(years->rowCount(), 0);
        QVERIFY(!page.findChild<QPushButton*>("xzdRestore")->isEnabled());
    }
    void realSamples()
    {
        const auto paths = qEnvironmentVariable("XZD_SAMPLE_DIRS").split('|', QString::SkipEmptyParts);
        if (paths.isEmpty())
            QSKIP("Set XZD_SAMPLE_DIRS for read-only real samples");
        for (const auto& path : paths) {
            QString error;
            const auto catalog = XinZhongDaBackupRestorer::discover(path, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QVERIFY(!catalog.isEmpty());
            QSet<int> years;
            for (const auto& c : catalog)
                years.insert(c.year);
            qInfo().noquote() << path << "snapshots" << catalog.size() << "years" << years.size();
        }
    }
    void realRestore()
    {
        const auto root = qEnvironmentVariable("XZD_TEST_ROOT");
        if (root.isEmpty())
            QSKIP("Set XZD_TEST_ROOT for isolated SQL integration");
        QString error;
        const auto catalog = XinZhongDaBackupRestorer::discover(root, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVector<XinZhongDaBackupAnalysis> selected;
        QSet<int> years;
        for (const auto& c : catalog)
            if (!years.contains(c.year)) {
                selected.append(c);
                years.insert(c.year);
            }
        const QString code = "T" + QUuid::createUuid().toString(QUuid::Id128).left(9);
        const QString control = "XzdTest_" + code;
        T3SqlConnection options;
        options.server = qEnvironmentVariable("XZD_TEST_SERVER", ".\\SQL2008R2");
        TestDatabases cleanup;
        cleanup.db.setDatabaseName(T3BackupRestorer::connectionString(options));
        QVERIFY2(cleanup.db.open(), qPrintable(cleanup.db.lastError().text()));
        cleanup.names.append(control);
        for (const auto& a : selected)
            cleanup.names.append(XinZhongDaBackupRestorer::targetDatabase(code, a.year));
        const auto restored = XinZhongDaBackupRestorer::restoreMany(options, selected, code, {}, {}, control);
        QVERIFY2(restored.success, qPrintable(restored.error));
        for (const auto& a : selected) {
            const QString db = XinZhongDaBackupRestorer::targetDatabase(code, a.year);
            QVERIFY(sql(cleanup.db, "USE " + T3BackupRestorer::quoteIdentifier(db)));
            QStringList unionParts;
            for (int m = 1; m <= 12; ++m)
                unionParts.append(QString("SELECT * FROM dbo.z_pz%1").arg(m, 2, 10, QChar('0')));
            const QString all = "(" + unionParts.join(" UNION ALL ") + ")";
            const QString keys = "MONTH(p.pzrq),p.pzlx,p.pzh,p.kmdm";
            const QString base = "SELECT " + keys + ",SUM(ISNULL(p.j,0)) j,SUM(ISNULL(p.d,0)) d FROM " + all
                + " p WHERE p.shbz=1 GROUP BY " + keys;
            const QString collected = "SELECT " + keys
                + ",SUM(ISNULL(x.j,ISNULL(p.j,0))) j,SUM(ISNULL(x.d,ISNULL(p.d,0))) d FROM " + all
                + " p LEFT JOIN dbo.z_pzxm x ON p.pzh=x.pzh AND p.pzlx=x.pzlx AND p.id2=x.id2 AND MONTH(p.pzrq)=x.mon "
                  "AND p.kmdm=x.kmdm WHERE p.shbz=1 GROUP BY "
                + keys;
            QSqlQuery q(cleanup.db);
            for (const auto& comparison : { base + " EXCEPT " + collected, collected + " EXCEPT " + base }) {
                QVERIFY2(q.exec("SELECT COUNT(*) FROM (" + comparison + ") mismatch(mon,pzlx,pzh,kmdm,j,d)"),
                    qPrintable(q.lastError().text()));
                QVERIFY(q.next());
                QCOMPARE(q.value(0).toInt(), 0);
                q.finish();
            }
            QVERIFY(q.exec("SELECT COUNT(*) FROM " + all + " p"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toLongLong(), a.voucherRows);
            q.finish();
            QVERIFY(q.exec("SELECT COUNT(*) FROM " + all + " p WHERE shbz=1"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toLongLong(), a.approvedVoucherRows);
            q.finish();
            QVERIFY(sql(cleanup.db,
                "SELECT k.kmdm,h.ncye,s.ncjcsl FROM km k JOIN hz h ON k.kmdm=h.kmdm LEFT JOIN slhz s ON "
                "k.kmdm=s.kmdm"));
            QVERIFY(sql(cleanup.db, "DBCC CHECKDB WITH NO_INFOMSGS, ALL_ERRORMSGS"));
        }
        const auto again = XinZhongDaBackupRestorer::restoreMany(options, selected, code, {}, {}, control);
        QVERIFY(!again.success);
        QVERIFY(again.error.contains(QString::fromUtf8(u8"已存在")));
        qInfo() << "Restored and checked" << selected.size() << "annual databases; all voucher/account totals match";
    }
    void rollbackAndControlProtection()
    {
        if (qEnvironmentVariableIsEmpty("XZD_TEST_ROOT"))
            QSKIP("Set XZD_TEST_ROOT for isolated SQL integration");
        QTemporaryDir dir;
        fixture(dir.path(), 2025, QString::fromUtf8(u8"测试账套"));
        const auto a = XinZhongDaBackupRestorer::analyze(dir.path());
        QVERIFY2(a.success, qPrintable(a.error));
        T3SqlConnection options;
        options.server = qEnvironmentVariable("XZD_TEST_SERVER", ".\\SQL2008R2");
        const QString code = "T" + QUuid::createUuid().toString(QUuid::Id128).left(9), control = "XzdTest_" + code;
        const QString db = XinZhongDaBackupRestorer::targetDatabase(code, 2025);
        TestDatabases cleanup;
        cleanup.db.setDatabaseName(T3BackupRestorer::connectionString(options));
        QVERIFY(cleanup.db.open());
        cleanup.names = QStringList { db, control };
        bool cancel = false;
        auto result = XinZhongDaBackupRestorer::restore(
            options, a, code, [&]() { return cancel; }, [&](const QString&) { cancel = true; }, control);
        QVERIFY(!result.success);
        QVERIFY(result.cancelled);
        QSqlQuery q(cleanup.db);
        QVERIFY(q.exec("SELECT DB_ID('" + db + "'),DB_ID('" + control + "')"));
        QVERIFY(q.next());
        QVERIFY(q.value(0).isNull());
        QVERIFY(q.value(1).isNull());
        q.finish();
        result = XinZhongDaBackupRestorer::restore(options, a, code, {}, {}, control);
        QVERIFY2(result.success, qPrintable(result.error));
        QVERIFY(sql(cleanup.db,
            "ALTER TABLE " + T3BackupRestorer::quoteIdentifier(control)
                + ".dbo.unit ADD extra_required int NOT NULL DEFAULT 1"));
        QVERIFY(sql(cleanup.db,
            "ALTER TABLE " + T3BackupRestorer::quoteIdentifier(control)
                + ".dbo.unit ADD CONSTRAINT RejectNew CHECK(dwdm='" + code + "')"));
        const QString second = code + "B", secondDb = XinZhongDaBackupRestorer::targetDatabase(second, 2025);
        cleanup.names.append(secondDb);
        result = XinZhongDaBackupRestorer::restore(options, a, second, {}, {}, control);
        QVERIFY(!result.success);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(q.exec("SELECT DB_ID('" + secondDb + "'),(SELECT COUNT(*) FROM "
            + T3BackupRestorer::quoteIdentifier(control) + ".dbo.unit)"));
        QVERIFY(q.next());
        QVERIFY(q.value(0).isNull());
        QCOMPARE(q.value(1).toInt(), 1);
        q.finish();
    }
};
QTEST_MAIN(XinZhongDaRestoreTests)
#include "XinZhongDaRestoreTests.moc"
