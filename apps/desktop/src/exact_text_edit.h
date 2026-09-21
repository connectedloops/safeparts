#pragma once

#include <QPlainTextEdit>

class QContextMenuEvent;
class QMimeData;

class ExactTextEdit final : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit ExactTextEdit(QWidget *parent = nullptr, qsizetype maximumUtf8Bytes = 1'048'576);
    QByteArray exactUtf8() const;
    qsizetype exactUtf8Size() const;
    void clearExact();

signals:
    void exactTextChanged();
    void inputRejected(int reason);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    bool insertExact(const QString &text);
    void pasteFromClipboard();
    void removeSelectionOrCharacter(bool backwards);
    void renderAt(int position);
    void copySelection() const;

    QString exact_;
    qsizetype exactUtf8Size_ = 0;
    qsizetype maximumUtf8Bytes_ = 0;
};
