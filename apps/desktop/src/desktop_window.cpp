#include "desktop_window.h"

#include "clipboard.h"
#include "exact_text_edit.h"
#include "rust_worker.h"

#include <QCloseEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr qsizetype kMaximumPasteBytes = 16 * 1'048'576;

int statusCode(Status status) {
    return static_cast<int>(static_cast<std::uint8_t>(status));
}

QLabel *label(const QString &text, bool secondary = false) {
    auto *widget = new QLabel(text);
    widget->setTextFormat(Qt::PlainText);
    widget->setWordWrap(true);
    if (secondary) {
        QPalette palette = widget->palette();
        QColor color = palette.color(QPalette::WindowText);
        color.setAlphaF(0.64);
        palette.setColor(QPalette::WindowText, color);
        widget->setPalette(palette);
    }
    return widget;
}

QWidget *surface() {
    auto *widget = new QFrame;
    widget->setProperty("surface", true);
    return widget;
}

void configureEditor(QPlainTextEdit *editor) {
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor->setFrameShape(QFrame::NoFrame);
}

QWidget *centeredPage(QWidget *content) {
    auto *viewport = new QWidget;
    auto *layout = new QHBoxLayout(viewport);
    layout->setContentsMargins(0, 6, 0, 6);
    layout->addStretch();
    content->setMaximumWidth(620);
    layout->addWidget(content, 1, Qt::AlignVCenter);
    layout->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(viewport);
    return scroll;
}
} // namespace

DesktopWindow::DesktopWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Safeparts"));
    resize(740, 590);
    setMinimumSize(620, 480);

    thread_ = new QThread(this);
    worker_ = new RustWorker;
    worker_->moveToThread(thread_);
    connect(this, &DesktopWindow::requestReset, worker_, &RustWorker::reset, Qt::QueuedConnection);
    connect(this, &DesktopWindow::requestCreate, worker_, &RustWorker::create, Qt::QueuedConnection);
    connect(this, &DesktopWindow::requestEncodeShare, worker_, &RustWorker::encodeShare, Qt::QueuedConnection);
    connect(this, &DesktopWindow::requestAddRecovery, worker_, &RustWorker::addRecovery, Qt::QueuedConnection);
    connect(this, &DesktopWindow::requestRemoveRecovery, worker_, &RustWorker::removeRecovery, Qt::QueuedConnection);
    connect(this, &DesktopWindow::requestRecover, worker_, &RustWorker::recover, Qt::QueuedConnection);
    connect(this, &DesktopWindow::requestRecoveredText, worker_, &RustWorker::recoveredText, Qt::QueuedConnection);
    connect(worker_, &RustWorker::operationFinished, this, &DesktopWindow::operationFinished);
    connect(worker_, &RustWorker::bytesFinished, this, &DesktopWindow::bytesFinished);
    thread_->start();

    buildUi();
    switchMode(0);
}

DesktopWindow::~DesktopWindow() {
    if (worker_ != nullptr && thread_->isRunning()) {
        const quint64 finalGeneration = ++generation_;
        QMetaObject::invokeMethod(worker_, [this, finalGeneration] { worker_->reset(finalGeneration); },
                                  Qt::BlockingQueuedConnection);
        RustWorker *ownedWorker = worker_;
        QMetaObject::invokeMethod(ownedWorker, [ownedWorker] { delete ownedWorker; },
                                  Qt::BlockingQueuedConnection);
        worker_ = nullptr;
        thread_->quit();
        thread_->wait();
    }
}

