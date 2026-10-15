#include "subjectbalanceimport.h"
#include "../lemoncofighelper.h"
#include "../httprequesthelper.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSet>
#include <QUrlQuery>
#include <QUuid>

namespace {
QJsonValue responseField(const QJsonObject &object, const QString &name)
{
    if (object.contains(name)) return object.value(name);
    QString camelCase = name;
    if (!camelCase.isEmpty()) camelCase[0] = camelCase.at(0).toLower();
    return object.value(camelCase);
}

QString readable(const QJsonValue &value)
{
    if (value.isString()) {
        const QString text = value.toString();
        const QJsonDocument embedded = QJsonDocument::fromJson(text.toUtf8());
        return embedded.isNull() ? text : QString::fromUtf8(embedded.toJson(QJsonDocument::Indented));
    }
    if (value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson());
    if (value.isObject()) return QString::fromUtf8(QJsonDocument(value.toObject()).toJson());
    return {};
}
}

QList<BalanceImportSoftware> SubjectBalanceImport::softwareCatalog(QString *error)
{
    QFile file(":/Resource/import/subject-balance-software.json");
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法读取来源软件附件配置");
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    QList<BalanceImportSoftware> catalog;
    QSet<int> ids;
    if (doc.isArray()) for (const auto &entry : doc.array()) {
        const QJsonObject object = entry.toObject();
        BalanceImportSoftware software;
        software.id = object.value("id").toInt(-1);
        software.name = object.value("name").toString();
        software.supportsAuxiliaryFiles = object.value("supportsAuxiliaryFiles").toBool();
        software.notes = object.value("notes").toString();
        for (const auto &standard : object.value("accountingStandards").toArray())
            software.accountingStandards.append(standard.toInt());
        QSet<QString> keys;
        for (const auto &item : object.value("files").toArray()) {
            const auto field = item.toObject();
            BalanceImportFileSlot slot;
            slot.key = field.value("key").toString();
            slot.label = field.value("label").toString();
            slot.notes = field.value("notes").toString();
            slot.required = field.value("required").toBool();
            for (const auto &extension : field.value("extensions").toArray())
                slot.extensions.append(extension.toString().toLower().remove('.'));
            if (slot.key.isEmpty() || slot.label.isEmpty() || slot.extensions.isEmpty() || keys.contains(slot.key)) {
                if (error) *error = QStringLiteral("来源软件附件配置不完整或存在重复字段");
                return {};
            }
            keys.insert(slot.key);
            software.files.append(slot);
        }
        if (software.id < 0 || software.name.isEmpty() || software.files.isEmpty() || ids.contains(software.id)) {
            if (error) *error = QStringLiteral("来源软件配置无效");
            return {};
        }
        ids.insert(software.id);
        catalog.append(software);
    }
    if (catalog.isEmpty() && error) *error = QStringLiteral("来源软件配置为空或格式错误");
    return catalog;
}

