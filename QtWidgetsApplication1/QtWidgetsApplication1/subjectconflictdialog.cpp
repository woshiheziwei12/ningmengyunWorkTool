#include "subjectconflictdialog.h"

#include <algorithm>
#include <limits>
#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

SubjectConflictDialog::SubjectConflictDialog(const QList<SubjectConflict> &conflicts,
                                             const QString &asName,
                                             QWidget *parent)
    : QDialog(parent),
      m_conflicts(conflicts),
      m_asName(asName),
      m_pTable(nullptr),
      m_pBtnConfirm(nullptr),
      m_pBtnCancel(nullptr),
      m_pBtnSelectFirst(nullptr),
      m_pBtnSelectLast(nullptr),
      m_pBtnCopy(nullptr),
      m_pBtnCopyAll(nullptr),
      m_pTitleLabel(nullptr)
{
    setupUi();
    fillTable();
}

void SubjectConflictDialog::setupUi()
{
    setWindowTitle(QStringLiteral("\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u5904\u7406"));
    setMinimumSize(900, 480);
    resize(1080, 620);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);

    m_pTitleLabel = new QLabel(this);
    m_pTitleLabel->setWordWrap(true);
    m_pTitleLabel->setText(QStringLiteral(
        "\u8d26\u5957\u300c%1\u300d\u4e2d\u68c0\u6d4b\u5230 %2 \u4e2a\u79d1\u76ee\u7f16\u7801\u51b2\u7a81"
        "\uff08\u7f16\u7801\u76f8\u540c\u4f46\u540d\u79f0\u4e0d\u540c\uff09\uff0c"
        "\u8bf7\u9009\u62e9\u8981\u4fdd\u7559\u7684\u79d1\u76ee\uff0c\u672a\u9009\u4e2d\u7684\u5c06\u88ab\u5220\u9664\uff1a")
        .arg(m_asName)
        .arg(m_conflicts.size()));
    layout->addWidget(m_pTitleLabel);

    // Batch selection buttons
    QHBoxLayout *batchLayout = new QHBoxLayout();
    m_pBtnSelectFirst = new QPushButton(QStringLiteral("\u6279\u91cf\u4fdd\u7559\u6700\u65e9\u5e74\u5ea6"), this);
    m_pBtnSelectFirst->setMinimumHeight(28);
    m_pBtnSelectFirst->setMaximumWidth(150);
    m_pBtnSelectLast = new QPushButton(QStringLiteral("\u6279\u91cf\u4fdd\u7559\u6700\u65b0\u5e74\u5ea6"), this);
    m_pBtnSelectLast->setMinimumHeight(28);
    m_pBtnSelectLast->setMaximumWidth(150);
    m_pBtnCopy = new QPushButton(QStringLiteral("\u590d\u5236\u9009\u4e2d\u51b2\u7a81"), this);
    m_pBtnCopy->setMinimumHeight(28);
    m_pBtnCopy->setMaximumWidth(130);
    m_pBtnCopyAll = new QPushButton(QStringLiteral("\u590d\u5236\u5168\u90e8\u51b2\u7a81"), this);
    m_pBtnCopyAll->setMinimumHeight(28);
    m_pBtnCopyAll->setMaximumWidth(130);
    batchLayout->addWidget(m_pBtnSelectFirst);
    batchLayout->addWidget(m_pBtnSelectLast);
    batchLayout->addStretch();
    batchLayout->addWidget(m_pBtnCopy);
    batchLayout->addWidget(m_pBtnCopyAll);
    layout->addLayout(batchLayout);

    connect(m_pBtnSelectFirst, &QPushButton::clicked, this, &SubjectConflictDialog::onSelectAllFirst);
    connect(m_pBtnSelectLast, &QPushButton::clicked, this, &SubjectConflictDialog::onSelectAllLast);
    connect(m_pBtnCopy, &QPushButton::clicked, this, &SubjectConflictDialog::onCopyClicked);
    connect(m_pBtnCopyAll, &QPushButton::clicked, this, &SubjectConflictDialog::onCopyAllClicked);

    // Table: each row is one name entry for a conflicting code
    m_pTable = new QTableWidget(this);
    m_pTable->setColumnCount(7);
    m_pTable->setHorizontalHeaderLabels({
        QStringLiteral("\u4fdd\u7559"),
        QStringLiteral("\u79d1\u76ee\u7f16\u7801"),
        QStringLiteral("\u79d1\u76ee\u540d\u79f0"),
        QStringLiteral("\u79d1\u76ee\u5b9a\u4e49\u5e74\u5ea6"),
        QStringLiteral("\u51ed\u8bc1\u4f7f\u7528\u5e74\u5ea6"),
        QStringLiteral("\u671f\u521d\u5e74\u5ea6"),
        QStringLiteral("\u4e0b\u7ea7\u79d1\u76ee\u5f15\u7528")
    });
    m_pTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_pTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_pTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_pTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_pTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_pTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_pTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    m_pTable->setColumnWidth(0, 60);
    m_pTable->setColumnWidth(1, 130);
    m_pTable->setColumnWidth(3, 125);
    m_pTable->setColumnWidth(4, 150);
    m_pTable->setColumnWidth(5, 150);
    m_pTable->setColumnWidth(6, 170);
    m_pTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTable->verticalHeader()->setVisible(false);
    m_pTable->verticalHeader()->setDefaultSectionSize(34);
    m_pTable->setAlternatingRowColors(true);
    m_pTable->setShowGrid(true);
    layout->addWidget(m_pTable);

    // Bottom buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_pBtnCancel = new QPushButton(QStringLiteral("\u53d6\u6d88"), this);
    m_pBtnCancel->setMinimumHeight(32);
    m_pBtnCancel->setMaximumWidth(120);
    btnLayout->addWidget(m_pBtnCancel);

    m_pBtnConfirm = new QPushButton(QStringLiteral("\u786e\u8ba4\u9009\u62e9"), this);
    m_pBtnConfirm->setMinimumHeight(32);
    m_pBtnConfirm->setMaximumWidth(160);
    btnLayout->addWidget(m_pBtnConfirm);

    layout->addLayout(btnLayout);

    connect(m_pBtnConfirm, &QPushButton::clicked, this, &SubjectConflictDialog::onConfirmClicked);
    connect(m_pBtnCancel, &QPushButton::clicked, this, &SubjectConflictDialog::onCancelClicked);
}

