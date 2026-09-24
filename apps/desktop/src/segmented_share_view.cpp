#include "segmented_share_view.h"

#include "clipboard.h"

#include <QAbstractListModel>
#include <QAccessible>
#include <QAccessibleTextCursorEvent>
#include <QAccessibleTextInsertEvent>
#include <QAccessibleTextRemoveEvent>
#include <QAccessibleTextSelectionEvent>
#include <QAccessibleWidget>
#include <QApplication>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTextLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <mutex>

constexpr qsizetype kMaximumShareBytes = 8 * 1'048'576;
constexpr qsizetype kSegmentBytes = 64;
constexpr int kHorizontalPadding = 8;
constexpr qsizetype kAccessibilityEventChunkBytes = 64 * 1024;

QFont segmentFont() { return QFontDatabase::systemFont(QFontDatabase::FixedFont); }
int segmentCharacterWidth() {
    return std::max(1, QFontMetrics(segmentFont()).horizontalAdvance(QLatin1Char('M')));
}
int segmentRowWidth() {
    return kHorizontalPadding + int(kSegmentBytes) * segmentCharacterWidth();
}

class ShareSegmentModel final : public QAbstractListModel {
public:
    explicit ShareSegmentModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override {
        if (parent.isValid() || bytes_.isEmpty())
            return 0;
        const qsizetype rows = (bytes_.size() + kSegmentBytes - 1) / kSegmentBytes;
        return rows > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max()
                                                      : static_cast<int>(rows);
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || role != Qt::DisplayRole)
            return {};
        const qsizetype begin = qsizetype(index.row()) * kSegmentBytes;
        if (begin >= bytes_.size())
            return {};
        const qsizetype length = std::min(kSegmentBytes, bytes_.size() - begin);
        return QString::fromLatin1(bytes_.data() + begin, length);
    }

    void setBytes(SecureByteBuffer bytes) {
        beginResetModel();
        bytes_ = std::move(bytes);
        endResetModel();
    }

    SecureByteBuffer takeBytes() {
        beginResetModel();
        SecureByteBuffer previous = std::move(bytes_);
        bytes_ = {};
        endResetModel();
        return previous;
    }

    [[nodiscard]] QByteArrayView bytes() const { return bytes_.view(); }
    [[nodiscard]] qsizetype byteSize() const { return bytes_.size(); }

private:
    SecureByteBuffer bytes_;
};

class SegmentDelegate final : public QStyledItemDelegate {
public:
    SegmentDelegate(const SegmentedShareView *owner, QObject *parent)
        : QStyledItemDelegate(parent), owner_(owner) {}

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        const QFont font = segmentFont();
        return {segmentRowWidth(), QFontMetrics(font).height() + 6};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        QStyleOptionViewItem background(option);
        initStyleOption(&background, index);
        background.text.clear();
        const QWidget *widget = option.widget;
        const QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &background, painter, widget);

        QString text = index.data(Qt::DisplayRole).toString();
        for (QChar &character : text) {
            if (character == QLatin1Char('\n'))
                character = QChar(0x240a); // ␊
            else if (character == QLatin1Char('\r'))
                character = QChar(0x240d); // ␍
            else if (character == QLatin1Char('\t'))
                character = QChar(0x2409); // ␉
        }
        const QFont font = segmentFont();
        QTextLayout layout(text, font);
        QList<QTextLayout::FormatRange> formats;
        const auto selected = owner_->selection();
        const qsizetype selectionBegin = std::min(selected.first, selected.second);
        const qsizetype selectionEnd = std::max(selected.first, selected.second);
        const qsizetype rowBegin = qsizetype(index.row()) * kSegmentBytes;
        const qsizetype rowEnd = rowBegin + text.size();
        const qsizetype intersectionBegin = std::max(selectionBegin, rowBegin);
        const qsizetype intersectionEnd = std::min(selectionEnd, rowEnd);
        if (intersectionBegin < intersectionEnd) {
            QTextCharFormat selectedFormat;
            selectedFormat.setBackground(option.palette.brush(QPalette::Highlight));
            selectedFormat.setForeground(option.palette.brush(QPalette::HighlightedText));
            formats.push_back({static_cast<int>(intersectionBegin - rowBegin),
                               static_cast<int>(intersectionEnd - intersectionBegin),
                               selectedFormat});
        }
        layout.beginLayout();
        QTextLine line = layout.createLine();
        line.setLineWidth(segmentRowWidth() - kHorizontalPadding);
        layout.endLayout();
        painter->save();
        painter->setPen(option.palette.color(QPalette::Text));
        const qreal y = option.rect.top()
                        + (option.rect.height() - QFontMetricsF(font).height()) / 2.0;
        layout.draw(painter, QPointF(option.rect.left() + 4, y), formats);
        painter->restore();
    }

