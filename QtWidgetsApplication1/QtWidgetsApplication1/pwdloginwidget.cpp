#include "pwdloginwidget.h"
#include "ui_pwdloginwidget.h"
#include "httprequesthelper.h"
#include "lemoncofighelper.h"
#include "Common/globaldata.h"

#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpressionValidator>
#include <QHttpMultiPart>
#include <QNetworkCookie>

PwdLoginWidget::PwdLoginWidget(GlobalData *globalData, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PwdLoginWidget),
    m_pGlobalData(globalData)
{
    ui->setupUi(this);
    ui->labelPwdError->setVisible(false);
    ui->labelRight->setVisible(false);

    QRegularExpression regx("[0-9]+$");
    QValidator *validator = new QRegularExpressionValidator(regx, this);
    ui->editUserName->setValidator(validator);

    forgetPwdUrl = QLatin1String("https://www.ningmengyun.com/?rp=fpd");

    connect(this, &PwdLoginWidget::tologin, this, &PwdLoginWidget::on_btnPwdLogin_clicked);

    QString appPath = qApp->applicationDirPath();
    QString filePath = appPath + "/LemonApp.dat";
    QFile file(filePath);

    if (file.exists())
    {
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QByteArray bytes = file.readAll();
            QJsonDocument document = QJsonDocument::fromJson(bytes);
            if (document.isObject())
            {
                QJsonObject jsonObj = document.object();
                if (jsonObj.contains("re"))
                {
                    if (jsonObj["re"].toInt() == 1)
                    {
                        ui->checkBtnAutoPwd->setChecked(true);
                        ui->editPwd->setText(jsonObj["p"].toString());
                        ui->editUserName->setText(jsonObj["u"].toString());
                    }
                }
            }
            file.close();
        }
    }
}

PwdLoginWidget::~PwdLoginWidget()
{
    delete ui;
}

void PwdLoginWidget::on_btnForgetPwd_clicked()
{
    QDesktopServices::openUrl(QUrl(forgetPwdUrl));
}

void PwdLoginWidget::on_btnPwdLogin_clicked()
{
    ui->labelPwdError->setText("");
    ui->labelPwdError->setVisible(false);
    QString strPwd = ui->editPwd->text();
    QString strUserName = ui->editUserName->text();
    if (strPwd.isEmpty() || strUserName.isEmpty())
    {
        ui->labelPwdError->setText(QStringLiteral("密码不能为空"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    lemoncofighelper configHelper;
    QString strLoginUrl = configHelper.GetAccountUrlByKey("loginAshxUrl");
    if (strLoginUrl.isEmpty())
    {
        ui->labelPwdError->setText(QStringLiteral("请求地址不能为空"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    // 构造 multipart/form-data 请求体
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart userNamePart;
    userNamePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QVariant("form-data; name=\"userName\""));
    userNamePart.setBody(strUserName.toUtf8());
    multiPart->append(userNamePart);

    QHttpPart passWordPart;
    passWordPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QVariant("form-data; name=\"passWord\""));
    passWordPart.setBody(strPwd.toUtf8());
    multiPart->append(passWordPart);

    QHttpPart loginSourcePart;
    loginSourcePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                              QVariant("form-data; name=\"loginSource\""));
    loginSourcePart.setBody("1010");
    multiPart->append(loginSourcePart);

    QHttpPart wxUserNamePart;
    wxUserNamePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                             QVariant("form-data; name=\"wxUserName\""));
    wxUserNamePart.setBody("gh_1e17d92980b8");
    multiPart->append(wxUserNamePart);

    QHttpPart rfromPart;
    rfromPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant("form-data; name=\"rfrom\""));
    rfromPart.setBody("1010");
    multiPart->append(rfromPart);

    QNetworkRequest request;
    request.setUrl(QUrl(strLoginUrl.trimmed()));
    CHttpRequestHelper::configureSsl(request);
    request.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36");

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->post(request, multiPart);
    multiPart->setParent(reply);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onLoginAshxFinished(reply);
    });
}

