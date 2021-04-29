/*
    Copyright © 2019 by The qTox Project Contributors

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

#include "chathistory.h"
#include "src/persistence/settings.h"
#include "src/widget/form/chatform.h"

namespace {
/**
 * @brief Determines if the given idx needs to be loaded from history
 * @param[in] idx index to check
 * @param[in] sessionChatLog SessionChatLog containing currently loaded items
 * @return True if load is needed
 */
bool needsLoadFromHistory(ChatLogIdx idx, const SessionChatLog& sessionChatLog)
{
    return idx < sessionChatLog.getFirstIdx();
}

/**
 * @brief Finds the first item in sessionChatLog that contains a message
 * @param[in] sessionChatLog
 * @return index of first message
 */
ChatLogIdx findFirstMessage(const SessionChatLog& sessionChatLog)
{
    auto it = sessionChatLog.getFirstIdx();
    while (it < sessionChatLog.getNextIdx()) {
        if (sessionChatLog.at(it).getContentType() == ChatLogItem::ContentType::message) {
            return it;
        }
        it++;
    }
    return ChatLogIdx(-1);
}

/**
 * @brief Handles presence of aciton prefix in content
 * @param[in/out] content
 * @return True if was an action
 */
bool handleActionPrefix(QString& content)
{
    // Unfortunately due to legacy reasons we have to continue
    // inserting and parsing for ACTION_PREFIX in our messages even
    // though we have the ability to something more intelligent now
    // that we aren't owned by chatform logic
    auto isAction = content.startsWith(ChatForm::ACTION_PREFIX, Qt::CaseInsensitive);
    if (isAction) {
        content.remove(0, ChatForm::ACTION_PREFIX.size());
    }

    return isAction;
}
} // namespace

ChatHistory::ChatHistory(Friend& f_, History* history_, const ICoreIdHandler& coreIdHandler,
                         const Settings& settings_, IMessageDispatcher& messageDispatcher)
    : f(f_)
    , history(history_)
    , settings(settings_)
    , coreIdHandler(coreIdHandler)
    , sessionChatLog(getInitialChatLogIdx(), coreIdHandler)
{
    connect(&messageDispatcher, &IMessageDispatcher::messageComplete, this,
            &ChatHistory::onMessageComplete);
    connect(&messageDispatcher, &IMessageDispatcher::messageReceived, this,
            &ChatHistory::onMessageReceived);
    connect(&messageDispatcher, &IMessageDispatcher::messageBroken, this,
            &ChatHistory::onMessageBroken);

    if (canUseHistory()) {
        // Defer messageSent callback until we finish firing off all our unsent messages.
        // If it was connected all our unsent messages would be re-added ot history again
        dispatchUnsentMessages(messageDispatcher);
    }

    // Now that we've fired off our unsent messages we can connect the message
    connect(&messageDispatcher, &IMessageDispatcher::messageSent, this, &ChatHistory::onMessageSent);

    // NOTE: this has to be done _after_ sending all sent messages since initial
    // state of the message has to be marked according to our dispatch state
    constexpr auto defaultNumMessagesToLoad = 100;
    auto firstChatLogIdx = sessionChatLog.getFirstIdx().get() < defaultNumMessagesToLoad
                               ? ChatLogIdx(0)
                               : sessionChatLog.getFirstIdx() - defaultNumMessagesToLoad;

    if (canUseHistory()) {
        loadHistoryIntoSessionChatLog(firstChatLogIdx);
    }

    // We don't manage any of the item updates ourselves, we just forward along
    // the underlying sessionChatLog's updates
    connect(&sessionChatLog, &IChatLog::itemUpdated, this, &IChatLog::itemUpdated);
}

const ChatLogItem& ChatHistory::at(ChatLogIdx idx) const
{
    if (canUseHistory()) {
        ensureIdxInSessionChatLog(idx);
    }

    return sessionChatLog.at(idx);
}

SearchResult ChatHistory::searchForward(SearchPos startIdx, const QString& phrase,
                                        const ParameterSearch& parameter) const
{
    if (startIdx.logIdx >= getNextIdx()) {
        SearchResult res;
        res.found = false;
        return res;
    }

    if (canUseHistory()) {
        ensureIdxInSessionChatLog(startIdx.logIdx);
    }

    return sessionChatLog.searchForward(startIdx, phrase, parameter);
}

