#include "file_io.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>

struct ReadTrace final {
    char *allocation = nullptr;
    int calls = 0;
    bool contiguous = true;
};

class ScriptedDevice final : public FileDevice {
public:
    QByteArray source;
    qsizetype position = 0;
    bool openRead = true;
    bool openWrite = true;
    bool flushResult = true;
    bool closeResult = true;
    qint64 forcedWrite = -2;
    qint64 failReadAfter = -1;
    qint64 maximumReadSize = -1;
    std::shared_ptr<ReadTrace> readTrace;
    bool growAfterFirstRead = false;
    bool grew = false;
    QByteArray written;

    bool openReadOnly() override { return openRead; }
    bool openWriteTruncate() override { return openWrite; }
    qint64 read(char *data, qint64 maximum) override {
        if (readTrace) {
            if (readTrace->allocation == nullptr)
                readTrace->allocation = data - position;
            readTrace->contiguous = readTrace->contiguous
                                    && data == readTrace->allocation + position;
            ++readTrace->calls;
        }
        if (failReadAfter >= 0 && position >= failReadAfter)
            return -1;
        const qsizetype permitted = maximumReadSize < 0
                                        ? static_cast<qsizetype>(maximum)
                                        : std::min(static_cast<qsizetype>(maximum),
                                                   static_cast<qsizetype>(maximumReadSize));
        const qsizetype count = std::min(permitted, source.size() - position);
        if (count <= 0)
            return 0;
        std::copy_n(source.constData() + position, count, data);
        position += count;
        if (growAfterFirstRead && !grew) {
            source.append('g');
            grew = true;
        }
        return count;
    }
    qint64 write(const char *data, qint64 size) override {
        if (forcedWrite != -2) {
            if (forcedWrite > 0)
                written.append(data, std::min(forcedWrite, size));
            return forcedWrite;
        }
        written.append(data, size);
        return size;
    }
    bool flush() override { return flushResult; }
    bool close() override { return closeResult; }
};

class WipeReset final {
public:
    ~WipeReset() { SecureByteBuffer::setWipeObserverForTests({}); }
};

class FileIoTest final : public QObject {
    Q_OBJECT

private slots:
    void bounded_actual_reads_cover_empty_at_and_above_limit() {
        for (qsizetype size : {qsizetype(0), qsizetype(4), qsizetype(5)}) {
            auto *script = new ScriptedDevice;
            script->source = QByteArray(size, 'x');
            FileIo io([script](const QString &) { return std::unique_ptr<FileDevice>(script); });
            const auto result = io.readBounded(QStringLiteral("ignored"), 4);
            QCOMPARE(result.status, size > 4 ? FileIo::Status::TooLarge : FileIo::Status::Ok);
            if (size <= 4)
                QCOMPARE(result.bytes.size(), size);
        }
    }

    void dynamic_growth_and_end_of_stream_are_bounded() {
        auto *growing = new ScriptedDevice;
        growing->source = QByteArray("abcd");
        growing->growAfterFirstRead = true;
        FileIo growingIo(
            [growing](const QString &) { return std::unique_ptr<FileDevice>(growing); });
        const auto rejected = growingIo.readBounded(QStringLiteral("growing"), 4);
        QCOMPARE(rejected.status, FileIo::Status::TooLarge);
        QVERIFY(rejected.bytes.isEmpty());

        auto trace = std::make_shared<ReadTrace>();
        auto *shrinking = new ScriptedDevice;
        shrinking->source = QByteArray("abc");
        shrinking->maximumReadSize = 1;
        shrinking->readTrace = trace;
        FileIo shrinkingIo(
            [shrinking](const QString &) { return std::unique_ptr<FileDevice>(shrinking); });
        const auto accepted = shrinkingIo.readBounded(QStringLiteral("shrinking"), 4);
        QCOMPARE(accepted.status, FileIo::Status::Ok);
        QCOMPARE(QByteArray(accepted.bytes.data(), accepted.bytes.size()), QByteArray("abc"));
        QVERIFY(trace->calls > 1);
        QVERIFY(trace->contiguous);
        QCOMPARE(accepted.bytes.data(), trace->allocation);
    }

