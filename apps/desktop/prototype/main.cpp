#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {
const QStringList kDemoTokens = {
    QStringLiteral("DEMO-SHARE-A7-KITE"),
    QStringLiteral("DEMO-SHARE-B4-MOSS"),
    QStringLiteral("DEMO-SHARE-C9-LAMP"),
};
const QString kDemoSecret = QStringLiteral("orchard-window-cobalt");

QLabel *sectionTitle(const QString &text) {
    auto *label = new QLabel(text);
    QFont font = label->font();
    font.setPointSize(17);
    font.setBold(true);
    label->setFont(font);
    return label;
}

QFrame *panel() {
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}
} // namespace

class PrototypeWindow final : public QWidget {
public:
    PrototypeWindow() {
        setWindowTitle(QStringLiteral("Safeparts UI Prototype (simulated data)"));
        setFixedSize(900, 680);
        setStyleSheet(QStringLiteral(R"(
            QWidget { background: #f4f5f7; color: #172033; font-size: 14px; }
            QFrame#banner { background: #fff2cc; border: 1px solid #d9ad35; border-radius: 7px; }
            QFrame#panel { background: white; border: 1px solid #d7dce5; border-radius: 8px; }
            QPushButton { background: #e8ebf1; border: 1px solid #bac2d0; border-radius: 6px; padding: 8px 14px; }
            QPushButton:hover { background: #dde2eb; }
            QPushButton#primary { background: #2357a6; color: white; border-color: #17427f; font-weight: 600; }
            QPushButton#modeButton:checked { background: #dbe8fb; border-color: #2357a6; font-weight: 600; }
            QPushButton:disabled { color: #808895; background: #eceef2; }
            QPlainTextEdit, QComboBox { background: white; border: 1px solid #aeb7c5; border-radius: 5px; padding: 7px; }
            QLabel#muted { color: #596273; }
            QLabel#status { background: #eef3fa; border-radius: 5px; padding: 9px; }
            QLabel#result { background: #e7f5e9; border: 1px solid #85b88b; border-radius: 6px; padding: 10px; }
        )"));

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(24, 20, 24, 20);
        root->setSpacing(14);

        auto *banner = new QFrame;
        banner->setObjectName(QStringLiteral("banner"));
        auto *bannerLayout = new QVBoxLayout(banner);
        auto *bannerTitle = new QLabel(QStringLiteral("UI PROTOTYPE · SIMULATED DATA ONLY"));
        QFont bannerFont = bannerTitle->font();
        bannerFont.setBold(true);
        bannerTitle->setFont(bannerFont);
        bannerLayout->addWidget(bannerTitle);
        bannerLayout->addWidget(new QLabel(QStringLiteral(
            "This mock does not use cryptography and is not safe for real secrets. Use only the fake demo tokens shown here.")));
        root->addWidget(banner);

        auto *toolbar = new QHBoxLayout;
        createModeButton_ = new QPushButton(QStringLiteral("Create shares"));
        recoverModeButton_ = new QPushButton(QStringLiteral("Recover secret"));
        for (auto *button : {createModeButton_, recoverModeButton_}) {
            button->setObjectName(QStringLiteral("modeButton"));
            button->setCheckable(true);
            toolbar->addWidget(button);
        }
        toolbar->addStretch();
        auto *helpButton = new QPushButton(QStringLiteral("Help"));
        auto *resetButton = new QPushButton(QStringLiteral("Start over"));
        toolbar->addWidget(helpButton);
        toolbar->addWidget(resetButton);
        root->addLayout(toolbar);

        pages_ = new QStackedWidget;
        buildCreatePage();
        buildRecoverPage();
        root->addWidget(pages_, 1);

        connect(createModeButton_, &QPushButton::clicked, this, [this] { setMode(0); });
        connect(recoverModeButton_, &QPushButton::clicked, this, [this] { setMode(1); });
        connect(resetButton, &QPushButton::clicked, this, [this] { resetCurrentMode(); });
        connect(helpButton, &QPushButton::clicked, this, [this] {
            QMessageBox::information(this, QStringLiteral("Prototype help"),
                QStringLiteral("Create shares shows three fixed fake tokens. Recover secret accepts only those tokens, one per line. Two different demo tokens make the mock ready. Nothing entered here is parsed as a real recovery share."));
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
    void buildCreatePage() {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
        layout->addWidget(sectionTitle(QStringLiteral("Create recovery shares")));

        auto *settings = panel();
        auto *settingsLayout = new QHBoxLayout(settings);
        settingsLayout->addWidget(new QLabel(QStringLiteral("Threshold")));
        auto *threshold = new QComboBox;
        threshold->addItem(QStringLiteral("2 shares"));
        settingsLayout->addWidget(threshold);
        settingsLayout->addWidget(new QLabel(QStringLiteral("Share count")));
        auto *count = new QComboBox;
        count->addItem(QStringLiteral("3 shares"));
        settingsLayout->addWidget(count);
        settingsLayout->addWidget(new QLabel(QStringLiteral("Encoding")));
        auto *encoding = new QComboBox;
        encoding->addItem(QStringLiteral("Words"));
        settingsLayout->addWidget(encoding);
        settingsLayout->addStretch();
        layout->addWidget(settings);

        auto *secretPanel = panel();
        auto *secretLayout = new QVBoxLayout(secretPanel);
        secretLayout->addWidget(new QLabel(QStringLiteral("Synthetic secret fixture")));
        auto *secret = new QPlainTextEdit(kDemoSecret);
        secret->setReadOnly(true);
        secret->setFixedHeight(58);
        secretLayout->addWidget(secret);
        auto *fixtureHint = new QLabel(QStringLiteral("Prefilled and read-only for this visual checkpoint. Real text entry is not connected."));
        fixtureHint->setObjectName(QStringLiteral("muted"));
        secretLayout->addWidget(fixtureHint);
        createButton_ = new QPushButton(QStringLiteral("Create demo shares"));
        createButton_->setObjectName(QStringLiteral("primary"));
        secretLayout->addWidget(createButton_, 0, Qt::AlignLeft);
        layout->addWidget(secretPanel);

        createdShares_ = panel();
        auto *sharesLayout = new QVBoxLayout(createdShares_);
        sharesLayout->addWidget(sectionTitle(QStringLiteral("Demo shares created")));
        auto *warning = new QLabel(QStringLiteral("FAKE AND INVALID AS REAL RECOVERY SHARES"));
        QFont warningFont = warning->font();
        warningFont.setBold(true);
        warning->setFont(warningFont);
        sharesLayout->addWidget(warning);
        for (int i = 0; i < kDemoTokens.size(); ++i) {
            auto *row = new QHBoxLayout;
            auto *token = new QPlainTextEdit(kDemoTokens.at(i));
            token->setReadOnly(true);
            token->setFixedHeight(45);
            auto *copy = new QPushButton(QStringLiteral("Copy share %1").arg(i + 1));
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
        pages_->addWidget(page);
    }

    void buildRecoverPage() {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
        layout->addWidget(sectionTitle(QStringLiteral("Recover a secret")));

        auto *inputPanel = panel();
        auto *inputLayout = new QVBoxLayout(inputPanel);
        inputLayout->addWidget(new QLabel(QStringLiteral("Paste demo tokens, one per line")));
        recoverInput_ = new QPlainTextEdit;
        recoverInput_->setPlaceholderText(QStringLiteral("DEMO-SHARE-A7-KITE\nDEMO-SHARE-B4-MOSS"));
        recoverInput_->setFixedHeight(125);
        inputLayout->addWidget(recoverInput_);
        status_ = new QLabel;
        status_->setObjectName(QStringLiteral("status"));
        status_->setWordWrap(true);
        inputLayout->addWidget(status_);
        recoverButton_ = new QPushButton(QStringLiteral("Recover demo secret"));
        recoverButton_->setObjectName(QStringLiteral("primary"));
        inputLayout->addWidget(recoverButton_, 0, Qt::AlignLeft);
        layout->addWidget(inputPanel);

        result_ = panel();
        auto *resultLayout = new QVBoxLayout(result_);
        auto *resultTitle = new QLabel(QStringLiteral("SIMULATED PROTOTYPE RESULT"));
        QFont resultFont = resultTitle->font();
        resultFont.setBold(true);
        resultTitle->setFont(resultFont);
        resultLayout->addWidget(resultTitle);
        auto *resultText = new QLabel(kDemoSecret);
        resultText->setObjectName(QStringLiteral("result"));
        resultText->setTextInteractionFlags(Qt::TextSelectableByMouse);
        resultLayout->addWidget(resultText);
        resultLayout->addWidget(new QLabel(QStringLiteral("Fixed fixture only. No recovery or cryptography occurred.")));
        result_->setVisible(false);
        layout->addWidget(result_);
        layout->addStretch();

        connect(recoverInput_, &QPlainTextEdit::textChanged, this, [this] { refreshRecoveryState(); });
        connect(recoverButton_, &QPushButton::clicked, this, [this] { recover(); });
        pages_->addWidget(page);
        refreshRecoveryState();
    }

    QStringList enteredTokens() const {
        QStringList tokens;
        const QStringList lines = recoverInput_->toPlainText().split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
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
            if (!kDemoTokens.contains(token)) {
                ++unknownCount;
            } else if (distinctKnown.contains(token)) {
                ++duplicateCount;
            } else {
                distinctKnown.append(token);
            }
        }

        const bool ready = distinctKnown.size() >= 2 && unknownCount == 0 && duplicateCount == 0;
        recoverButton_->setEnabled(ready);
        if (tokens.isEmpty()) {
            status_->setText(QStringLiteral("Add 2 different demo tokens to become ready."));
        } else if (unknownCount > 0) {
            status_->setText(QStringLiteral("Unknown demo token. Use only the three fake tokens from Create shares."));
        } else if (duplicateCount > 0) {
            status_->setText(QStringLiteral("Duplicate token. Remove repeated lines before recovering."));
        } else if (!ready) {
            status_->setText(QStringLiteral("1 of 2 different demo tokens added. Add one more."));

        } else {
            status_->setText(QStringLiteral("Ready to recover with %1 different demo tokens.").arg(distinctKnown.size()));
        }
    }

    void recover() {
        if (recoverButton_->isEnabled())
            result_->setVisible(true);
    }

    void setMode(int index) {
        pages_->setCurrentIndex(index);
        createModeButton_->setChecked(index == 0);
        recoverModeButton_->setChecked(index == 1);
        resetCurrentMode();
    }

    void resetCurrentMode() {
        createdShares_->setVisible(false);
        createButton_->setEnabled(true);
        recoverInput_->clear();
        result_->setVisible(false);
    }

    QStackedWidget *pages_ = nullptr;
    QPushButton *createModeButton_ = nullptr;
    QPushButton *recoverModeButton_ = nullptr;
    QPushButton *createButton_ = nullptr;
    QFrame *createdShares_ = nullptr;
    QPlainTextEdit *recoverInput_ = nullptr;
    QLabel *status_ = nullptr;
    QPushButton *recoverButton_ = nullptr;
    QFrame *result_ = nullptr;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setStyle(QStringLiteral("Fusion"));
    PrototypeWindow window;

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
    QTimer::singleShot(250, &app, [&window, outputDirectory, &app] {
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
            saved = window.grab().save(QDir(outputDirectory).filePath(capture.name), "PNG") && saved;
        }
        app.exit(saved ? 0 : 4);
    });
    return app.exec();
}
