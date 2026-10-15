#ifndef SUBJECTMERGEENGINE_H
#define SUBJECTMERGEENGINE_H

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct SubjectMergeNode
{
    QString code;
    QString name;
    QString parentCode;
    int direction = 1;
    int level = 1;
    bool isLeaf = true;
};

struct SubjectMergeRequest
{
    QStringList sourceCodes;
    QString targetCode;
    QString targetName;
    int targetDirection = 1;
    bool createTarget = false;
};

struct SubjectMergeResult
{
    QJsonObject root;
    QMap<QString, QString> codeMapping;
    QStringList logs;
    int removedAccountCount = 0;
    int createdAccountCount = 0;
    int changedVoucherCount = 0;
    int changedBalanceCount = 0;
    int mergedBalanceCount = 0;
    int reversedOpeningCount = 0;
};

namespace SubjectMergeEngine
{
bool buildTree(const QJsonObject &root,
               QList<SubjectMergeNode> *nodes,
               QString *errorMessage);

QString suggestChildCode(const QList<SubjectMergeNode> &nodes,
                         const QString &parentCode);

// Publishes result only after all writes and hierarchy checks succeed.
// On failure root and the caller's existing result are unchanged.
bool merge(const QJsonObject &root,
           const SubjectMergeRequest &request,
           SubjectMergeResult *result,
           QString *errorMessage);
}

#endif // SUBJECTMERGEENGINE_H
