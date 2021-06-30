/*
    Copyright © 2014-2019 by The qTox Project Contributors

    This file is part of qTox, a Qt-based graphical interface for Tox.

    qTox is libre software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    qTox is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with qTox.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "chatlog.h"
#include "chatlinecontent.h"
#include "chatlinecontentproxy.h"
#include "chatmessage.h"
#include "content/filetransferwidget.h"
#include "src/widget/gui.h"
#include "src/widget/translator.h"
#include "src/widget/style.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QMouseEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QTimer>

#include <algorithm>
#include <cassert>

/**
 * @var ChatLog::repNameAfter
 * @brief repetition interval sender name (sec)
 */

template <class T>
T clamp(T x, T min, T max)
{
    if (x > max)
        return max;
    if (x < min)
        return min;
    return x;
}

ChatLog::ChatLog(QWidget* parent)
    : QGraphicsView(parent)
{
    // Create the scene
    busyScene = new QGraphicsScene(this);
    scene = new QGraphicsScene(this);
    scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    setScene(scene);

    // Cfg.
    setInteractive(true);
    setAcceptDrops(false);
    setAlignment(Qt::AlignTop | Qt::AlignLeft);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragMode(QGraphicsView::NoDrag);
    setViewportUpdateMode(MinimalViewportUpdate);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setBackgroundBrush(QBrush(Style::getColor(Style::GroundBase), Qt::SolidPattern));

    // The selection rect for multi-line selection
    selGraphItem = scene->addRect(0, 0, 0, 0, selectionRectColor.darker(120), selectionRectColor);
    selGraphItem->setZValue(-1.0); // behind all other items

    // copy action (ie. Ctrl+C)
    copyAction = new QAction(this);
    copyAction->setIcon(QIcon::fromTheme("edit-copy"));
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setEnabled(false);
    connect(copyAction, &QAction::triggered, this, [this]() { copySelectedText(); });
    addAction(copyAction);

    // Ctrl+Insert shortcut
    QShortcut* copyCtrlInsShortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Insert), this);
    connect(copyCtrlInsShortcut, &QShortcut::activated, this, [this]() { copySelectedText(); });

    // select all action (ie. Ctrl+A)
    selectAllAction = new QAction(this);
    selectAllAction->setIcon(QIcon::fromTheme("edit-select-all"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, this, [this]() { selectAll(); });
    addAction(selectAllAction);

    // This timer is used to scroll the view while the user is
    // moving the mouse past the top/bottom edge of the widget while selecting.
    selectionTimer = new QTimer(this);
    selectionTimer->setInterval(1000 / 30);
    selectionTimer->setSingleShot(false);
    selectionTimer->start();
    connect(selectionTimer, &QTimer::timeout, this, &ChatLog::onSelectionTimerTimeout);

    // Background worker
    // Updates the layout of all chat-lines after a resize
    workerTimer = new QTimer(this);
    workerTimer->setSingleShot(false);
    workerTimer->setInterval(5);
    connect(workerTimer, &QTimer::timeout, this, &ChatLog::onWorkerTimeout);

    // This timer is used to detect multiple clicks
    multiClickTimer = new QTimer(this);
    multiClickTimer->setSingleShot(true);
    multiClickTimer->setInterval(QApplication::doubleClickInterval());
    connect(multiClickTimer, &QTimer::timeout, this, &ChatLog::onMultiClickTimeout);

    // selection
    connect(this, &ChatLog::selectionChanged, this, [this]() {
        copyAction->setEnabled(hasTextToBeCopied());
        copySelectedText(true);
    });

    connect(&GUI::getInstance(), &GUI::themeReload, this, &ChatLog::reloadTheme);

    reloadTheme();
    retranslateUi();
    Translator::registerHandler(std::bind(&ChatLog::retranslateUi, this), this);
}

