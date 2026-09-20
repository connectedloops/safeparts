#include "clipboard.h"
#include "desktop_window.h"
#include "exact_text_edit.h"

#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabBar>
#include <QTest>
#include <QToolButton>

namespace {
QByteArray exactBytes() {
    constexpr char kExact[] = "\0 leading\nline\xC2\xA0space\xE2\x80\xA8separator\xE2\x80\xA9paragraph\ne\xCC\x81\ntrailing \n";
    return {kExact, static_cast<qsizetype>(sizeof(kExact) - 1)};
}

QString exactText() {
    const QByteArray bytes = exactBytes();
    return QString::fromUtf8(bytes.constData(), bytes.size());
}

template <typename T>
T *required(QObject *root, const char *name) {
    T *value = root->findChild<T *>(QString::fromLatin1(name));
    if (value == nullptr)
        qFatal("required test control is missing");
    return value;
}

void pasteIntoCreate(DesktopWindow &window, const QString &text) {
    QApplication::clipboard()->setText(text);
    const ClipboardRead observed = readClipboardUtf8(1'048'576);
    QCOMPARE(static_cast<int>(observed.status), static_cast<int>(ClipboardRead::Status::Ok));
    auto *editor = required<ExactTextEdit>(&window, "secretInput");
    editor->setFocus();
    QTest::keySequence(editor, QKeySequence::Paste);
    QCOMPARE(editor->exactUtf8(), text.toUtf8());
}

QString createAndCopy(DesktopWindow &window, int index) {
    if (!required<QWidget>(&window, "createdShares")->isVisible())
        QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    if (!QTest::qWaitFor(
            [&window] { return required<QWidget>(&window, "createdShares")->isVisible(); },
            10'000))
        return {};
    auto *copy = required<QPushButton>(&window, qPrintable(QStringLiteral("copyShare%1").arg(index)));
    QApplication::clipboard()->clear();
    QTest::mouseClick(copy, Qt::LeftButton);
    if (!QTest::qWaitFor([] { return !QApplication::clipboard()->text().isEmpty(); }, 10'000))
        return {};
    return QApplication::clipboard()->text();
}

void chooseRecover(DesktopWindow &window) {
    auto *tabs = required<QTabBar>(&window, "modeSelector");
    QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(1).center());
    QTRY_COMPARE(tabs->currentIndex(), 1);
}

void pasteRecovery(DesktopWindow &window, const QString &share) {
    QApplication::clipboard()->setText(share);
    QTest::mouseClick(required<QPushButton>(&window, "pasteRecoveryButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "pasteRecoveryButton")->isEnabled(), 10'000);
}
} // namespace

class DesktopActions final : public QObject {
    Q_OBJECT

private slots:
    void create_copy_recover_preserves_exact_utf8_through_user_actions();
    void duplicate_blocks_and_correctable_input_is_preserved();
    void start_over_rejects_stale_success_and_error_results();
    void close_does_not_restore_sensitive_state();
};

void DesktopActions::create_copy_recover_preserves_exact_utf8_through_user_actions() {
    DesktopWindow window;
    window.show();
    pasteIntoCreate(window, exactText());

    const QString first = createAndCopy(window, 1);
    const QString second = createAndCopy(window, 2);
    QVERIFY(first != second);

    chooseRecover(window);
    pasteRecovery(window, first);
    QVERIFY(!required<QPushButton>(&window, "recoverButton")->isEnabled());
    pasteRecovery(window, second);
    QVERIFY2(required<QPushButton>(&window, "recoverButton")->isEnabled(),
             qPrintable(required<QLabel>(&window, "recoveryStatus")->text()));
    QVERIFY(!required<QWidget>(&window, "recoveryResult")->isVisible());

    QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);
    QApplication::clipboard()->clear();
    QTest::mouseClick(required<QPushButton>(&window, "copyRecoveredButton"), Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(QApplication::clipboard()->text(), exactText(), 10'000);
}

void DesktopActions::duplicate_blocks_and_correctable_input_is_preserved() {
    DesktopWindow window;
    window.show();
    pasteIntoCreate(window, QStringLiteral("synthetic duplicate action"));
    const QString first = createAndCopy(window, 1);
    chooseRecover(window);

    pasteRecovery(window, first);
    pasteRecovery(window, first);
    QVERIFY(!required<QPushButton>(&window, "recoverButton")->isEnabled());
    QVERIFY(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("duplicate")));
    QCOMPARE(required<QLabel>(&window, "recoveryCount")->text().left(10), QStringLiteral("2 paste(s)"));

    QTest::mouseClick(required<QPushButton>(&window, "removeRecoveryButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "pasteRecoveryButton")->isEnabled(), 10'000);
    QCOMPARE(required<QLabel>(&window, "recoveryCount")->text().left(10), QStringLiteral("1 paste(s)"));
}

void DesktopActions::start_over_rejects_stale_success_and_error_results() {
    DesktopWindow window;
    window.show();
    const QString large(500'000, QLatin1Char('x'));
    pasteIntoCreate(window, large);
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    QCOMPARE(required<ExactTextEdit>(&window, "secretInput")->exactUtf8(), QByteArray());
    QTest::qWait(1'500);
    QVERIFY(!required<QWidget>(&window, "createdShares")->isVisible());

    chooseRecover(window);
    QApplication::clipboard()->setText(QString(300'000, QLatin1Char('z')));
    QTest::mouseClick(required<QPushButton>(&window, "pasteRecoveryButton"), Qt::LeftButton);
    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    QCOMPARE(required<QLabel>(&window, "recoveryStatus")->text(), QStringLiteral("Add enough shares to recover."));
    QTest::qWait(1'000);
    QCOMPARE(required<QLabel>(&window, "recoveryStatus")->text(), QStringLiteral("Add enough shares to recover."));
    QVERIFY(!required<QWidget>(&window, "recoveryResult")->isVisible());
}

void DesktopActions::close_does_not_restore_sensitive_state() {
    {
        DesktopWindow first;
        first.show();
        pasteIntoCreate(first, QStringLiteral("synthetic close lifecycle"));
        first.close();
    }
    DesktopWindow reopened;
    reopened.show();
    QCOMPARE(required<ExactTextEdit>(&reopened, "secretInput")->exactUtf8(), QByteArray());
    QVERIFY(!required<QWidget>(&reopened, "recoveryResult")->isVisible());
}

QTEST_MAIN(DesktopActions)
#include "desktop_actions.moc"
