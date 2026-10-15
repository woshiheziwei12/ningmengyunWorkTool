#include "WecomBrowser.h"
#include "WecomSessionStore.h"
#include "../ThirdParty/WebView2/include/WebView2.h"
#include <wrl.h>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QSet>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QResizeEvent>
#include <QShowEvent>
#include <QHideEvent>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace {
constexpr int MaxResponseBytes = 32 * 1024 * 1024;
bool isDocumentOrigin(const QUrl &url)
{ return url.scheme() == "https" && url.host() == "doc.weixin.qq.com" && url.port(443) == 443; }
QString hrError(HRESULT hr)
{ return QStringLiteral("无法启动企业微信登录窗口（0x%1）。请检查 Microsoft Edge WebView2 Runtime 是否已安装。").arg(quint32(hr), 8, 16, QChar('0')); }
QString cookieKey(const QJsonObject &cookie)
{ return cookie.value("domain").toString() + '\n' + cookie.value("path").toString() + '\n' + cookie.value("name").toString(); }
bool cookieJson(ICoreWebView2Cookie *cookie, QJsonObject *object)
{
    for (const auto &field : {"Name", "Value", "Domain", "Path"}) {
        LPWSTR text = nullptr; HRESULT hr = E_FAIL;
        const QByteArray name(field);
        if (name == "Name") hr = cookie->get_Name(&text);
        else if (name == "Value") hr = cookie->get_Value(&text);
        else if (name == "Domain") hr = cookie->get_Domain(&text);
        else hr = cookie->get_Path(&text);
        const QString value = QString::fromWCharArray(text ? text : L""); CoTaskMemFree(text);
        if (FAILED(hr)) return false;
        object->insert(QString::fromLatin1(name).toLower(), value);
    }
    double expires = -1; BOOL secure = FALSE, httpOnly = FALSE, session = FALSE;
    COREWEBVIEW2_COOKIE_SAME_SITE_KIND sameSite{};
    if (FAILED(cookie->get_Expires(&expires)) || FAILED(cookie->get_IsSecure(&secure))
        || FAILED(cookie->get_IsHttpOnly(&httpOnly)) || FAILED(cookie->get_IsSession(&session))
        || FAILED(cookie->get_SameSite(&sameSite))) return false;
    object->insert("expires", session ? -1.0 : expires);
    object->insert("secure", secure != FALSE); object->insert("httpOnly", httpOnly != FALSE);
    object->insert("sameSite", int(sameSite));
    return true;
}
using CookiesReply = std::function<void(bool, const QJsonArray &)>;
void readCookies(ICoreWebView2CookieManager *manager, CookiesReply reply)
{
    const HRESULT hr = manager->GetCookies(nullptr, Callback<ICoreWebView2GetCookiesCompletedHandler>(
        [reply](HRESULT hr, ICoreWebView2CookieList *list) -> HRESULT {
            UINT count = 0; QJsonArray cookies;
            if (FAILED(hr) || !list || FAILED(list->get_Count(&count)) || count > 256) { reply(false, {}); return S_OK; }
            for (UINT i = 0; i < count; ++i) {
                ComPtr<ICoreWebView2Cookie> cookie; QJsonObject object;
                if (FAILED(list->GetValueAtIndex(i, &cookie)) || !cookie || !cookieJson(cookie.Get(), &object)) { reply(false, {}); return S_OK; }
                cookies.append(object);
            }
            reply(true, WecomSessionStore::usableCookies(cookies, QDateTime::currentMSecsSinceEpoch() / 1000.0));
            return S_OK;
        }).Get());
    if (FAILED(hr)) reply(false, {});
}
}

struct WecomBrowser::Impl {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    ComPtr<ICoreWebView2CookieManager> cookies;
    HMODULE loader = nullptr;
    bool comInitialized = false;
    bool initializing = false;
    bool sessionReady = false;
    QUrl pendingUrl;
    QString channel = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString profileDirectory;
    int nextRequest = 0;
    QMap<int, Reply> replies;
};

