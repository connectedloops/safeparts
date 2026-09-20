#include "exact_text_edit.h"

#include "clipboard.h"

#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QTextCursor>

namespace {
constexpr qsizetype kMaximumSecretBytes = 1'048'576;
}

ExactTextEdit::ExactTextEdit(QWidget *parent) : QPlainTextEdit(parent) {
    setUndoRedoEnabled(false);
    setAcceptDrops(false);
    setTabChangesFocus(true);
}

QByteArray ExactTextEdit::exactUtf8() const {
    return exact_.toUtf8();
}

void ExactTextEdit::clearExact() {
    if (exact_.isEmpty()) {
        QPlainTextEdit::clear();
        return;
    }
    exact_.clear();
    renderAt(0);
    emit exactTextChanged();
}

void ExactTextEdit::keyPressEvent(QKeyEvent *event) {
    if (event->matches(QKeySequence::Paste)) {
        const ClipboardRead paste = readClipboardUtf8(kMaximumSecretBytes);
        if (paste.status != ClipboardRead::Status::Ok) {
            emit pasteRejected(static_cast<int>(paste.status));
            event->accept();
            return;
        }
        insertExact(QString::fromUtf8(paste.bytes.constData(), paste.bytes.size()));
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        copySelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Cut)) {
        if (textCursor().hasSelection()) {
            copySelection();
            removeSelectionOrCharacter(false);
        }
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo)) {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Backspace) {
        removeSelectionOrCharacter(true);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete) {
        removeSelectionOrCharacter(false);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        insertExact(QStringLiteral("\n"));
        event->accept();
        return;
    }
    const Qt::KeyboardModifiers textBlockingModifiers =
        Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    if (!event->text().isEmpty() && !(event->modifiers() & textBlockingModifiers)) {
        insertExact(event->text());
        event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void ExactTextEdit::inputMethodEvent(QInputMethodEvent *event) {
    if (!event->commitString().isEmpty())
        insertExact(event->commitString());
    event->accept();
}

void ExactTextEdit::insertExact(const QString &text) {
    QString lfText = text;
    lfText.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    lfText.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QTextCursor cursor = textCursor();
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    exact_.replace(start, end - start, lfText);
    renderAt(start + lfText.size());
    emit exactTextChanged();
}

void ExactTextEdit::removeSelectionOrCharacter(bool backwards) {
    const QTextCursor cursor = textCursor();
    int start = cursor.selectionStart();
    int length = cursor.selectionEnd() - start;
    if (length == 0) {
        QTextCursor adjacent = cursor;
        const QTextCursor::MoveOperation direction =
            backwards ? QTextCursor::PreviousCharacter : QTextCursor::NextCharacter;
        if (adjacent.movePosition(direction, QTextCursor::KeepAnchor)) {
            start = adjacent.selectionStart();
            length = adjacent.selectionEnd() - start;
        }
    }
    if (length == 0)
        return;
    exact_.remove(start, length);
    renderAt(start);
    emit exactTextChanged();
}

void ExactTextEdit::renderAt(int position) {
    const QSignalBlocker blocker(this);
    QPlainTextEdit::setPlainText(exact_);
    QTextCursor cursor = textCursor();
    cursor.setPosition(qBound(0, position, document()->characterCount() - 1));
    setTextCursor(cursor);
}

void ExactTextEdit::copySelection() const {
    const QTextCursor cursor = textCursor();
    if (!cursor.hasSelection())
        return;
    writeClipboardUtf8(exact_.mid(cursor.selectionStart(), cursor.selectionEnd() - cursor.selectionStart()).toUtf8());
}