SearchResult ChatHistory::searchBackward(SearchPos startIdx, const QString& phrase,
                                         const ParameterSearch& parameter) const
{
    auto res = sessionChatLog.searchBackward(startIdx, phrase, parameter);

    if (res.found || !canUseHistory()) {
        return res;
    }

    auto earliestMessage = findFirstMessage(sessionChatLog);

    auto earliestMessageDate =
        (earliestMessage == ChatLogIdx(-1))
            ? QDateTime::currentDateTime()
            : sessionChatLog.at(earliestMessage).getContentAsMessage().message.timestamp;

    // Roundabout way of getting the first idx but I don't want to have to
    // deal with re-implementing so we'll just piece what we want together...
    //
    // If the double disk access is real bad we can optimize this by adding
    // another function to history
    auto dateWherePhraseFound =
        history->getDateWhereFindPhrase(f.getPublicKey(), earliestMessageDate, phrase,
                                        parameter);

    if (dateWherePhraseFound.isValid()) {
        auto loadIdx = history->getNumMessagesForFriendBeforeDate(f.getPublicKey(), dateWherePhraseFound);
        loadHistoryIntoSessionChatLog(ChatLogIdx(loadIdx));

        // Reset search pos to the message we just loaded to avoid a double search
        startIdx.logIdx = ChatLogIdx(loadIdx);
        startIdx.numMatches = 0;
        return sessionChatLog.searchBackward(startIdx, phrase, parameter);
    }

    SearchResult ret;
    ret.found = false;
    return ret;
}

ChatLogIdx ChatHistory::getFirstIdx() const
{
    if (canUseHistory()) {
        return ChatLogIdx(0);
    } else {
        return sessionChatLog.getFirstIdx();
    }
}

ChatLogIdx ChatHistory::getNextIdx() const
{
    return sessionChatLog.getNextIdx();
}

std::vector<IChatLog::DateChatLogIdxPair> ChatHistory::getDateIdxs(const QDate& startDate,
                                                                   size_t maxDates) const
{
    if (canUseHistory()) {
        auto counts = history->getNumMessagesForFriendBeforeDateBoundaries(f.getPublicKey(),
                                                                           startDate, maxDates);

        std::vector<IChatLog::DateChatLogIdxPair> ret;
        std::transform(counts.begin(), counts.end(), std::back_inserter(ret),
                       [&](const History::DateIdx& historyDateIdx) {
                           DateChatLogIdxPair pair;
                           pair.date = historyDateIdx.date;
                           pair.idx.get() = historyDateIdx.numMessagesIn;
                           return pair;
                       });

        // Do not re-search in the session chat log. If we have history the query to the history should have been sufficient
        return ret;
    } else {
        return sessionChatLog.getDateIdxs(startDate, maxDates);
    }
}

void ChatHistory::addSystemMessage(const SystemMessage& message)
{
    if (canUseHistory()) {
        history->addNewSystemMessage(f.getPublicKey(), QDateTime::currentDateTime(), message);
    }

    sessionChatLog.addSystemMessage(message);
}


void ChatHistory::onFileUpdated(const ToxPk& sender, const ToxFile& file)
{
    if (canUseHistory()) {
        switch (file.status) {
        case ToxFile::INITIALIZING: {
            auto selfPk = coreIdHandler.getSelfPublicKey();
            QString username(selfPk == sender ? coreIdHandler.getUsername() : f.getDisplayedName());

            // Note: There is some implcit coupling between history and the current
            // chat log. Both rely on generating a new id based on the state of
            // initializing. If this is changed in the session chat log we'll end up
            // with a different order when loading from history
            history->addNewFileMessage(f.getPublicKey(), file.resumeFileId, file.fileName,
                                       file.filePath, file.filesize, sender,
                                       QDateTime::currentDateTime(), username);
            break;
        }
        case ToxFile::CANCELED:
        case ToxFile::FINISHED:
        case ToxFile::BROKEN: {
            const bool isSuccess = file.status == ToxFile::FINISHED;
            history->setFileFinished(file.resumeFileId, isSuccess, file.filePath,
                                     file.hashGenerator->result());
            break;
        }
        case ToxFile::PAUSED:
        case ToxFile::TRANSMITTING:
        default:
            break;
        }
    }

    sessionChatLog.onFileUpdated(sender, file);
}

void ChatHistory::onFileTransferRemotePausedUnpaused(const ToxPk& sender, const ToxFile& file,
                                                     bool paused)
{
    sessionChatLog.onFileTransferRemotePausedUnpaused(sender, file, paused);
}

void ChatHistory::onFileTransferBrokenUnbroken(const ToxPk& sender, const ToxFile& file, bool broken)
{
    sessionChatLog.onFileTransferBrokenUnbroken(sender, file, broken);
}

