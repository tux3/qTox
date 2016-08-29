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

#ifndef CROPPINGLABEL_H
#define CROPPINGLABEL_H

#include <QLabel>

class QLineEdit;

class CroppingLabel : public QLabel
{
    Q_OBJECT
public:
    explicit CroppingLabel(QWidget* parent = 0);

    void editBegin();
    void setEditable(bool editable);
    void setElideMode(Qt::TextElideMode elide);

    void setText(const QString& text);
    QString fullText();


public slots:
    void minimizeMaximumWidth();

signals:
    void editFinished(const QString& newText);
    void editRemoved();
    void clicked();

protected:
    void paintEvent(QPaintEvent* paintEvent) override;
    void setElidedText();
    void hideTextEdit();
    void showTextEdit();
    virtual void resizeEvent(QResizeEvent *ev) final override;
    virtual QSize sizeHint() const final override;
    virtual QSize minimumSizeHint() const final override;
    virtual void mouseReleaseEvent(QMouseEvent *e) final override;

private slots:
    void editingFinished();

private:
    QString origText;
    QLineEdit* textEdit;
    bool blockPaintEvents;
    bool editable;
    Qt::TextElideMode elideMode;
};

#endif // CROPPINGLABEL_H
