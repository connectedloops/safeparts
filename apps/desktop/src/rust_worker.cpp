#include "rust_worker.h"

#include <algorithm>
#include <limits>

namespace {
rust::Slice<const std::uint8_t> slice(const QByteArray &bytes) {
    return {reinterpret_cast<const std::uint8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size())};
}

int statusValue(Status status) {
    return static_cast<int>(static_cast<std::uint8_t>(status));
}
} // namespace

RustWorker::RustWorker(QObject *parent) : QObject(parent), operation_(new_operation()) {}

void RustWorker::reset(quint64 generation) {
    emitOperation(operation_->reset(generation));
}

void RustWorker::create(quint64 generation, QByteArray secret, quint8 threshold, quint8 shareCount) {
    emitOperation(operation_->create_words(generation, slice(secret), threshold, shareCount));
    secret.fill('\0');
}

void RustWorker::encodeShare(quint64 generation, quint16 index) {
    BytesOutput output = operation_->encode_share(generation, index);
    emit bytesFinished(output.generation, statusValue(output.status), copyAndWipeBytes(output.bytes), 0, index);
}

void RustWorker::addRecovery(quint64 generation, QByteArray input) {
    emitOperation(operation_->add_recovery_words(generation, slice(input)));
    input.fill('\0');
}

void RustWorker::removeRecovery(quint64 generation, quint16 batchIndex) {
    emitOperation(operation_->remove_recovery_batch(generation, batchIndex));
}

void RustWorker::recover(quint64 generation) {
    BytesOutput output = operation_->recover_words(generation);
    emit bytesFinished(output.generation, statusValue(output.status), copyAndWipeBytes(output.bytes), 1, 0);
}

void RustWorker::recoveredText(quint64 generation) {
    BytesOutput output = operation_->recovered_text(generation);
    emit bytesFinished(output.generation, statusValue(output.status), copyAndWipeBytes(output.bytes), 2, 0);
}

void RustWorker::emitOperation(const OperationOutput &output) {
    emit operationFinished(output.generation, statusValue(output.status), output.threshold,
                           output.share_count, output.supplied_count,
                           output.recovery_batch_count, output.ready);
}

QByteArray RustWorker::copyAndWipeBytes(rust::Vec<std::uint8_t> &bytes) {
    QByteArray copy;
    if (bytes.size() <= static_cast<std::size_t>(std::numeric_limits<qsizetype>::max()))
        copy = {reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size())};
    std::fill(bytes.begin(), bytes.end(), std::uint8_t{0});
    return copy;
}
