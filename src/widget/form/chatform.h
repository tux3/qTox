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

#ifndef CHATFORM_H
#define CHATFORM_H

#include <QSet>
#include <QLabel>
#include <QTimer>
#include <QElapsedTimer>

#include "genericchatform.h"
#include "core/corestructs.h"
#include "widget/tool/screenshotgrabber.h"

class Friend;
class FileTransferInstance;
class QPixmap;
class CallConfirmWidget;
class QHideEvent;
class QMoveEvent;
class OfflineMsgEngine;
class CoreAV;

class ChatForm : public GenericChatForm
{
    Q_OBJECT
public:
    explicit ChatForm(Friend* chatFriend);
    ~ChatForm();
    void setStatusMessage(QString newMessage);
    void loadHistory(QDateTime since, bool processUndelivered = false);

    void dischargeReceipt(int receipt);
    void setFriendTyping(bool isTyping);
    OfflineMsgEngine* getOfflineMsgEngine();

    virtual void show(ContentLayout* contentLayout) final override;

    static const QString ACTION_PREFIX;

signals:
    void aliasChanged(const QString& alias);

public slots:
    void startFileSend(ToxFile file);
    void onFileRecvRequest(ToxFile file);
    void onAvInvite(uint32_t friendId, bool video);
    void onAvStart(uint32_t friendId, bool video);
    void onAvEnd(uint32_t friendId);
    void onAvatarChange(uint32_t friendId, const QPixmap& pic);
    void onAvatarRemoved(uint32_t friendId);

private slots:
    void clearChatArea(bool notInForm) override final;

    void onDeliverOfflineMessages();
    void onLoadChatHistory();
    void onSendTriggered();
    void onTextEditChanged();
    void onAttachClicked();
    void onCallTriggered();
    void onVideoCallTriggered();
    void onAnswerCallTriggered();
    void onRejectCallTriggered();
    void onMicMuteToggle();
    void onVolMuteToggle();

    void onFileSendFailed(uint32_t friendId, const QString &fname);
    void onFriendStatusChanged(quint32 friendId, Status status);
    void onFriendTypingChanged(quint32 friendId, bool isTyping);
    void onFriendNameChanged(const QString& name);
    void onFriendMessageReceived(quint32 friendId, const QString& message,
                                 bool isAction);
    void onStatusMessage(const QString& message);
    void onReceiptReceived(quint32 friendId, int receipt);
    void onLoadHistory();
    void onUpdateTime();
    void onScreenshotClicked();
    void onScreenshotTaken(const QPixmap &pixmap);
    void doScreenshot();
    void onCopyStatusMessage();

private:
    void updateMuteMicButton();
    void updateMuteVolButton();
    void retranslateUi();
    void showOutgoingCall(bool video);
    void startCounter();
    void stopCounter();
    QString secondsToDHMS(quint32 duration);
    void updateCallButtons();
    void SendMessageStr(QString msg);

protected:
    GenericNetCamView* createNetcam() final override;
    void insertChatMessage(ChatMessage::Ptr msg) final override;
    void dragEnterEvent(QDragEnterEvent* ev) final override;
    void dropEvent(QDropEvent* ev) final override;
    void hideEvent(QHideEvent* event) final override;
    void showEvent(QShowEvent* event) final override;

private:

    Friend* f;
    CroppingLabel *statusMessageLabel;
    QMenu statusMessageMenu;
    QLabel *callDuration;
    QTimer *callDurationTimer;
    QTimer typingTimer;
    QElapsedTimer timeElapsed;
    OfflineMsgEngine *offlineEngine;
    QAction* loadHistoryAction;
    QAction* copyStatusAction;

    QHash<uint, FileTransferInstance*> ftransWidgets;
    QPointer<CallConfirmWidget> callConfirm;
    bool isTyping;
};

#endif // CHATFORM_H
