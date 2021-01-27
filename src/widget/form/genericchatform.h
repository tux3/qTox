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

#include "src/chatlog/chatmessage.h"
#include "src/core/toxpk.h"
#include "src/model/ichatlog.h"
#include "src/widget/form/loadhistorydialog.h"
#include "src/widget/searchtypes.h"

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

class IMessageDispatcher;
struct Message;

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
    GenericChatForm(const Core& _core, const Contact* contact, IChatLog& chatLog,
                    IMessageDispatcher& messageDispatcher, QWidget* parent = nullptr);
    ~GenericChatForm() override;

    void setName(const QString& newName);
    virtual void show(ContentLayout* contentLayout);

    void addSystemInfoMessage(const QDateTime& datetime, SystemMessageType messageType,
                              SystemMessage::Args messageArgs);
    static QString resolveToxPk(const ToxPk& pk);
    QDateTime getLatestTime() const;

signals:
    void messageInserted();
    void messageNotFoundShow(SearchDirection direction);

public slots:
    void focusInput();
    void onChatMessageFontChanged(const QFont& font);
    void setColorizedNames(bool enable);
    virtual void reloadTheme();

protected slots:
    void onChatContextMenuRequested(QPoint pos);
    virtual void onScreenshotClicked() = 0;
    void onSendTriggered();
    virtual void onAttachClicked() = 0;
    void onEmoteButtonClicked();
    void onEmoteInsertRequested(QString str);
    void onCopyLogClicked();
    void clearChatArea();
    void clearChatArea(bool confirm, bool inform);
    void onSelectAllClicked();
    void showFileMenu();
    void hideFileMenu();
    void quoteSelectedText();
    void copyLink();
    void onLoadHistory();
    void onExportChat();
    void searchFormShow();
    void onSearchTriggered();
    void updateShowDateInfo(const ChatLine::Ptr& prevLine, const ChatLine::Ptr& topLine);

    void searchInBegin(const QString& phrase, const ParameterSearch& parameter);
    void onSearchUp(const QString& phrase, const ParameterSearch& parameter);
    void onSearchDown(const QString& phrase, const ParameterSearch& parameter);
    void handleSearchResult(SearchResult result, SearchDirection direction);
    void renderMessage(ChatLogIdx idx);
    void renderMessages(ChatLogIdx begin, ChatLogIdx end,
                        std::function<void(void)> onCompletion = std::function<void(void)>());
    void goToCurrentDate();

    void loadHistoryLower();
    void loadHistoryUpper();

private:
    void retranslateUi();
    void addSystemDateMessage(const QDate& date);
    QDateTime getTime(const ChatLine::Ptr& chatLine) const;
    void loadHistory(const QDateTime& time, const LoadHistoryDialog::LoadType type);
    void loadHistoryTo(const QDateTime& time);
    bool loadHistoryFrom(const QDateTime& time);
    void removeFirstsMessages(const int num);
    void removeLastsMessages(const int num);

    void renderItem(const ChatLogItem &item, bool hideName, bool colorizeNames, ChatMessage::Ptr &chatMessage);
    void renderFile(QString displayName, ToxFile file, bool isSelf, QDateTime timestamp, ChatMessage::Ptr &chatMessage);
protected:
    ChatMessage::Ptr createMessage(const ToxPk& author, const QString& message,
                                   const QDateTime& datetime, bool isAction, bool isSent, bool colorizeName = false);
    bool needsToHideName(ChatLogIdx idx) const;
    virtual void insertChatMessage(ChatMessage::Ptr msg);
    void adjustFileMenuPosition();
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent*) override;
    bool event(QEvent*) final;
    void resizeEvent(QResizeEvent* event) final;
    bool eventFilter(QObject* object, QEvent* event) final;
    void disableSearchText();
    void enableSearchText();
    bool searchInText(const QString& phrase, const ParameterSearch& parameter, SearchDirection direction);
    std::pair<int, int> indexForSearchInLine(const QString& txt, const QString& phrase, const ParameterSearch& parameter, SearchDirection direction);

protected:
    const Core& core;
    bool audioInputFlag;
    bool audioOutputFlag;
    int curRow;

    QAction* clearAction;
    QAction* quoteAction;
    QAction* copyLinkAction;
    QAction* searchAction;
    QAction* loadHistoryAction;
    QAction* exportChatAction;
    QAction* goCurrentDateAction;

    QMenu menu;

    QPushButton* emoteButton;
    QPushButton* fileButton;
    QPushButton* screenshotButton;
    QPushButton* sendButton;

    QSplitter* bodySplitter;

    ChatFormHeader* headWidget;

    SearchForm *searchForm;
    QLabel *dateInfo;
    ChatLog* chatWidget;
    ChatTextEdit* msgEdit;
#ifdef SPELL_CHECKING
    Sonnet::SpellCheckDecorator* decorator{nullptr};
#endif
    FlyoutOverlayWidget* fileFlyout;
    Widget* parent;

    IChatLog& chatLog;
    IMessageDispatcher& messageDispatcher;
    SearchResult searchResult;
    std::map<ChatLogIdx, ChatMessage::Ptr> messages;
    bool colorizeNames = false;
};
