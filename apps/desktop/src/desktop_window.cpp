#include "desktop_window.h"

#include "clipboard.h"
#include "exact_text_edit.h"
#include "rust_worker.h"

#include <QCloseEvent>
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
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace {
constexpr qsizetype kMaximumRecoveryFieldBytes = 8 * 1'048'576;
constexpr qsizetype kMaximumRetainedRecoveryBytes = 160 * 1'048'576;
constexpr qsizetype kMaximumRecoveryFields = 255;

int statusCode(Status status) {
    return static_cast<int>(static_cast<std::uint8_t>(status));
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
    connect(this, &DesktopWindow::requestReplaceRecovery, worker_, &RustWorker::replaceRecovery,
            Qt::QueuedConnection);
    connect(this, &DesktopWindow::requestRecover, worker_, &RustWorker::recover, Qt::QueuedConnection);
    connect(this, &DesktopWindow::requestRecoveredText, worker_, &RustWorker::recoveredText, Qt::QueuedConnection);
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
                                                "Combine runs only after the visible shares form a valid set. This build supports unprotected Words text shares."));
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

    auto *settings = new QGridLayout;
    settings->setHorizontalSpacing(12);
    settings->setVerticalSpacing(4);
    auto *thresholdLabel = label(QStringLiteral("Minimum shares to recover (k)"));
    threshold_ = new QSpinBox;
    threshold_->setObjectName(QStringLiteral("thresholdInput"));
    threshold_->setAccessibleName(QStringLiteral("Minimum shares to recover (k)"));
    threshold_->setRange(1, 255);
    threshold_->setValue(2);
    thresholdLabel->setBuddy(threshold_);
    settings->addWidget(thresholdLabel, 0, 0);
    settings->addWidget(threshold_, 1, 0);
    auto *shareCountLabel = label(QStringLiteral("Total shares to create (n)"));
    shareCount_ = new QSpinBox;
    shareCount_->setObjectName(QStringLiteral("shareCountInput"));
    shareCount_->setAccessibleName(QStringLiteral("Total shares to create (n)"));
    shareCount_->setRange(1, 255);
    shareCount_->setValue(3);
    shareCountLabel->setBuddy(shareCount_);
    settings->addWidget(shareCountLabel, 0, 1);
    settings->addWidget(shareCount_, 1, 1);
    settings->addWidget(label(QStringLiteral("Share format")), 0, 2);
    settings->addWidget(label(QStringLiteral("Words"), true), 1, 2);
    settings->setColumnStretch(0, 1);
    settings->setColumnStretch(1, 1);
    settings->setColumnStretch(2, 1);
    groupLayout->addLayout(settings);

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
    connect(secretInput_, &ExactTextEdit::exactTextChanged, this, &DesktopWindow::createInputChanged);
    connect(secretInput_, &ExactTextEdit::inputRejected, this, [this](int reason) {
        createStatus_->setText(reason == static_cast<int>(ClipboardRead::Status::TooLarge)
                                   ? QStringLiteral("Secret cannot exceed the 1 MiB UTF-8 limit.")
                                   : QStringLiteral("Secret must be valid UTF-8."));
    });
    connect(threshold_, &QSpinBox::valueChanged, this, &DesktopWindow::createInputChanged);
    connect(shareCount_, &QSpinBox::valueChanged, this, &DesktopWindow::createInputChanged);
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
    fieldActions->addStretch();
    recoveryCount_ = label(QStringLiteral("0 of 2 Recovery shares entered"), true);
    recoveryCount_->setObjectName(QStringLiteral("recoveryCount"));
    fieldActions->addWidget(recoveryCount_);
    groupLayout->addLayout(fieldActions);

    auto *action = new QHBoxLayout;
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
    resultLayout->addWidget(copyRecovered_, 0, Qt::AlignRight);
    recoveryResult_->hide();
    groupLayout->addWidget(recoveryResult_);
    layout->addWidget(group);

    connect(addRecoveryButton_, &QPushButton::clicked, this, &DesktopWindow::addRecoveryField);
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
    SecureByteBuffer secret = SecureByteBuffer::take(secretInput_->exactUtf8());
    const quint64 requestGeneration = nextGeneration();
    pending_ = Pending::Create;
    setBusy(true);
    createStatus_->setText(QStringLiteral("Working…"));
    emit requestCreate(requestGeneration, std::move(secret), static_cast<quint8>(threshold_->value()),
                       static_cast<quint8>(shareCount_->value()));
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
                                     ? QStringLiteral("A Recovery share cannot exceed the 8 MiB UTF-8 limit.")
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
}

void DesktopWindow::recoveryInputChanged() {
    if (clearingRecoveryFields_)
        return;
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
    pending_ = Pending::Inspect;
    setBusy(true);
    emit requestReplaceRecovery(generation_, std::move(inputs));
}

