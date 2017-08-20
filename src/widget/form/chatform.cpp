/*
    Copyright © 2014-2017 by The qTox Project Contributors

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

#include "src/audio/audio.h"
#include "src/chatlog/chatlinecontentproxy.h"
#include "src/chatlog/chatlog.h"
#include "src/chatlog/chatmessage.h"
#include "src/chatlog/content/filetransferwidget.h"
#include "src/chatlog/content/text.h"
#include "src/core/core.h"
#include "src/core/coreav.h"
#include "src/model/friend.h"
#include "src/nexus.h"
#include "src/persistence/offlinemsgengine.h"
#include "src/persistence/profile.h"
#include "src/persistence/settings.h"
#include "src/video/netcamview.h"
#include "src/widget/form/loadhistorydialog.h"
#include "src/widget/maskablepixmapwidget.h"
#include "src/widget/style.h"
#include "src/widget/tool/callconfirmwidget.h"
#include "src/widget/tool/chattextedit.h"
#include "src/widget/tool/screenshotgrabber.h"
#include "src/widget/translator.h"
#include "src/widget/widget.h"

#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScrollBar>
#include <QStringBuilder>

#include <cassert>

static const int CHAT_WIDGET_MIN_HEIGHT = 50;
static const int DELIVER_OFFLINE_MESSAGES_DELAY = 250;
static const int SCREENSHOT_GRABBER_OPENING_DELAY = 500;
static const int TYPING_NOTIFICATION_DURATION = 3000;

static const QString CALL_BTN_STYLESHEET = QStringLiteral(":/ui/callButton/callButton.css");
static const QString MIC_BTN_STYLESHEET = QStringLiteral(":/ui/micButton/micButton.css");
static const QString VIDEO_BTN_STYLESHEET = QStringLiteral(":/ui/videoButton/videoButton.css");
static const QString VOL_BTN_STYLESHEET = QStringLiteral(":/ui/volButton/volButton.css");

const QString ChatForm::ACTION_PREFIX = QStringLiteral("/me ");

QString statusToString(const Status status)
{
    QString result;
    switch (status) {
    case Status::Online:
        result = ChatForm::tr("online", "contact status");
        break;
    case Status::Away:
        result = ChatForm::tr("away", "contact status");
        break;
    case Status::Busy:
        result = ChatForm::tr("busy", "contact status");
        break;
    case Status::Offline:
        result = ChatForm::tr("offline", "contact status");
        break;
    }
    return result;
}

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
    chatWidget->setMinimumHeight(CHAT_WIDGET_MIN_HEIGHT);

    headTextLayout->addWidget(statusMessageLabel);
    headTextLayout->addStretch();
    callDuration = new QLabel();
    headTextLayout->addWidget(callDuration, 1, Qt::AlignCenter);
    callDuration->hide();

    loadHistoryAction = menu.addAction(QString(), this, SLOT(onLoadHistory()));
    copyStatusAction = statusMessageMenu.addAction(QString(), this, SLOT(onCopyStatusMessage()));

    exportChatAction =
        menu.addAction(QIcon::fromTheme("document-save"), QString(), this, SLOT(onExportChat()));

    const Core* core = Core::getInstance();
    connect(core, &Core::fileReceiveRequested, this, &ChatForm::onFileRecvRequest);
    connect(core, &Core::friendAvatarChanged, this, &ChatForm::onAvatarChange);
    connect(core, &Core::friendAvatarRemoved, this, &ChatForm::onAvatarRemoved);
    connect(core, &Core::fileSendStarted, this, &ChatForm::startFileSend);
    connect(core, &Core::fileSendFailed, this, &ChatForm::onFileSendFailed);
    connect(core, &Core::receiptRecieved, this, &ChatForm::onReceiptReceived);
    connect(core, &Core::friendMessageReceived, this, &ChatForm::onFriendMessageReceived);
    connect(core, &Core::friendTypingChanged, this, &ChatForm::onFriendTypingChanged);
    connect(core, &Core::friendStatusChanged, this, &ChatForm::onFriendStatusChanged);


    const CoreAV* av = core->getAv();
    connect(av, &CoreAV::avInvite, this, &ChatForm::onAvInvite);
    connect(av, &CoreAV::avStart, this, &ChatForm::onAvStart);
    connect(av, &CoreAV::avEnd, this, &ChatForm::onAvEnd);

    connect(sendButton, &QPushButton::clicked, this, &ChatForm::onSendTriggered);
    connect(fileButton, &QPushButton::clicked, this, &ChatForm::onAttachClicked);
    connect(screenshotButton, &QPushButton::clicked, this, &ChatForm::onScreenshotClicked);
    connect(callButton, &QAbstractButton::clicked, this, &ChatForm::onCallTriggered);
    connect(videoButton, &QAbstractButton::clicked, this, &ChatForm::onVideoCallTriggered);
    connect(micButton, &QAbstractButton::clicked, this, &ChatForm::onMicMuteToggle);
    connect(volButton, &QAbstractButton::clicked, this, &ChatForm::onVolMuteToggle);

    connect(msgEdit, &ChatTextEdit::enterPressed, this, &ChatForm::onSendTriggered);
    connect(msgEdit, &ChatTextEdit::textChanged, this, &ChatForm::onTextEditChanged);
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

    connect(nameLabel, &CroppingLabel::editFinished, this, [=](const QString& newName) {
        nameLabel->setText(newName);
        emit aliasChanged(newName);
    });

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

void ChatForm::onSendTriggered()
{
    SendMessageStr(msgEdit->toPlainText());
    msgEdit->clear();
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
    QStringList paths = QFileDialog::getOpenFileNames(Q_NULLPTR, tr("Send a file"), QDir::homePath(), 0, 0);

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
        core->sendFile(f->getId(), fileName, path, filesize);
    }
}

void ChatForm::startFileSend(ToxFile file)
{
    if (file.friendId != f->getId()) {
        return;
    }

    QString name;
    const Core* core = Core::getInstance();
    ToxPk self = core->getSelfId().getPublicKey();
    if (previousId != self) {
        name = core->getUsername();
        previousId = self;
    }

    insertChatMessage(
        ChatMessage::createFileTransferMessage(name, file, true, QDateTime::currentDateTime()));
    Widget::getInstance()->updateFriendActivity(f);
}

void ChatForm::onFileRecvRequest(ToxFile file)
{
    if (file.friendId != f->getId()) {
        return;
    }

    Widget::getInstance()->newFriendMessageAlert(file.friendId);
    QString name;
    ToxPk friendId = f->getPublicKey();
    if (friendId != previousId) {
        name = f->getDisplayedName();
        previousId = friendId;
    }

    ChatMessage::Ptr msg =
        ChatMessage::createFileTransferMessage(name, file, false, QDateTime::currentDateTime());
    insertChatMessage(msg);

    ChatLineContentProxy* proxy = static_cast<ChatLineContentProxy*>(msg->getContent(1));
    assert(proxy->getWidgetType() == ChatLineContentProxy::FileTransferWidgetType);
    FileTransferWidget* tfWidget = static_cast<FileTransferWidget*>(proxy->getWidget());

    const Settings& settings = Settings::getInstance();
    QString autoAcceptDir = settings.getAutoAcceptDir(f->getPublicKey());
    // there is auto-accept for that contact
    if (!autoAcceptDir.isEmpty()) {
        tfWidget->autoAcceptTransfer(autoAcceptDir);
        // global autosave to global directory
    } else if (settings.getAutoSaveEnabled()) {
        tfWidget->autoAcceptTransfer(settings.getGlobalAutoAcceptDir());
    }

    Widget::getInstance()->updateFriendActivity(f);
}

void ChatForm::onAvInvite(uint32_t friendId, bool video)
{
    if (friendId != f->getId()) {
        return;
    }

    callConfirm = new CallConfirmWidget(video ? videoButton : callButton, *f);
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
                                  Q_ARG(uint32_t, friendId));
        onAvStart(friendId, video);
    } else {
        callConfirm->show();
        CallConfirmWidget* confirmData = callConfirm.data();
        connect(confirmData, &CallConfirmWidget::accepted, this, &ChatForm::onAnswerCallTriggered);
        connect(confirmData, &CallConfirmWidget::rejected, this, &ChatForm::onRejectCallTriggered);
        auto msg = ChatMessage::createChatInfoMessage(tr("%1 calling").arg(displayedName),
                                                      ChatMessage::INFO, QDateTime::currentDateTime());
        insertChatMessage(msg);
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

    Audio::getInstance().stopLoop();
    updateCallButtons();
    startCounter();
}

void ChatForm::onAvEnd(uint32_t friendId, bool error)
{
    if (friendId != f->getId()) {
        return;
    }

    delete callConfirm;

    // Fixes an OS X bug with ending a call while in full screen
    if (netcam && netcam->isFullScreen()) {
        netcam->showNormal();
    }

    updateCallButtons();
    stopCounter(error);
    hideNetcam();
}

void ChatForm::showOutgoingCall(bool video)
{
    QPushButton* btn = video ? videoButton : callButton;
    btn->setObjectName("yellow");
    btn->setStyleSheet(Style::getStylesheet(video ? VIDEO_BTN_STYLESHEET : CALL_BTN_STYLESHEET));
    btn->setToolTip(video ? tr("Cancel video call") : tr("Cancel audio call"));
    addSystemInfoMessage(tr("Calling %1").arg(f->getDisplayedName()), ChatMessage::INFO,
                         QDateTime::currentDateTime());
    emit outgoingNotification();
    Widget::getInstance()->updateFriendActivity(f);
}

void ChatForm::onAnswerCallTriggered()
{
    delete callConfirm;
    uint32_t friendId = f->getId();
    emit acceptCall(friendId);

    updateCallButtons();
    CoreAV* av = Core::getInstance()->getAv();
    if (!av->answerCall(friendId)) {
        updateCallButtons();
        stopCounter();
        hideNetcam();
        return;
    }

    onAvStart(friendId, av->isCallVideoEnabled(f));
}

void ChatForm::onRejectCallTriggered()
{
    delete callConfirm;
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
    bool audio = av->isCallActive(f);
    bool video = av->isCallVideoEnabled(f);
    callButton->setEnabled(audio && !video);
    videoButton->setEnabled(video);
    if (audio) {
        videoButton->setObjectName(video ? "red" : "");
        videoButton->setToolTip(video ? tr("End video call") : tr("Can't start video call"));

        callButton->setObjectName(!video ? "red" : "");
        callButton->setToolTip(!video ? tr("End audio call") : tr("Can't start audio call"));
    } else {
        const Status fs = f->getStatus();
        bool online = fs != Status::Offline;
        callButton->setEnabled(online);
        videoButton->setEnabled(online);

        QString color = online ? "green" : "";
        callButton->setObjectName(color);
        callButton->setToolTip(online ? tr("Start audio call") : tr("Can't start audio call"));

        videoButton->setObjectName(color);
        videoButton->setToolTip(online ? tr("Start video call") : tr("Can't start video call"));
    }

    callButton->setStyleSheet(Style::getStylesheet(CALL_BTN_STYLESHEET));
    videoButton->setStyleSheet(Style::getStylesheet(VIDEO_BTN_STYLESHEET));
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

void ChatForm::onFileSendFailed(uint32_t friendId, const QString& fname)
{
    if (friendId != f->getId()) {
        return;
    }

    addSystemInfoMessage(tr("Failed to send file \"%1\"").arg(fname), ChatMessage::ERROR,
                         QDateTime::currentDateTime());
}

void ChatForm::onFriendStatusChanged(uint32_t friendId, Status status)
{
    // Disable call buttons if friend is offline
    if (friendId != f->getId()) {
        return;
    }

    if (status == Status::Offline) {
        // Hide the "is typing" message when a friend goes offline
        setFriendTyping(false);
    } else {
        QTimer::singleShot(DELIVER_OFFLINE_MESSAGES_DELAY, this, SLOT(onDeliverOfflineMessages()));
    }

    updateCallButtons();

    if (Settings::getInstance().getStatusChangeNotificationEnabled()) {
        QString fStatus = statusToString(status);
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

void ChatForm::onFriendMessageReceived(quint32 friendId, const QString& message, bool isAction)
{
    if (friendId != f->getId()) {
        return;
    }

    QDateTime timestamp = QDateTime::currentDateTime();
    addMessage(f->getPublicKey(), message, timestamp, isAction);
}

void ChatForm::onStatusMessage(const QString& message)
{
    if (sender() == f) {
        setStatusMessage(message);
    }
}

void ChatForm::onReceiptReceived(quint32 friendId, int receipt)
{
    if (friendId == f->getId()) {
        offlineEngine->dischargeReceipt(receipt);
    }
}

void ChatForm::onAvatarChange(uint32_t friendId, const QPixmap& pic)
{
    if (friendId != f->getId()) {
        return;
    }

    avatar->setPixmap(pic);
}

GenericNetCamView* ChatForm::createNetcam()
{
    qDebug() << "creating netcam";
    uint32_t friendId = f->getId();
    NetCamView* view = new NetCamView(friendId, this);
    CoreAV* av = Core::getInstance()->getAv();
    VideoSource* source = av->getVideoSourceFromCall(friendId);
    view->show(source, f->getDisplayedName());
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
            SendMessageStr(urlString);
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
            QMessageBox::critical(0, tr("Bad idea"), tr("You're trying to send a sequential file, "
                                                        "which is not going to work!"));
            continue;
        }

        if (info.exists()) {
            core->sendFile(f->getId(), fileName, info.absoluteFilePath(), info.size());
        }
    }
}

void ChatForm::onAvatarRemoved(uint32_t friendId)
{
    if (friendId != f->getId()) {
        return;
    }

    avatar->setPixmap(QPixmap(":/img/contact_dark.svg"));
}

void ChatForm::clearChatArea(bool notInForm)
{
    GenericChatForm::clearChatArea(notInForm);
    offlineEngine->removeAllReceipts();
}

void ChatForm::onDeliverOfflineMessages()
{
    offlineEngine->deliverOfflineMsgs();
}

void ChatForm::onLoadChatHistory()
{
    if (sender() == f) {
        loadHistory(QDateTime::currentDateTime().addDays(-7), true);
    }
}

static QString getMsgAuthorDispName(const ToxPk& authorPk, const QString& dispName = "")
{
    QString authorStr;
    const Core* core = Core::getInstance();
    bool isSelf = authorPk == core->getSelfId().getPublicKey();

    if (!dispName.isEmpty()) {
        authorStr = dispName;
    } else if (isSelf) {
        authorStr = core->getUsername();
    } else {
        authorStr = ChatForm::resolveToxPk(authorPk);
    }
    return authorStr;
}

// TODO: Split on smaller methods (style)
void ChatForm::loadHistory(const QDateTime& since, bool processUndelivered)
{
    QDateTime now = historyBaselineDate.addMSecs(-1);
    if (since > now) {
        return;
    }

    if (!earliestMessage.isNull()) {
        if (earliestMessage < since) {
            return;
        }

        if (earliestMessage < now) {
            now = earliestMessage;
            now = now.addMSecs(-1);
        }
    }

    History* history = Nexus::getProfile()->getHistory();
    QString pk = f->getPublicKey().toString();
    QList<TextMessage> msgs = history->getChatHistory(pk, since, now);

    ToxPk prevIdBackup = previousId;
    previousId = ToxPk{};

    QList<ChatLine::Ptr> historyMessages;

    QDate lastDate(1, 0, 0);
    for (const TextMessage& m : msgs) {
        // Show the date every new day
        QDateTime msgDateTime = m.getTime().toLocalTime();
        QDate msgDate = msgDateTime.date();

        if (msgDate > lastDate) {
            lastDate = msgDate;
            QString dateText = msgDate.toString(Settings::getInstance().getDateFormat());
            auto msg = ChatMessage::createChatInfoMessage(dateText, ChatMessage::INFO, QDateTime());
            historyMessages.append(msg);
        }

        // Show each messages
        const Core* core = Core::getInstance();
        const ToxPk& authorPk = m.getAuthor();
        QString authorStr = getMsgAuthorDispName(authorPk);
        bool isSelf = authorPk == core->getSelfId().getPublicKey();

        bool isAction = m.isAction();
        bool isSent = msgDateTime != QDateTime();
        bool needSending = !isSent && isSelf;

        QString messageText = isAction ? m.getText().mid(ACTION_PREFIX.length()) : m.getText();
        ChatMessage::MessageType type = isAction ? ChatMessage::ACTION : ChatMessage::NORMAL;
        QDateTime dateTime = needSending ? QDateTime() : msgDateTime;
        auto msg = ChatMessage::createChatMessage(authorStr, messageText, type, isSelf, dateTime);
        if (!isAction && needsToHideName(authorPk)) {
            msg->hideSender();
        }

        previousId = authorPk;
        prevMsgDateTime = msgDateTime;

        if (needSending && processUndelivered) {
            Core* core = Core::getInstance();
            uint32_t friendId = f->getId();
            QString stringMsg = msg->toString();
            int receipt = isAction ? core->sendAction(friendId, stringMsg)
                                   : core->sendMessage(friendId, stringMsg);
            getOfflineMsgEngine()->registerReceipt(receipt, m.getId(), msg);
        }

        historyMessages.append(msg);
    }

    previousId = prevIdBackup;
    earliestMessage = since;

    QScrollBar* verticalBar = chatWidget->verticalScrollBar();
    int savedSliderPos = verticalBar->maximum() - verticalBar->value();
    chatWidget->insertChatlineOnTop(historyMessages);
    savedSliderPos = verticalBar->maximum() - savedSliderPos;
    verticalBar->setValue(savedSliderPos);
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
    connect(grabber, &ScreenshotGrabber::screenshotTaken, this, &ChatForm::onScreenshotTaken);
    grabber->showGrabber();
    // Create dir for screenshots
    QDir(Settings::getInstance().getAppDataDirPath()).mkpath("screenshots");
}

void ChatForm::onScreenshotTaken(const QPixmap& pixmap)
{
    // use ~ISO 8601 for screenshot timestamp, considering FS limitations
    // https://en.wikipedia.org/wiki/ISO_8601
    // Windows has to be supported, thus filename can't have `:` in it :/
    // Format should be: `qTox_Screenshot_yyyy-MM-dd HH-mm-ss.zzz.png`
    QString filepath = QString("%1screenshots%2qTox_Screenshot_%3.png")
                           .arg(Settings::getInstance().getAppDataDirPath())
                           .arg(QDir::separator())
                           .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss.zzz"));
    QFile file(filepath);

    if (file.open(QFile::ReadWrite)) {
        pixmap.save(&file, "PNG");
        qint64 filesize = file.size();
        file.close();
        QFileInfo fi(file);
        Core::getInstance()->sendFile(f->getId(), fi.fileName(), fi.filePath(), filesize);
    } else {
        QMessageBox::warning(this,
                             tr("Failed to open temporary file", "Temporary file for screenshot"),
                             tr("qTox wasn't able to save the screenshot"));
    }
}

void ChatForm::onLoadHistory()
{
    if (!Nexus::getProfile()->isHistoryEnabled()) {
        return;
    }

    LoadHistoryDialog dlg(f->getPublicKey());
    if (dlg.exec()) {
        QDateTime fromTime = dlg.getFromDate();
        loadHistory(fromTime);
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

    micButton->setEnabled(av->isCallActive(f));
    if (micButton->isEnabled()) {
        bool inputMuted = av->isCallInputMuted(f);
        micButton->setObjectName(inputMuted ? "red" : "green");
        micButton->setToolTip(inputMuted ? tr("Unmute microphone") : tr("Mute microphone"));
    } else {
        micButton->setObjectName("");
        micButton->setToolTip(tr("Microphone can be muted only during a call"));
    }

    micButton->setStyleSheet(Style::getStylesheet(MIC_BTN_STYLESHEET));
}

void ChatForm::updateMuteVolButton()
{
    const CoreAV* av = Core::getInstance()->getAv();

    volButton->setEnabled(av->isCallActive(f));
    if (volButton->isEnabled()) {
        bool outputMuted = av->isCallOutputMuted(f);
        volButton->setObjectName(outputMuted ? "red" : "green");
        volButton->setToolTip(outputMuted ? tr("Unmute call") : tr("Mute call"));
    } else {
        volButton->setObjectName("");
        volButton->setToolTip(tr("Sound can be disabled only during a call"));
    }

    volButton->setStyleSheet(Style::getStylesheet(VOL_BTN_STYLESHEET));
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

    addSystemInfoMessage(mess.arg(name, dhms), ChatMessage::INFO,
                         QDateTime::currentDateTime());
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
    if (callConfirm) {
        callConfirm->show();
    }
}

void ChatForm::showEvent(QShowEvent* event)
{
    if (callConfirm) {
        callConfirm->show();
    }

    GenericChatForm::showEvent(event);
}

void ChatForm::hideEvent(QHideEvent* event)
{
    if (callConfirm) {
        callConfirm->hide();
    }

    GenericChatForm::hideEvent(event);
}

OfflineMsgEngine* ChatForm::getOfflineMsgEngine()
{
    return offlineEngine;
}

void ChatForm::SendMessageStr(QString msg)
{
    if (msg.isEmpty()) {
        return;
    }

    bool isAction = msg.startsWith(ACTION_PREFIX, Qt::CaseInsensitive);
    if (isAction) {
        msg.remove(0, ACTION_PREFIX.length());
    }

    QStringList splittedMsg = Core::splitMessage(msg, tox_max_message_length());
    QDateTime timestamp = QDateTime::currentDateTime();

    for (const QString& part : splittedMsg) {
        QString historyPart = part;
        if (isAction) {
            historyPart = ACTION_PREFIX + part;
        }

        bool status = !Settings::getInstance().getFauxOfflineMessaging();
        ChatMessage::Ptr ma = createSelfMessage(part, timestamp, isAction, false);
        Core* core = Core::getInstance();
        uint32_t friendId = f->getId();
        int rec = isAction ? core->sendAction(friendId, part) : core->sendMessage(friendId, part);

        Profile* profile = Nexus::getProfile();
        if (profile->isHistoryEnabled()) {
            auto* offMsgEngine = getOfflineMsgEngine();
            QString selfPk = Core::getInstance()->getSelfId().toString();
            QString pk = f->getPublicKey().toString();
            QString name = Core::getInstance()->getUsername();
            profile->getHistory()->addNewMessage(pk, historyPart, selfPk, timestamp, status, name,
                                                 [offMsgEngine, rec, ma](int64_t id) {
                                                     offMsgEngine->registerReceipt(rec, id, ma);
                                                 });
        } else {
            // TODO: Make faux-offline messaging work partially with the history disabled
            ma->markAsSent(QDateTime::currentDateTime());
        }

        // set last message only when sending it
        msgEdit->setLastMessage(msg);
        Widget::getInstance()->updateFriendActivity(f);
    }
}

void ChatForm::retranslateUi()
{
    QString volObjectName = volButton->objectName();
    QString micObjectName = micButton->objectName();
    loadHistoryAction->setText(tr("Load chat history..."));
    copyStatusAction->setText(tr("Copy"));
    exportChatAction->setText(tr("Export to file"));

    updateMuteMicButton();
    updateMuteVolButton();

    if (netcam) {
        netcam->setShowMessages(chatWidget->isVisible());
    }
}

void ChatForm::onExportChat()
{
    History* history = Nexus::getProfile()->getHistory();
    QString pk = f->getPublicKey().toString();
    QDateTime epochStart = QDateTime::fromMSecsSinceEpoch(0);
    QDateTime now = QDateTime::currentDateTime();
    QList<TextMessage> msgs = history->getChatHistory(pk, epochStart, now);

    QString path = QFileDialog::getSaveFileName(0, tr("Save chat log"), QString{}, QString{}, 0,
                                                QFileDialog::DontUseNativeDialog);
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QString buffer;
    for (const TextMessage& m : msgs) {
        QString timestamp = m.getTime().toString();
        const ToxPk& authorPk = m.getAuthor();
        QString author = getMsgAuthorDispName(authorPk);
        const QString& text = m.getText();

        QString line = QString("%1\t%2\t%3\n").arg(timestamp, author, text);
        buffer = buffer % line;
    }
    file.write(buffer.toUtf8());
    file.close();
}
