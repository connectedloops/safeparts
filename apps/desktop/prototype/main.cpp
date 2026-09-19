#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
const QStringList kDemoTokens = {
    QStringLiteral("DEMO-SHARE-A7-KITE"),
    QStringLiteral("DEMO-SHARE-B4-MOSS"),
    QStringLiteral("DEMO-SHARE-C9-LAMP"),
};
const QString kDemoSecret = QStringLiteral("orchard-window-cobalt");

QFont adjustedFont(const QWidget *widget, int pointDelta, QFont::Weight weight = QFont::Normal) {
    QFont font = widget->font();
    font.setPointSize(qMax(9, font.pointSize() + pointDelta));
    font.setWeight(weight);
    return font;
}

QLabel *heading(const QString &text) {
    auto *label = new QLabel(text);
    label->setFont(adjustedFont(label, 8, QFont::DemiBold));
    return label;
}

QLabel *secondaryLabel(const QString &text) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    QPalette palette = label->palette();
    QColor secondary = palette.color(QPalette::WindowText);
    secondary.setAlphaF(0.72);
    palette.setColor(QPalette::WindowText, secondary);
    label->setPalette(palette);
    return label;
}

QFrame *separator() {
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}
} // namespace

class PrototypeWindow final : public QMainWindow {
public:
    PrototypeWindow() {
        setWindowTitle(QStringLiteral("Safeparts Preview"));
        resize(820, 650);
        setMinimumSize(680, 560);
        setUnifiedTitleAndToolBarOnMac(true);
        buildToolbar();

        auto *central = new QWidget;
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(38, 28, 38, 24);
        root->setSpacing(20);

        auto *previewHost = new QWidget;
        previewHost->setFixedHeight(34);
        auto *previewRow = new QHBoxLayout(previewHost);
        previewRow->setContentsMargins(0, 0, 0, 0);
        previewRow->setSpacing(9);
        auto *badge = new QLabel(QStringLiteral("Preview"));
        badge->setFrameShape(QFrame::StyledPanel);
        badge->setMargin(5);
        badge->setFont(adjustedFont(badge, -1, QFont::DemiBold));
        previewRow->addWidget(badge, 0, Qt::AlignVCenter);
        previewRow->addWidget(secondaryLabel(QStringLiteral("Demo data only. Do not use real secrets.")), 0, Qt::AlignVCenter);
        previewRow->addStretch();
        root->addWidget(previewHost);

        pages_ = new QStackedWidget;
        pages_->setMinimumSize(0, 0);
        pages_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        buildCreatePage();
        buildRecoverPage();
        root->addWidget(pages_, 1);
        setCentralWidget(central);

        connect(createModeButton_, &QPushButton::clicked, this, [this] { setMode(0); });
        connect(recoverModeButton_, &QPushButton::clicked, this, [this] { setMode(1); });
        connect(resetButton_, &QPushButton::clicked, this, [this] { resetOperation(); });
        connect(helpButton_, &QPushButton::clicked, this, [this] {
            QMessageBox::information(this, QStringLiteral("About this preview"),
                QStringLiteral("This is a visual demo with three fixed fake tokens. It does not perform cryptography, split a secret, parse recovery shares, or recover data. Never enter a real secret here."));
        });
        setMode(0);
    }

    void showCreatedShares() {
        createdShares_->setVisible(true);
        createButton_->setEnabled(false);
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
    void buildToolbar() {
        auto *toolbar = addToolBar(QStringLiteral("Main"));
        toolbar->setMovable(false);
        toolbar->setFloatable(false);
        toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);

        auto *modeHost = new QWidget;
        auto *modeLayout = new QHBoxLayout(modeHost);
        modeLayout->setContentsMargins(8, 4, 4, 4);
        modeLayout->setSpacing(6);
        createModeButton_ = new QPushButton(QStringLiteral("Create shares"));
        recoverModeButton_ = new QPushButton(QStringLiteral("Recover secret"));
        auto *modeGroup = new QButtonGroup(this);
        modeGroup->setExclusive(true);
        for (auto *button : {createModeButton_, recoverModeButton_}) {
            button->setCheckable(true);
            button->setAutoDefault(false);
            modeGroup->addButton(button);
            modeLayout->addWidget(button);
        }
        toolbar->addWidget(modeHost);

        auto *spacer = new QWidget;
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        toolbar->addWidget(spacer);

        auto *actionsHost = new QWidget;
        auto *actionsLayout = new QHBoxLayout(actionsHost);
        actionsLayout->setContentsMargins(4, 4, 8, 4);
        actionsLayout->setSpacing(6);
        helpButton_ = new QPushButton(QStringLiteral("Help"));
        resetButton_ = new QPushButton(QStringLiteral("Start over"));
        for (auto *button : {helpButton_, resetButton_}) {
            button->setFlat(true);
            button->setAutoDefault(false);
            actionsLayout->addWidget(button);
        }
        toolbar->addWidget(actionsHost);
    }

