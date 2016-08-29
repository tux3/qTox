/*
    Copyright © 2014-2015 by The qTox Project

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

#include "croppinglabel.h"
#include <QResizeEvent>
#include <QLineEdit>
#include <QKeyEvent>
#include <QTextDocument> 

CroppingLabel::CroppingLabel(QWidget* parent)
    : QLabel(parent)
    , blockPaintEvents(false)
    , editable(false)
    , elideMode(Qt::ElideRight)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    class LineEdit : public QLineEdit
    {
    public:
        explicit LineEdit(QWidget* parent = 0) :
            QLineEdit(parent)
        {}

    protected:
        void keyPressEvent(QKeyEvent* event) override
        {
            if (event->key() == Qt::Key_Escape)
            {
                undo();
                clearFocus();
            }

            QLineEdit::keyPressEvent(event);
        }
    };

    textEdit = new LineEdit(this);
    textEdit->hide();
    textEdit->setInputMethodHints(Qt::ImhNoAutoUppercase
                                  | Qt::ImhNoPredictiveText
                                  | Qt::ImhPreferLatin);

    connect(textEdit, &QLineEdit::editingFinished, this, &CroppingLabel::editingFinished);
}

void CroppingLabel::editBegin()
{
    showTextEdit();
    textEdit->selectAll();
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

void CroppingLabel::paintEvent(QPaintEvent* paintEvent)
{
    if (blockPaintEvents)
    {
        paintEvent->ignore();
        return;
    }
    QLabel::paintEvent(paintEvent);
}

void CroppingLabel::setElidedText()
{
    QString elidedText = fontMetrics().elidedText(origText, elideMode, width());
    if (elidedText != origText)
        setToolTip(Qt::convertFromPlainText(origText, Qt::WhiteSpaceNormal));
    else
        setToolTip(QString());

    QLabel::setText(elidedText);
}

void CroppingLabel::hideTextEdit()
{
    textEdit->hide();
    blockPaintEvents = false;
}

void CroppingLabel::showTextEdit()
{
    blockPaintEvents = true;
    textEdit->show();
    textEdit->setFocus();
    textEdit->setText(origText);
    textEdit->setFocusPolicy(Qt::ClickFocus);
}

/**
 * @brief Get original full text.
 * @return The un-cropped text.
 */
QString CroppingLabel::fullText()
{
    return origText;
}

void CroppingLabel::minimizeMaximumWidth()
{
    // This function chooses the smallest possible maximum width.
    // Text width + padding. Without padding, we'll have elipses.
    setMaximumWidth(fontMetrics().width(origText) + fontMetrics().width("..."));
}

void CroppingLabel::editingFinished()
{
    hideTextEdit();
    QString newText = textEdit->text().trimmed().remove(QRegExp("[\\t\\n\\v\\f\\r\\x0000]"));

    if (origText != newText)
        emit editFinished(textEdit->text());

    emit editRemoved();
}