ChatLog::~ChatLog()
{
    Translator::unregister(this);

    // Remove chatlines from scene
    for (ChatLine::Ptr l : lines)
        l->removeFromScene();

    if (busyNotification)
        busyNotification->removeFromScene();

    if (typingNotification)
        typingNotification->removeFromScene();
}

void ChatLog::clearSelection()
{
    if (selectionMode == SelectionMode::None)
        return;

    for (int i = selFirstRow; i <= selLastRow; ++i)
        lines[i]->selectionCleared();

    selFirstRow = -1;
    selLastRow = -1;
    selClickedCol = -1;
    selClickedRow = -1;

    selectionMode = SelectionMode::None;
    emit selectionChanged();

    updateMultiSelectionRect();
}

QRect ChatLog::getVisibleRect() const
{
    return mapToScene(viewport()->rect()).boundingRect().toRect();
}

void ChatLog::updateSceneRect()
{
    setSceneRect(calculateSceneRect());
}

void ChatLog::layout(int start, int end, qreal width)
{
    if (lines.empty()) {
        return;
    }

    qreal h = 0.0;

    // Line at start-1 is considered to have the correct position. All following lines are
    // positioned in respect to this line.
    if (start - 1 >= 0)
        h = lines[start - 1]->sceneBoundingRect().bottom() + lineSpacing;

    start = clamp<int>(start, 0, lines.size());
    end = clamp<int>(end + 1, 0, lines.size());

    for (int i = start; i < end; ++i) {
        ChatLine* l = lines[i].get();

        l->layout(width, QPointF(0.0, h));
        h += l->sceneBoundingRect().height() + lineSpacing;
    }
}

void ChatLog::mousePressEvent(QMouseEvent* ev)
{
    QGraphicsView::mousePressEvent(ev);

    if (ev->button() == Qt::LeftButton) {
        clickPos = ev->pos();
        clearSelection();
    }

    if (lastClickButton == ev->button()) {
        // Counts only single clicks and first click of doule click
        clickCount++;
    }
    else {
        clickCount = 1; // restarting counter
        lastClickButton = ev->button();
    }
    lastClickPos = ev->pos();

    // Triggers on odd click counts
    handleMultiClickEvent();
}

void ChatLog::mouseReleaseEvent(QMouseEvent* ev)
{
    QGraphicsView::mouseReleaseEvent(ev);

    selectionScrollDir = AutoScrollDirection::NoDirection;

    multiClickTimer->start();
}

void ChatLog::mouseMoveEvent(QMouseEvent* ev)
{
    QGraphicsView::mouseMoveEvent(ev);

    QPointF scenePos = mapToScene(ev->pos());

    if (ev->buttons() & Qt::LeftButton) {
        // autoscroll
        if (ev->pos().y() < 0)
            selectionScrollDir = AutoScrollDirection::Up;
        else if (ev->pos().y() > height())
            selectionScrollDir = AutoScrollDirection::Down;
        else
            selectionScrollDir = AutoScrollDirection::NoDirection;

        // select
        if (selectionMode == SelectionMode::None
            && (clickPos - ev->pos()).manhattanLength() > QApplication::startDragDistance()) {
            QPointF sceneClickPos = mapToScene(clickPos.toPoint());
            ChatLine::Ptr line = findLineByPosY(scenePos.y());

            ChatLineContent* content = getContentFromPos(sceneClickPos);
            if (content) {
                selClickedRow = content->getRow();
                selClickedCol = content->getColumn();
                selFirstRow = content->getRow();
                selLastRow = content->getRow();

                content->selectionStarted(sceneClickPos);

                selectionMode = SelectionMode::Precise;

                // ungrab mouse grabber
                if (scene->mouseGrabberItem())
                    scene->mouseGrabberItem()->ungrabMouse();
            } else if (line.get()) {
                selClickedRow = line->getRow();
                selFirstRow = selClickedRow;
                selLastRow = selClickedRow;

                selectionMode = SelectionMode::Multi;
            }
        }

        if (selectionMode != SelectionMode::None) {
            ChatLineContent* content = getContentFromPos(scenePos);
            ChatLine::Ptr line = findLineByPosY(scenePos.y());

            int row;

            if (content) {
                row = content->getRow();
                int col = content->getColumn();

                if (row == selClickedRow && col == selClickedCol) {
                    selectionMode = SelectionMode::Precise;

                    content->selectionMouseMove(scenePos);
                    selGraphItem->hide();
                } else if (col != selClickedCol) {
                    selectionMode = SelectionMode::Multi;

                    lines[selClickedRow]->selectionCleared();
                }
            } else if (line.get()) {
                row = line->getRow();

                if (row != selClickedRow) {
                    selectionMode = SelectionMode::Multi;
                    lines[selClickedRow]->selectionCleared();
                }
            } else {
                return;
            }

            if (row >= selClickedRow)
                selLastRow = row;

            if (row <= selClickedRow)
                selFirstRow = row;

            updateMultiSelectionRect();
        }

        emit selectionChanged();
    }
}

