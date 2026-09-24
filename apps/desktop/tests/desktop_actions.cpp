#include "clipboard.h"
#include "desktop_window.h"
#include "exact_text_edit.h"
#include "file_io.h"
#include "rust_worker.h"
#include "secure_byte_buffer.h"
#include "segmented_share_view.h"

#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>

namespace {
QByteArray exactBytes() {
    constexpr char kExact[] = "\0 leading\nline\xC2\xA0space\xE2\x80\xA8separator\xE2\x80\xA9paragraph\ne\xCC\x81 \xF0\x9F\x98\x80\ntrailing \n";
    return {kExact, static_cast<qsizetype>(sizeof(kExact) - 1)};
}

QString exactText() {
    const QByteArray bytes = exactBytes();
    return QString::fromUtf8(bytes.constData(), bytes.size());
}

QString fixtureText(const QString &relativePath) {
    QFile file(QStringLiteral(SAFEPARTS_REPO_ROOT "/") + relativePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

QString mutateBase64Packet(const QString &share, qsizetype offset, const QByteArray &replacement) {
    QByteArray packet = QByteArray::fromBase64(share.trimmed().toLatin1(),
                                               QByteArray::Base64UrlEncoding);
    if (offset < 0 || offset + replacement.size() > packet.size())
        return {};
    std::copy(replacement.cbegin(), replacement.cend(), packet.begin() + offset);
    return QString::fromLatin1(
        packet.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

template <typename T>
T *required(QObject *root, const char *name) {
    T *value = root->findChild<T *>(QString::fromLatin1(name));
    if (value == nullptr)
        qFatal("required test control is missing: %s", name);
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

void replaceExact(ExactTextEdit *editor, const QString &text) {
    editor->clearExact();
    QApplication::clipboard()->setText(text);
    editor->setFocus();
    QTest::keySequence(editor, QKeySequence::Paste);
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
    if (!QTest::qWaitFor([copy] { return copy->isEnabled(); }, 10'000))
        return {};
    const QString expected = required<QPlainTextEdit>(
                                 &window, qPrintable(QStringLiteral("generatedShare%1").arg(index)))
                                 ->toPlainText();
    QApplication::clipboard()->clear();
    QTest::mouseClick(copy, Qt::LeftButton);
    if (!QTest::qWaitFor(
            [&window, index] {
                return required<QLabel>(&window, "createStatus")->text()
                       == QStringLiteral("Share %1 copied.").arg(index);
            },
            10'000))
        return {};
    if (!QTest::qWaitFor([&expected] { return QApplication::clipboard()->text() == expected; },
                         10'000))
        return {};
    return QApplication::clipboard()->text();
}

void chooseRecover(DesktopWindow &window) {
    auto *tabs = required<QTabBar>(&window, "modeSelector");
    QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(1).center());
    QTRY_COMPARE(tabs->currentIndex(), 1);
}

QList<ExactTextEdit *> recoveryEditors(DesktopWindow &window) {
    QList<ExactTextEdit *> editors = required<QWidget>(&window, "recoveryFields")->findChildren<ExactTextEdit *>();
    std::sort(editors.begin(), editors.end(), [](const ExactTextEdit *left, const ExactTextEdit *right) {
        return left->objectName() < right->objectName();
    });
    return editors;
}

void setRecoveryField(DesktopWindow &window, int fieldNumber, const QString &share) {
    auto *editor = required<ExactTextEdit>(&window,
                                           qPrintable(QStringLiteral("recoveryShare%1").arg(fieldNumber)));
    editor->clearExact();
    QApplication::clipboard()->setText(share);
    editor->setFocus();
    QTest::keySequence(editor, QKeySequence::Paste);
    QTRY_VERIFY_WITH_TIMEOUT(!required<QLabel>(&window, "recoveryStatus")
                                  ->text()
                                  .contains(QStringLiteral("Checking")),
                              10'000);
}

void pasteRecovery(DesktopWindow &window, const QString &share) {
    QList<ExactTextEdit *> editors = recoveryEditors(window);
    auto found = std::find_if(editors.begin(), editors.end(), [](const ExactTextEdit *editor) {
        return editor->exactUtf8Size() == 0;
    });
    if (found == editors.end()) {
        QTest::mouseClick(required<QPushButton>(&window, "addRecoveryShareButton"), Qt::LeftButton);
        editors = recoveryEditors(window);
        found = std::prev(editors.end());
    }
    setRecoveryField(window, editors.indexOf(*found) + 1, share);
}

constexpr qsizetype kQueuedCreatePassphraseSize = 37;
constexpr qsizetype kQueuedRecoveryPassphraseSize = 47;
static_assert(kQueuedCreatePassphraseSize != sizeof("queued protected create input") - 1);
static_assert(kQueuedRecoveryPassphraseSize
              != sizeof("synthetic protected desktop interoperability") - 1);

struct WipeObservation final {
    std::atomic<int> count{0};
    std::atomic<int> nonzero{0};
    std::atomic<int> queuedCreatePassphrases{0};
    std::atomic<int> queuedRecoveryPassphrases{0};
};

class WipeObserverReset final {
public:
    ~WipeObserverReset() { SecureByteBuffer::setWipeObserverForTests({}); }
};

struct AccessibilityTextRecord final {
    QAccessible::Event type;
    int position;
    int length;
    int observedCharacterCount;
    QByteArray sha256;
};
struct AccessibilityObservations final {
    QList<AccessibilityTextRecord> text;
    std::atomic<int> cursor{0};
    std::atomic<int> selection{0};
};
AccessibilityObservations accessibilityObservations;
void observeAccessibility(QAccessibleEvent *event) {
    if (event->object() == nullptr || !event->object()->inherits("SegmentedShareView"))
        return;
    switch (event->type()) {
    case QAccessible::TextInserted: {
        const auto *inserted = static_cast<QAccessibleTextInsertEvent *>(event);
        const QByteArray bytes = inserted->textInserted().toLatin1();
        QAccessibleInterface *interface = event->accessibleInterface();
        accessibilityObservations.text.push_back(
            {event->type(), inserted->changePosition(), static_cast<int>(bytes.size()),
             interface != nullptr && interface->textInterface() != nullptr
                 ? interface->textInterface()->characterCount()
                 : -1,
             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)});
        break;
    }
    case QAccessible::TextRemoved: {
        const auto *removed = static_cast<QAccessibleTextRemoveEvent *>(event);
        const QByteArray bytes = removed->textRemoved().toLatin1();
        QAccessibleInterface *interface = event->accessibleInterface();
        accessibilityObservations.text.push_back(
            {event->type(), removed->changePosition(), static_cast<int>(bytes.size()),
             interface != nullptr && interface->textInterface() != nullptr
                 ? interface->textInterface()->characterCount()
                 : -1,
             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)});
        break;
    }
    case QAccessible::TextCaretMoved:
        accessibilityObservations.cursor.fetch_add(1, std::memory_order_relaxed);
        break;
    case QAccessible::TextSelectionChanged:
        accessibilityObservations.selection.fetch_add(1, std::memory_order_relaxed);
        break;
    default:
        break;
    }
}

class OneShotAllocationPolicy final : public DesktopAllocationPolicy {
public:
    void failNext(DesktopAllocationBoundary boundary) { next_ = boundary; }
    bool allow(DesktopAllocationBoundary boundary) override {
        if (next_.has_value() && *next_ == boundary) {
            next_.reset();
            return false;
        }
        return true;
    }

private:
    std::optional<DesktopAllocationBoundary> next_;
};

class AccessibilityObserverReset final {
public:
    AccessibilityObserverReset()
        : previous_(QAccessible::installUpdateHandler(observeAccessibility)) {}
    ~AccessibilityObserverReset() { QAccessible::installUpdateHandler(previous_); }

private:
    QAccessible::UpdateHandler previous_;
};

class FailingWriteDevice final : public FileDevice {
public:
    enum class Failure { Open, Zero, Short, Error, Flush, Close };
    explicit FailingWriteDevice(Failure failure) : failure_(failure) {}
    bool openReadOnly() override { return false; }
    bool openWriteTruncate() override { return failure_ != Failure::Open; }
    qint64 read(char *, qint64) override { return -1; }
    qint64 write(const char *, qint64 size) override {
        if (failure_ == Failure::Zero)
            return 0;
        if (failure_ == Failure::Short)
            return std::max<qint64>(0, size - 1);
        if (failure_ == Failure::Error)
            return -1;
        return size;
    }
    bool flush() override { return failure_ != Failure::Flush; }
    bool close() override { return failure_ != Failure::Close; }

private:
    Failure failure_;
};
} // namespace

class DesktopActions final : public QObject {
    Q_OBJECT

private slots:
    void create_copy_recover_preserves_exact_utf8_through_user_actions();
    void binary_file_create_share_save_load_and_exact_recovery_save();
    void file_dialog_cancellation_and_rejected_imports_preserve_state();
    void exact_file_byte_forms_and_empty_recovery_save();
    void file_export_failures_preserve_share_identity();
    void stale_file_export_never_writes_after_edit();
    void file_workflow_storage_trace();
    void text_entry_preserves_content_and_bounds_typing_and_input_methods();
    void masked_passphrase_admission_and_disclosure_are_bounded();
    void protected_create_and_recovery_require_exact_confirmed_passphrase();
    void passphrase_edits_invalidate_outputs_and_lifecycle_clears_controls();
    void in_flight_passphrase_edits_reject_stale_protected_results();
    void duplicate_blocks_and_correctable_input_is_preserved();
    void malformed_and_mixed_inputs_block_without_filtering();
    void maximum_valid_workload_remains_bounded_and_resettable();
    void web_terms_icons_and_selector_keyboard_match();
    void recovery_fields_add_remove_renumber_and_invalidate();
    void recovery_worker_replaces_the_complete_visible_set_transactionally();
    void all_share_formats_round_trip_with_auto_and_manual_selection();
    void auto_detection_expands_to_the_required_threshold();
    void readiness_uses_distinct_shares_not_empty_placeholders();
    void released_v1_v2_fixtures_inspect_through_qt_auto();
    void protected_fixtures_are_safe_and_interoperate_through_qt();
    void protected_inspection_errors_preserve_passphrase_until_valid_unprotected_replacement();
    void unsupported_version_and_kdf_are_distinct_from_corruption();
    void empty_recovery_field_precedence_survives_encoding_inspection();
    void created_shares_keep_source_and_use_compact_native_layout();
    void generated_shares_show_authoritative_text_and_clear_stale_previews();
    void maximum_words_split_keeps_every_share_exportable();
    void injected_allocation_failures_are_transactional_and_retryable();
    void maximum_policy_recovery_rejects_stale_lifecycle_results();
    void segmented_share_view_is_exact_selectable_accessible_and_bounded();
    void maximum_accessibility_handoff_remains_bounded_and_responsive();
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
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8(), first.toUtf8());
    QVERIFY(!required<QPushButton>(&window, "recoverButton")->isEnabled());
    QCOMPARE(required<QLabel>(&window, "recoveryStatus")->text(),
             QStringLiteral("Share content is required."));
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

void DesktopActions::binary_file_create_share_save_load_and_exact_recovery_save() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray secret("\0\xff" "binary\r\n", 10);
    const QString source = directory.filePath(QStringLiteral("source.bin"));
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    QCOMPARE(sourceFile.write(secret), secret.size());
    sourceFile.close();

    QString openPath = source;
    QStringList openPaths;
    QString savePath;
    int saveDialogCalls = 0;
    DesktopWindow window;
    window.setFileServicesForTests(std::make_shared<FileIo>(), [&] { return openPath; },
                                   [&] { return openPaths; }, [&] {
                                       ++saveDialogCalls;
                                       return savePath;
                                   });
    window.show();
    pasteIntoCreate(window, QStringLiteral("inactive text must clear"));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("chooseSecretFileButton")) != nullptr);
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("secretFileMetadata")) != nullptr);
    QTest::mouseClick(required<QPushButton>(&window, "chooseSecretFileButton"), Qt::LeftButton);
    QVERIFY(required<QLabel>(&window, "secretFileMetadata")->text().contains(QStringLiteral("10 bytes")));
    QCOMPARE(required<ExactTextEdit>(&window, "secretInput")->exactUtf8(), QByteArray());
    required<QCheckBox>(&window, "protectWithPassphrase")->setChecked(true);
    replaceExact(required<ExactTextEdit>(&window, "createPassphrase"),
                 QStringLiteral("synthetic file passphrase"));
    replaceExact(required<ExactTextEdit>(&window, "confirmPassphrase"),
                 QStringLiteral("synthetic file passphrase"));
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QPushButton *>(QStringLiteral("saveShare1")) != nullptr, 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "saveShare1")->isEnabled(), 10'000);

    const QString first = directory.filePath(QStringLiteral("share-1.txt"));
    const QString second = directory.filePath(QStringLiteral("share-2.txt"));
    savePath.clear();
    QTest::mouseClick(required<QPushButton>(&window, "saveShare1"), Qt::LeftButton);
    QCOMPARE(saveDialogCalls, 1);
    QVERIFY(required<QWidget>(&window, "createdResult")->isVisible());

    savePath = first;
    QTest::mouseClick(required<QPushButton>(&window, "saveShare1"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(first), 10'000);
    QFile firstFile(first);
    QVERIFY(firstFile.open(QIODevice::ReadOnly));
    const QByteArray firstBytes = firstFile.readAll();
    QCOMPARE(firstBytes, required<QPlainTextEdit>(&window, "generatedShare1")->toPlainText().toUtf8());
    firstFile.close();
    savePath = second;
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("saveShare2")) != nullptr);
    QTest::mouseClick(required<QPushButton>(&window, "saveShare2"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(second), 10'000);

    chooseRecover(window);
    openPath.clear();
    openPaths = {first, second};
    QTest::mouseClick(required<QPushButton>(&window, "loadRecoveryFilesButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryPassphrasePanel")->isVisible(), 10'000);
    replaceExact(required<ExactTextEdit>(&window, "recoveryPassphrase"),
                 QStringLiteral("wrong file passphrase"));
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")
                                 ->text()
                                 .contains(QStringLiteral("may be incorrect")),
                             10'000);
    replaceExact(required<ExactTextEdit>(&window, "recoveryPassphrase"),
                 QStringLiteral("synthetic file passphrase"));
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);
    QVERIFY(!required<QPlainTextEdit>(&window, "recoveredText")->isVisible());
    QVERIFY(!required<QPushButton>(&window, "copyRecoveredButton")->isVisible());

    const QString recovered = directory.filePath(QStringLiteral("recovered.bin"));
    savePath = recovered;
    const int beforeRecoveredSave = saveDialogCalls;
    QTest::mouseClick(required<QPushButton>(&window, "saveRecoveredButton"), Qt::LeftButton);
    QTest::mouseClick(required<QPushButton>(&window, "saveRecoveredButton"), Qt::LeftButton);
    QCOMPARE(saveDialogCalls, beforeRecoveredSave + 1);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(recovered), 10'000);
    QFile recoveredFile(recovered);
    QVERIFY(recoveredFile.open(QIODevice::ReadOnly));
    QCOMPARE(recoveredFile.readAll(), secret);
}

void DesktopActions::file_dialog_cancellation_and_rejected_imports_preserve_state() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.bin"));
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    QCOMPARE(sourceFile.write("file", 4), qint64(4));
    sourceFile.close();

    QString openPath;
    QStringList openPaths;
    QString savePath;
    DesktopWindow window;
    window.setFileServicesForTests(std::make_shared<FileIo>(), [&] { return openPath; },
                                   [&] { return openPaths; }, [&] { return savePath; });
    window.show();
    pasteIntoCreate(window, QStringLiteral("preserved text"));
    QTest::mouseClick(required<QPushButton>(&window, "chooseSecretFileButton"), Qt::LeftButton);
    QCOMPARE(required<ExactTextEdit>(&window, "secretInput")->exactUtf8(),
             QByteArray("preserved text"));

    const QString first = createAndCopy(window, 1);
    QVERIFY(!first.isEmpty());
    savePath.clear();
    QTest::mouseClick(required<QPushButton>(&window, "saveShare1"), Qt::LeftButton);
    QVERIFY(required<QWidget>(&window, "createdResult")->isVisible());

    const QString emptySecret = directory.filePath(QStringLiteral("empty-secret.bin"));
    QFile emptySecretFile(emptySecret);
    QVERIFY(emptySecretFile.open(QIODevice::WriteOnly));
    emptySecretFile.close();
    openPath = emptySecret;
    QTest::mouseClick(required<QPushButton>(&window, "chooseSecretFileButton"), Qt::LeftButton);
    QVERIFY(required<QLabel>(&window, "createStatus")->text().contains(QStringLiteral("cannot be empty")));
    QVERIFY(required<QWidget>(&window, "createdResult")->isVisible());

    const QString oversized = directory.filePath(QStringLiteral("oversized-secret.bin"));
    QFile oversizedFile(oversized);
    QVERIFY(oversizedFile.open(QIODevice::WriteOnly));
    QCOMPARE(oversizedFile.write(QByteArray(1'048'577, 'x')), qint64(1'048'577));
    oversizedFile.close();
    openPath = oversized;
    QTest::mouseClick(required<QPushButton>(&window, "chooseSecretFileButton"), Qt::LeftButton);
    QVERIFY(required<QLabel>(&window, "createStatus")->text().contains(QStringLiteral("exceeds")));
    QVERIFY(required<QWidget>(&window, "createdResult")->isVisible());

    chooseRecover(window);
    setRecoveryField(window, 1, first);
    const QByteArray retained = required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8();
    openPaths.clear();
    QTest::mouseClick(required<QPushButton>(&window, "loadRecoveryFilesButton"), Qt::LeftButton);
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8(), retained);

    const QString empty = directory.filePath(QStringLiteral("empty.txt"));
    QFile emptyFile(empty);
    QVERIFY(emptyFile.open(QIODevice::WriteOnly));
    emptyFile.close();
    openPaths = {empty};
    QTest::mouseClick(required<QPushButton>(&window, "loadRecoveryFilesButton"), Qt::LeftButton);
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8(), retained);
    QVERIFY(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("cannot be empty")));

    const QString invalid = directory.filePath(QStringLiteral("invalid.txt"));
    QFile invalidFile(invalid);
    QVERIFY(invalidFile.open(QIODevice::WriteOnly));
    QCOMPARE(invalidFile.write("\xff", 1), qint64(1));
    invalidFile.close();
    openPaths = {invalid};
    QTest::mouseClick(required<QPushButton>(&window, "loadRecoveryFilesButton"), Qt::LeftButton);
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8(), retained);
    QVERIFY2(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("valid UTF-8")),
             qPrintable(required<QLabel>(&window, "recoveryStatus")->text()));

    openPaths = {directory.filePath(QStringLiteral("missing.txt"))};
    QTest::mouseClick(required<QPushButton>(&window, "loadRecoveryFilesButton"), Qt::LeftButton);
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8(), retained);
    QVERIFY(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("could not be read")));

    const QString malformed = directory.filePath(QStringLiteral("malformed.txt"));
    QFile malformedFile(malformed);
    QVERIFY(malformedFile.open(QIODevice::WriteOnly));
    QCOMPARE(malformedFile.write("complete but malformed share"), qint64(28));
    malformedFile.close();
    openPaths = {malformed};
    QTest::mouseClick(required<QPushButton>(&window, "loadRecoveryFilesButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")
                                 ->text()
                                 .contains(QStringLiteral("could not be decoded")),
                             10'000);
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8(), retained);
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare2")->exactUtf8(),
             QByteArray("complete but malformed share"));
}

