#pragma once

#include "secure_byte_buffer.h"

#include <QList>
#include <QMainWindow>
#include <QString>

class ExactTextEdit;
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

class DesktopWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit DesktopWindow(QWidget *parent = nullptr);
    ~DesktopWindow() override;

signals:
    void requestReset(quint64 generation);
    void requestCreate(quint64 generation, SecureByteBuffer secret, quint8 threshold,
                       quint8 shareCount, int encoding);
    void requestEncodeShare(quint64 generation, quint16 index, int purpose);
    void requestReplaceRecovery(quint64 generation, QList<SecureByteBuffer> inputs, int encoding);
    void requestRecover(quint64 generation);
    void requestRecoveredText(quint64 generation);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void switchMode(int index);
    void startOver();
    void createShares();
    void recover();
    void operationFinished(quint64 generation, int status, quint8 threshold, quint16 shareCount,
                           quint16 suppliedCount, quint16 batchCount, int encoding,
                           bool protectedInput, bool ready);
    void bytesFinished(quint64 generation, int status, SecureByteBuffer bytes, int purpose, quint16 index);

private:
    enum class Pending {
        None,
        Reset,
        Create,
        PreviewShares,
        Inspect,
        InspectReset,
        CopyShare,
        Recover,
        CopyRecovered
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
    void finishLazyGeneratedPresentation();
    void failGeneratedPresentation(const QString &message);
    void addRecoveryField();
    void removeRecoveryField(ExactTextEdit *editor);
    void renumberRecoveryFields();
    void clearRecoveryFields();
    void recoveryInputChanged();
    void synchronizeRecoveryFields();
    void setBusy(bool busy);
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
    QSpinBox *threshold_ = nullptr;
    QSpinBox *shareCount_ = nullptr;
    QComboBox *createEncoding_ = nullptr;
    QPushButton *createButton_ = nullptr;
    QLabel *createStatus_ = nullptr;
    QWidget *createdResult_ = nullptr;
    QWidget *createdRows_ = nullptr;
    QLabel *createdTitle_ = nullptr;
    QList<QPlainTextEdit *> generatedShareDisplays_;
    QList<QPushButton *> generatedShareCopyButtons_;
    quint16 nextGeneratedShare_ = 0;
    qsizetype retainedGeneratedPresentationBytes_ = 0;
    bool lazyGeneratedPresentation_ = false;

    QVBoxLayout *recoveryFieldsLayout_ = nullptr;
    QList<ExactTextEdit *> recoveryFields_;
    QTimer *recoverySyncTimer_ = nullptr;
    QComboBox *recoveryEncoding_ = nullptr;
    QLabel *recoveryDetectedFormat_ = nullptr;
    QLabel *recoveryCount_ = nullptr;
    QLabel *recoveryStatus_ = nullptr;
    QPushButton *addRecoveryButton_ = nullptr;
    QPushButton *recoverButton_ = nullptr;
    QWidget *recoveryResult_ = nullptr;
    QPlainTextEdit *recoveredDisplay_ = nullptr;
    QPushButton *copyRecovered_ = nullptr;
};
