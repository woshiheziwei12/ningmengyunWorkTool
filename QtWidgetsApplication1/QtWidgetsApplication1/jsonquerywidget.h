#ifndef JSONQUERYWIDGET_H
#define JSONQUERYWIDGET_H

#include <QJsonDocument>
#include <QList>
#include <QMap>
#include <QString>
#include <QWidget>

class GlobalData;
class QComboBox;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QPoint;
class QPushButton;
class QTableWidget;

struct JsonQueryFileInfo
{
    QString asName;
    QString startDate;
    QString endPeriod;
    QString jsonPath;
    QString status;
    int arrayCount;
    int recordCount;
    bool valid;
};

struct JsonQueryRecord
{
    QString sourceKey;
    QString source;
    QString year;
    QString period;
    QString acctCode;
    QString acctName;
    QString customerCode;
    QString supplierCode;
    QString summary;
    QString debit;
    QString credit;
    QString amount;
    QString rawJson;
    QMap<QString, QString> fields;
};

class JsonQueryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit JsonQueryWidget(GlobalData *globalData, QWidget *parent = nullptr);

    void loadFileList();

signals:
    void backToMain();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onBackClicked();
    void onBrowseClicked();
    void onRefreshClicked();
    void onLoadSelectedClicked();
    void onSearchClicked();
    void onClearClicked();
    void onFileCurrentCellChanged(int currentRow, int currentColumn, int previousRow, int previousColumn);
    void onResultTableContextMenuRequested(const QPoint &pos);
    void onCopyResultSelection();
    void onCopyCurrentRawJson();
    void onCopySelectedRawJson();
    void onFirstPageClicked();
    void onPrevPageClicked();
    void onNextPageClicked();
    void onLastPageClicked();
    void onPageSizeChanged(int index);

private:
    void setupUi();
    void ensureQueryDir() const;
    void startScanFiles(bool autoLoadFirst);
    void startImportFiles(const QStringList &filePaths);
    void startLoadFile(const QString &filePath);
    void startQuery();
    void setFileOperationRunning(bool running, const QString &message = QString());
    void setQueryOperationRunning(bool running, const QString &message = QString());
    void scanJsonFiles();
    void fillFileTable();
    bool parseFileInfo(const QString &filePath, JsonQueryFileInfo &info, QString *errorMessage) const;
    bool loadJsonFile(const QString &filePath, QString *errorMessage);
    void populateDatasetCombo();
    void buildIndexes();
    void runQuery();
    QList<JsonQueryRecord> queryRecords() const;
    JsonQueryRecord makeRecord(const QString &source, const QJsonObject &obj) const;
    bool recordMatches(const JsonQueryRecord &record, const QJsonObject &obj) const;
    void fillResultTable(const QList<JsonQueryRecord> &records);
    void renderCurrentPage();
    void clearResults(const QString &message);
    void updatePageControls();
    int pageCount() const;
    QString resultCellCopyText(int row, int column) const;
    QString selectedResultText(bool includeHiddenRawJson) const;
    bool importLocalFile(const QString &filePath, QString *errorMessage);
    bool extractZipToQueryDir(const QString &zipPath, QString *errorMessage);
    bool copyJsonToQueryDir(const QString &jsonPath, QString *errorMessage);
    QString queryDir() const;
    int selectedFileRow() const;
    QString displayNameForSource(const QString &source) const;
    QString accountNameFor(const QString &year, const QString &acctCode) const;
    QStringList projectCodesByTypeName(const QJsonObject &obj, const QString &year, const QString &acctCode, const QString &typeNamePart) const;

    GlobalData *m_pGlobalData;
    QLabel *m_pDirLabel;
    QLabel *m_pDropLabel;
    QTableWidget *m_pFileTable;
    QComboBox *m_pDatasetCombo;
    QLineEdit *m_pYearEdit;
    QLineEdit *m_pAcctCodeEdit;
    QLineEdit *m_pCustomerCodeEdit;
    QLineEdit *m_pSupplierCodeEdit;
    QLineEdit *m_pKeywordEdit;
    QLabel *m_pResultLabel;
    QTableWidget *m_pResultTable;
    QComboBox *m_pPageSizeCombo;
    QLabel *m_pPageLabel;
    QPushButton *m_pBtnFirstPage;
    QPushButton *m_pBtnPrevPage;
    QPushButton *m_pBtnNextPage;
    QPushButton *m_pBtnLastPage;
    QPushButton *m_pBtnBrowse;
    QPushButton *m_pBtnRefresh;
    QPushButton *m_pBtnLoadSelected;
    QPushButton *m_pBtnSearch;
    QPushButton *m_pBtnClear;
    QPushButton *m_pBtnBack;

    QList<JsonQueryFileInfo> m_fileList;
    QJsonDocument m_currentDoc;
    QString m_currentJsonPath;
    QMap<QString, QString> m_acctNameByCode;
    QMap<QString, QString> m_acctNameByYearAndCode;
    QMap<QString, QStringList> m_acctProjTypesByCode;
    QMap<QString, QStringList> m_acctProjTypesByYearAndCode;
    QMap<QString, QString> m_projectTypeNames;
    QList<JsonQueryRecord> m_currentResultRecords;
    QStringList m_currentResultColumns;
    int m_currentPage;
    int m_pageSize;
    int m_rawJsonColumn;
    bool m_blockFileSelection;
    int m_scanRequestId;
    int m_importRequestId;
    int m_loadRequestId;
    int m_queryRequestId;
};

#endif // JSONQUERYWIDGET_H
