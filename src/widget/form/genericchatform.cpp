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

#include "genericchatform.h"

#include "src/chatlog/chatlinecontentproxy.h"
#include "src/chatlog/chatlog.h"
#include "src/chatlog/content/filetransferwidget.h"
#include "src/chatlog/content/timestamp.h"
#include "src/core/core.h"
#include "src/friendlist.h"
#include "src/grouplist.h"
#include "src/model/friend.h"
#include "src/model/group.h"
#include "src/persistence/settings.h"
#include "src/persistence/smileypack.h"
#include "src/widget/chatformheader.h"
#include "src/widget/contentdialog.h"
#include "src/widget/contentdialogmanager.h"
#include "src/widget/contentlayout.h"
#include "src/widget/emoticonswidget.h"
#include "src/widget/form/chatform.h"
#include "src/widget/gui.h"
#include "src/widget/maskablepixmapwidget.h"
#include "src/widget/searchform.h"
#include "src/widget/style.h"
#include "src/widget/tool/chattextedit.h"
#include "src/widget/tool/flyoutoverlaywidget.h"
#include "src/widget/translator.h"
#include "src/widget/widget.h"

#include <QClipboard>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStringBuilder>
#include <QtGlobal>

#include <QDebug>

#ifdef SPELL_CHECKING
#include <KF5/SonnetUi/sonnet/spellcheckdecorator.h>
#endif

/**
 * @class GenericChatForm
 * @brief Parent class for all chatforms. It's provide the minimum required UI
 * elements and methods to work with chat messages.
 */

static const QSize FILE_FLYOUT_SIZE{24, 24};
static const short FOOT_BUTTONS_SPACING = 2;
static const short MESSAGE_EDIT_HEIGHT = 50;
static const short MAIN_FOOT_LAYOUT_SPACING = 5;
static const QString FONT_STYLE[]{"normal", "italic", "oblique"};

/**
 * @brief Creates CSS style string for needed class with specified font
 * @param font Font that needs to be represented for a class
 * @param name Class name
 * @return Style string
 */
static QString fontToCss(const QFont& font, const QString& name)
{
    QString result{"%1{"
                   "font-family: \"%2\"; "
                   "font-size: %3px; "
                   "font-style: \"%4\"; "
                   "font-weight: normal;}"};
    return result.arg(name).arg(font.family()).arg(font.pixelSize()).arg(FONT_STYLE[font.style()]);
}

/**
 * @brief Searches for name (possibly alias) of someone with specified public key among all of your
 * friends or groups you are participated
 * @param pk Searched public key
 * @return Name or alias of someone with such public key, or public key string representation if no
 * one was found
 */
QString GenericChatForm::resolveToxPk(const ToxPk& pk)
{
    Friend* f = FriendList::findFriend(pk);
    if (f) {
        return f->getDisplayedName();
    }

    for (Group* it : GroupList::getAllGroups()) {
        QString res = it->resolveToxId(pk);
        if (!res.isEmpty()) {
            return res;
        }
    }

    return pk.toString();
}

namespace
{
const QString STYLE_PATH = QStringLiteral("chatForm/buttons.css");
}

namespace
{

template <class T, class Fun>
QPushButton* createButton(const QString& name, T* self, Fun onClickSlot)
{
    QPushButton* btn = new QPushButton();
    // Fix for incorrect layouts on OS X as per
    // https://bugreports.qt-project.org/browse/QTBUG-14591
    btn->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    btn->setObjectName(name);
    btn->setProperty("state", "green");
    btn->setStyleSheet(Style::getStylesheet(STYLE_PATH));
    QObject::connect(btn, &QPushButton::clicked, self, onClickSlot);
    return btn;
}

ChatMessage::Ptr getChatMessageForIdx(ChatLogIdx idx,
                                      const std::map<ChatLogIdx, ChatMessage::Ptr>& messages)
{
    auto existingMessageIt = messages.find(idx);

    if (existingMessageIt == messages.end()) {
        return ChatMessage::Ptr();
    }

    return existingMessageIt->second;
}

bool shouldRenderDate(ChatLogIdx idxToRender, const IChatLog& chatLog)
{
    if (idxToRender == chatLog.getFirstIdx())
        return true;

    return chatLog.at(idxToRender - 1).getTimestamp().date()
           != chatLog.at(idxToRender).getTimestamp().date();
}

ChatMessage::Ptr dateMessageForItem(const ChatLogItem& item)
{
    const auto& s = Settings::getInstance();
    const auto date = item.getTimestamp().date();
    auto dateText = date.toString(s.getDateFormat());
    return ChatMessage::createChatInfoMessage(dateText, ChatMessage::INFO, QDateTime());
}

ChatMessage::Ptr createMessage(const QString& displayName, bool isSelf, bool colorizeNames,
                               const ChatLogMessage& chatLogMessage)
{
    auto messageType = chatLogMessage.message.isAction ? ChatMessage::MessageType::ACTION
                                                       : ChatMessage::MessageType::NORMAL;

    const bool bSelfMentioned =
        std::any_of(chatLogMessage.message.metadata.begin(), chatLogMessage.message.metadata.end(),
                    [](const MessageMetadata& metadata) {
                        return metadata.type == MessageMetadataType::selfMention;
                    });

    if (bSelfMentioned) {
        messageType = ChatMessage::MessageType::ALERT;
    }

    const auto timestamp = chatLogMessage.message.timestamp;
    return ChatMessage::createChatMessage(displayName, chatLogMessage.message.content, messageType,
                                          isSelf, chatLogMessage.state, timestamp, colorizeNames);
}

void renderMessageRaw(const QString& displayName, bool isSelf, bool colorizeNames,
                   const ChatLogMessage& chatLogMessage, ChatMessage::Ptr& chatMessage)
{

    if (chatMessage) {
        if (chatLogMessage.state == MessageState::complete) {
            chatMessage->markAsDelivered(chatLogMessage.message.timestamp);
        } else if (chatLogMessage.state == MessageState::broken) {
            chatMessage->markAsBroken();
        }
    } else {
        chatMessage = createMessage(displayName, isSelf, colorizeNames, chatLogMessage);
    }
}



ChatLogIdx firstItemAfterDate(QDate date, const IChatLog& chatLog)
{
    auto idxs = chatLog.getDateIdxs(date, 1);
    if (idxs.size()) {
        return idxs[0].idx;
    } else {
        return chatLog.getNextIdx();
    }
}
} // namespace

