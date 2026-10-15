#include <QtWidgets>
#include "../QtWidgetsApplication1/vouchergroupnumwidget.h"
#include "../QtWidgetsApplication1/vouchergroupnumwidget.cpp"
#include <cstdio>
#include <stdexcept>

class VoucherGroupNumTestAccess
{
public:
    static void process(VoucherGroupNumWidget &widget, const QString &path) {
        widget.m_batchProcessing = true;
        widget.processZip(path);
    }
    static int successes(const VoucherGroupNumWidget &widget) { return widget.m_batchSuccessCount; }
    static int noChanges(const VoucherGroupNumWidget &widget) { return widget.m_batchNoChangeCount; }
    static QString log(const VoucherGroupNumWidget &widget) { return widget.m_pLogEdit->toPlainText(); }
    static void finish(VoucherGroupNumWidget &widget) { widget.setRunning(false); }
};

static void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
static QJsonObject voucher(const QJsonValue &number, const QString &date = "2026-04-30", const QString &group = "J")
{
    return QJsonObject{{"GroupNum", number}, {"MakeDate", date}, {"VchGroup", group}, {"DebitBal", "5.28"}};
}
static QJsonObject rootFor(const QJsonArray &rows)
{
    return QJsonObject{{"GLVoucherList", rows}, {"Unrelated", "preserve"}};
}
static QString numberAt(const QJsonObject &root, int index)
{
    return root.value("GLVoucherList").toArray().at(index).toObject().value("GroupNum").toVariant().toString();
}
static QByteArray bytes(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read file");
    return file.readAll();
}
static void save(const QString &path, const QByteArray &data)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "open output");
    require(file.write(data) == data.size(), "write complete output");
}
static void unchangedExceptNumbers(QJsonObject before, QJsonObject after)
{
    auto a = before.value("GLVoucherList").toArray(), b = after.value("GLVoucherList").toArray();
    require(a.size() == b.size(), "row count preserved");
    for (int i = 0; i < a.size(); ++i) {
        auto x = a[i].toObject(), y = b[i].toObject();
        bool ok = false;
        y.value("GroupNum").toVariant().toString().toInt(&ok);
        require(ok, "every output number parses as Int32");
        x.remove("GroupNum"); y.remove("GroupNum");
        require(x == y, "all non-number voucher data preserved");
    }
    before.remove("GLVoucherList"); after.remove("GLVoucherList");
    require(before == after, "all other root data preserved");
}
static void unitTests()
{
    auto root = rootFor({voucher("1.1"), voucher("1.1"), voucher("1.0"), voucher("8.00"),
        voucher("2.3"), voucher("12", "2026-04-02", "S"), voucher("1.1", "2026-04-01", "S"),
        voucher("1.1", "2026-05-01"), voucher("30.0", "2025-04-01"), voucher("1.1", "2025-04-02")});
    const auto before = root;
    const auto r = VoucherGroupNumRepair::repair(root);
    require(r.error.isEmpty(), "mixed case success");
    require(numberAt(root,0)=="13" && numberAt(root,1)=="13", "same voucher uses full-month maximum");
    require(numberAt(root,2)=="1" && numberAt(root,3)=="8", "integral decimals normalized");
    require(numberAt(root,4)=="14" && numberAt(root,6)=="15", "increments across vouchers and groups");
    require(numberAt(root,7)=="1" && numberAt(root,9)=="31", "month and year isolation");
    require(r.normalizedVouchers==3 && r.renumberedVouchers==5 && r.changedRows==9 && r.details.size()==8, "counts and per-voucher detail");
    unchangedExceptNumbers(before,root);
    const auto once=root;
    require(VoucherGroupNumRepair::repair(root).changedRows==0 && root==once, "idempotent");

    root=rootFor({voucher("1.0000000000000001"),voucher("0009.000"),voucher(" +0003 "),voucher(7),voucher(1.5)});
    require(VoucherGroupNumRepair::repair(root).error.isEmpty(), "precision fixture success");
    require(numberAt(root,0)=="10" && numberAt(root,1)=="9" && numberAt(root,2)==" +0003 " && numberAt(root,4)=="11", "exact decimals and valid originals");
    auto fallback=voucher("2.0",""); fallback["IntYear"]=2026; fallback["IntPeriod"]=6;
    root=rootFor({fallback});
    require(VoucherGroupNumRepair::repair(root).error.isEmpty() && numberAt(root,0)=="2", "period fallback");
    for (const auto &bad : {voucher("text"),voucher(""),voucher(QJsonValue::Null),voucher("2147483648.0"),voucher("1.1","bad")}) {
        root=rootFor({voucher("1.0"),bad}); const auto unchanged=root;
        require(!VoucherGroupNumRepair::repair(root).error.isEmpty() && root==unchanged, "invalid data fails atomically");
    }
    root=rootFor({voucher("1.0"),voucher("2147483647"),voucher("1.1")}); const auto unchanged=root;
    require(!VoucherGroupNumRepair::repair(root).error.isEmpty() && root==unchanged, "Int32 exhaustion fails atomically");
    root=rootFor({}); require(VoucherGroupNumRepair::repair(root).error.isEmpty(), "empty list accepted");
    root=QJsonObject(); require(!VoucherGroupNumRepair::repair(root).error.isEmpty(), "missing list rejected");
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    try {
        const QString work=qEnvironmentVariable("GROUPNUM_TEST_WORK");
        require(!work.isEmpty() && QDir().mkpath(work), "explicit isolated test workspace");
        QCoreApplication::setOrganizationName("LemonGroupNumTests");
        QCoreApplication::setApplicationName("VoucherGroupNumTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,work+"/settings");
        RepairPathSettings::setInputDir(work+"/input");
        RepairPathSettings::setOutputDir(work+"/output");
        QDir().mkpath(work+"/input"); QDir().mkpath(work+"/output");
        unitTests();

        // Exercise the actual ZIP and widget pipeline with an unrelated ZIP member.
        const auto fixture=rootFor({voucher("1.1"),voucher("1.1"),voucher("9.0")});
        save(work+"/input/AccountSet.json",QJsonDocument(fixture).toJson());
        const QByteArray marker("untouched\0payload",17);
        save(work+"/input/keep.bin",marker);
        auto env=QProcessEnvironment::systemEnvironment();
        env.insert("GROUP_TEST_DIR",work+"/input"); env.insert("GROUP_TEST_ZIP",work+"/fixture.zip");
        QString error;
        require(runPowerShell(QStringLiteral("$ErrorActionPreference='Stop';Add-Type -AssemblyName System.IO.Compression.FileSystem;[IO.Compression.ZipFile]::CreateFromDirectory($env:GROUP_TEST_DIR,$env:GROUP_TEST_ZIP)"),env,&error),"make fixture ZIP");
        const auto originalZip=bytes(work+"/fixture.zip");
        VoucherGroupNumWidget widget(nullptr);
        VoucherGroupNumTestAccess::process(widget,work+"/fixture.zip");
        require(VoucherGroupNumTestAccess::successes(widget)==1,"widget writes repaired ZIP");
        const QString generated=QDir(work+"/output").entryList({"*.zip"},QDir::Files).value(0);
        require(!generated.isEmpty(),"output exists");
        require(extractAccountSetJson(work+"/output/"+generated,work+"/fixed.json",&error),"extract output JSON");
        const auto fixed=QJsonDocument::fromJson(bytes(work+"/fixed.json")).object();
        require(numberAt(fixed,0)=="10" && numberAt(fixed,1)=="10", "ZIP contains consistent new number");
        unchangedExceptNumbers(fixture,fixed);
        require(bytes(work+"/fixture.zip")==originalZip,"source ZIP unchanged");
        env.insert("GROUP_TEST_ZIP",work+"/output/"+generated); env.insert("GROUP_TEST_KEEP",work+"/keep-copy.bin");
        require(runPowerShell(QStringLiteral("$ErrorActionPreference='Stop';Add-Type -AssemblyName System.IO.Compression.FileSystem;$z=[IO.Compression.ZipFile]::OpenRead($env:GROUP_TEST_ZIP);try{[IO.Compression.ZipFileExtensions]::ExtractToFile($z.GetEntry('keep.bin'),$env:GROUP_TEST_KEEP)}finally{$z.Dispose()}"),env,&error),"extract unrelated member");
        require(bytes(work+"/keep-copy.bin")==marker,"unrelated ZIP member preserved");
        require(VoucherGroupNumTestAccess::log(widget).contains("[1.1]") && VoucherGroupNumTestAccess::log(widget).contains("[10]"),"visible original and new number notice");
        VoucherGroupNumTestAccess::process(widget,work+"/fixture.zip");
        require(QDir(work+"/output").entryList({"*.zip"},QDir::Files).size()==2,"repeat processing does not overwrite output");
        VoucherGroupNumTestAccess::process(widget,work+"/output/"+generated);
        require(VoucherGroupNumTestAccess::noChanges(widget)==1,"repaired ZIP detected as no-change");

        const QString source=qEnvironmentVariable("GROUPNUM_SAMPLE");
        if (!source.isEmpty()) {
            const auto sourceBytes=bytes(source);
            require(extractAccountSetJson(source,work+"/sample.json",&error),"read user sample");
            auto sample=QJsonDocument::fromJson(bytes(work+"/sample.json")).object(); const auto before=sample;
            const auto result=VoucherGroupNumRepair::repair(sample);
            require(result.error.isEmpty() && result.changedRows>0,"real sample normalized");
            unchangedExceptNumbers(before,sample);
            RepairPathSettings::setOutputDir(work+"/sample-output");
            VoucherGroupNumTestAccess::process(widget,source);
            require(VoucherGroupNumTestAccess::successes(widget)==3,"real sample ZIP pipeline");
            require(bytes(source)==sourceBytes,"real source ZIP unchanged");
            const auto output=QDir(work+"/sample-output").entryList({"*.zip"},QDir::Files);
            require(output.size()==1,"single real sample result");
            require(extractAccountSetJson(work+"/sample-output/"+output[0],work+"/sample-result.json",&error),"read sample result");
            require(QJsonDocument::fromJson(bytes(work+"/sample-result.json")).object()==sample,"sample pipeline matches verified data");
            save(work+"/sample-details.txt",result.details.join('\n').toUtf8());
            std::printf("SAMPLE rows=%d normalized=%d renumbered=%d\n",result.changedRows,result.normalizedVouchers,result.renumberedVouchers);
        }
        VoucherGroupNumTestAccess::finish(widget);
        widget.resize(1100,850); widget.show(); app.processEvents();
        require(widget.grab().save(work+"/page.png"),"save UI proof");
        std::puts("PASS: monthly numbering, entry grouping, exact decimals, errors, ZIP preservation, notices, no overwrite, idempotence, real sample.");
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
