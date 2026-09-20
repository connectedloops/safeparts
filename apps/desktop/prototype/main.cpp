#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleHints>
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const QStringList kDemoTokens = {
    QStringLiteral("DEMO-SHARE-A7-KITE"),
    QStringLiteral("DEMO-SHARE-B4-MOSS"),
    QStringLiteral("DEMO-SHARE-C9-LAMP"),
};
const QString kDemoSecret = QStringLiteral("orchard-window-cobalt");

QColor blend(const QColor &first, const QColor &second, qreal amount) {
    return QColor::fromRgbF(
        first.redF() * (1.0 - amount) + second.redF() * amount,
        first.greenF() * (1.0 - amount) + second.greenF() * amount,
        first.blueF() * (1.0 - amount) + second.blueF() * amount,
        1.0);
}

QString cssColor(const QColor &color) {
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

QFont pointFont(const QWidget *widget, qreal points, QFont::Weight weight = QFont::Normal) {
    QFont font = widget->font();
    font.setPointSizeF(points);
    font.setWeight(weight);
    return font;
}

QLabel *textLabel(const QString &text, qreal points = 13.0, QFont::Weight weight = QFont::Normal) {
    auto *label = new QLabel(text);
    label->setFont(pointFont(label, points, weight));
    label->setWordWrap(true);
    return label;
}

QLabel *secondaryLabel(const QString &text, qreal points = 11.5) {
    auto *label = textLabel(text, points);
    QPalette palette = label->palette();
    QColor color = palette.color(QPalette::WindowText);
    color.setAlphaF(0.62);
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
    return label;
}

QLabel *pageTitle(const QString &text) {
    return textLabel(text, 22.0, QFont::DemiBold);
}

class Surface final : public QWidget {
public:
    explicit Surface(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TranslucentBackground);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPalette current = palette();
        const QColor window = current.color(QPalette::Window);
        const QColor text = current.color(QPalette::WindowText);
        const bool dark = current.color(QPalette::Window).lightnessF() < 0.5;
        const QColor fill = blend(window, text, dark ? 0.075 : 0.035);
        QColor border = text;
        border.setAlphaF(dark ? 0.10 : 0.075);
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 14, 14);
    }
};

class Hairline final : public QWidget {
public:
    explicit Hairline(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedHeight(1);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        QColor line = palette().color(QPalette::WindowText);
        line.setAlphaF(0.10);
        painter.fillRect(rect(), line);
    }
};

class ModeSelector final : public QTabBar {
public:
    explicit ModeSelector(QWidget *parent = nullptr) : QTabBar(parent) {
        addTab(QStringLiteral("Create"));
        addTab(QStringLiteral("Recover"));
        setObjectName(QStringLiteral("modeSelector"));
        setAccessibleName(QStringLiteral("Choose Create or Recover"));
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
        QColor track = blend(window, text, dark ? 0.10 : 0.07);
        QColor edge = text;
        edge.setAlphaF(dark ? 0.12 : 0.08);
        painter.setPen(QPen(edge, 1));
        painter.setBrush(track);
        painter.drawRoundedRect(bounds, 8, 8);

        const qreal half = width() / 2.0;
        const QRectF selected(currentIndex() == 0 ? 2.5 : half + 0.5, 2.5, half - 3.0, height() - 5.0);
        QColor selection = blend(window, text, dark ? 0.19 : 0.015);
        painter.setPen(Qt::NoPen);
        painter.setBrush(selection);
        painter.drawRoundedRect(selected, 6, 6);

        if (hasFocus()) {
            QColor focus = palette().color(QPalette::Highlight);
            focus.setAlphaF(0.78);
            painter.setPen(QPen(focus, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 7, 7);
        }

        painter.setPen(text);
        painter.setFont(pointFont(this, 11.5, QFont::Medium));
        painter.drawText(QRectF(0, 0, half, height()), Qt::AlignCenter, tabText(0));
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

void styleInset(QWidget *widget) {
    const QPalette current = widget->palette();
    const QColor window = current.color(QPalette::Window);
    const QColor text = current.color(QPalette::WindowText);
    const bool dark = window.lightnessF() < 0.5;
    const QColor background = blend(window, text, dark ? 0.045 : 0.018);
    QColor border = text;
    border.setAlphaF(dark ? 0.16 : 0.11);
    QColor selection = current.color(QPalette::Highlight);
    widget->setStyleSheet(QStringLiteral(
        "QLineEdit, QPlainTextEdit { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 8px; padding: 8px 10px; selection-background-color: %4; } "
        "QLineEdit:focus, QPlainTextEdit:focus { border: 2px solid %4; padding: 7px 9px; }")
        .arg(cssColor(background), cssColor(text), cssColor(border), cssColor(selection)));
}

void styleQuietButton(QPushButton *button) {
    const QColor text = button->palette().color(QPalette::ButtonText);
    QColor hover = text;
    hover.setAlphaF(0.08);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { border: 0; border-radius: 7px; padding: 5px 10px; background: transparent; } "
        "QPushButton:hover { background: %1; } QPushButton:pressed { background: %2; }")
        .arg(cssColor(hover), cssColor(hover.darker(115))));
}
} // namespace

