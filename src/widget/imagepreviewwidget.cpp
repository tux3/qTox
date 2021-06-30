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

#include "imagepreviewwidget.h"
#include "src/model/exiftransform.h"

#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QApplication>
#include <QDesktopWidget>
#include <QBuffer>

namespace
{
QPixmap pixmapFromFile(const QString& filename)
{
    static const QStringList previewExtensions = {"png", "jpeg", "jpg", "gif", "svg",
                                                  "PNG", "JPEG", "JPG", "GIF", "SVG"};

    if (!previewExtensions.contains(QFileInfo(filename).suffix())) {
        return QPixmap();
    }

    QFile imageFile(filename);
    if (!imageFile.open(QIODevice::ReadOnly)) {
        return QPixmap();
    }

    const QByteArray imageFileData = imageFile.readAll();
    QImage image = QImage::fromData(imageFileData);
    auto orientation = ExifTransform::getOrientation(imageFileData);
    image = ExifTransform::applyTransformation(image, orientation);

    return QPixmap::fromImage(image);
}

QPixmap scaleCropIntoSquare(const QPixmap& source, const int targetSize)
{
    QPixmap result;

    // Make sure smaller-than-icon images (at least one dimension is smaller) will not be
    // upscaled
    if (source.width() < targetSize || source.height() < targetSize) {
        result = source;
    } else {
        result = source.scaled(targetSize, targetSize, Qt::KeepAspectRatioByExpanding,
                               Qt::SmoothTransformation);
    }

    // Then, image has to be cropped (if needed) so it will not overflow rectangle
    // Only one dimension will be bigger after Qt::KeepAspectRatioByExpanding
    if (result.width() > targetSize) {
        return result.copy((result.width() - targetSize) / 2, 0, targetSize, targetSize);
    } else if (result.height() > targetSize) {
        return result.copy(0, (result.height() - targetSize) / 2, targetSize, targetSize);
    }

    // Picture was rectangle in the first place, no cropping
    return result;
}

QString getToolTipDisplayingImage(const QPixmap& image)
{
    // Show mouseover preview, but make sure it's not larger than 50% of the screen
    // width/height
    const QRect desktopSize = QApplication::desktop()->geometry();
    const int maxPreviewWidth{desktopSize.width() / 2};
    const int maxPreviewHeight{desktopSize.height() / 2};
    const QPixmap previewImage = [&image, maxPreviewWidth, maxPreviewHeight]() {
        if (image.width() > maxPreviewWidth || image.height() > maxPreviewHeight) {
            return image.scaled(maxPreviewWidth, maxPreviewHeight, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation);
        } else {
            return image;
        }
    }();

    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);
    previewImage.save(&buffer, "PNG");
    buffer.close();

    return "<img src=data:image/png;base64," + imageData.toBase64() + "/>";
}

} // namespace

void ImagePreviewButton::initialize(const QPixmap& image)
{
    auto desiredSize = qMin(width(), height()); // Assume widget is a square
    desiredSize = qMax(desiredSize, 4) - 4; // Leave some room for a border

    auto croppedImage = scaleCropIntoSquare(image, desiredSize);
    setIcon(QIcon(croppedImage));
    setIconSize(croppedImage.size());
    setToolTip(getToolTipDisplayingImage(image));
}

void ImagePreviewButton::setIconFromFile(const QString& filename)
{
    initialize(pixmapFromFile(filename));
}

void ImagePreviewButton::setIconFromPixmap(const QPixmap& pixmap)
{
    initialize(pixmap);
}
