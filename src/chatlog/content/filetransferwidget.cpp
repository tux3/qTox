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

#include "filetransferwidget.h"
#include "ui_filetransferwidget.h"

#include "src/core/core.h"
#include "src/core/corefile.h"
#include "src/persistence/settings.h"
#include "src/widget/gui.h"
#include "src/widget/style.h"
#include "src/widget/widget.h"

#include <libexif/exif-loader.h>

#include <QBuffer>
#include <QDebug>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>

#include <cassert>
#include <math.h>


// The leftButton is used to accept, pause, or resume a file transfer, as well as to open a
// received file.
// The rightButton is used to cancel a file transfer, or to open the directory a file was
// downloaded to.

FileTransferWidget::FileTransferWidget(QWidget* parent, ToxFile file)
    : QWidget(parent)
    , ui(new Ui::FileTransferWidget)
    , fileInfo(file)
    , backgroundColor(Style::getColor(Style::TransferMiddle))
    , buttonColor(Style::getColor(Style::TransferWait))
    , buttonBackgroundColor(Style::getColor(Style::GroundBase))
    , active(true)
{
    ui->setupUi(this);

    // hide the QWidget background (background-color: transparent doesn't seem to work)
    setAttribute(Qt::WA_TranslucentBackground, true);

    ui->previewButton->hide();
    ui->filenameLabel->setText(file.fileName);
    ui->progressBar->setValue(0);
    ui->fileSizeLabel->setText(getHumanReadableSize(file.filesize));
    ui->etaLabel->setText("");

    backgroundColorAnimation = new QVariantAnimation(this);
    backgroundColorAnimation->setDuration(500);
    backgroundColorAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(backgroundColorAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& val) {
                backgroundColor = val.value<QColor>();
                update();
            });

    buttonColorAnimation = new QVariantAnimation(this);
    buttonColorAnimation->setDuration(500);
    buttonColorAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(buttonColorAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
        buttonColor = val.value<QColor>();
        update();
    });

    CoreFile* coreFile = Core::getInstance()->getCoreFile();

    connect(ui->leftButton, &QPushButton::clicked, this, &FileTransferWidget::onLeftButtonClicked);
    connect(ui->rightButton, &QPushButton::clicked, this, &FileTransferWidget::onRightButtonClicked);
    connect(ui->previewButton, &QPushButton::clicked, this,
            &FileTransferWidget::onPreviewButtonClicked);

    // Set lastStatus to anything but the file's current value, this forces an update
    lastStatus = file.status == ToxFile::FINISHED ? ToxFile::INITIALIZING : ToxFile::FINISHED;
    updateWidget(file);

    setFixedHeight(64);
}

FileTransferWidget::~FileTransferWidget()
{
    delete ui;
}

// TODO(sudden6): remove file IO from the UI
/**
 * @brief Dangerous way to find out if a path is writable.
 * @param filepath Path to file which should be deleted.
 * @return True, if file writeable, false otherwise.
 */
bool FileTransferWidget::tryRemoveFile(const QString& filepath)
{
    QFile tmp(filepath);
    bool writable = tmp.open(QIODevice::WriteOnly);
    tmp.remove();
    return writable;
}

void FileTransferWidget::onFileTransferUpdate(ToxFile file)
{
    updateWidget(file);
}

bool FileTransferWidget::isActive() const
{
    return active;
}

void FileTransferWidget::acceptTransfer(const QString& filepath)
{
    if (filepath.isEmpty()) {
        return;
    }

    // test if writable
    if (!tryRemoveFile(filepath)) {
        GUI::showWarning(tr("Location not writable", "Title of permissions popup"),
                         tr("You do not have permission to write that location. Choose another, or "
                            "cancel the save dialog.",
                            "text of permissions popup"));
        return;
    }

    // everything ok!
    CoreFile* coreFile = Core::getInstance()->getCoreFile();
    coreFile->acceptFileRecvRequest(fileInfo.friendId, fileInfo.fileNum, filepath);
}

void FileTransferWidget::setBackgroundColor(const QColor& c, bool whiteFont)
{
    if (c != backgroundColor) {
        backgroundColorAnimation->setStartValue(backgroundColor);
        backgroundColorAnimation->setEndValue(c);
        backgroundColorAnimation->start();
    }

    setProperty("fontColor", whiteFont ? "white" : "black");

    setStyleSheet(Style::getStylesheet("fileTransferInstance/filetransferWidget.css"));
    Style::repolish(this);

    update();
}