// Much faster than QGraphicsScene::itemAt()!
ChatLineContent* ChatLog::getContentFromPos(QPointF scenePos) const
{
    if (lines.empty()) {
        return nullptr;
    }

    auto itr =
        std::lower_bound(lines.cbegin(), lines.cend(), scenePos.y(), ChatLine::lessThanBSRectBottom);

    // find content
    if (itr != lines.cend() && (*itr)->sceneBoundingRect().contains(scenePos))
        return (*itr)->getContent(scenePos);

    return nullptr;
}

bool ChatLog::isOverSelection(QPointF scenePos) const
{
    if (selectionMode == SelectionMode::Precise) {
        ChatLineContent* content = getContentFromPos(scenePos);

        if (content)
            return content->isOverSelection(scenePos);
    } else if (selectionMode == SelectionMode::Multi) {
        if (selGraphItem->rect().contains(scenePos))
            return true;
    }

    return false;
}

qreal ChatLog::useableWidth() const
{
    return width() - verticalScrollBar()->sizeHint().width() - margins.right() - margins.left();
}

void ChatLog::reposition(int start, int end, qreal deltaY)
{
    if (lines.isEmpty())
        return;

    start = clamp<int>(start, 0, lines.size() - 1);
    end = clamp<int>(end + 1, 0, lines.size());

    for (int i = start; i < end; ++i) {
        ChatLine* l = lines[i].get();
        l->moveBy(deltaY);
    }
}

void ChatLog::insertChatlineAtBottom(ChatLine::Ptr l)
{
    numRemove = 0;
    if (!l.get())
        return;

    bool stickToBtm = stickToBottom();

    // insert
    l->setRow(lines.size());
    l->addToScene(scene);
    lines.append(l);

    // partial refresh
    layout(lines.last()->getRow(), lines.size(), useableWidth());
    updateSceneRect();

    if (stickToBtm)
        scrollToBottom();

    checkVisibility();
    updateTypingNotification();
}

void ChatLog::insertChatlineAtBottom(const QList<ChatLine::Ptr>& newLines)
{
    numRemove = 0;
    if (newLines.isEmpty())
        return;

    if (lines.size() + static_cast<int>(DEF_NUM_MSG_TO_LOAD) >= maxMessages) {
        removeFirsts(DEF_NUM_MSG_TO_LOAD);
    }

    for (ChatLine::Ptr l : newLines) {
        l->setRow(lines.size());
        l->addToScene(scene);
        l->visibilityChanged(false);
        lines.append(l);
    }

    layout(lines.last()->getRow(), lines.size(), useableWidth());

    // redo layout only when scrolled down
    if(stickToBottom()) {
        startResizeWorker(true);
    } else {
        updateSceneRect();
    }
}