void PwdLoginWidget::onLoginAshxFinished(QNetworkReply *reply)
{
    int reqCode = 0;
    QVariant variant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (variant.isValid())
        reqCode = variant.toInt();

    if (reqCode != 200)
    {
        reply->deleteLater();
        ui->labelPwdError->setText(QStringLiteral("登录失败，请检查用户名密码"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    // 解析响应体（JSONP 格式，外面包了一层括号）
    QByteArray body = reply->readAll();
    // 去除 JSONP 包裹的 ( )
    QString bodyStr = QString::fromUtf8(body).trimmed();
    if (bodyStr.startsWith('(') && bodyStr.endsWith(')'))
        bodyStr = bodyStr.mid(1, bodyStr.length() - 2);

    QJsonParseError jsonError;
    QJsonDocument document = QJsonDocument::fromJson(bodyStr.toUtf8(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject())
    {
        reply->deleteLater();
        ui->labelPwdError->setText(QStringLiteral("登录失败，响应格式错误"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    QJsonObject obj = document.object();
    if (obj["result"].toInt() != 1 || !obj["success"].toBool())
    {
        reply->deleteLater();
        ui->labelPwdError->setText(QStringLiteral("登录失败，请检查用户名密码"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    // 从 Set-Cookie 中提取 ningmengcookie（使用 QNetworkCookie API）
    QVariant cookieVar = reply->header(QNetworkRequest::SetCookieHeader);
    QList<QNetworkCookie> cookies = cookieVar.value<QList<QNetworkCookie>>();
    for (const QNetworkCookie &cookie : cookies)
    {
        if (cookie.name() == "ningmengcookie")
            m_pGlobalData->m_strNingMengCookie = QString::fromUtf8(cookie.value());
    }

    reply->deleteLater();

    if (m_pGlobalData->m_strNingMengCookie.isEmpty())
    {
        ui->labelPwdError->setText(QStringLiteral("登录失败，未获取到认证信息"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    // 同步设置 access_token，保持兼容
    m_pGlobalData->m_userToken.SetAccessToken(m_pGlobalData->m_strNingMengCookie);

    // 动态获取 serviceId 和 appAsId
    fetchServiceParams();
}

void PwdLoginWidget::fetchServiceParams()
{
    lemoncofighelper configHelper;
    QString wbgateUrl = configHelper.GetAccountUrlByKey("wbgateMenuUrl");
    if (wbgateUrl.isEmpty())
    {
        ui->labelPwdError->setText(QStringLiteral("wbgate地址配置为空"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(wbgateUrl));
    CHttpRequestHelper::configureSsl(request);

    // 不自动跟随重定向，手动解析 302 响应
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);

    // 携带 cookie
    QString cookie = "ningmengcookie=" + m_pGlobalData->m_strNingMengCookie;
    request.setRawHeader("Cookie", cookie.toUtf8());

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFetchServiceParamsFinished(reply);
    });
}

void PwdLoginWidget::onFetchServiceParamsFinished(QNetworkReply *reply)
{
    int reqCode = 0;
    QVariant variant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (variant.isValid())
        reqCode = variant.toInt();

    if (reqCode == 302 || reqCode == 301)
    {
        // 从 Location 头解析 serviceID 和 appasid
        QString location = QString::fromUtf8(reply->rawHeader("Location"));
        QUrl redirectUrl(location);
        QUrlQuery query(redirectUrl);

        QString serviceId = query.queryItemValue("serviceID");
        QString appAsId = query.queryItemValue("appasid");

        if (!serviceId.isEmpty())
            m_pGlobalData->m_nServiceId = serviceId.toInt();
        if (!appAsId.isEmpty())
            m_pGlobalData->m_strAppAsId = appAsId;

        // 也从 Set-Cookie 中提取 service-id（作为备用）
        QVariant ckVar = reply->header(QNetworkRequest::SetCookieHeader);
        QList<QNetworkCookie> ckList = ckVar.value<QList<QNetworkCookie>>();
        for (const QNetworkCookie &ck : ckList)
        {
            if (ck.name() == "service-id")
                m_pGlobalData->m_nServiceId = QString::fromUtf8(ck.value()).toInt();
        }

        reply->deleteLater();

        if (m_pGlobalData->m_nServiceId == 0 || m_pGlobalData->m_strAppAsId.isEmpty())
        {
            qDebug() << QStringLiteral("警告：serviceId 或 appAsId 未获取到");
            qDebug() << "serviceId:" << m_pGlobalData->m_nServiceId
                     << "appAsId:" << m_pGlobalData->m_strAppAsId;
        }

        m_pGlobalData->m_bLogin = true;
        saveCredentials();
        emit login_success();
    }
    else
    {
        reply->deleteLater();
        qDebug() << QStringLiteral("获取服务参数失败，HTTP状态码:") << reqCode;
        ui->labelPwdError->setText(QStringLiteral("获取服务信息失败"));
        ui->labelPwdError->setVisible(true);
    }
}

void PwdLoginWidget::saveCredentials()
{
    QString appPath = qApp->applicationDirPath();
    QString filePath = appPath + "/LemonApp.dat";
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QJsonObject jsonObj;
        jsonObj.insert("re", ui->checkBtnAutoPwd->checkState() == Qt::Checked ? 1 : 0);
        jsonObj.insert("u", ui->editUserName->text().trimmed());
        jsonObj.insert("p", ui->editPwd->text().trimmed());
        QJsonDocument document;
        document.setObject(jsonObj);
        file.write(document.toJson());
        file.close();
    }
}