GenericChatForm::GenericChatForm(const Core& _core, const Contact* contact, IChatLog& chatLog,
                                 IMessageDispatcher& messageDispatcher, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , core{_core}
    , audioInputFlag(false)
    , audioOutputFlag(false)
    , chatLog(chatLog)
    , messageDispatcher(messageDispatcher)
{
    curRow = 0;
    headWidget = new ChatFormHeader();
    searchForm = new SearchForm();
    dateInfo = new QLabel(this);
    chatWidget = new ChatLog(this);
    chatWidget->setBusyNotification(ChatMessage::createBusyNotification());
    searchForm->hide();
    dateInfo->setAlignment(Qt::AlignHCenter);
    dateInfo->setVisible(false);

    // settings
    const Settings& s = Settings::getInstance();
    connect(&s, &Settings::emojiFontPointSizeChanged, chatWidget, &ChatLog::forceRelayout);
    connect(&s, &Settings::chatMessageFontChanged, this, &GenericChatForm::onChatMessageFontChanged);

    msgEdit = new ChatTextEdit();
#ifdef SPELL_CHECKING
    if (s.getSpellCheckingEnabled()) {
        decorator = new Sonnet::SpellCheckDecorator(msgEdit);
    }
#endif

    sendButton = createButton("sendButton", this, &GenericChatForm::onSendTriggered);
    emoteButton = createButton("emoteButton", this, &GenericChatForm::onEmoteButtonClicked);

    fileButton = createButton("fileButton", this, &GenericChatForm::onAttachClicked);
    screenshotButton = createButton("screenshotButton", this, &GenericChatForm::onScreenshotClicked);

    // TODO: Make updateCallButtons (see ChatForm) abstract
    //       and call here to set tooltips.

    fileFlyout = new FlyoutOverlayWidget;
    QHBoxLayout* fileLayout = new QHBoxLayout(fileFlyout);
    fileLayout->addWidget(screenshotButton);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(0);
    fileLayout->setMargin(0);

    msgEdit->setFixedHeight(MESSAGE_EDIT_HEIGHT);
    msgEdit->setFrameStyle(QFrame::NoFrame);

    bodySplitter = new QSplitter(Qt::Vertical, this);
    QWidget* contentWidget = new QWidget(this);
    bodySplitter->addWidget(contentWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout();
    mainLayout->addWidget(bodySplitter);
    mainLayout->setMargin(0);

    setLayout(mainLayout);

    QVBoxLayout* footButtonsSmall = new QVBoxLayout();
    footButtonsSmall->setSpacing(FOOT_BUTTONS_SPACING);
    footButtonsSmall->addWidget(emoteButton);
    footButtonsSmall->addWidget(fileButton);

    QHBoxLayout* mainFootLayout = new QHBoxLayout();
    mainFootLayout->addWidget(msgEdit);
    mainFootLayout->addLayout(footButtonsSmall);
    mainFootLayout->addSpacing(MAIN_FOOT_LAYOUT_SPACING);
    mainFootLayout->addWidget(sendButton);
    mainFootLayout->setSpacing(0);

    QVBoxLayout* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->addWidget(searchForm);
    contentLayout->addWidget(dateInfo);
    contentLayout->addWidget(chatWidget);
    contentLayout->addLayout(mainFootLayout);

    quoteAction = menu.addAction(QIcon(), QString(), this, SLOT(quoteSelectedText()),
                                 QKeySequence(Qt::ALT + Qt::Key_Q));
    addAction(quoteAction);

    menu.addSeparator();

    goCurrentDateAction = menu.addAction(QIcon(), QString(), this, SLOT(goToCurrentDate()),
                                  QKeySequence(Qt::CTRL + Qt::Key_G));
    addAction(goCurrentDateAction);

    menu.addSeparator();

    searchAction = menu.addAction(QIcon(), QString(), this, SLOT(searchFormShow()),
                                  QKeySequence(Qt::CTRL + Qt::Key_F));
    addAction(searchAction);

    menu.addSeparator();

    menu.addActions(chatWidget->actions());
    menu.addSeparator();

    clearAction = menu.addAction(QIcon::fromTheme("edit-clear"), QString(),
                                 this, SLOT(clearChatArea()),
                                 QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_L));
    addAction(clearAction);

    copyLinkAction = menu.addAction(QIcon(), QString(), this, SLOT(copyLink()));
    menu.addSeparator();

    loadHistoryAction = menu.addAction(QIcon(), QString(), this, SLOT(onLoadHistory()));
    exportChatAction =
        menu.addAction(QIcon::fromTheme("document-save"), QString(), this, SLOT(onExportChat()));

    connect(chatWidget, &ChatLog::customContextMenuRequested, this,
            &GenericChatForm::onChatContextMenuRequested);
    connect(chatWidget, &ChatLog::firstVisibleLineChanged, this, &GenericChatForm::updateShowDateInfo);
    connect(chatWidget, &ChatLog::loadHistoryLower, this, &GenericChatForm::loadHistoryLower);
    connect(chatWidget, &ChatLog::loadHistoryUpper, this, &GenericChatForm::loadHistoryUpper);

    connect(searchForm, &SearchForm::searchInBegin, this, &GenericChatForm::searchInBegin);
    connect(searchForm, &SearchForm::searchUp, this, &GenericChatForm::onSearchUp);
    connect(searchForm, &SearchForm::searchDown, this, &GenericChatForm::onSearchDown);
    connect(searchForm, &SearchForm::visibleChanged, this, &GenericChatForm::onSearchTriggered);
    connect(this, &GenericChatForm::messageNotFoundShow, searchForm, &SearchForm::showMessageNotFound);

    connect(&chatLog, &IChatLog::itemUpdated, this, &GenericChatForm::renderMessage);

    connect(msgEdit, &ChatTextEdit::enterPressed, this, &GenericChatForm::onSendTriggered);

    connect(&GUI::getInstance(), &GUI::themeReload, this, &GenericChatForm::reloadTheme);

    reloadTheme();

    fileFlyout->setFixedSize(FILE_FLYOUT_SIZE);
    fileFlyout->setParent(this);
    fileButton->installEventFilter(this);
    fileFlyout->installEventFilter(this);

    retranslateUi();
    Translator::registerHandler(std::bind(&GenericChatForm::retranslateUi, this), this);

    // update header on name/title change
    connect(contact, &Contact::displayedNameChanged, this, &GenericChatForm::setName);

    auto chatLogIdxRange = chatLog.getNextIdx() - chatLog.getFirstIdx();
    auto firstChatLogIdx = (chatLogIdxRange < DEF_NUM_MSG_TO_LOAD) ? chatLog.getFirstIdx() : chatLog.getNextIdx() - DEF_NUM_MSG_TO_LOAD;

    renderMessages(firstChatLogIdx, chatLog.getNextIdx());
}

GenericChatForm::~GenericChatForm()
{
    Translator::unregister(this);
    delete searchForm;
}

void GenericChatForm::renderFile(QString displayName, ToxFile file, bool isSelf, QDateTime timestamp,
                ChatMessage::Ptr& chatMessage)
{
    if (!chatMessage) {
        CoreFile* coreFile = core.getCoreFile();
        assert(coreFile);
        chatMessage = ChatMessage::createFileTransferMessage(displayName, *coreFile, file, isSelf, timestamp);
    } else {
        auto proxy = static_cast<ChatLineContentProxy*>(chatMessage->getContent(1));
        assert(proxy->getWidgetType() == ChatLineContentProxy::FileTransferWidgetType);
        auto ftWidget = static_cast<FileTransferWidget*>(proxy->getWidget());
        ftWidget->onFileTransferUpdate(file);
    }
}

void GenericChatForm::adjustFileMenuPosition()
{
    QPoint pos = fileButton->mapTo(bodySplitter, QPoint());
    QSize size = fileFlyout->size();
    fileFlyout->move(pos.x() - size.width(), pos.y());
}

void GenericChatForm::showFileMenu()
{
    if (!fileFlyout->isShown() && !fileFlyout->isBeingShown()) {
        adjustFileMenuPosition();
    }

    fileFlyout->animateShow();
}

void GenericChatForm::hideFileMenu()
{
    if (fileFlyout->isShown() || fileFlyout->isBeingShown())
        fileFlyout->animateHide();
}

QDateTime GenericChatForm::getLatestTime() const
{
    if (chatLog.getFirstIdx() == chatLog.getNextIdx())
        return QDateTime();

    return chatLog.at(chatLog.getNextIdx() - 1).getTimestamp();
}

void GenericChatForm::reloadTheme()
{
    const Settings& s = Settings::getInstance();
    setStyleSheet(Style::getStylesheet("genericChatForm/genericChatForm.css"));
    msgEdit->setStyleSheet(Style::getStylesheet("msgEdit/msgEdit.css")
                           + fontToCss(s.getChatMessageFont(), "QTextEdit"));

    emoteButton->setStyleSheet(Style::getStylesheet(STYLE_PATH));
    fileButton->setStyleSheet(Style::getStylesheet(STYLE_PATH));
    screenshotButton->setStyleSheet(Style::getStylesheet(STYLE_PATH));
    sendButton->setStyleSheet(Style::getStylesheet(STYLE_PATH));
}

void GenericChatForm::setName(const QString& newName)
{
    headWidget->setName(newName);
}

void GenericChatForm::show(ContentLayout* contentLayout)
{
    contentLayout->mainHead->layout()->addWidget(headWidget);
    headWidget->show();

#if QT_VERSION < QT_VERSION_CHECK(5, 12, 4) && QT_VERSION > QT_VERSION_CHECK(5, 11, 0)
    // HACK: switching order happens to avoid a Qt bug causing segfault, present between these versions.
    // this could cause flickering if our form is shown before added to the layout
    // https://github.com/qTox/qTox/issues/5570
    QWidget::show();
    contentLayout->mainContent->layout()->addWidget(this);
#else
    contentLayout->mainContent->layout()->addWidget(this);
    QWidget::show();
#endif
}

void GenericChatForm::showEvent(QShowEvent*)
{
    msgEdit->setFocus();
    headWidget->showCallConfirm();
}

bool GenericChatForm::event(QEvent* e)
{
    // If the user accidentally starts typing outside of the msgEdit, focus it automatically
    if (e->type() == QEvent::KeyPress) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(e);
        if ((ke->modifiers() == Qt::NoModifier || ke->modifiers() == Qt::ShiftModifier)
                && !ke->text().isEmpty()) {
            if (searchForm->isHidden()) {
                msgEdit->sendKeyEvent(ke);
                msgEdit->setFocus();
            } else {
                searchForm->insertEditor(ke->text());
                searchForm->setFocusEditor();
            }
        }
    }
    return QWidget::event(e);
}

