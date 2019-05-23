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
#include "src/persistence/history.h"
#include "src/model/status.h"
#include "src/widget/form/loadhistorydialog.h"
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
    ChatForm(Friend* chatFriend, History* history);
    ~ChatForm();
    void setStatusMessage(const QString& newMessage);
    void loadHistoryByDateRange(const QDateTime& since, LoadHistoryDialog::LoadType loadType = LoadHistoryDialog::from,
                                bool processUndelivered = false);
    void loadHistoryDefaultNum(bool processUndelivered = false);

    void dischargeReceipt(int receipt);
    void setFriendTyping(bool isTyping);
    OfflineMsgEngine* getOfflineMsgEngine();

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
    void startFileSend(ToxFile file);
    void onFileTransferFinished(ToxFile file);
    void onFileTransferCancelled(ToxFile file);
    void onFileTransferBrokenUnbroken(ToxFile file, bool broken);
    void onFileRecvRequest(ToxFile file);
    void onAvInvite(uint32_t friendId, bool video);
    void onAvStart(uint32_t friendId, bool video);
    void onAvEnd(uint32_t friendId, bool error);
    void onAvatarChanged(const ToxPk& friendPk, const QPixmap& pic);
    void onFileNameChanged(const ToxPk& friendPk);
    void clearChatArea();

protected slots:
    void loadHistoryLower() override final;
    void loadHistoryUpper() override final;
    void searchInBegin(const QString& phrase, const ParameterSearch& parameter) override;
    void onSearchUp(const QString& phrase, const ParameterSearch& parameter) override;
    void onSearchDown(const QString& phrase, const ParameterSearch& parameter) override;

private slots:
    void onSendTriggered() override;
    void onAttachClicked() override;
    void onScreenshotClicked() override;

    void onTextEditChanged();
    void onCallTriggered();
    void onVideoCallTriggered();
    void onAnswerCallTriggered(bool video);
    void onRejectCallTriggered();
    void onMicMuteToggle();
    void onVolMuteToggle();

    void onFileSendFailed(uint32_t friendId, const QString& fname);
    void onFriendStatusChanged(quint32 friendId, Status::Status status);
    void onFriendTypingChanged(quint32 friendId, bool isTyping);
    void onFriendNameChanged(const QString& name);
    void onFriendMessageReceived(quint32 friendId, const QString& message, bool isAction);
    void onStatusMessage(const QString& message);
    void onReceiptReceived(quint32 friendId, ReceiptNum receipt);
    void onLoadHistory();
    void onUpdateTime();
    void sendImage(const QPixmap& pixmap);
    void doScreenshot();
    void onCopyStatusMessage();
    void onExportChat();

private:
    struct MessageMetadata
    {
        const bool isSelf;
        const bool needSending;
        const bool isAction;
        const RowId id;
        const ToxPk authorPk;
        const QDateTime msgDateTime;
        MessageMetadata(bool isSelf, bool needSending, bool isAction, RowId id, ToxPk authorPk,
                        QDateTime msgDateTime)
            : isSelf{isSelf}
            , needSending{needSending}
            , isAction{isAction}
            , id{id}
            , authorPk{authorPk}
            , msgDateTime{msgDateTime}
        {}
    };
    void handleLoadedMessages(QList<History::HistMessage> newHistMsgs, bool processUndelivered, bool onTop);
    QDate addDateLineIfNeeded(QList<ChatLine::Ptr>& msgs, QDate const& lastDate,
                              History::HistMessage const& newMessage, MessageMetadata const& metadata);
    MessageMetadata getMessageMetadata(History::HistMessage const& histMessage);
    ChatMessage::Ptr chatMessageFromHistMessage(History::HistMessage const& histMessage,
                                                MessageMetadata const& metadata);
    void sendLoadedMessage(ChatMessage::Ptr chatMsg, MessageMetadata const& metadata);
    void insertChatlines(QList<ChatLine::Ptr> chatLines, bool onTop);
    void updateMuteMicButton();
    void updateMuteVolButton();
    void retranslateUi();
    void showOutgoingCall(bool video);
    void startCounter();
    void stopCounter(bool error = false);
    void updateCallButtons();
    void SendMessageStr(QString msg);
    bool loadHistory(const QString& phrase, const ParameterSearch& parameter);
    QList<History::HistMessage> deleteDuplicate(QList<History::HistMessage> histMessage, const bool onTop);

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
    OfflineMsgEngine* offlineEngine;
    QAction* loadHistoryAction;
    QAction* copyStatusAction;
    QAction* exportChatAction;

    History* history;
    QHash<uint, FileTransferInstance*> ftransWidgets;
    bool isTyping;
    bool lastCallIsVideo;
};

#endif // CHATFORM_H
