/*
    Copyright © 2015 by The qTox Project

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


#ifndef CALLCONFIRMWIDGET_H
#define CALLCONFIRMWIDGET_H

#include <QWidget>
#include <QRect>
#include <QPolygon>
#include <QBrush>

class QPaintEvent;
class QShowEvent;
class Friend;

class CallConfirmWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit CallConfirmWidget(const QWidget *Anchor, const Friend& f);

signals:
    void accepted();
    void rejected();

public slots:
    void reposition();

protected:
    void paintEvent(QPaintEvent* event) final override;
    void showEvent(QShowEvent* event) final override;
    void hideEvent(QHideEvent* event) final override;
    bool eventFilter(QObject *, QEvent* event) final override;

private:
    const QWidget* anchor;
    const Friend& f;

    QRect mainRect;
    QPolygon spikePoly;
    QBrush brush;

    const int rectW, rectH;
    const int spikeW, spikeH;
    const int roundedFactor;
    const qreal rectRatio;
};

#endif // CALLCONFIRMWIDGET_H