void GenericChatForm::onChatContextMenuRequested(QPoint pos)
{
    QWidget* sender = static_cast<QWidget*>(QObject::sender());
    pos = sender->mapToGlobal(pos);

    // If we right-clicked on a link, give the option to copy it
    bool clickedOnLink = false;
    Text* clickedText = qobject_cast<Text*>(chatWidget->getContentFromGlobalPos(pos));
    if (clickedText) {
        QPointF scenePos = chatWidget->mapToScene(chatWidget->mapFromGlobal(pos));
        QString linkTarget = clickedText->getLinkAt(scenePos);
        if (!linkTarget.isEmpty()) {
            clickedOnLink = true;
            copyLinkAction->setData(linkTarget);
        }
    }
    copyLinkAction->setVisible(clickedOnLink);

    menu.exec(pos);
}

void GenericChatForm::onSendTriggered()
{
    auto msg = msgEdit->toPlainText();

    bool isAction = msg.startsWith(ChatForm::ACTION_PREFIX, Qt::CaseInsensitive);
    if (isAction) {
        msg.remove(0, ChatForm::ACTION_PREFIX.length());
    }

    if (msg.isEmpty()) {
        return;
    }

    msgEdit->setLastMessage(msg);
    msgEdit->clear();

    messageDispatcher.sendMessage(isAction, msg);
}