    void successful_read_owner_wipes_on_final_release() {
        std::atomic<int> wipes{0};
        std::atomic<int> nonzero{0};
        WipeReset reset;
        SecureByteBuffer::setWipeObserverForTests([&](QByteArrayView bytes) {
            if (bytes.size() == 4)
                wipes.fetch_add(1, std::memory_order_release);
            if (std::any_of(bytes.begin(), bytes.end(), [](char byte) { return byte != '\0'; }))
                nonzero.fetch_add(1, std::memory_order_release);
        });
        {
            auto *reader = new ScriptedDevice;
            reader->source = QByteArray("mark");
            FileIo io([reader](const QString &) { return std::unique_ptr<FileDevice>(reader); });
            const auto result = io.readBounded(QStringLiteral("selected"), 4);
            QCOMPARE(result.status, FileIo::Status::Ok);
            QCOMPARE(QByteArray(result.bytes.data(), result.bytes.size()), QByteArray("mark"));
        }
        QCOMPARE(wipes.load(std::memory_order_acquire), 1);
        QCOMPARE(nonzero.load(std::memory_order_acquire), 0);
    }

    void read_and_write_failures_are_typed() {
        auto *reader = new ScriptedDevice;
        reader->source = QByteArray("abcd");
        reader->maximumReadSize = 2;
        reader->failReadAfter = 2;
        FileIo readIo([reader](const QString &) { return std::unique_ptr<FileDevice>(reader); });
        const auto readFailure = readIo.readBounded(QStringLiteral("ignored"), 4);
        QCOMPARE(readFailure.status, FileIo::Status::ReadFailed);
        QVERIFY(readFailure.bytes.isEmpty());

        for (qint64 result : {qint64(0), qint64(2), qint64(-1)}) {
            auto *writer = new ScriptedDevice;
            writer->forcedWrite = result;
            FileIo io([writer](const QString &) { return std::unique_ptr<FileDevice>(writer); });
            QCOMPARE(io.writeDirect(QStringLiteral("selected"),
                                    SecureByteBuffer::take(QByteArray("abcd"))),
                     FileIo::Status::WriteFailed);
        }
        auto *flush = new ScriptedDevice;
        flush->flushResult = false;
        FileIo flushIo([flush](const QString &) { return std::unique_ptr<FileDevice>(flush); });
        QCOMPARE(flushIo.writeDirect(QStringLiteral("selected"), SecureByteBuffer{}),
                 FileIo::Status::FlushFailed);
    }

    void open_and_close_failures_use_one_device_for_the_exact_path() {
        for (const bool read : {true, false}) {
            int factoryCalls = 0;
            QString observedPath;
            auto *device = new ScriptedDevice;
            device->openRead = !read;
            device->openWrite = read;
            FileIo io([&](const QString &path) {
                ++factoryCalls;
                observedPath = path;
                return std::unique_ptr<FileDevice>(device);
            });
            const auto status = read
                                    ? io.readBounded(QStringLiteral("chosen/input"), 4).status
                                    : io.writeDirect(QStringLiteral("chosen/output"), SecureByteBuffer{});
            QCOMPARE(status, FileIo::Status::OpenFailed);
            QCOMPARE(factoryCalls, 1);
            QCOMPARE(observedPath,
                     read ? QStringLiteral("chosen/input") : QStringLiteral("chosen/output"));
        }

        auto *reader = new ScriptedDevice;
        reader->source = QByteArray("abc");
        reader->closeResult = false;
        FileIo readIo([reader](const QString &) { return std::unique_ptr<FileDevice>(reader); });
        const auto closeFailure = readIo.readBounded(QStringLiteral("chosen"), 4);
        QCOMPARE(closeFailure.status, FileIo::Status::ReadFailed);
        QVERIFY(closeFailure.bytes.isEmpty());

        auto *writer = new ScriptedDevice;
        writer->closeResult = false;
        FileIo writeIo([writer](const QString &) { return std::unique_ptr<FileDevice>(writer); });
        QCOMPARE(writeIo.writeDirect(QStringLiteral("chosen"), SecureByteBuffer{}),
                 FileIo::Status::CloseFailed);
    }

    void production_adapter_reads_and_writes_exact_selected_path() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("exact.bin"));
        const QByteArray expected("\0\xff\r\n", 4);
        FileIo io;
        QCOMPARE(io.writeDirect(path, SecureByteBuffer::take(QByteArray(expected))), FileIo::Status::Ok);
        const auto read = io.readBounded(path, expected.size());
        QCOMPARE(read.status, FileIo::Status::Ok);
        QCOMPARE(QByteArray(read.bytes.data(), read.bytes.size()), expected);
        QCOMPARE(io.writeDirect(path, SecureByteBuffer{}), FileIo::Status::Ok);
        QCOMPARE(QFile(path).size(), 0);
    }
};

QTEST_APPLESS_MAIN(FileIoTest)
#include "file_io.moc"
