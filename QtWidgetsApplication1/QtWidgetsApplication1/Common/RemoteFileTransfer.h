#pragma once
#include <QString>
#include <functional>
#include <memory>

struct RemoteTransferredFile
{
    QString serverPath;
    QString sharedPath;
    QString error;
    std::shared_ptr<QString> job;
};

namespace RemoteFileTransfer
{
    using Cancel = std::function<bool()>;
    using Progress = std::function<void(const QString&)>;
    enum class InputEncoding { RawFile, Gzip };
    bool hasLiveAgent(const QString& sharedRoot, const Cancel& cancel = {});
    // Copy to a new unique file and verify SHA-256 by reading the destination back.
    // No agent is required for this local/SMB path; the returned lease owns only that file.
    RemoteTransferredFile stageCopy(const QString& source, const QString& destinationDirectory,
        const Cancel& cancel = {}, const Progress& progress = {});
    // Client uses SMB only. Jobs contain data and fixed operations, never commands.
    RemoteTransferredFile upload(const QString& source, const QString& sharedRoot,
        InputEncoding encoding = InputEncoding::RawFile,
        const Cancel& cancel = {}, const Progress& progress = {});
    // Server runs against a local directory, on a dedicated worker thread.
    bool serve(const QString& localRoot, const Cancel& cancel, const Progress& progress);
    void release(const QString& jobDirectory);
}