/**
 * @brief Show, is it needed to hide message author name or not
 * @param idx ChatLogIdx of the message
 * @return True if the name should be hidden, false otherwise
 */
bool GenericChatForm::needsToHideName(ChatLogIdx idx) const
{
    // If the previous message is not rendered we should show the name
    // regardless of other constraints
    auto itemBefore = messages.find(idx - 1);
    if (itemBefore == messages.end()) {
        return false;
    }

    const auto& prevItem = chatLog.at(idx - 1);
    const auto& currentItem = chatLog.at(idx);

    // Always show the * in the name field for action messages
    if (currentItem.getContentType() == ChatLogItem::ContentType::message
        && currentItem.getContentAsMessage().message.isAction) {
        return false;
    }

    qint64 messagesTimeDiff = prevItem.getTimestamp().secsTo(currentItem.getTimestamp());
    return currentItem.getSender() == prevItem.getSender()
           && messagesTimeDiff < chatWidget->repNameAfter;
}

void GenericChatForm::onEmoteButtonClicked()
{
    // don't show the smiley selection widget if there are no smileys available
    if (SmileyPack::getInstance().getEmoticons().empty())
        return;

    EmoticonsWidget widget;
    connect(&widget, SIGNAL(insertEmoticon(QString)), this, SLOT(onEmoteInsertRequested(QString)));
    widget.installEventFilter(this);

    QWidget* sender = qobject_cast<QWidget*>(QObject::sender());
    if (sender) {
        QPoint pos =
            -QPoint(widget.sizeHint().width() / 2, widget.sizeHint().height()) - QPoint(0, 10);
        widget.exec(sender->mapToGlobal(pos));
    }
}

