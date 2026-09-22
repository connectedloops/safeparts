#include "rust_worker.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace {
rust::Slice<const std::uint8_t> slice(const SecureByteBuffer &bytes) {
    return {reinterpret_cast<const std::uint8_t *>(bytes.data()), static_cast<std::size_t>(bytes.size())};
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
    OperationOutput output = operation_->reset(generation);
    std::optional<Status> firstFatalStatus;
    for (const SecureByteBuffer &input : inputs) {
        const quint16 previousBatchCount = output.recovery_batch_count;
        output = operation_->add_recovery(generation, slice(input), shareEncoding(encoding));
        const bool fatal = output.status != Status::Ok && output.status != Status::NotEnoughShares;
        if (fatal && !firstFatalStatus.has_value())
            firstFatalStatus = output.status;

        const bool retained = output.recovery_batch_count > previousBatchCount;
        if (fatal && !retained)
            break;
    }
    if (firstFatalStatus.has_value())
        output.status = *firstFatalStatus;
    emitOperation(output);
}

void RustWorker::recover(quint64 generation, SecureByteBuffer passphrase) {
    BytesOutput output = operation_->recover_with_passphrase(generation, slice(passphrase));
    emit bytesFinished(output.generation, statusValue(output.status), copyAndWipeBytes(output.bytes), 1, 0);
}

void RustWorker::recoveredText(quint64 generation) {
    BytesOutput output = operation_->recovered_text(generation);
    emit bytesFinished(output.generation, statusValue(output.status), copyAndWipeBytes(output.bytes), 2, 0);
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
