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

#include "chatform.h"
#include "src/chatlog/chatlinecontentproxy.h"
#include "src/chatlog/chatlog.h"
#include "src/chatlog/chatmessage.h"
#include "src/chatlog/content/filetransferwidget.h"
#include "src/chatlog/content/text.h"
#include "src/core/core.h"
#include "src/core/coreav.h"
#include "src/core/corefile.h"
#include "src/model/friend.h"
#include "src/model/status.h"
#include "src/nexus.h"
#include "src/persistence/history.h"
#include "src/persistence/offlinemsgengine.h"
#include "src/persistence/profile.h"
#include "src/persistence/settings.h"
#include "src/video/netcamview.h"
#include "src/widget/chatformheader.h"
#include "src/widget/form/loadhistorydialog.h"
#include "src/widget/maskablepixmapwidget.h"
#include "src/widget/searchform.h"
#include "src/widget/style.h"
#include "src/widget/tool/callconfirmwidget.h"
#include "src/widget/tool/chattextedit.h"
#include "src/widget/tool/croppinglabel.h"
#include "src/widget/tool/screenshotgrabber.h"
#include "src/widget/translator.h"
#include "src/widget/widget.h"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStringBuilder>

#include <cassert>

/**
 * @brief ChatForm::incomingNotification Notify that we are called by someone.
 * @param friendId Friend that is calling us.
 *
 * @brief ChatForm::outgoingNotification Notify that we are calling someone.
 *
 * @brief stopNotification Tell others to stop notification of a call.
 */

static constexpr int CHAT_WIDGET_MIN_HEIGHT = 50;
static constexpr int SCREENSHOT_GRABBER_OPENING_DELAY = 500;
static constexpr int TYPING_NOTIFICATION_DURATION = 3000;

const QString ChatForm::ACTION_PREFIX = QStringLiteral("/me ");

namespace
{
    QString secondsToDHMS(quint32 duration)
    {
        QString res;
        QString cD = ChatForm::tr("Call duration: ");
        quint32 seconds = duration % 60;
        duration /= 60;
        quint32 minutes = duration % 60;
        duration /= 60;
        quint32 hours = duration % 24;
        quint32 days = duration / 24;

        // I assume no one will ever have call longer than a month
        if (days) {
            return cD + res.sprintf("%dd%02dh %02dm %02ds", days, hours, minutes, seconds);
        }

        if (hours) {
            return cD + res.sprintf("%02dh %02dm %02ds", hours, minutes, seconds);
        }

        if (minutes) {
            return cD + res.sprintf("%02dm %02ds", minutes, seconds);
        }

        return cD + res.sprintf("%02ds", seconds);
    }
} // namespace

