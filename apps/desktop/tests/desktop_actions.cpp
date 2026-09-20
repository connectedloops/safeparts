#include "clipboard.h"
#include "desktop_window.h"
#include "exact_text_edit.h"
#include "secure_byte_buffer.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFrame>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabBar>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <atomic>
#include <memory>

namespace {
QByteArray exactBytes() {
    constexpr char kExact[] = "\0 leading\nline\xC2\xA0space\xE2\x80\xA8separator\xE2\x80\xA9paragraph\ne\xCC\x81 \xF0\x9F\x98\x80\ntrailing \n";
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

void triggerContextAction(ExactTextEdit *editor, const QString &objectName) {
    bool triggered = false;
    QTimer menuTimer;
    menuTimer.setInterval(5);
    QObject::connect(&menuTimer, &QTimer::timeout, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (menu == nullptr)
            return;
        QAction *action = menu->findChild<QAction *>(objectName);
        if (action != nullptr) {
            triggered = true;
            action->trigger();
            menu->close();
        } else {
            menu->close();
        }
    });
    menuTimer.start();
    QContextMenuEvent event(QContextMenuEvent::Keyboard, QPoint(0, 0), editor->mapToGlobal(QPoint(0, 0)));
    QApplication::sendEvent(editor, &event);
    menuTimer.stop();
    QVERIFY2(triggered, qPrintable(QStringLiteral("missing context action %1").arg(objectName)));
}

void pasteIntoCreate(DesktopWindow &window, const QString &text) {
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
    QApplication::clipboard()->setText(text);
    const ClipboardRead observed = readClipboardUtf8(16 * 1'048'576);
    QCOMPARE(static_cast<int>(observed.status), static_cast<int>(ClipboardRead::Status::Ok));
    auto *editor = required<ExactTextEdit>(&window, "secretInput");
    editor->setFocus();
    QTest::keySequence(editor, QKeySequence::Paste);
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
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "pasteRecoveryButton")->isEnabled(), 10'000);
    QApplication::clipboard()->setText(share);
    QTest::mouseClick(required<QPushButton>(&window, "pasteRecoveryButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "pasteRecoveryButton")->isEnabled(), 10'000);
}

struct WipeObservation final {
    std::atomic<int> count{0};
    std::atomic<int> nonzero{0};
};

class WipeObserverReset final {
public:
    ~WipeObserverReset() { SecureByteBuffer::setWipeObserverForTests({}); }
};
} // namespace

class DesktopActions final : public QObject {
    Q_OBJECT

private slots:
    void create_copy_recover_preserves_exact_utf8_through_user_actions();
    void text_entry_preserves_content_and_bounds_typing_and_input_methods();
    void duplicate_blocks_and_correctable_input_is_preserved();
    void malformed_and_mixed_inputs_block_without_filtering();
    void maximum_valid_workload_remains_bounded_and_resettable();
    void created_shares_keep_source_and_use_compact_native_layout();
    void secure_queued_buffers_wipe_on_final_release();
    void ordinary_create_edits_reject_stale_success_and_error_results();
    void start_over_rejects_stale_success_and_error_results();
    void close_does_not_restore_sensitive_state();
};

void DesktopActions::create_copy_recover_preserves_exact_utf8_through_user_actions() {
    DesktopWindow window;
    window.show();
    pasteIntoCreate(window, exactText());
    QCOMPARE(required<ExactTextEdit>(&window, "secretInput")->exactUtf8(), exactBytes());

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

void DesktopActions::text_entry_preserves_content_and_bounds_typing_and_input_methods() {
    DesktopWindow window;
    window.show();
    const QString admitted = QStringLiteral("first\r\nsecond\rthird 😀");
    const QString normalized = QStringLiteral("first\nsecond\nthird 😀");
    pasteIntoCreate(window, admitted);
    auto *editor = required<ExactTextEdit>(&window, "secretInput");
    QCOMPARE(editor->exactUtf8(), normalized.toUtf8());
    QCOMPARE(editor->toPlainText(), normalized);

    QTest::keyClick(editor, Qt::Key_Backspace);
    QCOMPARE(editor->exactUtf8(), QStringLiteral("first\nsecond\nthird ").toUtf8());
    QCOMPARE(editor->toPlainText(), QStringLiteral("first\nsecond\nthird "));

    QKeyEvent modifiedText(QEvent::KeyPress, Qt::Key_2, Qt::AltModifier, QStringLiteral("@"));
    QApplication::sendEvent(editor, &modifiedText);
    QCOMPARE(editor->exactUtf8(), QStringLiteral("first\nsecond\nthird @").toUtf8());
    QCOMPARE(editor->toPlainText(), QStringLiteral("first\nsecond\nthird @"));

    QTextCursor replacementCursor = editor->textCursor();
    replacementCursor.setPosition(editor->toPlainText().size());
    editor->setTextCursor(replacementCursor);
    QInputMethodEvent replacementOnly;
    replacementOnly.setCommitString(QString(), -1, 1);
    QApplication::sendEvent(editor, &replacementOnly);
    QCOMPARE(editor->exactUtf8(), QStringLiteral("first\nsecond\nthird ").toUtf8());
    QCOMPARE(editor->toPlainText(), QStringLiteral("first\nsecond\nthird "));

    replacementCursor = editor->textCursor();
    replacementCursor.setPosition(0);
    replacementCursor.setPosition(5, QTextCursor::KeepAnchor);
    editor->setTextCursor(replacementCursor);
    QApplication::clipboard()->setText(QStringLiteral("menu\r\npaste"));
    editor->paste();
    QCOMPARE(editor->exactUtf8(), QStringLiteral("menu\npaste\nsecond\nthird ").toUtf8());
    QCOMPARE(editor->toPlainText(), QStringLiteral("menu\npaste\nsecond\nthird "));

    replacementCursor = editor->textCursor();
    replacementCursor.setPosition(0);
    replacementCursor.setPosition(4, QTextCursor::KeepAnchor);
    editor->setTextCursor(replacementCursor);
    triggerContextAction(editor, QStringLiteral("exactCutAction"));
    QCOMPARE(editor->exactUtf8(), QStringLiteral("\npaste\nsecond\nthird ").toUtf8());
    QCOMPARE(editor->toPlainText(), QStringLiteral("\npaste\nsecond\nthird "));
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("menu"));

    required<QToolButton>(&window, "startOverButton")->click();
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
    pasteIntoCreate(window, QStringLiteral("\r\n").repeated(1'048'576));
    QCOMPARE(editor->exactUtf8(), QByteArray(1'048'576, '\n'));
    QCOMPARE(editor->toPlainText(), QString(1'048'576, u'\n'));

    required<QToolButton>(&window, "startOverButton")->click();
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
    const QString maximum(1'048'576, QLatin1Char('x'));
    pasteIntoCreate(window, maximum);
    QCOMPARE(editor->exactUtf8().size(), 1'048'576);
    QTest::keyClicks(editor, QStringLiteral("y"));
    QCOMPARE(editor->exactUtf8(), maximum.toUtf8());
    QVERIFY(required<QLabel>(&window, "createStatus")->text().contains(QStringLiteral("1 MiB")));

    QInputMethodEvent inputMethod;
    inputMethod.setCommitString(QStringLiteral("😀"));
    QApplication::sendEvent(editor, &inputMethod);
    QCOMPARE(editor->exactUtf8(), maximum.toUtf8());
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

void DesktopActions::malformed_and_mixed_inputs_block_without_filtering() {
    DesktopWindow window;
    window.show();
    pasteIntoCreate(window, QStringLiteral("synthetic first UI set"));
    const QString first = createAndCopy(window, 1);

    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    pasteIntoCreate(window, QStringLiteral("synthetic second UI set"));
    const QString second = createAndCopy(window, 1);
    QVERIFY(first != second);

    chooseRecover(window);
    pasteRecovery(window, first + QStringLiteral(" abandon"));
    QVERIFY(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("could not be decoded")));
    QTest::mouseClick(required<QPushButton>(&window, "removeRecoveryButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "pasteRecoveryButton")->isEnabled(), 10'000);

    pasteRecovery(window, first);
    pasteRecovery(window, second);
    QVERIFY(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("compatible set")));
    QVERIFY(!required<QPushButton>(&window, "recoverButton")->isEnabled());
}

void DesktopActions::maximum_valid_workload_remains_bounded_and_resettable() {
    DesktopWindow window;
    window.show();
    required<QSpinBox>(&window, "shareCountInput")->setValue(16);
    pasteIntoCreate(window, QString(1'048'576, QLatin1Char('m')));
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "createdShares")->isVisible(), 30'000);

    chooseRecover(window);
    const QString maximumBatch(16 * 1'048'576, QLatin1Char('x'));
    for (int batch = 1; batch <= 10; ++batch) {
        pasteRecovery(window, maximumBatch);
        QVERIFY(required<QLabel>(&window, "recoveryCount")
                    ->text()
                    .startsWith(QStringLiteral("%1 paste(s)").arg(batch)));
    }
    pasteRecovery(window, maximumBatch);
    QVERIFY(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("Retained recovery input")));
    QVERIFY(!required<QPushButton>(&window, "recoverButton")->isEnabled());

    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "pasteRecoveryButton")->isEnabled(), 10'000);
    QCOMPARE(required<QLabel>(&window, "recoveryCount")->text(), QStringLiteral("No recovery shares pasted"));
}

