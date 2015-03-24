/* Copyright (c) 2014-2015, The Nuria Project
 * The NuriaProject Framework is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 * 
 * The NuriaProject Framework is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with The NuriaProject Framework.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "screengrabberoverlayitem.hpp"

#include <QGraphicsSceneMouseEvent>
#include <QPen>

#include "screenshotgrabber.h"

ScreenGrabberOverlayItem::ScreenGrabberOverlayItem(ScreenshotGrabber *grabber)
    : screnshootGrabber(grabber)
{
    
    QBrush overlayBrush(QColor(0x00, 0x00, 0x00, 0x70)); // Translucent black
    
    setCursor(QCursor(Qt::CrossCursor));
    setBrush(overlayBrush);
    setPen(QPen(Qt::NoPen));
    
}

ScreenGrabberOverlayItem::~ScreenGrabberOverlayItem()
{
    
}

void ScreenGrabberOverlayItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        this->screnshootGrabber->beginRectChooser(event);
    }
    
}
