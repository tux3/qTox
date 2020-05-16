/*
    Copyright © 2020 by The qTox Project Contributors

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

#pragma once

#include <QPixmap>

namespace ExifTransform
{
    enum class ExifOrientation
    {
        /* name corresponds to where the 0 row and 0 column is in form row-column
         * i.e. entry 5 here means that the 0'th row corresponds to the left side of the scene and
         * the 0'th column corresponds to the top of the captured scene. This means that the image
         * needs to be mirrored and rotated to be displayed.
         */
        TopLeft,
        TopRight,
        BottomRight,
        BottomLeft,
        LeftTop,
        RightTop,
        RightBottom,
        LeftBottom
    };

    ExifOrientation getExifOrientation(QByteArray imageData);
    QImage applyExifTransformation(QImage image, ExifOrientation orientation);
};