void ChatLog::insertChatlineOnTop(ChatLine::Ptr l)
{
    numRemove = 0;
    if (!l.get())
        return;

    insertChatlinesOnTop(QList<ChatLine::Ptr>() << l);
}

void ChatLog::insertChatlinesOnTop(const QList<ChatLine::Ptr>& newLines)
{
    numRemove = 0;
    if (newLines.isEmpty())
        return;

    QGraphicsScene::ItemIndexMethod oldIndexMeth = scene->itemIndexMethod();
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);

    // alloc space for old and new lines
    QVector<ChatLine::Ptr> combLines;
    combLines.reserve(newLines.size() + lines.size());

    // add the new lines
    int i = 0;
    for (ChatLine::Ptr l : newLines) {
        l->addToScene(scene);
        l->visibilityChanged(false);
        l->setRow(i++);
        combLines.push_back(l);
    }

    if (lines.size() + static_cast<int>(DEF_NUM_MSG_TO_LOAD) >= maxMessages) {
        removeLasts(DEF_NUM_MSG_TO_LOAD);
    }

    // add the old lines
    for (ChatLine::Ptr l : lines) {
        l->setRow(i++);
        combLines.push_back(l);
    }

    lines = combLines;

    moveSelectionRectDownIfSelected(newLines.size());

    scene->setItemIndexMethod(oldIndexMeth);

    // redo layout
    if (visibleLines.size() > 1) {
        startResizeWorker(stickToBottom(), visibleLines[1]);
    } else {
        startResizeWorker(stickToBottom());
    }

}

bool ChatLog::stickToBottom() const
{
    return verticalScrollBar()->value() == verticalScrollBar()->maximum();
}