void DesktopActions::created_shares_keep_source_and_use_compact_native_layout() {
    DesktopWindow window;
    window.resize(620, 480);
    window.show();
    QApplication::processEvents();

    auto *modeSelector = required<QTabBar>(&window, "modeSelector");
    auto *title = required<QLabel>(&window, "createPageTitle");
    auto *createButton = required<QPushButton>(&window, "createButton");
    auto *recoverButton = required<QPushButton>(&window, "recoverButton");
    const int selectorBottom = modeSelector->mapTo(&window, modeSelector->rect().bottomLeft()).y();
    const int titleTop = title->mapTo(&window, title->rect().topLeft()).y();
    QVERIFY2(titleTop - selectorBottom <= 72, "create content is not compactly top-aligned");
    QCOMPARE(createButton->height(), 32);
    QCOMPARE(recoverButton->height(), 32);
    QVERIFY2(!window.styleSheet().contains(QStringLiteral("QPushButton {")),
             "global QPushButton styling overrides the native platform button");

    const QByteArray source("keep this visible");
    pasteIntoCreate(window, QString::fromUtf8(source));
    QTest::mouseClick(createButton, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "createdShares")->isVisible(), 10'000);

    auto *editor = required<ExactTextEdit>(&window, "secretInput");
    QCOMPARE(editor->exactUtf8(), source);
    QVERIFY(editor->isVisible());
    const auto separators = required<QWidget>(&window, "createdShares")
                                ->findChildren<QFrame *>(QStringLiteral("shareSeparator"));
    QCOMPARE(separators.size(), 2);
    for (const auto *separator : separators) {
        QCOMPARE(separator->frameShape(), QFrame::HLine);
        QVERIFY(separator->isVisible());
    }

    QTest::mouseClick(createButton, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&window, "createStatus")->text(),
                              QStringLiteral("Shares created in memory."), 10'000);
    QCOMPARE(required<QWidget>(&window, "createdShares")
                 ->findChildren<QFrame *>(QStringLiteral("shareSeparator"))
                 .size(),
             2);

    editor->moveCursor(QTextCursor::End);
    editor->setFocus();
    QTest::keyClicks(editor, QStringLiteral("!"));
    QCOMPARE(editor->exactUtf8(), source + '!');
    QTRY_VERIFY_WITH_TIMEOUT(!required<QWidget>(&window, "createdShares")->isVisible(), 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(createButton->isEnabled(), 10'000);
}

void DesktopActions::secure_queued_buffers_wipe_on_final_release() {
    const auto observation = std::make_shared<WipeObservation>();
    WipeObserverReset resetObserver;
    SecureByteBuffer::setWipeObserverForTests([observation](QByteArrayView bytes) {
        if (std::any_of(bytes.begin(), bytes.end(), [](char byte) { return byte != '\0'; }))
            observation->nonzero.fetch_add(1, std::memory_order_relaxed);
        observation->count.fetch_add(1, std::memory_order_release);
    });

    SecureByteBuffer first = SecureByteBuffer::take(QByteArray("final-owner-marker"));
    SecureByteBuffer finalOwner = first;
    first = {};
    QCOMPARE(observation->count.load(std::memory_order_acquire), 0);
    finalOwner = {};
    QCOMPARE(observation->count.load(std::memory_order_acquire), 1);

    {
        DesktopWindow window;
        window.show();
        pasteIntoCreate(window, QStringLiteral("queued create input"));
        QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "createdShares")->isVisible(), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(observation->count.load(std::memory_order_acquire) >= 2, 10'000);

        QTest::mouseClick(required<QPushButton>(&window, "copyShare1"), Qt::LeftButton);
        QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(observation->count.load(std::memory_order_acquire) >= 3, 10'000);

        chooseRecover(window);
        pasteRecovery(window, QStringLiteral("malformed handled recovery input"));
        QVERIFY(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("could not be decoded")));
        QTRY_VERIFY_WITH_TIMEOUT(observation->count.load(std::memory_order_acquire) >= 4, 10'000);
    }

    const int beforeClose = observation->count.load(std::memory_order_acquire);
    {
        DesktopWindow closing;
        closing.show();
        pasteIntoCreate(closing, QString(500'000, QLatin1Char('c')));
        QTest::mouseClick(required<QPushButton>(&closing, "createButton"), Qt::LeftButton);
        closing.close();
    }

    QCoreApplication::sendPostedEvents();
    QTRY_VERIFY_WITH_TIMEOUT(observation->count.load(std::memory_order_acquire) > beforeClose, 10'000);
    QCOMPARE(observation->nonzero.load(std::memory_order_acquire), 0);
    QVERIFY(observation->count.load(std::memory_order_acquire) >= 5);
}

void DesktopActions::ordinary_create_edits_reject_stale_success_and_error_results() {
    DesktopWindow window;
    window.show();
    auto *editor = required<ExactTextEdit>(&window, "secretInput");
    pasteIntoCreate(window, QString(500'000, QLatin1Char('s')));
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QKeyEvent staleSuccessEdit(QEvent::KeyPress, Qt::Key_Y, Qt::NoModifier, QStringLiteral("y"));
    QApplication::sendEvent(editor, &staleSuccessEdit);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
    QVERIFY(!required<QWidget>(&window, "createdShares")->isVisible());
    QVERIFY(editor->exactUtf8().endsWith('y'));

    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    pasteIntoCreate(window, QStringLiteral("synthetic stale error"));
    required<QSpinBox>(&window, "thresholdInput")->setValue(4);
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QKeyEvent staleErrorEdit(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier, QStringLiteral("z"));
    QApplication::sendEvent(editor, &staleErrorEdit);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
    QVERIFY(!required<QWidget>(&window, "createdShares")->isVisible());
    QVERIFY(editor->exactUtf8().endsWith('z'));
    QCOMPARE(required<QLabel>(&window, "createStatus")->text(), QStringLiteral("4 of 3 · Words"));
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