ChatForm::ChatForm(Friend* chatFriend, IChatLog& chatLog, IMessageDispatcher& messageDispatcher)
    : GenericChatForm(chatFriend, chatLog, messageDispatcher)
    , f(chatFriend)
    , isTyping{false}
    , lastCallIsVideo{false}
{
    setName(f->getDisplayedName());

    headWidget->setAvatar(QPixmap(":/img/contact_dark.svg"));

    statusMessageLabel = new CroppingLabel();
    statusMessageLabel->setObjectName("statusLabel");
    statusMessageLabel->setFont(Style::getFont(Style::Medium));
    statusMessageLabel->setMinimumHeight(Style::getFont(Style::Medium).pixelSize());
    statusMessageLabel->setTextFormat(Qt::PlainText);
    statusMessageLabel->setContextMenuPolicy(Qt::CustomContextMenu);

    typingTimer.setSingleShot(true);

    callDurationTimer = nullptr;

    chatWidget->setTypingNotification(ChatMessage::createTypingNotification());
    chatWidget->setMinimumHeight(CHAT_WIDGET_MIN_HEIGHT);

    callDuration = new QLabel();
    headWidget->addWidget(statusMessageLabel);
    headWidget->addStretch();
    headWidget->addWidget(callDuration, 1, Qt::AlignCenter);
    callDuration->hide();

    copyStatusAction = statusMessageMenu.addAction(QString(), this, SLOT(onCopyStatusMessage()));

    const Core* core = Core::getInstance();
    const Profile* profile = Nexus::getProfile();
    const CoreFile* coreFile = core->getCoreFile();
    connect(profile, &Profile::friendAvatarChanged, this, &ChatForm::onAvatarChanged);
    connect(coreFile, &CoreFile::fileReceiveRequested, this, &ChatForm::updateFriendActivityForFile);
    connect(coreFile, &CoreFile::fileSendStarted, this, &ChatForm::updateFriendActivityForFile);
    connect(core, &Core::friendTypingChanged, this, &ChatForm::onFriendTypingChanged);
    connect(core, &Core::friendStatusChanged, this, &ChatForm::onFriendStatusChanged);
    connect(coreFile, &CoreFile::fileNameChanged, this, &ChatForm::onFileNameChanged);

    const CoreAV* av = core->getAv();
    connect(av, &CoreAV::avInvite, this, &ChatForm::onAvInvite);
    connect(av, &CoreAV::avStart, this, &ChatForm::onAvStart);
    connect(av, &CoreAV::avEnd, this, &ChatForm::onAvEnd);

    connect(headWidget, &ChatFormHeader::callTriggered, this, &ChatForm::onCallTriggered);
    connect(headWidget, &ChatFormHeader::videoCallTriggered, this, &ChatForm::onVideoCallTriggered);
    connect(headWidget, &ChatFormHeader::micMuteToggle, this, &ChatForm::onMicMuteToggle);
    connect(headWidget, &ChatFormHeader::volMuteToggle, this, &ChatForm::onVolMuteToggle);

    connect(sendButton, &QPushButton::pressed, this, &ChatForm::updateFriendActivity);
    connect(msgEdit, &ChatTextEdit::enterPressed, this, &ChatForm::updateFriendActivity);
    connect(msgEdit, &ChatTextEdit::textChanged, this, &ChatForm::onTextEditChanged);
    connect(msgEdit, &ChatTextEdit::pasteImage, this, &ChatForm::sendImage);
    connect(statusMessageLabel, &CroppingLabel::customContextMenuRequested, this,
            [&](const QPoint& pos) {
                if (!statusMessageLabel->text().isEmpty()) {
                    QWidget* sender = static_cast<QWidget*>(this->sender());
                    statusMessageMenu.exec(sender->mapToGlobal(pos));
                }
            });

    connect(&typingTimer, &QTimer::timeout, this, [=] {
        Core::getInstance()->sendTyping(f->getId(), false);
        isTyping = false;
    });

    // reflect name changes in the header
    connect(headWidget, &ChatFormHeader::nameChanged, this,
            [=](const QString& newName) { f->setAlias(newName); });
    connect(headWidget, &ChatFormHeader::callAccepted, this,
            [this] { onAnswerCallTriggered(lastCallIsVideo); });
    connect(headWidget, &ChatFormHeader::callRejected, this, &ChatForm::onRejectCallTriggered);

    updateCallButtons();

    setAcceptDrops(true);
    retranslateUi();
    Translator::registerHandler(std::bind(&ChatForm::retranslateUi, this), this);
}

ChatForm::~ChatForm()
{
    Translator::unregister(this);
    delete netcam;
    netcam = nullptr;
}

void ChatForm::setStatusMessage(const QString& newMessage)
{
    statusMessageLabel->setText(newMessage);
    // for long messsages
    statusMessageLabel->setToolTip(Qt::convertFromPlainText(newMessage, Qt::WhiteSpaceNormal));
}

void ChatForm::updateFriendActivity()
{
    Widget::getInstance()->updateFriendActivity(f);
}