void SubjectConflictDialog::fillTable()
{
    const auto yearsText = [](QList<int> years, bool unknownYear = false) {
        std::sort(years.begin(), years.end());
        years.erase(std::unique(years.begin(), years.end()), years.end());
        QStringList parts;
        int rangeStart = -1;
        int previous = -1;
        const auto appendRange = [&parts](int start, int end) {
            if (start <= 0)
                return;
            parts.append(start == end
                ? QStringLiteral("%1\u5e74").arg(start)
                : QStringLiteral("%1\u2014%2\u5e74").arg(start).arg(end));
        };
        for (int year : years)
        {
            if (year <= 0)
                continue;
            if (rangeStart < 0)
            {
                rangeStart = previous = year;
            }
            else if (year == previous + 1)
            {
                previous = year;
            }
            else
            {
                appendRange(rangeStart, previous);
                rangeStart = previous = year;
            }
        }
        appendRange(rangeStart, previous);
        if (unknownYear)
            parts.append(QStringLiteral("\u5e74\u4efd\u672a\u77e5"));
        return parts.isEmpty() ? QStringLiteral("\u65e0") : parts.join(QStringLiteral("\u3001"));
    };

    // Total rows = sum of all name entries across all conflicts
    int totalRows = 0;
    for (const SubjectConflict &c : m_conflicts)
        totalRows += c.names.size();

    m_pTable->setRowCount(totalRows);

    int row = 0;
    for (int gi = 0; gi < m_conflicts.size(); ++gi)
    {
        const SubjectConflict &conflict = m_conflicts[gi];
        QButtonGroup *group = new QButtonGroup(this);
        m_buttonGroups.append(group);

        for (int ni = 0; ni < conflict.names.size(); ++ni)
        {
            // Radio button in first column
            QRadioButton *radio = new QRadioButton(m_pTable);
            if (ni == 0)
                radio->setChecked(true);
            group->addButton(radio, ni);

            QWidget *radioContainer = new QWidget(m_pTable);
            QHBoxLayout *radioLayout = new QHBoxLayout(radioContainer);
            radioLayout->setContentsMargins(0, 0, 0, 0);
            radioLayout->setAlignment(Qt::AlignCenter);
            radioLayout->addWidget(radio);
            m_pTable->setCellWidget(row, 0, radioContainer);

            // AcctCode column
            QTableWidgetItem *codeItem = new QTableWidgetItem(conflict.acctCode);
            codeItem->setTextAlignment(Qt::AlignCenter);
            m_pTable->setItem(row, 1, codeItem);

            // AcctName column
            QTableWidgetItem *nameItem = new QTableWidgetItem(conflict.names[ni]);
            m_pTable->setItem(row, 2, nameItem);

            m_pTable->setItem(row, 3, new QTableWidgetItem(
                conflict.accountYearDataAvailable
                    ? yearsText(conflict.accountYearsByName.value(conflict.names[ni]))
                    : QStringLiteral("\u672a\u68c0\u67e5")));
            m_pTable->setItem(row, 4, new QTableWidgetItem(
                conflict.usageDataAvailable
                    ? yearsText(conflict.voucherYears)
                    : QStringLiteral("\u672a\u68c0\u67e5")));
            m_pTable->setItem(row, 5, new QTableWidgetItem(
                conflict.usageDataAvailable
                    ? yearsText(conflict.openingBalanceYears,
                                conflict.hasOpeningBalanceWithUnknownYear)
                    : QStringLiteral("\u672a\u68c0\u67e5")));

            QStringList descendantParts;
            if (!conflict.usageDataAvailable)
            {
                descendantParts.append(QStringLiteral("\u672a\u68c0\u67e5"));
            }
            else if (!conflict.descendantVoucherYears.isEmpty())
            {
                descendantParts.append(QStringLiteral("\u51ed\u8bc1\uff1a%1")
                    .arg(yearsText(conflict.descendantVoucherYears)));
            }
            if (!conflict.descendantOpeningBalanceYears.isEmpty()
                || conflict.hasDescendantOpeningBalanceWithUnknownYear)
            {
                descendantParts.append(QStringLiteral("\u671f\u521d\uff1a%1")
                    .arg(yearsText(conflict.descendantOpeningBalanceYears,
                                   conflict.hasDescendantOpeningBalanceWithUnknownYear)));
            }
            m_pTable->setItem(row, 6, new QTableWidgetItem(
                descendantParts.isEmpty()
                    ? QStringLiteral("\u65e0")
                    : descendantParts.join(QStringLiteral("\uff1b"))));

            ++row;
        }
    }
}