void GenericChatForm::onEmoteInsertRequested(QString str)
{
    // insert the emoticon
    QWidget* sender = qobject_cast<QWidget*>(QObject::sender());
    if (sender)
        msgEdit->insertPlainText(str);

    msgEdit->setFocus(); // refocus so that we can continue typing
}

void GenericChatForm::onCopyLogClicked()
{
    chatWidget->copySelectedText();
}

void GenericChatForm::focusInput()
{
    msgEdit->setFocus();
}

void GenericChatForm::onChatMessageFontChanged(const QFont& font)
{
    // chat log
    chatWidget->fontChanged(font);
    chatWidget->forceRelayout();
    // message editor
    msgEdit->setStyleSheet(Style::getStylesheet("msgEdit/msgEdit.css")
                           + fontToCss(font, "QTextEdit"));
}

void GenericChatForm::setColorizedNames(bool enable)
{
    colorizeNames = enable;
}

void GenericChatForm::addSystemInfoMessage(const QString& message, ChatMessage::SystemMessageType type,
                                           const QDateTime& datetime)
{
    insertChatMessage(ChatMessage::createChatInfoMessage(message, type, datetime));
}

void GenericChatForm::addSystemDateMessage(const QDate& date)
{
    const Settings& s = Settings::getInstance();
    QString dateText = date.toString(s.getDateFormat());

    insertChatMessage(ChatMessage::createChatInfoMessage(dateText, ChatMessage::INFO, QDateTime()));
}

QDateTime GenericChatForm::getTime(const ChatLine::Ptr &chatLine) const
{
    if (chatLine) {
        Timestamp* const timestamp = qobject_cast<Timestamp*>(chatLine->getContent(2));

        if (timestamp) {
            return timestamp->getTime();
        } else {
            return QDateTime();
        }
    }

    return QDateTime();
}

/**
 * @brief GenericChatForm::loadHistory load history
 * @param time start date
 * @param type indicates the direction of loading history
 */
void GenericChatForm::loadHistory(const QDateTime &time, const LoadHistoryDialog::LoadType type)
{
    chatWidget->clear();
    messages.clear();

    if (type == LoadHistoryDialog::from) {
        loadHistoryFrom(time);
        if (!messages.empty())
        {
            auto msg = messages.cbegin()->second;
            chatWidget->setScroll(true);
            chatWidget->scrollToLine(msg);
        }
    } else {
        loadHistoryTo(time);
    }
}

/**
 * @brief GenericChatForm::loadHistoryTo load history before to date "time" or before the first "messages" item
 * @param time start date
 */
void GenericChatForm::loadHistoryTo(const QDateTime &time)
{
    chatWidget->setScroll(false);
    auto end = chatLog.getFirstIdx();
    if (time.isNull()) {
        end = messages.begin()->first;
    } else {
        end = firstItemAfterDate(time.date(), chatLog);
    }

    auto begin = chatLog.getFirstIdx();
    if (end - begin > DEF_NUM_MSG_TO_LOAD) {
        begin = end - DEF_NUM_MSG_TO_LOAD;
    }

    if (begin != end) {
        if (searchResult.found == true && searchResult.pos.logIdx == end) {
            renderMessages(begin, end, [this]{enableSearchText();});
        } else {
            renderMessages(begin, end);
        }
    } else {
        chatWidget->setScroll(true);
    }
}

/**
 * @brief GenericChatForm::loadHistoryFrom load history starting from date "time" or from the last "messages" item
 * @param time start date
 * @return true if function loaded history else false
 */
bool GenericChatForm::loadHistoryFrom(const QDateTime &time)
{
    chatWidget->setScroll(false);
    auto begin = chatLog.getFirstIdx();
    if (time.isNull()) {
        begin = messages.rbegin()->first;
    } else {
        begin = firstItemAfterDate(time.date(), chatLog);
    }

    const auto end = chatLog.getNextIdx() < begin + DEF_NUM_MSG_TO_LOAD
        ? chatLog.getNextIdx()
        : begin + DEF_NUM_MSG_TO_LOAD;

    // The chatLog.getNextIdx() is usually 1 more than the idx on last "messages" item
    // so if we have nothing to load, "add" is equal 1
    if (end - begin <= 1) {
        chatWidget->setScroll(true);
        return false;
    }

    renderMessages(begin, end);

    return true;
}

void GenericChatForm::removeFirstsMessages(const int num)
{
    if (static_cast<int>(messages.size()) > num) {
        messages.erase(messages.begin(), std::next(messages.begin(), num));
    } else {
        messages.clear();
    }
}

void GenericChatForm::removeLastsMessages(const int num)
{
    if (static_cast<int>(messages.size()) > num) {
        messages.erase(std::next(messages.end(), -num), messages.end());
    } else {
        messages.clear();
    }
}


