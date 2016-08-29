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

#include "chatform.h"

#include <QApplication>
#include <QBitmap>
#include <QBoxLayout>
#include <QClipboard>
#include <QDebug>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSplitter>
#include <QStyle>
#include <QTemporaryFile>

#include <cassert>
#include "chatform.h"

#include"audio/audio.h"
#include"chatlog/chatlinecontentproxy.h"
#include"chatlog/chatlog.h"
#include"chatlog/chatmessage.h"
#include"chatlog/content/filetransferwidget.h"
#include"chatlog/content/text.h"
#include"core/core.h"
#include"core/coreav.h"
#include"core/cstring.h"
#include"friend.h"
#include"nexus.h"
#include"nexus.h"
#include"persistence/offlinemsgengine.h"
#include"persistence/profile.h"
#include"persistence/settings.h"
#include"video/camerasource.h"
#include"video/netcamview.h"
#include"video/videosource.h"
#include"widget/form/loadhistorydialog.h"
#include"widget/friendwidget.h"
#include"widget/maskablepixmapwidget.h"
#include"widget/style.h"
#include"widget/tool/callconfirmwidget.h"
#include"widget/tool/chattextedit.h"
#include"widget/tool/croppinglabel.h"
#include"widget/tool/flyoutoverlaywidget.h"
#include"widget/tool/screenshotgrabber.h"
#include"widget/translator.h"
#include"widget/widget.h"

const QString ChatForm::ACTION_PREFIX = QStringLiteral("/me ");

ChatForm::ChatForm(Friend* chatFriend)
    : f(chatFriend)
    , callDuration(new QLabel(this))
    , isTyping(false)
{
    nameLabel->setText(f->getDisplayedName());

    avatar->setPixmap(QPixmap(":/img/contact_dark.svg"));

    statusMessageLabel = new CroppingLabel();
    statusMessageLabel->setObjectName("statusLabel");
    statusMessageLabel->setFont(Style::getFont(Style::Medium));
    statusMessageLabel->setMinimumHeight(Style::getFont(Style::Medium).pixelSize());
    statusMessageLabel->setTextFormat(Qt::PlainText);
    statusMessageLabel->setContextMenuPolicy(Qt::CustomContextMenu);

    callConfirm = nullptr;
    offlineEngine = new OfflineMsgEngine(f);

    typingTimer.setSingleShot(true);

    callDurationTimer = nullptr;

    chatWidget->setTypingNotification(ChatMessage::createTypingNotification());
    chatWidget->setMinimumHeight(50);

    headTextLayout->addWidget(statusMessageLabel);
    headTextLayout->addStretch();
    callDuration = new QLabel();
    headTextLayout->addWidget(callDuration, 1, Qt::AlignCenter);
    callDuration->hide();

    loadHistoryAction = menu.addAction(QString(), this, SLOT(onLoadHistory()));
    copyStatusAction = statusMessageMenu.addAction(QString(), this, SLOT(onCopyStatusMessage()));

    const Core* core = Core::getInstance();
    connect(core, &Core::fileReceiveRequested,
            this, &ChatForm::onFileRecvRequest);
    connect(core, &Core::friendAvatarChanged,
            this, &ChatForm::onAvatarChange);
    connect(core, &Core::friendAvatarRemoved,
            this, &ChatForm::onAvatarRemoved);
    connect(core, &Core::fileSendStarted,
            this, &ChatForm::startFileSend);
    connect(core, &Core::fileSendFailed,
            this, &ChatForm::onFileSendFailed);
    connect(core, &Core::receiptRecieved,
            this, &ChatForm::onReceiptReceived);
    connect(core, &Core::friendMessageReceived,
            this, &ChatForm::onFriendMessageReceived);
    connect(core, &Core::friendTypingChanged,
            this, &ChatForm::onFriendTypingChanged);

    const CoreAV* av = core->getAv();
    connect(av, &CoreAV::avInvite, this, &ChatForm::onAvInvite);
    connect(av, &CoreAV::avStart, this, &ChatForm::onAvStart);
    connect(av, &CoreAV::avEnd, this, &ChatForm::onAvEnd);

    connect(sendButton, &QPushButton::clicked,
            this, &ChatForm::onSendTriggered);
    connect(fileButton, &QPushButton::clicked,
            this, &ChatForm::onAttachClicked);
    connect(screenshotButton, &QPushButton::clicked,
            this, &ChatForm::onScreenshotClicked);
    connect(callButton, &QAbstractButton::clicked,
            this, &ChatForm::onCallTriggered);
    connect(videoButton, &QAbstractButton::clicked,
            this, &ChatForm::onVideoCallTriggered);
    connect(micButton, &QAbstractButton::clicked,
            this, &ChatForm::onMicMuteToggle);
    connect(volButton, &QAbstractButton::clicked,
            this, &ChatForm::onVolMuteToggle);

    connect(msgEdit, &ChatTextEdit::enterPressed,
            this, &ChatForm::onSendTriggered);
    connect(msgEdit, &ChatTextEdit::textChanged,
            this, &ChatForm::onTextEditChanged);
    connect(statusMessageLabel, &CroppingLabel::customContextMenuRequested,
            this, [&](const QPoint& pos) {
        if (!statusMessageLabel->text().isEmpty())
        {
            QWidget* sender = static_cast<QWidget*>(this->sender());
            statusMessageMenu.exec(sender->mapToGlobal(pos));
        }
    });

    connect(&typingTimer, &QTimer::timeout, this, [=] {
        Core::getInstance()->sendTyping(f->getFriendID(), false);
        isTyping = false;
    });

    connect(nameLabel, &CroppingLabel::editFinished,
            this, [=](const QString& newName) {
        nameLabel->setText(newName);
        emit aliasChanged(newName);
    });

    setAcceptDrops(true);
    retranslateUi();
    Translator::registerHandler(std::bind(&ChatForm::retranslateUi, this), this);
}

