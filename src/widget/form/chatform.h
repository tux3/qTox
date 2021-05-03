/*
    Copyright © 2014-2019 by The qTox Project Contributors

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

#include <QElapsedTimer>
#include <QLabel>
#include <QSet>
#include <QTimer>

#include "genericchatform.h"
#include "src/core/core.h"
#include "src/core/coreav.h"
#include "src/model/ichatlog.h"
#include "src/model/imessagedispatcher.h"
#include "src/model/status.h"
#include "src/persistence/history.h"
#include "src/widget/tool/screenshotgrabber.h"
#include "src/video/netcamview.h"

class CallConfirmWidget;
class FileTransferInstance;
class Friend;
class History;
class OfflineMsgEngine;
class QPixmap;
class QHideEvent;
class QMoveEvent;

class ChatForm : public GenericChatForm
{
    Q_OBJECT
public:
    ChatForm(Profile& profile, Friend* chatFriend, IChatLog& chatLog, IMessageDispatcher& messageDispatcher);
    ~ChatForm() override;
    void setStatusMessage(const QString& newMessage);

    void setFriendTyping(bool isTyping);

    void show(ContentLayout* contentLayout) final;

    static const QString ACTION_PREFIX;

signals:

    void incomingNotification(uint32_t friendId);
    void outgoingNotification();
    void stopNotification();
    void endCallNotification();
    void rejectCall(uint32_t friendId);
    void acceptCall(uint32_t friendId);
    void updateFriendActivity(Friend& frnd);

public slots:
    void onAvInvite(uint32_t friendId, bool video, std::shared_ptr<ToxFriendCall> call);
    void onAvStart(uint32_t friendId, bool video);
    void onAvEnd(uint32_t friendId, bool error);
    void onAvatarChanged(const ToxPk& friendPk, const QPixmap& pic);
    void onFileNameChanged(const ToxPk& friendPk);
    void onExtensionSupportChanged(ExtensionSet extensions);
    void clearChatArea();
    void onShowMessagesClicked();
    void onSplitterMoved(int pos, int index);
    void reloadTheme() final;

private slots:
    void updateFriendActivityForFile(const ToxFile& file);
    void onAttachClicked() override;
    void onScreenshotClicked() override;

    void onTextEditChanged();
    void onCallTriggered();
    void onVideoCallTriggered();
    void onAnswerCallTriggered(bool video);
    void onRejectCallTriggered();
    void onMicMuteToggle();
    void onVolMuteToggle();

    void onFriendStatusChanged(const ToxPk& friendPk, Status::Status status);
    void onFriendTypingChanged(quint32 friendId, bool isTyping);
    void onFriendNameChanged(const QString& name);
    void onStatusMessage(const QString& message);
    void onUpdateTime();
    void sendImage(const QPixmap& pixmap);
    void doScreenshot();
    void onCopyStatusMessage();

    void callUpdateFriendActivity();

private:
    void updateMuteMicButton();
    void updateMuteVolButton();
    void retranslateUi();
    void showOutgoingCall(bool video);
    void startCounter();
    void stopCounter(bool error = false);
    void updateCallButtons();
    void showNetcam();
    void hideNetcam();

protected:
    std::unique_ptr<NetCamView> createNetcam();
    void insertChatMessage(ChatMessage::Ptr msg) final;
    void dragEnterEvent(QDragEnterEvent* ev) final;
    void dropEvent(QDropEvent* ev) final;
    void hideEvent(QHideEvent* event) final;
    void showEvent(QShowEvent* event) final;

private:
    Core& core;
    Friend* f;
    CroppingLabel* statusMessageLabel;
    QMenu statusMessageMenu;
    QLabel* callDuration;
    QTimer* callDurationTimer;
    QTimer typingTimer;
    QElapsedTimer timeElapsed;
    QAction* copyStatusAction;
    bool isTyping;
    bool lastCallIsVideo;
    std::unique_ptr<NetCamView> netcam;
    CoreAV::ToxFriendCallPtr call;
};