void ChatLog::scrollToBottom()
{
    updateSceneRect();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void ChatLog::startResizeWorker(bool stick, ChatLine::Ptr anchorLine)
{
    if (lines.empty()) {
        isScroll = true;
        return;
    }

    // (re)start the worker
    if (!workerTimer->isActive()) {
        // these values must not be reevaluated while the worker is running
        workerStb = stick;
        if (stick) {
            workerAnchorLine = ChatLine::Ptr();
        } else {
            workerAnchorLine = anchorLine;
        }
    }

    // switch to busy scene displaying the busy notification if there is a lot
    // of text to be resized
    int txt = 0;
    for (ChatLine::Ptr line : lines) {
        if (txt > 500000)
            break;
        for (ChatLineContent* content : line->content)
            txt += content->getText().size();
    }
    if (txt > 500000)
        setScene(busyScene);

    workerLastIndex = 0;
    workerTimer->start();

    verticalScrollBar()->hide();
}

void ChatLog::mouseDoubleClickEvent(QMouseEvent* ev)
{
    QPointF scenePos = mapToScene(ev->pos());
    ChatLineContent* content = getContentFromPos(scenePos);

    if (content) {
        content->selectionDoubleClick(scenePos);
        selClickedCol = content->getColumn();
        selClickedRow = content->getRow();
        selFirstRow = content->getRow();
        selLastRow = content->getRow();
        selectionMode = SelectionMode::Precise;

        emit selectionChanged();
    }

    if (lastClickButton == ev->button()) {
        // Counts the second click of double click
        clickCount++;
    }
    else {
        clickCount = 1; // restarting counter
        lastClickButton = ev->button();
    }
    lastClickPos = ev->pos();

    // Triggers on even click counts
    handleMultiClickEvent();
}

QString ChatLog::getSelectedText() const
{
    if (selectionMode == SelectionMode::Precise) {
        return lines[selClickedRow]->content[selClickedCol]->getSelectedText();
    } else if (selectionMode == SelectionMode::Multi) {
        // build a nicely formatted message
        QString out;

        for (int i = selFirstRow; i <= selLastRow; ++i) {
            if (lines[i]->content[1]->getText().isEmpty())
                continue;

            QString timestamp = lines[i]->content[2]->getText().isEmpty()
                                    ? tr("pending")
                                    : lines[i]->content[2]->getText();
            QString author = lines[i]->content[0]->getText();
            QString msg = lines[i]->content[1]->getText();

            out +=
                QString(out.isEmpty() ? "[%2] %1: %3" : "\n[%2] %1: %3").arg(author, timestamp, msg);
        }

        return out;
    }

    return QString();
}

bool ChatLog::isEmpty() const
{
    return lines.isEmpty();
}

bool ChatLog::hasTextToBeCopied() const
{
    return selectionMode != SelectionMode::None;
}

ChatLine::Ptr ChatLog::getTypingNotification() const
{
    return typingNotification;
}

/**
 * @brief Finds the chat line object at a position on screen
 * @param pos Position on screen in global coordinates
 * @sa getContentFromPos()
 */
ChatLineContent* ChatLog::getContentFromGlobalPos(QPoint pos) const
{
    return getContentFromPos(mapToScene(mapFromGlobal(pos)));
}

void ChatLog::clear()
{
    clearSelection();

    QVector<ChatLine::Ptr> savedLines;

    for (ChatLine::Ptr l : lines) {
        if (isActiveFileTransfer(l))
            savedLines.push_back(l);
        else
            l->removeFromScene();
    }

    lines.clear();
    visibleLines.clear();
    for (ChatLine::Ptr l : savedLines)
        insertChatlineAtBottom(l);

    updateSceneRect();
}

void ChatLog::copySelectedText(bool toSelectionBuffer) const
{
    QString text = getSelectedText();
    QClipboard* clipboard = QApplication::clipboard();

    if (clipboard && !text.isNull())
        clipboard->setText(text, toSelectionBuffer ? QClipboard::Selection : QClipboard::Clipboard);
}

void ChatLog::setBusyNotification(ChatLine::Ptr notification)
{
    if (!notification.get())
        return;

    busyNotification = notification;
    busyNotification->addToScene(busyScene);
    busyNotification->visibilityChanged(true);
}

void ChatLog::setTypingNotification(ChatLine::Ptr notification)
{
    typingNotification = notification;
    typingNotification->visibilityChanged(true);
    typingNotification->setVisible(false);
    typingNotification->addToScene(scene);
    updateTypingNotification();
}

void ChatLog::setTypingNotificationVisible(bool visible)
{
    if (typingNotification.get()) {
        typingNotification->setVisible(visible);
        updateTypingNotification();
    }
}

void ChatLog::scrollToLine(ChatLine::Ptr line)
{
    if (!line.get())
        return;

    if (workerTimer->isActive()) {
        workerAnchorLine = line;
        workerStb = false;
    } else {
        updateSceneRect();
        verticalScrollBar()->setValue(line->sceneBoundingRect().top());
    }
}

void ChatLog::selectAll()
{
    if (lines.empty()) {
        return;
    }

    clearSelection();

    selectionMode = SelectionMode::Multi;
    selFirstRow = 0;
    selLastRow = lines.size() - 1;

    emit selectionChanged();
    updateMultiSelectionRect();
}

void ChatLog::fontChanged(const QFont& font)
{
    for (ChatLine::Ptr l : lines) {
        l->fontChanged(font);
    }
}

void ChatLog::reloadTheme()
{
    setStyleSheet(Style::getStylesheet("chatArea/chatArea.css"));
    setBackgroundBrush(QBrush(Style::getColor(Style::GroundBase), Qt::SolidPattern));
    selectionRectColor = Style::getColor(Style::SelectText);
    selGraphItem->setBrush(QBrush(selectionRectColor));
    selGraphItem->setPen(QPen(selectionRectColor.darker(120)));

    for (ChatLine::Ptr l : lines) {
        l->reloadTheme();
    }
}

void ChatLog::removeFirsts(const int num)
{
    if (lines.size() > num) {
        lines.erase(lines.begin(), lines.begin()+num);
        numRemove = num;
    } else {
        lines.clear();
    }

    for (int i = 0; i < lines.size(); ++i) {
        lines[i]->setRow(i);
    }

    moveSelectionRectUpIfSelected(num);
}

void ChatLog::removeLasts(const int num)
{
    if (lines.size() > num) {
        lines.erase(lines.end()-num, lines.end());
        numRemove = num;
    } else {
        lines.clear();
    }
}

void ChatLog::setScroll(const bool scroll)
{
    isScroll = scroll;
}

int ChatLog::getNumRemove() const
{
    return numRemove;
}

void ChatLog::forceRelayout()
{
    startResizeWorker(stickToBottom());
}

void ChatLog::checkVisibility(bool causedWheelEvent)
{
    if (lines.empty()) {
        return;
    }

    // find first visible line
    auto lowerBound = std::lower_bound(lines.cbegin(), lines.cend(), getVisibleRect().top(),
                                       ChatLine::lessThanBSRectBottom);

    // find last visible line
    auto upperBound = std::lower_bound(lowerBound, lines.cend(), getVisibleRect().bottom(),
                                       ChatLine::lessThanBSRectTop);

    const ChatLine::Ptr lastLineBeforeVisible = lowerBound == lines.cbegin()
        ? ChatLine::Ptr()
        : *std::prev(lowerBound);

    // set visibilty
    QList<ChatLine::Ptr> newVisibleLines;
    for (auto itr = lowerBound; itr != upperBound; ++itr) {
        newVisibleLines.append(*itr);

        if (!visibleLines.contains(*itr))
            (*itr)->visibilityChanged(true);

        visibleLines.removeOne(*itr);
    }

    // these lines are no longer visible
    for (ChatLine::Ptr line : visibleLines)
        line->visibilityChanged(false);

    visibleLines = newVisibleLines;

    // enforce order
    std::sort(visibleLines.begin(), visibleLines.end(), ChatLine::lessThanRowIndex);

    // if (!visibleLines.empty())
    //  qDebug() << "visible from " << visibleLines.first()->getRow() << "to " <<
    //  visibleLines.last()->getRow() << " total " << visibleLines.size();

    if (!visibleLines.isEmpty()) {
        emit firstVisibleLineChanged(lastLineBeforeVisible, visibleLines.at(0));
    }

    if (causedWheelEvent) {
        if (lowerBound != lines.cend() && lowerBound->get()->row == 0) {
            emit loadHistoryLower();
        } else if (upperBound == lines.cend()) {
            emit loadHistoryUpper();
        }
    }
}

void ChatLog::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    checkVisibility();
}