WecomBrowser::WecomBrowser(QWidget *parent, const QString &profileDirectory) : QWidget(parent), d(new Impl)
{
    d->profileDirectory = profileDirectory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/LemonCloud/LemonWorkTool/WecomWebView"
        : profileDirectory;
    setAttribute(Qt::WA_NativeWindow);
    setMinimumSize(480, 420);
}

WecomBrowser::~WecomBrowser()
{
    d->replies.clear();
    if (d->controller) d->controller->Close();
    d->cookies.Reset();
    d->web.Reset();
    d->controller.Reset();
    // Keep the tiny loader loaded: asynchronous environment creation may still finish.
    if (d->comInitialized) CoUninitialize();
}

void WecomBrowser::navigate(const QUrl &url)
{
    if (!isDocumentOrigin(url)) { emit browserError(QStringLiteral("仅允许打开企业微信文档网站。")); return; }
    d->pendingUrl = url;
    cancelRequests();
    if (!d->web) { initialize(); return; }
    if (!d->sessionReady) return;
    const auto address = url.toString(QUrl::FullyEncoded).toStdWString();
    const HRESULT hr = d->web->Navigate(address.c_str());
    if (FAILED(hr)) emit browserError(hrError(hr));
}

void WecomBrowser::initialize()
{
    if (d->initializing || d->web) return;
    if (!d->comInitialized) {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) { emit browserError(hrError(hr)); return; }
        d->comInitialized = true;
    }
    if (!d->loader) {
        const auto path = QDir(QCoreApplication::applicationDirPath()).filePath("WebView2Loader.dll").toStdWString();
        d->loader = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    using CreateEnvironment = HRESULT (STDAPICALLTYPE *)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);
    auto create = d->loader ? reinterpret_cast<CreateEnvironment>(GetProcAddress(d->loader, "CreateCoreWebView2EnvironmentWithOptions")) : nullptr;
    if (!create) { emit browserError(QStringLiteral("缺少 WebView2Loader.dll，请使用完整程序目录运行。")); return; }
    const QString profile = d->profileDirectory;
    if (!QDir().mkpath(profile)) { emit browserError(QStringLiteral("无法创建企业微信登录配置目录。")); return; }
    d->initializing = true;
    QPointer<WecomBrowser> guard(this);
    const auto folder = QDir::toNativeSeparators(profile).toStdWString();
    HRESULT hr = create(nullptr, folder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [guard](HRESULT result, ICoreWebView2Environment *environment) -> HRESULT {
            if (!guard) return S_OK;
            if (FAILED(result) || !environment) {
                guard->d->initializing = false;
                emit guard->browserError(hrError(result)); return S_OK;
            }
            const HRESULT controllerResult = environment->CreateCoreWebView2Controller(reinterpret_cast<HWND>(guard->winId()),
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [guard](HRESULT result, ICoreWebView2Controller *controller) -> HRESULT {
                    if (!guard) { if (controller) controller->Close(); return S_OK; }
                    auto &state = *guard->d;
                    state.initializing = false;
                    if (FAILED(result) || !controller) { emit guard->browserError(hrError(result)); return S_OK; }
                    state.controller = controller;
                    controller->get_CoreWebView2(&state.web);
                    if (!state.web) { emit guard->browserError(QStringLiteral("无法初始化登录页面。")); return S_OK; }
                    ComPtr<ICoreWebView2Settings> settings;
                    state.web->get_Settings(&settings);
                    if (settings) {
                        settings->put_AreDefaultContextMenusEnabled(FALSE);
                        settings->put_AreDevToolsEnabled(FALSE);
                        settings->put_IsStatusBarEnabled(FALSE);
                    }
                    EventRegistrationToken token{};
                    state.web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
                        [guard](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                            if (!guard) return S_OK;
                            BOOL success = FALSE; args->get_IsSuccess(&success);
                            COREWEBVIEW2_WEB_ERROR_STATUS status{}; args->get_WebErrorStatus(&status);
                            if (!success && status == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED) return S_OK;
                            emit guard->navigationFinished(success != FALSE);
                            if (!success && guard) emit guard->browserError(QStringLiteral("企业微信网页加载失败，请检查网络后重新扫码。"));
                            return S_OK;
                        }).Get(), &token);
                    state.web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                        [guard](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                            args->put_Handled(TRUE);
                            if (!guard) return S_OK;
                            LPWSTR uri = nullptr; args->get_Uri(&uri);
                            QUrl url(QString::fromWCharArray(uri ? uri : L"")); CoTaskMemFree(uri);
                            // Official authentication links only. No shell/protocol execution.
                            if (url.scheme() == "https" && (url.host().endsWith(".weixin.qq.com") || url.host() == "open.work.weixin.qq.com")) {
                                auto address = url.toString(QUrl::FullyEncoded).toStdWString(); guard->d->web->Navigate(address.c_str());
                            }
                            return S_OK;
                        }).Get(), &token);
                    state.web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
                        [](ICoreWebView2 *, ICoreWebView2PermissionRequestedEventArgs *args) -> HRESULT {
                            args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK;
                        }).Get(), &token);
                    state.web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                        [guard](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                            if (!guard) return S_OK;
                            LPWSTR source = nullptr; args->get_Source(&source);
                            QUrl origin(QString::fromWCharArray(source ? source : L"")); CoTaskMemFree(source);
                            if (!isDocumentOrigin(origin)) return S_OK;
                            LPWSTR json = nullptr; args->get_WebMessageAsJson(&json);
                            QByteArray raw = QString::fromWCharArray(json ? json : L"").toUtf8(); CoTaskMemFree(json);
                            if (raw.size() > MaxResponseBytes * 2) return S_OK;
                            const QJsonObject object = QJsonDocument::fromJson(raw).object();
                            if (object.value("channel").toString() != guard->d->channel) return S_OK;
                            const int id = object.value("id").toInt();
                            auto it = guard->d->replies.find(id);
                            if (it == guard->d->replies.end()) return S_OK;
                            Reply callback = it.value(); guard->d->replies.erase(it);
                            if (!object.value("ok").toBool()) callback({}, QStringLiteral("文档读取失败，请检查网络或重新扫码登录。"));
                            else if (!object.value("body").isObject()) callback({}, QStringLiteral("文档接口返回了非预期数据，请重新登录。"));
                            else callback(object.value("body").toObject(), {});
                            return S_OK;
                        }).Get(), &token);
                    ComPtr<ICoreWebView2_2> web2;
                    if (FAILED(state.web.As(&web2))) { emit guard->browserError(QStringLiteral("WebView2 Runtime 版本过低，请更新后重试。")); return S_OK; }
                    web2->get_CookieManager(&state.cookies);
                    web2->add_WebResourceResponseReceived(Callback<ICoreWebView2WebResourceResponseReceivedEventHandler>(
                        [guard](ICoreWebView2 *, ICoreWebView2WebResourceResponseReceivedEventArgs *args) -> HRESULT {
                            if (!guard) return S_OK;
                            ComPtr<ICoreWebView2WebResourceRequest> request; args->get_Request(&request);
                            LPWSTR uri = nullptr; request->get_Uri(&uri);
                            QUrl url(QString::fromWCharArray(uri ? uri : L"")); CoTaskMemFree(uri);
                            if (!isDocumentOrigin(url) || url.path() != "/dop-api/opendoc") return S_OK;
                            ComPtr<ICoreWebView2WebResourceResponseView> response; args->get_Response(&response);
                            int status = 0; response->get_StatusCode(&status);
                            if (status != 200) return S_OK;
                            response->GetContent(Callback<ICoreWebView2WebResourceResponseViewGetContentCompletedHandler>(
                                [guard, url](HRESULT result, IStream *stream) -> HRESULT {
                                    if (!guard || FAILED(result) || !stream) return S_OK;
                                    QByteArray bytes; char buffer[16384]; ULONG count = 0;
                                    while (SUCCEEDED(stream->Read(buffer, sizeof(buffer), &count)) && count) {
                                        if (bytes.size() + int(count) > MaxResponseBytes) return S_OK;
                                        bytes.append(buffer, int(count));
                                    }
                                    const auto document = QJsonDocument::fromJson(bytes);
                                    if (document.isObject()) emit guard->bootstrapReceived(url, document.object());
                                    return S_OK;
                                }).Get());
                            return S_OK;
                        }).Get(), &token);
                    guard->syncBounds();
                    guard->restoreSession();
                    return S_OK;
                }).Get());
            if (FAILED(controllerResult) && guard) { guard->d->initializing = false; emit guard->browserError(hrError(controllerResult)); }
            return S_OK;
        }).Get());
    if (FAILED(hr)) { d->initializing = false; emit browserError(hrError(hr)); }
}