void DesktopWindow::buildUi() {
    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(28, 18, 28, 18);
    root->setSpacing(14);

    auto *header = new QHBoxLayout;
    modeSelector_ = new QTabBar;
    modeSelector_->setObjectName(QStringLiteral("modeSelector"));
    modeSelector_->setAccessibleName(QStringLiteral("Choose Create or Recover"));
    modeSelector_->addTab(QStringLiteral("Create"));
    modeSelector_->addTab(QStringLiteral("Recover"));
    modeSelector_->setExpanding(true);
    modeSelector_->setUsesScrollButtons(false);
    modeSelector_->setFixedSize(244, 31);
    header->addWidget(modeSelector_);
    header->addStretch();
    auto *help = new QToolButton;
    help->setObjectName(QStringLiteral("helpButton"));
    help->setText(QStringLiteral("Help"));
    help->setAutoRaise(true);
    auto *startOverButton = new QToolButton;
    startOverButton->setObjectName(QStringLiteral("startOverButton"));
    startOverButton->setText(QStringLiteral("Start over"));
    startOverButton->setAutoRaise(true);
    header->addWidget(help);
    header->addWidget(startOverButton);
    root->addLayout(header);

    pages_ = new QStackedWidget;
    pages_->addWidget(centeredPage(buildCreatePage()));
    pages_->addWidget(centeredPage(buildRecoverPage()));
    root->addWidget(pages_, 1);
    setCentralWidget(central);

    setStyleSheet(QStringLiteral(
        "QFrame[surface=\"true\"] { border: 1px solid palette(midlight); border-radius: 14px; background: palette(base); }"
        "QPlainTextEdit { border: 1px solid palette(midlight); border-radius: 9px; padding: 8px; background: palette(base); }"
        "QPlainTextEdit:focus { border: 2px solid palette(highlight); padding: 7px; }"
        "QPushButton { border-radius: 8px; padding: 6px 12px; min-height: 20px; }"
        "QTabBar::tab { min-width: 110px; padding: 5px; border-radius: 7px; }"
        "QTabBar::tab:selected { background: palette(button); }"));

    connect(modeSelector_, &QTabBar::currentChanged, this, &DesktopWindow::switchMode);
    connect(startOverButton, &QToolButton::clicked, this, &DesktopWindow::startOver);
    connect(help, &QToolButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("Safeparts help"),
                                 QStringLiteral("Create keeps one share set in memory. Store recovery shares separately. "
                                                "Recover only runs after you choose Recover. This build supports unprotected Words text shares."));
    });
}

QWidget *DesktopWindow::buildCreatePage() {
    createStates_ = new QStackedWidget;

    auto *entry = new QWidget;
    auto *entryLayout = new QVBoxLayout(entry);
    entryLayout->setContentsMargins(0, 0, 0, 0);
    entryLayout->setSpacing(16);
    auto *title = label(QStringLiteral("Create recovery shares"));
    QFont titleFont = title->font();
    titleFont.setPointSizeF(22);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    entryLayout->addWidget(title);
    entryLayout->addWidget(label(QStringLiteral("Enter text exactly as you want to recover it."), true));

    auto *group = surface();
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(20, 16, 20, 16);
    groupLayout->setSpacing(10);
    auto *secretLabel = label(QStringLiteral("Secret text"));
    groupLayout->addWidget(secretLabel);
    secretInput_ = new ExactTextEdit;
    secretInput_->setObjectName(QStringLiteral("secretInput"));
    secretInput_->setAccessibleName(QStringLiteral("Secret text"));
    secretInput_->setPlaceholderText(QStringLiteral("Type or paste text"));
    secretInput_->setFixedHeight(150);
    configureEditor(secretInput_);
    secretLabel->setBuddy(secretInput_);
    groupLayout->addWidget(secretInput_);

    auto *settings = new QHBoxLayout;
    settings->addWidget(label(QStringLiteral("Threshold")));
    threshold_ = new QSpinBox;
    threshold_->setObjectName(QStringLiteral("thresholdInput"));
    threshold_->setAccessibleName(QStringLiteral("Threshold"));
    threshold_->setRange(1, 255);
    threshold_->setValue(2);
    settings->addWidget(threshold_);
    settings->addSpacing(12);
    settings->addWidget(label(QStringLiteral("Share count")));
    shareCount_ = new QSpinBox;
    shareCount_->setObjectName(QStringLiteral("shareCountInput"));
    shareCount_->setAccessibleName(QStringLiteral("Share count"));
    shareCount_->setRange(1, 255);
    shareCount_->setValue(3);
    settings->addWidget(shareCount_);
    settings->addStretch();
    settings->addWidget(label(QStringLiteral("Words"), true));
    groupLayout->addLayout(settings);

    auto *action = new QHBoxLayout;
    createStatus_ = label(QStringLiteral("2 of 3 · Words"), true);
    createStatus_->setObjectName(QStringLiteral("createStatus"));
    action->addWidget(createStatus_, 1);
    createButton_ = new QPushButton(QStringLiteral("Create shares"));
    createButton_->setObjectName(QStringLiteral("createButton"));
    createButton_->setDefault(true);
    action->addWidget(createButton_);
    groupLayout->addLayout(action);
    entryLayout->addWidget(group);
    entryLayout->addStretch();
    createStates_->addWidget(entry);

    auto *created = new QWidget;
    auto *createdLayout = new QVBoxLayout(created);
    createdLayout->setContentsMargins(0, 0, 0, 0);
    createdLayout->setSpacing(16);
    createdTitle_ = label(QStringLiteral("Recovery shares"));
    createdTitle_->setObjectName(QStringLiteral("createdTitle"));
    createdTitle_->setFont(titleFont);
    createdLayout->addWidget(createdTitle_);
    createdLayout->addWidget(label(QStringLiteral("Copy each complete share and store them separately."), true));
    createdRows_ = surface();
    createdRows_->setObjectName(QStringLiteral("createdShares"));
    createdLayout->addWidget(createdRows_);
    createdLayout->addStretch();
    createStates_->addWidget(created);

    connect(createButton_, &QPushButton::clicked, this, &DesktopWindow::createShares);
    connect(secretInput_, &ExactTextEdit::exactTextChanged, this, &DesktopWindow::createInputChanged);
    connect(secretInput_, &ExactTextEdit::inputRejected, this, [this](int reason) {
        createStatus_->setText(reason == static_cast<int>(ClipboardRead::Status::TooLarge)
                                   ? QStringLiteral("Secret text cannot exceed the 1 MiB UTF-8 limit.")
                                   : QStringLiteral("Secret text must be valid UTF-8."));
    });
    connect(threshold_, &QSpinBox::valueChanged, this, &DesktopWindow::createInputChanged);
    connect(shareCount_, &QSpinBox::valueChanged, this, &DesktopWindow::createInputChanged);
    return createStates_;
}