void ChatLog::resizeEvent(QResizeEvent* ev)
{
    bool stb = stickToBottom();

    if (ev->size().width() != ev->oldSize().width()) {
        startResizeWorker(stb);
        stb = false; // let the resize worker handle it
    }

    QGraphicsView::resizeEvent(ev);

    if (stb)
        scrollToBottom();

    updateBusyNotification();
}

void ChatLog::updateMultiSelectionRect()
{
    if (selectionMode == SelectionMode::Multi && selFirstRow >= 0 && selLastRow >= 0) {
        QRectF selBBox;
        selBBox = selBBox.united(lines[selFirstRow]->sceneBoundingRect());
        selBBox = selBBox.united(lines[selLastRow]->sceneBoundingRect());

        if (selGraphItem->rect() != selBBox)
            scene->invalidate(selGraphItem->rect());

        selGraphItem->setRect(selBBox);
        selGraphItem->show();
    } else {
        selGraphItem->hide();
    }
}

void ChatLog::updateTypingNotification()
{
    ChatLine* notification = typingNotification.get();
    if (!notification)
        return;

    qreal posY = 0.0;

    if (!lines.empty()) {
        posY = lines.last()->sceneBoundingRect().bottom() + lineSpacing;
    }

    notification->layout(useableWidth(), QPointF(0.0, posY));
}

