#include "subjectmergeengine.h"

#include <algorithm>
#include <new>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>
#include <QVariant>
#include <QVector>

namespace
{
struct JsonStorageError {};

void checkedInsert(QJsonObject *object, const QString &key, const QJsonValue &value)
{
    object->insert(key, value);
    // Qt 5.12 may only warn and return without writing when its JSON limit is hit.
    if (object->value(key) != value)
        throw JsonStorageError();
}

QJsonObject packedObject(const QJsonObject &source)
{
    QJsonObject result;
    for (auto it = source.constBegin(); it != source.constEnd(); ++it)
        checkedInsert(&result, it.key(), it.value());
    return result;
}

void appendRecord(QJsonArray *array, const QJsonObject &record)
{
    const QJsonObject packed = packedObject(record);
    const int previousSize = array->size();
    array->append(packed);
    if (array->size() != previousSize + 1 || array->last().toObject() != packed)
        throw JsonStorageError();
}

struct MutableNode
{
    QJsonObject account;
    QString originalCode;
    QString code;
    QString name;
    int parentIndex = -1;
    int direction = 1;
    int originalIndex = -1;
    QList<int> children;
    bool alive = true;
};

struct BalanceContribution
{
    QString code;
    int direction = 1;
};

QString normalizedCode(const QJsonValue &value)
{
    if (value.isString())
    {
        QString code;
        const QString text = value.toString();
        code.reserve(text.size());
        for (const QChar character : text)
        {
            if (!character.isSpace())
                code.append(character);
        }
        return code;
    }
    if (value.isDouble())
    {
        const double number = value.toDouble();
        const qint64 integer = static_cast<qint64>(number);
        if (qAbs(number - static_cast<double>(integer)) < 0.000001)
            return QString::number(integer);
    }
    return QString();
}

int normalizedDirection(const QJsonValue &value)
{
    return value.toVariant().toInt() < 0 ? -1 : 1;
}

bool isParentPrefix(const QString &parentCode, const QString &childCode)
{
    if (parentCode.isEmpty() || childCode.size() <= parentCode.size()
        || !childCode.startsWith(parentCode))
    {
        return false;
    }
    if (parentCode.contains(QLatin1Char('.')))
        return childCode.at(parentCode.size()) == QLatin1Char('.');
    return true;
}

QString childSegment(const QString &parentCode, const QString &childCode)
{
    QString segment = childCode.mid(parentCode.size());
    while (segment.startsWith(QLatin1Char('.')))
        segment.remove(0, 1);
    return segment;
}

bool parseNodes(const QJsonObject &root,
                QVector<MutableNode> *output,
                QString *errorMessage)
{
    if (!root.value(QStringLiteral("GLAccountList")).isArray())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("未找到 GLAccountList 科目表");
        return false;
    }

    const QJsonArray accounts = root.value(QStringLiteral("GLAccountList")).toArray();
    if (accounts.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("GLAccountList 科目表为空");
        return false;
    }

    QVector<MutableNode> nodes;
    nodes.reserve(accounts.size() + 16);
    QSet<QString> seenCodes;
    for (int index = 0; index < accounts.size(); ++index)
    {
        if (!accounts.at(index).isObject())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("科目表第 %1 条记录不是对象").arg(index + 1);
            return false;
        }

        const QJsonObject account = accounts.at(index).toObject();
        const QString code = normalizedCode(account.value(QStringLiteral("AcctCode")));
        if (code.isEmpty())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("科目表第 %1 条记录的科目编码为空").arg(index + 1);
            return false;
        }
        if (seenCodes.contains(code))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("科目编码 %1 重复，请先完成科目编码冲突拆分后再合并")
                    .arg(code);
            return false;
        }
        seenCodes.insert(code);

        MutableNode node;
        node.account = account;
        node.originalCode = code;
        node.code = code;
        node.name = account.value(QStringLiteral("AcctName")).toString().trimmed();
        node.direction = normalizedDirection(account.value(QStringLiteral("AcctDC")));
        node.originalIndex = index;
        nodes.append(node);
    }

    for (int index = 0; index < nodes.size(); ++index)
    {
        int parentIndex = -1;
        int parentLength = -1;
        for (int candidate = 0; candidate < nodes.size(); ++candidate)
        {
            if (candidate == index)
                continue;
            const QString candidateCode = nodes.at(candidate).code;
            if (candidateCode.size() > parentLength
                && isParentPrefix(candidateCode, nodes.at(index).code))
            {
                parentIndex = candidate;
                parentLength = candidateCode.size();
            }
        }
        nodes[index].parentIndex = parentIndex;
        if (parentIndex >= 0)
            nodes[parentIndex].children.append(index);
    }

    *output = nodes;
    return true;
}

