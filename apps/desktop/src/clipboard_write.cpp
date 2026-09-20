#include "clipboard.h"

#include <QApplication>
#include <QClipboard>
#include <QStringDecoder>

bool writeClipboardUtf8(QByteArrayView bytes) {
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(bytes);
    if (decoder.hasError())
        return false;
    QApplication::clipboard()->setText(text, QClipboard::Clipboard);
    return true;
}
