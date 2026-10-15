#include "SudaBackupUnpacker.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fdi.h>
#include "../ThirdParty/zlib/zlib.h"
#include <array>
#include <atomic>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <new>

#pragma comment(lib, "cabinet.lib")

struct SudaFileHandle
{
    HANDLE value = INVALID_HANDLE_VALUE;
    SudaFileHandle() = default;
    SudaFileHandle(const SudaFileHandle&) = delete;
    SudaFileHandle& operator=(const SudaFileHandle&) = delete;
    ~SudaFileHandle()
    {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};

struct SudaTemporaryFile
{
    std::wstring path;
    SudaFileHandle file;
    ~SudaTemporaryFile()
    {
        if (file.value != INVALID_HANDLE_VALUE)
        {
            CloseHandle(file.value);
            file.value = INVALID_HANDLE_VALUE;
        }
        if (!path.empty())
            DeleteFileW(path.c_str());
    }
};

struct SudaInflateStream
{
    z_stream stream = {};
    bool initialized = false;
    ~SudaInflateStream()
    {
        if (initialized)
            inflateEnd(&stream);
    }
};

static std::string sudaWindowsError(const char* action)
{
    const DWORD error = GetLastError();
    return std::string(action) + "（Windows 错误 " + std::to_string(error) + ")";
}

static bool readSudaBytes(HANDLE file, unsigned char* data, DWORD size, DWORD& count)
{
    count = 0;
    while (count < size)
    {
        DWORD current = 0;
        if (!ReadFile(file, data + count, size - count, &current, nullptr))
            return false;
        if (!current)
            break;
        count += current;
    }
    return true;
}

static bool createSudaOutput(const std::wstring& destination, SudaTemporaryFile& output)
{
    static std::atomic<unsigned long> counter{0};
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const auto candidate = destination + L".unpacking-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(++counter);
        output.file.value = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output.file.value != INVALID_HANDLE_VALUE)
        {
            output.path = candidate;
            return true;
        }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            break;
    }
    return false;
}

static bool publishSudaOutput(SudaTemporaryFile& output, const std::wstring& destination,
    SudaUnpackResult& result)
{
    if (!FlushFileBuffers(output.file.value))
    {
        result.error = sudaWindowsError("无法保存输出文件");
        return false;
    }
    if (!CloseHandle(output.file.value))
    {
        result.error = sudaWindowsError("无法关闭输出文件");
        return false;
    }
    output.file.value = INVALID_HANDLE_VALUE;
    if (!MoveFileExW(output.path.c_str(), destination.c_str(), 0))
    {
        result.error = sudaWindowsError("无法生成目标文件，请检查同名文件及目录权限");
        return false;
    }
    output.path.clear();
    return true;
}

namespace
{
struct CabContext;
struct CabCursor
{
    CabContext* context = nullptr;
    std::int64_t position = 0;
    bool output = false;
};
struct CabContext
{
    HANDLE input = INVALID_HANDLE_VALUE;
    HANDLE output = INVALID_HANDLE_VALUE;
    std::int64_t offset = 0;
    long size = 0;
    std::uint64_t limit = 0;
    std::uint64_t expected = 0;
    std::uint64_t written = 0;
    const std::function<bool()>* cancel = nullptr;
    const char* error = nullptr;
    bool selected = false;
    bool completed = false;
    std::array<unsigned char, 4> signature = {};
    CabCursor outputCursor;

