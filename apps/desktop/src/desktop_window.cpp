#include "desktop_window.h"

#include "clipboard.h"
#include "exact_text_edit.h"
#include "file_io.h"
#include "rust_worker.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringDecoder>
#include <QUtf8StringView>
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <limits>
#include <utility>

namespace {
constexpr qsizetype kMaximumPassphraseBytes = 1'048'576;
constexpr qsizetype kMaximumRecoveryFieldBytes = 16 * 1'048'576;
constexpr qsizetype kMaximumRetainedRecoveryBytes = 160 * 1'048'576;
constexpr qsizetype kMaximumGeneratedPresentationBytes = 160 * 1'048'576;
constexpr qsizetype kGeneratedPresentationExpansion = 4;
constexpr qsizetype kGeneratedWorstCasePerPacketByte = 16;
constexpr qsizetype kMaximumRecoveryFields = 255;
constexpr int kShareClipboardPurpose = 0;
constexpr int kRecoveredDisplayPurpose = 1;
constexpr int kRecoveredClipboardPurpose = 2;
constexpr int kShareDisplayPurpose = 3;
constexpr int kShareSavePurpose = 4;
constexpr int kRecoveredSavePurpose = 5;
constexpr qsizetype kMaximumSecretBytes = 1'048'576;
constexpr qsizetype kMaximumShareFileBytes = 16 * 1'048'576;

int statusCode(Status status) {
    return static_cast<int>(static_cast<std::uint8_t>(status));
}

QString encodingName(int encoding) {
    switch (encoding) {
    case 1:
        return QStringLiteral("Base64url");
    case 2:
        return QStringLiteral("Base58check");
    case 3:
        return QStringLiteral("Words");
    case 4:
        return QStringLiteral("BIP-39");
    default:
        return QStringLiteral("Auto");
    }
}

void populateEncodingChoices(QComboBox *combo, bool includeAuto) {
    if (includeAuto)
        combo->addItem(QStringLiteral("Auto"), 0);
    combo->addItem(QStringLiteral("Base64url"), 1);
    combo->addItem(QStringLiteral("Base58check"), 2);
    combo->addItem(QStringLiteral("Words"), 3);
    combo->addItem(QStringLiteral("BIP-39"), 4);
}

QColor blend(const QColor &first, const QColor &second, qreal amount) {
    return QColor::fromRgbF(first.redF() * (1.0 - amount) + second.redF() * amount,
                            first.greenF() * (1.0 - amount) + second.greenF() * amount,
                            first.blueF() * (1.0 - amount) + second.blueF() * amount, 1.0);
}

QFont pointFont(const QWidget *widget, qreal points, QFont::Weight weight = QFont::Normal) {
    QFont font = widget->font();
    font.setPointSizeF(points);
    font.setWeight(weight);
    return font;
}

class DisclosureButton final : public QToolButton {
public:
    explicit DisclosureButton(QWidget *parent = nullptr) : QToolButton(parent) {
        setCheckable(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAutoRaise(false);
        setFixedSize(112, 30);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor window = palette().color(QPalette::Window);
        const QColor foreground = palette().color(QPalette::ButtonText);
        const bool dark = window.lightnessF() < 0.5;
        const QRectF bounds = QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75);

        QColor edge = foreground;
        edge.setAlphaF(hasFocus() ? 0.32 : (dark ? 0.16 : 0.11));
        painter.setPen(QPen(edge, hasFocus() ? 1.5 : 1.0));
        painter.setBrush(blend(window, foreground, underMouse() ? (dark ? 0.12 : 0.075)
                                                                : (dark ? 0.08 : 0.045)));
        painter.drawRoundedRect(bounds, 7, 7);

        QPainterPath chevron;
        if (isChecked()) {
            chevron.moveTo(10, 12);
            chevron.lineTo(15, 17);
            chevron.lineTo(20, 12);
        } else {
            chevron.moveTo(12, 10);
            chevron.lineTo(17, 15);
            chevron.lineTo(12, 20);
        }
        QPen chevronPen(foreground, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(chevronPen);
        painter.drawPath(chevron);

        painter.setPen(foreground);
        painter.setFont(pointFont(this, 11.0, QFont::Medium));
        painter.drawText(QRectF(28, 0, width() - 32, height()),
                         Qt::AlignVCenter | Qt::AlignLeft, text());
    }
};

class ModeSelector final : public QTabBar {
public:
    explicit ModeSelector(QWidget *parent = nullptr) : QTabBar(parent) {
        addTab(QStringLiteral("Split"));
        addTab(QStringLiteral("Combine"));
        setObjectName(QStringLiteral("modeSelector"));
        setAccessibleName(QStringLiteral("Choose Split or Combine"));
        setFocusPolicy(Qt::StrongFocus);
        setUsesScrollButtons(false);
        setExpanding(true);
        setDrawBase(false);
        setFixedSize(244, 30);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor window = palette().color(QPalette::Window);
        const QColor text = palette().color(QPalette::WindowText);
        const bool dark = window.lightnessF() < 0.5;
        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor edge = text;
        edge.setAlphaF(dark ? 0.12 : 0.08);
        painter.setPen(QPen(edge, 1));
        painter.setBrush(blend(window, text, dark ? 0.10 : 0.07));
        painter.drawRoundedRect(bounds, 8, 8);

        const qreal half = width() / 2.0;
        const QRectF selected(currentIndex() == 0 ? 2.5 : half + 0.5, 2.5, half - 3.0,
                              height() - 5.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(blend(window, text, dark ? 0.19 : 0.015));
        painter.drawRoundedRect(selected, 6, 6);

        painter.setPen(text);
        painter.setFont(pointFont(this, 11.5, currentIndex() == 0 ? QFont::DemiBold : QFont::Medium));
        painter.drawText(QRectF(0, 0, half, height()), Qt::AlignCenter, tabText(0));
        painter.setFont(pointFont(this, 11.5, currentIndex() == 1 ? QFont::DemiBold : QFont::Medium));
        painter.drawText(QRectF(half, 0, half, height()), Qt::AlignCenter, tabText(1));
    }

    void mousePressEvent(QMouseEvent *event) override {
        setCurrentIndex(event->position().x() < width() / 2.0 ? 0 : 1);
        setFocus(Qt::MouseFocusReason);
        event->accept();
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Up) {
            setCurrentIndex(0);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Down) {
            setCurrentIndex(1);
            event->accept();
            return;
        }
        QTabBar::keyPressEvent(event);
    }
};

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
    widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    return widget;
}

void configureEditor(QPlainTextEdit *editor) {
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor->setFrameShape(QFrame::NoFrame);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
}

QIcon copyIcon(const QWidget *widget) {
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(widget->palette().color(QPalette::ButtonText), 2.0, Qt::SolidLine, Qt::RoundCap,
             Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(8, 8, 10, 10), 0.8, 0.8);
    QPainterPath rear;
    rear.moveTo(5, 14);
    rear.lineTo(4, 14);
    rear.quadTo(3, 14, 3, 13);
    rear.lineTo(3, 4);
    rear.quadTo(3, 3, 4, 3);
    rear.lineTo(13, 3);
    rear.quadTo(14, 3, 14, 4);
    rear.lineTo(14, 5);
    painter.drawPath(rear);
    return QIcon(pixmap);
}

void configureCopyButton(QPushButton *button, const QString &accessibleName) {
    button->setText({});
    button->setIcon(copyIcon(button));
    button->setIconSize(QSize(20, 20));
    button->setAccessibleName(accessibleName);
    button->setToolTip(accessibleName);
    button->setAutoDefault(false);
    button->setFlat(true);
    button->setFixedSize(32, 30);
}

QWidget *centeredPage(QWidget *content) {
    auto *viewport = new QWidget;
    auto *layout = new QHBoxLayout(viewport);
    layout->setContentsMargins(0, 16, 0, 6);
    layout->addStretch();
    content->setMaximumWidth(620);
    layout->addWidget(content, 1, Qt::AlignTop);
    layout->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(viewport);
    return scroll;
}
} // namespace

