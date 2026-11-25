#include "access2003converter.h"

#include "logger.h"
#include "Jet3Recovery.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cwctype>
#include <iterator>
#include <set>
#include <thread>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <oleauto.h>
#endif

namespace
{
const int kAccess2002FileFormat = 10; // Access 2002-2003 (Jet 4.0)
const char kDefaultAccessPassword[] = "ypbwkfyjhyhgzj";

bool isSupportedSuffix(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("ais")
        || suffix == QStringLiteral("aiy")
        || suffix == QStringLiteral("mdb");
}

bool isJet4Database(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray header = file.read(24);
    return header.size() >= 21
        && header.mid(4, 15) == QByteArray("Standard Jet DB", 15)
        && static_cast<unsigned char>(header.at(20)) == 1;
}

bool isJet3Database(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto header=file.read(24);
    return header.size()==24 && header.mid(4,15)=="Standard Jet DB" && header[20]==0;
}

QString normalizedPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();
}

#ifdef Q_OS_WIN
QString windowsErrorMessage(HRESULT result)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(result),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr);
    QString message;
    if (length > 0 && buffer)
        message = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
    if (buffer)
        LocalFree(buffer);
    if (message.isEmpty())
        message = QStringLiteral("HRESULT 0x%1").arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
    return message;
}

QString takeExceptionDescription(EXCEPINFO *exceptionInfo)
{
    if (!exceptionInfo)
        return QString();
    if (exceptionInfo->pfnDeferredFillIn)
        exceptionInfo->pfnDeferredFillIn(exceptionInfo);

    QStringList parts;
    if (exceptionInfo->bstrSource)
        parts.append(QString::fromWCharArray(exceptionInfo->bstrSource));
    if (exceptionInfo->bstrDescription)
        parts.append(QString::fromWCharArray(exceptionInfo->bstrDescription));

    if (exceptionInfo->bstrSource)
        SysFreeString(exceptionInfo->bstrSource);
    if (exceptionInfo->bstrDescription)
        SysFreeString(exceptionInfo->bstrDescription);
    if (exceptionInfo->bstrHelpFile)
        SysFreeString(exceptionInfo->bstrHelpFile);
    return parts.join(QStringLiteral("："));
}

HRESULT invokeDispatch(IDispatch *dispatch,
                       const wchar_t *memberName,
                       WORD flags,
                       VARIANTARG *arguments,
                       UINT argumentCount,
                       VARIANT *result,
                       QString *errorMessage)
{
    if (!dispatch)
        return E_POINTER;

    LPOLESTR name = const_cast<LPOLESTR>(memberName);
    DISPID dispatchId = DISPID_UNKNOWN;
    HRESULT hr = dispatch->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispatchId);
    if (FAILED(hr))
    {
        if (errorMessage)
            *errorMessage = QString::fromWCharArray(memberName) + QStringLiteral("：") + windowsErrorMessage(hr);
        return hr;
    }

    DISPPARAMS parameters = {};
    parameters.rgvarg = arguments;
    parameters.cArgs = argumentCount;
    DISPID propertyPutId = DISPID_PROPERTYPUT;
    if (flags & DISPATCH_PROPERTYPUT)
    {
        parameters.rgdispidNamedArgs = &propertyPutId;
        parameters.cNamedArgs = 1;
    }

    EXCEPINFO exceptionInfo = {};
    UINT argumentError = 0;
    hr = dispatch->Invoke(dispatchId, IID_NULL, LOCALE_USER_DEFAULT, flags,
                          &parameters, result, &exceptionInfo, &argumentError);
    if (FAILED(hr) && errorMessage)
    {
        const QString exceptionMessage = takeExceptionDescription(&exceptionInfo);
        *errorMessage = QString::fromWCharArray(memberName) + QStringLiteral("：")
            + (exceptionMessage.isEmpty() ? windowsErrorMessage(hr) : exceptionMessage);
    }
    else
    {
        takeExceptionDescription(&exceptionInfo);
    }
    return hr;
}

class ScopedComInitialization
{
public:
    ScopedComInitialization()
        : m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
          m_shouldUninitialize(SUCCEEDED(m_result))
    {
    }

    ~ScopedComInitialization()
    {
        if (m_shouldUninitialize)
            CoUninitialize();
    }

    HRESULT result() const { return m_result; }

private:
    HRESULT m_result;
    bool m_shouldUninitialize;
};

std::wstring lowerText(std::wstring text)
{
    for (wchar_t &character : text)
        character = static_cast<wchar_t>(towlower(character));
    return text;
}