QString SubjectBalanceImport::validate(const BalanceImportInput &input, const BalanceImportSession &session)
{
    if (session.token.trimmed().isEmpty()) return QStringLiteral("请先登录");
    if (session.serviceId <= 0) return QStringLiteral("请先选择有效的企业专业版套餐");
    QUrl url;
    QString reason;
    if (!lemoncofighelper::TryBuildUrl(session.host, "/api/ImportFromOther/ImportAsFromClientBatch", url, reason))
        return QStringLiteral("导账域名无效：") + reason;
    if (input.accountName.trimmed().isEmpty()) return QStringLiteral("请填写新账套名称");
    if (!input.startMonth.isValid()) return QStringLiteral("请选择有效的启用年月");
    if (!QSet<int>{1, 2, 3, 4, 5, 6, 7, 9}.contains(input.accountingStandard))
        return QStringLiteral("请选择会计准则");
    if (input.taxType != 1 && input.taxType != 2) return QStringLiteral("请选择纳税人类型");
    const auto catalog = softwareCatalog();
    for (const auto &software : catalog) if (software.id == input.software) {
        if (software.id == 22001 && QFileInfo(input.files.value("subject")).suffix().compare("txt", Qt::CaseInsensitive) == 0
            && input.files.value("trialbalance").trimmed().isEmpty())
            return QStringLiteral("亿企代账使用 TXT 科目时，必须同时选择余额/凭证组合工作簿");
        if (!software.accountingStandards.isEmpty() && !software.accountingStandards.contains(input.accountingStandard))
            return QStringLiteral("当前软件不支持所选会计准则，请重新选择");
        QSet<QString> keys;
        bool hasFile = false;
        for (const auto &slot : software.files) {
            keys.insert(slot.key);
            const QString path = input.files.value(slot.key).trimmed();
            if (path.isEmpty()) {
                if (slot.required) return QStringLiteral("请选择%1").arg(slot.label);
                continue;
            }
            QFile file(path);
            const QFileInfo info(file);
            if (!info.isFile() || !file.open(QIODevice::ReadOnly) || file.size() <= 0)
                return QStringLiteral("%1不存在、为空或无法读取").arg(slot.label);
            if (!slot.extensions.contains(info.suffix().toLower()))
                return QStringLiteral("%1支持的格式：%2").arg(slot.label, slot.extensions.join(" / "));
            hasFile = true;
        }
        for (auto it = input.files.constBegin(); it != input.files.constEnd(); ++it) if (!keys.contains(it.key())) {
            if (!software.supportsAuxiliaryFiles || it.key().trimmed().isEmpty()
                || it.key().contains('"') || it.key().contains('\r') || it.key().contains('\n')
                || QSet<QString>{"trialbalance", "voucher", "subject"}.contains(it.key()))
                return QStringLiteral("辅助附件类别与当前软件不匹配，请重新选择文件");
            QFile extra(it.value());
            const QFileInfo info(extra);
            if (!info.isFile() || !extra.open(QIODevice::ReadOnly) || extra.size() <= 0
                || !QStringList{"xls", "xlsx"}.contains(info.suffix().toLower()))
                return QStringLiteral("辅助核算附件必须是可读取的非空 Excel 文件");
            hasFile = true;
        }
        return hasFile ? QString() : QStringLiteral("请至少选择一份原始表格");
    }
    return QStringLiteral("请选择受支持的来源软件");
}

QUrl SubjectBalanceImport::submitUrl(const BalanceImportInput &input, const QString &host, int serviceId)
{
    QUrl url;
    QString reason;
    if (!lemoncofighelper::TryBuildUrl(host, "/api/ImportFromOther/ImportAsFromClientBatch", url, reason)) return {};
    QUrlQuery query;
    query.addQueryItem("software", QString::number(input.software));
    query.addQueryItem("asName", input.accountName.trimmed());
    query.addQueryItem("asStartPeriod", QDate(input.startMonth.year(), input.startMonth.month(), 1).toString(Qt::ISODate));
    query.addQueryItem("accountingStandard", QString::number(input.accountingStandard));
    query.addQueryItem("taxType", QString::number(input.taxType));
    query.addQueryItem("isZeroImport", input.importZero ? "1" : "2");
    query.addQueryItem("accountSetVersion", "2");
    query.addQueryItem("importSystem", "1");
    query.addQueryItem("serviceId", QString::number(serviceId));
    // QUrlQuery keeps '+' literal; escape it for ASP.NET query-string decoding.
    url.setQuery(query.query(QUrl::FullyEncoded).replace("+", "%2B"));
    return url;
}