void FileTransferWidget::setButtonColor(const QColor& c)
{
    if (c != buttonColor) {
        buttonColorAnimation->setStartValue(buttonColor);
        buttonColorAnimation->setEndValue(c);
        buttonColorAnimation->start();
    }
}

bool FileTransferWidget::drawButtonAreaNeeded() const
{
    return (ui->rightButton->isVisible() || ui->leftButton->isVisible())
           && !(ui->leftButton->isVisible() && ui->leftButton->objectName() == "ok");
}

void FileTransferWidget::paintEvent(QPaintEvent*)
{
    // required by Hi-DPI support as border-image doesn't work.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    qreal ratio = static_cast<qreal>(geometry().height()) / static_cast<qreal>(geometry().width());
    const int r = 24;
    const int buttonFieldWidth = 32;
    const int lineWidth = 1;

    // Draw the widget background:
    painter.setClipRect(QRect(0, 0, width(), height()));
    painter.setBrush(QBrush(backgroundColor));
    painter.drawRoundRect(geometry(), r * ratio, r);

    if (drawButtonAreaNeeded()) {
        // Draw the button background:
        QPainterPath buttonBackground;
        buttonBackground.addRoundRect(width() - 2 * buttonFieldWidth - lineWidth * 2, 0,
                                      buttonFieldWidth, buttonFieldWidth + lineWidth, 50, 50);
        buttonBackground.addRect(width() - 2 * buttonFieldWidth - lineWidth * 2, 0,
                                 buttonFieldWidth * 2, buttonFieldWidth / 2);
        buttonBackground.addRect(width() - 1.5 * buttonFieldWidth - lineWidth * 2, 0,
                                 buttonFieldWidth * 2, buttonFieldWidth + 1);
        buttonBackground.setFillRule(Qt::WindingFill);
        painter.setBrush(QBrush(buttonBackgroundColor));
        painter.drawPath(buttonBackground);

        // Draw the left button:
        QPainterPath leftButton;
        leftButton.addRoundRect(QRect(width() - 2 * buttonFieldWidth - lineWidth, 0,
                                      buttonFieldWidth, buttonFieldWidth),
                                50, 50);
        leftButton.addRect(QRect(width() - 2 * buttonFieldWidth - lineWidth, 0,
                                 buttonFieldWidth / 2, buttonFieldWidth / 2));
        leftButton.addRect(QRect(width() - 1.5 * buttonFieldWidth - lineWidth, 0,
                                 buttonFieldWidth / 2, buttonFieldWidth));
        leftButton.setFillRule(Qt::WindingFill);
        painter.setBrush(QBrush(buttonColor));
        painter.drawPath(leftButton);

        // Draw the right button:
        painter.setBrush(QBrush(buttonColor));
        painter.setClipRect(QRect(width() - buttonFieldWidth, 0, buttonFieldWidth, buttonFieldWidth));
        painter.drawRoundRect(geometry(), r * ratio, r);
    }
}

QString FileTransferWidget::getHumanReadableSize(qint64 size)
{
    static const char* suffix[] = {"B", "kiB", "MiB", "GiB", "TiB"};
    int exp = 0;

    if (size > 0) {
        exp = std::min((int)(log(size) / log(1024)), (int)(sizeof(suffix) / sizeof(suffix[0]) - 1));
    }

    return QString().setNum(size / pow(1024, exp), 'f', exp > 1 ? 2 : 0).append(suffix[exp]);
}

void FileTransferWidget::updateWidgetColor(ToxFile const& file)
{
    if (lastStatus == file.status) {
        return;
    }

    switch (file.status) {
    case ToxFile::INITIALIZING:
    case ToxFile::PAUSED:
    case ToxFile::TRANSMITTING:
        setBackgroundColor(Style::getColor(Style::TransferMiddle), false);
        break;
    case ToxFile::BROKEN:
    case ToxFile::CANCELED:
        setBackgroundColor(Style::getColor(Style::TransferBad), true);
        break;
    case ToxFile::FINISHED:
        setBackgroundColor(Style::getColor(Style::TransferGood), true);
        break;
    default:
        qCritical() << "Invalid file status";
        assert(false);
    }
}

