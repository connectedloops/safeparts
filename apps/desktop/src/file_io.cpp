#include "file_io.h"

#include <QFile>

#include <algorithm>
#include <utility>

namespace {
class DirectQFileDevice final : public FileDevice {
public:
    explicit DirectQFileDevice(QString path) : file_(std::move(path)) {}
    bool openReadOnly() override { return file_.open(QIODevice::ReadOnly); }
    bool openWriteTruncate() override { return file_.open(QIODevice::WriteOnly | QIODevice::Truncate); }
    qint64 read(char *data, qint64 maximum) override { return file_.read(data, maximum); }
    qint64 write(const char *data, qint64 size) override { return file_.write(data, size); }
    bool flush() override { return file_.flush(); }
    bool close() override {
        file_.close();
        return file_.error() == QFileDevice::NoError;
    }

private:
    QFile file_;
};

constexpr qsizetype kChunkBytes = 64 * 1024;
} // namespace

FileIo::FileIo(DeviceFactory factory) : factory_(std::move(factory)) {
    if (!factory_)
        factory_ = [](const QString &path) { return std::make_unique<DirectQFileDevice>(path); };
}

FileIo::ReadResult FileIo::readBounded(const QString &path, qsizetype inclusiveMaximum) const {
    auto device = factory_(path);
    if (!device || !device->openReadOnly())
        return {Status::OpenFailed, {}};

    QByteArray bytes;
    const qsizetype observedMaximum = inclusiveMaximum + 1;
    while (bytes.size() < observedMaximum) {
        const qsizetype request = std::min(kChunkBytes, observedMaximum - bytes.size());
        QByteArray chunk(request, Qt::Uninitialized);
        const qint64 count = device->read(chunk.data(), request);
        if (count < 0) {
            device->close();
            return {Status::ReadFailed, {}};
        }
        if (count == 0)
            break;
        chunk.resize(static_cast<qsizetype>(count));
        bytes.append(chunk);
    }
    const bool closed = device->close();
    if (!closed)
        return {Status::ReadFailed, {}};
    if (bytes.size() > inclusiveMaximum) {
        bytes.fill(0);
        return {Status::TooLarge, {}};
    }
    return {Status::Ok, SecureByteBuffer::take(std::move(bytes))};
}

FileIo::Status FileIo::writeDirect(const QString &path, const SecureByteBuffer &bytes) const {
    auto device = factory_(path);
    if (!device || !device->openWriteTruncate())
        return Status::OpenFailed;
    const qint64 count = device->write(bytes.data(), bytes.size());
    if (count != bytes.size()) {
        device->close();
        return Status::WriteFailed;
    }
    if (!device->flush()) {
        device->close();
        return Status::FlushFailed;
    }
    if (!device->close())
        return Status::CloseFailed;
    return Status::Ok;
}