struct PasswordChildSearch
{
    HWND passwordEdit = nullptr;
    HWND fallbackEdit = nullptr;
    HWND defaultButton = nullptr;
    std::wstring visibleText;
};

BOOL CALLBACK findPasswordChildren(HWND window, LPARAM parameter)
{
    PasswordChildSearch *search = reinterpret_cast<PasswordChildSearch*>(parameter);
    if (!search)
        return TRUE;

    wchar_t className[128] = {};
    wchar_t caption[512] = {};
    GetClassNameW(window, className, static_cast<int>(std::size(className)));
    GetWindowTextW(window, caption, static_cast<int>(std::size(caption)));
    if (caption[0])
    {
        search->visibleText.append(L" ");
        search->visibleText.append(caption);
    }

    const std::wstring classText = lowerText(className);
    if ((classText.find(L"edit") != std::wstring::npos
         || classText.find(L"richedit") != std::wstring::npos)
        && IsWindowEnabled(window))
    {
        search->fallbackEdit = window;
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        if ((style & ES_PASSWORD) != 0)
            search->passwordEdit = window;
    }
    else if (classText == L"button" && IsWindowEnabled(window))
    {
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        const std::wstring buttonText = lowerText(caption);
        if ((style & BS_DEFPUSHBUTTON) == BS_DEFPUSHBUTTON
            || buttonText == L"ok"
            || buttonText.find(L"确定") != std::wstring::npos)
        {
            search->defaultButton = window;
        }
    }
    return TRUE;
}

struct PasswordWindowSearch
{
    std::set<DWORD> candidateProcessIds;
    HWND dialog = nullptr;
    HWND passwordEdit = nullptr;
    HWND defaultButton = nullptr;
};

BOOL CALLBACK findPasswordWindow(HWND window, LPARAM parameter)
{
    PasswordWindowSearch *search = reinterpret_cast<PasswordWindowSearch*>(parameter);
    if (!search || search->dialog || !IsWindowEnabled(window))
        return TRUE;

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (search->candidateProcessIds.find(processId) == search->candidateProcessIds.end())
        return TRUE;

    PasswordChildSearch children;
    wchar_t caption[512] = {};
    GetWindowTextW(window, caption, static_cast<int>(std::size(caption)));
    children.visibleText = caption;
    EnumChildWindows(window, findPasswordChildren, reinterpret_cast<LPARAM>(&children));

    const std::wstring dialogText = lowerText(children.visibleText);
    const bool mentionsPassword = dialogText.find(L"password") != std::wstring::npos
        || dialogText.find(L"密码") != std::wstring::npos;
    HWND edit = children.passwordEdit;
    if (!edit && mentionsPassword)
        edit = children.fallbackEdit;
    if (!edit)
        return TRUE;

    search->dialog = window;
    search->passwordEdit = edit;
    search->defaultButton = children.defaultButton;
    return FALSE;
}

void clickDialogButton(HWND dialog, HWND button, int fallbackCommand)
{
    DWORD_PTR ignored = 0;
    if (button && IsWindow(button))
    {
        SendMessageTimeoutW(button, BM_CLICK, 0, 0,
                            SMTO_ABORTIFHUNG, 2000, &ignored);
        return;
    }
    PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(fallbackCommand, BN_CLICKED), 0);
}

class AccessPasswordDialogAutomation
{
public:
    explicit AccessPasswordDialogAutomation(const QString &password)
        : m_password(password.toStdWString()),
          m_stopRequested(false),
          m_targetProcessId(0),
          m_passwordRejected(false)
    {
    }

    ~AccessPasswordDialogAutomation()
    {
        stop();
        if (!m_password.empty())
            SecureZeroMemory(&m_password[0], m_password.size() * sizeof(wchar_t));
    }

    void start()
    {
        if (m_password.empty() || !m_targetProcessId.load() || m_thread.joinable())
            return;
        m_thread = std::thread([this]() { run(); });
    }

    void setTargetProcessId(DWORD processId)
    {
        m_targetProcessId.store(processId);
    }

    void stop()
    {
        m_stopRequested.store(true);
        if (m_thread.joinable())
            m_thread.join();
    }