void ChatLog::updateBusyNotification()
{
    if (busyNotification.get()) {
        // repoisition the busy notification (centered)
        busyNotification->layout(useableWidth(), getVisibleRect().topLeft()
                                                     + QPointF(0, getVisibleRect().height() / 2.0));
    }
}

ChatLine::Ptr ChatLog::findLineByPosY(qreal yPos) const
{
    auto itr = std::lower_bound(lines.cbegin(), lines.cend(), yPos, ChatLine::lessThanBSRectBottom);

    if (itr != lines.cend())
        return *itr;

    return ChatLine::Ptr();
}

QRectF ChatLog::calculateSceneRect() const
{
    qreal bottom = (lines.empty() ? 0.0 : lines.last()->sceneBoundingRect().bottom());

    if (typingNotification.get() != nullptr) {
        bottom += typingNotification->sceneBoundingRect().height() + lineSpacing;
    }

    return QRectF(-margins.left(), -margins.top(), useableWidth(),
                  bottom + margins.bottom() + margins.top());
}

void ChatLog::onSelectionTimerTimeout()
{
    const int scrollSpeed = 10;

    switch (selectionScrollDir) {
    case AutoScrollDirection::Up:
        verticalScrollBar()->setValue(verticalScrollBar()->value() - scrollSpeed);
        break;
    case AutoScrollDirection::Down:
        verticalScrollBar()->setValue(verticalScrollBar()->value() + scrollSpeed);
        break;
    default:
        break;
    }
}

void ChatLog::onWorkerTimeout()
{
    // Fairly arbitrary but
    // large values will make the UI unresponsive
    const int stepSize = 50;

    layout(workerLastIndex, workerLastIndex + stepSize, useableWidth());
    workerLastIndex += stepSize;

    // done?
    if (workerLastIndex >= lines.size()) {
        workerTimer->stop();

        // switch back to the scene containing the chat messages
        setScene(scene);

        // make sure everything gets updated
        updateSceneRect();
        checkVisibility();
        updateTypingNotification();
        updateMultiSelectionRect();

        // scroll
        if (workerStb) {
            scrollToBottom();
            workerStb = false;
        } else {
            scrollToLine(workerAnchorLine);
        }

        // don't keep a Ptr to the anchor line
        workerAnchorLine = ChatLine::Ptr();

        // hidden during busy screen
        verticalScrollBar()->show();

        isScroll = true;
        emit workerTimeoutFinished();
    }
}

void ChatLog::onMultiClickTimeout()
{
    clickCount = 0;
}

void ChatLog::handleMultiClickEvent()
{
    // Ignore single or double clicks
    if (clickCount < 2)
        return;

    switch (clickCount) {
    case 3:
        QPointF scenePos = mapToScene(lastClickPos);
        ChatLineContent* content = getContentFromPos(scenePos);

        if (content) {
            content->selectionTripleClick(scenePos);
            selClickedCol = content->getColumn();
            selClickedRow = content->getRow();
            selFirstRow = content->getRow();
            selLastRow = content->getRow();
            selectionMode = SelectionMode::Precise;

            emit selectionChanged();
        }
        break;
    }
}

void ChatLog::showEvent(QShowEvent*)
{
    // Empty.
    // The default implementation calls centerOn - for some reason - causing
    // the scrollbar to move.
}

void ChatLog::focusInEvent(QFocusEvent* ev)
{
    QGraphicsView::focusInEvent(ev);

    if (selectionMode != SelectionMode::None) {
        selGraphItem->setBrush(QBrush(selectionRectColor));

        for (int i = selFirstRow; i <= selLastRow; ++i)
            lines[i]->selectionFocusChanged(true);
    }
}

