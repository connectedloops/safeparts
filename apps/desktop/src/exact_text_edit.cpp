#include "exact_text_edit.h"

#include "clipboard.h"

#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QStringView>

#include <optional>

namespace {
constexpr qsizetype kMaximumSecretBytes = 1'048'576;

std::optional<qsizetype> utf8Length(QStringView text) {
    qsizetype bytes = 0;
    for (qsizetype index = 0; index < text.size(); ++index) {
        const char16_t value = text.at(index).unicode();
        qsizetype encoded = 0;
        if (QChar::isHighSurrogate(value)) {
            if (index + 1 >= text.size() || !QChar::isLowSurrogate(text.at(index + 1).unicode()))
                return std::nullopt;
            ++index;
            encoded = 4;
        } else if (QChar::isLowSurrogate(value)) {
            return std::nullopt;
        } else if (value < 0x80) {
            encoded = 1;
        } else if (value < 0x800) {
            encoded = 2;
        } else {
            encoded = 3;
        }
        if (bytes > kMaximumSecretBytes - encoded)
            return kMaximumSecretBytes + 1;
        bytes += encoded;
    }
    return bytes;
}
} // namespace

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
    exactUtf8Size_ = 0;
    renderAt(0);
    emit exactTextChanged();
}

void ExactTextEdit::keyPressEvent(QKeyEvent *event) {
    if (event->matches(QKeySequence::Paste)) {
        const ClipboardRead paste = readClipboardUtf8(kMaximumSecretBytes);
        if (paste.status != ClipboardRead::Status::Ok) {
            emit inputRejected(static_cast<int>(paste.status));
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
    if (!event->commitString().isEmpty()) {
        QTextCursor cursor = textCursor();
        if (event->replacementStart() != 0 || event->replacementLength() != 0) {
            const int start = cursor.position() + event->replacementStart();
            const int end = start + event->replacementLength();
            if (start < 0 || end < start || end > exact_.size()) {
                emit inputRejected(static_cast<int>(ClipboardRead::Status::InvalidUtf8));
                event->accept();
                return;
            }
            cursor.setPosition(start);
            cursor.setPosition(end, QTextCursor::KeepAnchor);
            setTextCursor(cursor);
        }
        insertExact(event->commitString());
    }
    event->accept();
}

bool ExactTextEdit::insertExact(const QString &text) {
    const QTextCursor cursor = textCursor();
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    const auto insertedBytes = utf8Length(QStringView(text));
    const auto removedBytes = utf8Length(QStringView(exact_).mid(start, end - start));
    if (!insertedBytes || !removedBytes) {
        emit inputRejected(static_cast<int>(ClipboardRead::Status::InvalidUtf8));
        return false;
    }
    if (*removedBytes > exactUtf8Size_) {
        emit inputRejected(static_cast<int>(ClipboardRead::Status::InvalidUtf8));
        return false;
    }
    const qsizetype retainedBytes = exactUtf8Size_ - *removedBytes;
    if (*insertedBytes > kMaximumSecretBytes
        || retainedBytes > kMaximumSecretBytes - *insertedBytes) {
        emit inputRejected(static_cast<int>(ClipboardRead::Status::TooLarge));
        return false;
    }

    exact_.replace(start, end - start, text);
    exactUtf8Size_ = retainedBytes + *insertedBytes;
    renderAt(start + text.size());
    emit exactTextChanged();
    return true;
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
    const auto removedBytes = utf8Length(QStringView(exact_).mid(start, length));
    if (!removedBytes)
        return;
    exact_.remove(start, length);
    exactUtf8Size_ -= *removedBytes;
    renderAt(start);
    emit exactTextChanged();
}

void ExactTextEdit::renderAt(int position) {
    const QSignalBlocker blocker(this);
    QString presentation = exact_;
    // QTextDocument normalizes carriage returns. A one-code-unit display
    // substitute keeps its cursor positions aligned with the exact model.
    presentation.replace(u'\r', QChar::LineSeparator);
    QPlainTextEdit::setPlainText(presentation);
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