ChatForm::~ChatForm()
{
    Translator::unregister(this);
    delete netcam;
}

void ChatForm::setStatusMessage(QString newMessage)
{
    statusMessageLabel->setText(newMessage);
    // for long messsages
    statusMessageLabel->setToolTip(Qt::convertFromPlainText(newMessage, Qt::WhiteSpaceNormal));
}

void ChatForm::onSendTriggered()
{
    SendMessageStr(msgEdit->toPlainText());
    msgEdit->clear();
}

void ChatForm::onTextEditChanged()
{
    if (!Settings::getInstance().getTypingNotification())
    {
        if (isTyping)
            Core::getInstance()->sendTyping(f->getFriendID(), false);

        isTyping = false;
        return;
    }

    if (msgEdit->toPlainText().length() > 0)
    {
        typingTimer.start(3000);
        if (!isTyping)
            Core::getInstance()->sendTyping(f->getFriendID(), (isTyping = true));
    }
    else
    {
        Core::getInstance()->sendTyping(f->getFriendID(), (isTyping = false));
    }
}

void ChatForm::onAttachClicked()
{
    QStringList paths = QFileDialog::getOpenFileNames(this,
                                                      tr("Send a file"),
                                                      QDir::homePath(),
                                                      0,
                                                      0,
                                                      QFileDialog::DontUseNativeDialog);
    if (paths.isEmpty())
        return;

    Core* core = Core::getInstance();
    for (QString path : paths)
    {
        QFile file(path);
        if (!file.exists() || !file.open(QIODevice::ReadOnly))
        {
            QMessageBox::warning(this,
                                 tr("Unable to open"),
                                 tr("qTox wasn't able to open %1").arg(QFileInfo(path).fileName()));
            continue;
        }

        if (file.isSequential())
        {
            QMessageBox::critical(this,
                                  tr("Bad idea"),
                                  tr("You're trying to send a sequential file,"
                                     " which is not going to work!"));
            file.close();
            continue;
        }
        qint64 filesize = file.size();
        file.close();
        QFileInfo fi(path);

        core->sendFile(f->getFriendID(), fi.fileName(), path,
                       filesize);
    }
}

