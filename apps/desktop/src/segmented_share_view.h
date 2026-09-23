#pragma once

#include "secure_byte_buffer.h"

#include <QPair>
#include <QWidget>

class QListView;
class ShareSegmentModel;

class SegmentedShareView final : public QWidget {
    Q_OBJECT

public:
    explicit SegmentedShareView(QWidget *parent = nullptr);

    bool setShare(quint64 generation, quint16 index, SecureByteBuffer bytes, bool asciiValidated);
    void clearSensitive();
    [[nodiscard]] bool hasShare() const noexcept;
    [[nodiscard]] qsizetype byteSize() const noexcept;
    [[nodiscard]] QPair<qsizetype, qsizetype> selection() const noexcept;
    void selectAll();
    void setSelectionRange(qsizetype begin, qsizetype end);
    void copySelection();

    // Public read-only projections used by accessibility and focused tests.
    [[nodiscard]] QString textRange(qsizetype begin, qsizetype end) const;
    [[nodiscard]] qsizetype cursorPosition() const noexcept;
    void setCursorPosition(qsizetype position);
    [[nodiscard]] QRect characterRect(qsizetype position) const;
    [[nodiscard]] qsizetype offsetAtGlobalPoint(const QPoint &point) const;
    void scrollToOffset(qsizetype position);
    [[nodiscard]] int segmentCount() const noexcept;
    [[nodiscard]] qsizetype segmentBytes() const noexcept;

signals:
    void copyRequested(quint64 generation, quint16 index);
    void copyFailed();
    void selectionChanged(qsizetype utf8Begin, qsizetype utf8End);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setCaret(qsizetype position, bool extend);
    void updateSelection(qsizetype previousAnchor, qsizetype previousCaret);
    void notifyTextRefresh();
    [[nodiscard]] qsizetype offsetAtViewportPoint(const QPoint &point) const;

    ShareSegmentModel *model_ = nullptr;
    QListView *view_ = nullptr;
    quint64 generation_ = 0;
    quint16 shareIndex_ = 0;
    qsizetype anchor_ = 0;
    qsizetype caret_ = 0;
    bool dragging_ = false;
};
