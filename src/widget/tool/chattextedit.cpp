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

#include "chattextedit.h"

#include "src/widget/translator.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMimeData>

ChatTextEdit::ChatTextEdit(QWidget* parent)
    : QTextEdit(parent)
{
    retranslateUi();
    setAcceptRichText(false);
    setAcceptDrops(false);

    Translator::registerHandler(std::bind(&ChatTextEdit::retranslateUi, this), this);
}

ChatTextEdit::~ChatTextEdit()
{
    Translator::unregister(this);
}

void ChatTextEdit::keyPressEvent(QKeyEvent* event)
{
    int key = event->key();
    if ((key == Qt::Key_Enter || key == Qt::Key_Return) && !(event->modifiers() & Qt::ShiftModifier)) {
        emit enterPressed();
        return;
    }
    if (key == Qt::Key_Escape) {
        emit escapePressed();
        return;
    }
    if (key == Qt::Key_Tab) {
        if (event->modifiers())
            event->ignore();
        else {
            emit tabPressed();
            event->ignore();
        }
        return;
    }
    if (key == Qt::Key_Up && this->toPlainText().isEmpty()) {
        this->setPlainText(lastMessage);
        this->setFocus();
        this->moveCursor(QTextCursor::MoveOperation::End, QTextCursor::MoveMode::MoveAnchor);
        return;
    }
    if (event->matches(QKeySequence::Paste) && pasteIfImage(event)) {
        return;
    }
    emit keyPressed();
    QTextEdit::keyPressEvent(event);
}

void ChatTextEdit::setLastMessage(QString lm)
{
    lastMessage = lm;
}

void ChatTextEdit::retranslateUi()
{
    setPlaceholderText(tr("Type your message here..."));
}

void ChatTextEdit::sendKeyEvent(QKeyEvent* event)
{
    emit keyPressEvent(event);
}

bool ChatTextEdit::pasteIfImage(QKeyEvent* event)
{
    const QClipboard* const clipboard = QApplication::clipboard();
    if (!clipboard) {
        return false;
    }

    const QMimeData* const mimeData = clipboard->mimeData();
    if (!mimeData || !mimeData->hasImage()) {
        return false;
    }

    const QPixmap pixmap(clipboard->pixmap());
    if (pixmap.isNull()) {
        return false;
    }

    emit pasteImage(pixmap);
    return true;
}

