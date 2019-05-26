/*
    Copyright © 2014-2018 by The qTox Project Contributors

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

#ifndef FILETRANSFERWIDGET_H
#define FILETRANSFERWIDGET_H

#include <QTime>
#include <QWidget>

#include "src/chatlog/chatlinecontent.h"
#include "src/chatlog/toxfileprogress.h"
#include "src/core/toxfile.h"


namespace Ui {
class FileTransferWidget;
}

class QVariantAnimation;
class QPushButton;

class FileTransferWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FileTransferWidget(QWidget* parent, ToxFile file);
    virtual ~FileTransferWidget();
    bool isActive() const;
    static QString getHumanReadableSize(qint64 size);

    void onFileTransferUpdate(ToxFile file);

protected:
    void updateWidgetColor(ToxFile const& file);
    void updateWidgetText(ToxFile const& file);
    void updateFileProgress(ToxFile const& file);
    void updateSignals(ToxFile const& file);
    void updatePreview(ToxFile const& file);
    void setupButtons(ToxFile const& file);
    void handleButton(QPushButton* btn);
    void showPreview(const QString& filename);
    void acceptTransfer(const QString& filepath);
    void setBackgroundColor(const QColor& c, bool whiteFont);
    void setButtonColor(const QColor& c);

    bool drawButtonAreaNeeded() const;

    virtual void paintEvent(QPaintEvent*) final override;

private slots:
    void onLeftButtonClicked();
    void onRightButtonClicked();
    void onPreviewButtonClicked();

private:
    static QPixmap scaleCropIntoSquare(const QPixmap& source, int targetSize);
    static int getExifOrientation(const char* data, const int size);
    static void applyTransformation(const int oritentation, QImage& image);
    static bool tryRemoveFile(const QString &filepath);

    void updateWidget(ToxFile const& file);

private:
    Ui::FileTransferWidget* ui;
    ToxFileProgress fileProgress;
    ToxFile fileInfo;
    QVariantAnimation* backgroundColorAnimation = nullptr;
    QVariantAnimation* buttonColorAnimation = nullptr;
    QColor backgroundColor;
    QColor buttonColor;
    QColor buttonBackgroundColor;

    bool active;
    ToxFile::FileStatus lastStatus = ToxFile::INITIALIZING;

    enum class ExifOrientation
    {
        /* do not change values, this is exif spec
         *
         * name corresponds to where the 0 row and 0 column is in form row-column
         * i.e. entry 5 here means that the 0'th row corresponds to the left side of the scene and
         * the 0'th column corresponds to the top of the captured scene. This means that the image
         * needs to be mirrored and rotated to be displayed.
         */
        TopLeft = 1,
        TopRight = 2,
        BottomRight = 3,
        BottomLeft = 4,
        LeftTop = 5,
        RightTop = 6,
        RightBottom = 7,
        LeftBottom = 8
    };
};

#endif // FILETRANSFERWIDGET_H