QMap<QString, QString> SubjectConflictDialog::selectedNames() const
{
    QMap<QString, QString> result;
    for (int i = 0; i < m_conflicts.size(); ++i)
    {
        QButtonGroup *group = m_buttonGroups[i];
        int checkedId = group->checkedId();
        if (checkedId >= 0 && checkedId < m_conflicts[i].names.size())
        {
            result.insert(m_conflicts[i].acctCode, m_conflicts[i].names[checkedId]);
        }
    }
    return result;
}

void SubjectConflictDialog::onSelectAllFirst()
{
    for (int i = 0; i < m_buttonGroups.size(); ++i)
    {
        int selectedIndex = 0;
        int selectedYear = std::numeric_limits<int>::max();
        const SubjectConflict &conflict = m_conflicts[i];
        for (int nameIndex = 0; nameIndex < conflict.names.size(); ++nameIndex)
        {
            const QList<int> years = conflict.accountYearsByName.value(conflict.names[nameIndex]);
            for (int year : years)
            {
                if (year > 0 && year < selectedYear)
                {
                    selectedYear = year;
                    selectedIndex = nameIndex;
                }
            }
        }
        QAbstractButton *btn = m_buttonGroups[i]->button(selectedIndex);
        if (btn)
            btn->setChecked(true);
    }
}

void SubjectConflictDialog::onSelectAllLast()
{
    for (int i = 0; i < m_buttonGroups.size(); ++i)
    {
        int selectedIndex = m_conflicts[i].names.size() - 1;
        int selectedYear = std::numeric_limits<int>::min();
        const SubjectConflict &conflict = m_conflicts[i];
        for (int nameIndex = 0; nameIndex < conflict.names.size(); ++nameIndex)
        {
            const QList<int> years = conflict.accountYearsByName.value(conflict.names[nameIndex]);
            for (int year : years)
            {
                if (year > 0 && year > selectedYear)
                {
                    selectedYear = year;
                    selectedIndex = nameIndex;
                }
            }
        }
        QAbstractButton *btn = m_buttonGroups[i]->button(selectedIndex);
        if (btn)
            btn->setChecked(true);
    }
}