DesktopWindow::DesktopWindow(QWidget *parent) : QMainWindow(parent), fileIo_(std::make_shared<FileIo>()) {
    openFileDialog_ = [this] { return QFileDialog::getOpenFileName(this, QStringLiteral("Choose Secret file")); };
    openFilesDialog_ = [this] { return QFileDialog::getOpenFileNames(this, QStringLiteral("Load Recovery share files")); };
    saveFileDialog_ = [this] { return QFileDialog::getSaveFileName(this, QStringLiteral("Save exact bytes")); };
    setWindowTitle(QStringLiteral("Safeparts"));
    resize(740, 590);
    setMinimumSize(620, 480);

    thread_ = new QThread(this);
    worker_ = new RustWorker;
    worker_->moveToThread(thread_);
    connect(this, &DesktopWindow::requestReset, worker_, &RustWorker::reset, Qt::QueuedConnection);
    connect(this, &DesktopWindow::requestCreate, worker_, &RustWorker::create, Qt::QueuedConnection);
    connect(this, &DesktopWindow::requestEncodeShare, worker_, &RustWorker::encodeShare, Qt::QueuedConnection);
    connect(this, &DesktopWindow::requestReplaceRecovery, worker_, &RustWorker::replaceRecovery,
            Qt::QueuedConnection);
    connect(this, &DesktopWindow::requestRecover, worker_, &RustWorker::recover, Qt::QueuedConnection);
    connect(this, &DesktopWindow::requestRecoveredBytes, worker_, &RustWorker::recoveredBytes, Qt::QueuedConnection);
    connect(worker_, &RustWorker::operationFinished, this, &DesktopWindow::operationFinished);
    connect(worker_, &RustWorker::bytesFinished, this, &DesktopWindow::bytesFinished);
    thread_->start();

    recoverySyncTimer_ = new QTimer(this);
    recoverySyncTimer_->setSingleShot(true);
    recoverySyncTimer_->setInterval(250);
    connect(recoverySyncTimer_, &QTimer::timeout, this, &DesktopWindow::synchronizeRecoveryFields);

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

void DesktopWindow::setFileServicesForTests(std::shared_ptr<FileIo> fileIo, OpenFileDialog openFile,
                                            OpenFilesDialog openFiles, SaveFileDialog saveFile) {
    fileIo_ = std::move(fileIo);
    openFileDialog_ = std::move(openFile);
    openFilesDialog_ = std::move(openFiles);
    saveFileDialog_ = std::move(saveFile);
}

void DesktopWindow::buildUi() {
    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(28, 18, 28, 18);
    root->setSpacing(14);

    auto *header = new QHBoxLayout;
    header->setSpacing(8);
    modeSelector_ = new ModeSelector;
    header->addWidget(modeSelector_);
    header->addStretch();
    auto *help = new QToolButton;
    help->setObjectName(QStringLiteral("helpButton"));
    help->setText(QStringLiteral("Help"));
    help->setAutoRaise(true);
    help->setToolButtonStyle(Qt::ToolButtonTextOnly);
    help->setStyleSheet(QStringLiteral("QToolButton { border: 0; padding: 5px 7px; background: transparent; }"));
    auto *startOverButton = new QToolButton;
    startOverButton->setObjectName(QStringLiteral("startOverButton"));
    startOverButton->setText(QStringLiteral("Start over"));
    startOverButton->setAutoRaise(true);
    startOverButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    startOverButton->setStyleSheet(
        QStringLiteral("QToolButton { border: 0; padding: 5px 7px; background: transparent; }"));
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
        "QPlainTextEdit:focus { border: 2px solid palette(highlight); padding: 7px; }"));

    connect(modeSelector_, &QTabBar::currentChanged, this, &DesktopWindow::switchMode);
    connect(startOverButton, &QToolButton::clicked, this, &DesktopWindow::startOver);
    connect(help, &QToolButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("Safeparts help"),
                                 QStringLiteral("Split keeps one share set in memory. Store Recovery shares separately. "
                                                "Combine auto-detects or manually selects one Share format. Passphrases are never included in Recovery shares or exports."));
    });
}