void FileTransferWidget::updateWidgetText(ToxFile const& file)
{
    if (lastStatus == file.status && file.status != ToxFile::PAUSED) {
        return;
    }

    switch (file.status) {
    case ToxFile::INITIALIZING:
        if (file.direction == ToxFile::SENDING) {
            ui->progressLabel->setText(tr("Waiting to send...", "file transfer widget"));
        } else {
            ui->progressLabel->setText(tr("Accept to receive this file", "file transfer widget"));
        }
        break;
    case ToxFile::PAUSED:
        ui->etaLabel->setText("");
        if (file.pauseStatus.localPaused()) {
            ui->progressLabel->setText(tr("Paused", "file transfer widget"));
        } else {
            ui->progressLabel->setText(tr("Remote Paused", "file transfer widget"));
        }
        break;
    case ToxFile::TRANSMITTING:
        ui->etaLabel->setText("");
        ui->progressLabel->setText(tr("Resuming...", "file transfer widget"));
        break;
    case ToxFile::BROKEN:
    case ToxFile::CANCELED:
        break;
    case ToxFile::FINISHED:
        break;
    default:
        qCritical() << "Invalid file status";
        assert(false);
    }
}

void FileTransferWidget::updatePreview(ToxFile const& file)
{
    if (lastStatus == file.status) {
        return;
    }

    switch (file.status) {
    case ToxFile::INITIALIZING:
    case ToxFile::PAUSED:
    case ToxFile::TRANSMITTING:
    case ToxFile::BROKEN:
    case ToxFile::CANCELED:
        if (file.direction == ToxFile::SENDING) {
            showPreview(file.filePath);
        }
        break;
    case ToxFile::FINISHED:
        showPreview(file.filePath);
        break;
    default:
        qCritical() << "Invalid file status";
        assert(false);
    }
}

void FileTransferWidget::updateFileProgress(ToxFile const& file)
{
    switch (file.status) {
    case ToxFile::INITIALIZING:
        break;
    case ToxFile::PAUSED:
        fileProgress.resetSpeed();
        break;
    case ToxFile::TRANSMITTING: {
        if (!fileProgress.needsUpdate()) {
            break;
        }

        fileProgress.addSample(file);
        auto speed = fileProgress.getSpeed();
        auto progress = fileProgress.getProgress();
        auto remainingTime = fileProgress.getTimeLeftSeconds();

        ui->progressBar->setValue(static_cast<int>(progress * 100.0));

        // update UI
        if (speed > 0) {
            // ETA
            QTime toGo = QTime(0, 0).addSecs(remainingTime);
            QString format = toGo.hour() > 0 ? "hh:mm:ss" : "mm:ss";
            ui->etaLabel->setText(toGo.toString(format));
        } else {
            ui->etaLabel->setText("");
        }

        ui->progressLabel->setText(getHumanReadableSize(speed) + "/s");
        break;
    }
    case ToxFile::BROKEN:
    case ToxFile::CANCELED:
    case ToxFile::FINISHED: {
        ui->progressBar->hide();
        ui->progressLabel->hide();
        ui->etaLabel->hide();
        break;
    }
    default:
        qCritical() << "Invalid file status";
        assert(false);
    }
}

void FileTransferWidget::updateSignals(ToxFile const& file)
{
    if (lastStatus == file.status) {
        return;
    }

    switch (file.status) {
    case ToxFile::CANCELED:
    case ToxFile::BROKEN:
    case ToxFile::FINISHED:
        active = false;
        disconnect(Core::getInstance()->getCoreFile(), nullptr, this, nullptr);
        break;
    case ToxFile::INITIALIZING:
    case ToxFile::PAUSED:
    case ToxFile::TRANSMITTING:
        break;
    default:
        qCritical() << "Invalid file status";
        assert(false);
    }
}