void DesktopActions::exact_file_byte_forms_and_empty_recovery_save() {
    const QList<QByteArray> forms = {
        QByteArray("\xef\xbb\xbf" "BOM\r\nNUL\0tail\n", 17),
        QStringLiteral(" leading NFC é / NFD e\u0301\r\ntrailing \n").toUtf8(),
    };
    for (qsizetype formIndex = 0; formIndex < forms.size(); ++formIndex) {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath(QStringLiteral("source.bin"));
        QFile sourceFile(source);
        QVERIFY(sourceFile.open(QIODevice::WriteOnly));
        QCOMPARE(sourceFile.write(forms.at(formIndex)), forms.at(formIndex).size());
        sourceFile.close();
        QString openPath = source;
        QStringList openPaths;
        QString savePath;
        DesktopWindow window;
        window.setFileServicesForTests(std::make_shared<FileIo>(), [&] { return openPath; },
                                       [&] { return openPaths; }, [&] { return savePath; });
        window.show();
        QTest::mouseClick(required<QPushButton>(&window, "chooseSecretFileButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
        QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "createdResult")->isVisible(), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "saveShare1")->isEnabled(), 10'000);
        const QString first = directory.filePath(QStringLiteral("first.txt"));
        const QString second = directory.filePath(QStringLiteral("second.txt"));
        savePath = first;
        QTest::mouseClick(required<QPushButton>(&window, "saveShare1"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(first), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "saveShare2")->isEnabled(), 10'000);
        savePath = second;
        QTest::mouseClick(required<QPushButton>(&window, "saveShare2"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(second), 10'000);
        chooseRecover(window);
        openPath.clear();
        openPaths = {first, second};
        QTest::mouseClick(required<QPushButton>(&window, "loadRecoveryFilesButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
        QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);
        QVERIFY(required<QPlainTextEdit>(&window, "recoveredText")->isVisible());
        const QString recovered = directory.filePath(QStringLiteral("recovered.bin"));
        savePath = recovered;
        QTest::mouseClick(required<QPushButton>(&window, "saveRecoveredButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(recovered), 10'000);
        QFile recoveredFile(recovered);
        QVERIFY(recoveredFile.open(QIODevice::ReadOnly));
        QCOMPARE(recoveredFile.readAll(), forms.at(formIndex));
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "copyRecoveredButton")->isEnabled(), 10'000);
        QTest::mouseClick(required<QPushButton>(&window, "copyRecoveredButton"), Qt::LeftButton);
        const QByteArray expectedClipboard = formIndex == 0 ? forms.at(formIndex).mid(3)
                                                            : forms.at(formIndex);
        QTRY_COMPARE_WITH_TIMEOUT(QApplication::clipboard()->text().toUtf8(), expectedClipboard,
                                  10'000);
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString emptyShare = directory.filePath(QStringLiteral("empty-share.txt"));
    QFile shareFile(emptyShare);
    QVERIFY(shareFile.open(QIODevice::WriteOnly));
    const QByteArray encoded(
        "U01OMQIAAQEBFH3mxqWB2Xj8LRQg38V3fwAAACCvE0m59fmhpqBATeo23MlJm8slya3BErfMmpPK5B8yYg");
    QCOMPARE(shareFile.write(encoded), encoded.size());
    shareFile.close();
    QStringList openPaths{emptyShare};
    QString savePath;
    DesktopWindow window;
    window.setFileServicesForTests(std::make_shared<FileIo>(), [] { return QString(); },
                                   [&] { return openPaths; }, [&] { return savePath; });
    window.show();
    chooseRecover(window);
    QTest::mouseClick(required<QPushButton>(&window, "loadRecoveryFilesButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);
    const QString emptyOutput = directory.filePath(QStringLiteral("empty-output.bin"));
    savePath = emptyOutput;
    QTest::mouseClick(required<QPushButton>(&window, "saveRecoveredButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(emptyOutput), 10'000);
    QCOMPARE(QFileInfo(emptyOutput).size(), qint64(0));
}

void DesktopActions::file_export_failures_preserve_share_identity() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString savePath = directory.filePath(QStringLiteral("selected-share.txt"));
    DesktopWindow window;
    window.setFileServicesForTests(std::make_shared<FileIo>(), [] { return QString(); },
                                   [] { return QStringList(); }, [&] { return savePath; });
    window.show();
    pasteIntoCreate(window, QStringLiteral("stable export identity"));
    const QString expected = createAndCopy(window, 1);
    QVERIFY(!expected.isEmpty());

    for (const FailingWriteDevice::Failure failure : {
             FailingWriteDevice::Failure::Open, FailingWriteDevice::Failure::Zero,
             FailingWriteDevice::Failure::Short, FailingWriteDevice::Failure::Error,
             FailingWriteDevice::Failure::Flush, FailingWriteDevice::Failure::Close}) {
        auto io = std::make_shared<FileIo>([failure](const QString &) {
            return std::make_unique<FailingWriteDevice>(failure);
        });
        window.setFileServicesForTests(io, [] { return QString(); }, [] { return QStringList(); },
                                       [&] { return savePath; });
        QTest::mouseClick(required<QPushButton>(&window, "saveShare1"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QLabel>(&window, "createStatus")
                                     ->text()
                                     .contains(QStringLiteral("could not be saved")),
                                 10'000);
        QVERIFY(required<QWidget>(&window, "createdResult")->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "copyShare1")->isEnabled(), 10'000);
        QCOMPARE(createAndCopy(window, 1), expected);
    }

    window.setFileServicesForTests(std::make_shared<FileIo>(), [] { return QString(); },
                                   [] { return QStringList(); }, [&] { return savePath; });
    QTest::mouseClick(required<QPushButton>(&window, "saveShare1"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(savePath), 10'000);
    QFile saved(savePath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), expected.toUtf8());
}

void DesktopActions::stale_file_export_never_writes_after_edit() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString savePath = directory.filePath(QStringLiteral("must-not-exist.txt"));
    DesktopWindow window;
    window.setFileServicesForTests(std::make_shared<FileIo>(), [] { return QString(); },
                                   [] { return QStringList(); }, [&] { return savePath; });
    window.show();
    pasteIntoCreate(window, QStringLiteral("stale file export"));
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "createdResult")->isVisible(), 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "saveShare1")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "saveShare1"), Qt::LeftButton);
    auto *secret = required<ExactTextEdit>(&window, "secretInput");
    secret->moveCursor(QTextCursor::End);
    QTest::keyClicks(secret, QStringLiteral("!"));
    QTest::qWait(500);
    QVERIFY(!QFileInfo::exists(savePath));
    QVERIFY(!required<QWidget>(&window, "createdResult")->isVisible());
}

void DesktopActions::file_workflow_storage_trace() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray secret("trace\0binary", 12);
    const QString source = directory.filePath(QStringLiteral("selected-source.bin"));
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    QCOMPARE(sourceFile.write(secret), secret.size());
    sourceFile.close();

    QString openPath = source;
    QStringList openPaths;
    QString savePath;
    DesktopWindow window;
    window.setFileServicesForTests(std::make_shared<FileIo>(), [&] { return openPath; },
                                   [&] { return openPaths; }, [&] { return savePath; });
    window.show();
    QTest::mouseClick(required<QPushButton>(&window, "chooseSecretFileButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "createdResult")->isVisible(), 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "saveShare1")->isEnabled(), 10'000);

    savePath.clear();
    QTest::mouseClick(required<QPushButton>(&window, "saveShare1"), Qt::LeftButton);
    QVERIFY(required<QWidget>(&window, "createdResult")->isVisible());

    savePath = directory.path();
    QTest::mouseClick(required<QPushButton>(&window, "saveShare1"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QLabel>(&window, "createStatus")
                                 ->text()
                                 .contains(QStringLiteral("could not be saved")),
                             10'000);
    QVERIFY(required<QWidget>(&window, "createdResult")->isVisible());

    const QString first = directory.filePath(QStringLiteral("selected-share-1.txt"));
    const QString second = directory.filePath(QStringLiteral("selected-share-2.txt"));
    savePath = first;
    QTest::mouseClick(required<QPushButton>(&window, "saveShare1"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(first), 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "saveShare2")->isEnabled(), 10'000);
    savePath = second;
    QTest::mouseClick(required<QPushButton>(&window, "saveShare2"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(second), 10'000);

    chooseRecover(window);
    openPath.clear();
    openPaths = {first, second};
    QTest::mouseClick(required<QPushButton>(&window, "loadRecoveryFilesButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);
    const QString recovered = directory.filePath(QStringLiteral("selected-recovered.bin"));
    savePath = recovered;
    QTest::mouseClick(required<QPushButton>(&window, "saveRecoveredButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(recovered), 10'000);
    QFile recoveredFile(recovered);
    QVERIFY(recoveredFile.open(QIODevice::ReadOnly));
    QCOMPARE(recoveredFile.readAll(), secret);
}

void DesktopActions::masked_passphrase_admission_and_disclosure_are_bounded() {
    ExactTextEdit editor(nullptr, 1'048'576, true, false);
    editor.show();
    const QString maximum = QStringLiteral(" \r\ne\u0301\r")
                                + QString(1'048'569, QLatin1Char('p'));
    QCOMPARE(maximum.toUtf8().size(), 1'048'576);
    replaceExact(&editor, maximum);
    QCOMPARE(editor.exactUtf8(), maximum.toUtf8());
    QCOMPARE(editor.toPlainText(), QString(maximum.size(), QChar(0x2022)));
    QVERIFY(!editor.toPlainText().contains(QStringLiteral("e")));
    QVERIFY(!editor.isUndoRedoEnabled());

    QSignalSpy rejection(&editor, &ExactTextEdit::inputRejected);
    QTextCursor replacement = editor.textCursor();
    replacement.setPosition(maximum.size() - 1);
    replacement.setPosition(maximum.size(), QTextCursor::KeepAnchor);
    editor.setTextCursor(replacement);
    QApplication::clipboard()->setText(QStringLiteral("xx"));
    QTest::keySequence(&editor, QKeySequence::Paste);
    QCOMPARE(editor.exactUtf8(), maximum.toUtf8());
    QCOMPARE(rejection.count(), 1);
    QCOMPARE(rejection.takeFirst().at(0).toInt(),
             static_cast<int>(ClipboardRead::Status::TooLarge));

    editor.moveCursor(QTextCursor::End);
    QTest::keyClicks(&editor, QStringLiteral("x"));
    QCOMPARE(editor.exactUtf8(), maximum.toUtf8());
    QInputMethodEvent inputMethod;
    inputMethod.setCommitString(QStringLiteral("😀"));
    QApplication::sendEvent(&editor, &inputMethod);
    QCOMPARE(editor.exactUtf8(), maximum.toUtf8());
    QApplication::clipboard()->setText(QStringLiteral("clipboard sentinel"));
    editor.selectAll();
    QTest::keySequence(&editor, QKeySequence::Copy);
    QTest::keySequence(&editor, QKeySequence::Cut);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("clipboard sentinel"));
    QCOMPARE(editor.exactUtf8(), maximum.toUtf8());
    QTest::keySequence(&editor, QKeySequence::Undo);
    QCOMPARE(editor.exactUtf8(), maximum.toUtf8());

    QContextMenuEvent context(QContextMenuEvent::Keyboard, QPoint(), editor.mapToGlobal(QPoint()));
    QApplication::sendEvent(&editor, &context);
    QVERIFY(QApplication::activePopupWidget() == nullptr);
}

void DesktopActions::protected_create_and_recovery_require_exact_confirmed_passphrase() {
    DesktopWindow window;
    window.show();
    pasteIntoCreate(window, QStringLiteral("synthetic protected Qt secret"));
    required<QCheckBox>(&window, "protectWithPassphrase")->setChecked(true);
    replaceExact(required<ExactTextEdit>(&window, "createPassphrase"),
                 QStringLiteral("  cafe\u0301 🔐  "));
    replaceExact(required<ExactTextEdit>(&window, "confirmPassphrase"),
                 QStringLiteral("different"));
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QCOMPARE(required<QLabel>(&window, "createStatus")->text(),
             QStringLiteral("Enter and confirm the same nonempty passphrase."));

    replaceExact(required<ExactTextEdit>(&window, "confirmPassphrase"),
                 QStringLiteral("  cafe\u0301 🔐  "));
    const QString first = createAndCopy(window, 1);
    const QString second = createAndCopy(window, 2);
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    chooseRecover(window);
    pasteRecovery(window, first);
    pasteRecovery(window, second);
    QVERIFY(required<QWidget>(&window, "recoveryPassphrasePanel")->isVisible());
    QVERIFY(!required<QPushButton>(&window, "recoverButton")->isEnabled());
    replaceExact(required<ExactTextEdit>(&window, "recoveryPassphrase"),
                 QStringLiteral("wrong"));
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")
                                 ->text()
                                 .contains(QStringLiteral("may be incorrect")),
                             10'000);
    QVERIFY(!required<QWidget>(&window, "recoveryResult")->isVisible());
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8(), first.toUtf8());

    replaceExact(required<ExactTextEdit>(&window, "recoveryPassphrase"),
                 QStringLiteral("  cafe\u0301 🔐  "));
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);
    QCOMPARE(required<QPlainTextEdit>(&window, "recoveredText")->toPlainText(),
             QStringLiteral("synthetic protected Qt secret"));
}

void DesktopActions::passphrase_edits_invalidate_outputs_and_lifecycle_clears_controls() {
    DesktopWindow window;
    window.show();
    pasteIntoCreate(window, QStringLiteral("passphrase output lifecycle"));
    required<QCheckBox>(&window, "protectWithPassphrase")->setChecked(true);
    replaceExact(required<ExactTextEdit>(&window, "createPassphrase"), QStringLiteral("first"));
    replaceExact(required<ExactTextEdit>(&window, "confirmPassphrase"), QStringLiteral("first"));
    QVERIFY(!createAndCopy(window, 1).isEmpty());
    QTest::keyClicks(required<ExactTextEdit>(&window, "createPassphrase"), QStringLiteral("!"));
    QVERIFY(!required<QWidget>(&window, "createdResult")->isVisible());

    chooseRecover(window);
    const QStringList shares = fixtureText(QStringLiteral(
        "crates/safeparts_core/tests/fixtures/protected_surface_interoperability/desktop/base64url.txt"))
                                   .split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                          Qt::SkipEmptyParts);
    setRecoveryField(window, 1, shares.at(0));
    setRecoveryField(window, 2, shares.at(1));
    replaceExact(required<ExactTextEdit>(&window, "recoveryPassphrase"),
                 QStringLiteral("issue-142 synthetic interoperability passphrase"));
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);
    QTest::keyClicks(required<ExactTextEdit>(&window, "recoveryPassphrase"), QStringLiteral("!"));
    QVERIFY(!required<QWidget>(&window, "recoveryResult")->isVisible());

    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryPassphrase")->exactUtf8(), QByteArray());
    auto *tabs = required<QTabBar>(&window, "modeSelector");
    QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(0).center());
    QCOMPARE(required<ExactTextEdit>(&window, "createPassphrase")->exactUtf8(), QByteArray());
    QCOMPARE(required<ExactTextEdit>(&window, "confirmPassphrase")->exactUtf8(), QByteArray());
    replaceExact(required<ExactTextEdit>(&window, "createPassphrase"), QStringLiteral("close"));
    window.close();
    QCOMPARE(required<ExactTextEdit>(&window, "createPassphrase")->exactUtf8(), QByteArray());
}

void DesktopActions::in_flight_passphrase_edits_reject_stale_protected_results() {
    DesktopWindow createWindow;
    createWindow.show();
    pasteIntoCreate(createWindow, QString(500'000, QLatin1Char('k')));
    required<QCheckBox>(&createWindow, "protectWithPassphrase")->setChecked(true);
    replaceExact(required<ExactTextEdit>(&createWindow, "createPassphrase"), QStringLiteral("argon first"));
    replaceExact(required<ExactTextEdit>(&createWindow, "confirmPassphrase"), QStringLiteral("argon first"));
    QTest::mouseClick(required<QPushButton>(&createWindow, "createButton"), Qt::LeftButton);
    QTest::keyClicks(required<ExactTextEdit>(&createWindow, "createPassphrase"), QStringLiteral("!"));
    QTest::qWait(1'000);
    QVERIFY(!required<QWidget>(&createWindow, "createdResult")->isVisible());
    replaceExact(required<ExactTextEdit>(&createWindow, "confirmPassphrase"),
                 QStringLiteral("argon first!"));
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&createWindow, "createButton")->isEnabled(),
                             30'000);
    QTest::mouseClick(required<QPushButton>(&createWindow, "createButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&createWindow, "createdResult")->isVisible(), 30'000);

    DesktopWindow recoverWindow;
    recoverWindow.show();
    chooseRecover(recoverWindow);
    const QStringList shares = fixtureText(QStringLiteral(
        "crates/safeparts_core/tests/fixtures/protected_surface_interoperability/desktop/base64url.txt"))
                                   .split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                          Qt::SkipEmptyParts);
    setRecoveryField(recoverWindow, 1, shares.at(0));
    setRecoveryField(recoverWindow, 2, shares.at(1));
    auto *passphrase = required<ExactTextEdit>(&recoverWindow, "recoveryPassphrase");
    replaceExact(passphrase, QStringLiteral("wrong passphrase"));
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&recoverWindow, "recoverButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&recoverWindow, "recoverButton"), Qt::LeftButton);
    QTest::keyClicks(passphrase, QStringLiteral("!"));
    QTest::qWait(1'000);
    QVERIFY(!required<QWidget>(&recoverWindow, "recoveryResult")->isVisible());
    QVERIFY(!required<QLabel>(&recoverWindow, "recoveryStatus")->text().contains(
        QStringLiteral("may be incorrect")));
    replaceExact(passphrase, QStringLiteral("issue-142 synthetic interoperability passphrase"));
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&recoverWindow, "recoverButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&recoverWindow, "recoverButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&recoverWindow, "recoveryResult")->isVisible(), 10'000);
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
    const QString second = createAndCopy(window, 2);
    chooseRecover(window);

    pasteRecovery(window, first);
    pasteRecovery(window, first);
    QVERIFY(!required<QPushButton>(&window, "recoverButton")->isEnabled());
    QVERIFY(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("duplicate")));
    QCOMPARE(required<QLabel>(&window, "recoveryCount")->text(),
             QStringLiteral("2 of 2 Recovery shares entered"));

    setRecoveryField(window, 2, second);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
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
    setRecoveryField(window, 1, first + QStringLiteral(" abandon"));
    QVERIFY(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("could not be decoded")));

    setRecoveryField(window, 1, first);
    setRecoveryField(window, 2, second);
    QVERIFY(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("compatible set")));
    QVERIFY(!required<QPushButton>(&window, "recoverButton")->isEnabled());

    const QString fixtureRoot = QStringLiteral(
        "crates/safeparts_core/tests/fixtures/share_compatibility/v2-unprotected/");
    const QString words = fixtureText(fixtureRoot + QStringLiteral("mnemo-words.txt"))
                              .split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                     Qt::SkipEmptyParts)
                              .first();
    const QString base64 = fixtureText(fixtureRoot + QStringLiteral("base64url.txt"))
                               .split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                      Qt::SkipEmptyParts)
                               .first();
    const QString sameFieldMixed = words + QStringLiteral("\n\n") + base64;
    setRecoveryField(window, 1, sameFieldMixed);
    setRecoveryField(window, 2, QString());
    QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")->text(),
                              QStringLiteral("Use one Share format for every Recovery share."),
                              10'000);
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8(),
             sameFieldMixed.toUtf8());
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
    auto *editor = required<ExactTextEdit>(&window, "recoveryShare1");
    const QString largeVisibleShare(1'048'576, QLatin1Char('x'));
    QApplication::clipboard()->setText(largeVisibleShare);
    editor->setFocus();
    QTest::keySequence(editor, QKeySequence::Paste);
    QCOMPARE(editor->exactUtf8Size(), 1'048'576);

    ExactTextEdit boundedEditor(nullptr, 4);
    boundedEditor.show();
    QApplication::clipboard()->setText(QStringLiteral("four"));
    boundedEditor.setFocus();
    QTest::keySequence(&boundedEditor, QKeySequence::Paste);
    QCOMPARE(boundedEditor.exactUtf8(), QByteArray("four"));
    QTest::keyClicks(&boundedEditor, QStringLiteral("x"));
    QCOMPARE(boundedEditor.exactUtf8(), QByteArray("four"));

    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(recoveryEditors(window).size(), 2, 10'000);
    QCOMPARE(required<QLabel>(&window, "recoveryCount")->text(),
             QStringLiteral("0 of 2 Recovery shares entered"));
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8(), QByteArray());
}

void DesktopActions::web_terms_icons_and_selector_keyboard_match() {
    DesktopWindow window;
    window.show();
    auto *selector = required<QTabBar>(&window, "modeSelector");
    QCOMPARE(selector->tabText(0), QStringLiteral("Split"));
    QCOMPARE(selector->tabText(1), QStringLiteral("Combine"));
    QCOMPARE(selector->accessibleName(), QStringLiteral("Choose Split or Combine"));
    selector->setFocus();
    QTest::keyClick(selector, Qt::Key_Right);
    QTRY_COMPARE(selector->currentIndex(), 1);
    QTest::keyClick(selector, Qt::Key_Left);
    QTRY_COMPARE(selector->currentIndex(), 0);

    QCOMPARE(required<QLabel>(&window, "createPageTitle")->text(), QStringLiteral("Split"));
    QCOMPARE(required<QPushButton>(&window, "createButton")->text(), QStringLiteral("Split"));
    QCOMPARE(required<ExactTextEdit>(&window, "secretInput")->accessibleName(), QStringLiteral("Secret"));
    auto *threshold = required<QSpinBox>(&window, "thresholdInput");
    auto *shareCount = required<QSpinBox>(&window, "shareCountInput");
    QCOMPARE(threshold->accessibleName(), QStringLiteral("Minimum shares to recover (k)"));
    QCOMPARE(shareCount->accessibleName(), QStringLiteral("Total shares to create (n)"));
    QCOMPARE(threshold->size(), QSize(112, 32));
    QCOMPARE(shareCount->size(), QSize(112, 32));
    QCOMPARE(threshold->alignment(), Qt::AlignCenter);
    QCOMPARE(shareCount->alignment(), Qt::AlignCenter);
    QCOMPARE(threshold->geometry().top(), shareCount->geometry().top());

    auto *additionalOptions = required<QToolButton>(&window, "additionalOptionsButton");
    auto *additionalPanel = required<QWidget>(&window, "additionalOptionsPanel");
    QCOMPARE(additionalOptions->text(), QStringLiteral("Advanced"));
    QCOMPARE(additionalOptions->accessibleName(), QStringLiteral("Advanced options"));
    const QSize disclosureSize(112, 30);
    QCOMPARE(additionalOptions->size(), disclosureSize);
    QVERIFY(additionalOptions->isCheckable());
    QVERIFY(!additionalOptions->autoRaise());
    QVERIFY(!additionalOptions->isChecked());
    QCOMPARE(additionalOptions->arrowType(), Qt::RightArrow);
    QVERIFY(additionalPanel->isHidden());
    additionalOptions->setFocus();
    QCOMPARE(additionalOptions->size(), disclosureSize);
    QTest::keyClick(additionalOptions, Qt::Key_Space);
    QVERIFY(additionalOptions->isChecked());
    QCOMPARE(additionalOptions->arrowType(), Qt::DownArrow);
    QCOMPARE(additionalOptions->size(), disclosureSize);
    QVERIFY(!additionalPanel->isHidden());
    additionalOptions->clearFocus();
    QCOMPARE(additionalOptions->size(), disclosureSize);
    additionalOptions->setFocus();
    QTest::keyClick(additionalOptions, Qt::Key_Space);
    QVERIFY(!additionalOptions->isChecked());
    QCOMPARE(additionalOptions->arrowType(), Qt::RightArrow);
    QCOMPARE(additionalOptions->size(), disclosureSize);
    QVERIFY(additionalPanel->isHidden());

    const auto labels = window.findChildren<QLabel *>();
    const auto hasLabel = [&labels](const QString &text) {
        return std::any_of(labels.begin(), labels.end(), [&text](const QLabel *item) {
            return item->text() == text;
        });
    };
    QVERIFY(hasLabel(QStringLiteral("Secret")));
    QVERIFY(hasLabel(QStringLiteral("Share format")));
    QVERIFY(!hasLabel(QStringLiteral("Enter text exactly as you want to recover it.")));
    QVERIFY(!hasLabel(QStringLiteral("Copy each complete share and store them separately.")));
    QVERIFY(!hasLabel(QStringLiteral("Paste complete, distinct Words shares. Recovery starts only when you choose Recover.")));

    pasteIntoCreate(window, QStringLiteral("icon test"));
    const QString copied = createAndCopy(window, 1);
    QVERIFY(!copied.isEmpty());
    auto *copyShare = required<QPushButton>(&window, "copyShare1");
    QVERIFY(copyShare->text().isEmpty());
    QVERIFY(!copyShare->icon().isNull());
    QCOMPARE(copyShare->accessibleName(), QStringLiteral("Copy Recovery share 1"));

    chooseRecover(window);
    QCOMPARE(required<QLabel>(&window, "recoverPageTitle")->text(), QStringLiteral("Combine"));
    QCOMPARE(required<QPushButton>(&window, "recoverButton")->text(), QStringLiteral("Combine"));
    auto *copyRecovered = required<QPushButton>(&window, "copyRecoveredButton");
    QVERIFY(copyRecovered->text().isEmpty());
    QVERIFY(!copyRecovered->icon().isNull());
    QCOMPARE(copyRecovered->accessibleName(), QStringLiteral("Copy recovered Secret"));
}

void DesktopActions::recovery_fields_add_remove_renumber_and_invalidate() {
    DesktopWindow window;
    window.show();
    pasteIntoCreate(window, QStringLiteral("editable recovery fields"));
    const QString first = createAndCopy(window, 1);
    const QString second = createAndCopy(window, 2);
    chooseRecover(window);

    QCOMPARE(recoveryEditors(window).size(), 2);
    QVERIFY(!required<QPushButton>(&window, "removeRecoveryShare1")->isEnabled());
    QVERIFY(!required<QPushButton>(&window, "removeRecoveryShare2")->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "addRecoveryShareButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "addRecoveryShareButton"), Qt::LeftButton);
    QCOMPARE(recoveryEditors(window).size(), 3);
    QVERIFY(required<QPushButton>(&window, "removeRecoveryShare1")->isEnabled());
    QCOMPARE(required<QLabel>(&window, "recoveryShareLabel3")->text(),
             QStringLiteral("Recovery share 3"));

    setRecoveryField(window, 3, QStringLiteral("third field marker\r\nline"));
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare3")->exactUtf8(),
             QStringLiteral("third field marker\nline").toUtf8());
    QTest::mouseClick(required<QPushButton>(&window, "removeRecoveryShare2"), Qt::LeftButton);
    QCOMPARE(recoveryEditors(window).size(), 2);
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare2")->exactUtf8(),
             QStringLiteral("third field marker\nline").toUtf8());
    QVERIFY(!required<QPushButton>(&window, "removeRecoveryShare1")->isEnabled());

    setRecoveryField(window, 1, first);
    setRecoveryField(window, 2, second);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);

    auto *firstEditor = required<ExactTextEdit>(&window, "recoveryShare1");
    firstEditor->moveCursor(QTextCursor::End);
    firstEditor->setFocus();
    QTest::keyClicks(firstEditor, QStringLiteral(" abandon"));
    QVERIFY(!required<QWidget>(&window, "recoveryResult")->isVisible());
    QVERIFY(!required<QPushButton>(&window, "recoverButton")->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")
                                 ->text()
                                 .contains(QStringLiteral("could not be decoded")),
                             10'000);

    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(recoveryEditors(window).size(), 2, 10'000);
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8(), QByteArray());
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare2")->exactUtf8(), QByteArray());
}

void DesktopActions::recovery_worker_replaces_the_complete_visible_set_transactionally() {
    DesktopWindow window;
    window.show();
    pasteIntoCreate(window, QStringLiteral("complete visible set worker boundary"));
    const QString first = createAndCopy(window, 1);
    const QString second = createAndCopy(window, 2);
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    const auto wipeObservation = std::make_shared<WipeObservation>();
    WipeObserverReset resetObserver;
    SecureByteBuffer::setWipeObserverForTests([wipeObservation](QByteArrayView bytes) {
        if (std::any_of(bytes.begin(), bytes.end(), [](char byte) { return byte != '\0'; }))
            wipeObservation->nonzero.fetch_add(1, std::memory_order_relaxed);
        wipeObservation->count.fetch_add(1, std::memory_order_release);
    });

    RustWorker worker;
    QSignalSpy operationSpy(&worker, &RustWorker::operationFinished);

    QList<SecureByteBuffer> emptyInput;
    emptyInput.append(SecureByteBuffer{});
    worker.replaceRecovery(76, std::move(emptyInput), 3);
    QCOMPARE(operationSpy.count(), 1);
    const QList<QVariant> emptyResult = operationSpy.takeFirst();
    QCOMPARE(emptyResult.at(0).toULongLong(), 76ULL);
    QCOMPARE(emptyResult.at(1).toInt(),
             static_cast<int>(static_cast<std::uint8_t>(Status::MalformedInput)));
    QCOMPARE(emptyResult.at(5).toUInt(), 0U);

    QList<SecureByteBuffer> inputs;
    inputs.append(SecureByteBuffer::take(first.toUtf8()));
    inputs.append(SecureByteBuffer::take(QByteArray("invalid middle Recovery share")));
    inputs.append(SecureByteBuffer::take(second.toUtf8()));

    worker.replaceRecovery(77, std::move(inputs), 3);

    QCOMPARE(operationSpy.count(), 1);
    const QList<QVariant> result = operationSpy.takeFirst();
    QCOMPARE(result.at(0).toULongLong(), 77ULL);
    QCOMPARE(result.at(1).toInt(), static_cast<int>(static_cast<std::uint8_t>(Status::MalformedInput)));
    QCOMPARE(result.at(5).toUInt(), 0U);
    QVERIFY(!result.at(6).toBool());

    QList<SecureByteBuffer> corrected;
    corrected.append(SecureByteBuffer::take(first.toUtf8()));
    corrected.append(SecureByteBuffer::take(second.toUtf8()));
    worker.replaceRecovery(78, std::move(corrected), 3);
    QCOMPARE(operationSpy.count(), 1);
    const QList<QVariant> accepted = operationSpy.takeFirst();
    QCOMPARE(accepted.at(0).toULongLong(), 78ULL);
    QCOMPARE(accepted.at(1).toInt(), static_cast<int>(static_cast<std::uint8_t>(Status::Ok)));
    QCOMPARE(accepted.at(5).toUInt(), 2U);
    QVERIFY(accepted.at(6).toBool());
    QVERIFY(wipeObservation->count.load(std::memory_order_acquire) >= 5);
    QCOMPARE(wipeObservation->nonzero.load(std::memory_order_acquire), 0);
}

void DesktopActions::all_share_formats_round_trip_with_auto_and_manual_selection() {
    const QList<QPair<int, QString>> formats = {
        {1, QStringLiteral("Base64url")},
        {2, QStringLiteral("Base58check")},
        {3, QStringLiteral("Words")},
        {4, QStringLiteral("BIP-39")},
    };
    for (const auto &[encoding, name] : formats) {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString savePath;
        const QString secret = QStringLiteral("synthetic %1 desktop round trip").arg(name);
        const QString sourcePath = directory.filePath(QStringLiteral("source.bin"));
        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(secret.toUtf8()), secret.toUtf8().size());
        source.close();
        DesktopWindow window;
        window.setFileServicesForTests(std::make_shared<FileIo>(), [&] { return sourcePath; },
                                       [] { return QStringList(); }, [&] { return savePath; });
        window.show();
        auto *createEncoding = required<QComboBox>(&window, "createEncoding");
        createEncoding->setCurrentIndex(createEncoding->findData(encoding));
        QTest::mouseClick(required<QPushButton>(&window, "chooseSecretFileButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
        const QString first = createAndCopy(window, 1);
        const QString second = createAndCopy(window, 2);
        QVERIFY(!first.isEmpty());
        QVERIFY(!second.isEmpty());
        QVERIFY(first != second);

        const QString savedFirst = directory.filePath(QStringLiteral("first.txt"));
        const QString repeatedFirst = directory.filePath(QStringLiteral("first-again.txt"));
        savePath = savedFirst;
        QTest::mouseClick(required<QPushButton>(&window, "saveShare1"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(savedFirst), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "saveShare1")->isEnabled(), 10'000);
        savePath = repeatedFirst;
        QTest::mouseClick(required<QPushButton>(&window, "saveShare1"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(repeatedFirst), 10'000);
        QFile saved(savedFirst);
        QFile repeated(repeatedFirst);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(repeated.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), first.toUtf8());
        QCOMPARE(repeated.readAll(), first.toUtf8());
        QCOMPARE(createAndCopy(window, 1), first);

        chooseRecover(window);
        setRecoveryField(window, 1, first);
        QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&window, "detectedRecoveryEncoding")->text(),
                                  QStringLiteral("Detected: %1").arg(name), 10'000);
        auto *manual = required<QComboBox>(&window, "recoveryEncoding");
        manual->setCurrentIndex(manual->findData(encoding));
        QTRY_VERIFY_WITH_TIMEOUT(!required<QLabel>(&window, "recoveryStatus")
                                      ->text()
                                      .contains(QStringLiteral("Checking")),
                                  10'000);
        setRecoveryField(window, 2, second);
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(),
                                 10'000);
        QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);
        QCOMPARE(required<QPlainTextEdit>(&window, "recoveredText")->toPlainText(), secret);
    }
}

void DesktopActions::auto_detection_expands_to_the_required_threshold() {
    DesktopWindow window;
    window.show();
    required<QSpinBox>(&window, "thresholdInput")->setValue(3);
    pasteIntoCreate(window, QStringLiteral("synthetic threshold detection"));
    const QString first = createAndCopy(window, 1);
    chooseRecover(window);
    setRecoveryField(window, 1, first);
    QTRY_COMPARE_WITH_TIMEOUT(recoveryEditors(window).size(), 3, 10'000);
    QCOMPARE(required<QLabel>(&window, "detectedRecoveryEncoding")->text(),
             QStringLiteral("Detected: Words"));
    QVERIFY(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("1 of 3"))
            || required<QLabel>(&window, "recoveryStatus")->text()
                   .contains(QStringLiteral("Share content")));
}

void DesktopActions::readiness_uses_distinct_shares_not_empty_placeholders() {
    const auto waitUntilReady = [](DesktopWindow &window) {
        return QTest::qWaitFor(
            [&window] { return required<QPushButton>(&window, "recoverButton")->isEnabled(); },
            10'000);
    };

    {
        DesktopWindow window;
        window.show();
        required<QSpinBox>(&window, "thresholdInput")->setValue(1);
        pasteIntoCreate(window, QStringLiteral("one of three readiness"));
        const QString share = createAndCopy(window, 2);
        chooseRecover(window);
        setRecoveryField(window, 1, share);
        QVERIFY2(waitUntilReady(window),
                 qPrintable(required<QLabel>(&window, "recoveryStatus")->text()));
        QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare2")->exactUtf8(), QByteArray());
        QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(),
                                 10'000);
    }

    {
        DesktopWindow window;
        window.show();
        required<QSpinBox>(&window, "shareCountInput")->setValue(5);
        pasteIntoCreate(window, QStringLiteral("reordered non-leading subset"));
        QString third = createAndCopy(window, 3);
        QString fifth = createAndCopy(window, 5);
        const qsizetype thirdWrap = third.indexOf(QLatin1Char(' '), third.size() / 2);
        const qsizetype fifthWrap = fifth.indexOf(QLatin1Char(' '), fifth.size() / 2);
        QVERIFY(thirdWrap > 0);
        QVERIFY(fifthWrap > 0);
        third.replace(thirdWrap, 1, QStringLiteral("\r\n"));
        fifth.replace(fifthWrap, 1, QStringLiteral("\r\n"));
        chooseRecover(window);
        setRecoveryField(window, 1, fifth + QStringLiteral("\r\n\r\n") + third);
        QVERIFY2(waitUntilReady(window),
                 qPrintable(required<QLabel>(&window, "recoveryStatus")->text()));
        QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare2")->exactUtf8(), QByteArray());
        QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(),
                                 10'000);
        QCOMPARE(required<QPlainTextEdit>(&window, "recoveredText")->toPlainText(),
                 QStringLiteral("reordered non-leading subset"));
    }
}

void DesktopActions::released_v1_v2_fixtures_inspect_through_qt_auto() {
    const QList<QPair<QString, QString>> fixtures = {
        {QStringLiteral("v1-unprotected/base64url.txt"), QStringLiteral("Base64url")},
        {QStringLiteral("v1-unprotected/base58check.txt"), QStringLiteral("Base58check")},
        {QStringLiteral("v1-unprotected/mnemo-words.txt"), QStringLiteral("Words")},
        {QStringLiteral("v1-unprotected/mnemo-bip39.txt"), QStringLiteral("BIP-39")},
        {QStringLiteral("v2-unprotected/base64url.txt"), QStringLiteral("Base64url")},
        {QStringLiteral("v2-unprotected/base58check.txt"), QStringLiteral("Base58check")},
        {QStringLiteral("v2-unprotected/mnemo-words.txt"), QStringLiteral("Words")},
        {QStringLiteral("v2-unprotected/mnemo-bip39.txt"), QStringLiteral("BIP-39")},
    };
    const QString fixtureRoot = QStringLiteral(
        "crates/safeparts_core/tests/fixtures/share_compatibility/");
    for (const auto &[relativePath, format] : fixtures) {
        const QStringList shares = fixtureText(fixtureRoot + relativePath)
                                       .split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                              Qt::SkipEmptyParts);
        QCOMPARE(shares.size(), 3);
        DesktopWindow window;
        window.show();
        chooseRecover(window);
        setRecoveryField(window, 1, shares.at(0));
        setRecoveryField(window, 2, shares.at(1));
        QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&window, "detectedRecoveryEncoding")->text(),
                                  QStringLiteral("Detected: %1").arg(format), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(),
                                 10'000);
        QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")
                                     ->text()
                                     .contains(QStringLiteral("binary Secret")),
                                 10'000);
        QVERIFY(required<QWidget>(&window, "recoveryResult")->isVisible());
        QVERIFY(!required<QPlainTextEdit>(&window, "recoveredText")->isVisible());
        QVERIFY(required<QPushButton>(&window, "saveRecoveredButton")->isVisible());
    }
}

void DesktopActions::protected_fixtures_are_safe_and_interoperate_through_qt() {
    struct FixtureCase { QString file; int encoding; QString expected; };
    const QList<FixtureCase> released = {
        {QStringLiteral("base64url.txt"), 1, {}}, {QStringLiteral("base58check.txt"), 2, {}},
        {QStringLiteral("mnemo-words.txt"), 3, {}}, {QStringLiteral("mnemo-bip39.txt"), 4, {}}};
    for (const auto &item : released) {
        for (const int requested : {0, item.encoding}) {
            DesktopWindow window; window.show(); chooseRecover(window);
            required<QComboBox>(&window, "recoveryEncoding")->setCurrentIndex(
                required<QComboBox>(&window, "recoveryEncoding")->findData(requested));
            const QStringList shares = fixtureText(QStringLiteral("crates/safeparts_core/tests/fixtures/share_compatibility/v2-passphrase-protected/") + item.file)
                                           .split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
            setRecoveryField(window, 1, shares.at(0)); setRecoveryField(window, 2, shares.at(1));
            QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryPassphrasePanel")->isVisible(), 10'000);
            replaceExact(required<ExactTextEdit>(&window, "recoveryPassphrase"), QStringLiteral("issue-60 synthetic fixture passphrase"));
            QApplication::clipboard()->setText(QStringLiteral("sentinel"));
            QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
            QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("binary Secret")), 10'000);
            QVERIFY(required<QWidget>(&window, "recoveryResult")->isVisible());
            QVERIFY(!required<QPlainTextEdit>(&window, "recoveredText")->isVisible());
            QVERIFY(required<QPushButton>(&window, "saveRecoveredButton")->isVisible());
            QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("sentinel"));
            QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8(), shares.at(0).toUtf8());
            QCOMPARE(required<ExactTextEdit>(&window, "recoveryPassphrase")->exactUtf8(), QByteArray("issue-60 synthetic fixture passphrase"));
        }
    }

    const QList<FixtureCase> desktop = {
        {QStringLiteral("base64url.txt"), 1, QStringLiteral("synthetic protected desktop interoperability")},
        {QStringLiteral("base58check.txt"), 2, QStringLiteral("synthetic protected desktop interoperability")},
        {QStringLiteral("mnemo-words.txt"), 3, QStringLiteral("synthetic protected desktop interoperability")},
        {QStringLiteral("mnemo-bip39.txt"), 4, QStringLiteral("synthetic protected desktop interoperability")}};
    for (const auto &item : desktop) {
        for (const int requested : {0, item.encoding}) {
            DesktopWindow window; window.show(); chooseRecover(window);
            required<QComboBox>(&window, "recoveryEncoding")->setCurrentIndex(
                required<QComboBox>(&window, "recoveryEncoding")->findData(requested));
            const QStringList shares = fixtureText(QStringLiteral("crates/safeparts_core/tests/fixtures/protected_surface_interoperability/desktop/") + item.file)
                                           .split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
            setRecoveryField(window, 1, shares.at(0)); setRecoveryField(window, 2, shares.at(1));
            replaceExact(required<ExactTextEdit>(&window, "recoveryPassphrase"), QStringLiteral("issue-142 synthetic interoperability passphrase"));
            QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
            QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);
            QCOMPARE(required<QPlainTextEdit>(&window, "recoveredText")->toPlainText(), item.expected);
        }
    }
}

void DesktopActions::protected_inspection_errors_preserve_passphrase_until_valid_unprotected_replacement() {
    const QString protectedText = fixtureText(QStringLiteral(
        "crates/safeparts_core/tests/fixtures/protected_surface_interoperability/desktop/base64url.txt"));
    const QStringList protectedShares = protectedText.split(
        QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    QCOMPARE(protectedShares.size(), 3);

    DesktopWindow window;
    window.show();
    chooseRecover(window);
    setRecoveryField(window, 1, protectedShares.at(0));
    setRecoveryField(window, 2, protectedShares.at(1));
    auto *passphrase = required<ExactTextEdit>(&window, "recoveryPassphrase");
    replaceExact(passphrase, QStringLiteral("issue-142 synthetic interoperability passphrase"));
    const QByteArray expectedPassphrase = passphrase->exactUtf8();

    const QString malformed = protectedShares.at(0) + QStringLiteral("!");
    setRecoveryField(window, 1, malformed);
    QVERIFY(required<QWidget>(&window, "recoveryPassphrasePanel")->isVisible());
    QCOMPARE(passphrase->exactUtf8(), expectedPassphrase);
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8(), malformed.toUtf8());
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare2")->exactUtf8(),
             protectedShares.at(1).toUtf8());

    setRecoveryField(window, 1, protectedShares.at(0));
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);

    const QString unprotectedText = fixtureText(QStringLiteral(
        "crates/safeparts_core/tests/fixtures/surface_interoperability/cli/base64url.txt"));
    const QStringList unprotectedShares = unprotectedText.split(
        QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    setRecoveryField(window, 1, unprotectedShares.at(0));
    setRecoveryField(window, 2, unprotectedShares.at(1));
    QTRY_VERIFY_WITH_TIMEOUT(!required<QWidget>(&window, "recoveryPassphrasePanel")->isVisible(),
                             10'000);
    QCOMPARE(passphrase->exactUtf8(), QByteArray());
}

void DesktopActions::unsupported_version_and_kdf_are_distinct_from_corruption() {
    const QString fixtureRoot = QStringLiteral(
        "crates/safeparts_core/tests/fixtures/share_compatibility/");
    const QString versionSource = fixtureText(fixtureRoot
        + QStringLiteral("v2-unprotected/base64url.txt"))
                                      .split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                             Qt::SkipEmptyParts)
                                      .first();
    const QString unsupportedVersion = mutateBase64Packet(versionSource, 4, QByteArray(1, char(99)));
    QVERIFY(!unsupportedVersion.isEmpty());

    DesktopWindow versionWindow;
    versionWindow.show();
    chooseRecover(versionWindow);
    setRecoveryField(versionWindow, 1, unsupportedVersion);
    QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&versionWindow, "recoveryStatus")->text(),
                              QStringLiteral("This Recovery share packet version is not supported."),
                              10'000);
    QCOMPARE(required<ExactTextEdit>(&versionWindow, "recoveryShare1")->exactUtf8(),
             unsupportedVersion.toUtf8());
    QVERIFY(!required<QPushButton>(&versionWindow, "recoverButton")->isEnabled());

    const QString protectedSource = fixtureText(fixtureRoot
        + QStringLiteral("v2-passphrase-protected/base64url.txt"))
                                        .split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                               Qt::SkipEmptyParts)
                                        .first();
    const QString unsupportedKdf = mutateBase64Packet(
        protectedSource, 53, QByteArray::fromHex("ffffffff"));
    QVERIFY(!unsupportedKdf.isEmpty());

    DesktopWindow kdfWindow;
    kdfWindow.show();
    chooseRecover(kdfWindow);
    setRecoveryField(kdfWindow, 1, unsupportedKdf);
    QTRY_COMPARE_WITH_TIMEOUT(
        required<QLabel>(&kdfWindow, "recoveryStatus")->text(),
        QStringLiteral("These Recovery shares use unsupported protection parameters."), 10'000);
    QCOMPARE(required<ExactTextEdit>(&kdfWindow, "recoveryShare1")->exactUtf8(),
             unsupportedKdf.toUtf8());
    QVERIFY(!required<QPushButton>(&kdfWindow, "recoverButton")->isEnabled());

    const QString protectedShares = fixtureText(
        fixtureRoot + QStringLiteral("v2-passphrase-protected/base64url.txt"));
    DesktopWindow protectedWindow;
    protectedWindow.show();
    chooseRecover(protectedWindow);
    setRecoveryField(protectedWindow, 1, protectedShares);
    QTRY_COMPARE_WITH_TIMEOUT(
        required<QLabel>(&protectedWindow, "recoveryStatus")->text(),
        QStringLiteral("Enter the passphrase required by these Recovery shares."),
        10'000);
    QCOMPARE(required<QLabel>(&protectedWindow, "detectedRecoveryEncoding")->text(),
             QStringLiteral("Detected: Base64url"));
    QVERIFY(!required<QPushButton>(&protectedWindow, "recoverButton")->isEnabled());
}

void DesktopActions::empty_recovery_field_precedence_survives_encoding_inspection() {
    DesktopWindow window;
    window.show();
    pasteIntoCreate(window, QStringLiteral("empty-field inspection precedence"));
    const QString share = createAndCopy(window, 1);
    QVERIFY(!share.isEmpty());

    chooseRecover(window);
    setRecoveryField(window, 1, share);
    QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")->text(),
                              QStringLiteral("Share content is required."), 10'000);
    QCOMPARE(required<QLabel>(&window, "detectedRecoveryEncoding")->text(),
             QStringLiteral("Detected: Words"));

    auto *encoding = required<QComboBox>(&window, "recoveryEncoding");
    encoding->setCurrentIndex(encoding->findData(1));
    QTRY_VERIFY_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")
                                 ->text()
                                 .contains(QStringLiteral("could not be decoded")),
                             10'000);

    encoding->setCurrentIndex(encoding->findData(0));
    QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")->text(),
                              QStringLiteral("Share content is required."), 10'000);

    setRecoveryField(window, 1, share + QStringLiteral(" abandon"));
    QVERIFY(required<QLabel>(&window, "recoveryStatus")
                ->text()
                .contains(QStringLiteral("could not be decoded")));

    setRecoveryField(window, 1, share);
    QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")->text(),
                              QStringLiteral("Share content is required."), 10'000);
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

void DesktopActions::generated_shares_show_authoritative_text_and_clear_stale_previews() {
    DesktopWindow window;
    window.show();
    pasteIntoCreate(window, QStringLiteral("visible generated shares"));
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "createdShares")->isVisible(), 10'000);

    QStringList displayed;
    for (int index = 1; index <= 3; ++index) {
        auto *share = required<QPlainTextEdit>(
            &window, qPrintable(QStringLiteral("generatedShare%1").arg(index)));
        QTRY_VERIFY_WITH_TIMEOUT(!share->toPlainText().isEmpty(), 10'000);
        QVERIFY(share->isReadOnly());
        QVERIFY(share->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
        displayed.append(share->toPlainText());
    }
    QCOMPARE(displayed.size(), 3);
    QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&window, "createStatus")->text(),
                              QStringLiteral("Shares created in memory."), 10'000);
    for (int index = 1; index <= 3; ++index) {
        QApplication::clipboard()->clear();
        auto *copy = required<QPushButton>(
            &window, qPrintable(QStringLiteral("copyShare%1").arg(index)));
        QVERIFY(copy->isEnabled());
        QTest::mouseClick(copy, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(QApplication::clipboard()->text(), displayed.at(index - 1), 10'000);
    }
    QVERIFY(displayed.at(0) != displayed.at(1));
    QVERIFY(displayed.at(1) != displayed.at(2));

    const auto labels = required<QWidget>(&window, "createdShares")->findChildren<QLabel *>();
    QVERIFY(std::none_of(labels.cbegin(), labels.cend(), [](const QLabel *candidate) {
        return candidate->text().startsWith(QStringLiteral("Recovery share "));
    }));

    auto *secret = required<ExactTextEdit>(&window, "secretInput");
    required<QSpinBox>(&window, "thresholdInput")->setValue(1);
    QTRY_VERIFY_WITH_TIMEOUT(!required<QWidget>(&window, "createdShares")->isVisible(), 10'000);
    for (int index = 1; index <= 3; ++index) {
        auto *share = window.findChild<QPlainTextEdit *>(
            QStringLiteral("generatedShare%1").arg(index));
        QVERIFY(share == nullptr || share->toPlainText().isEmpty());
    }

    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "createdShares")->isVisible(), 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(!required<QPlainTextEdit>(&window, "generatedShare3")
                                  ->toPlainText()
                                  .isEmpty(),
                              10'000);
    secret->moveCursor(QTextCursor::End);
    QTest::keyClicks(secret, QStringLiteral("!"));
    QTRY_VERIFY_WITH_TIMEOUT(!required<QWidget>(&window, "createdShares")->isVisible(), 10'000);
    QTest::qWait(500);
    for (QPlainTextEdit *share : window.findChildren<QPlainTextEdit *>(QRegularExpression(
             QStringLiteral("generatedShare\\d+"))))
        QVERIFY(share->toPlainText().isEmpty());

    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    for (QPlainTextEdit *share : window.findChildren<QPlainTextEdit *>(QRegularExpression(
             QStringLiteral("generatedShare\\d+"))))
        QVERIFY(share->toPlainText().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
    required<QSpinBox>(&window, "shareCountInput")->setValue(16);
    pasteIntoCreate(window, QString(500'000, QLatin1Char('p')));
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "createdShares")->isVisible(), 30'000);
    secret = required<ExactTextEdit>(&window, "secretInput");
    secret->moveCursor(QTextCursor::End);
    QTest::keyClicks(secret, QStringLiteral("!"));
    QTRY_VERIFY_WITH_TIMEOUT(!required<QWidget>(&window, "createdShares")->isVisible(), 10'000);
    QTest::qWait(1'000);
    for (QPlainTextEdit *share : window.findChildren<QPlainTextEdit *>(QRegularExpression(
             QStringLiteral("generatedShare\\d+"))))
        QVERIFY(share->toPlainText().isEmpty());
}

void DesktopActions::maximum_words_split_keeps_every_share_exportable() {
    DesktopWindow window;
    window.show();
    required<QSpinBox>(&window, "shareCountInput")->setValue(16);
    const QString maximumSecret(1'048'576, QLatin1Char('b'));
    pasteIntoCreate(window, maximumSecret);

    QElapsedTimer heartbeatClock;
    heartbeatClock.start();
    qint64 lastHeartbeat = heartbeatClock.elapsed();
    qint64 maximumHeartbeatGap = 0;
    int heartbeatCount = 0;
    QTimer heartbeat;
    heartbeat.setInterval(15);
    connect(&heartbeat, &QTimer::timeout, &window, [&] {
        const qint64 now = heartbeatClock.elapsed();
        maximumHeartbeatGap = std::max(maximumHeartbeatGap, now - lastHeartbeat);
        lastHeartbeat = now;
        ++heartbeatCount;
    });
    heartbeat.start();
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "createdShares")->isVisible(), 30'000);
    QTRY_VERIFY_WITH_TIMEOUT(
        required<QLabel>(&window, "createStatus")->text().contains(
            QStringLiteral("Copy any share")),
        30'000);
    QCOMPARE(required<ExactTextEdit>(&window, "secretInput")->exactUtf8(),
             maximumSecret.toUtf8());

    for (int index = 1; index <= 16; ++index)
        QVERIFY(required<QPushButton>(
                    &window, qPrintable(QStringLiteral("copyShare%1").arg(index)))
                    ->isEnabled());

    QByteArray firstHash;
    QByteArray repeatedFirstHash;
    QByteArray lastHash;
    qint64 maximumCopyLatency = 0;
    qint64 maximumNativeClipboardLatency = 0;
    QSignalSpy clipboardLatency(&window, &DesktopWindow::clipboardWriteObserved);
    for (const int index : {1, 16, 1}) {
        auto *copy = required<QPushButton>(
            &window, qPrintable(QStringLiteral("copyShare%1").arg(index)));
        QApplication::clipboard()->clear();
        QElapsedTimer copyClock;
        copyClock.start();
        QTest::mouseClick(copy, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(
            required<QLabel>(&window, "createStatus")->text(),
            QStringLiteral("Share %1 copied.").arg(index), 30'000);
        QTRY_VERIFY_WITH_TIMEOUT(!clipboardLatency.isEmpty(), 30'000);
        maximumNativeClipboardLatency =
            std::max(maximumNativeClipboardLatency, clipboardLatency.takeFirst().at(0).toLongLong());
        maximumCopyLatency = std::max(maximumCopyLatency, copyClock.elapsed());
        const QByteArray copied = QApplication::clipboard()->text().toUtf8();
        QVERIFY(!copied.isEmpty());
        const QByteArray hash = QCryptographicHash::hash(copied, QCryptographicHash::Sha256);
        auto *revealed = required<SegmentedShareView>(
            &window, qPrintable(QStringLiteral("generatedShare%1").arg(index)));
        QCOMPARE(revealed->byteSize(), copied.size());
        QCOMPARE(QCryptographicHash::hash(revealed->textRange(0, revealed->byteSize()).toLatin1(),
                                          QCryptographicHash::Sha256),
                 hash);
        for (int displayedIndex = 1; displayedIndex <= 16; ++displayedIndex) {
            if (displayedIndex == index)
                continue;
            auto *display = window.findChild<QPlainTextEdit *>(
                QStringLiteral("generatedShare%1").arg(displayedIndex));
            QVERIFY(display != nullptr);
            QVERIFY(display->toPlainText().isEmpty());
        }
        if (index == 1 && firstHash.isEmpty())
            firstHash = hash;
        else if (index == 1)
            repeatedFirstHash = hash;
        else
            lastHash = hash;
    }
    QVERIFY(firstHash != lastHash);
    QCOMPARE(repeatedFirstHash, firstHash);
    QVERIFY(required<QWidget>(&window, "createdShares")->isVisible());
    heartbeat.stop();
    maximumHeartbeatGap = std::max(maximumHeartbeatGap, heartbeatClock.elapsed() - lastHeartbeat);
    QVERIFY2(heartbeatCount > 0, "the Qt event loop did not service the 15 ms heartbeat");
    QVERIFY2(maximumHeartbeatGap < 500,
             qPrintable(QStringLiteral("maximum Qt heartbeat gap was %1 ms")
                            .arg(maximumHeartbeatGap)));
    qInfo().noquote() << QStringLiteral("CAPACITY maximum-copy-latency-ms=%1")
                             .arg(maximumCopyLatency)
                      << QStringLiteral("maximum-heartbeat-gap-ms=%1")
                             .arg(maximumHeartbeatGap)
                      << QStringLiteral("maximum-native-clipboard-ms=%1")
                             .arg(maximumNativeClipboardLatency);
    QPointer<SegmentedShareView> revealedBeforeReset = window.findChild<SegmentedShareView *>();
    QVERIFY(!revealedBeforeReset.isNull());
    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    QCOMPARE(revealedBeforeReset->byteSize(), qsizetype(0));
    QVERIFY(!required<QWidget>(&window, "createdShares")->isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(revealedBeforeReset.isNull());
}

void DesktopActions::injected_allocation_failures_are_transactional_and_retryable() {
    auto policy = std::make_shared<OneShotAllocationPolicy>();
    DesktopWindow window(policy);
    window.show();
    pasteIntoCreate(window, QStringLiteral("allocation retry secret"));
    required<QSpinBox>(&window, "thresholdInput")->setValue(1);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
    policy->failNext(DesktopAllocationBoundary::EditorUtf8);
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QCOMPARE(required<QLabel>(&window, "createStatus")->text(), QStringLiteral("Operation could not reserve memory."));
    policy->failNext(DesktopAllocationBoundary::GeneratedPresentation);
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&window, "createStatus")->text(),
                              QStringLiteral("Operation could not reserve memory."), 10'000);
    QVERIFY(!required<QWidget>(&window, "createdShares")->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "createdShares")->isVisible(), 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "copyShare1")->isEnabled(), 10'000);
    policy->failNext(DesktopAllocationBoundary::ClipboardHandoff);
    QApplication::clipboard()->clear();
    QTest::mouseClick(required<QPushButton>(&window, "copyShare1"), Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&window, "createStatus")->text(), QStringLiteral("Operation could not reserve memory."), 10'000);
    QVERIFY(QApplication::clipboard()->text().isEmpty());
    const QString first = createAndCopy(window, 1);
    QTemporaryDir recoveryDirectory;
    QVERIFY(recoveryDirectory.isValid());
    const QString recoveryPath = recoveryDirectory.filePath(QStringLiteral("share.txt"));
    QFile recoveryFile(recoveryPath);
    QVERIFY(recoveryFile.open(QIODevice::WriteOnly));
    QCOMPARE(recoveryFile.write(first.toUtf8()), first.toUtf8().size());
    recoveryFile.close();
    chooseRecover(window);
    window.setFileServicesForTests(std::make_shared<FileIo>(), [] { return QString(); },
                                   [&] { return QStringList{recoveryPath}; },
                                   [] { return QString(); });
    policy->failNext(DesktopAllocationBoundary::FileAcquisition);
    QTest::mouseClick(required<QPushButton>(&window, "loadRecoveryFilesButton"), Qt::LeftButton);
    QCOMPARE(required<QLabel>(&window, "recoveryStatus")->text(),
             QStringLiteral("Operation could not reserve memory."));
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryShare1")->exactUtf8(), QByteArray());
    QTest::mouseClick(required<QPushButton>(&window, "loadRecoveryFilesButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
    policy->failNext(DesktopAllocationBoundary::RecoveryTransport);
    setRecoveryField(window, 1, first + QStringLiteral(" "));
    QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")->text(), QStringLiteral("Operation could not reserve memory."), 10'000);
    setRecoveryField(window, 1, first + QStringLiteral("  "));
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
    const QByteArray recoveryPassphrase =
        required<ExactTextEdit>(&window, "recoveryPassphrase")->exactUtf8();
    policy->failNext(DesktopAllocationBoundary::EditorUtf8);
    QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
    QCOMPARE(required<QLabel>(&window, "recoveryStatus")->text(),
             QStringLiteral("Operation could not reserve memory."));
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryPassphrase")->exactUtf8(),
             recoveryPassphrase);
    QVERIFY(required<QPushButton>(&window, "recoverButton")->isEnabled());
    policy->failNext(DesktopAllocationBoundary::RecoveredPresentation);
    QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")->text(), QStringLiteral("Operation could not reserve memory."), 10'000);
    QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);
    policy->failNext(DesktopAllocationBoundary::ClipboardHandoff);
    QApplication::clipboard()->clear();
    QTest::mouseClick(required<QPushButton>(&window, "copyRecoveredButton"), Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(required<QLabel>(&window, "recoveryStatus")->text(), QStringLiteral("Operation could not reserve memory."), 10'000);
    QVERIFY(QApplication::clipboard()->text().isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("secret.bin"));
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("retry-file", 10), qint64(10));
    source.close();
    required<QTabBar>(&window, "modeSelector")->setCurrentIndex(0);
    window.setFileServicesForTests(std::make_shared<FileIo>(), [&] { return path; }, [] { return QStringList(); }, [] { return QString(); });
    policy->failNext(DesktopAllocationBoundary::FileAcquisition);
    QTest::mouseClick(required<QPushButton>(&window, "chooseSecretFileButton"), Qt::LeftButton);
    QCOMPARE(required<QLabel>(&window, "createStatus")->text(), QStringLiteral("Operation could not reserve memory."));
    QTest::mouseClick(required<QPushButton>(&window, "chooseSecretFileButton"), Qt::LeftButton);
    QVERIFY(required<QLabel>(&window, "secretFileMetadata")->isVisible());
    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    QVERIFY(!required<QLabel>(&window, "secretFileMetadata")->isVisible());
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryPassphrase")->exactUtf8(), QByteArray());
}

void DesktopActions::maximum_policy_recovery_rejects_stale_lifecycle_results() {
    const QString packet = QStringLiteral("U01OMQIBAQEBKg-DHAgPdfNRFsshslnE4jPad-ssRtOXwf8Y1-S3N60T-zhAFOw6-ak5yh4ABAAAAAAACgAAAAQAAABPvZb5vVVCremTHFeQSndDO0tDF-uJ-gBBF9GqPlAi15vaDm0QUdCfY1WvLHt5_KSDvZjaKU89RzfOaT4bHFh32ZNj4bbcUEsTpNEcXfYxEQ");
    const QString correct = QStringLiteral("synthetic maximum-policy passphrase");
    const QString wrong = QStringLiteral("synthetic maximum-policy wrong pass");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString forbiddenDestination = directory.filePath(QStringLiteral("must-not-exist"));
    int saveDialogCalls = 0;
    const auto wiped = std::make_shared<std::atomic<int>>(0);
    WipeObserverReset wipeReset;
    SecureByteBuffer::setWipeObserverForTests([wiped](QByteArrayView bytes) {
        if (!bytes.isEmpty())
            wiped->fetch_add(1, std::memory_order_release);
    });

    DesktopWindow window;
    window.setFileServicesForTests(std::make_shared<FileIo>(), [] { return QString(); },
                                   [] { return QStringList(); }, [&] {
                                       ++saveDialogCalls;
                                       return forbiddenDestination;
                                   });
    window.show();
    qint64 maximumGap = 0;
    QElapsedTimer heartbeatClock;
    heartbeatClock.start();
    qint64 prior = heartbeatClock.elapsed();
    QTimer heartbeat;
    heartbeat.setInterval(15);
    connect(&heartbeat, &QTimer::timeout, &window, [&] {
        const qint64 now = heartbeatClock.elapsed();
        maximumGap = std::max(maximumGap, now - prior);
        prior = now;
    });
    heartbeat.start();

    const auto prepare = [&](const QString &passphrase) {
        chooseRecover(window);
        setRecoveryField(window, 1, packet);
        replaceExact(required<ExactTextEdit>(&window, "recoveryPassphrase"), passphrase);
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
        QApplication::clipboard()->setText(QStringLiteral("lifecycle-sentinel"));
    };
    const auto begin = [&] {
        QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!required<QPushButton>(&window, "recoverButton")->isEnabled(), 2'000);
    };
    const auto assertNoEffects = [&] {
        QVERIFY(!required<QWidget>(&window, "recoveryResult")->isVisible());
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("lifecycle-sentinel"));
        QVERIFY(!QFileInfo::exists(forbiddenDestination));
        QCOMPARE(saveDialogCalls, 0);
    };

    prepare(wrong);
    begin();
    QTest::keyClicks(required<ExactTextEdit>(&window, "recoveryPassphrase"), QStringLiteral("!"));
    assertNoEffects();
    QTest::qWait(4'500);
    assertNoEffects();
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(), 10'000);
    const int afterEditWipes = wiped->load(std::memory_order_acquire);
    QVERIFY(afterEditWipes > 0);

    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    prepare(correct);
    begin();
    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    assertNoEffects();
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryPassphrase")->exactUtf8(), QByteArray());
    QTest::qWait(4'500);
    assertNoEffects();
    QVERIFY(required<QPushButton>(&window, "addRecoveryShareButton")->isEnabled());
    QVERIFY(wiped->load(std::memory_order_acquire) > afterEditWipes);

    prepare(correct);
    begin();
    auto *tabs = required<QTabBar>(&window, "modeSelector");
    QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(0).center());
    assertNoEffects();
    QTest::qWait(4'500);
    assertNoEffects();
    QVERIFY(required<QPushButton>(&window, "chooseSecretFileButton")->isEnabled());
    QCOMPARE(required<ExactTextEdit>(&window, "recoveryPassphrase")->exactUtf8(), QByteArray());

    prepare(correct);
    begin();
    QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);
    QCOMPARE(required<QPlainTextEdit>(&window, "recoveredText")->toPlainText(),
             QStringLiteral("synthetic maximum-policy secret"));
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("lifecycle-sentinel"));
    QVERIFY(!QFileInfo::exists(forbiddenDestination));
    QCOMPARE(saveDialogCalls, 0);
    QVERIFY(maximumGap < 500);

    auto *closing = new DesktopWindow;
    closing->setFileServicesForTests(std::make_shared<FileIo>(), [] { return QString(); },
                                     [] { return QStringList(); }, [&] {
                                         ++saveDialogCalls;
                                         return forbiddenDestination;
                                     });
    closing->show();
    chooseRecover(*closing);
    setRecoveryField(*closing, 1, packet);
    replaceExact(required<ExactTextEdit>(closing, "recoveryPassphrase"), correct);
    QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(closing, "recoverButton")->isEnabled(), 10'000);
    QApplication::clipboard()->setText(QStringLiteral("lifecycle-sentinel"));
    QTest::mouseClick(required<QPushButton>(closing, "recoverButton"), Qt::LeftButton);
    QElapsedTimer closeClock;
    closeClock.start();
    closing->close();
    const qint64 closeMilliseconds = closeClock.elapsed();
    QVERIFY(closeMilliseconds < 500);
    QCOMPARE(required<ExactTextEdit>(closing, "recoveryPassphrase")->exactUtf8(), QByteArray());
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("lifecycle-sentinel"));
    QVERIFY(!QFileInfo::exists(forbiddenDestination));
    QCOMPARE(saveDialogCalls, 0);
    QElapsedTimer destructionClock;
    destructionClock.start();
    delete closing;
    const qint64 destructionMilliseconds = destructionClock.elapsed();
    QVERIFY(destructionMilliseconds < 10'000);
    QVERIFY(maximumGap < 500);
    qInfo().noquote()
        << QStringLiteral("CAPACITY maximum-policy-heartbeat-gap-ms=%1 close-ms=%2 destruction-wait-ms=%3 wipes=%4")
               .arg(maximumGap)
               .arg(closeMilliseconds)
               .arg(destructionMilliseconds)
               .arg(wiped->load(std::memory_order_acquire));
}

void DesktopActions::segmented_share_view_is_exact_selectable_accessible_and_bounded() {
    accessibilityObservations.text.clear();
    accessibilityObservations.cursor = 0;
    accessibilityObservations.selection = 0;
    AccessibilityObserverReset accessibilityObserver;
    const auto wiped = std::make_shared<std::atomic<int>>(0);
    WipeObserverReset resetObserver;
    SecureByteBuffer::setWipeObserverForTests([wiped](QByteArrayView bytes) {
        if (bytes.size() == 8 * 1'048'576)
            wiped->fetch_add(1, std::memory_order_release);
    });

    QByteArray payload(8 * 1'048'576, '1');
    for (qsizetype offset = 0; offset < payload.size(); offset += 97)
        payload[offset] = 'z';
    const QByteArray firstHundred = payload.sliced(10, 100);
    QList<QByteArray> expectedChunkHashes;
    for (qsizetype offset = 0; offset < payload.size(); offset += 64 * 1024) {
        expectedChunkHashes.push_back(QCryptographicHash::hash(
            QByteArrayView(payload).sliced(offset, std::min<qsizetype>(64 * 1024,
                                                                       payload.size() - offset)),
            QCryptographicHash::Sha256));
    }
    auto allocationPolicy = std::make_shared<OneShotAllocationPolicy>();
    SegmentedShareView view(allocationPolicy);
    view.resize(720, 240);
    view.show();
    QElapsedTimer install;
    install.start();
    QVERIFY(view.setShare(7, 15, SecureByteBuffer::take(std::move(payload)), true));
    QVERIFY2(install.elapsed() < 500,
             qPrintable(QStringLiteral("segmented reveal install took %1 ms").arg(install.elapsed())));
    QCOMPARE(view.byteSize(), qsizetype(8 * 1'048'576));
    QCOMPARE(view.segmentBytes(), qsizetype(64));
    QCOMPARE(view.segmentCount(), 131'072);
    QVERIFY(view.findChild<QPlainTextEdit *>() == nullptr);
    QCOMPARE(view.textRange(10, 110).toLatin1(), firstHundred);

    QCOMPARE(accessibilityObservations.text.size(), expectedChunkHashes.size());
    for (int index = 0; index < accessibilityObservations.text.size(); ++index) {
        const AccessibilityTextRecord &record = accessibilityObservations.text.at(index);
        QCOMPARE(record.type, QAccessible::TextInserted);
        QCOMPARE(record.position, index * 64 * 1024);
        QVERIFY(record.length > 0 && record.length <= 64 * 1024);
        QCOMPARE(record.observedCharacterCount, int(8 * 1'048'576));
        QCOMPARE(record.sha256, expectedChunkHashes.at(index));
    }
    view.setCursorPosition(9);
    view.setCursorPosition(10);
    QVERIFY(accessibilityObservations.cursor.load() >= 2);
    view.setSelectionRange(10, 110);
    QCOMPARE(view.selection(), (QPair<qsizetype, qsizetype>(10, 110)));
    QVERIFY(accessibilityObservations.selection.load() >= 1);
    QApplication::clipboard()->clear();
    view.copySelection();
    QCOMPARE(QApplication::clipboard()->text().toLatin1(), firstHundred);

    auto *list = view.findChild<QListView *>();
    QVERIFY(list != nullptr);
    list->setFocus();
    QTest::keyClick(list, Qt::Key_Home);
    QTest::keyClick(list, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(view.selection(), (QPair<qsizetype, qsizetype>(64, 65)));
    QTest::keySequence(list, QKeySequence::SelectAll);
    QCOMPARE(view.selection(), (QPair<qsizetype, qsizetype>(0, view.byteSize())));

    QSignalSpy authoritativeCopy(&view, &SegmentedShareView::copyRequested);
    view.setSelectionRange(25, 25);
    view.copySelection();
    QCOMPARE(authoritativeCopy.count(), 1);
    QCOMPARE(authoritativeCopy.at(0).at(0).toULongLong(), quint64(7));
    QCOMPARE(authoritativeCopy.at(0).at(1).toUInt(), uint(15));
    view.setSelectionRange(10, 110);

    QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(accessible != nullptr);
    auto *text = accessible->textInterface();
    QVERIFY(text != nullptr);
    QCOMPARE(text->characterCount(), int(view.byteSize()));
    const auto preservedSelection = view.selection();
    QSignalSpy allocationFailure(&view, &SegmentedShareView::allocationFailed);
    allocationPolicy->failNext(DesktopAllocationBoundary::AccessibilityPresentation);
    QCOMPARE(text->text(0, text->characterCount()), QString());
    QCOMPARE(allocationFailure.count(), 1);
    QCOMPARE(view.selection(), preservedSelection);
    QCOMPARE(text->text(10, 110).toLatin1(), firstHundred);
    QCOMPARE(text->selectionCount(), 1);

    for (const QPair<qsizetype, qsizetype> range :
         {QPair<qsizetype, qsizetype>(10, 110),
          QPair<qsizetype, qsizetype>(0, view.byteSize())}) {
        view.setSelectionRange(range.first, range.second);
        QApplication::clipboard()->setText(QStringLiteral("sentinel"));
        allocationPolicy->failNext(DesktopAllocationBoundary::ClipboardHandoff);
        view.copySelection();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("sentinel"));
        QCOMPARE(view.selection(), range);
        view.copySelection();
        QCOMPARE(QApplication::clipboard()->text().size(), int(range.second - range.first));
    }

    for (const bool dark : {false, true}) {
        QPalette palette = view.palette();
        if (dark) {
            palette.setColor(QPalette::Base, QColor(30, 30, 30));
            palette.setColor(QPalette::Text, QColor(235, 235, 235));
        } else {
            palette.setColor(QPalette::Base, Qt::white);
            palette.setColor(QPalette::Text, Qt::black);
        }
        view.setPalette(palette);
        QPixmap rendered(view.size());
        view.render(&rendered);
        QVERIFY(!rendered.isNull());
    }

    const int eventsBeforeClear = accessibilityObservations.text.size();
    view.clearSensitive();
    QCOMPARE(view.byteSize(), qsizetype(0));
    QCOMPARE(view.segmentCount(), 0);
    QCOMPARE(wiped->load(std::memory_order_acquire), 1);
    QCOMPARE(accessibilityObservations.text.size() - eventsBeforeClear,
             expectedChunkHashes.size());
    for (int index = 0; index < expectedChunkHashes.size(); ++index) {
        const AccessibilityTextRecord &record =
            accessibilityObservations.text.at(eventsBeforeClear + index);
        const int expectedChunk = expectedChunkHashes.size() - index - 1;
        QCOMPARE(record.type, QAccessible::TextRemoved);
        QCOMPARE(record.position, expectedChunk * 64 * 1024);
        QVERIFY(record.length > 0 && record.length <= 64 * 1024);
        QCOMPARE(record.observedCharacterCount, 0);
        QCOMPARE(record.sha256, expectedChunkHashes.at(expectedChunk));
    }
    QVERIFY(!view.setShare(8, 0, SecureByteBuffer::take(QByteArray("not admitted")), false));
    QVERIFY(!view.setShare(8, 0, SecureByteBuffer::take(QByteArray(1, char(0xff))), true));
    QCOMPARE(view.byteSize(), qsizetype(0));

    SegmentedShareView narrow;
    narrow.resize(120, 90);
    narrow.show();
    QByteArray narrowBytes(64, 'a');
    narrowBytes[63] = 'Z';
    QVERIFY(narrow.setShare(9, 0, SecureByteBuffer::take(std::move(narrowBytes)), true));
    QCoreApplication::processEvents();
    auto *narrowList = narrow.findChild<QListView *>();
    QVERIFY(narrowList != nullptr);
    QVERIFY(narrowList->horizontalScrollBar()->maximum() > 0);
    narrow.scrollToOffset(63);
    QCoreApplication::processEvents();
    const QRect finalCharacter = narrow.characterRect(63);
    QVERIFY(narrowList->viewport()->rect().contains(
        narrowList->viewport()->mapFromGlobal(finalCharacter.center())));
    QCOMPARE(narrow.offsetAtGlobalPoint(finalCharacter.topLeft() + QPoint(1, 1)), qsizetype(63));
    const QRect dragStartCharacter = narrow.characterRect(60);
    QWidget *narrowViewport = narrowList->viewport();
    const QPoint dragStart = narrowViewport->mapFromGlobal(dragStartCharacter.topLeft() + QPoint(1, 1));
    const QPoint dragEnd = narrowViewport->mapFromGlobal(finalCharacter.bottomRight() - QPoint(1, 1));
    QTest::mousePress(narrowViewport, Qt::LeftButton, Qt::NoModifier, dragStart);
    QTest::mouseMove(narrowViewport, dragEnd, 10);
    QTest::mouseRelease(narrowViewport, Qt::LeftButton, Qt::NoModifier, dragEnd);
    QVERIFY(narrow.selection().first != narrow.selection().second);
    QVERIFY(std::max(narrow.selection().first, narrow.selection().second) >= 63);
    narrow.setSelectionRange(63, 64);
    narrow.copySelection();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Z"));

    SegmentedShareView controls;
    controls.resize(220, 90);
    controls.show();
    const QByteArray controlBytes("A\n\r\tB", 5);
    QVERIFY(controls.setShare(10, 0, SecureByteBuffer::take(QByteArray(controlBytes)), true));
    QCOMPARE(controls.textRange(0, 5).toLatin1(), controlBytes);
    controls.setSelectionRange(0, 5);
    controls.copySelection();
    QCOMPARE(QApplication::clipboard()->text().toLatin1(), controlBytes);
    QPixmap controlRendering(controls.size());
    controls.render(&controlRendering);
    QVERIFY(!controlRendering.isNull());

    const int eventsBeforeReplacement = accessibilityObservations.text.size();
    QVERIFY(controls.setShare(11, 1, SecureByteBuffer::take(QByteArray("replacement")), true));
    QCOMPARE(accessibilityObservations.text.size(), eventsBeforeReplacement + 2);
    const AccessibilityTextRecord &removed = accessibilityObservations.text.at(eventsBeforeReplacement);
    const AccessibilityTextRecord &inserted =
        accessibilityObservations.text.at(eventsBeforeReplacement + 1);
    QCOMPARE(removed.type, QAccessible::TextRemoved);
    QCOMPARE(removed.position, 0);
    QCOMPARE(removed.length, 5);
    QCOMPARE(removed.observedCharacterCount, 0);
    QCOMPARE(inserted.type, QAccessible::TextInserted);
    QCOMPARE(inserted.position, 0);
    QCOMPARE(inserted.length, 11);
    QCOMPARE(inserted.observedCharacterCount, 11);

    SegmentedShareView transitions;
    const int transitionsBegin = accessibilityObservations.text.size();
    QVERIFY(transitions.setShare(20, 0, SecureByteBuffer::take(QByteArray("1")), true));
    QVERIFY(transitions.setShare(21, 15, SecureByteBuffer::take(QByteArray(16, 'x')), true));
    QVERIFY(transitions.setShare(22, 0, SecureByteBuffer::take(QByteArray("1")), true));
    QCOMPARE(accessibilityObservations.text.size(), transitionsBegin + 5);
    const QList<QPair<QAccessible::Event, int>> expectedTransitions = {
        {QAccessible::TextInserted, 1}, {QAccessible::TextRemoved, 1},
        {QAccessible::TextInserted, 16}, {QAccessible::TextRemoved, 16},
        {QAccessible::TextInserted, 1}};
    for (int index = 0; index < expectedTransitions.size(); ++index) {
        const AccessibilityTextRecord &record =
            accessibilityObservations.text.at(transitionsBegin + index);
        QCOMPARE(record.type, expectedTransitions.at(index).first);
        QCOMPARE(record.length, expectedTransitions.at(index).second);
        QCOMPARE(record.position, 0);
    }
}

void DesktopActions::maximum_accessibility_handoff_remains_bounded_and_responsive() {
    accessibilityObservations.text.clear();
    accessibilityObservations.cursor = 0;
    accessibilityObservations.selection = 0;
    AccessibilityObserverReset accessibilityObserver;
    QByteArray payload(8 * 1'048'576, '1');
    for (qsizetype offset = 0; offset < payload.size(); offset += 101)
        payload[offset] = 'z';
    const QByteArray expectedHash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    SegmentedShareView view;
    view.resize(240, 120);
    view.show();

    qint64 maximumHeartbeatGap = 0;
    qint64 lastHeartbeat = 0;
    int heartbeatCount = 0;
    QElapsedTimer heartbeatClock;
    heartbeatClock.start();
    QTimer heartbeat;
    heartbeat.setInterval(15);
    connect(&heartbeat, &QTimer::timeout, &view, [&] {
        const qint64 now = heartbeatClock.elapsed();
        maximumHeartbeatGap = std::max(maximumHeartbeatGap, now - lastHeartbeat);
        lastHeartbeat = now;
        ++heartbeatCount;
    });
    heartbeat.start();
    QTest::qWait(45);

    QElapsedTimer reveal;
    reveal.start();
    QVERIFY(view.setShare(12, 0, SecureByteBuffer::take(std::move(payload)), true));
    const qint64 revealLatency = reveal.elapsed();
    QCOMPARE(accessibilityObservations.text.size(), 128);
    qsizetype insertedTotal = 0;
    for (int index = 0; index < 128; ++index) {
        const AccessibilityTextRecord &record = accessibilityObservations.text.at(index);
        QCOMPARE(record.type, QAccessible::TextInserted);
        QCOMPARE(record.position, index * 64 * 1024);
        QVERIFY(record.length > 0 && record.length <= 64 * 1024);
        QCOMPARE(record.observedCharacterCount, int(8 * 1'048'576));
        insertedTotal += record.length;
    }
    QCOMPARE(insertedTotal, qsizetype(8 * 1'048'576));

    QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(accessible != nullptr);
    auto *text = accessible->textInterface();
    QVERIFY(text != nullptr);
    QElapsedTimer query;
    query.start();
    QString handoff = text->text(0, text->characterCount());
    const qint64 queryLatency = query.elapsed();
    QCOMPARE(handoff.size(), int(8 * 1'048'576));
    QCOMPARE(QCryptographicHash::hash(handoff.toLatin1(), QCryptographicHash::Sha256), expectedHash);
    handoff.clear();
    handoff.squeeze();

    QElapsedTimer clear;
    clear.start();
    view.clearSensitive();
    const qint64 clearLatency = clear.elapsed();
    QCOMPARE(accessibilityObservations.text.size(), 256);
    qsizetype removedTotal = 0;
    for (int index = 128; index < 256; ++index) {
        const AccessibilityTextRecord &record = accessibilityObservations.text.at(index);
        QCOMPARE(record.type, QAccessible::TextRemoved);
        QVERIFY(record.length > 0 && record.length <= 64 * 1024);
        QCOMPARE(record.observedCharacterCount, 0);
        removedTotal += record.length;
    }
    QCOMPARE(removedTotal, qsizetype(8 * 1'048'576));

    QCoreApplication::processEvents();
    heartbeat.stop();
    maximumHeartbeatGap = std::max(maximumHeartbeatGap, heartbeatClock.elapsed() - lastHeartbeat);
    QVERIFY(heartbeatCount > 0);
    QVERIFY2(std::max({revealLatency, queryLatency, clearLatency}) < 500,
             qPrintable(QStringLiteral("maximum accessibility phase took %1/%2/%3 ms")
                            .arg(revealLatency)
                            .arg(queryLatency)
                            .arg(clearLatency)));
    QVERIFY2(maximumHeartbeatGap < 500,
             qPrintable(QStringLiteral("maximum accessibility heartbeat gap was %1 ms")
                            .arg(maximumHeartbeatGap)));
    qInfo().noquote() << QStringLiteral("CAPACITY maximum-accessibility-reveal-ms=%1")
                             .arg(revealLatency)
                      << QStringLiteral("maximum-accessibility-query-ms=%1").arg(queryLatency)
                      << QStringLiteral("maximum-accessibility-clear-ms=%1").arg(clearLatency)
                      << QStringLiteral("maximum-heartbeat-gap-ms=%1")
                             .arg(maximumHeartbeatGap);
}

void DesktopActions::secure_queued_buffers_wipe_on_final_release() {
    const auto observation = std::make_shared<WipeObservation>();
    WipeObserverReset resetObserver;
    SecureByteBuffer::setWipeObserverForTests([observation](QByteArrayView bytes) {
        if (std::any_of(bytes.begin(), bytes.end(), [](char byte) { return byte != '\0'; }))
            observation->nonzero.fetch_add(1, std::memory_order_relaxed);
        if (bytes.size() == kQueuedCreatePassphraseSize)
            observation->queuedCreatePassphrases.fetch_add(1, std::memory_order_release);
        if (bytes.size() == kQueuedRecoveryPassphraseSize)
            observation->queuedRecoveryPassphrases.fetch_add(1, std::memory_order_release);
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

        QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
        pasteIntoCreate(window, QStringLiteral("queued protected create input"));
        required<QCheckBox>(&window, "protectWithPassphrase")->setChecked(true);
        const QString createPassphrase(kQueuedCreatePassphraseSize, QLatin1Char('p'));
        replaceExact(required<ExactTextEdit>(&window, "createPassphrase"), createPassphrase);
        replaceExact(required<ExactTextEdit>(&window, "confirmPassphrase"), createPassphrase);
        QTest::mouseClick(required<QPushButton>(&window, "createButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "createdShares")->isVisible(), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(observation->queuedCreatePassphrases.load(
                                     std::memory_order_acquire)
                                     >= 1,
                                 10'000);

        QTest::mouseClick(required<QPushButton>(&window, "copyShare1"), Qt::LeftButton);
        QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "createButton")->isEnabled(), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(observation->count.load(std::memory_order_acquire) >= 3, 10'000);

        chooseRecover(window);
        pasteRecovery(window, QStringLiteral("malformed handled recovery input"));
        QVERIFY(required<QLabel>(&window, "recoveryStatus")->text().contains(QStringLiteral("could not be decoded")));
        QTRY_VERIFY_WITH_TIMEOUT(observation->count.load(std::memory_order_acquire) >= 7, 10'000);

        const QStringList protectedShares = fixtureText(QStringLiteral(
            "crates/safeparts_core/tests/fixtures/protected_surface_interoperability/desktop/base64url.txt"))
                                                .split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                                       Qt::SkipEmptyParts);
        setRecoveryField(window, 1, protectedShares.at(0));
        setRecoveryField(window, 2, protectedShares.at(1));
        replaceExact(required<ExactTextEdit>(&window, "recoveryPassphrase"),
                     QStringLiteral("issue-142 synthetic interoperability passphrase"));
        QTRY_VERIFY_WITH_TIMEOUT(required<QPushButton>(&window, "recoverButton")->isEnabled(),
                                 10'000);
        QTest::mouseClick(required<QPushButton>(&window, "recoverButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(required<QWidget>(&window, "recoveryResult")->isVisible(), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(observation->queuedRecoveryPassphrases.load(
                                     std::memory_order_acquire)
                                     >= 1,
                                 10'000);
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
    auto *recoveryEditor = required<ExactTextEdit>(&window, "recoveryShare1");
    QApplication::clipboard()->setText(QString(300'000, QLatin1Char('z')));
    recoveryEditor->setFocus();
    QTest::keySequence(recoveryEditor, QKeySequence::Paste);
    QTest::mouseClick(required<QToolButton>(&window, "startOverButton"), Qt::LeftButton);
    QCOMPARE(required<QLabel>(&window, "recoveryStatus")->text(), QStringLiteral("Share content is required."));
    QTest::qWait(1'000);
    QCOMPARE(required<QLabel>(&window, "recoveryStatus")->text(), QStringLiteral("Share content is required."));
    QCOMPARE(recoveryEditor->exactUtf8(), QByteArray());
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