QWidget *DesktopWindow::buildCreatePage() {
    auto *entry = new QWidget;
    auto *entryLayout = new QVBoxLayout(entry);
    entryLayout->setContentsMargins(0, 0, 0, 0);
    entryLayout->setSpacing(12);
    entryLayout->setSizeConstraint(QLayout::SetMinimumSize);
    auto *title = label(QStringLiteral("Split"));
    title->setObjectName(QStringLiteral("createPageTitle"));
    QFont titleFont = title->font();
    titleFont.setPointSizeF(22);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    entryLayout->addWidget(title);

    auto *group = surface();
    group->setObjectName(QStringLiteral("createSurface"));
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(20, 16, 20, 16);
    groupLayout->setSpacing(10);
    auto *secretLabel = label(QStringLiteral("Secret"));
    groupLayout->addWidget(secretLabel);
    secretInput_ = new ExactTextEdit;
    secretInput_->setObjectName(QStringLiteral("secretInput"));
    secretInput_->setAccessibleName(QStringLiteral("Secret"));
    secretInput_->setPlaceholderText(QStringLiteral("Type or paste text"));
    secretInput_->setFixedHeight(130);
    configureEditor(secretInput_);
    secretLabel->setBuddy(secretInput_);
    groupLayout->addWidget(secretInput_);
    secretFileMetadata_ = label({}, true);
    secretFileMetadata_->setObjectName(QStringLiteral("secretFileMetadata"));
    secretFileMetadata_->hide();
    groupLayout->addWidget(secretFileMetadata_);
    auto *sourceActions = new QHBoxLayout;
    chooseSecretFileButton_ = new QPushButton(QStringLiteral("Choose file…"));
    chooseSecretFileButton_->setObjectName(QStringLiteral("chooseSecretFileButton"));
    useTextSecretButton_ = new QPushButton(QStringLiteral("Use text instead"));
    useTextSecretButton_->setObjectName(QStringLiteral("useTextSecretButton"));
    useTextSecretButton_->hide();
    sourceActions->addWidget(chooseSecretFileButton_);
    sourceActions->addWidget(useTextSecretButton_);
    sourceActions->addStretch();
    groupLayout->addLayout(sourceActions);

    auto *settings = new QGridLayout;
    settings->setHorizontalSpacing(28);
    settings->setVerticalSpacing(6);
    auto *thresholdLabel = label(QStringLiteral("Minimum shares to recover (k)"));
    threshold_ = new QSpinBox;
    threshold_->setObjectName(QStringLiteral("thresholdInput"));
    threshold_->setAccessibleName(QStringLiteral("Minimum shares to recover (k)"));
    threshold_->setRange(1, 255);
    threshold_->setValue(2);
    threshold_->setAlignment(Qt::AlignCenter);
    threshold_->setFixedSize(112, 32);
    thresholdLabel->setBuddy(threshold_);
    settings->addWidget(thresholdLabel, 0, 0);
    settings->addWidget(threshold_, 1, 0, Qt::AlignLeft);
    auto *shareCountLabel = label(QStringLiteral("Total shares to create (n)"));
    shareCount_ = new QSpinBox;
    shareCount_->setObjectName(QStringLiteral("shareCountInput"));
    shareCount_->setAccessibleName(QStringLiteral("Total shares to create (n)"));
    shareCount_->setRange(1, 255);
    shareCount_->setValue(3);
    shareCount_->setAlignment(Qt::AlignCenter);
    shareCount_->setFixedSize(112, 32);
    shareCountLabel->setBuddy(shareCount_);
    settings->addWidget(shareCountLabel, 0, 1);
    settings->addWidget(shareCount_, 1, 1, Qt::AlignLeft);
    settings->setColumnStretch(0, 1);
    settings->setColumnStretch(1, 1);
    groupLayout->addLayout(settings);

    auto *additionalOptions = new DisclosureButton;
    additionalOptions->setObjectName(QStringLiteral("additionalOptionsButton"));
    additionalOptions->setText(QStringLiteral("Advanced"));
    additionalOptions->setAccessibleName(QStringLiteral("Advanced options"));
    additionalOptions->setArrowType(Qt::RightArrow);
    groupLayout->addWidget(additionalOptions, 0, Qt::AlignLeft);
    auto *additionalOptionsPanel = new QWidget;
    additionalOptionsPanel->setObjectName(QStringLiteral("additionalOptionsPanel"));
    auto *additionalLayout = new QHBoxLayout(additionalOptionsPanel);
    additionalLayout->setContentsMargins(0, 0, 0, 0);
    auto *formatLabel = label(QStringLiteral("Share format"));
    createEncoding_ = new QComboBox;
    createEncoding_->setObjectName(QStringLiteral("createEncoding"));
    createEncoding_->setAccessibleName(QStringLiteral("Share format"));
    populateEncodingChoices(createEncoding_, false);
    createEncoding_->setCurrentIndex(2);
    formatLabel->setBuddy(createEncoding_);
    additionalLayout->addWidget(formatLabel);
    additionalLayout->addWidget(createEncoding_, 1);
    additionalOptionsPanel->hide();
    groupLayout->addWidget(additionalOptionsPanel);

    protectWithPassphrase_ = new QCheckBox(QStringLiteral("Protect with passphrase"));
    protectWithPassphrase_->setObjectName(QStringLiteral("protectWithPassphrase"));
    groupLayout->addWidget(protectWithPassphrase_);
    createPassphrasePanel_ = new QWidget;
    auto *passphraseLayout = new QGridLayout(createPassphrasePanel_);
    passphraseLayout->setContentsMargins(0, 0, 0, 0);
    auto *passphraseLabel = label(QStringLiteral("Passphrase"));
    createPassphrase_ = new ExactTextEdit(nullptr, kMaximumPassphraseBytes, true, false);
    createPassphrase_->setObjectName(QStringLiteral("createPassphrase"));
    createPassphrase_->setAccessibleName(QStringLiteral("Passphrase input (contents hidden)"));
    createPassphrase_->setFixedHeight(40);
    auto *confirmationLabel = label(QStringLiteral("Confirm passphrase"));
    confirmPassphrase_ = new ExactTextEdit(nullptr, kMaximumPassphraseBytes, true, false);
    confirmPassphrase_->setObjectName(QStringLiteral("confirmPassphrase"));
    confirmPassphrase_->setAccessibleName(QStringLiteral("Passphrase confirmation (contents hidden)"));
    confirmPassphrase_->setFixedHeight(40);
    passphraseLabel->setBuddy(createPassphrase_);
    confirmationLabel->setBuddy(confirmPassphrase_);
    passphraseLayout->addWidget(passphraseLabel, 0, 0);
    passphraseLayout->addWidget(createPassphrase_, 0, 1);
    passphraseLayout->addWidget(confirmationLabel, 1, 0);
    passphraseLayout->addWidget(confirmPassphrase_, 1, 1);
    createPassphrasePanel_->hide();
    groupLayout->addWidget(createPassphrasePanel_);
    connect(protectWithPassphrase_, &QCheckBox::toggled, this, [this](bool enabled) {
        createPassphrasePanel_->setVisible(enabled);
        if (!enabled) {
            createPassphrase_->clearExact();
            confirmPassphrase_->clearExact();
        }
        createInputChanged();
    });
    connect(createPassphrase_, &ExactTextEdit::exactTextChanged, this, &DesktopWindow::createInputChanged);
    connect(confirmPassphrase_, &ExactTextEdit::exactTextChanged, this, &DesktopWindow::createInputChanged);
    connect(additionalOptions, &QToolButton::toggled, this,
            [additionalOptions, additionalOptionsPanel](bool shown) {
                additionalOptions->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
                additionalOptionsPanel->setVisible(shown);
            });

    auto *action = new QHBoxLayout;
    action->setSpacing(12);
    createStatus_ = label(QStringLiteral("2 of 3 · Words"), true);
    createStatus_->setObjectName(QStringLiteral("createStatus"));
    action->addWidget(createStatus_, 1);
    createButton_ = new QPushButton(QStringLiteral("Split"));
    createButton_->setObjectName(QStringLiteral("createButton"));
    createButton_->setDefault(true);
    createButton_->setFixedHeight(32);
    action->addWidget(createButton_);
    groupLayout->addLayout(action);
    group->setMinimumHeight(groupLayout->sizeHint().height());
    entryLayout->addWidget(group);

    createdResult_ = new QWidget;
    createdResult_->setObjectName(QStringLiteral("createdResult"));
    auto *createdLayout = new QVBoxLayout(createdResult_);
    createdLayout->setContentsMargins(0, 6, 0, 0);
    createdLayout->setSpacing(10);
    createdLayout->setSizeConstraint(QLayout::SetMinimumSize);
    createdTitle_ = label(QStringLiteral("Recovery shares"));
    createdTitle_->setObjectName(QStringLiteral("createdTitle"));
    QFont createdTitleFont = createdTitle_->font();
    createdTitleFont.setPointSizeF(16);
    createdTitleFont.setWeight(QFont::DemiBold);
    createdTitle_->setFont(createdTitleFont);
    createdLayout->addWidget(createdTitle_);
    createdRows_ = surface();
    createdRows_->setObjectName(QStringLiteral("createdShares"));
    createdLayout->addWidget(createdRows_);
    createdResult_->hide();
    entryLayout->addWidget(createdResult_);

    connect(createButton_, &QPushButton::clicked, this, &DesktopWindow::createShares);
    connect(chooseSecretFileButton_, &QPushButton::clicked, this, &DesktopWindow::chooseSecretFile);
    connect(useTextSecretButton_, &QPushButton::clicked, this, &DesktopWindow::useTextSecret);
    connect(secretInput_, &ExactTextEdit::exactTextChanged, this, &DesktopWindow::createInputChanged);
    connect(secretInput_, &ExactTextEdit::inputRejected, this, [this](int reason) {
        createStatus_->setText(reason == static_cast<int>(ClipboardRead::Status::TooLarge)
                                   ? QStringLiteral("Secret cannot exceed the 1 MiB UTF-8 limit.")
                                   : QStringLiteral("Secret must be valid UTF-8."));
    });
    connect(threshold_, &QSpinBox::valueChanged, this, &DesktopWindow::createInputChanged);
    connect(shareCount_, &QSpinBox::valueChanged, this, &DesktopWindow::createInputChanged);
    connect(createEncoding_, &QComboBox::currentIndexChanged, this,
            &DesktopWindow::createInputChanged);
    return entry;
}