QWidget *DesktopWindow::buildRecoverPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto *title = label(QStringLiteral("Recover a secret"));
    QFont titleFont = title->font();
    titleFont.setPointSizeF(22);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    layout->addWidget(title);
    layout->addWidget(label(QStringLiteral("Paste complete, distinct Words shares. Recovery starts only when you choose Recover."), true));

    auto *group = surface();
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(20, 16, 20, 16);
    groupLayout->setSpacing(11);
    recoveryCount_ = label(QStringLiteral("No recovery shares pasted"));
    recoveryCount_->setObjectName(QStringLiteral("recoveryCount"));
    groupLayout->addWidget(recoveryCount_);
    auto *pasteActions = new QHBoxLayout;
    pasteButton_ = new QPushButton(QStringLiteral("Paste share(s)"));
    pasteButton_->setObjectName(QStringLiteral("pasteRecoveryButton"));
    pasteButton_->setAccessibleName(QStringLiteral("Paste recovery shares from clipboard"));
    removeButton_ = new QPushButton(QStringLiteral("Remove last"));
    removeButton_->setObjectName(QStringLiteral("removeRecoveryButton"));
    removeButton_->setEnabled(false);
    pasteActions->addWidget(pasteButton_);
    pasteActions->addWidget(removeButton_);
    pasteActions->addStretch();
    groupLayout->addLayout(pasteActions);

    auto *action = new QHBoxLayout;
    recoveryStatus_ = label(QStringLiteral("Add enough shares to recover."), true);
    recoveryStatus_->setObjectName(QStringLiteral("recoveryStatus"));
    action->addWidget(recoveryStatus_, 1);
    recoverButton_ = new QPushButton(QStringLiteral("Recover"));
    recoverButton_->setObjectName(QStringLiteral("recoverButton"));
    recoverButton_->setEnabled(false);
    action->addWidget(recoverButton_);
    groupLayout->addLayout(action);

    recoveryResult_ = new QWidget;
    recoveryResult_->setObjectName(QStringLiteral("recoveryResult"));
    auto *resultLayout = new QVBoxLayout(recoveryResult_);
    resultLayout->setContentsMargins(0, 8, 0, 0);
    resultLayout->addWidget(label(QStringLiteral("Recovered text")));
    recoveredDisplay_ = new QPlainTextEdit;
    recoveredDisplay_->setObjectName(QStringLiteral("recoveredText"));
    recoveredDisplay_->setAccessibleName(QStringLiteral("Recovered text"));
    recoveredDisplay_->setReadOnly(true);
    recoveredDisplay_->setUndoRedoEnabled(false);
    recoveredDisplay_->setFixedHeight(130);
    configureEditor(recoveredDisplay_);
    resultLayout->addWidget(recoveredDisplay_);
    copyRecovered_ = new QPushButton(QStringLiteral("Copy recovered text"));
    copyRecovered_->setObjectName(QStringLiteral("copyRecoveredButton"));
    resultLayout->addWidget(copyRecovered_, 0, Qt::AlignRight);
    recoveryResult_->hide();
    groupLayout->addWidget(recoveryResult_);
    layout->addWidget(group);
    layout->addStretch();

    connect(pasteButton_, &QPushButton::clicked, this, &DesktopWindow::pasteRecovery);
    connect(removeButton_, &QPushButton::clicked, this, &DesktopWindow::removeLastRecovery);
    connect(recoverButton_, &QPushButton::clicked, this, &DesktopWindow::recover);
    connect(copyRecovered_, &QPushButton::clicked, this, [this] {
        pending_ = Pending::CopyRecovered;
        emit requestRecoveredText(generation_);
    });
    return page;
}

