#pragma once

#include <QByteArray>
#include <QByteArrayView>

struct ClipboardRead final {
    enum class Status { Ok, Empty, TooLarge, InvalidUtf8, Unavailable };

    Status status = Status::Unavailable;
    QByteArray bytes;
};

ClipboardRead readClipboardUtf8(qsizetype maximumBytes);
bool writeClipboardUtf8(QByteArrayView bytes);