void FileTransferWidget::setupButtons(ToxFile const& file)
{
    if (lastStatus == file.status && file.status != ToxFile::PAUSED) {
        return;
    }

    switch (file.status) {
    case ToxFile::TRANSMITTING:
        ui->leftButton->setIcon(QIcon(Style::getImagePath("fileTransferInstance/pause.svg")));
        ui->leftButton->setObjectName("pause");
        ui->leftButton->setToolTip(tr("Pause transfer"));

        ui->rightButton->setIcon(QIcon(Style::getImagePath("fileTransferInstance/no.svg")));
        ui->rightButton->setObjectName("cancel");
        ui->rightButton->setToolTip(tr("Cancel transfer"));

        setButtonColor(Style::getColor(Style::TransferGood));
        break;

    case ToxFile::PAUSED:
        if (file.pauseStatus.localPaused()) {
            ui->leftButton->setIcon(QIcon(Style::getImagePath("fileTransferInstance/arrow_white.svg")));
            ui->leftButton->setObjectName("resume");
            ui->leftButton->setToolTip(tr("Resume transfer"));
        } else {
            ui->leftButton->setIcon(QIcon(Style::getImagePath("fileTransferInstance/pause.svg")));
            ui->leftButton->setObjectName("pause");
            ui->leftButton->setToolTip(tr("Pause transfer"));
        }

        ui->rightButton->setIcon(QIcon(Style::getImagePath("fileTransferInstance/no.svg")));
        ui->rightButton->setObjectName("cancel");
        ui->rightButton->setToolTip(tr("Cancel transfer"));

        setButtonColor(Style::getColor(Style::TransferMiddle));
        break;

    case ToxFile::INITIALIZING:
        ui->rightButton->setIcon(QIcon(Style::getImagePath("fileTransferInstance/no.svg")));
        ui->rightButton->setObjectName("cancel");
        ui->rightButton->setToolTip(tr("Cancel transfer"));

        if (file.direction == ToxFile::SENDING) {
            ui->leftButton->setIcon(QIcon(Style::getImagePath("fileTransferInstance/pause.svg")));
            ui->leftButton->setObjectName("pause");
            ui->leftButton->setToolTip(tr("Pause transfer"));
        } else {
            ui->leftButton->setIcon(QIcon(Style::getImagePath("fileTransferInstance/yes.svg")));
            ui->leftButton->setObjectName("accept");
            ui->leftButton->setToolTip(tr("Accept transfer"));
        }
        break;
    case ToxFile::CANCELED:
    case ToxFile::BROKEN:
        ui->leftButton->hide();
        ui->rightButton->hide();
        break;
    case ToxFile::FINISHED:
        ui->leftButton->setIcon(QIcon(Style::getImagePath("fileTransferInstance/yes.svg")));
        ui->leftButton->setObjectName("ok");
        ui->leftButton->setToolTip(tr("Open file"));
        ui->leftButton->show();

        ui->rightButton->setIcon(QIcon(Style::getImagePath("fileTransferInstance/dir.svg")));
        ui->rightButton->setObjectName("dir");
        ui->rightButton->setToolTip(tr("Open file directory"));
        ui->rightButton->show();

        break;
    default:
        qCritical() << "Invalid file status";
        assert(false);
    }
}

void FileTransferWidget::handleButton(QPushButton* btn)
{
    CoreFile* coreFile = Core::getInstance()->getCoreFile();
    if (fileInfo.direction == ToxFile::SENDING) {
        if (btn->objectName() == "cancel") {
            coreFile->cancelFileSend(fileInfo.friendId, fileInfo.fileNum);
        } else if (btn->objectName() == "pause") {
            coreFile->pauseResumeFile(fileInfo.friendId, fileInfo.fileNum);
        } else if (btn->objectName() == "resume") {
            coreFile->pauseResumeFile(fileInfo.friendId, fileInfo.fileNum);
        }
    } else // receiving or paused
    {
        if (btn->objectName() == "cancel") {
            coreFile->cancelFileRecv(fileInfo.friendId, fileInfo.fileNum);
        } else if (btn->objectName() == "pause") {
            coreFile->pauseResumeFile(fileInfo.friendId, fileInfo.fileNum);
        } else if (btn->objectName() == "resume") {
            coreFile->pauseResumeFile(fileInfo.friendId, fileInfo.fileNum);
        } else if (btn->objectName() == "accept") {
            QString path =
                QFileDialog::getSaveFileName(Q_NULLPTR,
                                             tr("Save a file", "Title of the file saving dialog"),
                                             Settings::getInstance().getGlobalAutoAcceptDir() + "/"
                                                 + fileInfo.fileName);
            acceptTransfer(path);
        }
    }

    if (btn->objectName() == "ok" || btn->objectName() == "previewButton") {
        Widget::confirmExecutableOpen(QFileInfo(fileInfo.filePath));
    } else if (btn->objectName() == "dir") {
        QString dirPath = QFileInfo(fileInfo.filePath).dir().path();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
    }
}

