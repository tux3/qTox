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

#ifndef GENERICCHATFORM_H
#define GENERICCHATFORM_H

#include <QWidget>
#include <QPoint>
#include <QDateTime>
#include <QMenu>
#include "core/corestructs.h"
#include "chatlog/chatmessage.h"
#include "core/toxid.h"

/**
 * Spacing in px inserted when the author of the last message changes
 * @note Why the hell is this a thing? surely the different font is enough?
 */
#define AUTHOR_CHANGE_SPACING 5

class QLabel;
class QVBoxLayout;
class QPushButton;
class CroppingLabel;
class ChatTextEdit;
class ChatLog;
class MaskablePixmapWidget;
class Widget;
class FlyoutOverlayWidget;
class ContentLayout;
class QSplitter;
class GenericNetCamView;

namespace Ui {
    class MainWindow;
}

class GenericChatForm : public QWidget
{
    Q_OBJECT
public:
    explicit GenericChatForm(QWidget *parent = 0);
    ~GenericChatForm();

    void setName(const QString &newName);
    virtual void show() final{}
    virtual void show(ContentLayout* contentLayout);

    ChatMessage::Ptr addMessage(const ToxId& author, const QString &message, bool isAction, const QDateTime &datetime, bool isSent);
    ChatMessage::Ptr addSelfMessage(const QString &message, bool isAction, const QDateTime &datetime, bool isSent);

    void addSystemInfoMessage(const QString &message, ChatMessage::SystemMessageType type, const QDateTime &datetime);
    void addAlertMessage(const ToxId& author, QString message, QDateTime datetime);
    bool isEmpty();

    ChatLog* getChatLog() const;
    QDate getLatestDate() const;

signals:
    void sendMessage(uint32_t, QString);
    void sendAction(uint32_t, QString);
    void chatAreaCleared();
    void messageInserted();

public slots:
    void focusInput();

protected slots:
    void onChatContextMenuRequested(QPoint pos);
    void onEmoteButtonClicked();
    void onEmoteInsertRequested(QString str);
    void onSaveLogClicked();
    void onCopyLogClicked();
    virtual void clearChatArea(bool);
    void clearChatArea();
    void onSelectAllClicked();
    void showFileMenu();
    void hideFileMenu();
    void onShowMessagesClicked();
    void onSplitterMoved(int pos, int index);
    void quoteSelectedText();

private:
    void retranslateUi();

protected:
    void showNetcam();
    void hideNetcam();
    virtual GenericNetCamView* createNetcam() = 0;
    QString resolveToxId(const ToxId &id);
    virtual void insertChatMessage(ChatMessage::Ptr msg);
    void adjustFileMenuPosition();
    virtual void hideEvent(QHideEvent* event) override;
    virtual void showEvent(QShowEvent *) override;
    virtual bool event(QEvent *) final override;
    virtual void resizeEvent(QResizeEvent* event) final override;
    virtual bool eventFilter(QObject* object, QEvent* event) final override;

protected:
    QAction* saveChatAction, *clearAction, *quoteAction;
    ToxId previousId;
    QDateTime prevMsgDateTime;
    Widget *parent;
    QMenu menu;
    int curRow;
    CroppingLabel *nameLabel;
    MaskablePixmapWidget *avatar;
    QWidget *headWidget;
    QPushButton *fileButton, *screenshotButton, *emoteButton, *callButton, *videoButton, *volButton, *micButton;
    FlyoutOverlayWidget *fileFlyout;
    QVBoxLayout *headTextLayout;
    ChatTextEdit *msgEdit;
    QPushButton *sendButton;
    ChatLog *chatWidget;
    QDateTime earliestMessage;
    QDateTime historyBaselineDate = QDateTime::currentDateTime(); // used by HistoryKeeper to load messages from t to historyBaselineDate (excluded)
    bool audioInputFlag;
    bool audioOutputFlag;
    QSplitter* bodySplitter;
    GenericNetCamView* netcam;
};

#endif // GENERICCHATFORM_H
