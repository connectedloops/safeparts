#include "clipboard.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QStringDecoder>

ClipboardRead readClipboardUtf8(qsizetype maximumBytes) {
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (mime == nullptr || !mime->hasText())
        return {ClipboardRead::Status::Empty, {}};
    const QByteArray bytes = mime->data(QStringLiteral("text/plain"));
    if (bytes.size() > maximumBytes)
        return {ClipboardRead::Status::TooLarge, {}};
    QStringDecoder decoder(QStringDecoder::Utf8);
    decoder.decode(bytes);
    if (decoder.hasError())
        return {ClipboardRead::Status::InvalidUtf8, {}};
    return {ClipboardRead::Status::Ok, bytes};
}