    bool stopped() noexcept
    {
        if (error) return true;
        try { if (cancel && *cancel && (*cancel)()) error = "已取消解包。"; }
        catch (...) { error = "取消检查发生异常。"; }
        return error != nullptr;
    }
};
// FDI's open callback has no user pointer. Scope it to this worker thread;
// every virtual handle thereafter owns an independent logical CAB cursor.
thread_local CabContext* currentCab = nullptr;
struct CabScope
{
    CabContext* previous;
    explicit CabScope(CabContext& context) : previous(currentCab) { currentCab = &context; }
    ~CabScope() { currentCab = previous; }
};
struct CabDecoder
{
    HFDI value = nullptr;
    ~CabDecoder() { if (value) FDIDestroy(value); }
};
static FNALLOC(cabAllocate) { return std::malloc(cb); }
static FNFREE(cabFree) { std::free(pv); }
static FNOPEN(cabOpen)
{
    (void)oflag; (void)pmode;
    if (!currentCab || currentCab->stopped() || std::strcmp(pszFile, "suda.cab") != 0)
        return -1;
    auto* cursor = new (std::nothrow) CabCursor;
    if (!cursor) return -1;
    cursor->context = currentCab;
    return reinterpret_cast<INT_PTR>(cursor);
}
static FNREAD(cabRead)
{
    auto& cursor = *reinterpret_cast<CabCursor*>(hf);
    auto& context = *cursor.context;
    if (cursor.output || context.stopped()) return static_cast<UINT>(-1);
    const auto remaining = context.size - cursor.position;
    const DWORD requested = static_cast<DWORD>(remaining < cb ? remaining : cb);
    LARGE_INTEGER offset; offset.QuadPart = context.offset + cursor.position;
    DWORD count = 0;
    if (!SetFilePointerEx(context.input, offset, nullptr, FILE_BEGIN)
        || !readSudaBytes(context.input, static_cast<unsigned char*>(pv), requested, count)
        || count != requested)
    {
        context.error = "无法读取 CAB 压缩数据或文件已截断。";
        return static_cast<UINT>(-1);
    }
    cursor.position += count;
    return count;
}
static FNWRITE(cabWrite)
{
    auto& cursor = *reinterpret_cast<CabCursor*>(hf);
    auto& context = *cursor.context;
    if (!cursor.output || context.stopped()) return static_cast<UINT>(-1);
    if (cb > context.limit - context.written || cb > context.expected - context.written)
    {
        context.error = "解包文件超过允许的大小上限或 CAB 记录大小。";
        return static_cast<UINT>(-1);
    }
    const auto* bytes = static_cast<const unsigned char*>(pv);
    for (UINT i = 0; i < cb && context.written + i < 4; ++i)
        context.signature[static_cast<size_t>(context.written + i)] = bytes[i];
    if (context.written + cb >= 4 && std::memcmp(context.signature.data(), "TAPE", 4) != 0)
    {
        context.error = "解包内容不是 SQL Server 备份，本功能不支持此数据库类型。";
        return static_cast<UINT>(-1);
    }
    DWORD count = 0;
    if (!WriteFile(context.output, pv, cb, &count, nullptr) || count != cb)
    {
        context.error = "无法写入输出文件，请检查磁盘空间和权限。";
        return static_cast<UINT>(-1);
    }
    context.written += count;
    return count;
}
static FNCLOSE(cabClose)
{
    auto* cursor = reinterpret_cast<CabCursor*>(hf);
    // The output's backing Windows handle belongs to SudaTemporaryFile.
    if (!cursor->output) delete cursor;
    return 0;
}
static FNSEEK(cabSeek)
{
    auto& cursor = *reinterpret_cast<CabCursor*>(hf);
    if (cursor.output || cursor.context->stopped()) return -1;
    std::int64_t position = dist;
    if (seektype == SEEK_CUR) position += cursor.position;
    else if (seektype == SEEK_END) position += cursor.context->size;
    else if (seektype != SEEK_SET) return -1;
    if (position < 0 || position > cursor.context->size) return -1;
    cursor.position = position;
    return static_cast<long>(position);
}
static FNFDINOTIFY(cabNotify)
{
    auto& context = *static_cast<CabContext*>(pfdin->pv);
    if (context.stopped()) return -1;
    switch (fdint)
    {
    case fdintCOPY_FILE:
        // Never use a cabinet member's name as a filesystem path.
        if (context.selected || _stricmp(pfdin->psz1, "SDBackup.bak") != 0 || pfdin->cb < 4)
        {
            context.error = "CAB 必须仅包含一个完整的 SDBackup.bak 文件。";
            return -1;
        }
        context.expected = static_cast<std::uint64_t>(pfdin->cb);
        if (context.expected > context.limit)
        {
            context.error = "解包文件超过允许的大小上限。";
            return -1;
        }
        context.selected = true;
        context.outputCursor.context = &context;
        context.outputCursor.output = true;
        return reinterpret_cast<INT_PTR>(&context.outputCursor);
    case fdintCLOSE_FILE_INFO:
        cabClose(pfdin->hf);
        context.completed = context.written == context.expected && context.written >= 4;
        return context.completed ? TRUE : FALSE;
    case fdintPARTIAL_FILE:
    case fdintNEXT_CABINET:
        context.error = "不支持分卷 CAB 备份。";
        return -1;
    default:
        return 0;
    }
}
static bool readCabUint32(HANDLE input, std::uint32_t& value)
{
    unsigned char bytes[4]; DWORD count = 0;
    if (!readSudaBytes(input, bytes, 4, count) || count != 4) return false;
    value = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8)
        | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
    return true;
}
static bool readCabString(HANDLE input, std::string& value)
{
    std::uint32_t length = 0;
    if (!readCabUint32(input, length) || !length || length > 4096) return false;
    std::string bytes(length, '\0'); DWORD count = 0;
    if (!readSudaBytes(input, reinterpret_cast<unsigned char*>(&bytes[0]), length, count)
        || count != length || bytes.back() != '\0' || bytes.find('\0') != length - 1)
        return false;
    bytes.pop_back(); value.swap(bytes);
    return true;
}
static SudaUnpackResult unpackSuperData(HANDLE input, const std::wstring& destination,
    std::uint64_t limit, const std::function<bool()>& cancel)
{
    SudaUnpackResult result;
    LARGE_INTEGER position; position.QuadPart = sizeof("SuperData SQLServer Backup File") - 1;
    unsigned char version[2]; DWORD count = 0;
    std::uint32_t cabSize = 0;
    if (!SetFilePointerEx(input, position, nullptr, FILE_BEGIN)
        || !readSudaBytes(input, version, 2, count) || count != 2 || version[0] != 1 || version[1] != 0
        || !readCabString(input, result.product) || !readCabString(input, result.accountCode)
        || !readCabString(input, result.accountName) || !readCabUint32(input, cabSize))
    {
        result.error = "SuperData 文件头不完整、版本不支持或字段长度无效。";
        return result;
    }
    LARGE_INTEGER zero = {}, offset = {}, size = {};
    if (!SetFilePointerEx(input, zero, &offset, FILE_CURRENT) || !GetFileSizeEx(input, &size)
        || cabSize < 36 || cabSize > LONG_MAX || offset.QuadPart + cabSize != size.QuadPart)
    {
        result.error = "SuperData CAB 大小无效、文件被截断或包含尾部数据（单包上限 2 GiB）。";
        return result;
    }
    CabContext context;
    context.input = input; context.offset = offset.QuadPart; context.size = static_cast<long>(cabSize);
    context.limit = limit; context.cancel = &cancel;
    CabScope scope(context);
    CabDecoder decoder;
    ERF error = {};
    decoder.value = FDICreate(cabAllocate, cabFree, cabOpen, cabRead, cabWrite,
        cabClose, cabSeek, cpuUNKNOWN, &error);
    if (!decoder.value)
    {
        result.error = "无法初始化 Windows CAB 解压模块。";
        return result;
    }
    char cabinetName[] = "suda.cab", cabinetPath[] = "";
    const INT_PTR probe = cabOpen(cabinetName, 0, 0);
    FDICABINETINFO info = {};
    const bool valid = probe != -1 && FDIIsCabinet(decoder.value, probe, &info);
    if (probe != -1) cabClose(probe);
    if (!valid || info.cbCabinet != context.size || info.cFiles != 1
        || info.hasprev || info.hasnext || info.iCabinet != 0)
    {
        result.error = context.error ? context.error : "CAB 无效，或不是仅含一个文件的完整单卷备份。";
        return result;
    }
    SudaTemporaryFile output;
    if (!createSudaOutput(destination, output))
    {
        result.error = sudaWindowsError("无法创建输出临时文件，请检查目录和权限");
        return result;
    }
    context.output = output.file.value;
    if (!FDICopy(decoder.value, cabinetName, cabinetPath, 0, cabNotify, nullptr, &context)
        || !context.completed || context.stopped())
    {
        result.error = context.error ? context.error : "CAB 解压失败：压缩数据损坏、校验失败或内容不完整。";
        return result;
    }
    if (!publishSudaOutput(output, destination, result)) return result;
    result.outputBytes = context.written;
    result.success = true;
    return result;
}
} // namespace