int nodeLevel(const QVector<MutableNode> &nodes, int index)
{
    int level = 1;
    int current = nodes.at(index).parentIndex;
    while (current >= 0)
    {
        ++level;
        current = nodes.at(current).parentIndex;
    }
    return level;
}

QString childCodeForParent(const QVector<MutableNode> &nodes,
                           int parentIndex,
                           const QSet<QString> &reservedCodes,
                           const QString &preferredSegment = QString())
{
    const QString parentCode = parentIndex < 0 ? QString() : nodes.at(parentIndex).code;
    bool dotted = parentCode.contains(QLatin1Char('.'));
    int width = parentIndex < 0 ? 4 : 3;
    QList<int> siblingIndices;

    if (parentIndex >= 0)
        siblingIndices = nodes.at(parentIndex).children;
    else
    {
        for (int index = 0; index < nodes.size(); ++index)
        {
            if (nodes.at(index).alive && nodes.at(index).parentIndex < 0)
                siblingIndices.append(index);
        }
    }

    for (const int siblingIndex : siblingIndices)
    {
        const MutableNode &sibling = nodes.at(siblingIndex);
        if (!sibling.alive)
            continue;
        if (parentIndex < 0)
        {
            width = qMax(width, sibling.code.size());
            continue;
        }
        if (sibling.code.mid(parentCode.size()).startsWith(QLatin1Char('.')))
            dotted = true;
        const QString segment = childSegment(parentCode, sibling.code);
        if (!segment.isEmpty())
            width = qMax(width, segment.size());
    }

    if (parentIndex >= 0 && !dotted)
    {
        for (int index = 0; index < nodes.size(); ++index)
        {
            const MutableNode &candidate = nodes.at(index);
            if (!candidate.alive || candidate.parentIndex < 0)
                continue;
            const QString candidateParentCode = nodes.at(candidate.parentIndex).code;
            if (candidate.code.startsWith(candidateParentCode + QLatin1Char('.')))
            {
                dotted = true;
                break;
            }
        }
    }

    const QString separator = parentIndex >= 0 && dotted
        ? QStringLiteral(".") : QString();
    if (!preferredSegment.isEmpty())
    {
        const QString candidate = parentCode + separator + preferredSegment;
        if (!reservedCodes.contains(candidate))
            return candidate;
    }

    for (int number = 1; number < 10000000; ++number)
    {
        const QString segment = QStringLiteral("%1").arg(number, width, 10, QLatin1Char('0'));
        const QString candidate = parentCode + separator + segment;
        if (!reservedCodes.contains(candidate))
            return candidate;
    }
    return QString();
}

bool isOpeningAmountField(const QString &key)
{
    return key == QStringLiteral("BegBal")
        || key == QStringLiteral("BegQty")
        || key == QStringLiteral("FX_BAmount_s")
        || key.startsWith(QStringLiteral("Beg"), Qt::CaseInsensitive)
        || key.startsWith(QStringLiteral("Begin"), Qt::CaseInsensitive)
        || key.startsWith(QStringLiteral("Opening"), Qt::CaseInsensitive);
}

bool isSummableBalanceField(const QString &key)
{
    return isOpeningAmountField(key)
        || key.startsWith(QStringLiteral("FX_LJ_"), Qt::CaseInsensitive);
}

QString balanceIdentity(const QJsonObject &balance)
{
    QJsonObject identity = balance;
    identity.remove(QStringLiteral("AcctId"));
    const QStringList keys = identity.keys();
    for (const QString &key : keys)
    {
        if (isSummableBalanceField(key))
            identity.remove(key);
    }
    return QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact));
}

void addBalanceAmounts(QJsonObject *target, const QJsonObject &source)
{
    for (auto it = source.constBegin(); it != source.constEnd(); ++it)
    {
        if (!isSummableBalanceField(it.key()) || !it.value().isDouble())
            continue;
        const double previous = target->value(it.key()).toDouble();
        checkedInsert(target, it.key(), previous + it.value().toDouble());
    }
}

