/*
    Copyright (C) 2014 by Project Tox <https://tox.im>

    This file is part of qTox, a Qt-based graphical interface for Tox.

    This program is libre software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

    See the COPYING file for more details.
*/

#include "croppinglabel.h"
#include "src/widget/widget.h"
#include <QResizeEvent>
#include <QLineEdit>

CroppingLabel::CroppingLabel(QWidget* parent)
    : QLabel(parent)
    , blockPaintEvents(false)
    , editable(false)
    , elideMode(Qt::ElideRight)
    , highlightURLs(false)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setOpenExternalLinks(false);

    textEdit = new QLineEdit(this);
    textEdit->hide();

    installEventFilter(this);
    textEdit->installEventFilter(this);
}

void CroppingLabel::setEditable(bool editable)
{
    this->editable = editable;

    if (editable)
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();
}

void CroppingLabel::setElideMode(Qt::TextElideMode elide)
{
    elideMode = elide;
}

void CroppingLabel::setClickableURLs(bool clickableURLs)
{
    this->highlightURLs = clickableURLs;
    setOpenExternalLinks(clickableURLs);
}

void CroppingLabel::setText(const QString& text)
{
    origText = text.trimmed();
    setElidedText();
}

void CroppingLabel::resizeEvent(QResizeEvent* ev)
{
    setElidedText();
    textEdit->resize(ev->size());

    QLabel::resizeEvent(ev);
}

QSize CroppingLabel::sizeHint() const
{
    return QSize(0, QLabel::sizeHint().height());
}

QSize CroppingLabel::minimumSizeHint() const
{
    return QSize(fontMetrics().width("..."), QLabel::minimumSizeHint().height());
}

void CroppingLabel::mouseReleaseEvent(QMouseEvent *e)
{
    if (editable)
        showTextEdit();

    emit clicked();

    QLabel::mouseReleaseEvent(e);
}

bool CroppingLabel::eventFilter(QObject *obj, QEvent *e)
{
    // catch paint events if needed
    if (obj == this)
    {
        if (e->type() == QEvent::Paint && blockPaintEvents)
            return true;
    }

    // events fired by the QLineEdit
    if (obj == textEdit)
    {
        if (!textEdit->isVisible())
            return false;

        if (e->type() == QEvent::KeyPress)
        {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(e);
            if (keyEvent->key() == Qt::Key_Return)
                hideTextEdit(true);

            if (keyEvent->key() == Qt::Key_Escape)
                hideTextEdit(false);
        }

        if (e->type() == QEvent::FocusOut)
            hideTextEdit(true);
    }

    return false;
}

void CroppingLabel::setElidedText()
{
    QString elidedText = fontMetrics().elidedText(origText, elideMode, width());

    if (elidedText != origText)
    {
        QString parsedText = Widget::parseMessage(origText, highlightURLs, elidedText.length() - 1);
        QLabel::setText("<div>" + parsedText.trimmed() + "&hellip;</div>");
    } else {
        QString parsedText = Widget::parseMessage(origText, highlightURLs);
        QLabel::setText(parsedText);
    }

    // Don't underline links in tooltips because they are unclickable
    setToolTip(origText);
}

void CroppingLabel::hideTextEdit(bool acceptText)
{
    if (acceptText)
    {
        QString oldOrigText = origText;
        setText(textEdit->text()); // set before emitting so we don't override external reactions to signal
        emit textChanged(textEdit->text(), oldOrigText);
    }

    textEdit->hide();
    blockPaintEvents = false;
}

void CroppingLabel::showTextEdit()
{
    blockPaintEvents = true;
    textEdit->show();
    textEdit->setFocus();
    textEdit->setText(origText);
}

QString CroppingLabel::fullText()
{
    return origText;
}
