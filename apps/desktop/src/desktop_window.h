#pragma once

#include "allocation_policy.h"
#include "secure_byte_buffer.h"

#include <QList>
#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class ExactTextEdit;
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTabBar;
class QThread;
class QTimer;
class QVBoxLayout;
class QWidget;
class RustWorker;
class FileIo;
class SegmentedShareView;

class DesktopWindow final : public QMainWindow {
    Q_OBJECT

public:
    using OpenFileDialog = std::function<QString()>;
    using OpenFilesDialog = std::function<QStringList()>;
    using SaveFileDialog = std::function<QString()>;

    explicit DesktopWindow(QWidget *parent = nullptr);
    explicit DesktopWindow(std::shared_ptr<DesktopAllocationPolicy> allocationPolicy,
                           QWidget *parent = nullptr);
    ~DesktopWindow() override;
    void setFileServicesForTests(std::shared_ptr<FileIo> fileIo, OpenFileDialog openFile,
                                 OpenFilesDialog openFiles, SaveFileDialog saveFile);

signals:
    void requestReset(quint64 generation);
    void requestCreate(quint64 generation, SecureByteBuffer secret, quint8 threshold,
                       quint8 shareCount, int encoding, SecureByteBuffer passphrase);
    void requestEncodeShare(quint64 generation, quint16 index, int purpose);
    void requestReplaceRecovery(quint64 generation, QList<SecureByteBuffer> inputs, int encoding);
    void requestRecover(quint64 generation, SecureByteBuffer passphrase);
    void requestRecoveredBytes(quint64 generation, int purpose);
    void clipboardWriteObserved(qint64 elapsedMilliseconds);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void switchMode(int index);
    void startOver();
    void createShares();
    void chooseSecretFile();
    void useTextSecret();
    void loadShareFiles();
    void recover();
    void operationFinished(quint64 generation, int status, quint8 threshold, quint16 shareCount,
                           quint16 suppliedCount, quint16 batchCount, int encoding,
                           bool protectedInput, bool ready);
    void bytesFinished(quint64 generation, int status, SecureByteBuffer bytes, int purpose,
                       quint16 index, bool asciiValidated);

private:
    enum class Pending {
        None,
        Reset,
        Create,
        PreviewShares,
        Inspect,
        InspectReset,
        CopyShare,
        SaveShare,
        Recover,
        CopyRecovered,
        SaveRecovered
    };

    void buildUi();
    QWidget *buildCreatePage();
    QWidget *buildRecoverPage();
    void clearVisibleState();
    void createInputChanged();
    void queueCurrentReset();
    void showCreated(quint8 threshold, quint16 shareCount);
    void requestNextGeneratedShare();
    void clearGeneratedPresentation();
    void releaseRevealedGeneratedShare();
    void finishLazyGeneratedPresentation();
    void failGeneratedPresentation(const QString &message);
    void addRecoveryField();
    void removeRecoveryField(ExactTextEdit *editor);
    void renumberRecoveryFields();
    void clearRecoveryFields();
    void recoveryInputChanged();
    void synchronizeRecoveryFields();
    void setBusy(bool busy);
    void clearPendingExport();
    void setRecoveryStatus(int status, quint8 threshold, quint16 suppliedCount, bool ready);
    static QString statusText(int status);
    quint64 nextGeneration();

    quint64 generation_ = 0;
    quint64 requestedResetGeneration_ = 0;
    bool resetRequested_ = false;
    bool clearingRecoveryFields_ = false;
    bool recoveryHasEmptyFields_ = true;
    bool pendingInspectionHasEmptyFields_ = true;
    quint64 pendingInspectionGeneration_ = 0;
    Pending pending_ = Pending::None;
    QThread *thread_ = nullptr;
    RustWorker *worker_ = nullptr;

    QTabBar *modeSelector_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    ExactTextEdit *secretInput_ = nullptr;
    QLabel *secretFileMetadata_ = nullptr;
    QPushButton *chooseSecretFileButton_ = nullptr;
    QPushButton *useTextSecretButton_ = nullptr;
    SecureByteBuffer secretFileBytes_;
    bool usesSecretFile_ = false;
    QSpinBox *threshold_ = nullptr;
    QSpinBox *shareCount_ = nullptr;
    QComboBox *createEncoding_ = nullptr;
    QCheckBox *protectWithPassphrase_ = nullptr;
    QWidget *createPassphrasePanel_ = nullptr;
    ExactTextEdit *createPassphrase_ = nullptr;
    ExactTextEdit *confirmPassphrase_ = nullptr;
    QPushButton *createButton_ = nullptr;
    QLabel *createStatus_ = nullptr;
    QWidget *createdResult_ = nullptr;
    QWidget *createdRows_ = nullptr;
    QLabel *createdTitle_ = nullptr;
    QList<QPlainTextEdit *> generatedShareDisplays_;
    QList<QPushButton *> generatedShareCopyButtons_;
    QList<QPushButton *> generatedShareSaveButtons_;
    quint16 nextGeneratedShare_ = 0;
    qsizetype retainedGeneratedPresentationBytes_ = 0;
    bool lazyGeneratedPresentation_ = false;
    int revealedGeneratedShareIndex_ = -1;
    SegmentedShareView *revealedGeneratedShare_ = nullptr;

    QVBoxLayout *recoveryFieldsLayout_ = nullptr;
    QList<ExactTextEdit *> recoveryFields_;
    QTimer *recoverySyncTimer_ = nullptr;
    QComboBox *recoveryEncoding_ = nullptr;
    QLabel *recoveryDetectedFormat_ = nullptr;
    QLabel *recoveryCount_ = nullptr;
    QLabel *recoveryStatus_ = nullptr;
    QWidget *recoveryPassphrasePanel_ = nullptr;
    ExactTextEdit *recoveryPassphrase_ = nullptr;
    bool recoveryProtected_ = false;
    QPushButton *addRecoveryButton_ = nullptr;
    QPushButton *loadRecoveryFilesButton_ = nullptr;
    QPushButton *recoverButton_ = nullptr;
    QWidget *recoveryResult_ = nullptr;
    QPlainTextEdit *recoveredDisplay_ = nullptr;
    QPushButton *copyRecovered_ = nullptr;
    QPushButton *saveRecovered_ = nullptr;

    std::shared_ptr<DesktopAllocationPolicy> allocationPolicy_;
    std::shared_ptr<FileIo> fileIo_;
    OpenFileDialog openFileDialog_;
    OpenFilesDialog openFilesDialog_;
    SaveFileDialog saveFileDialog_;
    QString pendingDestination_;
    quint16 pendingShareIndex_ = 0;
};