void ChatForm::startFileSend(ToxFile file)
{
    if (file.friendId != f->getFriendID())
        return;

    QString name;
    const Core* core = Core::getInstance();
    ToxId self = core->getSelfId();
    if (previousId != self)
    {
        name = core->getUsername();
        previousId = self;
    }

    insertChatMessage(ChatMessage::createFileTransferMessage(name, file, true, QDateTime::currentDateTime()));

    Widget::getInstance()->updateFriendActivity(f);
}

void ChatForm::onFileRecvRequest(ToxFile file)
{
    if (file.friendId != f->getFriendID())
        return;

    Widget::getInstance()->newFriendMessageAlert(file.friendId);

    QString name;
    ToxId friendId = f->getToxId();
    if (friendId != previousId)
    {
        name = f->getDisplayedName();
        previousId = friendId;
    }

    ChatMessage::Ptr msg = ChatMessage::createFileTransferMessage(name, file, false, QDateTime::currentDateTime());
    insertChatMessage(msg);

    ChatLineContentProxy* proxy = static_cast<ChatLineContentProxy*>(msg->getContent(1));
    assert(proxy->getWidgetType() == ChatLineContentProxy::FileTransferWidgetType);
    FileTransferWidget* tfWidget = static_cast<FileTransferWidget*>(proxy->getWidget());

    // there is auto-accept for that conact
    if (!Settings::getInstance().getAutoAcceptDir(f->getToxId()).isEmpty())
    {
        tfWidget->autoAcceptTransfer(Settings::getInstance().getAutoAcceptDir(f->getToxId()));
    }
    else if (Settings::getInstance().getAutoSaveEnabled())
    {   //global autosave to global directory
        tfWidget->autoAcceptTransfer(Settings::getInstance().getGlobalAutoAcceptDir());
    }

    Widget::getInstance()->updateFriendActivity(f);
}

void ChatForm::onAvInvite(uint32_t friendId, bool video)
{
    if (friendId != f->getFriendID())
        return;

    callConfirm = new CallConfirmWidget(video ? videoButton : callButton, *f);
    insertChatMessage(ChatMessage::createChatInfoMessage(tr("%1 calling").arg(f->getDisplayedName()),
                                                         ChatMessage::INFO,
                                                         QDateTime::currentDateTime()));
    /* AutoAcceptCall is set for this friend */
    if ((video && Settings::getInstance().getAutoAcceptCall(f->getToxId()).testFlag(Settings::AutoAcceptCall::Video)) ||
       (!video && Settings::getInstance().getAutoAcceptCall(f->getToxId()).testFlag(Settings::AutoAcceptCall::Audio)))
    {
        uint32_t friendId = f->getFriendID();
        qDebug() << "automatic call answer";
        CoreAV* coreav = Core::getInstance()->getAv();
        QMetaObject::invokeMethod(coreav, "answerCall", Qt::QueuedConnection, Q_ARG(uint32_t, friendId));
        onAvStart(friendId,video);
    }
    else
    {
        callConfirm->show();

        connect(callConfirm.data(), &CallConfirmWidget::accepted,
                this, &ChatForm::onAnswerCallTriggered);
        connect(callConfirm.data(), &CallConfirmWidget::rejected,
                this, &ChatForm::onRejectCallTriggered);

        insertChatMessage(ChatMessage::createChatInfoMessage(
                              tr("%1 calling").arg(f->getDisplayedName()),
                              ChatMessage::INFO,
                              QDateTime::currentDateTime()));

        Widget::getInstance()->newFriendMessageAlert(friendId, false);
        Audio& audio = Audio::getInstance();
        audio.startLoop();
        audio.playMono16Sound(Audio::getSound(Audio::Sound::IncomingCall));
    }
}

void ChatForm::onAvStart(uint32_t FriendId, bool video)
{
    if (FriendId != f->getFriendID())
        return;

    if (video)
        showNetcam();
    else
        hideNetcam();

    updateCallButtons();
    startCounter();
}

void ChatForm::onAvEnd(uint32_t FriendId)
{
    if (FriendId != f->getFriendID())
        return;

    delete callConfirm;

    //Fixes an OS X bug with ending a call while in full screen
    if (netcam && netcam->isFullScreen())
        netcam->showNormal();

    Audio::getInstance().stopLoop();

    updateCallButtons();
    stopCounter();
    hideNetcam();
}