private:
    const SegmentedShareView *owner_;
};

class AccessibleSegmentedShareView final : public QAccessibleWidget,
                                           public QAccessibleTextInterface {
public:
    using QAccessibleWidget::text;
    explicit AccessibleSegmentedShareView(SegmentedShareView *view)
        : QAccessibleWidget(view, QAccessible::StaticText), view_(view) {}

    void *interface_cast(QAccessible::InterfaceType type) override {
        if (type == QAccessible::TextInterface)
            return static_cast<QAccessibleTextInterface *>(this);
        return QAccessibleWidget::interface_cast(type);
    }

    void selection(int selectionIndex, int *startOffset, int *endOffset) const override {
        if (selectionIndex != 0 || selectionCount() == 0) {
            *startOffset = -1;
            *endOffset = -1;
            return;
        }
        const auto range = view_->selection();
        *startOffset = static_cast<int>(std::min(range.first, range.second));
        *endOffset = static_cast<int>(std::max(range.first, range.second));
    }
    int selectionCount() const override {
        const auto range = view_->selection();
        return range.first == range.second ? 0 : 1;
    }
    void addSelection(int startOffset, int endOffset) override {
        view_->setSelectionRange(startOffset, endOffset);
    }
    void removeSelection(int) override { view_->setCursorPosition(view_->cursorPosition()); }
    void setSelection(int, int startOffset, int endOffset) override {
        view_->setSelectionRange(startOffset, endOffset);
    }
    int cursorPosition() const override { return static_cast<int>(view_->cursorPosition()); }
    void setCursorPosition(int position) override { view_->setCursorPosition(position); }
    QString text(int startOffset, int endOffset) const override {
        return view_->accessibilityTextRange(startOffset, endOffset);
    }
    int characterCount() const override {
        return static_cast<int>(std::min<qsizetype>(view_->byteSize(),
                                                    std::numeric_limits<int>::max()));
    }
    QRect characterRect(int offset) const override { return view_->characterRect(offset); }
    int offsetAtPoint(const QPoint &point) const override {
        return static_cast<int>(view_->offsetAtGlobalPoint(point));
    }
    void scrollToSubstring(int startIndex, int) override { view_->scrollToOffset(startIndex); }
    QString attributes(int offset, int *startOffset, int *endOffset) const override {
        *startOffset = std::clamp(offset, 0, characterCount());
        *endOffset = std::min(*startOffset + 1, characterCount());
        return QStringLiteral("readonly:true");
    }

private:
    SegmentedShareView *view_;
};

QAccessibleInterface *accessibleFactory(const QString &className, QObject *object) {
    if (className == QLatin1String("SegmentedShareView")) {
        if (auto *view = qobject_cast<SegmentedShareView *>(object))
            return new AccessibleSegmentedShareView(view);
    }
    return nullptr;
}