SudaUnpackResult SudaBackupUnpacker::unpack(
    const std::wstring& sourcePath,
    const std::wstring& destinationPath,
    std::uint64_t maxOutputBytes,
    const std::function<bool()>& isCancelled)
{
    SudaUnpackResult result;
    if (sourcePath.empty() || destinationPath.empty() || !maxOutputBytes)
    {
        result.error = "路径为空或输出大小上限无效。";
        return result;
    }
    if (GetFileAttributesW(destinationPath.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        result.error = "输出文件已存在，请选择新的文件名。";
        return result;
    }
    SudaFileHandle input;
    input.value = CreateFileW(sourcePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (input.value == INVALID_HANDLE_VALUE)
    {
        result.error = sudaWindowsError("无法打开源文件");
        return result;
    }
    if (GetFileType(input.value) != FILE_TYPE_DISK)
    {
        result.error = "源文件必须是磁盘上的普通文件。";
        return result;
    }
    std::array<unsigned char, 181> header = {};
    DWORD count = 0;
    if (!readSudaBytes(input.value, header.data(), static_cast<DWORD>(header.size()), count))
    {
        result.error = sudaWindowsError("无法读取文件头");
        return result;
    }
    const char superDataSignature[] = "SuperData SQLServer Backup File";
    if (count >= sizeof(superDataSignature) - 1
        && std::memcmp(header.data(), superDataSignature, sizeof(superDataSignature) - 1) == 0)
        return unpackSuperData(input.value, destinationPath, maxOutputBytes, isCancelled);
    const unsigned char signature[] = "\x20" "SD19960617-1998-0908-SDSOFT-ZWLQ";
    if (count != header.size() || std::memcmp(header.data(), signature, 33) != 0
        || header[33] > 16 || header[50] > 32 || header[83] > 64)
    {
        result.error = "文件头不完整或不属于支持的速达单卷备份格式。";
        return result;
    }
    result.product.assign(reinterpret_cast<const char*>(header.data() + 34), header[33]);
    result.accountCode.assign(reinterpret_cast<const char*>(header.data() + 51), header[50]);
    result.accountName.assign(reinterpret_cast<const char*>(header.data() + 84), header[83]);

    SudaInflateStream inflater;
    if (inflateInit(&inflater.stream) != Z_OK)
    {
        result.error = "无法初始化解压模块。";
        return result;
    }
    inflater.initialized = true;
    SudaTemporaryFile output;
    if (!createSudaOutput(destinationPath, output))
    {
        result.error = sudaWindowsError("无法创建输出临时文件，请检查目录和权限");
        return result;
    }

    std::array<unsigned char, 16384> encoded = {};
    std::array<unsigned char, 65536> decoded = {};
    std::uint32_t seed = 0x000BC2FFu;
    std::uint64_t outputBytes = 0;
    bool finished = false;
    std::array<unsigned char, 4> mediaSignature = {};
    DWORD signatureBytes = 0;
    while (!finished)
    {
        if (isCancelled && isCancelled())
        {
            result.error = "已取消解包。";
            return result;
        }
        if (!readSudaBytes(input.value, encoded.data(), static_cast<DWORD>(encoded.size()), count))
        {
            result.error = sudaWindowsError("无法读取备份内容");
            return result;
        }
        if (!count)
        {
            result.error = "备份内容被截断，无法完整解包。";
            return result;
        }
        // The position restarts in each 16 KiB block; the PRNG state does not.
        for (DWORD index = 0; index < count; ++index)
        {
            seed = seed * 0x08088405u + 1u;
            const std::uint32_t delta = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(seed) * (index + 1u)) >> 32);
            encoded[index] = static_cast<unsigned char>(encoded[index] - delta);
        }
        inflater.stream.next_in = encoded.data();
        inflater.stream.avail_in = count;
        do
        {
            inflater.stream.next_out = decoded.data();
            inflater.stream.avail_out = static_cast<uInt>(decoded.size());
            const uInt previousInput = inflater.stream.avail_in;
            const int status = inflate(&inflater.stream, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR)
            {
                result.error = "备份压缩数据损坏或校验失败。";
                return result;
            }
            const DWORD produced = static_cast<DWORD>(decoded.size()) - inflater.stream.avail_out;
            for (DWORD i = 0; i < produced && signatureBytes < mediaSignature.size(); ++i)
                mediaSignature[signatureBytes++] = decoded[i];
            if (signatureBytes == mediaSignature.size() && std::memcmp(mediaSignature.data(), "TAPE", 4) != 0)
            {
                result.error = "解包内容不是 SQL Server 备份，本功能不支持此数据库类型。";
                return result;
            }
            if (produced > maxOutputBytes - outputBytes)
            {
                result.error = "解包文件超过允许的大小上限。";
                return result;
            }
            DWORD written = 0;
            if (produced && (!WriteFile(output.file.value, decoded.data(), produced, &written, nullptr)
                || written != produced))
            {
                result.error = sudaWindowsError("无法写入输出文件，请检查磁盘空间和权限");
                return result;
            }
            outputBytes += produced;
            if (status == Z_STREAM_END)
            {
                unsigned char extra = 0;
                DWORD extraCount = 0;
                if (inflater.stream.avail_in != 0)
                {
                    result.error = "备份包含未识别的尾部数据，无法确认文件格式。";
                    return result;
                }
                if (!readSudaBytes(input.value, &extra, 1, extraCount))
                {
                    result.error = sudaWindowsError("无法检查备份文件结尾");
                    return result;
                }
                if (extraCount)
                {
                    result.error = "备份包含未识别的尾部数据，无法确认文件格式。";
                    return result;
                }
                finished = true;
                break;
            }
            if (!produced && previousInput == inflater.stream.avail_in)
            {
                if (!inflater.stream.avail_in)
                    break;
                result.error = "压缩数据异常，解包无法继续。";
                return result;
            }
        } while (inflater.stream.avail_in || !inflater.stream.avail_out);
    }
    if (signatureBytes != mediaSignature.size())
    {
        result.error = "解包内容不是完整的 SQL Server 备份。";
        return result;
    }
    if (isCancelled && isCancelled())
    {
        result.error = "已取消解包。";
        return result;
    }
    if (!publishSudaOutput(output, destinationPath, result)) return result;
    result.outputBytes = outputBytes;
    result.success = true;
    return result;
}
