// Include the private dialog and export path without adding production test APIs.
#include "../QtWidgetsApplication1/subjectmergewidget.cpp"
#include <QTimer>
#include <cstdio>
#include <stdexcept>

static void requireMergeUi(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static QPushButton *buttonNamed(QWidget *widget, const QString &text)
{
    for (auto button : widget->findChildren<QPushButton *>())
        if (button->text() == text) return button;
    return nullptr;
}

static void answerDialog(QMessageBox::StandardButton answer)
{
    QTimer::singleShot(0, [answer]() {
        auto box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box || !box->button(answer)) {
            std::fprintf(stderr, "FAIL unexpected confirmation dialog\n");
            std::exit(1);
        }
        box->button(answer)->click();
    });
}

static void selectAccounts(SubjectMergeDialog *dialog, const QStringList &codes, const QString &target)
{
    auto tree = dialog->findChild<QTreeWidget *>();
    QSet<QString> found;
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        const QString code = (*it)->data(0, Qt::UserRole).toString();
        if (codes.contains(code)) {
            (*it)->setCheckState(0, Qt::Checked);
            found.insert(code);
        }
        ++it;
    }
    requireMergeUi(found.size() == codes.size(), "cannot select source accounts");
    for (auto combo : dialog->findChildren<QComboBox *>()) {
        const int index = combo->findData(target);
        if (index >= 0) { combo->setCurrentIndex(index); return; }
    }
    requireMergeUi(false, "target option missing");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try {
        requireMergeUi(argc == 3, "expected ZIP and isolated output directory");
        const QString work = app.arguments().at(2);
        QDir().mkpath(work);
        const auto analysis = analyzeInput(app.arguments().at(1), work + "/read");
        requireMergeUi(analysis.ok, "failed reading actual ZIP");
        SubjectMergeDialog dialog(analysis);
        dialog.setAttribute(Qt::WA_DontShowOnScreen);
        dialog.show();
        QApplication::processEvents();
        requireMergeUi(!buttonNamed(&dialog, QStringLiteral("撤销上一次合并")), "undo button still exists");
        auto merge = buttonNamed(&dialog, QStringLiteral("合并勾选科目"));
        auto exportButton = buttonNamed(&dialog, QStringLiteral("生成合并后的 ZIP"));
        requireMergeUi(merge && exportButton && !exportButton->isEnabled(), "initial export state invalid");
        selectAccounts(&dialog, {"3141", "4104"}, "3141");
        answerDialog(QMessageBox::No);
        merge->click();
        requireMergeUi(dialog.operationCount() == 0 && dialog.mergedRoot() == analysis.root
            && dialog.mapping().isEmpty() && !exportButton->isEnabled(), "cancel changed current data");
        answerDialog(QMessageBox::Yes);
        merge->click();
        requireMergeUi(dialog.operationCount() == 1 && exportButton->isEnabled(), "accepted merge not counted");
        requireMergeUi(dialog.changedVoucherCount() == 5 && dialog.mapping().value("4104") == "3141",
                       "profit merge result incorrect");
        selectAccounts(&dialog, {"4001", "3101"}, "4001");
        answerDialog(QMessageBox::Yes);
        merge->click();
        requireMergeUi(dialog.operationCount() == 2, "consecutive merge counter failed");
        requireMergeUi(!QApplication::overrideCursor(), "wait cursor was not restored");
        QApplication::processEvents();
        requireMergeUi(dialog.grab().save(work + "/subject-merge.png"), "screenshot failed");

        const auto finalized = finalizeOutput(analysis, dialog.mergedRoot(), dialog.mapping(), work + "/output");
        requireMergeUi(finalized.ok, "actual ZIP export failed");
        const auto reopened = analyzeInput(finalized.outputZipPath, work + "/reopen");
        requireMergeUi(reopened.ok && reopened.root == dialog.mergedRoot(), "exported ZIP differs after reopening");
        QFile mapping(finalized.mappingPath);
        requireMergeUi(mapping.open(QIODevice::ReadOnly) && mapping.readAll().contains("4104"), "mapping CSV missing");

        SubjectMergeAnalysis malformed = analysis;
        malformed.root.insert("GLVoucherList", QJsonArray{42});
        SubjectMergeDialog failureDialog(malformed);
        failureDialog.setAttribute(Qt::WA_DontShowOnScreen);
        failureDialog.show();
        selectAccounts(&failureDialog, {"3141", "4104"}, "3141");
        answerDialog(QMessageBox::Ok);
        buttonNamed(&failureDialog, QStringLiteral("合并勾选科目"))->click();
        requireMergeUi(failureDialog.operationCount() == 0 && failureDialog.mergedRoot() == malformed.root
            && !buttonNamed(&failureDialog, QStringLiteral("生成合并后的 ZIP"))->isEnabled(),
            "failed merge changed UI data");
        requireMergeUi(!QApplication::overrideCursor(), "failure left wait cursor active");
        std::puts("PASS UI: no undo, cancel/accept, count, failure rollback, ZIP/CSV export and reopen");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what()); return 1;
    }
}