void DesktopWindow::switchMode(int index) {
    if (pages_ == nullptr || index < 0 || index > 1)
        return;
    modeSelector_->blockSignals(true);
    modeSelector_->setCurrentIndex(index);
    modeSelector_->blockSignals(false);
    pages_->setCurrentIndex(index);
    startOver();
}

void DesktopWindow::startOver() {
    nextGeneration();
    pending_ = Pending::Reset;
    clearVisibleState();
    setBusy(true);
    queueCurrentReset();
}

void DesktopWindow::createShares() {
    const QByteArray secret = secretInput_->exactUtf8();
    const quint64 requestGeneration = nextGeneration();
    pending_ = Pending::Create;
    setBusy(true);
    createStatus_->setText(QStringLiteral("Creating shares…"));
    emit requestCreate(requestGeneration, secret, static_cast<quint8>(threshold_->value()),
                       static_cast<quint8>(shareCount_->value()));
}

void DesktopWindow::pasteRecovery() {
    const ClipboardRead paste = readClipboardUtf8(kMaximumPasteBytes);
    if (paste.status != ClipboardRead::Status::Ok) {
        recoveryStatus_->setText(paste.status == ClipboardRead::Status::TooLarge
                                     ? QStringLiteral("This paste exceeds the 16 MiB limit.")
                                     : QStringLiteral("Clipboard does not contain valid UTF-8 share text."));
        return;
    }
    recoveryResult_->hide();
    recoveredDisplay_->clear();
    pending_ = Pending::Inspect;
    setBusy(true);
    recoveryStatus_->setText(QStringLiteral("Checking all pasted shares…"));
    emit requestAddRecovery(nextGeneration(), paste.bytes);
}

void DesktopWindow::removeLastRecovery() {
    if (recoveryBatchCount_ == 0)
        return;
    recoveryResult_->hide();
    recoveredDisplay_->clear();
    pending_ = Pending::Inspect;
    setBusy(true);
    emit requestRemoveRecovery(nextGeneration(), recoveryBatchCount_ - 1);
}

void DesktopWindow::recover() {
    recoveryResult_->hide();
    recoveredDisplay_->clear();
    pending_ = Pending::Recover;
    setBusy(true);
    recoveryStatus_->setText(QStringLiteral("Recovering…"));
    emit requestRecover(generation_);
}

