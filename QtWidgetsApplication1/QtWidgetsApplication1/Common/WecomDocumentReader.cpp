#include "WecomDocumentReader.h"
#include "WecomBrowser.h"
#include <QPointer>
#include <QUrlQuery>
#include <QVariant>

namespace {
QString revision(const QJsonValue &value)
{ return value.isString() ? value.toString() : QString::number(value.toDouble(), 'f', 0); }
}
WecomDocumentReader::WecomDocumentReader(WecomBrowser *browser, QObject *parent)
    : QObject(parent), m_browser(browser)
{
    qRegisterMetaType<WecomDocument::Snapshot>();
    m_loginTimeout.setSingleShot(true);
    connect(&m_loginTimeout, &QTimer::timeout, this, [this]() { fail(QStringLiteral("登录或文档加载超时，请重新扫码。")); });
    connect(browser, &WecomBrowser::browserError, this, [this](const QString &message) { if (m_busy) fail(message); });
    connect(browser, &WecomBrowser::bootstrapReceived, this, [this](const QUrl &url, const QJsonObject &response) {
        if (!m_busy || !m_waitingBootstrap || QUrlQuery(url).queryItemValue("id") != WecomDocument::documentId()) return;
        WecomDocument::Metadata metadata; QString error;
        // The official page may first issue an unauthenticated request. Keep the login window open.
        if (!WecomDocument::parseMetadata(response, &metadata, &error)) return;
        m_openRequest = url; m_waitingBootstrap = false; m_loginTimeout.stop();
        emit authenticated();
        const int generation = m_generation; QPointer<WecomDocumentReader> guard(this);
        // Save a newly verified login before fetching sheets, so a later network failure
        // does not discard the successful scan. Save again at completion for rotated cookies.
        m_browser->saveSession([guard, generation, response](const QString &) {
            if (!guard || generation != guard->m_generation || !guard->m_busy) return;
            if (guard->m_readAfterLogin) guard->acceptMetadata(response);
            else { guard->setBusy(false); emit guard->progress(QStringLiteral("企业微信文档登录成功，可以获取最新数据。")); }
        });
    });
}
void WecomDocumentReader::setBusy(bool busy)
{
    if (m_busy == busy) return;
    m_busy = busy; setProperty("sessionBusyWecomRead", busy); emit busyChanged(busy);
}
void WecomDocumentReader::signIn(const QUrl &url) { begin(url, false, QDate::currentDate()); }
void WecomDocumentReader::read(const QUrl &url, const QDate &date) { begin(url, true, date); }
void WecomDocumentReader::begin(const QUrl &url, bool readAfterLogin, const QDate &date)
{
    if (m_busy) return;
    QString error;
    if (!WecomDocument::validateUrl(url, &error) || !date.isValid()) {
        emit failed(error.isEmpty() ? QStringLiteral("筛选日期无效。") : error); return;
    }
    const bool canRefresh = readAfterLogin && !m_openRequest.isEmpty() && url == m_documentUrl;
    ++m_generation; m_readAfterLogin = readAfterLogin; m_documentUrl = url;
    m_snapshot = {}; m_snapshot.fromDate = date; m_sheetIndex = 0;
    m_asyncPolling = false;
    setBusy(true);
    if (canRefresh) {
        emit progress(QStringLiteral("正在获取最新工作表目录…"));
        QUrl request = m_openRequest; QUrlQuery query(request);
        query.removeAllQueryItems("t"); query.addQueryItem("t", QString::number(QDateTime::currentMSecsSinceEpoch())); request.setQuery(query);
        const int generation = m_generation; QPointer<WecomDocumentReader> guard(this);
        m_browser->get(request, [guard, generation](const QJsonObject &response, const QString &error) {
            if (!guard || generation != guard->m_generation || !guard->m_busy) return;
            if (!error.isEmpty()) guard->fail(error); else guard->acceptMetadata(response);
        });
    } else {
        m_openRequest.clear(); m_waitingBootstrap = true;
        if (readAfterLogin && m_browser->hasSavedSession()) {
            emit progress(QStringLiteral("正在复用本地登录并验证文档权限…"));
            const int generation = m_generation;
            QTimer::singleShot(8000, this, [this, generation]() {
                if (generation != m_generation || !m_waitingBootstrap || !m_busy) return;
                emit progress(QStringLiteral("尚未完成登录验证，请在官方页面确认登录状态；失效时重新扫码。"));
                emit loginRequired();
            });
        } else {
            emit progress(QStringLiteral("请在官方页面扫码登录，登录后自动识别文档。"));
            emit loginRequired();
        }
        m_loginTimeout.start(180000); m_browser->navigate(url);
    }
}
void WecomDocumentReader::acceptMetadata(const QJsonObject &response)
{
    QString error;
    if (!WecomDocument::parseMetadata(response, &m_metadata, &error)) { m_openRequest.clear(); fail(error); return; }
    m_snapshot.title = m_metadata.title;
    m_asyncPolling = response.value("clientVars").toObject().value("cgicode").toInt() == 200156;
    m_snapshot.sheets = WecomDocument::selectSheets(m_metadata, m_snapshot.fromDate);
    if (m_snapshot.sheets.isEmpty()) { finish(); return; }
    readNextSheet();
}
void WecomDocumentReader::readNextSheet(int retry)
{
    if (!m_busy) return;
    if (m_sheetIndex >= m_snapshot.sheets.size()) { finish(); return; }
    auto &sheet = m_snapshot.sheets[m_sheetIndex];
    emit progress(QStringLiteral("正在读取 %1（%2/%3）…").arg(sheet.name).arg(m_sheetIndex + 1).arg(m_snapshot.sheets.size()));
    QUrl request("https://doc.weixin.qq.com/dop-api/get/sheet"); QUrlQuery query;
    query.addQueryItem("padId", m_metadata.padId); query.addQueryItem("subId", sheet.id);
    query.addQueryItem("startrow", "0"); query.addQueryItem("endrow", "-1");
    query.addQueryItem("outformat", "1"); query.addQueryItem("nowb", "1");
    query.addQueryItem("xsrf", QUrlQuery(m_openRequest).queryItemValue("xsrf"));
    query.addQueryItem("_r", QString::number(QDateTime::currentMSecsSinceEpoch()));
    query.addQueryItem("rev", m_metadata.revision); request.setQuery(query);
    if (m_asyncPolling) { query.addQueryItem("async", "1"); request.setQuery(query); }
    const int generation = m_generation; QPointer<WecomDocumentReader> guard(this);
    m_browser->get(request, [guard, generation, retry](const QJsonObject &response, const QString &networkError) {
        if (!guard || generation != guard->m_generation || !guard->m_busy) return;
        const auto data = response.value("data").toObject();
        const int code = response.value("retcode").toInt(-1);
        if (networkError.isEmpty() && code == 200156) guard->m_asyncPolling = true;
        const bool stillLoading = guard->m_asyncPolling && (code == 200156 || (code == 0 && data.value("async_open_done") != QJsonValue(true)));
        if (networkError.isEmpty() && stillLoading && retry < 30) {
            const int interval = qBound(500, data.value("async_poll_interval").toInt(1000), 5000);
            QTimer::singleShot(interval, guard, [guard, generation, retry]() {
                if (guard && generation == guard->m_generation) guard->readNextSheet(retry + 1);
            }); return;
        }
        auto &sheet = guard->m_snapshot.sheets[guard->m_sheetIndex];
        QString error = networkError;
        if (error.isEmpty() && stillLoading) error = QStringLiteral("服务器仍在加载工作表，请稍后重新获取。");
        if (error.isEmpty() && code != 0) error = QStringLiteral("工作表读取被拒绝，请重新扫码并确认文档访问权限。");
        if (error.isEmpty() && (!data.contains("rev") || revision(data.value("rev")) != guard->m_metadata.revision))
            error = QStringLiteral("文档版本发生变化或版本信息缺失，请重新获取，避免混合不同版本。" );
        if (error.isEmpty()) WecomDocument::parseSheet(response, &sheet, &error);
        sheet.error = error;
        guard->m_asyncPolling = false;
        ++guard->m_sheetIndex;
        guard->readNextSheet();
    });
}
void WecomDocumentReader::finish()
{
    m_snapshot.fetchedAt = QDateTime::currentDateTimeUtc();
    // Finish persistence before releasing the busy guard, including sign-in without a read.
    const int generation = m_generation; QPointer<WecomDocumentReader> guard(this);
    m_browser->saveSession([guard, generation](const QString &) {
        if (!guard || generation != guard->m_generation || !guard->m_busy) return;
        guard->setBusy(false); emit guard->finished(guard->m_snapshot);
    });
}
void WecomDocumentReader::fail(const QString &message)
{
    ++m_generation; m_loginTimeout.stop(); m_waitingBootstrap = false;
    m_browser->stop(); setBusy(false); emit failed(message);
}
void WecomDocumentReader::cancel()
{
    if (!m_busy) return;
    ++m_generation; m_loginTimeout.stop(); m_waitingBootstrap = false; m_browser->stop();
    if (!m_snapshot.sheets.isEmpty()) {
        m_snapshot.cancelled = true;
        for (auto &sheet : m_snapshot.sheets) if (!sheet.loaded && sheet.error.isEmpty()) sheet.error = QStringLiteral("已取消读取。");
        finish();
    } else { setBusy(false); emit failed(QStringLiteral("已取消。")); }
}