void ChatForm::updateFriendActivityForFile(const ToxFile& file)
{
    if (file.friendId != f->getId()) {
        return;
    }

    Widget::getInstance()->updateFriendActivity(f);
}

void ChatForm::onFileNameChanged(const ToxPk& friendPk)
{
    if (friendPk != f->getPublicKey()) {
        return;
    }

    QMessageBox::warning(this, tr("Filename contained illegal characters"),
                         tr("Illegal characters have been changed to _ \n"
                            "so you can save the file on windows."));
}

void ChatForm::onTextEditChanged()
{
    if (!Settings::getInstance().getTypingNotification()) {
        if (isTyping) {
            isTyping = false;
            Core::getInstance()->sendTyping(f->getId(), false);
        }

        return;
    }
    bool isTypingNow = !msgEdit->toPlainText().isEmpty();
    if (isTyping != isTypingNow) {
        Core::getInstance()->sendTyping(f->getId(), isTypingNow);
        if (isTypingNow) {
            typingTimer.start(TYPING_NOTIFICATION_DURATION);
        }

        isTyping = isTypingNow;
    }
}

void ChatForm::onAttachClicked()
{
    QStringList paths = QFileDialog::getOpenFileNames(Q_NULLPTR, tr("Send a file"),
                                                      QDir::homePath(), nullptr, nullptr);

    if (paths.isEmpty()) {
        return;
    }

    Core* core = Core::getInstance();
    for (QString path : paths) {
        QFile file(path);
        QString fileName = QFileInfo(path).fileName();
        if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Unable to open"),
                                 tr("qTox wasn't able to open %1").arg(fileName));
            continue;
        }

        file.close();
        if (file.isSequential()) {
            QMessageBox::critical(this, tr("Bad idea"),
                                  tr("You're trying to send a sequential file, "
                                     "which is not going to work!"));
            continue;
        }

        qint64 filesize = file.size();
        core->getCoreFile()->sendFile(f->getId(), fileName, path, filesize);
    }
}

void ChatForm::onAvInvite(uint32_t friendId, bool video)
{
    if (friendId != f->getId()) {
        return;
    }

    QString displayedName = f->getDisplayedName();
    insertChatMessage(ChatMessage::createChatInfoMessage(tr("%1 calling").arg(displayedName),
                                                         ChatMessage::INFO,
                                                         QDateTime::currentDateTime()));

    auto testedFlag = video ? Settings::AutoAcceptCall::Video : Settings::AutoAcceptCall::Audio;
    // AutoAcceptCall is set for this friend
    if (Settings::getInstance().getAutoAcceptCall(f->getPublicKey()).testFlag(testedFlag)) {
        uint32_t friendId = f->getId();
        qDebug() << "automatic call answer";
        CoreAV* coreav = Core::getInstance()->getAv();
        QMetaObject::invokeMethod(coreav, "answerCall", Qt::QueuedConnection,
                                  Q_ARG(uint32_t, friendId), Q_ARG(bool, video));
        onAvStart(friendId, video);
    } else {
        headWidget->createCallConfirm(video);
        headWidget->showCallConfirm();
        lastCallIsVideo = video;
        emit incomingNotification(friendId);
    }
}

void ChatForm::onAvStart(uint32_t friendId, bool video)
{
    if (friendId != f->getId()) {
        return;
    }

    if (video) {
        showNetcam();
    } else {
        hideNetcam();
    }

    emit stopNotification();
    updateCallButtons();
    startCounter();
}

void ChatForm::onAvEnd(uint32_t friendId, bool error)
{
    if (friendId != f->getId()) {
        return;
    }

    headWidget->removeCallConfirm();
    // Fixes an OS X bug with ending a call while in full screen
    if (netcam && netcam->isFullScreen()) {
        netcam->showNormal();
    }

    emit stopNotification();
    emit endCallNotification();
    updateCallButtons();
    stopCounter(error);
    hideNetcam();
}

