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

#ifndef GENERICCHATFORM_H
#define GENERICCHATFORM_H

#include "src/chatlog/chatmessage.h"
#include "src/core/toxpk.h"
#include "src/widget/searchtypes.h"
#include "src/persistence/history.h"

#include <QMenu>
#include <QWidget>

/**
 * Spacing in px inserted when the author of the last message changes
 * @note Why the hell is this a thing? surely the different font is enough?
 *        - Even a different font is not enough – TODO #1307 ~~zetok
 */

class ChatFormHeader;
class ChatLog;
class ChatTextEdit;
class Contact;
class ContentLayout;
class CroppingLabel;
class FlyoutOverlayWidget;
class GenericNetCamView;
class MaskablePixmapWidget;
class SearchForm;
class Widget;

class QLabel;
class QPushButton;
class QSplitter;
class QToolButton;
class QVBoxLayout;

namespace Ui {
class MainWindow;
}

#ifdef SPELL_CHECKING
namespace Sonnet {
class SpellCheckDecorator;
}
#endif

class GenericChatForm : public QWidget
{
    Q_OBJECT
public:
    explicit GenericChatForm(const Contact* contact, History* history, QWidget* parent = nullptr);
    ~GenericChatForm() override;

    void setName(const QString& newName);
    virtual void show() final
    {
    }
    virtual void show(ContentLayout* contentLayout);

    void addMessage(const ToxPk& author, const QString& message, const QDateTime& datetime,
                    bool isAction, bool colorizeName = false);
    void addSelfMessage(const QString& message, const QDateTime& datetime, bool isAction);
    void addSystemInfoMessage(const QString& message, ChatMessage::SystemMessageType type,
                              const QDateTime& datetime);
    void addAlertMessage(const ToxPk& author, const QString& message, const QDateTime& datetime, bool colorizeName = false);
    static QString resolveToxPk(const ToxPk& pk);
    QDate getLatestDate() const;
    QDate getFirstDate() const;
    void loadHistoryDefaultNum(bool processUndelivered);
    void loadHistoryByDateRange(const QDateTime& since, bool processUndelivered = false);

signals:
    void messageInserted();
    void messageNotFoundShow(SearchDirection direction);

public slots:
    void focusInput();
    void onChatMessageFontChanged(const QFont& font);

protected slots:
    void onChatContextMenuRequested(QPoint pos);
    virtual void onScreenshotClicked() = 0;
    virtual void onSendTriggered() = 0;
    virtual void onAttachClicked() = 0;
    void onEmoteButtonClicked();
    void onEmoteInsertRequested(QString str);
    void onSaveLogClicked();
    void onCopyLogClicked();
    void clearChatArea();
    void clearChatArea(bool confirm, bool inform);
    void onSelectAllClicked();
    void showFileMenu();
    void hideFileMenu();
    void onShowMessagesClicked();
    void onSplitterMoved(int pos, int index);
    void quoteSelectedText();
    void copyLink();
    void searchFormShow();
    void onSearchTriggered();
    void onExportChat();
    void onLoadHistory();
    void searchInBegin(const QString& phrase, const ParameterSearch& parameter);
    void onSearchUp(const QString& phrase, const ParameterSearch& parameter);
    void onSearchDown(const QString& phrase, const ParameterSearch& parameter);
    void onContinueSearch();

private:
    void retranslateUi();
    void addSystemDateMessage();
    QDate getDate(const ChatLine::Ptr& chatLine) const;

protected:
   struct MessageMetadata
    {
        const bool isSelf;
        const bool needSending;
        const bool isAction;
        const qint64 id;
        const ToxPk authorPk;
        const QDateTime msgDateTime;
        MessageMetadata(bool isSelf, bool needSending, bool isAction, qint64 id, ToxPk authorPk,
                        QDateTime msgDateTime)
            : isSelf{isSelf}
            , needSending{needSending}
            , isAction{isAction}
            , id{id}
            , authorPk{authorPk}
            , msgDateTime{msgDateTime}
        {}
    };

    ChatMessage::Ptr createMessage(const ToxPk& author, const QString& message,
                                   const QDateTime& datetime, bool isAction, bool isSent, bool colorizeName = false);
    ChatMessage::Ptr createSelfMessage(const QString& message, const QDateTime& datetime,
                                       bool isAction, bool isSent);
    bool needsToHideName(const ToxPk& messageAuthor, const QDateTime& messageTime) const;
    void showNetcam();
    void hideNetcam();
    void handleLoadedMessages(QList<History::HistMessage> newHistMsgs, bool processUndelivered);
    void insertChatlines(QList<ChatLine::Ptr> chatLines);
    QDate addDateLineIfNeeded(QList<ChatLine::Ptr>& msgs, QDate const& lastDate,
                                    History::HistMessage const& newMessage,
                                    MessageMetadata const& metadata);
    MessageMetadata getMessageMetadata(History::HistMessage const& histMessage);
    ChatMessage::Ptr chatMessageFromHistMessage(History::HistMessage const& histMessage,
                                                      MessageMetadata const& metadata);
    virtual GenericNetCamView* createNetcam() = 0;
    virtual void insertChatMessage(ChatMessage::Ptr msg);
    void adjustFileMenuPosition();
    virtual void hideEvent(QHideEvent* event) override;
    virtual void showEvent(QShowEvent*) override;
    virtual bool event(QEvent*) final override;
    virtual void resizeEvent(QResizeEvent* event) final override;
    virtual bool eventFilter(QObject* object, QEvent* event) final override;
    void disableSearchText();
    bool searchInText(const QString& phrase, const ParameterSearch& parameter, SearchDirection direction);
    std::pair<int, int> indexForSearchInLine(const QString& txt, const QString& phrase, const ParameterSearch& parameter, SearchDirection direction);
    QString getMsgAuthorDispName(const ToxPk& authorPk, const QString& dispName);
    bool loadHistory(const QString& phrase, const ParameterSearch& parameter);

public:
    static const QString ACTION_PREFIX;
protected:
    bool audioInputFlag;
    bool audioOutputFlag;
    int curRow;

    QAction* saveChatAction;
    QAction* clearAction;
    QAction* quoteAction;
    QAction* copyLinkAction;
    QAction* searchAction;

    ToxPk previousId;

    QDateTime prevMsgDateTime;
    QDateTime earliestMessage;

    QMenu menu;

    QPushButton* emoteButton;
    QPushButton* fileButton;
    QPushButton* screenshotButton;
    QPushButton* sendButton;

    QSplitter* bodySplitter;

    ChatFormHeader* headWidget;

    SearchForm *searchForm;
    ChatLog* chatWidget;
    ChatTextEdit* msgEdit;
#ifdef SPELL_CHECKING
    Sonnet::SpellCheckDecorator* decorator{nullptr};
#endif
    FlyoutOverlayWidget* fileFlyout;
    GenericNetCamView* netcam;
    Widget* parent;

    QPoint searchPoint;
    bool searchAfterLoadHistory;
    const Contact* contact;
    History* history;
    QAction* exportChatAction;
    QAction* loadHistoryAction;
};

#endif // GENERICCHATFORM_H