    void addPage(QWidget *page) {
        auto *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(page);
        pages_->addWidget(scroll);
    }

    void buildCreatePage() {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(18);

        layout->addWidget(heading(QStringLiteral("Create recovery shares")));
        layout->addWidget(secondaryLabel(QStringLiteral("Try the flow with a read-only example. The preview always creates three fake shares.")));

        auto *facts = new QHBoxLayout;
        facts->setSpacing(26);
        for (const auto &fact : {QStringLiteral("2 needed"), QStringLiteral("3 shares"), QStringLiteral("Words")}) {
            auto *label = new QLabel(fact);
            label->setFont(adjustedFont(label, 0, QFont::DemiBold));
            facts->addWidget(label);
        }
        facts->addStretch();
        layout->addLayout(facts);
        layout->addWidget(separator());

        auto *secretLabel = new QLabel(QStringLiteral("Example secret"));
        secretLabel->setFont(adjustedFont(secretLabel, 0, QFont::DemiBold));
        layout->addWidget(secretLabel);
        auto *secret = new QLineEdit(kDemoSecret);
        secret->setReadOnly(true);
        secret->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(secret);
        layout->addWidget(secondaryLabel(QStringLiteral("Synthetic and read-only for this preview.")));

        createButton_ = new QPushButton(QStringLiteral("Create demo shares"));
        createButton_->setDefault(true);
        createButton_->setMinimumWidth(154);
        layout->addWidget(createButton_, 0, Qt::AlignLeft);

        createdShares_ = new QFrame;
        auto *sharesLayout = new QVBoxLayout(createdShares_);
        sharesLayout->setContentsMargins(0, 8, 0, 0);
        sharesLayout->setSpacing(10);
        auto *resultHeader = new QHBoxLayout;
        auto *resultTitle = new QLabel(QStringLiteral("Three demo shares"));
        resultTitle->setFont(adjustedFont(resultTitle, 2, QFont::DemiBold));
        resultHeader->addWidget(resultTitle);
        resultHeader->addStretch();
        resultHeader->addWidget(secondaryLabel(QStringLiteral("Synthetic output")));
        sharesLayout->addLayout(resultHeader);

        for (int i = 0; i < kDemoTokens.size(); ++i) {
            if (i > 0)
                sharesLayout->addWidget(separator());
            auto *row = new QHBoxLayout;
            row->setSpacing(14);
            auto *number = new QLabel(QString::number(i + 1));
            number->setMinimumWidth(18);
            number->setAlignment(Qt::AlignCenter);
            number->setFont(adjustedFont(number, 0, QFont::DemiBold));
            auto *token = new QLabel(kDemoTokens.at(i));
            token->setTextInteractionFlags(Qt::TextSelectableByMouse);
            token->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            auto *copy = new QPushButton(QStringLiteral("Copy share %1").arg(i + 1));
            copy->setAutoDefault(false);
            row->addWidget(number);
            row->addWidget(token, 1);
            row->addWidget(copy);
            const QString fakeToken = kDemoTokens.at(i);
            connect(copy, &QPushButton::clicked, this, [fakeToken] {
                QApplication::clipboard()->setText(fakeToken);
            });
            sharesLayout->addLayout(row);
        }
        createdShares_->setVisible(false);
        layout->addWidget(createdShares_);
        layout->addStretch();
        connect(createButton_, &QPushButton::clicked, this, [this] { showCreatedShares(); });
        addPage(page);
    }