class MergeState
{
public:
    MergeState(const QJsonObject &root,
               const QVector<MutableNode> &nodes,
               SubjectMergeResult *result)
        : m_root(root), m_nodes(nodes), m_result(result)
    {
        for (int index = 0; index < m_nodes.size(); ++index)
        {
            m_originalIndexByCode.insert(m_nodes.at(index).originalCode, index);
            m_originalDirections.insert(m_nodes.at(index).originalCode,
                                        m_nodes.at(index).direction);
            m_reservedCodes.insert(m_nodes.at(index).code);
        }
    }

    bool apply(const SubjectMergeRequest &request, QString *errorMessage)
    {
        QStringList sourceCodes;
        QSet<QString> sourceSet;
        for (const QString &rawCode : request.sourceCodes)
        {
            const QString code = normalizedCode(QJsonValue(rawCode));
            if (!code.isEmpty() && !sourceSet.contains(code))
            {
                sourceSet.insert(code);
                sourceCodes.append(code);
            }
        }
        if (sourceCodes.size() < 2)
            return fail(QStringLiteral("请至少选择两个同父级科目进行合并"), errorMessage);

        QList<int> sourceIndices;
        int sharedParent = -2;
        bool hasNonLeaf = false;
        for (const QString &code : sourceCodes)
        {
            if (!m_originalIndexByCode.contains(code))
                return fail(QStringLiteral("待合并科目 %1 不存在").arg(code), errorMessage);
            const int index = m_originalIndexByCode.value(code);
            if (sharedParent == -2)
                sharedParent = m_nodes.at(index).parentIndex;
            else if (sharedParent != m_nodes.at(index).parentIndex)
                return fail(QStringLiteral("只有父级相同的同级科目才能合并"), errorMessage);
            hasNonLeaf = hasNonLeaf || !m_nodes.at(index).children.isEmpty();
            sourceIndices.append(index);
        }

        const QString targetCode = normalizedCode(QJsonValue(request.targetCode));
        if (targetCode.isEmpty())
            return fail(QStringLiteral("目标科目编码不能为空"), errorMessage);

        int targetIndex = -1;
        if (request.createTarget)
        {
            if (request.targetName.trimmed().isEmpty())
                return fail(QStringLiteral("新建目标科目时，科目名称不能为空"), errorMessage);
            if (request.targetDirection != 1 && request.targetDirection != -1)
                return fail(QStringLiteral("目标科目的余额方向必须为借方或贷方"), errorMessage);
            if (m_reservedCodes.contains(targetCode))
                return fail(QStringLiteral("目标科目编码 %1 已存在").arg(targetCode), errorMessage);
            if (!validateNewTargetCode(targetCode, sharedParent, errorMessage))
                return false;

            targetIndex = cloneNode(sourceIndices.first(), sharedParent, targetCode,
                                    request.targetName.trimmed(), request.targetDirection);
        }
        else
        {
            if (!sourceSet.contains(targetCode))
                return fail(QStringLiteral("现有目标科目必须是本次选中的科目之一"), errorMessage);
            targetIndex = m_originalIndexByCode.value(targetCode);
        }

        if (hasNonLeaf && !request.createTarget && m_nodes.at(targetIndex).children.isEmpty())
            promoteLeaf(targetIndex);

        QList<int> orderedSources;
        for (const int index : sourceIndices)
        {
            if (index != targetIndex && !m_nodes.at(index).children.isEmpty())
                orderedSources.append(index);
        }
        for (const int index : sourceIndices)
        {
            if (index != targetIndex && m_nodes.at(index).children.isEmpty())
                orderedSources.append(index);
        }

        for (const int sourceIndex : orderedSources)
            mergeNode(sourceIndex, targetIndex, hasNonLeaf);

        rebuildAccounts();
        if (!rewriteBalances(errorMessage) || !rewriteVouchers(errorMessage))
            return false;

        if (!assembleResult(errorMessage))
            return false;
        m_result->codeMapping = m_mapping;
        m_result->logs.append(QStringLiteral("合并 %1 个同级科目，目标：%2 %3")
            .arg(sourceCodes.size()).arg(m_nodes.at(targetIndex).code,
                                          m_nodes.at(targetIndex).name));
        m_result->logs.append(QStringLiteral("删除科目 %1 个，新增科目 %2 个，凭证科目编码调整 %3 条，期初科目编码调整 %4 条")
            .arg(m_result->removedAccountCount)
            .arg(m_result->createdAccountCount)
            .arg(m_result->changedVoucherCount)
            .arg(m_result->changedBalanceCount));
        if (m_result->reversedOpeningCount > 0)
            m_result->logs.append(QStringLiteral("借贷方向相反，期初金额取反 %1 条")
                .arg(m_result->reversedOpeningCount));
        return true;
    }

private:
    bool fail(const QString &message, QString *errorMessage) const
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    }

    bool validateNewTargetCode(const QString &targetCode,
                               int sharedParent,
                               QString *errorMessage) const
    {
        int inferredParent = -1;
        int inferredLength = -1;
        for (int index = 0; index < m_nodes.size(); ++index)
        {
            const MutableNode &node = m_nodes.at(index);
            if (node.code.size() > inferredLength
                && isParentPrefix(node.code, targetCode))
            {
                inferredParent = index;
                inferredLength = node.code.size();
            }
            if (isParentPrefix(targetCode, node.code))
            {
                return fail(QStringLiteral("目标编码 %1 会改变现有科目 %2 的父级")
                            .arg(targetCode, node.code), errorMessage);
            }
        }
        if (inferredParent != sharedParent)
        {
            const QString expectedParent = sharedParent < 0
                ? QStringLiteral("一级科目") : m_nodes.at(sharedParent).code;
            return fail(QStringLiteral("目标科目 %1 与所选科目的父级不一致，预期父级：%2")
                        .arg(targetCode, expectedParent), errorMessage);
        }
        return true;
    }

    int cloneNode(int templateIndex,
                  int parentIndex,
                  const QString &code,
                  const QString &name,
                  int direction)
    {
        MutableNode node;
        node.account = m_nodes.at(templateIndex).account;
        node.code = code;
        node.name = name;
        node.parentIndex = parentIndex;
        node.direction = direction;
        node.originalIndex = m_nodes.size();
        checkedInsert(&node.account, QStringLiteral("AcctCode"), code);
        checkedInsert(&node.account, QStringLiteral("AcctName"), name);
        checkedInsert(&node.account, QStringLiteral("AcctDC"), direction);
        const int index = m_nodes.size();
        m_nodes.append(node);
        if (parentIndex >= 0)
            m_nodes[parentIndex].children.append(index);
        m_reservedCodes.insert(code);
        ++m_result->createdAccountCount;
        return index;
    }

    void mapOriginal(const QString &oldCode, const QString &newCode)
    {
        if (!oldCode.isEmpty())
            m_mapping.insert(oldCode, newCode);
    }

    void addBalanceContribution(const QString &sourceCode,
                                int destinationIndex)
    {
        if (sourceCode.isEmpty())
            return;
        BalanceContribution contribution;
        contribution.code = m_nodes.at(destinationIndex).code;
        contribution.direction = m_nodes.at(destinationIndex).direction;
        m_extraBalances[sourceCode].append(contribution);
    }

    int promoteLeaf(int index)
    {
        const QString sourceCode = m_nodes.at(index).originalCode;
        const QString code = childCodeForParent(m_nodes, index, m_reservedCodes);
        const int child = cloneNode(index, index, code, m_nodes.at(index).name,
                                    m_nodes.at(index).direction);
        for (auto it = m_mapping.begin(); it != m_mapping.end(); ++it)
        {
            if (it.value() == m_nodes.at(index).code)
            {
                it.value() = code;
                addBalanceContribution(it.key(), index);
            }
        }
        if (!sourceCode.isEmpty() && !m_mapping.contains(sourceCode))
        {
            mapOriginal(sourceCode, code);
            addBalanceContribution(sourceCode, index);
        }
        return child;
    }

    int childWithName(int parentIndex, const QString &name) const
    {
        const QString wanted = name.trimmed();
        for (const int index : m_nodes.at(parentIndex).children)
        {
            if (m_nodes.at(index).alive && m_nodes.at(index).name.trimmed() == wanted)
                return index;
        }
        return -1;
    }

    void detachNode(int index)
    {
        const int parent = m_nodes.at(index).parentIndex;
        if (parent >= 0)
            m_nodes[parent].children.removeAll(index);
    }

    void removeNode(int index)
    {
        detachNode(index);
        m_nodes[index].alive = false;
        ++m_result->removedAccountCount;
    }

    void reparentSubtree(int sourceIndex, int targetParent)
    {
        const QString previousCode = m_nodes.at(sourceIndex).code;
        const int oldParent = m_nodes.at(sourceIndex).parentIndex;
        const QString previousParentCode = oldParent < 0
            ? QString() : m_nodes.at(oldParent).code;
        const QString segment = oldParent < 0
            ? previousCode : childSegment(previousParentCode, previousCode);
        detachNode(sourceIndex);
        m_nodes[sourceIndex].parentIndex = targetParent;
        m_nodes[targetParent].children.append(sourceIndex);
        m_nodes[sourceIndex].code = childCodeForParent(m_nodes, targetParent,
                                                       m_reservedCodes, segment);
        m_nodes[sourceIndex].direction = m_nodes.at(targetParent).direction;
        m_reservedCodes.insert(m_nodes.at(sourceIndex).code);
        mapOriginal(m_nodes.at(sourceIndex).originalCode,
                    m_nodes.at(sourceIndex).code);

        const QList<int> children = m_nodes.at(sourceIndex).children;
        for (const int child : children)
            recodeDescendant(child, sourceIndex, previousCode);
    }

    void recodeDescendant(int index,
                          int parentIndex,
                          const QString &oldParentCode)
    {
        const QString oldCode = m_nodes.at(index).code;
        const QString segment = childSegment(oldParentCode, oldCode);
        m_nodes[index].code = childCodeForParent(m_nodes, parentIndex,
                                                m_reservedCodes, segment);
        m_nodes[index].direction = m_nodes.at(parentIndex).direction;
        m_reservedCodes.insert(m_nodes.at(index).code);
        mapOriginal(m_nodes.at(index).originalCode, m_nodes.at(index).code);
        const QList<int> children = m_nodes.at(index).children;
        for (const int child : children)
            recodeDescendant(child, index, oldCode);
    }

    void mergeLeafIntoNonLeaf(int sourceIndex, int destinationIndex)
    {
        const QString sourceCode = m_nodes.at(sourceIndex).originalCode;
        const QString sourceName = m_nodes.at(sourceIndex).name;
        const int matchingChild = childWithName(destinationIndex, sourceName);
        if (matchingChild < 0)
        {
            reparentSubtree(sourceIndex, destinationIndex);
        }
        else if (m_nodes.at(matchingChild).children.isEmpty())
        {
            mapOriginal(sourceCode, m_nodes.at(matchingChild).code);
            removeNode(sourceIndex);
        }
        else
        {
            mergeLeafIntoNonLeaf(sourceIndex, matchingChild);
        }
        addBalanceContribution(sourceCode, destinationIndex);
    }

    void mergeNode(int sourceIndex,
                   int destinationIndex,
                   bool destinationMustBeNonLeaf = false)
    {
        if (sourceIndex == destinationIndex || !m_nodes.at(sourceIndex).alive)
            return;

        const bool sourceIsLeaf = m_nodes.at(sourceIndex).children.isEmpty();
        const bool destinationIsLeaf = m_nodes.at(destinationIndex).children.isEmpty();
        if (sourceIsLeaf && !destinationIsLeaf)
        {
            mergeLeafIntoNonLeaf(sourceIndex, destinationIndex);
            return;
        }
        if (sourceIsLeaf && destinationIsLeaf && destinationMustBeNonLeaf)
        {
            reparentSubtree(sourceIndex, destinationIndex);
            addBalanceContribution(m_nodes.at(sourceIndex).originalCode,
                                   destinationIndex);
            return;
        }
        if (!sourceIsLeaf && destinationIsLeaf
            && !m_nodes.at(destinationIndex).originalCode.isEmpty())
        {
            promoteLeaf(destinationIndex);
        }

        mapOriginal(m_nodes.at(sourceIndex).originalCode,
                    m_nodes.at(destinationIndex).code);
        if (sourceIsLeaf && m_nodes.at(destinationIndex).children.isEmpty())
        {
            removeNode(sourceIndex);
            return;
        }

        const QList<int> sourceChildren = m_nodes.at(sourceIndex).children;
        for (const int childIndex : sourceChildren)
        {
            const int matchingChild = childWithName(destinationIndex,
                                                    m_nodes.at(childIndex).name);
            if (matchingChild >= 0)
                mergeNode(childIndex, matchingChild);
            else
                reparentSubtree(childIndex, destinationIndex);
        }
        removeNode(sourceIndex);
    }

    void rebuildAccounts()
    {
        QList<int> indices;
        for (int index = 0; index < m_nodes.size(); ++index)
        {
            if (m_nodes.at(index).alive)
                indices.append(index);
        }
        std::sort(indices.begin(), indices.end(), [this](int left, int right) {
            return m_nodes.at(left).code < m_nodes.at(right).code;
        });

        QJsonArray accounts;
        for (const int index : indices)
        {
            MutableNode &node = m_nodes[index];
            QJsonObject account = node.account;
            checkedInsert(&account, QStringLiteral("AcctCode"), node.code);
            checkedInsert(&account, QStringLiteral("AcctName"), node.name);
            checkedInsert(&account, QStringLiteral("AcctDC"), node.direction);
            checkedInsert(&account, QStringLiteral("AcctLevel"), nodeLevel(m_nodes, index));
            checkedInsert(&account, QStringLiteral("AcctIsDetail"), node.children.isEmpty() ? 1 : 0);
            if (node.parentIndex >= 0)
                checkedInsert(&account, QStringLiteral("AcctParent"), m_nodes.at(node.parentIndex).code);
            else
            {
                const QString originalParent = normalizedCode(account.value(QStringLiteral("AcctParent")));
                checkedInsert(&account, QStringLiteral("AcctParent"),
                               originalParent == QStringLiteral("0")
                                   ? QStringLiteral("0") : QString());
            }

            if (!normalizedCode(account.value(QStringLiteral("AcctFullCode"))).isEmpty())
                checkedInsert(&account, QStringLiteral("AcctFullCode"), node.code);
            if (!normalizedCode(account.value(QStringLiteral("AcctRoot"))).isEmpty())
            {
                int rootIndex = index;
                while (m_nodes.at(rootIndex).parentIndex >= 0)
                    rootIndex = m_nodes.at(rootIndex).parentIndex;
                checkedInsert(&account, QStringLiteral("AcctRoot"), m_nodes.at(rootIndex).code);
            }
            node.account = account;
            m_directionsByCode.insert(node.code, node.direction);
            appendRecord(&accounts, account);
        }
        m_accounts = accounts;
    }

    QJsonObject convertedBalance(const QJsonObject &source,
                                 const QString &targetCode,
                                 int targetDirection,
                                 bool countChange)
    {
        QJsonObject converted = source;
        const QString oldCode = normalizedCode(source.value(QStringLiteral("AcctCode")));
        checkedInsert(&converted, QStringLiteral("AcctCode"), targetCode);
        if (countChange && oldCode != targetCode)
            ++m_result->changedBalanceCount;
        if (m_originalDirections.value(oldCode, targetDirection) != targetDirection)
        {
            const QStringList keys = converted.keys();
            for (const QString &key : keys)
            {
                const QJsonValue value = converted.value(key);
                if (isOpeningAmountField(key) && value.isDouble())
                    checkedInsert(&converted, key, -value.toDouble());
            }
            ++m_result->reversedOpeningCount;
        }
        return converted;
    }

    bool rewriteBalances(QString *errorMessage)
    {
        if (!m_root.contains(QStringLiteral("GLBalanceList")))
            return true;
        if (!m_root.value(QStringLiteral("GLBalanceList")).isArray())
            return fail(QStringLiteral("GLBalanceList 不是期初余额数组"), errorMessage);

        QSet<QString> affectedTargets;
        for (auto it = m_mapping.constBegin(); it != m_mapping.constEnd(); ++it)
            affectedTargets.insert(it.value());
        for (auto it = m_extraBalances.constBegin(); it != m_extraBalances.constEnd(); ++it)
        {
            for (const BalanceContribution &contribution : it.value())
                affectedTargets.insert(contribution.code);
        }

        QVector<QJsonObject> output;
        QMap<QString, int> outputIndex;
        const QJsonArray balances = m_root.value(QStringLiteral("GLBalanceList")).toArray();
        for (const QJsonValue &value : balances)
        {
            if (!value.isObject())
                return fail(QStringLiteral("GLBalanceList 中存在不是对象的期初记录"), errorMessage);
            const QJsonObject balance = value.toObject();
            const QString sourceCode = normalizedCode(balance.value(QStringLiteral("AcctCode")));
            const QString targetCode = m_mapping.value(sourceCode, sourceCode);
            const int targetDirection = m_directionsByCode.value(
                targetCode, m_originalDirections.value(sourceCode, 1));
            appendBalance(&output, &outputIndex,
                          convertedBalance(balance, targetCode, targetDirection, true),
                          affectedTargets.contains(targetCode));

            const QList<BalanceContribution> contributions = m_extraBalances.value(sourceCode);
            for (const BalanceContribution &contribution : contributions)
            {
                appendBalance(&output, &outputIndex,
                              convertedBalance(balance, contribution.code,
                                               contribution.direction, false), true);
            }
        }
        // Aggregate in ordinary objects; append each final row only once.
        for (const QJsonObject &balance : output)
            appendRecord(&m_balances, balance);
        return true;
    }

    void appendBalance(QVector<QJsonObject> *output,
                       QMap<QString, int> *outputIndex,
                       const QJsonObject &balance,
                       bool mergeDuplicates)
    {
        if (!mergeDuplicates)
        {
            output->append(balance);
            return;
        }
        const QString identity = balanceIdentity(balance);
        if (!outputIndex->contains(identity))
        {
            outputIndex->insert(identity, output->size());
            output->append(balance);
            return;
        }
        const int index = outputIndex->value(identity);
        QJsonObject combined = output->at(index);
        addBalanceAmounts(&combined, balance);
        output->replace(index, combined);
        ++m_result->mergedBalanceCount;
    }

    bool rewriteVouchers(QString *errorMessage)
    {
        if (!m_root.contains(QStringLiteral("GLVoucherList")))
            return true;
        if (!m_root.value(QStringLiteral("GLVoucherList")).isArray())
            return fail(QStringLiteral("GLVoucherList 不是凭证数组"), errorMessage);

        const QJsonArray vouchers = m_root.value(QStringLiteral("GLVoucherList")).toArray();
        for (int index = 0; index < vouchers.size(); ++index)
        {
            if (!vouchers.at(index).isObject())
                return fail(QStringLiteral("GLVoucherList 中存在不是对象的凭证记录"), errorMessage);
            QJsonObject voucher = vouchers.at(index).toObject();
            const QString sourceCode = normalizedCode(voucher.value(QStringLiteral("AcctCode")));
            const QString targetCode = m_mapping.value(sourceCode, sourceCode);
            if (sourceCode != targetCode)
            {
                checkedInsert(&voucher, QStringLiteral("AcctCode"), targetCode);
                ++m_result->changedVoucherCount;
            }
            appendRecord(&m_vouchers, voucher);
        }
        return true;
    }

    bool assembleResult(QString *errorMessage)
    {
        for (auto it = m_mapping.constBegin(); it != m_mapping.constEnd(); ++it)
        {
            if (!m_directionsByCode.contains(it.value()))
                return fail(QStringLiteral("合并结果校验失败：目标科目 %1 不存在").arg(it.value()), errorMessage);
        }

        // Insert each top-level member once. Replacing arrays on a shared root
        // retains obsolete binary JSON storage across consecutive merges in Qt 5.
        QJsonObject output;
        for (auto it = m_root.constBegin(); it != m_root.constEnd(); ++it)
        {
            QJsonValue value = it.value();
            if (it.key() == QStringLiteral("GLAccountList"))
                value = m_accounts;
            else if (it.key() == QStringLiteral("GLBalanceList"))
                value = m_balances;
            else if (it.key() == QStringLiteral("GLVoucherList"))
                value = m_vouchers;
            checkedInsert(&output, it.key(), value);
        }

        QVector<MutableNode> verifiedNodes;
        if (!parseNodes(output, &verifiedNodes, errorMessage))
            return false;
        QMap<QString, int> indices;
        for (int index = 0; index < verifiedNodes.size(); ++index)
            indices.insert(verifiedNodes.at(index).code, index);
        for (const MutableNode &node : m_nodes)
        {
            if (!node.alive)
                continue;
            const int index = indices.value(node.code, -1);
            if (index < 0)
                return fail(QStringLiteral("合并结果校验失败：科目写入不完整"), errorMessage);
            const MutableNode &verified = verifiedNodes.at(index);
            const QString expectedParent = node.parentIndex < 0 ? QString() : m_nodes.at(node.parentIndex).code;
            const QString actualParent = verified.parentIndex < 0 ? QString() : verifiedNodes.at(verified.parentIndex).code;
            if (actualParent != expectedParent || verified.children.isEmpty() != node.children.isEmpty())
                return fail(QStringLiteral("合并结果校验失败：科目 %1 的层级不一致").arg(node.code), errorMessage);
        }
        m_result->root = output;
        return true;
    }

    const QJsonObject m_root;
    QJsonArray m_accounts;
    QJsonArray m_balances;
    QJsonArray m_vouchers;
    QVector<MutableNode> m_nodes;
    SubjectMergeResult *m_result;
    QMap<QString, int> m_originalIndexByCode;
    QMap<QString, int> m_originalDirections;
    QMap<QString, int> m_directionsByCode;
    QMap<QString, QString> m_mapping;
    QMap<QString, QList<BalanceContribution>> m_extraBalances;
    QSet<QString> m_reservedCodes;
};
}