void ChatForm::showOutgoingCall(bool video)
{
    headWidget->showOutgoingCall(video);
    addSystemInfoMessage(tr("Calling %1").arg(f->getDisplayedName()), ChatMessage::INFO,
                         QDateTime::currentDateTime());
    emit outgoingNotification();
    Widget::getInstance()->updateFriendActivity(f);
}

void ChatForm::onAnswerCallTriggered(bool video)
{
    headWidget->removeCallConfirm();
    uint32_t friendId = f->getId();
    emit stopNotification();
    emit acceptCall(friendId);

    updateCallButtons();
    CoreAV* av = Core::getInstance()->getAv();
    if (!av->answerCall(friendId, video)) {
        updateCallButtons();
        stopCounter();
        hideNetcam();
        return;
    }

    onAvStart(friendId, av->isCallVideoEnabled(f));
}

void ChatForm::onRejectCallTriggered()
{
    headWidget->removeCallConfirm();
    emit rejectCall(f->getId());
}

void ChatForm::onCallTriggered()
{
    CoreAV* av = Core::getInstance()->getAv();
    uint32_t friendId = f->getId();
    if (av->isCallStarted(f)) {
        av->cancelCall(friendId);
    } else if (av->startCall(friendId, false)) {
        showOutgoingCall(false);
    }
}

void ChatForm::onVideoCallTriggered()
{
    CoreAV* av = Core::getInstance()->getAv();
    uint32_t friendId = f->getId();
    if (av->isCallStarted(f)) {
        // TODO: We want to activate video on the active call.
        if (av->isCallVideoEnabled(f)) {
            av->cancelCall(friendId);
        }
    } else if (av->startCall(friendId, true)) {
        showOutgoingCall(true);
    }
}

void ChatForm::updateCallButtons()
{
    CoreAV* av = Core::getInstance()->getAv();
    const bool audio = av->isCallActive(f);
    const bool video = av->isCallVideoEnabled(f);
    const bool online = f->isOnline();
    headWidget->updateCallButtons(online, audio, video);
    updateMuteMicButton();
    updateMuteVolButton();
}

void ChatForm::onMicMuteToggle()
{
    CoreAV* av = Core::getInstance()->getAv();
    av->toggleMuteCallInput(f);
    updateMuteMicButton();
}

void ChatForm::onVolMuteToggle()
{
    CoreAV* av = Core::getInstance()->getAv();
    av->toggleMuteCallOutput(f);
    updateMuteVolButton();
}

void ChatForm::onFriendStatusChanged(uint32_t friendId, Status::Status status)
{
    // Disable call buttons if friend is offline
    if (friendId != f->getId()) {
        return;
    }

    if (!f->isOnline()) {
        // Hide the "is typing" message when a friend goes offline
        setFriendTyping(false);
    }

    updateCallButtons();

    if (Settings::getInstance().getStatusChangeNotificationEnabled()) {
        QString fStatus = Status::getTitle(status);
        addSystemInfoMessage(tr("%1 is now %2", "e.g. \"Dubslow is now online\"")
                                 .arg(f->getDisplayedName())
                                 .arg(fStatus),
                             ChatMessage::INFO, QDateTime::currentDateTime());
    }
}

void ChatForm::onFriendTypingChanged(quint32 friendId, bool isTyping)
{
    if (friendId == f->getId()) {
        setFriendTyping(isTyping);
    }
}

void ChatForm::onFriendNameChanged(const QString& name)
{
    if (sender() == f) {
        setName(name);
    }
}

void ChatForm::onStatusMessage(const QString& message)
{
    if (sender() == f) {
        setStatusMessage(message);
    }
}

void ChatForm::onAvatarChanged(const ToxPk& friendPk, const QPixmap& pic)
{
    if (friendPk != f->getPublicKey()) {
        return;
    }

    headWidget->setAvatar(pic);
}