SegmentedShareView::SegmentedShareView(
    std::shared_ptr<DesktopAllocationPolicy> allocationPolicy, QWidget *parent)
    : QWidget(parent), allocationPolicy_(std::move(allocationPolicy)) {
    if (allocationPolicy_ == nullptr)
        allocationPolicy_ = defaultDesktopAllocationPolicy();
    static std::once_flag accessibilityFactory;
    std::call_once(accessibilityFactory,
                   [] { QAccessible::installFactory(accessibleFactory); });
    model_ = new ShareSegmentModel(this);
    view_ = new QListView(this);
    view_->setModel(model_);
    view_->setItemDelegate(new SegmentDelegate(this, view_));
    view_->setUniformItemSizes(true);
    view_->setSelectionMode(QAbstractItemView::NoSelection);
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view_->setFont(segmentFont());
    view_->viewport()->installEventFilter(this);
    view_->installEventFilter(this);
    setFocusProxy(view_);
    setAccessibleName(QStringLiteral("Recovery share"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view_);
}

bool SegmentedShareView::setShare(quint64 generation, quint16 index, SecureByteBuffer bytes,
                                  bool asciiValidated) {
    const bool actuallyAscii = std::all_of(bytes.view().begin(), bytes.view().end(),
                                           [](char byte) {
                                               return static_cast<unsigned char>(byte) <= 0x7f;
                                           });
    if (!asciiValidated || !actuallyAscii || bytes.isEmpty()
        || bytes.size() > kMaximumShareBytes) {
        bytes = {};
        return false;
    }
    SecureByteBuffer previous = model_->takeBytes();
    anchor_ = 0;
    caret_ = 0;
    dragging_ = false;
    emitTextRemoved(previous.view());
    previous = {};

    generation_ = generation;
    shareIndex_ = index;
    model_->setBytes(std::move(bytes));
    setAccessibleName(QStringLiteral("Recovery share %1").arg(index + 1));
    view_->scrollToTop();
    view_->horizontalScrollBar()->setValue(0);
    emitTextInserted(model_->bytes());
    QAccessibleEvent nameEvent(this, QAccessible::NameChanged);
    QAccessible::updateAccessibility(&nameEvent);
    return true;
}

void SegmentedShareView::clearSensitive() {
    SecureByteBuffer previous = model_->takeBytes();
    generation_ = 0;
    shareIndex_ = 0;
    anchor_ = 0;
    caret_ = 0;
    dragging_ = false;
    view_->viewport()->update();
    emitTextRemoved(previous.view());
    previous = {};
}

bool SegmentedShareView::hasShare() const noexcept { return model_->byteSize() != 0; }
qsizetype SegmentedShareView::byteSize() const noexcept { return model_->byteSize(); }
QPair<qsizetype, qsizetype> SegmentedShareView::selection() const noexcept {
    return {anchor_, caret_};
}

void SegmentedShareView::selectAll() {
    const qsizetype previousAnchor = anchor_;
    const qsizetype previousCaret = caret_;
    anchor_ = 0;
    caret_ = byteSize();
    updateSelection(previousAnchor, previousCaret);
}

void SegmentedShareView::setSelectionRange(qsizetype begin, qsizetype end) {
    const qsizetype previousAnchor = anchor_;
    const qsizetype previousCaret = caret_;
    anchor_ = std::clamp<qsizetype>(begin, 0, byteSize());
    caret_ = std::clamp<qsizetype>(end, 0, byteSize());
    updateSelection(previousAnchor, previousCaret);
}

void SegmentedShareView::copySelection() {
    const qsizetype begin = std::min(anchor_, caret_);
    const qsizetype end = std::max(anchor_, caret_);
    if (begin == end) {
        emit copyRequested(generation_, shareIndex_);
        return;
    }
    if (!allocationPolicy_->allow(DesktopAllocationBoundary::ClipboardHandoff)) {
        emit allocationFailed();
        return;
    }
    const QByteArrayView bytes = model_->bytes();
    if (!writeClipboardUtf8(QByteArrayView(bytes.data() + begin, end - begin)))
        emit copyFailed();
}

QString SegmentedShareView::accessibilityTextRange(qsizetype begin, qsizetype end) const {
    begin = std::clamp<qsizetype>(begin, 0, byteSize());
    end = std::clamp<qsizetype>(end, begin, byteSize());
    if (begin == end)
        return {};
    if (!allocationPolicy_->allow(DesktopAllocationBoundary::AccessibilityPresentation)) {
        emit const_cast<SegmentedShareView *>(this)->allocationFailed();
        return {};
    }
    return textRange(begin, end);
}

QString SegmentedShareView::textRange(qsizetype begin, qsizetype end) const {
    begin = std::clamp<qsizetype>(begin, 0, byteSize());
    end = std::clamp<qsizetype>(end, begin, byteSize());
    if (begin == end)
        return {};
    return QString::fromLatin1(model_->bytes().data() + begin, end - begin);
}

qsizetype SegmentedShareView::cursorPosition() const noexcept { return caret_; }
void SegmentedShareView::setCursorPosition(qsizetype position) { setCaret(position, false); }

QRect SegmentedShareView::characterRect(qsizetype position) const {
    position = std::clamp<qsizetype>(position, 0, std::max<qsizetype>(byteSize() - 1, 0));
    const int row = static_cast<int>(position / kSegmentBytes);
    const int column = static_cast<int>(position % kSegmentBytes);
    const QRect rowRect = view_->visualRect(model_->index(row, 0));
    const int width = segmentCharacterWidth();
    QRect result(rowRect.left() + 4 + column * width, rowRect.top(), width, rowRect.height());
    result.moveTopLeft(view_->viewport()->mapToGlobal(result.topLeft()));
    return result;
}

qsizetype SegmentedShareView::offsetAtGlobalPoint(const QPoint &point) const {
    return offsetAtViewportPoint(view_->viewport()->mapFromGlobal(point));
}

void SegmentedShareView::scrollToOffset(qsizetype position) {
    if (!hasShare())
        return;
    position = std::clamp<qsizetype>(position, 0, byteSize() - 1);
    view_->scrollTo(model_->index(static_cast<int>(position / kSegmentBytes), 0));
    const int column = static_cast<int>(position % kSegmentBytes);
    const int characterLeft = 4 + column * segmentCharacterWidth();
    QScrollBar *horizontal = view_->horizontalScrollBar();
    if (characterLeft < horizontal->value())
        horizontal->setValue(characterLeft);
    else if (characterLeft + segmentCharacterWidth()
             > horizontal->value() + view_->viewport()->width())
        horizontal->setValue(characterLeft + segmentCharacterWidth()
                             - view_->viewport()->width());
}

int SegmentedShareView::segmentCount() const noexcept { return model_->rowCount(); }
qsizetype SegmentedShareView::segmentBytes() const noexcept { return kSegmentBytes; }

bool SegmentedShareView::eventFilter(QObject *watched, QEvent *event) {
    if (watched != view_ && watched != view_->viewport())
        return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->matches(QKeySequence::SelectAll)) {
            selectAll();
            return true;
        }
        if (key->matches(QKeySequence::Copy)) {
            copySelection();
            return true;
        }
        const bool extend = key->modifiers().testFlag(Qt::ShiftModifier);
        switch (key->key()) {
        case Qt::Key_Left:
            setCaret(caret_ - 1, extend);
            return true;
        case Qt::Key_Right:
            setCaret(caret_ + 1, extend);
            return true;
        case Qt::Key_Home:
            setCaret((caret_ / kSegmentBytes) * kSegmentBytes, extend);
            return true;
        case Qt::Key_End:
            setCaret(std::min(byteSize(), ((caret_ / kSegmentBytes) + 1) * kSegmentBytes), extend);
            return true;
        default:
            break;
        }
    } else if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            dragging_ = true;
            setCaret(offsetAtViewportPoint(mouse->position().toPoint()),
                     mouse->modifiers().testFlag(Qt::ShiftModifier));
            return true;
        }
    } else if (event->type() == QEvent::MouseMove && dragging_) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        setCaret(offsetAtViewportPoint(mouse->position().toPoint()), true);
        return true;
    } else if (event->type() == QEvent::MouseButtonRelease) {
        dragging_ = false;
    } else if (event->type() == QEvent::ContextMenu) {
        QMenu menu(this);
        QAction *copy = menu.addAction(QStringLiteral("Copy"));
        QAction *selectAllAction = menu.addAction(QStringLiteral("Select All"));
        QAction *chosen = menu.exec(static_cast<QContextMenuEvent *>(event)->globalPos());
        if (chosen == copy)
            copySelection();
        else if (chosen == selectAllAction)
            selectAll();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void SegmentedShareView::setCaret(qsizetype position, bool extend) {
    position = std::clamp<qsizetype>(position, 0, byteSize());
    const qsizetype previousAnchor = anchor_;
    const qsizetype previousCaret = caret_;
    if (!extend)
        anchor_ = position;
    caret_ = position;
    scrollToOffset(position == byteSize() && position != 0 ? position - 1 : position);
    updateSelection(previousAnchor, previousCaret);
}

void SegmentedShareView::updateSelection(qsizetype previousAnchor, qsizetype previousCaret) {
    if (anchor_ == previousAnchor && caret_ == previousCaret)
        return;
    view_->viewport()->update();
    emit selectionChanged(std::min(anchor_, caret_), std::max(anchor_, caret_));
    const bool previousHadSelection = previousAnchor != previousCaret;
    const bool hasSelection = anchor_ != caret_;
    const bool selectionChanged = previousHadSelection != hasSelection
                                  || (hasSelection
                                      && (std::min(previousAnchor, previousCaret)
                                              != std::min(anchor_, caret_)
                                          || std::max(previousAnchor, previousCaret)
                                                 != std::max(anchor_, caret_)));
    if (selectionChanged) {
        QAccessibleTextSelectionEvent event(this, static_cast<int>(std::min(anchor_, caret_)),
                                            static_cast<int>(std::max(anchor_, caret_)));
        QAccessible::updateAccessibility(&event);
    } else {
        QAccessibleTextCursorEvent event(this, static_cast<int>(caret_));
        QAccessible::updateAccessibility(&event);
    }
}

void SegmentedShareView::emitTextInserted(QByteArrayView bytes) {
    for (qsizetype offset = 0; offset < bytes.size(); offset += kAccessibilityEventChunkBytes) {
        const qsizetype length = std::min(kAccessibilityEventChunkBytes, bytes.size() - offset);
        QAccessibleTextInsertEvent event(
            this, static_cast<int>(offset), QString::fromLatin1(bytes.data() + offset, length));
        QAccessible::updateAccessibility(&event);
    }
}

void SegmentedShareView::emitTextRemoved(QByteArrayView bytes) {
    qsizetype end = bytes.size();
    while (end > 0) {
        const qsizetype begin = std::max<qsizetype>(0, end - kAccessibilityEventChunkBytes);
        QAccessibleTextRemoveEvent event(
            this, static_cast<int>(begin), QString::fromLatin1(bytes.data() + begin, end - begin));
        QAccessible::updateAccessibility(&event);
        end = begin;
    }
}

qsizetype SegmentedShareView::offsetAtViewportPoint(const QPoint &point) const {
    const QModelIndex index = view_->indexAt(point);
    if (!index.isValid())
        return point.y() < 0 ? 0 : byteSize();
    const QRect rowRect = view_->visualRect(index);
    const int width = segmentCharacterWidth();
    const qsizetype column = std::clamp((point.x() - rowRect.left() - 4 + width / 2) / width,
                                       0, int(kSegmentBytes));
    return std::min(byteSize(), qsizetype(index.row()) * kSegmentBytes + column);
}