void DesktopWindow::recover() {
    recoveryResult_->hide();
    recoveredDisplay_->clear();
    pending_ = Pending::Recover;
    setBusy(true);
    recoveryStatus_->setText(QStringLiteral("Working…"));
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
    if (pending_ == Pending::Reset || pending_ == Pending::InspectReset) {
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
        Q_UNUSED(batchCount);
        const bool invalid = status != statusCode(Status::Ok)
                             && status != statusCode(Status::NotEnoughShares);
        if (recoveryHasEmptyFields_ && !invalid) {
            recoverButton_->setEnabled(false);
            recoveryStatus_->setText(QStringLiteral("Share content is required."));
        } else {
            setRecoveryStatus(status, threshold, suppliedCount, ready);
        }
    }
    pending_ = Pending::None;
}

void DesktopWindow::bytesFinished(quint64 generation, int status, SecureByteBuffer bytes, int purpose,
                                  quint16 index) {
    if (generation != generation_)
        return;
    setBusy(false);
    if (status != statusCode(Status::Ok)) {
        if (purpose == 0) {
            createStatus_->setText(statusText(status));
        } else {
            recoveryStatus_->setText(statusText(status));
            recoverButton_->setEnabled(purpose == 1);
        }
        return;
    }
    if (purpose == 0) {
        if (writeClipboardUtf8(bytes.view()))
            createStatus_->setText(QStringLiteral("Share %1 copied.").arg(index + 1));
    } else if (purpose == 1) {
        recoveredDisplay_->setPlainText(QString::fromUtf8(bytes.data(), bytes.size()));
        recoveryResult_->show();
        recoverButton_->setEnabled(true);
        recoveryStatus_->setText(QStringLiteral("Recovered exact UTF-8 secret."));
    } else if (writeClipboardUtf8(bytes.view())) {
        recoveryStatus_->setText(QStringLiteral("Recovered secret copied."));
    }
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
    if (threshold_ != nullptr)
        threshold_->setValue(2);
    if (shareCount_ != nullptr)
        shareCount_->setValue(3);
    if (createdResult_ != nullptr)
        createdResult_->hide();
    if (createStatus_ != nullptr)
        createStatus_->setText(QStringLiteral("2 of 3 · Words"));
    clearRecoveryFields();
    if (recoveryCount_ != nullptr)
        recoveryCount_->setText(QStringLiteral("0 of 2 Recovery shares entered"));
    if (recoveryStatus_ != nullptr)
        recoveryStatus_->setText(QStringLiteral("Share content is required."));
    if (recoverButton_ != nullptr)
        recoverButton_->setEnabled(false);
    if (recoveryResult_ != nullptr)
        recoveryResult_->hide();
    if (recoveredDisplay_ != nullptr)
        recoveredDisplay_->clear();
}

void DesktopWindow::createInputChanged() {
    const bool invalidatesCreatedShares = createdResult_ != nullptr && !createdResult_->isHidden();
    nextGeneration();
    if (createdResult_ != nullptr)
        createdResult_->hide();
    createStatus_->setText(
        QStringLiteral("%1 of %2 · Words").arg(threshold_->value()).arg(shareCount_->value()));
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
        row->setMinimumHeight(44);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(12);
        auto *number = label(QString::number(index + 1));
        number->setAlignment(Qt::AlignCenter);
        number->setFixedSize(24, 24);
        QFont numberFont = number->font();
        numberFont.setWeight(QFont::DemiBold);
        number->setFont(numberFont);
        rowLayout->addWidget(number);
        rowLayout->addWidget(label(QStringLiteral("Recovery share %1").arg(index + 1)), 1);
        auto *copy = new QPushButton;
        copy->setObjectName(QStringLiteral("copyShare%1").arg(index + 1));
        configureCopyButton(copy, QStringLiteral("Copy Recovery share %1").arg(index + 1));
        connect(copy, &QPushButton::clicked, this, [this, index] {
            pending_ = Pending::CopyShare;
            setBusy(true);
            emit requestEncodeShare(generation_, index);
        });
        rowLayout->addWidget(copy);
        rows->addWidget(row);
    }
    rows->addWidget(label(QStringLiteral("%1 of %2 · Words").arg(threshold).arg(shareCount), true));
    createdRows_->setMinimumHeight(rows->sizeHint().height());
    createdResult_->show();
    createdResult_->updateGeometry();
    createStatus_->setText(QStringLiteral("Shares created in memory."));
}

void DesktopWindow::setBusy(bool busy) {
    if (createButton_ != nullptr)
        createButton_->setEnabled(!busy);
    if (addRecoveryButton_ != nullptr)
        addRecoveryButton_->setEnabled(!busy && recoveryFields_.size() < kMaximumRecoveryFields);
    for (ExactTextEdit *editor : std::as_const(recoveryFields_)) {
        QWidget *container = editor->parentWidget();
        if (auto *remove = container->findChild<QPushButton *>(); remove != nullptr)
            remove->setEnabled(!busy && recoveryFields_.size() > 2);
    }
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