BalanceImportOutcome SubjectBalanceImport::parseOutcome(const QByteArray &body, const QString &expectedId)
{
    BalanceImportOutcome result;
    const auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) { result.error = QStringLiteral("结果响应不是有效 JSON 对象"); return result; }
    const auto root = doc.object();
    if (responseField(root, "State").toInt(-1) != 1000) {
        result.error = readable(responseField(root, "Msg"));
        if (result.error.isEmpty()) result.error = QStringLiteral("结果查询失败或响应状态无效");
        return result;
    }
    const auto wrapper = responseField(root, "Data").toObject();
    if (responseField(wrapper, "Code").toInt(-1) != 0) {
        result.error = readable(responseField(wrapper, "Message"));
        if (result.error.isEmpty()) result.error = QStringLiteral("结果查询业务状态无效");
        return result;
    }
    if (responseField(wrapper, "Data").isNull()) { result.status = 0; return result; }
    const auto detail = responseField(wrapper, "Data").toObject();
    if (responseField(detail, "Id").toString().compare(expectedId, Qt::CaseInsensitive) != 0) {
        result.error = QStringLiteral("查询返回的任务编号不匹配"); return result;
    }
    const auto status = responseField(detail, "Status");
    if (!status.isDouble() || status.toDouble() != status.toInt(-1) || status.toInt(-1) < 0 || status.toInt(-1) > 3) {
        result.error = QStringLiteral("服务端返回未知导账状态，请保留任务编号后重新查询"); return result;
    }
    result.status = status.toInt();
    result.error = readable(responseField(detail, "Error_Msg"));
    result.warnings = readable(responseField(detail, "Reminder_Msg"));
    const QString summary = readable(detail.value("newPrimarySubjectSummary"));
    if (!summary.isEmpty()) result.warnings += "\n" + summary;
    const auto start = QDate::fromString(responseField(detail, "AsStartDate").toString().left(10), Qt::ISODate);
    if (start.isValid() && start.year() > 1) result.startMonth = start.toString("yyyy-MM");
    result.accountId = responseField(detail, "AppAsId").toString();
    return result;
}

SubjectBalanceImportClient::SubjectBalanceImportClient(QObject *parent) : QObject(parent)
{
    m_pollTimer.setSingleShot(true);
    m_pollTimer.setInterval(10000);
    connect(&m_pollTimer, &QTimer::timeout, this, &SubjectBalanceImportClient::poll);
}

SubjectBalanceImportClient::~SubjectBalanceImportClient()
{
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); }
}

bool SubjectBalanceImportClient::busy() const { return m_busy; }
void SubjectBalanceImportClient::setBusy(bool busy) { m_busy = busy; emit busyChanged(busy); }

