#pragma once

#include "bridge.rs.h"
#include "secure_byte_buffer.h"

#include <QList>
#include <QObject>

class RustWorker final : public QObject {
    Q_OBJECT

public:
    explicit RustWorker(QObject *parent = nullptr);

public slots:
    void reset(quint64 generation);
    void create(quint64 generation, SecureByteBuffer secret, quint8 threshold, quint8 shareCount);
    void encodeShare(quint64 generation, quint16 index);
    void replaceRecovery(quint64 generation, QList<SecureByteBuffer> inputs);
    void recover(quint64 generation);
    void recoveredText(quint64 generation);

signals:
    void operationFinished(quint64 generation, int status, quint8 threshold, quint16 shareCount,
                           quint16 suppliedCount, quint16 batchCount, bool ready);
    void bytesFinished(quint64 generation, int status, SecureByteBuffer bytes, int purpose, quint16 index);

private:
    void emitOperation(const OperationOutput &output);
    static SecureByteBuffer copyAndWipeBytes(rust::Vec<std::uint8_t> &bytes);

    rust::Box<Operation> operation_;
};
