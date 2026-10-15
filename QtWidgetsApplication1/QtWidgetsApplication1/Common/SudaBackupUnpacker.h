#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct SudaUnpackResult
{
    bool success = false;
    std::string error;
    // Original header bytes, normally GBK. Convert at the UI boundary.
    std::string product;
    std::string accountCode;
    std::string accountName;
    std::uint64_t outputBytes = 0;
};

class SudaBackupUnpacker
{
public:
    // Windows, C++11, zlib and system Cabinet API. Destination must not exist; parent must exist.
    // Supports single-volume SD19960617 and SuperData v1 with one SDBackup.bak member.
    // Embedded CAB size must fit the Cabinet API's signed 32-bit seek range.
    // Only publishes content with the SQL Server TAPE media signature.
    static SudaUnpackResult unpack(
        const std::wstring& sourcePath,
        const std::wstring& destinationPath,
        std::uint64_t maxOutputBytes = 16ULL * 1024 * 1024 * 1024,
        const std::function<bool()>& isCancelled = {});
};