bool WecomBrowser::hasSavedSession() const
{ return QFileInfo::exists(QDir(d->profileDirectory).filePath("session.dat")); }

void WecomBrowser::restoreSession()
{
    QJsonArray saved; QString error;
    const bool loaded = WecomSessionStore::load(QDir(d->profileDirectory).filePath("session.dat"), &saved, &error);
    QPointer<WecomBrowser> guard(this);
    auto done = std::make_shared<bool>(false);
    auto finish = [guard, done](const QString &message) {
        if (!guard || *done) return;
        *done = true; guard->d->sessionReady = true;
        if (!message.isEmpty()) emit guard->sessionStatus(message);
        if (guard && !guard->d->pendingUrl.isEmpty()) guard->navigate(guard->d->pendingUrl);
    };
    if (!loaded || saved.isEmpty() || !d->cookies) { finish(error); return; }
    QTimer::singleShot(10000, this, [finish]() { finish(QStringLiteral("恢复本地登录超时，将使用官方页面验证登录。")); });
    readCookies(d->cookies.Get(), [guard, done, finish, saved](bool ok, const QJsonArray &existing) {
        if (!guard || *done) return;
        if (!ok) { finish(QStringLiteral("无法恢复本地登录，请在官方页面验证登录。")); return; }
        QSet<QString> keys;
        for (const auto &value : existing) keys.insert(cookieKey(value.toObject()));
        bool restored = true;
        for (const auto &value : saved) {
            const auto object = value.toObject();
            // Browser-managed cookies may be newer than our backup; only restore missing ones.
            if (keys.contains(cookieKey(object))) continue;
            ComPtr<ICoreWebView2Cookie> cookie;
            auto wide = [&object](const char *key) { return object.value(key).toString().toStdWString(); };
            HRESULT hr = guard->d->cookies->CreateCookie(wide("name").c_str(), wide("value").c_str(), wide("domain").c_str(), wide("path").c_str(), &cookie);
            if (SUCCEEDED(hr) && cookie) hr = cookie->put_Expires(object.value("expires").toDouble());
            if (SUCCEEDED(hr) && cookie) hr = cookie->put_IsSecure(object.value("secure").toBool());
            if (SUCCEEDED(hr) && cookie) hr = cookie->put_IsHttpOnly(object.value("httpOnly").toBool());
            if (SUCCEEDED(hr) && cookie) hr = cookie->put_SameSite(COREWEBVIEW2_COOKIE_SAME_SITE_KIND(object.value("sameSite").toInt()));
            if (SUCCEEDED(hr) && cookie) hr = guard->d->cookies->AddOrUpdateCookie(cookie.Get());
            if (FAILED(hr) || !cookie) restored = false;
        }
        finish(restored ? QStringLiteral("已恢复本地登录记录，正在向企业微信验证是否有效。")
            : QStringLiteral("部分登录记录无法恢复，请在官方页面验证登录。"));
    });
}