bool SubjectMergeEngine::buildTree(const QJsonObject &root,
                                   QList<SubjectMergeNode> *output,
                                   QString *errorMessage)
{
    QVector<MutableNode> nodes;
    if (!parseNodes(root, &nodes, errorMessage))
        return false;

    QList<SubjectMergeNode> result;
    for (int index = 0; index < nodes.size(); ++index)
    {
        const MutableNode &node = nodes.at(index);
        SubjectMergeNode item;
        item.code = node.code;
        item.name = node.name;
        item.parentCode = node.parentIndex < 0
            ? QString() : nodes.at(node.parentIndex).code;
        item.direction = node.direction;
        item.level = nodeLevel(nodes, index);
        item.isLeaf = node.children.isEmpty();
        result.append(item);
    }
    std::sort(result.begin(), result.end(),
              [](const SubjectMergeNode &left, const SubjectMergeNode &right) {
        return left.code < right.code;
    });
    if (output)
        *output = result;
    return true;
}

QString SubjectMergeEngine::suggestChildCode(const QList<SubjectMergeNode> &nodes,
                                             const QString &parentCode)
{
    QVector<MutableNode> mutableNodes;
    QMap<QString, int> indexByCode;
    QSet<QString> reservedCodes;
    for (const SubjectMergeNode &node : nodes)
    {
        MutableNode item;
        item.code = node.code;
        item.name = node.name;
        const int index = mutableNodes.size();
        mutableNodes.append(item);
        indexByCode.insert(node.code, index);
        reservedCodes.insert(node.code);
    }
    for (int index = 0; index < nodes.size(); ++index)
    {
        const QString parent = nodes.at(index).parentCode;
        if (!parent.isEmpty() && indexByCode.contains(parent))
        {
            mutableNodes[index].parentIndex = indexByCode.value(parent);
            mutableNodes[indexByCode.value(parent)].children.append(index);
        }
    }
    const int parentIndex = parentCode.isEmpty()
        ? -1 : indexByCode.value(parentCode, -1);
    if (!parentCode.isEmpty() && parentIndex < 0)
        return QString();
    return childCodeForParent(mutableNodes, parentIndex, reservedCodes);
}

bool SubjectMergeEngine::merge(const QJsonObject &root,
                               const SubjectMergeRequest &request,
                               SubjectMergeResult *result,
                               QString *errorMessage)
{
    if (!result)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("未提供科目合并结果对象");
        return false;
    }
    try
    {
        SubjectMergeResult pending;
        QVector<MutableNode> nodes;
        if (!parseNodes(root, &nodes, errorMessage))
            return false;
        MergeState state(root, nodes, &pending);
        if (!state.apply(request, errorMessage))
            return false;
        *result = pending;
        return true;
    }
    catch (const JsonStorageError &)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("合并后的数据超过 JSON 存储限制，本次合并未应用。");
    }
    catch (const std::bad_alloc &)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("内存不足，本次合并未应用，请关闭其他账套后重试。");
    }
    return false;
}