    void buildRecoverPage() {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(18);

        layout->addWidget(heading(QStringLiteral("Recover a secret")));
        layout->addWidget(secondaryLabel(QStringLiteral("Paste at least two different demo shares. This preview recognizes only its three fixed tokens.")));

        auto *inputLabel = new QLabel(QStringLiteral("Demo shares"));
        inputLabel->setFont(adjustedFont(inputLabel, 0, QFont::DemiBold));
        layout->addWidget(inputLabel);
        recoverInput_ = new QPlainTextEdit;
        recoverInput_->setPlaceholderText(QStringLiteral("Paste one demo token per line"));
        recoverInput_->setMinimumHeight(132);
        recoverInput_->setMaximumHeight(180);
        recoverInput_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(recoverInput_);

        auto *actionRow = new QHBoxLayout;
        actionRow->setSpacing(14);
        recoverButton_ = new QPushButton(QStringLiteral("Recover demo secret"));
        recoverButton_->setDefault(true);
        recoverButton_->setMinimumWidth(164);
        status_ = secondaryLabel(QString());
        status_->setObjectName(QStringLiteral("status"));
        actionRow->addWidget(recoverButton_);
        actionRow->addWidget(status_, 1);
        layout->addLayout(actionRow);

        result_ = new QFrame;
        result_->setFrameShape(QFrame::StyledPanel);
        auto *resultLayout = new QVBoxLayout(result_);
        resultLayout->setContentsMargins(18, 14, 18, 14);
        resultLayout->setSpacing(6);
        auto *resultTitle = new QLabel(QStringLiteral("Demo result"));
        resultTitle->setFont(adjustedFont(resultTitle, 0, QFont::DemiBold));
        resultLayout->addWidget(resultTitle);
        auto *resultText = new QLabel(kDemoSecret);
        resultText->setObjectName(QStringLiteral("result"));
        resultText->setTextInteractionFlags(Qt::TextSelectableByMouse);
        resultText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        resultLayout->addWidget(resultText);
        resultLayout->addWidget(secondaryLabel(QStringLiteral("Fixed synthetic result. No recovery occurred.")));
        result_->setVisible(false);
        layout->addWidget(result_);
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
            status_->setText(QStringLiteral("Unknown token. Use a token from Create shares."));
        else if (duplicateCount > 0)
            status_->setText(QStringLiteral("Remove duplicate tokens to continue."));
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
        pages_->setCurrentIndex(index);
        createModeButton_->setChecked(index == 0);
        recoverModeButton_->setChecked(index == 1);
        resetOperation();
    }

    void resetOperation() {
        createdShares_->setVisible(false);
        createButton_->setEnabled(true);
        recoverInput_->clear();
        result_->setVisible(false);
    }

    QStackedWidget *pages_ = nullptr;
    QPushButton *createModeButton_ = nullptr;
    QPushButton *recoverModeButton_ = nullptr;
    QPushButton *helpButton_ = nullptr;
    QPushButton *resetButton_ = nullptr;
    QPushButton *createButton_ = nullptr;
    QFrame *createdShares_ = nullptr;
    QPlainTextEdit *recoverInput_ = nullptr;
    QLabel *status_ = nullptr;
    QPushButton *recoverButton_ = nullptr;
    QFrame *result_ = nullptr;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    PrototypeWindow window;
    if (app.arguments().contains(QStringLiteral("--small")))
        window.resize(window.minimumSize());

    const QFont resolvedFont = app.font();
    qInfo().noquote() << QStringLiteral("Qt %1 | platform %2 | style %3 | font %4 %5pt")
                             .arg(qVersion(), QGuiApplication::platformName(), app.style()->name(),
                                  resolvedFont.family(), QString::number(resolvedFont.pointSizeF()));

    const QStringList arguments = app.arguments();
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
            QThread::msleep(120);
            QApplication::processEvents();
            const QPixmap shot = window.screen()->grabWindow(window.winId());
            saved = shot.save(QDir(outputDirectory).filePath(capture.name), "PNG") && saved;
        }
        app.exit(saved ? 0 : 4);
    });
    return app.exec();
}