void WecomBrowser::saveSession(SessionReply reply)
{
    if (!d->cookies) { reply(QStringLiteral("无法保存本地登录记录，下次打开可能需要重新扫码。")); return; }
    QPointer<WecomBrowser> guard(this); auto done = std::make_shared<bool>(false);
    auto finish = [guard, done, reply](const QString &error) {
        if (!guard || *done) return;
        *done = true;
        emit guard->sessionStatus(error.isEmpty() ? QStringLiteral("登录已加密保存，重开后可复用；有效期以企业微信验证结果为准。") : error);
        if (guard) reply(error);
    };
    QTimer::singleShot(10000, this, [finish]() { finish(QStringLiteral("保存登录超时，下次打开可能需要重新扫码。")); });
    readCookies(d->cookies.Get(), [guard, done, finish](bool ok, const QJsonArray &cookies) {
        if (!guard || *done) return;
        QString error;
        if (!ok) error = QStringLiteral("无法保存本地登录记录，下次打开可能需要重新扫码。");
        else {
            WecomSessionStore::save(QDir(guard->d->profileDirectory).filePath("session.dat"), cookies, &error);
            if (error.isEmpty() && cookies.isEmpty()) error = QStringLiteral("未获取到可保存的登录记录，下次打开可能需要重新扫码。");
        }
        finish(error);
    });
}