void ChatHistory::onMessageReceived(const ToxPk& sender, const Message& message)
{
    if (canUseHistory()) {
        auto friendPk = f.getPublicKey();
        auto displayName = f.getDisplayedName();
        auto content = message.content;
        if (message.isAction) {
            content = ChatForm::ACTION_PREFIX + content;
        }

        history->addNewMessage(friendPk, content, friendPk, message.timestamp, true, message.extensionSet, displayName);
    }

    sessionChatLog.onMessageReceived(sender, message);
}

void ChatHistory::onMessageSent(DispatchedMessageId id, const Message& message)
{
    if (canUseHistory()) {
        auto selfPk = coreIdHandler.getSelfPublicKey();
        auto friendPk = f.getPublicKey();

        auto content = message.content;
        if (message.isAction) {
            content = ChatForm::ACTION_PREFIX + content;
        }

        auto username = coreIdHandler.getUsername();

        auto onInsertion = [this, id](RowId historyId) { handleDispatchedMessage(id, historyId); };

        history->addNewMessage(friendPk, content, selfPk, message.timestamp, false, message.extensionSet, username,
                               onInsertion);
    }

    sessionChatLog.onMessageSent(id, message);
}

void ChatHistory::onMessageComplete(DispatchedMessageId id)
{
    if (canUseHistory()) {
        completeMessage(id);
    }

    sessionChatLog.onMessageComplete(id);
}

void ChatHistory::onMessageBroken(DispatchedMessageId id, BrokenMessageReason reason)
{
    if (canUseHistory()) {
        breakMessage(id, reason);
    }

    sessionChatLog.onMessageBroken(id, reason);
}

/**
 * @brief Forces the given index and all future indexes to be in the chatlog
 * @param[in] idx
 * @note Marked const since this doesn't change _external_ state of the class. We
     still have all the same items at all the same indexes, we've just stuckem
     in ram
 */
void ChatHistory::ensureIdxInSessionChatLog(ChatLogIdx idx) const
{
    if (needsLoadFromHistory(idx, sessionChatLog)) {
        loadHistoryIntoSessionChatLog(idx);
    }
}
/**
 * @brief Unconditionally loads the given index and all future messages that
 * are not in the session chat log into the session chat log
 * @param[in] idx
 * @note Marked const since this doesn't change _external_ state of the class. We
   still have all the same items at all the same indexes, we've just stuckem
   in ram
 * @note no end idx as we always load from start -> latest. In the future we
 * could have a less contiguous history
 */
void ChatHistory::loadHistoryIntoSessionChatLog(ChatLogIdx start) const
{
    if (!needsLoadFromHistory(start, sessionChatLog)) {
        return;
    }

    auto end = sessionChatLog.getFirstIdx();

    // We know that both history and us have a start index of 0 so the type
    // conversion should be safe
    assert(getFirstIdx() == ChatLogIdx(0));
    auto messages = history->getMessagesForFriend(f.getPublicKey(), start.get(), end.get());

    assert(messages.size() == static_cast<int>(end.get() - start.get()));
    ChatLogIdx nextIdx = start;

    for (const auto& message : messages) {
        // Note that message.id is _not_ a valid conversion here since it is a
        // global id not a per-chat id like the ChatLogIdx
        auto currentIdx = nextIdx++;
        auto sender = ToxId(message.sender).getPublicKey();
        switch (message.content.getType()) {
        case HistMessageContentType::file: {
            const auto date = message.timestamp;
            const auto file = message.content.asFile();
            const auto chatLogFile = ChatLogFile{date, file};
            sessionChatLog.insertFileAtIdx(currentIdx, sender, message.dispName, chatLogFile);
            break;
        }
        case HistMessageContentType::message: {
            auto messageContent = message.content.asMessage();

            auto isAction = handleActionPrefix(messageContent);

            // It's okay to skip the message processor here. The processor is
            // meant to convert between boundaries of our internal
            // representation. We already had to go through the processor before
            // we hit IMessageDispatcher's signals which history listens for.
            // Items added to history have already been sent so we know they already
            // reflect what was sent/received.
            auto processedMessage = Message{isAction, messageContent, message.timestamp};

            auto dispatchedMessageIt =
                std::find_if(dispatchedMessageRowIdMap.begin(), dispatchedMessageRowIdMap.end(),
                             [&](RowId dispatchedId) { return dispatchedId == message.id; });

            assert((message.state != MessageState::pending && dispatchedMessageIt == dispatchedMessageRowIdMap.end()) ||
                   (message.state == MessageState::pending && dispatchedMessageIt != dispatchedMessageRowIdMap.end()));

            auto chatLogMessage = ChatLogMessage{message.state, processedMessage};
            switch (message.state) {
                case MessageState::complete:
                    sessionChatLog.insertCompleteMessageAtIdx(currentIdx, sender, message.dispName,
                                                              chatLogMessage);
                    break;
                case MessageState::pending:
                    sessionChatLog.insertIncompleteMessageAtIdx(currentIdx, sender, message.dispName,
                                                                chatLogMessage, dispatchedMessageIt.key());
                    break;
                case MessageState::broken:
                    sessionChatLog.insertBrokenMessageAtIdx(currentIdx, sender, message.dispName,
                                                            chatLogMessage);
                    break;
            }
            break;
        }
        case HistMessageContentType::system: {
            const auto& systemMessage = message.content.asSystemMessage();
            sessionChatLog.insertSystemMessageAtIdx(currentIdx, systemMessage);
            break;
        }
        }
    }

    assert(nextIdx == end);
}