void DesktopWindow::operationFinished(quint64 generation, int status, quint8 threshold,
                                      quint16 shareCount, quint16 suppliedCount,
                                      quint16 batchCount, bool ready) {
    if (generation != generation_) {
        if (pending_ == Pending::Reset)
            queueCurrentReset();
        return;
    }
    setBusy(false);
    if (pending_ == Pending::Reset) {
        pending_ = Pending::None;
        return;
    }
    if (pending_ == Pending::Create) {
        pending_ = Pending::None;
        if (status == statusCode(Status::Ok))
            showCreated(threshold, shareCount);
        else
            createStatus_->setText(statusText(status));
    } else if (pending_ == Pending::Inspect) {
        recoveryBatchCount_ = batchCount;
        recoveryCount_->setText(batchCount == 0
                                    ? QStringLiteral("No recovery shares pasted")
                                    : QStringLiteral("%1 paste(s), %2 valid share(s)").arg(batchCount).arg(suppliedCount));
        removeButton_->setEnabled(batchCount > 0);
        setRecoveryStatus(status, threshold, suppliedCount, ready);
    }
    pending_ = Pending::None;
}

void DesktopWindow::bytesFinished(quint64 generation, int status, QByteArray bytes, int purpose,
                                  quint16 index) {
    if (generation != generation_) {
        bytes.fill('\0');
        return;
    }
    setBusy(false);
    if (status != statusCode(Status::Ok)) {
        if (purpose == 0) {
            createStatus_->setText(statusText(status));
        } else {
            recoveryStatus_->setText(statusText(status));
            recoverButton_->setEnabled(purpose == 1);
        }
        bytes.fill('\0');
        return;
    }
    if (purpose == 0) {
        if (writeClipboardUtf8(bytes))
            createStatus_->setText(QStringLiteral("Share %1 copied.").arg(index + 1));
    } else if (purpose == 1) {
        recoveredDisplay_->setPlainText(QString::fromUtf8(bytes.constData(), bytes.size()));
        recoveryResult_->show();
        recoverButton_->setEnabled(true);
        recoveryStatus_->setText(QStringLiteral("Recovered exact UTF-8 text."));
    } else if (writeClipboardUtf8(bytes)) {
        recoveryStatus_->setText(QStringLiteral("Recovered text copied."));
    }
    bytes.fill('\0');
    pending_ = Pending::None;
}

void DesktopWindow::closeEvent(QCloseEvent *event) {
    ++generation_;
    clearVisibleState();
    event->accept();
}

void DesktopWindow::clearVisibleState() {
    if (secretInput_ != nullptr)
        secretInput_->clearExact();
    if (threshold_ != nullptr)
        threshold_->setValue(2);
    if (shareCount_ != nullptr)
        shareCount_->setValue(3);
    if (createStates_ != nullptr)
        createStates_->setCurrentIndex(0);
    if (createStatus_ != nullptr)
        createStatus_->setText(QStringLiteral("2 of 3 · Words"));
    recoveryBatchCount_ = 0;
    if (recoveryCount_ != nullptr)
        recoveryCount_->setText(QStringLiteral("No recovery shares pasted"));
    if (recoveryStatus_ != nullptr)
        recoveryStatus_->setText(QStringLiteral("Add enough shares to recover."));
    if (removeButton_ != nullptr)
        removeButton_->setEnabled(false);
    if (recoverButton_ != nullptr)
        recoverButton_->setEnabled(false);
    if (recoveryResult_ != nullptr)
        recoveryResult_->hide();
    if (recoveredDisplay_ != nullptr)
        recoveredDisplay_->clear();
}

void DesktopWindow::createInputChanged() {
    nextGeneration();
    createStatus_->setText(
        QStringLiteral("%1 of %2 · Words").arg(threshold_->value()).arg(shareCount_->value()));
    if (pending_ == Pending::Create) {
        pending_ = Pending::Reset;
        setBusy(true);
        queueCurrentReset();
    }
}

void DesktopWindow::queueCurrentReset() {
    if (resetRequested_ && requestedResetGeneration_ == generation_)
        return;
    resetRequested_ = true;
    requestedResetGeneration_ = generation_;
    emit requestReset(generation_);
}

