// THROWAWAY: synthetic-only Qt/CUPS printing probe. No Safeparts implementation.
#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QFont>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPrinter>
#include <cups/cups.h>
#include <cups/http.h>
#include <cstdio>
#include <stdexcept>
#include <string>

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

static void render(QPagedPaintDevice &device) {
    device.setPageSize(QPageSize(QPageSize::A4));
    device.setPageMargins(QMarginsF(36, 36, 36, 36), QPageLayout::Point);
    QPainter painter;
    require(painter.begin(&device), "painter begin failed");
    painter.setFont(QFont("DejaVu Sans", 9));
    for (int page = 1; page <= 3; ++page) {
        if (page > 1) require(device.newPage(), "new page failed");
        painter.drawText(0, 14, QString("SYNTHETIC_PAGE_%1_OF_3").arg(page));
        for (int row = 1; row <= 40; ++row)
            painter.drawText(0, 16 * (row + 1),
                QString("SYNTHETIC_P%1_ROW_%2 alpha beta gamma delta").arg(page).arg(row));
    }
    require(painter.end(), "painter end failed");
}

static http_t *connectLocal() {
    // The runner provides a private Unix-domain CUPS socket, not a network printer.
    http_t *connection = httpConnect2(cupsServer(), ippPort(), nullptr, AF_UNSPEC,
        HTTP_ENCRYPTION_IF_REQUESTED, 1, 5000, nullptr);
    require(connection != nullptr, "CUPS connection failed");
    return connection;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        require(argc == 2, "expected memory, cancel, failure, or control");
        const std::string mode(argv[1]);
        require(mode == "memory" || mode == "cancel" || mode == "failure" || mode == "control",
                "unknown mode");
        if (mode == "control") {
            QPrinter printer(QPrinter::PrinterResolution);
            printer.setResolution(72);
            printer.setPrinterName("probe");
            printer.setDocName("synthetic-control");
            render(printer);
            std::puts("mode=control submitted=1");
            return 0;
        }

        QByteArray pdf;
        QBuffer buffer(&pdf);
        require(buffer.open(QIODevice::WriteOnly), "buffer open failed");
        {
            QPdfWriter writer(&buffer);
            writer.setResolution(72);
            writer.setTitle("SYNTHETIC PRINT PROTOTYPE");
            writer.setCreator("throwaway Qt memory probe");
            render(writer);
        }
        buffer.close();
        const QByteArray digest = QCryptographicHash::hash(pdf, QCryptographicHash::Sha256).toHex();
        std::printf("mode=%s pdf_bytes=%lld sha256=%s\n", mode.c_str(),
                    static_cast<long long>(pdf.size()), digest.constData());
        std::fflush(stdout);

        http_t *connection = connectLocal();
        const char *destination = mode == "failure" ? "does-not-exist" : "probe";
        cups_option_t *options = nullptr;
        int count = cupsAddOption("job-hold-until", "indefinite", 0, &options);
        const int job = cupsCreateJob(connection, destination, "synthetic-memory", count, options);
        cupsFreeOptions(count, options);
        if (mode == "failure") {
            require(job == 0, "missing printer unexpectedly accepted");
            httpClose(connection);
            std::puts("expected_failure=missing_printer");
            return 0;
        }
        require(job > 0, "create job failed");
        std::printf("job_id=%d\n", job);
        require(cupsStartDocument(connection, destination, job, "synthetic.pdf",
                                  CUPS_FORMAT_PDF, 1) == HTTP_STATUS_CONTINUE,
                "start document failed");
        const size_t length = mode == "cancel" ? static_cast<size_t>(pdf.size() / 2)
                                               : static_cast<size_t>(pdf.size());
        require(cupsWriteRequestData(connection, pdf.constData(), length) == HTTP_STATUS_CONTINUE,
                "stream write failed");
        if (mode == "cancel") {
            // Abort a partial upload while the job is held; cancel using a separate connection.
            httpClose(connection);
            connection = connectLocal();
            require(cupsCancelJob2(connection, destination, job, 0) <= IPP_STATUS_OK_EVENTS_COMPLETE,
                    "cancel failed");
            cups_job_t *jobs = nullptr;
            const int jobCount = cupsGetJobs2(connection, &jobs, nullptr, 0, CUPS_WHICHJOBS_ALL);
            bool cancelled = false;
            for (int i = 0; i < jobCount; ++i)
                if (jobs[i].id == job) cancelled = jobs[i].state == IPP_JSTATE_CANCELED;
            cupsFreeJobs(jobCount, jobs);
            require(cancelled, "server did not record cancelled state");
            httpClose(connection);
            std::puts("cancelled_during_upload=1 server_state=cancelled");
            return 0;
        }
        require(cupsFinishDocument(connection, destination) <= IPP_STATUS_OK_EVENTS_COMPLETE,
                "finish document failed");
        // Release only after a complete upload. A held, partial upload must not print.
        ipp_t *request = ippNewRequest(IPP_OP_RELEASE_JOB);
        char uri[256];
        std::snprintf(uri, sizeof(uri), "ipp://localhost/jobs/%d", job);
        ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri", nullptr, uri);
        ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", nullptr, cupsUser());
        ipp_t *response = cupsDoRequest(connection, request, "/jobs/");
        require(response != nullptr, "release response missing");
        const auto status = ippGetStatusCode(response);
        ippDelete(response);
        require(status <= IPP_STATUS_OK_EVENTS_COMPLETE, "release failed");
        httpClose(connection);
        std::puts("submitted=1");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "probe_failure=%s\n", error.what());
        return 1;
    }
}
