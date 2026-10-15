#include "SudaBackupWidget.h"
#include "Common/repairpathconfigwidget.h"
#include "Common/repairpathsettings.h"
#include "Common/repairinputfileutils.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <cstdio>
#include <stdexcept>

static void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
    std::printf("PASS %s\n", message);
}

static bool drag(QWidget* target, const QList<QUrl>& urls, bool drop = true,
    Qt::DropActions actions = Qt::CopyAction)
{
    QMimeData data;
    data.setUrls(urls);
    QDragEnterEvent enter(QPoint(5, 5), actions, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    if (!enter.isAccepted() || !drop) return enter.isAccepted();
    QDropEvent event(QPointF(5, 5), actions, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
    return event.isAccepted();
}

static QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("read fixture");
    return file.readAll();
}

static void testInputRetention(const QDir& dir, const QString& source)
{
    const QByteArray original = readFile(source);
    const QString previousInput = RepairPathSettings::inputDir();
    const QString targetDir = dir.filePath("retention/nested");
    RepairPathSettings::setInputDir(targetDir);
    QString error;
    const QString first = RepairInputFileUtils::copyToInputDirectory(source, &error);
    check(!first.isEmpty() && error.isEmpty() && readFile(first) == original,
        "copy creates missing shared input directory and preserves content");
    const QString second = RepairInputFileUtils::copyToInputDirectory(source, &error);
    check(QFileInfo(second).fileName() == QStringLiteral("中文 备份_2.BAK")
        && readFile(first) == original && readFile(second) == original,
        "duplicate names use suffix without overwriting");
    check(RepairInputFileUtils::copyToInputDirectory(first, &error) == first,
        "reuse file already in input directory");
    QDir().mkpath(QDir(targetDir).filePath("child"));
    const QString nested = QDir(targetDir).filePath("child/a.BAK");
    check(QFile::copy(source, nested), "create nested fixture");
    check(RepairInputFileUtils::copyToInputDirectory(nested, &error) == nested,
        "reuse file inside input subdirectory");
    RepairPathSettings::setInputDir(first + "/blocked");
    check(RepairInputFileUtils::copyToInputDirectory(source, &error).isEmpty() && !error.isEmpty(),
        "copy failure is reported without returning source path");
    RepairPathSettings::setInputDir(targetDir);
    check(RepairInputFileUtils::copyToInputDirectory(dir.filePath("missing.BAK"), &error).isEmpty()
        && !error.isEmpty(), "missing source rejected");
    check(RepairInputFileUtils::copyToInputDirectory(targetDir, &error).isEmpty(),
        "directories are not copied as files");
    check(readFile(source) == original, "external source remains unchanged");
    RepairPathSettings::setInputDir(previousInput);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const int fontId = QFontDatabase::addApplicationFont(
        QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/msyh.ttc"));
    const auto families = QFontDatabase::applicationFontFamilies(fontId);
    if (!families.isEmpty()) app.setFont(QFont(families.first(), 10));
    try
    {
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary workspace");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temporary.path());
        const QDir dir(temporary.path());
        RepairPathSettings::setInputDir(dir.filePath("input"));
        RepairPathSettings::setOutputDir(dir.filePath("output/nested"));
        check(RepairPathSettings::inputDir() == dir.filePath("input"), "isolated shared input setting");
        check(QDir().mkpath(RepairPathSettings::inputDir()), "create input directory");

        const QByteArray content = QByteArray("TAPE") + QByteArray(1024 * 1024, 'x');
        QByteArray payload = qCompress(content).mid(4);
        quint32 seed = 0xBC2FFu;
        for (int i = 0; i < payload.size(); ++i)
        {
            seed = seed * 0x08088405u + 1u;
            payload[i] = static_cast<char>(static_cast<unsigned char>(payload.at(i))
                + ((quint64(seed) * quint64(i % 16384 + 1)) >> 32));
        }
        QByteArray header(181, '\0');
        header.replace(0, 33, QByteArray("\x20" "SD19960617-1998-0908-SDSOFT-ZWLQ", 33));
        const QString source = dir.filePath(QStringLiteral("中文 备份.BAK"));
        QFile input(source);
        check(input.open(QIODevice::WriteOnly), "create fixture");
        input.write(header + payload);
        input.close();
        testInputRetention(dir, source);

        SudaBackupWidget page;
        page.setAttribute(Qt::WA_DontShowOnScreen);
        page.resize(1000, 700);
        page.show();
        app.processEvents();
        auto* sourceEdit = page.findChild<QLineEdit*>("sudaSourcePath");
        auto* outputEdit = page.findChild<QLineEdit*>("sudaOutputPath");
        auto* start = page.findChild<QPushButton*>("sudaStartButton");
        auto* log = page.findChild<QPlainTextEdit*>("sudaUnpackLog");
        check(sourceEdit && outputEdit && start && log, "page controls");
        QList<QUrl> urls{QUrl::fromLocalFile(source)};
        check(drag(&page, urls, false) && QDir(RepairPathSettings::inputDir()).entryList(QDir::Files).isEmpty(),
            "drag hover does not copy file");
        check(!drag(&page, urls, true, Qt::MoveAction)
            && QDir(RepairPathSettings::inputDir()).entryList(QDir::Files).isEmpty(),
            "reject move-only drop to preserve original file");
        check(drag(&page, urls), "drop local file on page");
        const QString retained = QDir(RepairPathSettings::inputDir()).filePath(QFileInfo(source).fileName());
        check(QFileInfo(sourceEdit->text()).absoluteFilePath() == retained, "drop selects retained input path");
        check(readFile(retained) == readFile(source) && !QFileInfo::exists(outputEdit->text()),
            "drop retains copy before unpack starts");
        urls = {QUrl::fromLocalFile(retained)};
        const QString expected = dir.filePath(QStringLiteral("output/nested/中文 备份_解包.bak"));
        check(QDir::fromNativeSeparators(outputEdit->text()) == expected, "default output uses shared directory");
        check(!drag(&page, {QUrl::fromLocalFile(temporary.path())}), "reject directories");
        check(!drag(&page, {QUrl("https://example.com/a.bak")}), "reject remote URLs");
        check(!drag(&page, {QUrl::fromLocalFile(dir.filePath("missing.bak"))}), "reject missing files");
        check(!drag(&page, {urls.first(), urls.first()}), "reject multiple files without discarding extras");
        sourceEdit->clear();
        check(outputEdit->text().isEmpty(), "clear stale default output");
        check(drag(sourceEdit, urls), "drop over source editor reaches page");
        check(drag(outputEdit, urls), "drop over output editor selects source");
        check(drag(log->viewport(), urls), "drop over log reaches page");

        page.hide();
        RepairPathSettings::setOutputDir(dir.filePath("changed/nested"));
        page.show();
        app.processEvents();
        check(QFileInfo(outputEdit->text()).absolutePath() == RepairPathSettings::outputDir(),
            "returning to page refreshes shared output");
        const QString output = outputEdit->text();
        bool finished = false, success = false;
        QObject::connect(&page, &SudaBackupWidget::unpackFinished, &page,
            [&](bool ok, const QString&) { finished = true; success = ok; });
        start->click();
        check(!start->isEnabled(), "unpack enters busy state");
        check(!drag(&page, urls), "reject drops while unpacking");
        const auto configs = page.findChildren<QWidget*>();
        for (auto* widget : configs)
            if (dynamic_cast<RepairPathConfigWidget*>(widget))
                check(!widget->isEnabled(), "shared directory controls disabled while busy");
        QElapsedTimer timer;
        timer.start();
        while (!finished && timer.elapsed() < 10000)
        {
            app.processEvents();
            QThread::msleep(5);
        }
        check(finished && success && start->isEnabled(), "unpack succeeds and returns to idle");
        QFile result(output);
        check(result.open(QIODevice::ReadOnly) && result.readAll() == content,
            "creates shared output directory and writes correct content");
        result.close();
        start->click();
        check(start->isEnabled() && log->toPlainText().contains(QStringLiteral("已存在")),
            "existing output remains protected");
        if (app.arguments().size() > 2)
        {
            const QString sample = app.arguments().at(2);
            const auto original = readFile(sample);
            check(drag(&page, {QUrl::fromLocalFile(sample)}), "drop real SuperData sample");
            check(readFile(sourceEdit->text()) == original, "retain real SDB before unpack");
            finished = false; success = false;
            start->click(); timer.restart();
            while (!finished && timer.elapsed() < 30000) { app.processEvents(); QThread::msleep(5); }
            check(finished && success && start->isEnabled(), "real SuperData widget completes");
            check(QCryptographicHash::hash(readFile(outputEdit->text()), QCryptographicHash::Sha256).toHex()
                == "efc522c6269b0c9765075fca851cb4df02232254983b76c74ee2b55f7a67009e",
                "real SuperData widget output matches independent extraction");
            check(readFile(sample) == original, "real SDB source remains unchanged");
        }
        if (app.arguments().size() > 1) page.grab().save(app.arguments().at(1));
        std::puts("ALL TESTS PASSED");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
