#pragma once

#include "subjectbalanceimport.h"

struct BalanceFileSuggestions
{
    QMap<QString, QString> matches;
    QMap<QString, QStringList> candidates;
};

// Reads filenames only, within the selected file's directory. Never reads or rewrites workbooks.
BalanceFileSuggestions suggestBalanceFiles(const BalanceImportSoftware &software,
                                           const QString &selectedFile,
                                           const QMap<QString, QString> &existing);