    bool passwordRejected() const { return m_passwordRejected.load(); }

private:
    void run()
    {
        using Clock = std::chrono::steady_clock;
        // This object belongs to one file conversion. Access recreates its
        // password dialog after rejection, so HWND lifetime cannot bound retries.
        HWND submittedDialog = nullptr;
        Clock::time_point submittedAt;
        bool dialogDisappeared = false;
        while (!m_stopRequested.load())
        {
            PasswordWindowSearch search;
            search.candidateProcessIds.insert(m_targetProcessId.load());
            EnumWindows(findPasswordWindow, reinterpret_cast<LPARAM>(&search));

            if (search.dialog && !submittedDialog)
            {
                submittedDialog = search.dialog;
                submittedAt = Clock::now();
                DWORD_PTR ignored = 0;
                SendMessageTimeoutW(search.passwordEdit, WM_SETTEXT, 0,
                                    reinterpret_cast<LPARAM>(m_password.c_str()),
                                    SMTO_ABORTIFHUNG, 2000, &ignored);
                clickDialogButton(search.dialog, search.defaultButton, IDOK);
            }
            else if (search.dialog && (search.dialog != submittedDialog || dialogDisappeared
                     || Clock::now() - submittedAt > std::chrono::seconds(5)))
            {
                m_passwordRejected.store(true);
                // Cancel every replacement prompt, never submit the same password again.
                clickDialogButton(search.dialog, GetDlgItem(search.dialog, IDCANCEL), IDCANCEL);
            }
            if (submittedDialog && (!search.dialog || !IsWindow(submittedDialog)))
                dialogDisappeared = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        }
    }

    std::wstring m_password;
    std::atomic<bool> m_stopRequested;
    std::atomic<DWORD> m_targetProcessId;
    std::atomic<bool> m_passwordRejected;
    std::thread m_thread;
};

class AccessApplication
{
public:
    AccessApplication() : m_dispatch(nullptr) {}

    ~AccessApplication()
    {
        close();
    }

    bool open(QString *errorMessage)
    {
        CLSID classId;
        HRESULT hr = CLSIDFromProgID(L"Access.Application.11", &classId);
        if (FAILED(hr))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("未找到 Access 2003（Access.Application.11）注册信息：")
                    + windowsErrorMessage(hr);
            return false;
        }

        hr = CoCreateInstance(classId, nullptr, CLSCTX_LOCAL_SERVER,
                              IID_IDispatch, reinterpret_cast<void**>(&m_dispatch));
        if (FAILED(hr) || !m_dispatch)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("无法启动 Access 2003：") + windowsErrorMessage(hr)
                    + QStringLiteral("。请确认 Access 2003 已安装，且当前程序为 32 位版本。");
            return false;
        }

        VARIANTARG visible;
        VariantInit(&visible);
        visible.vt = VT_BOOL;
        visible.boolVal = VARIANT_FALSE;
        QString ignoredError;
        invokeDispatch(m_dispatch, L"Visible", DISPATCH_PROPERTYPUT,
                       &visible, 1, nullptr, &ignoredError);
        return true;
    }

    QString version() const
    {
        if (!m_dispatch)
            return QString();
        VARIANT result;
        VariantInit(&result);
        QString ignoredError;
        const HRESULT hr = invokeDispatch(m_dispatch, L"Version", DISPATCH_PROPERTYGET,
                                          nullptr, 0, &result, &ignoredError);
        QString value;
        if (SUCCEEDED(hr) && result.vt == VT_BSTR && result.bstrVal)
            value = QString::fromWCharArray(result.bstrVal);
        VariantClear(&result);
        return value;
    }

    DWORD processId() const
    {
        if (!m_dispatch)
            return 0;
        VARIANT result;
        VariantInit(&result);
        QString ignoredError;
        const HRESULT hr = invokeDispatch(m_dispatch, L"hWndAccessApp", DISPATCH_METHOD,
                                          nullptr, 0, &result, &ignoredError);
        HWND window = nullptr;
        if (SUCCEEDED(hr))
        {
            if (result.vt == VT_I4)
                window = reinterpret_cast<HWND>(static_cast<INT_PTR>(result.lVal));
            else if (result.vt == VT_I8)
                window = reinterpret_cast<HWND>(static_cast<INT_PTR>(result.llVal));
        }
        VariantClear(&result);
        DWORD processId = 0;
        if (window)
            GetWindowThreadProcessId(window, &processId);
        return processId;
    }

    bool convert(const QString &sourcePath,
                 const QString &destinationPath,
                 QString *errorMessage)
    {
        if (!m_dispatch)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("Access 2003 尚未启动");
            return false;
        }

        const std::wstring source = QDir::toNativeSeparators(sourcePath).toStdWString();
        const std::wstring destination = QDir::toNativeSeparators(destinationPath).toStdWString();
        VARIANTARG arguments[3];
        for (VARIANTARG &argument : arguments)
            VariantInit(&argument);

        // IDispatch 参数顺序与 VBA 方法签名相反。
        arguments[0].vt = VT_I4;
        arguments[0].lVal = kAccess2002FileFormat;
        arguments[1].vt = VT_BSTR;
        arguments[1].bstrVal = SysAllocString(destination.c_str());
        arguments[2].vt = VT_BSTR;
        arguments[2].bstrVal = SysAllocString(source.c_str());

        const HRESULT hr = invokeDispatch(m_dispatch, L"ConvertAccessProject",
                                          DISPATCH_METHOD, arguments, 3, nullptr, errorMessage);
        for (VARIANTARG &argument : arguments)
            VariantClear(&argument);
        return SUCCEEDED(hr);
    }

    void close()
    {
        if (!m_dispatch)
            return;
        QString ignoredError;
        invokeDispatch(m_dispatch, L"Quit", DISPATCH_METHOD,
                       nullptr, 0, nullptr, &ignoredError);
        m_dispatch->Release();
        m_dispatch = nullptr;
    }