void ChatForm::showOutgoingCall(bool video)
{
    if (video)
    {
        videoButton->setObjectName("yellow");
        videoButton->setStyleSheet(Style::getStylesheet(QStringLiteral(":/ui/videoButton/videoButton.css")));
        videoButton->setToolTip(tr("Cancel video call"));
    }
    else
    {
        callButton->setObjectName("yellow");
        callButton->setStyleSheet(Style::getStylesheet(QStringLiteral(":/ui/callButton/callButton.css")));
        callButton->setToolTip(tr("Cancel audio call"));
    }

    addSystemInfoMessage(tr("Calling %1").arg(f->getDisplayedName()),
                         ChatMessage::INFO,
                         QDateTime::currentDateTime());

    Widget::getInstance()->updateFriendActivity(f);
}

void ChatForm::onAnswerCallTriggered()
{
    delete callConfirm;

    Audio::getInstance().stopLoop();

    updateCallButtons();

    CoreAV* av = Core::getInstance()->getAv();
    if (!av->answerCall(f->getFriendID()))
    {
        updateCallButtons();
        stopCounter();
        hideNetcam();
        return;
    }

    onAvStart(f->getFriendID(), av->isCallVideoEnabled(f));
}

void ChatForm::onRejectCallTriggered()
{
    delete callConfirm;

    Audio::getInstance().stopLoop();

    CoreAV* av = Core::getInstance()->getAv();
    av->cancelCall(f->getFriendID());
}

void ChatForm::onCallTriggered()
{
    CoreAV* av = Core::getInstance()->getAv();
    if (av->isCallActive(f))
    {
        av->cancelCall(f->getFriendID());
    }
    else if (av->startCall(f->getFriendID(), false))
    {
        showOutgoingCall(false);
    }
}

void ChatForm::onVideoCallTriggered()
{
    CoreAV* av = Core::getInstance()->getAv();
    if (av->isCallActive(f))
    {
        // TODO: We want to activate video on the active call.
        if (av->isCallVideoEnabled(f))
            av->cancelCall(f->getFriendID());
    }
    else if (av->startCall(f->getFriendID(), true))
    {
        showOutgoingCall(true);
    }
}