GenericNetCamView* ChatForm::createNetcam()
{
    qDebug() << "creating netcam";
    uint32_t friendId = f->getId();
    NetCamView* view = new NetCamView(f->getPublicKey(), this);
    CoreAV* av = Core::getInstance()->getAv();
    VideoSource* source = av->getVideoSourceFromCall(friendId);
    view->show(source, f->getDisplayedName());
    connect(view, &GenericNetCamView::videoCallEnd, this, &ChatForm::onVideoCallTriggered);
    connect(view, &GenericNetCamView::volMuteToggle, this, &ChatForm::onVolMuteToggle);
    connect(view, &GenericNetCamView::micMuteToggle, this, &ChatForm::onMicMuteToggle);
    connect(view, &GenericNetCamView::videoPreviewToggle, view, &NetCamView::toggleVideoPreview);
    return view;
}

void ChatForm::dragEnterEvent(QDragEnterEvent* ev)
{
    if (ev->mimeData()->hasUrls()) {
        ev->acceptProposedAction();
    }
}

void ChatForm::dropEvent(QDropEvent* ev)
{
    if (!ev->mimeData()->hasUrls()) {
        return;
    }

    Core* core = Core::getInstance();
    for (const QUrl& url : ev->mimeData()->urls()) {
        QFileInfo info(url.path());
        QFile file(info.absoluteFilePath());

        QString urlString = url.toString();
        if (url.isValid() && !url.isLocalFile()
            && urlString.length() < static_cast<int>(tox_max_message_length())) {
            messageDispatcher.sendMessage(false, urlString);

            continue;
        }

        QString fileName = info.fileName();
        if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
            info.setFile(url.toLocalFile());
            file.setFileName(info.absoluteFilePath());
            if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
                QMessageBox::warning(this, tr("Unable to open"),
                                     tr("qTox wasn't able to open %1").arg(fileName));
                continue;
            }
        }

        file.close();
        if (file.isSequential()) {
            QMessageBox::critical(nullptr, tr("Bad idea"),
                                  tr("You're trying to send a sequential file, "
                                     "which is not going to work!"));
            continue;
        }

        if (info.exists()) {
            core->getCoreFile()->sendFile(f->getId(), fileName, info.absoluteFilePath(), info.size());
        }
    }
}

void ChatForm::clearChatArea()
{
    GenericChatForm::clearChatArea(/* confirm = */ false, /* inform = */ true);
}

void ChatForm::onScreenshotClicked()
{
    doScreenshot();
    // Give the window manager a moment to open the fullscreen grabber window
    QTimer::singleShot(SCREENSHOT_GRABBER_OPENING_DELAY, this, SLOT(hideFileMenu()));
}

void ChatForm::doScreenshot()
{
    // note: grabber is self-managed and will destroy itself when done
    ScreenshotGrabber* grabber = new ScreenshotGrabber;
    connect(grabber, &ScreenshotGrabber::screenshotTaken, this, &ChatForm::sendImage);
    grabber->showGrabber();
}

void ChatForm::sendImage(const QPixmap& pixmap)
{
    QDir(Settings::getInstance().getAppDataDirPath()).mkpath("images");

    // use ~ISO 8601 for screenshot timestamp, considering FS limitations
    // https://en.wikipedia.org/wiki/ISO_8601
    // Windows has to be supported, thus filename can't have `:` in it :/
    // Format should be: `qTox_Screenshot_yyyy-MM-dd HH-mm-ss.zzz.png`
    QString filepath = QString("%1images%2qTox_Image_%3.png")
                           .arg(Settings::getInstance().getAppDataDirPath())
                           .arg(QDir::separator())
                           .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss.zzz"));
    QFile file(filepath);

    if (file.open(QFile::ReadWrite)) {
        pixmap.save(&file, "PNG");
        qint64 filesize = file.size();
        file.close();
        QFileInfo fi(file);
        CoreFile* coreFile = Core::getInstance()->getCoreFile();
        coreFile->sendFile(f->getId(), fi.fileName(), fi.filePath(), filesize);
    } else {
        QMessageBox::warning(this,
                             tr("Failed to open temporary file", "Temporary file for screenshot"),
                             tr("qTox wasn't able to save the screenshot"));
    }
}

