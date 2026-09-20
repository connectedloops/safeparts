#pragma once

#include <QByteArray>
#include <QMainWindow>
#include <QString>

class ExactTextEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTabBar;
class QThread;
class QWidget;
class RustWorker;

class DesktopWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit DesktopWindow(QWidget *parent = nullptr);
    ~DesktopWindow() override;

signals:
    void requestReset(quint64 generation);
    void requestCreate(quint64 generation, QByteArray secret, quint8 threshold, quint8 shareCount);
    void requestEncodeShare(quint64 generation, quint16 index);
    void requestAddRecovery(quint64 generation, QByteArray input);
    void requestRemoveRecovery(quint64 generation, quint16 batchIndex);
    void requestRecover(quint64 generation);
    void requestRecoveredText(quint64 generation);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void switchMode(int index);
    void startOver();
    void createShares();
    void pasteRecovery();
    void removeLastRecovery();
    void recover();
    void operationFinished(quint64 generation, int status, quint8 threshold, quint16 shareCount,
                           quint16 suppliedCount, quint16 batchCount, bool ready);
    void bytesFinished(quint64 generation, int status, QByteArray bytes, int purpose, quint16 index);

private:
    enum class Pending { None, Reset, Create, Inspect, CopyShare, Recover, CopyRecovered };

    void buildUi();
    QWidget *buildCreatePage();
    QWidget *buildRecoverPage();
    void clearVisibleState();
    void showCreated(quint8 threshold, quint16 shareCount);
    void setBusy(bool busy);
    void setRecoveryStatus(int status, quint8 threshold, quint16 suppliedCount, bool ready);
    static QString statusText(int status);
    quint64 nextGeneration();

    quint64 generation_ = 0;
    Pending pending_ = Pending::None;
    quint16 recoveryBatchCount_ = 0;
    QThread *thread_ = nullptr;
    RustWorker *worker_ = nullptr;

    QTabBar *modeSelector_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QStackedWidget *createStates_ = nullptr;
    ExactTextEdit *secretInput_ = nullptr;
    QSpinBox *threshold_ = nullptr;
    QSpinBox *shareCount_ = nullptr;
    QPushButton *createButton_ = nullptr;
    QLabel *createStatus_ = nullptr;
    QWidget *createdRows_ = nullptr;
    QLabel *createdTitle_ = nullptr;

    QLabel *recoveryCount_ = nullptr;
    QLabel *recoveryStatus_ = nullptr;
    QPushButton *pasteButton_ = nullptr;
    QPushButton *removeButton_ = nullptr;
    QPushButton *recoverButton_ = nullptr;
    QWidget *recoveryResult_ = nullptr;
    QPlainTextEdit *recoveredDisplay_ = nullptr;
    QPushButton *copyRecovered_ = nullptr;
};