class PrototypeWindow final : public QMainWindow {
public:
    PrototypeWindow() {
        setWindowTitle(QStringLiteral("Safeparts Preview"));
        resize(720, 560);
        setMinimumSize(620, 480);
        buildWindow();
        setMode(0);
    }

    void showCreatedShares() {
        createButton_->setEnabled(false);
        createStates_->setCurrentIndex(1);
    }

    void showRecoverReady() {
        setMode(1);
        recoverInput_->setPlainText(kDemoTokens.at(0) + QLatin1Char('\n') + kDemoTokens.at(1));
    }

    void showRecoveredResult() {
        showRecoverReady();
        recover();
    }

private:
    void buildWindow() {
        auto *central = new QWidget;
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(28, 18, 28, 15);
        root->setSpacing(14);

        auto *header = new QHBoxLayout;
        header->setSpacing(8);
        modeSelector_ = new ModeSelector;
        header->addWidget(modeSelector_);
        header->addStretch();
        helpButton_ = new QToolButton;
        helpButton_->setObjectName(QStringLiteral("helpButton"));
        helpButton_->setText(QStringLiteral("Help"));
        helpButton_->setAutoRaise(true);
        helpButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
        helpButton_->setStyleSheet(QStringLiteral("QToolButton { border: 0; padding: 5px 7px; background: transparent; }"));
        resetButton_ = new QToolButton;
        resetButton_->setObjectName(QStringLiteral("resetButton"));
        resetButton_->setText(QStringLiteral("Start over"));
        resetButton_->setAutoRaise(true);
        resetButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
        resetButton_->setStyleSheet(QStringLiteral("QToolButton { border: 0; padding: 5px 7px; background: transparent; }"));
        header->addWidget(helpButton_);
        header->addWidget(resetButton_);
        root->addLayout(header);

        pages_ = new QStackedWidget;
        buildCreatePage();
        buildRecoverPage();
        root->addWidget(pages_, 1);

        auto *footer = new QHBoxLayout;
        footer->setSpacing(8);
        auto *preview = textLabel(QStringLiteral("PREVIEW"), 10.0, QFont::DemiBold);
        QPalette previewPalette = preview->palette();
        previewPalette.setColor(QPalette::WindowText, previewPalette.color(QPalette::Highlight));
        preview->setPalette(previewPalette);
        footer->addStretch();
        footer->addWidget(preview);
        auto *notice = secondaryLabel(QStringLiteral("Demo data only. Not for real secrets."), 10.5);
        notice->setWordWrap(false);
        footer->addWidget(notice);
        footer->addStretch();
        root->addLayout(footer);
        central->setFocusPolicy(Qt::StrongFocus);
        setCentralWidget(central);
        central->setFocus(Qt::OtherFocusReason);

        connect(modeSelector_, &QTabBar::currentChanged, this, [this](int index) { setMode(index); });
        connect(resetButton_, &QToolButton::clicked, this, [this] { resetOperation(); });
        connect(helpButton_, &QToolButton::clicked, this, [this] {
            QMessageBox::information(this, QStringLiteral("About this preview"),
                QStringLiteral("This preview uses three fixed fake tokens and a fixed result. "
                               "It does not use cryptography, split a secret, parse recovery shares, or recover data."));
        });
    }

    void addPage(QWidget *page) {
        auto *viewport = new QWidget;
        auto *viewportLayout = new QHBoxLayout(viewport);
        viewportLayout->setContentsMargins(0, 4, 0, 4);
        viewportLayout->addStretch();
        page->setMaximumWidth(590);
        page->setMinimumWidth(0);
        viewportLayout->addWidget(page, 1, Qt::AlignTop);
        viewportLayout->addStretch();

        auto *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(viewport);
        pages_->addWidget(scroll);
    }

