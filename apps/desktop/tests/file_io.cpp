#include "file_io.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <memory>
#include <utility>

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
    QByteArray written;

    bool openReadOnly() override { return openRead; }
    bool openWriteTruncate() override { return openWrite; }
    qint64 read(char *data, qint64 maximum) override {
        if (failReadAfter >= 0 && position >= failReadAfter)
            return -1;
        const qsizetype count = std::min(static_cast<qsizetype>(maximum), source.size() - position);
        if (count <= 0)
            return 0;
        std::copy_n(source.constData() + position, count, data);
        position += count;
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

    void read_and_write_failures_are_typed() {
        auto *reader = new ScriptedDevice;
        reader->source = QByteArray("abcd");
        reader->failReadAfter = 0;
        FileIo readIo([reader](const QString &) { return std::unique_ptr<FileDevice>(reader); });
        QCOMPARE(readIo.readBounded(QStringLiteral("ignored"), 4).status, FileIo::Status::ReadFailed);

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
        QCOMPARE(readIo.readBounded(QStringLiteral("chosen"), 4).status, FileIo::Status::ReadFailed);

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
