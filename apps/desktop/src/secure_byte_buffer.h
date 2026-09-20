#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QMetaType>

#include <functional>
#include <memory>

class SecureByteBuffer final {
public:
    using WipeObserver = std::function<void(QByteArrayView)>;

    SecureByteBuffer() = default;

    static SecureByteBuffer take(QByteArray &&bytes);

    [[nodiscard]] const char *data() const;
    [[nodiscard]] qsizetype size() const;
    [[nodiscard]] QByteArrayView view() const;
    [[nodiscard]] bool isEmpty() const;

    // Test seam: observes storage only after it has been overwritten and before release.
    static void setWipeObserverForTests(WipeObserver observer);

private:
    struct Storage;
    explicit SecureByteBuffer(std::shared_ptr<Storage> storage);

    std::shared_ptr<Storage> storage_;
};

Q_DECLARE_METATYPE(SecureByteBuffer)