void WecomBrowser::get(const QUrl &url, Reply reply)
{
    if (!d->web || !isDocumentOrigin(url) || (url.path() != "/dop-api/opendoc" && url.path() != "/dop-api/get/sheet")) {
        reply({}, QStringLiteral("请先扫码登录企业微信文档。")); return;
    }
    const int id = ++d->nextRequest;
    d->replies.insert(id, reply);
    const QJsonObject options{{"url", url.toString(QUrl::FullyEncoded)}, {"channel", d->channel}, {"id", id}};
    const QString script = QStringLiteral(R"JS((function(o) {
        const send = (ok, body) => window.chrome.webview.postMessage({channel:o.channel,id:o.id,ok:ok,body:body});
        if (location.origin !== 'https://doc.weixin.qq.com') { send(false,null); return; }
        const controller = new AbortController();
        const timer = setTimeout(() => controller.abort(), 30000);
        const token = document.cookie.split(';').map(v=>v.trim()).find(v=>v.startsWith('TOK='));
        if (token) { const url = new URL(o.url); url.searchParams.set('xsrf',token.slice(4)); o.url=url.href; }
        fetch(o.url, {credentials:'same-origin',cache:'no-store',signal:controller.signal})
        .then(async response => {
            if (!response.ok || new URL(response.url).origin !== location.origin) throw new Error('response');
            const reader = response.body.getReader(); const chunks = []; let size = 0;
            while (true) {
                const part = await reader.read(); if (part.done) break;
                size += part.value.length; if (size > 33554432) { controller.abort(); throw new Error('limit'); }
                chunks.push(part.value);
            }
            const data = new Uint8Array(size); let at=0;
            for (const chunk of chunks) {data.set(chunk,at); at+=chunk.length;}
            send(true, JSON.parse(new TextDecoder('utf-8').decode(data)));
        }).catch(() => send(false,null)).finally(() => clearTimeout(timer));
    })(%1);)JS").arg(QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact)));
    QPointer<WecomBrowser> guard(this);
    const HRESULT hr = d->web->ExecuteScript(script.toStdWString().c_str(), nullptr);
    if (FAILED(hr)) { d->replies.remove(id); reply({}, QStringLiteral("无法执行文档读取，请重新扫码。")); return; }
    QTimer::singleShot(35000, this, [guard, id]() {
        if (!guard) return;
        auto it = guard->d->replies.find(id);
        if (it == guard->d->replies.end()) return;
        auto reply = it.value(); guard->d->replies.erase(it);
        reply({}, QStringLiteral("文档读取超时，请重试。"));
    });
}

void WecomBrowser::cancelRequests() { d->replies.clear(); }
void WecomBrowser::stop() { cancelRequests(); d->pendingUrl.clear(); if (d->web) d->web->Stop(); }
void WecomBrowser::syncBounds()
{
    if (!d->controller) return;
    RECT bounds{}; GetClientRect(reinterpret_cast<HWND>(winId()), &bounds);
    d->controller->put_Bounds(bounds);
    d->controller->put_IsVisible(isVisible() ? TRUE : FALSE);
    d->controller->NotifyParentWindowPositionChanged();
}
void WecomBrowser::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); syncBounds(); }
void WecomBrowser::showEvent(QShowEvent *event) { QWidget::showEvent(event); syncBounds(); }
void WecomBrowser::hideEvent(QHideEvent *event) { QWidget::hideEvent(event); syncBounds(); }