void FileTransferWidget::showPreview(const QString& filename)
{
    static const QStringList previewExtensions = {"png", "jpeg", "jpg", "gif", "svg",
                                                  "PNG", "JPEG", "JPG", "GIF", "SVG"};

    if (previewExtensions.contains(QFileInfo(filename).suffix())) {
        // Subtract to make border visible
        const int size = qMax(ui->previewButton->width(), ui->previewButton->height()) - 4;

        QFile imageFile(filename);
        if (!imageFile.open(QIODevice::ReadOnly)) {
            return;
        }
        const QByteArray imageFileData = imageFile.readAll();
        QImage image = QImage::fromData(imageFileData);
        const int exifOrientation =
            getExifOrientation(imageFileData.constData(), imageFileData.size());
        if (exifOrientation) {
            applyTransformation(exifOrientation, image);
        }

        const QPixmap iconPixmap = scaleCropIntoSquare(QPixmap::fromImage(image), size);

        ui->previewButton->setIcon(QIcon(iconPixmap));
        ui->previewButton->setIconSize(iconPixmap.size());
        ui->previewButton->show();
        // Show mouseover preview, but make sure it's not larger than 50% of the screen
        // width/height
        const QRect desktopSize = QApplication::desktop()->screenGeometry();
        const int maxPreviewWidth{desktopSize.width() / 2};
        const int maxPreviewHeight{desktopSize.height() / 2};
        const QImage previewImage = [&image, maxPreviewWidth, maxPreviewHeight]() {
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
        ui->previewButton->setToolTip("<img src=data:image/png;base64," + imageData.toBase64() + "/>");
    }
}

void FileTransferWidget::onLeftButtonClicked()
{
    handleButton(ui->leftButton);
}

void FileTransferWidget::onRightButtonClicked()
{
    handleButton(ui->rightButton);
}

void FileTransferWidget::onPreviewButtonClicked()
{
    handleButton(ui->previewButton);
}

QPixmap FileTransferWidget::scaleCropIntoSquare(const QPixmap& source, const int targetSize)
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

int FileTransferWidget::getExifOrientation(const char* data, const int size)
{
    ExifData* exifData = exif_data_new_from_data(reinterpret_cast<const unsigned char*>(data), size);

    if (!exifData) {
        return 0;
    }

    int orientation = 0;
    const ExifByteOrder byteOrder = exif_data_get_byte_order(exifData);
    const ExifEntry* const exifEntry = exif_data_get_entry(exifData, EXIF_TAG_ORIENTATION);
    if (exifEntry) {
        orientation = exif_get_short(exifEntry->data, byteOrder);
    }
    exif_data_free(exifData);
    return orientation;
}

void FileTransferWidget::applyTransformation(const int orientation, QImage& image)
{
    QTransform exifTransform;
    switch (static_cast<ExifOrientation>(orientation)) {
    case ExifOrientation::TopLeft:
        break;
    case ExifOrientation::TopRight:
        image = image.mirrored(1, 0);
        break;
    case ExifOrientation::BottomRight:
        exifTransform.rotate(180);
        break;
    case ExifOrientation::BottomLeft:
        image = image.mirrored(0, 1);
        break;
    case ExifOrientation::LeftTop:
        exifTransform.rotate(90);
        image = image.mirrored(0, 1);
        break;
    case ExifOrientation::RightTop:
        exifTransform.rotate(-90);
        break;
    case ExifOrientation::RightBottom:
        exifTransform.rotate(-90);
        image = image.mirrored(0, 1);
        break;
    case ExifOrientation::LeftBottom:
        exifTransform.rotate(90);
        break;
    default:
        qWarning() << "Invalid exif orientation passed to applyTransformation!";
    }
    image = image.transformed(exifTransform);
}

void FileTransferWidget::updateWidget(ToxFile const& file)
{
    assert(file == fileInfo);

    fileInfo = file;

    // If we repainted on every packet our gui would be *very* slow
    bool bTransmitNeedsUpdate = fileProgress.needsUpdate();

    updatePreview(file);
    updateFileProgress(file);
    updateWidgetText(file);
    updateWidgetColor(file);
    setupButtons(file);
    updateSignals(file);

    lastStatus = file.status;

    // trigger repaint
    switch (file.status) {
    case ToxFile::TRANSMITTING:
        if (!bTransmitNeedsUpdate) {
            break;
        }
    // fallthrough
    default:
        update();
    }
}