QString SubjectConflictDialog::copyText(int conflictIndex) const
{
    const auto yearsText = [](QList<int> years, bool unknownYear = false) {
        std::sort(years.begin(), years.end());
        years.erase(std::unique(years.begin(), years.end()), years.end());
        QStringList parts;
        for (int year : years)
        {
            if (year > 0)
                parts.append(QStringLiteral("%1\u5e74").arg(year));
        }
        if (unknownYear)
            parts.append(QStringLiteral("\u5e74\u4efd\u672a\u77e5"));
        return parts.isEmpty() ? QStringLiteral("\u65e0") : parts.join(QStringLiteral("\u3001"));
    };

    QStringList lines;
    lines.append(QStringLiteral("\u8d26\u5957\uff1a%1").arg(m_asName));
    lines.append(QStringLiteral("\u51b2\u7a81\u79d1\u76ee\u7f16\u7801\u6570\uff1a%1")
        .arg(conflictIndex >= 0 ? 1 : m_conflicts.size()));
    lines.append(QString());

    const int startIndex = conflictIndex >= 0 ? conflictIndex : 0;
    const int endIndex = conflictIndex >= 0 ? conflictIndex + 1 : m_conflicts.size();
    for (int conflictNumber = startIndex;
         conflictNumber < endIndex && conflictNumber < m_conflicts.size();
         ++conflictNumber)
    {
        const SubjectConflict &conflict = m_conflicts[conflictNumber];
        lines.append(QStringLiteral("\u79d1\u76ee\u7f16\u7801\uff1a%1").arg(conflict.acctCode));
        lines.append(QStringLiteral("\u51ed\u8bc1\u4f7f\u7528\u5e74\u5ea6\uff1a%1")
            .arg(conflict.usageDataAvailable
                ? yearsText(conflict.voucherYears)
                : QStringLiteral("\u672a\u68c0\u67e5")));
        lines.append(QStringLiteral("\u671f\u521d\u5e74\u5ea6\uff1a%1")
            .arg(conflict.usageDataAvailable
                ? yearsText(conflict.openingBalanceYears,
                            conflict.hasOpeningBalanceWithUnknownYear)
                : QStringLiteral("\u672a\u68c0\u67e5")));
        if (conflict.usageDataAvailable
            && (!conflict.descendantVoucherYears.isEmpty()
                || !conflict.descendantOpeningBalanceYears.isEmpty()
                || conflict.hasDescendantOpeningBalanceWithUnknownYear))
        {
            lines.append(QStringLiteral("\u4e0b\u7ea7\u79d1\u76ee\u51ed\u8bc1\u5e74\u5ea6\uff1a%1")
                .arg(yearsText(conflict.descendantVoucherYears)));
            lines.append(QStringLiteral("\u4e0b\u7ea7\u79d1\u76ee\u671f\u521d\u5e74\u5ea6\uff1a%1")
                .arg(yearsText(conflict.descendantOpeningBalanceYears,
                               conflict.hasDescendantOpeningBalanceWithUnknownYear)));
        }
        for (int i = 0; i < conflict.names.size(); ++i)
        {
            const QString &name = conflict.names[i];
            lines.append(QStringLiteral("%1. %2\uff08\u79d1\u76ee\u5b9a\u4e49\u5e74\u5ea6\uff1a%3\uff09")
                .arg(i + 1)
                .arg(name)
                .arg(conflict.accountYearDataAvailable
                    ? yearsText(conflict.accountYearsByName.value(name))
                    : QStringLiteral("\u672a\u68c0\u67e5")));
        }
        lines.append(QString());
    }
    return lines.join(QStringLiteral("\n")).trimmed();
}

void SubjectConflictDialog::onCopyClicked()
{
    const int currentRow = m_pTable->currentRow();
    int conflictIndex = -1;
    int firstRow = 0;
    for (int i = 0; i < m_conflicts.size(); ++i)
    {
        const int nextFirstRow = firstRow + m_conflicts[i].names.size();
        if (currentRow >= firstRow && currentRow < nextFirstRow)
        {
            conflictIndex = i;
            break;
        }
        firstRow = nextFirstRow;
    }
    if (conflictIndex < 0)
    {
        m_pBtnCopy->setText(QStringLiteral("\u8bf7\u5148\u9009\u4e2d\u4e00\u884c"));
        return;
    }
    QApplication::clipboard()->setText(copyText(conflictIndex));
    m_pBtnCopy->setText(QStringLiteral("\u5df2\u590d\u5236"));
}

void SubjectConflictDialog::onCopyAllClicked()
{
    QApplication::clipboard()->setText(copyText());
    m_pBtnCopyAll->setText(QStringLiteral("\u5df2\u590d\u5236"));
}

void SubjectConflictDialog::onConfirmClicked()
{
    accept();
}

void SubjectConflictDialog::onCancelClicked()
{
    reject();
}
