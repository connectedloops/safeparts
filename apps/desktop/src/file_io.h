#pragma once

#include "secure_byte_buffer.h"

#include <QString>

#include <functional>
#include <memory>

class FileDevice {
public:
    virtual ~FileDevice() = default;
    virtual bool openReadOnly() = 0;
    virtual bool openWriteTruncate() = 0;
    virtual qint64 read(char *data, qint64 maximum) = 0;
    virtual qint64 write(const char *data, qint64 size) = 0;
    virtual bool flush() = 0;
    virtual bool close() = 0;
};

class FileIo final {
public:
    enum class Status { Ok, OpenFailed, TooLarge, ReadFailed, WriteFailed, FlushFailed, CloseFailed };
    struct ReadResult {
        Status status = Status::ReadFailed;
        SecureByteBuffer bytes;
    };
    using DeviceFactory = std::function<std::unique_ptr<FileDevice>(const QString &path)>;

    explicit FileIo(DeviceFactory factory = {});
    ReadResult readBounded(const QString &path, qsizetype inclusiveMaximum) const;
    Status writeDirect(const QString &path, const SecureByteBuffer &bytes) const;

private:
    DeviceFactory factory_;
};
