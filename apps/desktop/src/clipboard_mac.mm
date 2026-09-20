#include "clipboard.h"

#import <AppKit/AppKit.h>

#include <QStringDecoder>

ClipboardRead readClipboardUtf8(qsizetype maximumBytes) {
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    // AppKit exposes string payloads only as complete NSData/NSString values;
    // it has no supported length-only or ranged read before this acquisition.
    NSData *data = [pasteboard dataForType:NSPasteboardTypeString];
    if (data == nil)
        return {ClipboardRead::Status::Empty, {}};
    if ([data length] > static_cast<NSUInteger>(maximumBytes))
        return {ClipboardRead::Status::TooLarge, {}};

    QByteArray bytes(static_cast<const char *>([data bytes]), static_cast<qsizetype>([data length]));
    QStringDecoder decoder(QStringDecoder::Utf8);
    decoder.decode(bytes);
    if (decoder.hasError())
        return {ClipboardRead::Status::InvalidUtf8, {}};
    return {ClipboardRead::Status::Ok, bytes};
}
