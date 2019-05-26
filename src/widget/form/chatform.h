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

#ifndef CHATFORM_H
#define CHATFORM_H

#include <QElapsedTimer>
#include <QLabel>
#include <QSet>
#include <QTimer>

#include "genericchatform.h"
#include "src/core/core.h"
#include "src/model/ichatlog.h"
#include "src/model/imessagedispatcher.h"
#include "src/model/status.h"
#include "src/persistence/history.h"
#include "src/widget/tool/screenshotgrabber.h"

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
    ChatForm(Friend* chatFriend, IChatLog& chatLog, IMessageDispatcher& messageDispatcher);
    ~ChatForm();
    void setStatusMessage(const QString& newMessage);

    void setFriendTyping(bool isTyping);

    virtual void show(ContentLayout* contentLayout) final override;
    virtual void reloadTheme() final override;

    static const QString ACTION_PREFIX;

signals:

    void incomingNotification(uint32_t friendId);
    void outgoingNotification();
    void stopNotification();
    void endCallNotification();
    void rejectCall(uint32_t friendId);
    void acceptCall(uint32_t friendId);

public slots:
    void onAvInvite(uint32_t friendId, bool video);
    void onAvStart(uint32_t friendId, bool video);
    void onAvEnd(uint32_t friendId, bool error);
    void onAvatarChanged(const ToxPk& friendPk, const QPixmap& pic);
    void onFileNameChanged(const ToxPk& friendPk);
    void clearChatArea();

private slots:
    void updateFriendActivity();
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

    void onFriendStatusChanged(quint32 friendId, Status::Status status);
    void onFriendTypingChanged(quint32 friendId, bool isTyping);
    void onFriendNameChanged(const QString& name);
    void onStatusMessage(const QString& message);
    void onUpdateTime();
    void sendImage(const QPixmap& pixmap);
    void doScreenshot();
    void onCopyStatusMessage();

private:
    void updateMuteMicButton();
    void updateMuteVolButton();
    void retranslateUi();
    void showOutgoingCall(bool video);
    void startCounter();
    void stopCounter(bool error = false);
    void updateCallButtons();

protected:
    GenericNetCamView* createNetcam() final override;
    void insertChatMessage(ChatMessage::Ptr msg) final override;
    void dragEnterEvent(QDragEnterEvent* ev) final override;
    void dropEvent(QDropEvent* ev) final override;
    void hideEvent(QHideEvent* event) final override;
    void showEvent(QShowEvent* event) final override;

private:
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
};

#endif // CHATFORM_H