void ChatForm::updateCallButtons()
{
    CoreAV* av = Core::getInstance()->getAv();
    if (av->isCallActive(f))
    {
        callButton->setObjectName("red");
        callButton->setToolTip(tr("End audio call"));

        if (av->isCallVideoEnabled(f))
        {
            videoButton->setObjectName("red");
            videoButton->setToolTip(tr("End video call"));
        }
    }
    else
    {
        const Status fs = f->getStatus();
        callButton->setEnabled(fs != Status::Offline);
        videoButton->setEnabled(fs != Status::Offline);

        if (callButton->isEnabled())
        {
            callButton->setObjectName("green");
            callButton->setToolTip(tr("Start audio call"));
        }

        if (videoButton->isEnabled())
        {
            videoButton->setObjectName("green");
            videoButton->setToolTip(tr("Start video call"));
        }
    }

    callButton->setStyleSheet(Style::getStylesheet(QStringLiteral(":/ui/callButton/callButton.css")));
    videoButton->setStyleSheet(Style::getStylesheet(QStringLiteral(":/ui/videoButton/videoButton.css")));

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

void ChatForm::onFileSendFailed(uint32_t friendId, const QString &fname)
{
    if (friendId != f->getFriendID())
        return;

    addSystemInfoMessage(tr("Failed to send file \"%1\"").arg(fname), ChatMessage::ERROR, QDateTime::currentDateTime());
}

void ChatForm::onFriendStatusChanged(uint32_t friendId, Status status)
{
    // Disable call buttons if friend is offline
    if(friendId != f->getFriendID())
        return;

    if (status == Status::Offline)
    {
        // Hide the "is typing" message when a friend goes offline
        setFriendTyping(false);
    }
    else
    {
        QTimer::singleShot(250, this, SLOT(onDeliverOfflineMessages()));
    }

    updateCallButtons();

    if (Settings::getInstance().getStatusChangeNotificationEnabled())
    {
        QString fStatus = "";
        switch (status)
        {
        case Status::Away:
            fStatus = tr("away", "contact status"); break;
        case Status::Busy:
            fStatus = tr("busy", "contact status"); break;
        case Status::Offline:
            fStatus = tr("offline", "contact status"); break;
        case Status::Online:
            fStatus = tr("online", "contact status"); break;
        }

        addSystemInfoMessage(tr("%1 is now %2", "e.g. \"Dubslow is now online\"")
                             .arg(f->getDisplayedName()).arg(fStatus),
                             ChatMessage::INFO, QDateTime::currentDateTime());
    }
}

void ChatForm::onFriendTypingChanged(quint32 friendId, bool isTyping)
{
    if (friendId == f->getFriendID())
        setFriendTyping(isTyping);
}

void ChatForm::onFriendNameChanged(const QString& name)
{
    if (sender() == f)
        setName(name);
}

void ChatForm::onFriendMessageReceived(quint32 friendId, const QString& message,
                                       bool isAction)
{
    if (friendId != f->getFriendID())
        return;

    QDateTime timestamp = QDateTime::currentDateTime();
    addMessage(f->getToxId(), message, isAction, timestamp, true);
}

void ChatForm::onStatusMessage(const QString& message)
{
    if (sender() == f)
        setStatusMessage(message);
}

void ChatForm::onReceiptReceived(quint32 friendId, int receipt)
{
    if (friendId == f->getFriendID())
        f->getChatForm()->getOfflineMsgEngine()->dischargeReceipt(receipt);
}

void ChatForm::onAvatarChange(uint32_t friendId, const QPixmap &pic)
{
    if (friendId != f->getFriendID())
        return;

    avatar->setPixmap(pic);
}

GenericNetCamView *ChatForm::createNetcam()
{
    qDebug() << "creating netcam";
    NetCamView* view = new NetCamView(f->getFriendID(), this);
    view->show(Core::getInstance()->getAv()->getVideoSourceFromCall(f->getFriendID()), f->getDisplayedName());
    return view;
}

void ChatForm::dragEnterEvent(QDragEnterEvent *ev)
{
    if (ev->mimeData()->hasUrls())
        ev->acceptProposedAction();
}

void ChatForm::dropEvent(QDropEvent *ev)
{
    if (ev->mimeData()->hasUrls())
    {
        Core* core = Core::getInstance();
        for (QUrl url : ev->mimeData()->urls())
        {
            QFileInfo info(url.path());

            QFile file(info.absoluteFilePath());
            if (url.isValid() && !url.isLocalFile() && (url.toString().length() < TOX_MAX_MESSAGE_LENGTH))
            {
                SendMessageStr(url.toString());
                continue;
            }

            if (!file.exists() || !file.open(QIODevice::ReadOnly))
            {
                info.setFile(url.toLocalFile());
                file.setFileName(info.absoluteFilePath());
                if (!file.exists() || !file.open(QIODevice::ReadOnly))
                {
                    QMessageBox::warning(this, tr("Unable to open"),
                                         tr("qTox wasn't able to open %1")
                                         .arg(info.fileName()));
                    continue;
                }
            }

            if (file.isSequential())
            {
                QMessageBox::critical(0, tr("Bad idea"),
                                      tr("You're trying to send a sequential"
                                         " file, which is not going to work!"));
                file.close();
                continue;
            }

            file.close();

            if (info.exists())
                core->sendFile(f->getFriendID(), info.fileName(),
                               info.absoluteFilePath(), info.size());
        }
    }
}

void ChatForm::onAvatarRemoved(uint32_t FriendId)
{
    if (FriendId != f->getFriendID())
        return;

    avatar->setPixmap(QPixmap(":/img/contact_dark.svg"));
}

void ChatForm::clearChatArea(bool notInForm)
{
    GenericChatForm::clearChatArea(notInForm);
    f->getChatForm()->getOfflineMsgEngine()->removeAllReceipts();
}

void ChatForm::onDeliverOfflineMessages()
{
    f->getChatForm()->getOfflineMsgEngine()->deliverOfflineMsgs();
}

void ChatForm::onLoadChatHistory()
{
    if (sender() == f)
        loadHistory(QDateTime::currentDateTime().addDays(-7), true);
}

// TODO: Split on smaller methods (style)
void ChatForm::loadHistory(QDateTime since, bool processUndelivered)
{
    QDateTime now = historyBaselineDate.addMSecs(-1);

    if (since > now)
        return;

    if (!earliestMessage.isNull())
    {
        if (earliestMessage < since)
            return;

        if (earliestMessage < now)
        {
            now = earliestMessage;
            now = now.addMSecs(-1);
        }
    }

    auto msgs = Nexus::getProfile()->getHistory()->getChatHistory(f->getToxId().publicKey, since, now);

    ToxId storedPrevId = previousId;
    ToxId prevId;

    QList<ChatLine::Ptr> historyMessages;

    QDate lastDate(1,0,0);
    for (const auto &it : msgs)
    {
        // Show the date every new day
        QDateTime msgDateTime = it.timestamp.toLocalTime();
        QDate msgDate = msgDateTime.date();

        if (msgDate > lastDate)
        {
            lastDate = msgDate;
            QString dateText = msgDate.toString(Settings::getInstance().getDateFormat());
            historyMessages.append(
                        ChatMessage::createChatInfoMessage(dateText,
                                                           ChatMessage::INFO,
                                                           QDateTime()));
        }

        // Show each messages
        const Core* core = Core::getInstance();
        ToxId authorId(it.sender);
        QString authorStr;
        bool isSelf = authorId == core->getSelfId();

        if (!it.dispName.isEmpty())
        {
            authorStr = it.dispName;
        }
        else if (isSelf)
        {
            authorStr = core->getUsername();
        }
        else
        {
            authorStr = resolveToxId(authorId);
        }

        bool isAction = it.message.startsWith(ACTION_PREFIX, Qt::CaseInsensitive);
        bool needSending = !it.isSent && isSelf;

        ChatMessage::Ptr msg =
                ChatMessage::createChatMessage(authorStr,
                                               isAction ? it.message.mid(4) : it.message,
                                               isAction ? ChatMessage::ACTION : ChatMessage::NORMAL,
                                               isSelf,
                                               needSending ? QDateTime() : msgDateTime);

        if (!isAction && (prevId == authorId) && (prevMsgDateTime.secsTo(msgDateTime) < getChatLog()->repNameAfter) )
            msg->hideSender();

        prevId = authorId;
        prevMsgDateTime = msgDateTime;

        if (needSending)
        {
            if (processUndelivered)
            {
                int rec;
                if (!isAction)
                    rec = Core::getInstance()->sendMessage(f->getFriendID(), msg->toString());
                else
                    rec = Core::getInstance()->sendAction(f->getFriendID(), msg->toString());

                getOfflineMsgEngine()->registerReceipt(rec, it.id, msg);
            }
        }
        historyMessages.append(msg);
    }

    previousId = storedPrevId;
    int savedSliderPos = chatWidget->verticalScrollBar()->maximum() - chatWidget->verticalScrollBar()->value();

    earliestMessage = since;

    chatWidget->insertChatlineOnTop(historyMessages);

    savedSliderPos = chatWidget->verticalScrollBar()->maximum() - savedSliderPos;
    chatWidget->verticalScrollBar()->setValue(savedSliderPos);
}

void ChatForm::onScreenshotClicked()
{
    doScreenshot();

    // Give the window manager a moment to open the fullscreen grabber window
    QTimer::singleShot(500, this, SLOT(hideFileMenu()));
}

void ChatForm::doScreenshot()
{
    // note: grabber is self-managed and will destroy itself when done
    ScreenshotGrabber* screenshotGrabber = new ScreenshotGrabber;

    connect(screenshotGrabber, &ScreenshotGrabber::screenshotTaken,
            this, &ChatForm::onScreenshotTaken);

    screenshotGrabber->showGrabber();

    // Create dir for screenshots
    QDir(Settings::getInstance().getAppDataDirPath()).mkpath("screenshots");
}

void ChatForm::onScreenshotTaken(const QPixmap &pixmap) {
    // use ~ISO 8601 for screenshot timestamp, considering FS limitations
    // https://en.wikipedia.org/wiki/ISO_8601
    // Windows has to be supported, thus filename can't have `:` in it :/
    // Format should be: `qTox_Screenshot_yyyy-MM-dd HH-mm-ss.zzz.png`
    QString filepath = QString("%1screenshots%2qTox_Screenshot_%3.png")
                           .arg(Settings::getInstance().getAppDataDirPath())
                           .arg(QDir::separator())
                           .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss.zzz"));

    QFile file(filepath);

    if (file.open(QFile::ReadWrite))
    {
        pixmap.save(&file, "PNG");

        qint64 filesize = file.size();
        file.close();
        QFileInfo fi(file);

        Core::getInstance()->sendFile(f->getFriendID(), fi.fileName(),
                                      fi.filePath(), filesize);
    }
    else
    {
        QMessageBox::warning(this,
                             tr("Failed to open temporary file", "Temporary file for screenshot"),
                             tr("qTox wasn't able to save the screenshot"));
    }
}

void ChatForm::onLoadHistory()
{
    if (!Nexus::getProfile()->isHistoryEnabled())
        return;

    LoadHistoryDialog dlg;

    if (dlg.exec())
    {
        QDateTime fromTime = dlg.getFromDate();
        loadHistory(fromTime);
    }
}

void ChatForm::insertChatMessage(ChatMessage::Ptr msg)
{
    GenericChatForm::insertChatMessage(msg);

    if (netcam && bodySplitter->sizes()[1] == 0)
        netcam->setShowMessages(true, true);
}

void ChatForm::onCopyStatusMessage()
{
    QString text = statusMessageLabel->text();
    QClipboard* clipboard = QApplication::clipboard();

    if (clipboard)
    {
        clipboard->setText(text, QClipboard::Clipboard);
    }
}

void ChatForm::updateMuteMicButton()
{
    const CoreAV* av = Core::getInstance()->getAv();

    micButton->setEnabled(av->isCallActive(f));

    if (micButton->isEnabled())
    {
        if (av->isCallInputMuted(f))
        {
            micButton->setObjectName("red");
            micButton->setToolTip(tr("Unmute microphone"));
        }
        else
        {
            micButton->setObjectName("green");
            micButton->setToolTip(tr("Mute microphone"));
        }
    }

    micButton->setStyleSheet(Style::getStylesheet(QStringLiteral(":/ui/micButton/micButton.css")));
}

void ChatForm::updateMuteVolButton()
{
    const CoreAV* av = Core::getInstance()->getAv();

    volButton->setEnabled(av->isCallActive(f));

    if (videoButton->isEnabled())
    {
        if (av->isCallOutputMuted(f))
        {
            volButton->setObjectName("red");
            volButton->setToolTip(tr("Unmute call"));
        }
        else
        {
            volButton->setObjectName("green");
            volButton->setToolTip(tr("Mute call"));
        }
    }

    volButton->setStyleSheet(Style::getStylesheet(QStringLiteral(":/ui/volButton/volButton.css")));
}

void ChatForm::startCounter()
{
    if (!callDurationTimer)
    {
        callDurationTimer = new QTimer();
        connect(callDurationTimer, &QTimer::timeout,
                this, &ChatForm::onUpdateTime);
        callDurationTimer->start(1000);
        timeElapsed.start();
        callDuration->show();
    }
}

void ChatForm::stopCounter()
{
    if (callDurationTimer)
    {
        addSystemInfoMessage(tr("Call with %1 ended. %2").arg(f->getDisplayedName(),secondsToDHMS(timeElapsed.elapsed()/1000)),
                             ChatMessage::INFO, QDateTime::currentDateTime());
        callDurationTimer->stop();
        callDuration->setText("");
        callDuration->hide();

        delete callDurationTimer;
        callDurationTimer = nullptr;
    }
}

void ChatForm::onUpdateTime()
{
    callDuration->setText(secondsToDHMS(timeElapsed.elapsed()/1000));
}

QString ChatForm::secondsToDHMS(quint32 duration)
{
    QString res;
    QString cD = tr("Call duration: ");
    int seconds = (int) (duration % 60);
    duration /= 60;
    int minutes = (int) (duration % 60);
    duration /= 60;
    int hours = (int) (duration % 24);
    int days = (int) (duration / 24);

    // I assume no one will ever have call longer than a month
    if (days)
        return cD + res.sprintf("%dd%02dh %02dm %02ds", days, hours, minutes, seconds);
    else if (hours)
        return cD + res.sprintf("%02dh %02dm %02ds", hours, minutes, seconds);
    else if (minutes)
        return cD + res.sprintf("%02dm %02ds", minutes, seconds);
    else
        return cD + res.sprintf("%02ds", seconds);
}

void ChatForm::setFriendTyping(bool isTyping)
{
    chatWidget->setTypingNotificationVisible(isTyping);

    Text* text = static_cast<Text*>(chatWidget->getTypingNotification()->getContent(1));
    text->setText("<div class=typing>" + tr("%1 is typing").arg(f->getDisplayedName().toHtmlEscaped()) + "</div>");
}

void ChatForm::show(ContentLayout* contentLayout)
{
    GenericChatForm::show(contentLayout);

    if (callConfirm)
        callConfirm->show();
}

void ChatForm::showEvent(QShowEvent* event)
{
    if (callConfirm)
        callConfirm->show();

    GenericChatForm::showEvent(event);
}

void ChatForm::hideEvent(QHideEvent* event)
{
    if (callConfirm)
        callConfirm->hide();

    GenericChatForm::hideEvent(event);
}

OfflineMsgEngine *ChatForm::getOfflineMsgEngine()
{
    return offlineEngine;
}

void ChatForm::SendMessageStr(QString msg)
{
    if (msg.isEmpty())
        return;

    bool isAction = msg.startsWith(ACTION_PREFIX, Qt::CaseInsensitive);
    if (isAction)
        msg.remove(0, ACTION_PREFIX.length());

    QList<CString> splittedMsg = Core::splitMessage(msg, TOX_MAX_MESSAGE_LENGTH);
    QDateTime timestamp = QDateTime::currentDateTime();

    for (CString& c_msg : splittedMsg)
    {
        QString qt_msg = CString::toString(c_msg.data(), c_msg.size());
        QString qt_msg_hist = qt_msg;
        if (isAction)
            qt_msg_hist = ACTION_PREFIX + qt_msg;

        bool status = !Settings::getInstance().getFauxOfflineMessaging();

        ChatMessage::Ptr ma = addSelfMessage(qt_msg, isAction, timestamp, false);

        int rec;
        if (isAction)
            rec = Core::getInstance()->sendAction(f->getFriendID(), qt_msg);
        else
            rec = Core::getInstance()->sendMessage(f->getFriendID(), qt_msg);


        Profile* profile = Nexus::getProfile();
        if (profile->isHistoryEnabled())
        {
            auto* offMsgEngine = getOfflineMsgEngine();
            profile->getHistory()->addNewMessage(f->getToxId().publicKey, qt_msg_hist,
                        Core::getInstance()->getSelfId().publicKey, timestamp, status, Core::getInstance()->getUsername(),
                                        [offMsgEngine,rec,ma](int64_t id)
            {
                offMsgEngine->registerReceipt(rec, id, ma);
            });
        }
        else
        {
            // TODO: Make faux-offline messaging work partially with the history disabled
            ma->markAsSent(QDateTime::currentDateTime());
        }

        msgEdit->setLastMessage(msg); //set last message only when sending it

        Widget::getInstance()->updateFriendActivity(f);
    }
}

void ChatForm::retranslateUi()
{
    QString volObjectName = volButton->objectName();
    QString micObjectName = micButton->objectName();
    loadHistoryAction->setText(tr("Load chat history..."));
    copyStatusAction->setText(tr("Copy"));

    updateMuteMicButton();
    updateMuteVolButton();

    if (netcam)
        netcam->setShowMessages(chatWidget->isVisible());
}