void ChatLog::focusOutEvent(QFocusEvent* ev)
{
    QGraphicsView::focusOutEvent(ev);

    if (selectionMode != SelectionMode::None) {
        selGraphItem->setBrush(QBrush(selectionRectColor.lighter(120)));

        for (int i = selFirstRow; i <= selLastRow; ++i)
            lines[i]->selectionFocusChanged(false);
    }
}

void ChatLog::wheelEvent(QWheelEvent *event)
{
    if (!isScroll) {
        return;
    }

    QGraphicsView::wheelEvent(event);
    checkVisibility(true);
}

void ChatLog::retranslateUi()
{
    copyAction->setText(tr("Copy"));
    selectAllAction->setText(tr("Select all"));
}

bool ChatLog::isActiveFileTransfer(ChatLine::Ptr l)
{
    int count = l->getColumnCount();
    for (int i = 0; i < count; ++i) {
        ChatLineContent* content = l->getContent(i);
        ChatLineContentProxy* proxy = qobject_cast<ChatLineContentProxy*>(content);
        if (!proxy)
            continue;

        QWidget* widget = proxy->getWidget();
        FileTransferWidget* transferWidget = qobject_cast<FileTransferWidget*>(widget);
        if (transferWidget && transferWidget->isActive())
            return true;
    }

    return false;
}

/**
 * @brief Adjusts the selection based on chatlog changing lines
 * @param offset Amount to shift selection rect up by. Must be non-negative.
  */
void ChatLog::moveSelectionRectUpIfSelected(int offset)
{
    assert(offset >= 0);
    switch (selectionMode)
    {
        case SelectionMode::None:
            return;
        case SelectionMode::Precise:
            movePreciseSelectionUp(offset);
            break;
        case SelectionMode::Multi:
            moveMultiSelectionUp(offset);
            break;
    }
}

/**
 * @brief Adjusts the selections based on chatlog changing lines
 * @param offset removed from the lines indexes. Must be non-negative.
  */
void ChatLog::moveSelectionRectDownIfSelected(int offset)
{
    assert(offset >= 0);
    switch (selectionMode)
    {
        case SelectionMode::None:
            return;
        case SelectionMode::Precise:
            movePreciseSelectionDown(offset);
            break;
        case SelectionMode::Multi:
            moveMultiSelectionDown(offset);
            break;
    }
}

void ChatLog::movePreciseSelectionDown(int offset)
{
    assert(selFirstRow == selLastRow && selFirstRow == selClickedRow);
    const int lastLine = lines.size() - 1;
    if (selClickedRow + offset > lastLine) {
        clearSelection();
    } else {
        const int newRow = selClickedRow + offset;
        selClickedRow = newRow;
        selLastRow = newRow;
        selFirstRow = newRow;
        emit selectionChanged();
    }
}

void ChatLog::movePreciseSelectionUp(int offset)
{
    assert(selFirstRow == selLastRow && selFirstRow == selClickedRow);
    if (selClickedRow < offset) {
        clearSelection();
    } else {
        const int newRow = selClickedRow - offset;
        selClickedRow = newRow;
        selLastRow = newRow;
        selFirstRow = newRow;
        emit selectionChanged();
    }
}

void ChatLog::moveMultiSelectionUp(int offset)
{
    if (selLastRow < offset) { // entire selection now out of bounds
        clearSelection();
    } else {
        selLastRow -= offset;
        selClickedRow = std::max(0, selClickedRow - offset);
        selFirstRow = std::max(0, selFirstRow - offset);
        updateMultiSelectionRect();
        emit selectionChanged();
    }
}

void ChatLog::moveMultiSelectionDown(int offset)
{
    const int lastLine = lines.size() - 1;
    if (selFirstRow + offset > lastLine) { // entire selection now out of bounds
        clearSelection();
    } else {
        selFirstRow += offset;
        selClickedRow = std::min(lastLine, selClickedRow + offset);
        selLastRow = std::min(lastLine, selLastRow + offset);
        updateMultiSelectionRect();
        emit selectionChanged();
    }
}