    QWidget *createEntry() {
        auto *entry = new QWidget;
        auto *layout = new QVBoxLayout(entry);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(18);
        layout->addWidget(pageTitle(QStringLiteral("Create recovery shares")));
        layout->addWidget(secondaryLabel(QStringLiteral("Use the fixed example to see how a 2-of-3 set is presented."), 12.5));

        auto *surface = new Surface;
        auto *surfaceLayout = new QVBoxLayout(surface);
        surfaceLayout->setContentsMargins(20, 16, 20, 16);
        surfaceLayout->setSpacing(12);
        surfaceLayout->addWidget(textLabel(QStringLiteral("Example secret"), 11.0, QFont::DemiBold));
        secretField_ = new QLineEdit(kDemoSecret);
        secretField_->setObjectName(QStringLiteral("syntheticSecret"));
        secretField_->setReadOnly(true);
        secretField_->setAccessibleDescription(QStringLiteral("Fixed synthetic example"));
        secretField_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        secretField_->setFixedHeight(38);
        styleInset(secretField_);
        surfaceLayout->addWidget(secretField_);
        surfaceLayout->addWidget(new Hairline);

        auto *summary = new QHBoxLayout;
        summary->addWidget(textLabel(QStringLiteral("Recovery setup"), 12.0));
        summary->addStretch();
        summary->addWidget(textLabel(QStringLiteral("2 of 3  ·  Words"), 12.0, QFont::DemiBold));
        surfaceLayout->addLayout(summary);
        surfaceLayout->addWidget(new Hairline);

        auto *action = new QHBoxLayout;
        action->setSpacing(12);
        action->addWidget(secondaryLabel(QStringLiteral("Creates three fixed demo shares."), 11.0), 1);
        createButton_ = new QPushButton(QStringLiteral("Create demo shares"));
        createButton_->setObjectName(QStringLiteral("createButton"));
        createButton_->setDefault(true);
        createButton_->setFixedHeight(32);
        action->addWidget(createButton_);
        surfaceLayout->addLayout(action);
        layout->addWidget(surface);
        layout->addStretch();
        connect(createButton_, &QPushButton::clicked, this, [this] { showCreatedShares(); });
        return entry;
    }

    QWidget *createdResult() {
        auto *resultPage = new QWidget;
        auto *layout = new QVBoxLayout(resultPage);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(18);
        layout->addWidget(pageTitle(QStringLiteral("Three demo shares")));
        layout->addWidget(secondaryLabel(QStringLiteral("Any two would be enough in the real workflow. These tokens are only fixtures."), 12.5));

        auto *surface = new Surface;
        surface->setObjectName(QStringLiteral("createdShares"));
        auto *surfaceLayout = new QVBoxLayout(surface);
        surfaceLayout->setContentsMargins(20, 8, 14, 8);
        surfaceLayout->setSpacing(0);
        for (int i = 0; i < kDemoTokens.size(); ++i) {
            if (i > 0)
                surfaceLayout->addWidget(new Hairline);
            auto *row = new QWidget;
            row->setFixedHeight(52);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(12);
            auto *number = textLabel(QString::number(i + 1), 11.0, QFont::DemiBold);
            number->setAlignment(Qt::AlignCenter);
            number->setFixedSize(24, 24);
            rowLayout->addWidget(number);
            auto *token = textLabel(kDemoTokens.at(i), 11.5, QFont::Medium);
            token->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            token->setTextInteractionFlags(Qt::TextSelectableByMouse);
            rowLayout->addWidget(token, 1);
            auto *copy = new QPushButton(QStringLiteral("Copy"));
            copy->setObjectName(QStringLiteral("copyShare%1").arg(i + 1));
            copy->setAccessibleName(QStringLiteral("Copy demo share %1").arg(i + 1));
            copy->setAutoDefault(false);
            copy->setFixedHeight(30);
            styleQuietButton(copy);
            rowLayout->addWidget(copy);
            const QString fakeToken = kDemoTokens.at(i);
            connect(copy, &QPushButton::clicked, this, [fakeToken] {
                QApplication::clipboard()->setText(fakeToken);
            });
            surfaceLayout->addWidget(row);
        }
        layout->addWidget(surface);

        auto *summary = new QHBoxLayout;
        summary->addWidget(secondaryLabel(QStringLiteral("Threshold")));
        summary->addWidget(textLabel(QStringLiteral("2 of 3"), 11.5, QFont::DemiBold));
        summary->addSpacing(14);
        summary->addWidget(secondaryLabel(QStringLiteral("Encoding")));
        summary->addWidget(textLabel(QStringLiteral("Words"), 11.5, QFont::DemiBold));
        summary->addStretch();
        layout->addLayout(summary);
        layout->addStretch();
        return resultPage;
    }