void GenericChatForm::disableSearchText()
{
    auto msgIt = messages.find(searchResult.pos.logIdx);
    if (msgIt != messages.end()) {
        auto text = qobject_cast<Text*>(msgIt->second->getContent(1));
        text->deselectText();
    }
}

void GenericChatForm::enableSearchText()
{
    auto msg = messages.at(searchResult.pos.logIdx);
    chatWidget->scrollToLine(msg);

    auto text = qobject_cast<Text*>(msg->getContent(1));
    text->visibilityChanged(true);
    text->selectText(searchResult.exp, std::make_pair(searchResult.start, searchResult.len));
}

void GenericChatForm::clearChatArea()
{
    clearChatArea(/* confirm = */ true, /* inform = */ true);
}

void GenericChatForm::clearChatArea(bool confirm, bool inform)
{
    if (confirm) {
        QMessageBox::StandardButton mboxResult =
                QMessageBox::question(this, tr("Confirmation"),
                                      tr("Are you sure that you want to clear all displayed messages?"),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (mboxResult == QMessageBox::No) {
            return;
        }
    }

    chatWidget->clear();

    if (inform)
        addSystemInfoMessage(tr("Cleared"), ChatMessage::INFO, QDateTime::currentDateTime());

    messages.clear();
}

void GenericChatForm::onSelectAllClicked()
{
    chatWidget->selectAll();
}

void GenericChatForm::insertChatMessage(ChatMessage::Ptr msg)
{
    chatWidget->insertChatlineAtBottom(std::static_pointer_cast<ChatLine>(msg));
    emit messageInserted();
}

void GenericChatForm::hideEvent(QHideEvent* event)
{
    hideFileMenu();
    QWidget::hideEvent(event);
}

void GenericChatForm::resizeEvent(QResizeEvent* event)
{
    adjustFileMenuPosition();
    QWidget::resizeEvent(event);
}

bool GenericChatForm::eventFilter(QObject* object, QEvent* event)
{
    EmoticonsWidget* ev = qobject_cast<EmoticonsWidget*>(object);
    if (ev && event->type() == QEvent::KeyPress) {
        QKeyEvent* key = static_cast<QKeyEvent*>(event);
        msgEdit->sendKeyEvent(key);
        msgEdit->setFocus();
        return false;
    }

    if (object != this->fileButton && object != this->fileFlyout)
        return false;

    if (!qobject_cast<QWidget*>(object)->isEnabled())
        return false;

    switch (event->type()) {
    case QEvent::Enter:
        showFileMenu();
        break;

    case QEvent::Leave: {
        QPoint flyPos = fileFlyout->mapToGlobal(QPoint());
        QSize flySize = fileFlyout->size();

        QPoint filePos = fileButton->mapToGlobal(QPoint());
        QSize fileSize = fileButton->size();

        QRect region = QRect(flyPos, flySize).united(QRect(filePos, fileSize));

        if (!region.contains(QCursor::pos()))
            hideFileMenu();

        break;
    }

    case QEvent::MouseButtonPress:
        hideFileMenu();
        break;

    default:
        break;
    }

    return false;
}

void GenericChatForm::quoteSelectedText()
{
    QString selectedText = chatWidget->getSelectedText();

    if (selectedText.isEmpty())
        return;

    // forming pretty quote text
    // 1. insert "> " to the begining of quote;
    // 2. replace all possible line terminators with "\n> ";
    // 3. append new line to the end of quote.
    QString quote = selectedText;

    quote.insert(0, "> ");
    quote.replace(QRegExp(QString("\r\n|[\r\n\u2028\u2029]")), QString("\n> "));
    quote.append("\n");

    msgEdit->append(quote);
}

/**
 * @brief Callback of GenericChatForm::copyLinkAction
 */
void GenericChatForm::copyLink()
{
    QString linkText = copyLinkAction->data().toString();
    QApplication::clipboard()->setText(linkText);
}

void GenericChatForm::searchFormShow()
{
    if (searchForm->isHidden()) {
        searchForm->show();
        searchForm->setFocusEditor();
    }
}

void GenericChatForm::onLoadHistory()
{
    LoadHistoryDialog dlg(&chatLog);
    if (dlg.exec()) {
        QDateTime time = dlg.getFromDate();
        auto type = dlg.getLoadType();

        loadHistory(time, type);
    }
}

void GenericChatForm::onExportChat()
{
    QString path = QFileDialog::getSaveFileName(Q_NULLPTR, tr("Save chat log"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QString buffer;
    for (auto i = chatLog.getFirstIdx(); i < chatLog.getNextIdx(); ++i) {
        const auto& item = chatLog.at(i);
        if (item.getContentType() != ChatLogItem::ContentType::message) {
            continue;
        }

        QString timestamp = item.getTimestamp().time().toString("hh:mm:ss");
        QString datestamp = item.getTimestamp().date().toString("yyyy-MM-dd");
        QString author = item.getDisplayName();

        buffer = buffer
                 % QString{datestamp % '\t' % timestamp % '\t' % author % '\t'
                           % item.getContentAsMessage().message.content % '\n'};
    }
    file.write(buffer.toUtf8());
    file.close();
}

void GenericChatForm::onSearchTriggered()
{
    if (searchForm->isHidden()) {
        searchResult.found = false;
        searchForm->removeSearchPhrase();
    }
    disableSearchText();
}

void GenericChatForm::searchInBegin(const QString& phrase, const ParameterSearch& parameter)
{
    if (phrase.isEmpty()) {
        disableSearchText();

        return;
    }

    if (messages.size() == 0) {
        return;
    }

    if (chatLog.getNextIdx() == messages.rbegin()->first + 1) {
        disableSearchText();
    } else {
        goToCurrentDate();
    }

    bool bForwardSearch = false;
    switch (parameter.period) {
    case PeriodSearch::WithTheFirst: {
        bForwardSearch = true;
        searchResult.pos.logIdx = chatLog.getFirstIdx();
        searchResult.pos.numMatches = 0;
        break;
    }
    case PeriodSearch::WithTheEnd:
    case PeriodSearch::None: {
        bForwardSearch = false;
        searchResult.pos.logIdx = chatLog.getNextIdx();
        searchResult.pos.numMatches = 0;
        break;
    }
    case PeriodSearch::AfterDate: {
        bForwardSearch = true;
        searchResult.pos.logIdx = firstItemAfterDate(parameter.time.date(), chatLog);
        searchResult.pos.numMatches = 0;
        break;
    }
    case PeriodSearch::BeforeDate: {
        bForwardSearch = false;
        searchResult.pos.logIdx = firstItemAfterDate(parameter.time.date(), chatLog);
        searchResult.pos.numMatches = 0;
        break;
    }
    }

    if (bForwardSearch) {
        onSearchDown(phrase, parameter);
    } else {
        onSearchUp(phrase, parameter);
    }
}

void GenericChatForm::onSearchUp(const QString& phrase, const ParameterSearch& parameter)
{
    auto result = chatLog.searchBackward(searchResult.pos, phrase, parameter);
    handleSearchResult(result, SearchDirection::Up);
}

void GenericChatForm::onSearchDown(const QString& phrase, const ParameterSearch& parameter)
{
    auto result = chatLog.searchForward(searchResult.pos, phrase, parameter);
    handleSearchResult(result, SearchDirection::Down);
}

void GenericChatForm::handleSearchResult(SearchResult result, SearchDirection direction)
{
    if (!result.found) {
        emit messageNotFoundShow(direction);
        return;
    }

    disableSearchText();

    searchResult = result;

    auto searchIdx = result.pos.logIdx;

    auto firstRenderedIdx = messages.begin()->first;
    auto endRenderedIdx = messages.rbegin()->first;

    if (direction == SearchDirection::Up) {
        if (searchIdx < firstRenderedIdx) {
            if (searchIdx - chatLog.getFirstIdx() > DEF_NUM_MSG_TO_LOAD / 2) {
                firstRenderedIdx = searchIdx - DEF_NUM_MSG_TO_LOAD / 2;
            } else {
                firstRenderedIdx = chatLog.getFirstIdx();
            }
        }

        if (endRenderedIdx - firstRenderedIdx > DEF_NUM_MSG_TO_LOAD) {
            endRenderedIdx = firstRenderedIdx + DEF_NUM_MSG_TO_LOAD;
        }
    } else {
        if (searchIdx < firstRenderedIdx) {
            firstRenderedIdx = searchIdx;
        }

        if (firstRenderedIdx == searchIdx || searchIdx > endRenderedIdx) {
            if (searchIdx + DEF_NUM_MSG_TO_LOAD > chatLog.getNextIdx()) {
                endRenderedIdx = chatLog.getNextIdx();
            } else {
                endRenderedIdx = searchIdx + DEF_NUM_MSG_TO_LOAD;
            }
        }

        if (endRenderedIdx - firstRenderedIdx > DEF_NUM_MSG_TO_LOAD) {
            if (endRenderedIdx - chatLog.getFirstIdx() > DEF_NUM_MSG_TO_LOAD) {
                firstRenderedIdx = endRenderedIdx - DEF_NUM_MSG_TO_LOAD;
            } else {
                firstRenderedIdx = chatLog.getFirstIdx();
            }
        }
    }

    if (!messages.empty() && (firstRenderedIdx < messages.begin()->first
                              || endRenderedIdx > messages.rbegin()->first)) {
        chatWidget->clear();
        messages.clear();

        auto mediator = endRenderedIdx;
        endRenderedIdx = firstRenderedIdx;
        firstRenderedIdx = mediator;
    }

    renderMessages(endRenderedIdx, firstRenderedIdx, [this]{enableSearchText();});
}

void GenericChatForm::renderItem(const ChatLogItem& item, bool hideName, bool colorizeNames, ChatMessage::Ptr& chatMessage)
{
    const auto& sender = item.getSender();

    bool isSelf = sender == core.getSelfId().getPublicKey();

    switch (item.getContentType()) {
    case ChatLogItem::ContentType::message: {
        const auto& chatLogMessage = item.getContentAsMessage();

        renderMessageRaw(item.getDisplayName(), isSelf, colorizeNames, chatLogMessage, chatMessage);

        break;
    }
    case ChatLogItem::ContentType::fileTransfer: {
        const auto& file = item.getContentAsFile();
        renderFile(item.getDisplayName(), file.file, isSelf, item.getTimestamp(), chatMessage);
        break;
    }
    }

    if (hideName) {
        chatMessage->hideSender();
    }
}

void GenericChatForm::renderMessage(ChatLogIdx idx)
{
    renderMessages(idx, idx + 1);
}

void GenericChatForm::renderMessages(ChatLogIdx begin, ChatLogIdx end,
                                     std::function<void(void)> onCompletion)
{
    QList<ChatLine::Ptr> beforeLines;
    QList<ChatLine::Ptr> afterLines;

    for (auto i = begin; i < end; ++i) {
        auto chatMessage = getChatMessageForIdx(i, messages);
        renderItem(chatLog.at(i), needsToHideName(i), colorizeNames, chatMessage);

        if (messages.find(i) == messages.end()) {
            QList<ChatLine::Ptr>* lines =
                (messages.empty() || i > messages.rbegin()->first) ? &afterLines : &beforeLines;

            messages.insert({i, chatMessage});

            if (shouldRenderDate(i, chatLog)) {
                lines->push_back(dateMessageForItem(chatLog.at(i)));
            }
            lines->push_back(chatMessage);
        }
    }

    if (beforeLines.isEmpty() && afterLines.isEmpty()) {
        chatWidget->setScroll(true);
    }

    chatWidget->insertChatlineAtBottom(afterLines);
    if (chatWidget->getNumRemove()) {
        removeFirstsMessages(chatWidget->getNumRemove());
    }

    if (!beforeLines.empty()) {
        // Rendering upwards is expensive and has async behavior for chatWidget.
        // Once rendering completes we call our completion callback once and
        // then disconnect the signal
        if (onCompletion) {
            auto connection = std::make_shared<QMetaObject::Connection>();
            *connection = connect(chatWidget, &ChatLog::workerTimeoutFinished,
                                  [this, onCompletion, connection] {
                                      onCompletion();
                                      this->disconnect(*connection);
                                  });
        }

        chatWidget->insertChatlinesOnTop(beforeLines);
        if (chatWidget->getNumRemove()) {
            removeLastsMessages(chatWidget->getNumRemove());
        }
    } else if (onCompletion) {
        onCompletion();
    }
}

void GenericChatForm::goToCurrentDate()
{
    chatWidget->clear();
    messages.clear();
    auto end = chatLog.getNextIdx();
    auto numMessages = std::min(DEF_NUM_MSG_TO_LOAD, chatLog.getNextIdx() - chatLog.getFirstIdx());
    auto begin = end - numMessages;

    renderMessages(begin, end);
}

/**
 * @brief GenericChatForm::loadHistoryLower load history after scrolling chatlog before first "messages" item
 */
void GenericChatForm::loadHistoryLower()
{
    loadHistoryTo(QDateTime());
}

/**
 * @brief GenericChatForm::loadHistoryUpper load history after scrolling chatlog after last "messages" item
 */
void GenericChatForm::loadHistoryUpper()
{
    if (messages.empty()) {
        return;
    }

    auto msg = messages.crbegin()->second;
    if (loadHistoryFrom(QDateTime())) {
        chatWidget->scrollToLine(msg);
    }
}

void GenericChatForm::updateShowDateInfo(const ChatLine::Ptr& prevLine, const ChatLine::Ptr& topLine)
{
    // If the dateInfo is visible we need to pretend the top line is the one
    // covered by the date to prevent oscillations
    const auto effectiveTopLine = (dateInfo->isVisible() && prevLine)
        ? prevLine : topLine;

    const auto date = getTime(effectiveTopLine);

    if (date.isValid() && date.date() != QDate::currentDate()) {
        const auto dateText = QStringLiteral("<b>%1<\b>").arg(date.toString(Settings::getInstance().getDateFormat()));
        dateInfo->setText(dateText);
        dateInfo->setVisible(true);
    } else {
        dateInfo->setVisible(false);
    }
}

void GenericChatForm::retranslateUi()
{
    sendButton->setToolTip(tr("Send message"));
    emoteButton->setToolTip(tr("Smileys"));
    fileButton->setToolTip(tr("Send file(s)"));
    screenshotButton->setToolTip(tr("Send a screenshot"));
    clearAction->setText(tr("Clear displayed messages"));
    quoteAction->setText(tr("Quote selected text"));
    copyLinkAction->setText(tr("Copy link address"));
    searchAction->setText(tr("Search in text"));
    goCurrentDateAction->setText(tr("Go to current date"));
    loadHistoryAction->setText(tr("Load chat history..."));
    exportChatAction->setText(tr("Export to file"));
}
