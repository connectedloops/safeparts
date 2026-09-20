#include "secure_byte_buffer.h"

#include <algorithm>
#include <mutex>
#include <utility>

namespace {
std::mutex observerMutex;
SecureByteBuffer::WipeObserver wipeObserver;

void observeWipe(QByteArrayView bytes) {
    SecureByteBuffer::WipeObserver observer;
    {
        const std::lock_guard lock(observerMutex);
        observer = wipeObserver;
    }
    if (observer)
        observer(bytes);
}
} // namespace

struct SecureByteBuffer::Storage final {
    explicit Storage(QByteArray value) : bytes(std::move(value)) {}

    ~Storage() {
        std::fill(bytes.begin(), bytes.end(), '\0');
        if (!bytes.isEmpty())
            observeWipe(QByteArrayView(bytes));
    }

    QByteArray bytes;
};

SecureByteBuffer SecureByteBuffer::take(QByteArray &&bytes) {
    bytes.detach();
    return SecureByteBuffer(std::make_shared<Storage>(std::move(bytes)));
}

const char *SecureByteBuffer::data() const {
    return storage_ == nullptr ? nullptr : storage_->bytes.constData();
}

qsizetype SecureByteBuffer::size() const {
    return storage_ == nullptr ? 0 : storage_->bytes.size();
}

QByteArrayView SecureByteBuffer::view() const {
    return {data(), size()};
}

bool SecureByteBuffer::isEmpty() const {
    return size() == 0;
}

void SecureByteBuffer::setWipeObserverForTests(WipeObserver observer) {
    const std::lock_guard lock(observerMutex);
    wipeObserver = std::move(observer);
}

SecureByteBuffer::SecureByteBuffer(std::shared_ptr<Storage> storage) : storage_(std::move(storage)) {}