QNetworkRequest SubjectBalanceImportClient::request(const QUrl &url) const
{
    QNetworkRequest req(url);
    CHttpRequestHelper::setInternalSessionHeader(req);
    req.setRawHeader("Authorization", "Bearer " + m_session.token.toUtf8());
    req.setRawHeader("Cookie", lemoncofighelper::BuildJproCookie(m_session.token, m_session.serviceId).toUtf8());
    req.setRawHeader("Accept", "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    return req;
}

void SubjectBalanceImportClient::submit(const BalanceImportInput &input, const BalanceImportSession &session)
{
    if (m_busy) return;
    const QString error = SubjectBalanceImport::validate(input, session);
    if (!error.isEmpty()) { emit failed(error, false); return; }
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    for (auto it = input.files.constBegin(); it != input.files.constEnd(); ++it) {
        if (it.value().trimmed().isEmpty()) continue;
        auto *file = new QFile(it.value(), multipart);
        if (!file->open(QIODevice::ReadOnly)) {
            delete multipart;
            emit failed(QStringLiteral("附件已无法读取，请重新选择"), false);
            return;
        }
        QHttpPart part;
        QString filename = QFileInfo(*file).fileName();
        filename.replace('"', '_').replace('\r', '_').replace('\n', '_');
        part.setRawHeader("Content-Disposition",
            QStringLiteral("form-data; name=\"%1\"; filename=\"%2\"; filename*=UTF-8''%3")
                .arg(it.key(), filename, QString::fromLatin1(QUrl::toPercentEncoding(filename))).toUtf8());
        part.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
        part.setBodyDevice(file);
        multipart->append(part);
    }
    m_session = session;
    m_importId.clear();
    m_submitting = true;
    setBusy(true);
    auto *reply = m_network.post(request(SubjectBalanceImport::submitUrl(input, session.host, session.serviceId)), multipart);
    multipart->setParent(reply);
    watch(reply, true);
    emit progress(QStringLiteral("正在上传并解析原始表格，请勿重复提交…"));
}

void SubjectBalanceImportClient::query(const QString &importId, const BalanceImportSession &session)
{
    if (m_busy) return;
    if (QUuid(importId).isNull() || session.token.isEmpty() || session.serviceId <= 0) {
        emit failed(QStringLiteral("任务编号、登录信息或套餐无效"), false); return;
    }
    m_session = session;
    m_importId = importId;
    m_submitting = false;
    setBusy(true);
    poll();
}

void SubjectBalanceImportClient::poll()
{
    if (m_tokenProvider) m_session.token = m_tokenProvider();
    if (m_session.token.isEmpty()) {
        finishError(QStringLiteral("登录授权为空，请刷新授权后重新查询"));
        return;
    }
    QUrl url;
    QString reason;
    if (!lemoncofighelper::TryBuildUrl(m_session.host, "/api/ImportFromYun/GetImportInfoById", url, reason)) {
        finishError(QStringLiteral("查询域名无效：") + reason); return;
    }
    QUrlQuery query;
    query.addQueryItem("importInfoId", m_importId);
    url.setQuery(query);
    watch(m_network.get(request(url)), false);
}

void SubjectBalanceImportClient::stopQuery()
{
    if (m_submitting || !m_busy) return;
    m_pollTimer.stop();
    if (m_reply) {
        auto *reply = m_reply.data();
        m_reply.clear();
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    m_session.token.clear();
    setBusy(false);
    emit progress(QStringLiteral("已停止查询；服务端导账任务继续执行，可从历史记录继续查询。"));
}

void SubjectBalanceImportClient::finishError(const QString &message, bool uncertain)
{
    QString safeMessage = message;
    if (!m_session.token.isEmpty()) safeMessage.replace(m_session.token, "[已隐藏登录凭据]");
    m_pollTimer.stop();
    m_submitting = false;
    m_session.token.clear();
    setBusy(false);
    emit failed(safeMessage, uncertain);
}

void SubjectBalanceImportClient::watch(QNetworkReply *reply, bool submission)
{
    m_reply = reply;
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply]() { reply->setProperty("timedOut", true); reply->abort(); });
    timeout->start(submission ? 600000 : 60000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, timeout, submission]() {
        timeout->stop();
        if (m_reply != reply) { reply->deleteLater(); return; }
        m_reply.clear();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto body = reply->readAll();
        const bool networkError = reply->error() != QNetworkReply::NoError;
        const bool timedOut = reply->property("timedOut").toBool();
        reply->deleteLater();
        if (networkError || http != 200) {
            const QString error = (http == 401 || http == 403) ? QStringLiteral("认证失败，请重新登录或检查当前套餐权限")
                : timedOut ? QStringLiteral("请求超时") : QStringLiteral("网络请求失败（HTTP %1）").arg(http);
            finishError(error, submission && http != 401 && http != 403);
            return;
        }
        if (submission) {
            const auto doc = QJsonDocument::fromJson(body);
            const auto root = doc.object();
            if (!doc.isObject() || !responseField(root, "Code").isDouble()) {
                finishError(QStringLiteral("提交响应无法识别"), true); return;
            }
            if (responseField(root, "Code").toInt(-1) != 0) {
                const QString message = responseField(root, "Message").toString();
                finishError(message.isEmpty() ? QStringLiteral("服务端拒绝导账请求") : message); return;
            }
            m_importId = responseField(root, "Data").toString();
            if (QUuid(m_importId).isNull()) { finishError(QStringLiteral("提交响应缺少有效任务编号"), true); return; }
            m_submitting = false;
            emit accepted(m_importId);
            emit progress(QStringLiteral("任务已受理，正在查询导账结果…"));
            poll();
        } else {
            const auto result = SubjectBalanceImport::parseOutcome(body, m_importId);
            if (result.status < 0) { finishError(result.error); return; }
            if (result.status == 0) {
                emit progress(QStringLiteral("正在导账，10 秒后继续查询…"));
                m_pollTimer.start();
            } else {
                m_session.token.clear();
                setBusy(false);
                emit outcome(result.status, result.error, result.warnings, result.startMonth, result.accountId);
            }
        }
    });
}
