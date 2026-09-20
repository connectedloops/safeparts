#pragma once

#include <QPlainTextEdit>

class ExactTextEdit final : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit ExactTextEdit(QWidget *parent = nullptr);
    QByteArray exactUtf8() const;
    void clearExact();

signals:
    void exactTextChanged();
    void pasteRejected(int reason);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;

private:
    void insertExact(const QString &text);
    void removeSelectionOrCharacter(bool backwards);
    void renderAt(int position);
    void copySelection() const;

    QString exact_;
};