void DesktopWindow::showCreated(quint8 threshold, quint16 shareCount) {
    secretInput_->clearExact();
    createdTitle_->setText(QStringLiteral("%1 recovery shares").arg(shareCount));
    delete createdRows_->layout();
    auto *rows = new QVBoxLayout(createdRows_);
    rows->setContentsMargins(16, 8, 12, 8);
    rows->setSpacing(2);
    for (quint16 index = 0; index < shareCount; ++index) {
        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(label(QStringLiteral("Recovery share %1").arg(index + 1)));
        rowLayout->addStretch();
        auto *copy = new QPushButton(QStringLiteral("Copy"));
        copy->setObjectName(QStringLiteral("copyShare%1").arg(index + 1));
        copy->setAccessibleName(QStringLiteral("Copy recovery share %1").arg(index + 1));
        connect(copy, &QPushButton::clicked, this, [this, index] {
            pending_ = Pending::CopyShare;
            setBusy(true);
            emit requestEncodeShare(generation_, index);
        });
        rowLayout->addWidget(copy);
        rows->addWidget(row);
    }
    rows->addWidget(label(QStringLiteral("%1 of %2 · Words").arg(threshold).arg(shareCount), true));
    createStates_->setCurrentIndex(1);
    createStatus_->setText(QStringLiteral("Shares created in memory."));
}

void DesktopWindow::setBusy(bool busy) {
    if (createButton_ != nullptr)
        createButton_->setEnabled(!busy);
    if (pasteButton_ != nullptr)
        pasteButton_->setEnabled(!busy);
    if (recoverButton_ != nullptr && busy)
        recoverButton_->setEnabled(false);
}

void DesktopWindow::setRecoveryStatus(int status, quint8 threshold, quint16 suppliedCount, bool ready) {
    recoverButton_->setEnabled(ready);
    if (ready) {
        recoveryStatus_->setText(QStringLiteral("Ready: %1 of %2 required shares supplied.")
                                     .arg(suppliedCount)
                                     .arg(threshold));
    } else if (status == statusCode(Status::NotEnoughShares) && suppliedCount > 0) {
        recoveryStatus_->setText(QStringLiteral("%1 of %2 required shares supplied.")
                                     .arg(suppliedCount)
                                     .arg(threshold));
    } else {
        recoveryStatus_->setText(statusText(status));
    }
}

QString DesktopWindow::statusText(int status) {
    if (status == statusCode(Status::EmptySecret))
        return QStringLiteral("Enter at least one UTF-8 byte. Whitespace is allowed.");
    if (status == statusCode(Status::InvalidThreshold))
        return QStringLiteral("Use a threshold from 1 through the share count.");
    if (status == statusCode(Status::SecretTooLarge))
        return QStringLiteral("The secret exceeds the 1 MiB limit.");
    if (status == statusCode(Status::LogicalVolumeTooLarge))
        return QStringLiteral("Secret size × supplied or requested shares exceeds 16 MiB.");
    if (status == statusCode(Status::PasteTooLarge))
        return QStringLiteral("This paste exceeds the 16 MiB limit.");
    if (status == statusCode(Status::RetainedInputTooLarge))
        return QStringLiteral("Retained recovery input would exceed 160 MiB.");
    if (status == statusCode(Status::TokenLimit))
        return QStringLiteral("This paste contains too many words.");
    if (status == statusCode(Status::DuplicateShare))
        return QStringLiteral("Remove the duplicate recovery share.");
    if (status == statusCode(Status::MixedShareSet))
        return QStringLiteral("All recovery shares must come from one compatible set.");
    if (status == statusCode(Status::UnsupportedInput))
        return QStringLiteral("This slice accepts V2 unprotected Words shares only.");
    if (status == statusCode(Status::MalformedInput))
        return QStringLiteral("A complete Words recovery share could not be decoded.");
    if (status == statusCode(Status::NotEnoughShares))
        return QStringLiteral("Add enough distinct recovery shares.");
    if (status == statusCode(Status::InvalidUtf8))
        return QStringLiteral("Input or recovered output is not valid UTF-8.");
    if (status == statusCode(Status::IntegrityFailure))
        return QStringLiteral("Recovery integrity verification failed. No output was shown.");
    if (status == statusCode(Status::InternalPanic))
        return QStringLiteral("Processing stopped safely. Start over before retrying.");
    return QStringLiteral("The operation could not be completed.");
}

quint64 DesktopWindow::nextGeneration() {
    return ++generation_;
}
