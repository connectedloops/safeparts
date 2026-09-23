#include "rust_worker.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace {
rust::Slice<const std::uint8_t> slice(const SecureByteBuffer &bytes) {
    static constexpr std::uint8_t empty = 0;
    const auto *data = bytes.isEmpty()
                           ? &empty
                           : reinterpret_cast<const std::uint8_t *>(bytes.data());
    return {data, static_cast<std::size_t>(bytes.size())};
}

int statusValue(Status status) {
    return static_cast<int>(static_cast<std::uint8_t>(status));
}

ShareEncoding shareEncoding(int value) {
    switch (value) {
    case 1:
        return ShareEncoding::Base64url;
    case 2:
        return ShareEncoding::Base58check;
    case 3:
        return ShareEncoding::MnemoWords;
    case 4:
        return ShareEncoding::MnemoBip39;
    default:
        return ShareEncoding::Auto;
    }
}
} // namespace

RustWorker::RustWorker(QObject *parent) : QObject(parent), operation_(new_operation()) {}

void RustWorker::reset(quint64 generation) {
    emitOperation(operation_->reset(generation));
}

void RustWorker::create(quint64 generation, SecureByteBuffer secret, quint8 threshold,
                        quint8 shareCount, int encoding, SecureByteBuffer passphrase) {
    emitOperation(operation_->create_with_passphrase(generation, slice(secret), threshold, shareCount,
                                                     shareEncoding(encoding), slice(passphrase)));
}

void RustWorker::encodeShare(quint64 generation, quint16 index, int purpose) {
    BytesOutput output = operation_->encode_share(generation, index);
    emit bytesFinished(output.generation, statusValue(output.status), copyAndWipeBytes(output.bytes),
                       purpose, index);
}

void RustWorker::replaceRecovery(quint64 generation, QList<SecureByteBuffer> inputs, int encoding) {
    std::uint64_t totalBytes = 0;
    bool overflow = false;
    for (const SecureByteBuffer &input : std::as_const(inputs)) {
        const auto size = static_cast<std::uint64_t>(input.size());
        if (size > std::numeric_limits<std::uint64_t>::max() - totalBytes) {
            overflow = true;
            break;
        }
        totalBytes += size;
    }
    const quint16 batchCount = inputs.size() > std::numeric_limits<quint16>::max()
                                   ? std::numeric_limits<quint16>::max()
                                   : static_cast<quint16>(inputs.size());
    OperationOutput output = operation_->begin_recovery_replace(
        generation, overflow ? std::numeric_limits<std::uint64_t>::max() : totalBytes,
        batchCount, shareEncoding(encoding));
    if (output.status == Status::Ok) {
        for (SecureByteBuffer &input : inputs) {
            output = operation_->stage_recovery_batch(generation, slice(input));
            input = {};
            if (output.status != Status::Ok)
                break;
        }
        inputs.clear();
        if (output.status == Status::Ok)
            output = operation_->finish_recovery_replace(generation);
    } else {
        inputs.clear();
    }
    emitOperation(output);
}

void RustWorker::recover(quint64 generation, SecureByteBuffer passphrase) {
    BytesOutput output = operation_->recover_bytes_with_passphrase(generation, slice(passphrase));
    emit bytesFinished(output.generation, statusValue(output.status), copyAndWipeBytes(output.bytes), 1, 0);
}

void RustWorker::recoveredBytes(quint64 generation, int purpose) {
    BytesOutput output = operation_->recovered_bytes(generation);
    emit bytesFinished(output.generation, statusValue(output.status), copyAndWipeBytes(output.bytes), purpose, 0);
}

void RustWorker::emitOperation(const OperationOutput &output) {
    emit operationFinished(output.generation, statusValue(output.status), output.threshold,
                           output.share_count, output.supplied_count,
                           output.recovery_batch_count,
                           static_cast<int>(static_cast<std::uint8_t>(output.encoding)),
                           output.passphrase_protected, output.ready);
}

SecureByteBuffer RustWorker::copyAndWipeBytes(rust::Vec<std::uint8_t> &bytes) {
    QByteArray copy;
    if (bytes.size() <= static_cast<std::size_t>(std::numeric_limits<qsizetype>::max()))
        copy = {reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size())};
    std::fill(bytes.begin(), bytes.end(), std::uint8_t{0});
    return SecureByteBuffer::take(std::move(copy));
}