/**
 * @brief Sends any unsent messages in history to the underlying message dispatcher
 * @param[in] messageDispatcher
 */
void ChatHistory::dispatchUnsentMessages(IMessageDispatcher& messageDispatcher)
{
    auto unsentMessages = history->getUndeliveredMessagesForFriend(f.getPublicKey());

    auto requiredExtensions = std::accumulate(
        unsentMessages.begin(), unsentMessages.end(),
        ExtensionSet(), [] (const ExtensionSet& a, const History::HistMessage& b) {
            return a | b.extensionSet;
        });

    for (auto& message : unsentMessages) {
        // We should only store messages as unsent, if this changes in the
        // future we need to extend this logic
        assert(message.content.getType() == HistMessageContentType::message);

        auto messageContent = message.content.asMessage();
        auto isAction = handleActionPrefix(messageContent);

        // NOTE: timestamp will be generated in messageDispatcher but we haven't
        // hooked up our history callback so it will not be shown in our chatlog
        // with the new timestamp. This is intentional as everywhere else we use
        // attempted send time (which is whenever the it was initially inserted
        // into history
        auto dispatchId = requiredExtensions.none()
            // We should only send a single message, but in the odd case where we end
            // up having to split more than when we added the message to history we'll
            // just associate the last dispatched id with the history message
            ? messageDispatcher.sendMessage(isAction, messageContent).second
            : messageDispatcher.sendExtendedMessage(messageContent, requiredExtensions).second;

        handleDispatchedMessage(dispatchId, message.id);

        // We don't add the messages to the underlying chatlog since
        // 1. We don't even know the ChatLogIdx of this message
        // 2. We only want to display the latest N messages on boot by default,
        //    even if there are more than N messages that haven't been sent
    }
}

void ChatHistory::handleDispatchedMessage(DispatchedMessageId dispatchId, RowId historyId)
{
    auto completedMessageIt = completedMessages.find(dispatchId);
    auto brokenMessageIt = brokenMessages.find(dispatchId);

    const auto isCompleted = completedMessageIt != completedMessages.end();
    const auto isBroken = brokenMessageIt != brokenMessages.end();
    assert(!(isCompleted && isBroken));

    if (isCompleted) {
        history->markAsDelivered(historyId);
        completedMessages.erase(completedMessageIt);
    } else if (isBroken) {
        history->markAsBroken(historyId, brokenMessageIt.value());
        brokenMessages.erase(brokenMessageIt);
    } else {
        dispatchedMessageRowIdMap.insert(dispatchId, historyId);
    }
}

void ChatHistory::completeMessage(DispatchedMessageId id)
{
    auto dispatchedMessageIt = dispatchedMessageRowIdMap.find(id);

    if (dispatchedMessageIt == dispatchedMessageRowIdMap.end()) {
        completedMessages.insert(id);
    } else {
        history->markAsDelivered(*dispatchedMessageIt);
        dispatchedMessageRowIdMap.erase(dispatchedMessageIt);
    }
}

void ChatHistory::breakMessage(DispatchedMessageId id, BrokenMessageReason reason)
{
    auto dispatchedMessageIt = dispatchedMessageRowIdMap.find(id);

    if (dispatchedMessageIt == dispatchedMessageRowIdMap.end()) {
        brokenMessages.insert(id, reason);
    } else {
        history->markAsBroken(*dispatchedMessageIt, reason);
        dispatchedMessageRowIdMap.erase(dispatchedMessageIt);
    }
}

bool ChatHistory::canUseHistory() const
{
    return history && settings.getEnableLogging();
}

/**
 * @brief Gets the initial chat log index for a sessionChatLog with 0 items loaded from history.
 * Needed to keep history indexes in sync with chat log indexes
 * @param[in] history
 * @param[in] f
 * @return Initial chat log index
 */
ChatLogIdx ChatHistory::getInitialChatLogIdx() const
{
    if (canUseHistory()) {
        return ChatLogIdx(history->getNumMessagesForFriend(f.getPublicKey()));
    }
    return ChatLogIdx(0);
}