    void buildCreatePage() {
        createStates_ = new QStackedWidget;
        createStates_->addWidget(createEntry());
        createStates_->addWidget(createdResult());
        addPage(createStates_);
    }

    void buildRecoverPage() {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(18);
        layout->addWidget(pageTitle(QStringLiteral("Recover a secret")));
        layout->addWidget(secondaryLabel(QStringLiteral("Paste two different demo tokens to reveal the fixed example result."), 12.5));

        auto *surface = new Surface;
        auto *surfaceLayout = new QVBoxLayout(surface);
        surfaceLayout->setContentsMargins(20, 16, 20, 16);
        surfaceLayout->setSpacing(11);
        surfaceLayout->addWidget(textLabel(QStringLiteral("Demo shares"), 11.0, QFont::DemiBold));
        recoverInput_ = new QPlainTextEdit;
        recoverInput_->setObjectName(QStringLiteral("recoveryInput"));
        recoverInput_->setPlaceholderText(QStringLiteral("Paste one demo token per line"));
        recoverInput_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        recoverInput_->setFixedHeight(122);
        styleInset(recoverInput_);
        surfaceLayout->addWidget(recoverInput_);

        auto *action = new QHBoxLayout;
        action->setSpacing(12);
        status_ = secondaryLabel(QString(), 11.0);
        status_->setObjectName(QStringLiteral("recoveryStatus"));
        action->addWidget(status_, 1);
        recoverButton_ = new QPushButton(QStringLiteral("Recover demo secret"));
        recoverButton_->setObjectName(QStringLiteral("recoverButton"));
        recoverButton_->setDefault(true);
        recoverButton_->setFixedHeight(32);
        action->addWidget(recoverButton_);
        surfaceLayout->addLayout(action);

        result_ = new QWidget;
        result_->setObjectName(QStringLiteral("recoveryResult"));
        auto *resultLayout = new QVBoxLayout(result_);
        resultLayout->setContentsMargins(0, 5, 0, 0);
        resultLayout->setSpacing(7);
        resultLayout->addWidget(new Hairline);
        auto *resultHeader = new QHBoxLayout;
        resultHeader->setContentsMargins(0, 4, 0, 0);
        resultHeader->addWidget(textLabel(QStringLiteral("Demo result"), 11.0, QFont::DemiBold));
        resultHeader->addStretch();
        resultHeader->addWidget(secondaryLabel(QStringLiteral("Fixed synthetic output"), 10.5));
        resultLayout->addLayout(resultHeader);
        auto *resultText = textLabel(kDemoSecret, 12.0, QFont::Medium);
        resultText->setObjectName(QStringLiteral("result"));
        resultText->setTextInteractionFlags(Qt::TextSelectableByMouse);
        resultText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        resultLayout->addWidget(resultText);
        result_->setVisible(false);
        surfaceLayout->addWidget(result_);
        layout->addWidget(surface);
        layout->addStretch();

        connect(recoverInput_, &QPlainTextEdit::textChanged, this, [this] { refreshRecoveryState(); });
        connect(recoverButton_, &QPushButton::clicked, this, [this] { recover(); });
        addPage(page);
        refreshRecoveryState();
    }

    QStringList enteredTokens() const {
        QStringList tokens;
        const QStringList lines = recoverInput_->toPlainText().split(
            QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QString token = line.trimmed();
            if (!token.isEmpty())
                tokens.append(token);
        }
        return tokens;
    }

    void refreshRecoveryState() {
        result_->setVisible(false);
        const QStringList tokens = enteredTokens();
        QStringList distinctKnown;
        int duplicateCount = 0;
        int unknownCount = 0;
        for (const QString &token : tokens) {
            if (!kDemoTokens.contains(token))
                ++unknownCount;
            else if (distinctKnown.contains(token))
                ++duplicateCount;
            else
                distinctKnown.append(token);
        }

        const bool ready = distinctKnown.size() >= 2 && unknownCount == 0 && duplicateCount == 0;
        recoverButton_->setEnabled(ready);
        if (tokens.isEmpty())
            status_->setText(QStringLiteral("Add 2 different demo shares."));
        else if (unknownCount > 0)
            status_->setText(QStringLiteral("Unknown token. Use a token from Create."));
        else if (duplicateCount > 0)
            status_->setText(QStringLiteral("Remove the duplicate token."));
        else if (!ready)
            status_->setText(QStringLiteral("1 of 2 shares added."));
        else
            status_->setText(QStringLiteral("Ready with %1 different shares.").arg(distinctKnown.size()));
    }