private:
    IDispatch *m_dispatch;
};
#endif
}

QStringList Access2003Converter::collectDatabaseFiles(const QString &directoryPath, bool recursive)
{
    QStringList result;
    if (!QFileInfo(directoryPath).isDir())
        return result;

    const QDirIterator::IteratorFlags flags = recursive
        ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator iterator(directoryPath,
                          QStringList() << QStringLiteral("*.ais")
                                        << QStringLiteral("*.AIS")
                                        << QStringLiteral("*.aiy")
                                        << QStringLiteral("*.AIY")
                                        << QStringLiteral("*.mdb")
                                        << QStringLiteral("*.MDB"),
                          QDir::Files | QDir::Readable,
                          flags);
    QSet<QString> seen;
    while (iterator.hasNext())
    {
        const QString path = QDir::cleanPath(iterator.next());
        const QString key = normalizedPath(path);
        if (!seen.contains(key))
        {
            seen.insert(key);
            result.append(path);
        }
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QString Access2003Converter::suggestedOutputFileName(const QString &sourcePath)
{
    const QFileInfo sourceInfo(sourcePath);
    const QString baseName = sourceInfo.completeBaseName();
    return baseName + QStringLiteral("_03转换后.ais");
}

QString Access2003Converter::uniqueOutputPath(const QString &outputDirectory,
                                              const QString &sourcePath,
                                              const QStringList &reservedPaths)
{
    const QString suggestedName = suggestedOutputFileName(sourcePath);
    const QFileInfo nameInfo(suggestedName);
    const QString stem = nameInfo.completeBaseName();
    const QString suffix = QStringLiteral(".") + nameInfo.suffix();
    QSet<QString> reserved;
    for (const QString &path : reservedPaths)
        reserved.insert(normalizedPath(path));

    int counter = 1;
    QString candidate;
    do
    {
        const QString fileName = counter == 1
            ? stem + suffix
            : QStringLiteral("%1_%2%3").arg(stem).arg(counter).arg(suffix);
        candidate = QDir(outputDirectory).filePath(fileName);
        ++counter;
    }
    while (QFileInfo::exists(candidate) || QFileInfo::exists(candidate+".recovery.json") || reserved.contains(normalizedPath(candidate)));
    return QDir::cleanPath(candidate);
}

Access2003BatchResult Access2003Converter::convertFiles(const QStringList &sourcePaths,
                                                        const QString &outputDirectory,
                                                        const QString &password,
                                                        Access2003ConversionMode mode)
{
    Access2003BatchResult batchResult;
    QDir outputDir(outputDirectory);
    if (!outputDir.exists() && !QDir().mkpath(outputDirectory))
    {
        batchResult.startupError = QStringLiteral("无法创建输出目录：")
            + QDir::toNativeSeparators(outputDirectory);
        return batchResult;
    }

#ifndef Q_OS_WIN
    Q_UNUSED(sourcePaths)
    Q_UNUSED(password)
    Q_UNUSED(mode)
    batchResult.startupError = QStringLiteral("Access 2003 批量转换仅支持 Windows");
    return batchResult;
#else
    ScopedComInitialization com;
    if (FAILED(com.result()))
    {
        batchResult.startupError = QStringLiteral("初始化 Windows COM 失败：")
            + windowsErrorMessage(com.result());
        return batchResult;
    }

    const QString effectivePassword = password.isEmpty()
        ? QString::fromLatin1(kDefaultAccessPassword)
        : password;
    AccessApplication access;
    DWORD accessProcessId=0;
    batchResult.batchStarted=true;
    QStringList reservedPaths;
    for (const QString &sourcePath : sourcePaths)
    {
        Access2003ConversionItem item;
        item.sourcePath = QDir::cleanPath(QFileInfo(sourcePath).absoluteFilePath());
        item.destinationPath = uniqueOutputPath(outputDirectory, item.sourcePath, reservedPaths);
        reservedPaths.append(item.destinationPath);

        const QFileInfo sourceInfo(item.sourcePath);
        if (!sourceInfo.exists() || !sourceInfo.isFile())
        {
            item.errorMessage = QStringLiteral("源文件不存在或不是普通文件");
        }
        else if (!sourceInfo.isReadable())
        {
            item.errorMessage = QStringLiteral("源文件不可读");
        }
        else if (!isSupportedSuffix(item.sourcePath))
        {
            item.errorMessage = QStringLiteral("仅支持 AIS、AIY、MDB 文件");
        }
        else if (normalizedPath(item.sourcePath) == normalizedPath(item.destinationPath))
        {
            item.errorMessage = QStringLiteral("输出路径不能与源文件相同");
        }
        else if (isJet4Database(item.sourcePath))
        {
            item.success = QFile::copy(item.sourcePath, item.destinationPath);
            item.copiedBecauseAlreadyCompatible = item.success;
            if (!item.success)
                item.errorMessage = QStringLiteral("文件已经是 Jet 4.0 格式，但复制到输出目录失败");
        }
        else if (mode==Access2003ConversionMode::PreferDataRecovery && isJet3Database(item.sourcePath))
        {
            const auto recovery=recoverJet3Data(item.sourcePath,item.destinationPath);
            item.success=recovery.success;
            item.recoveredData=recovery.success;
            item.recoveredTables=recovery.tables;
            item.recoveredRows=recovery.rows;
            item.recoveryReportPath=recovery.reportPath;
            item.warnings=recovery.warnings;
            item.errorMessage=recovery.error;
        }
        else
        {
            if (!accessProcessId)
            {
                if (access.open(&item.errorMessage))
                {
                    accessProcessId=access.processId();
                    batchResult.accessStarted=true;
                    batchResult.accessVersion=access.version();
                    if (!accessProcessId)
                        item.errorMessage=QStringLiteral("无法识别本次 Access 2003 进程，已停止此文件转换");
                }
                if (!accessProcessId)
                {
                    access.close();
                    batchResult.items.append(item);
                    continue;
                }
            }
            QTemporaryDir staging(outputDir.filePath(".access-conversion-XXXXXX"));
            if (!staging.isValid())
            {
                item.errorMessage = QStringLiteral("无法创建转换暂存目录");
                batchResult.items.append(item);
                continue;
            }
            const QString stagedFile = staging.filePath(QFileInfo(item.destinationPath).fileName());
            AccessPasswordDialogAutomation passwordAutomation(effectivePassword);
            passwordAutomation.setTargetProcessId(accessProcessId);
            passwordAutomation.start();
            item.success = access.convert(item.sourcePath, stagedFile, &item.errorMessage);
            passwordAutomation.stop();
            if (passwordAutomation.passwordRejected())
            {
                item.success = false;
                item.errorMessage = QStringLiteral("Access 未接受数据库密码或重复要求输入密码，已取消此文件转换。请确认数据库密码后重试。");
            }
            if (item.success && QFileInfo(stagedFile).size() <= 0)
            {
                item.success = false;
                item.errorMessage = QStringLiteral("Access 未生成有效的目标文件");
            }
            if (item.success && !QFile::rename(stagedFile, item.destinationPath))
            {
                item.success = false;
                item.errorMessage = QStringLiteral("无法保存转换结果，目标文件可能已存在");
            }
        }

        if (item.success)
        {
            AppLogger::operation(QStringLiteral("Access 2003 转换完成 source=%1 destination=%2 compatibleCopy=%3")
                .arg(QDir::toNativeSeparators(item.sourcePath),
                     QDir::toNativeSeparators(item.destinationPath))
                .arg(item.copiedBecauseAlreadyCompatible));
        }
        else
        {
            AppLogger::error(QStringLiteral("Access 2003 转换失败 source=%1 error=%2")
                .arg(QDir::toNativeSeparators(item.sourcePath), item.errorMessage));
        }
        batchResult.items.append(item);
    }
    access.close();
    return batchResult;
#endif
}

bool Access2003Converter::isAccess2003Registered(QString *registrationName)
{
#ifndef Q_OS_WIN
    Q_UNUSED(registrationName)
    return false;
#else
    CLSID classId;
    const HRESULT hr = CLSIDFromProgID(L"Access.Application.11", &classId);
    if (SUCCEEDED(hr) && registrationName)
        *registrationName = QStringLiteral("Access.Application.11");
    return SUCCEEDED(hr);
#endif
}
