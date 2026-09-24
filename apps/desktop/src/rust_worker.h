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
    void create(quint64 generation, SecureByteBuffer secret, quint8 threshold, quint8 shareCount,
                int encoding, SecureByteBuffer passphrase);
    void encodeShare(quint64 generation, quint16 index, int purpose);
    void replaceRecovery(quint64 generation, QList<SecureByteBuffer> inputs, int encoding);
    void recover(quint64 generation, SecureByteBuffer passphrase);
    void recoveredBytes(quint64 generation, int purpose);

signals:
    void recoveryExecutionStarted(quint64 generation);
    void recoveryExecutionFinished(quint64 generation);
    void operationFinished(quint64 generation, int status, quint8 threshold, quint16 shareCount,
                           quint16 suppliedCount, quint16 batchCount, int encoding, bool protectedInput,
                           bool ready);
    void bytesFinished(quint64 generation, int status, SecureByteBuffer bytes, int purpose,
                       quint16 index, bool asciiValidated);

private:
    void emitOperation(const OperationOutput &output);
    static SecureByteBuffer copyAndWipeBytes(rust::Vec<std::uint8_t> &bytes);

    rust::Box<Operation> operation_;
};