    void recover() {
        if (recoverButton_->isEnabled())
            result_->setVisible(true);
    }

    void setMode(int index) {
        if (!pages_ || index < 0 || index > 1)
            return;
        modeSelector_->blockSignals(true);
        modeSelector_->setCurrentIndex(index);
        modeSelector_->blockSignals(false);
        pages_->setCurrentIndex(index);
        resetOperation();
    }

    void resetOperation() {
        createStates_->setCurrentIndex(0);
        createButton_->setEnabled(true);
        recoverInput_->clear();
        result_->setVisible(false);
    }

    ModeSelector *modeSelector_ = nullptr;
    QToolButton *helpButton_ = nullptr;
    QToolButton *resetButton_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QStackedWidget *createStates_ = nullptr;
    QLineEdit *secretField_ = nullptr;
    QPushButton *createButton_ = nullptr;
    QPlainTextEdit *recoverInput_ = nullptr;
    QLabel *status_ = nullptr;
    QPushButton *recoverButton_ = nullptr;
    QWidget *result_ = nullptr;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Safeparts Preview"));
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));

    const QStringList arguments = app.arguments();
    const int appearanceIndex = arguments.indexOf(QStringLiteral("--appearance"));
    QString requestedAppearance = QStringLiteral("system");
    if (appearanceIndex >= 0 && appearanceIndex + 1 < arguments.size()) {
        requestedAppearance = arguments.at(appearanceIndex + 1).toLower();
        if (requestedAppearance == QStringLiteral("light"))
            QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
        else if (requestedAppearance == QStringLiteral("dark"))
            QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
        else
            return 5;
        QApplication::processEvents();
    }

    PrototypeWindow window;
    if (arguments.contains(QStringLiteral("--small")))
        window.resize(window.minimumSize());

    const QFont resolvedFont = app.font();
    const Qt::ColorScheme activeScheme = QGuiApplication::styleHints()->colorScheme();
    const QString schemeName = activeScheme == Qt::ColorScheme::Dark ? QStringLiteral("dark")
        : activeScheme == Qt::ColorScheme::Light ? QStringLiteral("light") : QStringLiteral("unknown");
    qInfo().noquote() << QStringLiteral("Qt %1 | platform %2 | style %3 | system font %4 %5pt | appearance requested %6 active %7 | window %8x%9")
        .arg(qVersion(), QGuiApplication::platformName(), app.style()->name(), resolvedFont.family(),
             QString::number(resolvedFont.pointSizeF()), requestedAppearance, schemeName,
             QString::number(window.width()), QString::number(window.height()));

    const int captureIndex = arguments.indexOf(QStringLiteral("--capture"));
    if (captureIndex < 0) {
        window.show();
        return app.exec();
    }
    if (captureIndex + 1 >= arguments.size())
        return 2;

    const QString outputDirectory = QFileInfo(arguments.at(captureIndex + 1)).absoluteFilePath();
    if (!QDir().mkpath(outputDirectory))
        return 3;

    window.show();
    QTimer::singleShot(500, &app, [&window, outputDirectory, &app] {
        struct Capture {
            QString name;
            void (PrototypeWindow::*prepare)();
        };
        const QList<Capture> captures = {
            {QStringLiteral("create.png"), nullptr},
            {QStringLiteral("created-shares.png"), &PrototypeWindow::showCreatedShares},
            {QStringLiteral("recover-ready.png"), &PrototypeWindow::showRecoverReady},
            {QStringLiteral("recovered-result.png"), &PrototypeWindow::showRecoveredResult},
        };
        bool saved = true;
        for (const Capture &capture : captures) {
            if (capture.prepare)
                (window.*capture.prepare)();
            QApplication::processEvents();
            QThread::msleep(100);
            QApplication::processEvents();
            const QPixmap shot = window.screen()->grabWindow(window.winId());
            saved = shot.save(QDir(outputDirectory).filePath(capture.name), "PNG") && saved;
        }
        app.exit(saved ? 0 : 4);
    });
    return app.exec();
}