void ChatForm::insertChatMessage(ChatMessage::Ptr msg)
{
    GenericChatForm::insertChatMessage(msg);
    if (netcam && bodySplitter->sizes()[1] == 0) {
        netcam->setShowMessages(true, true);
    }
}

void ChatForm::onCopyStatusMessage()
{
    // make sure to copy not truncated text directly from the friend
    QString text = f->getStatusMessage();
    QClipboard* clipboard = QApplication::clipboard();
    if (clipboard) {
        clipboard->setText(text, QClipboard::Clipboard);
    }
}

void ChatForm::updateMuteMicButton()
{
    const CoreAV* av = Core::getInstance()->getAv();
    bool active = av->isCallActive(f);
    bool inputMuted = av->isCallInputMuted(f);
    headWidget->updateMuteMicButton(active, inputMuted);
    if (netcam) {
        netcam->updateMuteMicButton(inputMuted);
    }
}

void ChatForm::updateMuteVolButton()
{
    const CoreAV* av = Core::getInstance()->getAv();
    bool active = av->isCallActive(f);
    bool outputMuted = av->isCallOutputMuted(f);
    headWidget->updateMuteVolButton(active, outputMuted);
    if (netcam) {
        netcam->updateMuteVolButton(outputMuted);
    }
}

void ChatForm::startCounter()
{
    if (callDurationTimer) {
        return;
    }
    callDurationTimer = new QTimer();
    connect(callDurationTimer, &QTimer::timeout, this, &ChatForm::onUpdateTime);
    callDurationTimer->start(1000);
    timeElapsed.start();
    callDuration->show();
}

void ChatForm::stopCounter(bool error)
{
    if (!callDurationTimer) {
        return;
    }
    QString dhms = secondsToDHMS(timeElapsed.elapsed() / 1000);
    QString name = f->getDisplayedName();
    QString mess = error ? tr("Call with %1 ended unexpectedly. %2") : tr("Call with %1 ended. %2");
    // TODO: add notification once notifications are implemented

    addSystemInfoMessage(mess.arg(name, dhms), ChatMessage::INFO, QDateTime::currentDateTime());
    callDurationTimer->stop();
    callDuration->setText("");
    callDuration->hide();

    delete callDurationTimer;
    callDurationTimer = nullptr;
}

void ChatForm::onUpdateTime()
{
    callDuration->setText(secondsToDHMS(timeElapsed.elapsed() / 1000));
}

void ChatForm::setFriendTyping(bool isTyping)
{
    chatWidget->setTypingNotificationVisible(isTyping);
    Text* text = static_cast<Text*>(chatWidget->getTypingNotification()->getContent(1));
    QString typingDiv = "<div class=typing>%1</div>";
    QString name = f->getDisplayedName();
    text->setText(typingDiv.arg(tr("%1 is typing").arg(name)));
}

void ChatForm::show(ContentLayout* contentLayout)
{
    GenericChatForm::show(contentLayout);
}

void ChatForm::reloadTheme()
{
    chatWidget->setTypingNotification(ChatMessage::createTypingNotification());
    GenericChatForm::reloadTheme();
}

void ChatForm::showEvent(QShowEvent* event)
{
    GenericChatForm::showEvent(event);
}

void ChatForm::hideEvent(QHideEvent* event)
{
    GenericChatForm::hideEvent(event);
}

void ChatForm::retranslateUi()
{
    copyStatusAction->setText(tr("Copy"));

    updateMuteMicButton();
    updateMuteVolButton();

    if (netcam) {
        netcam->setShowMessages(chatWidget->isVisible());
    }
}