QWidget *DesktopWindow::buildRecoverPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto *title = label(QStringLiteral("Combine"));
    title->setObjectName(QStringLiteral("recoverPageTitle"));
    QFont titleFont = title->font();
    titleFont.setPointSizeF(22);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto *group = surface();
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(20, 16, 20, 16);
    groupLayout->setSpacing(11);
    auto *sharesLabel = label(QStringLiteral("Shares"));
    QFont sharesFont = sharesLabel->font();
    sharesFont.setWeight(QFont::DemiBold);
    sharesLabel->setFont(sharesFont);
    groupLayout->addWidget(sharesLabel);

    auto *formatRow = new QHBoxLayout;
    auto *formatLabel = label(QStringLiteral("Share format"));
    recoveryEncoding_ = new QComboBox;
    recoveryEncoding_->setObjectName(QStringLiteral("recoveryEncoding"));
    recoveryEncoding_->setAccessibleName(QStringLiteral("Share format"));
    populateEncodingChoices(recoveryEncoding_, true);
    formatLabel->setBuddy(recoveryEncoding_);
    recoveryDetectedFormat_ = label(QStringLiteral("Detected after paste"), true);
    recoveryDetectedFormat_->setObjectName(QStringLiteral("detectedRecoveryEncoding"));
    formatRow->addWidget(formatLabel);
    formatRow->addWidget(recoveryEncoding_);
    formatRow->addWidget(recoveryDetectedFormat_, 1);
    groupLayout->addLayout(formatRow);

    auto *fields = new QWidget;
    fields->setObjectName(QStringLiteral("recoveryFields"));
    recoveryFieldsLayout_ = new QVBoxLayout(fields);
    recoveryFieldsLayout_->setContentsMargins(0, 0, 0, 0);
    recoveryFieldsLayout_->setSpacing(10);
    groupLayout->addWidget(fields);
    addRecoveryField();
    addRecoveryField();

    auto *fieldActions = new QHBoxLayout;
    addRecoveryButton_ = new QPushButton(QStringLiteral("Add share"));
    addRecoveryButton_->setObjectName(QStringLiteral("addRecoveryShareButton"));
    addRecoveryButton_->setFixedHeight(32);
    fieldActions->addWidget(addRecoveryButton_);
    loadRecoveryFilesButton_ = new QPushButton(QStringLiteral("Load share files…"));
    loadRecoveryFilesButton_->setObjectName(QStringLiteral("loadRecoveryFilesButton"));
    loadRecoveryFilesButton_->setFixedHeight(32);
    fieldActions->addWidget(loadRecoveryFilesButton_);
    fieldActions->addStretch();
    recoveryCount_ = label(QStringLiteral("0 of 2 Recovery shares entered"), true);
    recoveryCount_->setObjectName(QStringLiteral("recoveryCount"));
    fieldActions->addWidget(recoveryCount_);
    groupLayout->addLayout(fieldActions);

    auto *action = new QHBoxLayout;
    recoveryPassphrasePanel_ = new QWidget;
    recoveryPassphrasePanel_->setObjectName(QStringLiteral("recoveryPassphrasePanel"));
    auto *recoveryPassphraseLayout = new QHBoxLayout(recoveryPassphrasePanel_);
    recoveryPassphraseLayout->setContentsMargins(0, 0, 0, 0);
    auto *recoveryPassphraseLabel = label(QStringLiteral("Passphrase"));
    recoveryPassphrase_ = new ExactTextEdit(nullptr, kMaximumPassphraseBytes, true, false);
    recoveryPassphrase_->setObjectName(QStringLiteral("recoveryPassphrase"));
    recoveryPassphrase_->setAccessibleName(QStringLiteral("Passphrase input (contents hidden)"));
    recoveryPassphrase_->setFixedHeight(40);
    recoveryPassphraseLabel->setBuddy(recoveryPassphrase_);
    recoveryPassphraseLayout->addWidget(recoveryPassphraseLabel);
    recoveryPassphraseLayout->addWidget(recoveryPassphrase_, 1);
    recoveryPassphrasePanel_->hide();
    groupLayout->addWidget(recoveryPassphrasePanel_);
    connect(recoveryPassphrase_, &ExactTextEdit::exactTextChanged, this, &DesktopWindow::recoveryInputChanged);

    recoveryStatus_ = label(QStringLiteral("Share content is required."), true);
    recoveryStatus_->setObjectName(QStringLiteral("recoveryStatus"));
    action->addWidget(recoveryStatus_, 1);
    recoverButton_ = new QPushButton(QStringLiteral("Combine"));
    recoverButton_->setObjectName(QStringLiteral("recoverButton"));
    recoverButton_->setEnabled(false);
    recoverButton_->setDefault(true);
    recoverButton_->setFixedHeight(32);
    action->addWidget(recoverButton_);
    groupLayout->addLayout(action);

    recoveryResult_ = new QWidget;
    recoveryResult_->setObjectName(QStringLiteral("recoveryResult"));
    auto *resultLayout = new QVBoxLayout(recoveryResult_);
    resultLayout->setContentsMargins(0, 8, 0, 0);
    resultLayout->addWidget(label(QStringLiteral("Recovered secret")));
    recoveredDisplay_ = new QPlainTextEdit;
    recoveredDisplay_->setObjectName(QStringLiteral("recoveredText"));
    recoveredDisplay_->setAccessibleName(QStringLiteral("Recovered secret"));
    recoveredDisplay_->setReadOnly(true);
    recoveredDisplay_->setUndoRedoEnabled(false);
    recoveredDisplay_->setFixedHeight(130);
    configureEditor(recoveredDisplay_);
    resultLayout->addWidget(recoveredDisplay_);
    copyRecovered_ = new QPushButton;
    copyRecovered_->setObjectName(QStringLiteral("copyRecoveredButton"));
    configureCopyButton(copyRecovered_, QStringLiteral("Copy recovered Secret"));
    auto *resultActions = new QHBoxLayout;
    resultActions->addStretch();
    resultActions->addWidget(copyRecovered_);
    saveRecovered_ = new QPushButton(QStringLiteral("Save…"));
    saveRecovered_->setObjectName(QStringLiteral("saveRecoveredButton"));
    resultActions->addWidget(saveRecovered_);
    resultLayout->addLayout(resultActions);
    recoveryResult_->hide();
    groupLayout->addWidget(recoveryResult_);
    layout->addWidget(group);

    connect(addRecoveryButton_, &QPushButton::clicked, this, &DesktopWindow::addRecoveryField);
    connect(loadRecoveryFilesButton_, &QPushButton::clicked, this, &DesktopWindow::loadShareFiles);
    connect(recoveryEncoding_, &QComboBox::currentIndexChanged, this,
            &DesktopWindow::recoveryInputChanged);
    connect(recoverButton_, &QPushButton::clicked, this, &DesktopWindow::recover);
    connect(copyRecovered_, &QPushButton::clicked, this, [this] {
        if (pending_ != Pending::None)
            return;
        pending_ = Pending::CopyRecovered;
        setBusy(true);
        emit requestRecoveredBytes(generation_, kRecoveredClipboardPurpose);
    });
    connect(saveRecovered_, &QPushButton::clicked, this, [this] {
        if (pending_ != Pending::None)
            return;
        const QString destination = saveFileDialog_();
        if (destination.isEmpty())
            return;
        pendingDestination_ = destination;
        pending_ = Pending::SaveRecovered;
        setBusy(true);
        emit requestRecoveredBytes(generation_, kRecoveredSavePurpose);
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

void DesktopWindow::chooseSecretFile() {
    const QString path = openFileDialog_();
    if (path.isEmpty())
        return;
    const qsizetype logicalMaximum = (16 * 1'048'576) / shareCount_->value();
    const qsizetype maximum = std::min(kMaximumSecretBytes, logicalMaximum);
    FileIo::ReadResult result = fileIo_->readBounded(path, maximum);
    if (result.status != FileIo::Status::Ok) {
        createStatus_->setText(result.status == FileIo::Status::TooLarge
                                   ? QStringLiteral("Secret file exceeds the operation limit.")
                                   : QStringLiteral("Secret file could not be read."));
        return;
    }
    if (result.bytes.isEmpty()) {
        createStatus_->setText(QStringLiteral("Secret cannot be empty."));
        return;
    }
    secretInput_->blockSignals(true);
    secretInput_->clearExact();
    secretInput_->blockSignals(false);
    secretFileBytes_ = std::move(result.bytes);
    usesSecretFile_ = true;
    secretInput_->hide();
    secretFileMetadata_->setText(QStringLiteral("%1 · %2 bytes")
                                     .arg(QFileInfo(path).fileName())
                                     .arg(secretFileBytes_.size()));
    secretFileMetadata_->show();
    chooseSecretFileButton_->hide();
    useTextSecretButton_->show();
    createInputChanged();
}

void DesktopWindow::useTextSecret() {
    if (!usesSecretFile_)
        return;
    secretFileBytes_ = {};
    usesSecretFile_ = false;
    secretFileMetadata_->clear();
    secretFileMetadata_->hide();
    secretInput_->show();
    chooseSecretFileButton_->show();
    useTextSecretButton_->hide();
    createInputChanged();
}

void DesktopWindow::loadShareFiles() {
    const QStringList paths = openFilesDialog_();
    if (paths.isEmpty())
        return;
    qsizetype availableEmpty = 0;
    qsizetype retainedBytes = 0;
    for (ExactTextEdit *field : std::as_const(recoveryFields_)) {
        retainedBytes += field->exactUtf8Size();
        if (field->exactUtf8Size() == 0)
            ++availableEmpty;
    }
    if (paths.size() > availableEmpty + kMaximumRecoveryFields - recoveryFields_.size()) {
        recoveryStatus_->setText(QStringLiteral("At most 255 Recovery share batches are accepted."));
        return;
    }
    QList<SecureByteBuffer> acquired;
    for (const QString &path : paths) {
        FileIo::ReadResult result = fileIo_->readBounded(path, kMaximumShareFileBytes);
        if (result.status != FileIo::Status::Ok) {
            recoveryStatus_->setText(result.status == FileIo::Status::TooLarge
                                         ? QStringLiteral("A Recovery share file exceeds 16 MiB.")
                                         : QStringLiteral("Recovery share files could not be read."));
            return;
        }
        if (result.bytes.isEmpty()) {
            recoveryStatus_->setText(QStringLiteral("Recovery share files cannot be empty."));
            return;
        }
        const QUtf8StringView utf8(result.bytes.data(), result.bytes.size());
        if (!utf8.isValidUtf8()) {
            recoveryStatus_->setText(QStringLiteral("Recovery share files must be valid UTF-8."));
            return;
        }
        if (retainedBytes > kMaximumRetainedRecoveryBytes - result.bytes.size()) {
            recoveryStatus_->setText(QStringLiteral("Retained Recovery share input exceeds 160 MiB."));
            return;
        }
        retainedBytes += result.bytes.size();
        acquired.append(std::move(result.bytes));
    }
    for (const SecureByteBuffer &bytes : std::as_const(acquired)) {
        ExactTextEdit *target = nullptr;
        for (ExactTextEdit *field : std::as_const(recoveryFields_)) {
            if (field->exactUtf8Size() == 0) {
                target = field;
                break;
            }
        }
        if (target == nullptr) {
            addRecoveryField();
            target = recoveryFields_.last();
        }
        if (!target->setExactUtf8(bytes.view(), true)) {
            recoveryStatus_->setText(QStringLiteral("Recovery share file could not be retained."));
            return;
        }
    }
    recoverySyncTimer_->stop();
    synchronizeRecoveryFields();
}

void DesktopWindow::createShares() {
    QByteArray passphraseBytes = createPassphrase_->exactUtf8();
    QByteArray confirmationBytes = confirmPassphrase_->exactUtf8();
    if (protectWithPassphrase_->isChecked()
        && (passphraseBytes.isEmpty() || passphraseBytes != confirmationBytes)) {
        createStatus_->setText(QStringLiteral("Enter and confirm the same nonempty passphrase."));
        passphraseBytes.fill(0);
        confirmationBytes.fill(0);
        return;
    }
    if (passphraseBytes.size() > kMaximumPassphraseBytes) {
        createStatus_->setText(QStringLiteral("Passphrase cannot exceed the 1 MiB UTF-8 limit."));
        passphraseBytes.fill(0);
        confirmationBytes.fill(0);
        return;
    }
    confirmationBytes.fill(0);
    SecureByteBuffer passphrase = SecureByteBuffer::take(
        protectWithPassphrase_->isChecked() ? std::move(passphraseBytes) : QByteArray());
    passphraseBytes.fill(0);
    SecureByteBuffer secret = usesSecretFile_
                                  ? secretFileBytes_
                                  : SecureByteBuffer::take(secretInput_->exactUtf8());
    const quint64 requestGeneration = nextGeneration();
    pending_ = Pending::Create;
    setBusy(true);
    createStatus_->setText(QStringLiteral("Working…"));
    emit requestCreate(requestGeneration, std::move(secret), static_cast<quint8>(threshold_->value()),
                       static_cast<quint8>(shareCount_->value()),
                       createEncoding_->currentData().toInt(), std::move(passphrase));
}

void DesktopWindow::addRecoveryField() {
    if (recoveryFieldsLayout_ == nullptr || recoveryFields_.size() >= kMaximumRecoveryFields)
        return;

    auto *field = new QWidget;
    auto *fieldLayout = new QVBoxLayout(field);
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(5);
    auto *header = new QHBoxLayout;
    auto *fieldLabel = label({});
    fieldLabel->setProperty("recoveryFieldLabel", true);
    header->addWidget(fieldLabel);
    header->addStretch();
    auto *remove = new QPushButton(QStringLiteral("Remove"));
    remove->setProperty("recoveryRemoveButton", true);
    remove->setFlat(true);
    remove->setAutoDefault(false);
    header->addWidget(remove);
    fieldLayout->addLayout(header);

    auto *editor = new ExactTextEdit(nullptr, kMaximumRecoveryFieldBytes);
    editor->setPlaceholderText(QStringLiteral("Paste a share here…"));
    editor->setFixedHeight(84);
    configureEditor(editor);
    fieldLayout->addWidget(editor);
    recoveryFields_.append(editor);
    recoveryFieldsLayout_->addWidget(field);

    connect(editor, &ExactTextEdit::exactTextChanged, this, &DesktopWindow::recoveryInputChanged);
    connect(editor, &ExactTextEdit::inputRejected, this, [this](int reason) {
        recoveryStatus_->setText(reason == static_cast<int>(ClipboardRead::Status::TooLarge)
                                     ? QStringLiteral("A Recovery share batch cannot exceed the 16 MiB UTF-8 limit.")
                                     : QStringLiteral("A Recovery share must be valid UTF-8."));
    });
    connect(remove, &QPushButton::clicked, this, [this, editor] { removeRecoveryField(editor); });
    renumberRecoveryFields();
    if (recoveryFields_.size() > 2)
        recoveryInputChanged();
}

void DesktopWindow::removeRecoveryField(ExactTextEdit *editor) {
    if (recoveryFields_.size() <= 2 || !recoveryFields_.contains(editor))
        return;
    QWidget *container = editor->parentWidget();
    recoveryFields_.removeOne(editor);
    delete container;
    renumberRecoveryFields();
    recoveryInputChanged();
}

void DesktopWindow::renumberRecoveryFields() {
    const bool canRemove = recoveryFields_.size() > 2;
    for (qsizetype index = 0; index < recoveryFields_.size(); ++index) {
        ExactTextEdit *editor = recoveryFields_.at(index);
        QWidget *container = editor->parentWidget();
        auto *fieldLabel = container->findChild<QLabel *>(QString(), Qt::FindDirectChildrenOnly);
        auto *remove = container->findChild<QPushButton *>(QString(), Qt::FindChildrenRecursively);
        const QString fieldName = QStringLiteral("Recovery share %1").arg(index + 1);
        if (fieldLabel != nullptr) {
            fieldLabel->setText(fieldName);
            fieldLabel->setObjectName(QStringLiteral("recoveryShareLabel%1").arg(index + 1));
            fieldLabel->setBuddy(editor);
        }
        editor->setObjectName(QStringLiteral("recoveryShare%1").arg(index + 1));
        editor->setAccessibleName(fieldName);
        if (remove != nullptr) {
            remove->setObjectName(QStringLiteral("removeRecoveryShare%1").arg(index + 1));
            remove->setAccessibleName(QStringLiteral("Remove Recovery share %1").arg(index + 1));
            remove->setEnabled(canRemove);
        }
    }
    if (addRecoveryButton_ != nullptr)
        addRecoveryButton_->setEnabled(recoveryFields_.size() < kMaximumRecoveryFields);
}

void DesktopWindow::clearRecoveryFields() {
    if (recoveryFieldsLayout_ == nullptr)
        return;
    clearingRecoveryFields_ = true;
    while (recoveryFields_.size() > 2) {
        ExactTextEdit *editor = recoveryFields_.takeLast();
        delete editor->parentWidget();
    }
    for (ExactTextEdit *editor : std::as_const(recoveryFields_))
        editor->clearExact();
    clearingRecoveryFields_ = false;
    renumberRecoveryFields();
    recoveryHasEmptyFields_ = true;
    pendingInspectionHasEmptyFields_ = true;
    pendingInspectionGeneration_ = 0;
}

void DesktopWindow::recoveryInputChanged() {
    if (clearingRecoveryFields_)
        return;
    clearPendingExport();
    nextGeneration();
    recoverySyncTimer_->start();
    recoveryResult_->hide();
    recoveredDisplay_->clear();
    recoverButton_->setEnabled(false);
    pending_ = Pending::Inspect;
    recoveryStatus_->setText(QStringLiteral("Checking Recovery shares…"));
}

void DesktopWindow::synchronizeRecoveryFields() {
    qsizetype aggregateBytes = 0;
    int nonempty = 0;
    recoveryHasEmptyFields_ = false;
    for (const ExactTextEdit *editor : std::as_const(recoveryFields_)) {
        const qsizetype fieldBytes = editor->exactUtf8Size();
        if (fieldBytes == 0) {
            recoveryHasEmptyFields_ = true;
            continue;
        }
        if (aggregateBytes > kMaximumRetainedRecoveryBytes - fieldBytes) {
            pending_ = Pending::InspectReset;
            setBusy(true);
            recoveryStatus_->setText(QStringLiteral("Retained Recovery share input would exceed 160 MiB."));
            emit requestReset(generation_);
            return;
        }
        aggregateBytes += fieldBytes;
        ++nonempty;
    }

    QList<SecureByteBuffer> inputs;
    inputs.reserve(nonempty);
    for (ExactTextEdit *editor : std::as_const(recoveryFields_)) {
        if (editor->exactUtf8Size() > 0)
            inputs.append(SecureByteBuffer::take(editor->exactUtf8()));
    }
    recoveryCount_->setText(QStringLiteral("%1 of %2 Recovery shares entered")
                                .arg(nonempty)
                                .arg(recoveryFields_.size()));
    pendingInspectionGeneration_ = generation_;
    pendingInspectionHasEmptyFields_ = recoveryHasEmptyFields_;
    pending_ = Pending::Inspect;
    setBusy(true);
    emit requestReplaceRecovery(generation_, std::move(inputs),
                                recoveryEncoding_->currentData().toInt());
}

void DesktopWindow::recover() {
    QByteArray passphraseBytes = recoveryPassphrase_->exactUtf8();
    if (recoveryProtected_ && passphraseBytes.isEmpty()) {
        recoveryStatus_->setText(QStringLiteral("Enter the passphrase required by these Recovery shares."));
        passphraseBytes.fill(0);
        return;
    }
    if (passphraseBytes.size() > kMaximumPassphraseBytes) {
        recoveryStatus_->setText(QStringLiteral("Passphrase cannot exceed the 1 MiB UTF-8 limit."));
        passphraseBytes.fill(0);
        return;
    }
    SecureByteBuffer passphrase = SecureByteBuffer::take(std::move(passphraseBytes));
    passphraseBytes.fill(0);
    recoveryResult_->hide();
    recoveredDisplay_->clear();
    pending_ = Pending::Recover;
    setBusy(true);
    recoveryStatus_->setText(QStringLiteral("Working…"));
    emit requestRecover(generation_, std::move(passphrase));
}

void DesktopWindow::operationFinished(quint64 generation, int status, quint8 threshold,
                                      quint16 shareCount, quint16 suppliedCount,
                                      quint16 batchCount, int encoding, bool protectedInput,
                                      bool ready) {
    if (generation != generation_) {
        if (pending_ == Pending::Reset)
            queueCurrentReset();
        return;
    }
    setBusy(false);
    if (pending_ == Pending::Reset || pending_ == Pending::InspectReset) {
        pending_ = Pending::None;
        return;
    }
    if (pending_ == Pending::Create) {
        if (status == statusCode(Status::Ok)) {
            showCreated(threshold, shareCount);
            return;
        }
        createStatus_->setText(statusText(status));
    } else if (pending_ == Pending::Inspect) {
        Q_UNUSED(batchCount);
        const bool validInspection = status == statusCode(Status::Ok)
                                     || status == statusCode(Status::NotEnoughShares)
                                     || status == statusCode(Status::PassphraseRequired);
        if (validInspection) {
            recoveryProtected_ = protectedInput;
            recoveryPassphrasePanel_->setVisible(protectedInput);
            if (!protectedInput && recoveryPassphrase_->exactUtf8Size() != 0) {
                const QSignalBlocker blocker(recoveryPassphrase_);
                recoveryPassphrase_->clearExact();
            }
        }
        if (encoding != 0) {
            recoveryDetectedFormat_->setText(
                QStringLiteral("%1: %2")
                    .arg(recoveryEncoding_->currentData().toInt() == 0 ? QStringLiteral("Detected")
                                                                       : QStringLiteral("Using"),
                         encodingName(encoding)));
        }
        if (threshold >= 2 && recoveryFields_.size() < threshold) {
            clearingRecoveryFields_ = true;
            while (recoveryFields_.size() < threshold)
                addRecoveryField();
            clearingRecoveryFields_ = false;
            renumberRecoveryFields();
            recoveryInputChanged();
            return;
        }
        const bool invalid = status != statusCode(Status::Ok)
                             && status != statusCode(Status::NotEnoughShares);
        const bool hasRequiredEmptyField = pendingInspectionGeneration_ == generation
                                           && pendingInspectionHasEmptyFields_;
        if (hasRequiredEmptyField && !invalid && !ready) {
            recoverButton_->setEnabled(false);
            recoveryStatus_->setText(QStringLiteral("Share content is required."));
        } else {
            const bool passphraseReady = !recoveryProtected_
                                         || recoveryPassphrase_->exactUtf8Size() != 0;
            setRecoveryStatus(status, threshold, suppliedCount,
                              ready || (validInspection && recoveryProtected_ && passphraseReady
                                        && suppliedCount >= threshold));
        }
    }
    pending_ = Pending::None;
}

void DesktopWindow::bytesFinished(quint64 generation, int status, SecureByteBuffer bytes, int purpose,
                                  quint16 index) {
    if (generation != generation_)
        return;
    if (purpose == kShareDisplayPurpose) {
        if (status != statusCode(Status::Ok)) {
            failGeneratedPresentation(statusText(status));
            return;
        }
        if (index != nextGeneratedShare_
            || index >= static_cast<quint16>(generatedShareDisplays_.size())) {
            failGeneratedPresentation(QStringLiteral("Recovery shares could not be displayed safely."));
            return;
        }
        const qsizetype remaining = kMaximumGeneratedPresentationBytes
                                    - retainedGeneratedPresentationBytes_;
        if (bytes.size() > remaining / kGeneratedPresentationExpansion) {
            finishLazyGeneratedPresentation();
            return;
        }
        retainedGeneratedPresentationBytes_ += bytes.size() * kGeneratedPresentationExpansion;
        generatedShareDisplays_.at(index)->setPlainText(
            QString::fromUtf8(bytes.data(), bytes.size()));
        ++nextGeneratedShare_;
        requestNextGeneratedShare();
        return;
    }

    const Pending expected = purpose == kShareClipboardPurpose   ? Pending::CopyShare
                             : purpose == kShareSavePurpose      ? Pending::SaveShare
                             : purpose == kRecoveredDisplayPurpose ? Pending::Recover
                             : purpose == kRecoveredClipboardPurpose ? Pending::CopyRecovered
                             : purpose == kRecoveredSavePurpose  ? Pending::SaveRecovered
                                                                 : Pending::None;
    if (pending_ != expected
        || ((purpose == kShareClipboardPurpose || purpose == kShareSavePurpose)
            && index != pendingShareIndex_))
        return;

    setBusy(false);
    if (status != statusCode(Status::Ok)) {
        if (purpose == kShareClipboardPurpose || purpose == kShareSavePurpose) {
            createStatus_->setText(statusText(status));
        } else {
            recoveryStatus_->setText(statusText(status));
            recoverButton_->setEnabled(purpose == kRecoveredDisplayPurpose);
        }
        clearPendingExport();
        pending_ = Pending::None;
        return;
    }
    if (purpose == kShareClipboardPurpose) {
        if (lazyGeneratedPresentation_
            && index < static_cast<quint16>(generatedShareDisplays_.size())) {
            for (QPlainTextEdit *display : std::as_const(generatedShareDisplays_))
                display->clear();
            generatedShareDisplays_.at(index)->setPlainText(
                QString::fromUtf8(bytes.data(), bytes.size()));
            retainedGeneratedPresentationBytes_ = bytes.size() * kGeneratedPresentationExpansion;
        }
        if (writeClipboardUtf8(bytes.view()))
            createStatus_->setText(QStringLiteral("Share %1 copied.").arg(index + 1));
    } else if (purpose == kShareSavePurpose) {
        const FileIo::Status result = fileIo_->writeDirect(pendingDestination_, bytes);
        createStatus_->setText(result == FileIo::Status::Ok
                                   ? QStringLiteral("Share %1 saved.").arg(index + 1)
                                   : QStringLiteral("Share could not be saved."));
    } else if (purpose == kRecoveredDisplayPurpose) {
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString text = decoder.decode(bytes.view());
        const bool validUtf8 = !decoder.hasError();
        recoveredDisplay_->setVisible(validUtf8);
        copyRecovered_->setVisible(validUtf8);
        if (validUtf8)
            recoveredDisplay_->setPlainText(text);
        else
            recoveredDisplay_->clear();
        saveRecovered_->show();
        recoveryResult_->show();
        copyRecovered_->setEnabled(validUtf8);
        saveRecovered_->setEnabled(true);
        recoverButton_->setEnabled(true);
        recoveryStatus_->setText(validUtf8 ? QStringLiteral("Recovered exact valid UTF-8 Secret.")
                                           : QStringLiteral("Recovered binary Secret. Save exact bytes."));
    } else if (purpose == kRecoveredClipboardPurpose && writeClipboardUtf8(bytes.view())) {
        recoveryStatus_->setText(QStringLiteral("Recovered Secret copied."));
    } else if (purpose == kRecoveredSavePurpose) {
        const FileIo::Status result = fileIo_->writeDirect(pendingDestination_, bytes);
        recoveryStatus_->setText(result == FileIo::Status::Ok
                                     ? QStringLiteral("Recovered exact bytes saved.")
                                     : QStringLiteral("Recovered bytes could not be saved."));
    }
    clearPendingExport();
    pending_ = Pending::None;
}

void DesktopWindow::closeEvent(QCloseEvent *event) {
    ++generation_;
    clearVisibleState();
    event->accept();
}

void DesktopWindow::clearVisibleState() {
    if (recoverySyncTimer_ != nullptr)
        recoverySyncTimer_->stop();
    if (secretInput_ != nullptr)
        secretInput_->clearExact();
    secretFileBytes_ = {};
    usesSecretFile_ = false;
    clearPendingExport();
    if (secretFileMetadata_ != nullptr) {
        secretFileMetadata_->clear();
        secretFileMetadata_->hide();
        secretInput_->show();
        chooseSecretFileButton_->show();
        useTextSecretButton_->hide();
    }
    if (threshold_ != nullptr)
        threshold_->setValue(2);
    if (shareCount_ != nullptr)
        shareCount_->setValue(3);
    if (protectWithPassphrase_ != nullptr) {
        protectWithPassphrase_->setChecked(false);
        createPassphrase_->clearExact();
        confirmPassphrase_->clearExact();
    }
    if (createEncoding_ != nullptr) {
        createEncoding_->blockSignals(true);
        createEncoding_->setCurrentIndex(2);
        createEncoding_->blockSignals(false);
    }
    clearGeneratedPresentation();
    if (createdResult_ != nullptr)
        createdResult_->hide();
    if (createStatus_ != nullptr)
        createStatus_->setText(QStringLiteral("2 of 3 · Words"));
    clearRecoveryFields();
    if (recoveryEncoding_ != nullptr) {
        recoveryEncoding_->blockSignals(true);
        recoveryEncoding_->setCurrentIndex(0);
        recoveryEncoding_->blockSignals(false);
    }
    if (recoveryDetectedFormat_ != nullptr)
        recoveryDetectedFormat_->setText(QStringLiteral("Detected after paste"));
    if (recoveryCount_ != nullptr)
        recoveryCount_->setText(QStringLiteral("0 of 2 Recovery shares entered"));
    recoveryProtected_ = false;
    if (recoveryPassphrase_ != nullptr)
        recoveryPassphrase_->clearExact();
    if (recoveryPassphrasePanel_ != nullptr)
        recoveryPassphrasePanel_->hide();
    if (recoveryStatus_ != nullptr)
        recoveryStatus_->setText(QStringLiteral("Share content is required."));
    if (recoverButton_ != nullptr)
        recoverButton_->setEnabled(false);
    if (recoveryResult_ != nullptr)
        recoveryResult_->hide();
    if (recoveredDisplay_ != nullptr) {
        recoveredDisplay_->clear();
        recoveredDisplay_->show();
    }
    if (copyRecovered_ != nullptr)
        copyRecovered_->show();
}

void DesktopWindow::createInputChanged() {
    clearPendingExport();
    const bool invalidatesCreatedShares = createdResult_ != nullptr && !createdResult_->isHidden();
    nextGeneration();
    clearGeneratedPresentation();
    if (createdResult_ != nullptr)
        createdResult_->hide();
    createStatus_->setText(QStringLiteral("%1 of %2 · %3")
                               .arg(threshold_->value())
                               .arg(shareCount_->value())
                               .arg(encodingName(createEncoding_->currentData().toInt())));
    if (pending_ == Pending::Create || invalidatesCreatedShares) {
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
    createdTitle_->setText(QStringLiteral("Recovery shares"));
    clearGeneratedPresentation();
    generatedShareDisplays_.clear();
    generatedShareCopyButtons_.clear();
    generatedShareSaveButtons_.clear();
    if (QLayout *oldRows = createdRows_->layout(); oldRows != nullptr) {
        while (QLayoutItem *item = oldRows->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        delete oldRows;
    }
    auto *rows = new QVBoxLayout(createdRows_);
    rows->setContentsMargins(16, 8, 12, 8);
    rows->setSpacing(0);
    rows->setSizeConstraint(QLayout::SetMinimumSize);
    for (quint16 index = 0; index < shareCount; ++index) {
        if (index > 0) {
            auto *separator = new QFrame;
            separator->setObjectName(QStringLiteral("shareSeparator"));
            separator->setFrameShape(QFrame::HLine);
            separator->setFrameShadow(QFrame::Plain);
            QColor line = separator->palette().color(QPalette::WindowText);
            line.setAlphaF(0.10);
            QPalette separatorPalette = separator->palette();
            separatorPalette.setColor(QPalette::WindowText, line);
            separator->setPalette(separatorPalette);
            rows->addWidget(separator);
        }
        auto *row = new QWidget;
        row->setMinimumHeight(84);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 6, 0, 6);
        rowLayout->setSpacing(12);
        auto *number = label(QString::number(index + 1));
        number->setAlignment(Qt::AlignCenter);
        number->setFixedSize(24, 24);
        QFont numberFont = number->font();
        numberFont.setWeight(QFont::DemiBold);
        number->setFont(numberFont);
        rowLayout->addWidget(number, 0, Qt::AlignTop);

        auto *share = new QPlainTextEdit;
        share->setObjectName(QStringLiteral("generatedShare%1").arg(index + 1));
        share->setAccessibleName(QStringLiteral("Recovery share %1").arg(index + 1));
        share->setPlaceholderText(QStringLiteral("Loading…"));
        share->setReadOnly(true);
        share->setUndoRedoEnabled(false);
        share->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
        share->setFixedHeight(72);
        configureEditor(share);
        generatedShareDisplays_.append(share);
        rowLayout->addWidget(share, 1);

        auto *copy = new QPushButton;
        copy->setObjectName(QStringLiteral("copyShare%1").arg(index + 1));
        configureCopyButton(copy, QStringLiteral("Copy Recovery share %1").arg(index + 1));
        copy->setEnabled(false);
        connect(copy, &QPushButton::clicked, this, [this, index] {
            if (pending_ != Pending::None)
                return;
            pending_ = Pending::CopyShare;
            pendingShareIndex_ = index;
            setBusy(true);
            emit requestEncodeShare(generation_, index, kShareClipboardPurpose);
        });
        generatedShareCopyButtons_.append(copy);
        rowLayout->addWidget(copy, 0, Qt::AlignTop);
        auto *save = new QPushButton(QStringLiteral("Save…"));
        save->setObjectName(QStringLiteral("saveShare%1").arg(index + 1));
        save->setEnabled(false);
        connect(save, &QPushButton::clicked, this, [this, index] {
            if (pending_ != Pending::None)
                return;
            const QString destination = saveFileDialog_();
            if (destination.isEmpty())
                return;
            pendingDestination_ = destination;
            pending_ = Pending::SaveShare;
            pendingShareIndex_ = index;
            setBusy(true);
            emit requestEncodeShare(generation_, index, kShareSavePurpose);
        });
        generatedShareSaveButtons_.append(save);
        rowLayout->addWidget(save, 0, Qt::AlignTop);
        rows->addWidget(row);
    }
    rows->addWidget(label(QStringLiteral("%1 of %2 · %3")
                              .arg(threshold)
                              .arg(shareCount)
                              .arg(encodingName(createEncoding_->currentData().toInt())),
                          true));
    createdRows_->setMinimumHeight(rows->sizeHint().height());
    createdRows_->show();
    createdResult_->show();
    createdResult_->updateGeometry();
    const qsizetype secretBytes = usesSecretFile_ ? secretFileBytes_.size()
                                                   : secretInput_->exactUtf8Size();
    const qsizetype shareCountSize = qsizetype(shareCount);
    const bool presentationEstimateOverflows =
        shareCountSize > 0
        && secretBytes > std::numeric_limits<qsizetype>::max() / shareCountSize
                              / kGeneratedWorstCasePerPacketByte;
    const qsizetype worstCasePresentation = presentationEstimateOverflows
                                                ? std::numeric_limits<qsizetype>::max()
                                                : secretBytes * shareCountSize
                                                      * kGeneratedWorstCasePerPacketByte;
    if (worstCasePresentation > kMaximumGeneratedPresentationBytes) {
        finishLazyGeneratedPresentation();
        return;
    }
    createStatus_->setText(QStringLiteral("Loading Recovery shares…"));
    pending_ = Pending::PreviewShares;
    setBusy(true);
    requestNextGeneratedShare();
}

void DesktopWindow::requestNextGeneratedShare() {
    if (pending_ != Pending::PreviewShares)
        return;
    if (nextGeneratedShare_ >= static_cast<quint16>(generatedShareDisplays_.size())) {
        pending_ = Pending::None;
        setBusy(false);
        createStatus_->setText(QStringLiteral("Shares created in memory."));
        return;
    }
    emit requestEncodeShare(generation_, nextGeneratedShare_, kShareDisplayPurpose);
}

void DesktopWindow::clearGeneratedPresentation() {
    for (QPlainTextEdit *display : std::as_const(generatedShareDisplays_))
        display->clear();
    for (QPushButton *copy : std::as_const(generatedShareCopyButtons_))
        copy->setEnabled(false);
    for (QPushButton *save : std::as_const(generatedShareSaveButtons_))
        save->setEnabled(false);
    nextGeneratedShare_ = 0;
    retainedGeneratedPresentationBytes_ = 0;
    lazyGeneratedPresentation_ = false;
}

void DesktopWindow::finishLazyGeneratedPresentation() {
    for (QPlainTextEdit *display : std::as_const(generatedShareDisplays_)) {
        display->clear();
        display->setPlaceholderText(QStringLiteral("Copy to reveal this Recovery share."));
    }
    retainedGeneratedPresentationBytes_ = 0;
    lazyGeneratedPresentation_ = true;
    nextGeneratedShare_ = static_cast<quint16>(generatedShareDisplays_.size());
    pending_ = Pending::None;
    setBusy(false);
    createStatus_->setText(
        QStringLiteral("Shares created in memory. Copy any share to reveal and export it."));
}

void DesktopWindow::failGeneratedPresentation(const QString &message) {
    clearGeneratedPresentation();
    if (createdResult_ != nullptr)
        createdResult_->hide();
    pending_ = Pending::None;
    setBusy(false);
    createStatus_->setText(message);
}

void DesktopWindow::setBusy(bool busy) {
    if (createButton_ != nullptr)
        createButton_->setEnabled(!busy);
    const bool generatedSharesReady = !busy && createdResult_ != nullptr
                                      && !createdResult_->isHidden()
                                      && nextGeneratedShare_
                                             == static_cast<quint16>(generatedShareDisplays_.size());
    for (QPushButton *copy : std::as_const(generatedShareCopyButtons_))
        copy->setEnabled(generatedSharesReady);
    for (QPushButton *save : std::as_const(generatedShareSaveButtons_))
        save->setEnabled(generatedSharesReady);
    if (addRecoveryButton_ != nullptr)
        addRecoveryButton_->setEnabled(!busy && recoveryFields_.size() < kMaximumRecoveryFields);
    for (ExactTextEdit *editor : std::as_const(recoveryFields_)) {
        QWidget *container = editor->parentWidget();
        if (auto *remove = container->findChild<QPushButton *>(); remove != nullptr)
            remove->setEnabled(!busy && recoveryFields_.size() > 2);
    }
    if (recoverButton_ != nullptr && busy)
        recoverButton_->setEnabled(false);
    const bool recoveredReady = !busy && recoveryResult_ != nullptr
                                && !recoveryResult_->isHidden();
    if (copyRecovered_ != nullptr)
        copyRecovered_->setEnabled(recoveredReady && !copyRecovered_->isHidden());
    if (saveRecovered_ != nullptr)
        saveRecovered_->setEnabled(recoveredReady);
}

void DesktopWindow::clearPendingExport() {
    pendingDestination_.clear();
    pendingShareIndex_ = 0;
    if (pending_ == Pending::CopyShare || pending_ == Pending::SaveShare
        || pending_ == Pending::CopyRecovered || pending_ == Pending::SaveRecovered)
        pending_ = Pending::None;
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
        return QStringLiteral("Choose one supported Share format.");
    if (status == statusCode(Status::PassphraseRequired))
        return QStringLiteral("Enter the passphrase required by these Recovery shares.");
    if (status == statusCode(Status::UnsupportedParameters))
        return QStringLiteral("These Recovery shares use unsupported protection parameters.");
    if (status == statusCode(Status::UnsupportedVersion))
        return QStringLiteral("This Recovery share packet version is not supported.");
    if (status == statusCode(Status::MixedEncoding))
        return QStringLiteral("Use one Share format for every Recovery share.");
    if (status == statusCode(Status::MixedVersion))
        return QStringLiteral("Recovery shares from different packet versions cannot be mixed.");
    if (status == statusCode(Status::MalformedInput))
        return QStringLiteral("A complete Recovery share could not be decoded in the selected format.");
    if (status == statusCode(Status::NotEnoughShares))
        return QStringLiteral("Add enough distinct recovery shares.");
    if (status == statusCode(Status::InvalidUtf8))
        return QStringLiteral("Input or recovered output is not valid UTF-8.");
    if (status == statusCode(Status::IntegrityFailure))
        return QStringLiteral("The passphrase may be incorrect or the Recovery shares may be damaged. No output was shown.");
    if (status == statusCode(Status::PassphraseTooLarge))
        return QStringLiteral("Passphrase cannot exceed the 1 MiB UTF-8 limit.");
    if (status == statusCode(Status::InternalPanic))
        return QStringLiteral("Processing stopped safely. Start over before retrying.");
    return QStringLiteral("The operation could not be completed.");
}

quint64 DesktopWindow::nextGeneration() {
    return ++generation_;
}
