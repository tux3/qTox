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

#include "sessionchatlog.h"
#include "src/friendlist.h"

#include <QDebug>
#include <QtGlobal>
#include <mutex>

namespace {

/**
 * lower_bound needs two way comparisons. This adaptor allows us to compare
 * between a Message and QDateTime in both directions
 */
struct MessageDateAdaptor
{
    static const QDateTime invalidDateTime;
    MessageDateAdaptor(const std::pair<const ChatLogIdx, ChatLogItem>& item)
        : timestamp(item.second.getContentType() == ChatLogItem::ContentType::message
                        ? item.second.getContentAsMessage().message.timestamp
                        : invalidDateTime)
    {}

    MessageDateAdaptor(const QDateTime& timestamp)
        : timestamp(timestamp)
    {}

    const QDateTime& timestamp;
};

const QDateTime MessageDateAdaptor::invalidDateTime;

/**
 * @brief The search types all can be represented as some regular expression. This function
 *   takes the input phrase and filter and generates the appropriate regular expression
 * @return Regular expression which finds the input
 */
QRegularExpression getRegexpForPhrase(const QString& phrase, FilterSearch filter)
{
    constexpr auto regexFlags = QRegularExpression::UseUnicodePropertiesOption;
    constexpr auto caseInsensitiveFlags = QRegularExpression::CaseInsensitiveOption;

    switch (filter) {
    case FilterSearch::Register:
        return QRegularExpression(QRegularExpression::escape(phrase), regexFlags);
    case FilterSearch::WordsOnly:
        return QRegularExpression(SearchExtraFunctions::generateFilterWordsOnly(phrase),
                                  caseInsensitiveFlags);
    case FilterSearch::RegisterAndWordsOnly:
        return QRegularExpression(SearchExtraFunctions::generateFilterWordsOnly(phrase), regexFlags);
    case FilterSearch::RegisterAndRegular:
        return QRegularExpression(phrase, regexFlags);
    case FilterSearch::Regular:
        return QRegularExpression(phrase, caseInsensitiveFlags);
    default:
        return QRegularExpression(QRegularExpression::escape(phrase), caseInsensitiveFlags);
    }
}

/**
 * @return True if the given status indicates no future updates will come in
 */
bool toxFileIsComplete(ToxFile::FileStatus status)
{
    switch (status) {
    case ToxFile::INITIALIZING:
    case ToxFile::PAUSED:
    case ToxFile::TRANSMITTING:
        return false;
    case ToxFile::BROKEN:
    case ToxFile::CANCELED:
    case ToxFile::FINISHED:
    default:
        return true;
    }
}

std::map<ChatLogIdx, ChatLogItem>::const_iterator
firstItemAfterDate(QDate date, const std::map<ChatLogIdx, ChatLogItem>& items)
{
    return std::lower_bound(items.begin(), items.end(), QDateTime(date),
                            [](const MessageDateAdaptor& a, MessageDateAdaptor const& b) {
                                return a.timestamp.date() < b.timestamp.date();
                            });
}
} // namespace

SessionChatLog::SessionChatLog(const ICoreIdHandler& coreIdHandler)
    : coreIdHandler(coreIdHandler)
{}

/**
 * @brief Alternate constructor that allows for an initial index to be set
 */
SessionChatLog::SessionChatLog(ChatLogIdx initialIdx, const ICoreIdHandler& coreIdHandler)
    : coreIdHandler(coreIdHandler)
    , nextIdx(initialIdx)
{}

SessionChatLog::~SessionChatLog() = default;

const ChatLogItem& SessionChatLog::at(ChatLogIdx idx) const
{
    auto item = items.find(idx);
    if (item == items.end())
        std::terminate();

    return item->second;
}

SearchResult SessionChatLog::searchForward(SearchPos startPos, const QString& phrase,
                                           const ParameterSearch& parameter) const
{
    if (startPos.logIdx >= getNextIdx()) {
        SearchResult res;
        res.found = false;
        return res;
    }

    auto currentPos = startPos;

    auto regexp = getRegexpForPhrase(phrase, parameter.filter);

    for (auto it = items.find(currentPos.logIdx); it != items.end(); ++it) {
        const auto& key = it->first;
        const auto& item = it->second;

        if (item.getContentType() != ChatLogItem::ContentType::message)
            continue;

        const auto& content = item.getContentAsMessage();

        auto match = regexp.globalMatch(content.message.content, 0);

        auto numMatches = 0;
        QRegularExpressionMatch lastMatch;
        while (match.isValid() && numMatches <= currentPos.numMatches && match.hasNext()) {
            lastMatch = match.next();
            numMatches++;
        }

        if (numMatches > currentPos.numMatches) {
            SearchResult res;
            res.found = true;
            res.pos.logIdx = key;
            res.pos.numMatches = numMatches;
            res.start = lastMatch.capturedStart();
            res.len = lastMatch.capturedLength();
            return res;
        }

        // After the first iteration we force this to 0 to search the whole
        // message
        currentPos.numMatches = 0;
    }

    // We should have returned from the above loop if we had found anything
    SearchResult ret;
    ret.found = false;
    return ret;
}

SearchResult SessionChatLog::searchBackward(SearchPos startPos, const QString& phrase,
                                            const ParameterSearch& parameter) const
{
    auto currentPos = startPos;
    auto regexp = getRegexpForPhrase(phrase, parameter.filter);
    auto startIt = items.find(currentPos.logIdx);

    // If we don't have it we'll start at the end
    if (startIt == items.end()) {
        startIt = std::prev(items.end());
        startPos.numMatches = 0;
    }

    // Off by 1 due to reverse_iterator api
    auto rStartIt = std::reverse_iterator<decltype(startIt)>(std::next(startIt));
    auto rEnd = std::reverse_iterator<decltype(startIt)>(items.begin());

    for (auto it = rStartIt; it != rEnd; ++it) {
        const auto& key = it->first;
        const auto& item = it->second;

        if (item.getContentType() != ChatLogItem::ContentType::message)
            continue;

        const auto& content = item.getContentAsMessage();
        auto match = regexp.globalMatch(content.message.content, 0);

        auto totalMatches = 0;
        auto numMatchesBeforePos = 0;
        QRegularExpressionMatch lastMatch;
        while (match.isValid() && match.hasNext()) {
            auto currentMatch = match.next();
            totalMatches++;
            if (currentPos.numMatches == 0 || currentPos.numMatches > numMatchesBeforePos) {
                lastMatch = currentMatch;
                numMatchesBeforePos++;
            }
        }

        if ((numMatchesBeforePos < currentPos.numMatches || currentPos.numMatches == 0)
            && numMatchesBeforePos > 0) {
            SearchResult res;
            res.found = true;
            res.pos.logIdx = key;
            res.pos.numMatches = numMatchesBeforePos;
            res.start = lastMatch.capturedStart();
            res.len = lastMatch.capturedLength();
            return res;
        }

        // After the first iteration we force this to 0 to search the whole
        // message
        currentPos.numMatches = 0;
    }

    // We should have returned from the above loop if we had found anything
    SearchResult ret;
    ret.found = false;
    return ret;
}

ChatLogIdx SessionChatLog::getFirstIdx() const
{
    if (items.empty())
        return nextIdx;

    return items.begin()->first;
}

ChatLogIdx SessionChatLog::getNextIdx() const
{
    return nextIdx;
}

std::vector<IChatLog::DateChatLogIdxPair> SessionChatLog::getDateIdxs(const QDate& startDate,
                                                                      size_t maxDates) const
{
    std::vector<DateChatLogIdxPair> ret;
    auto dateIt = startDate;

    while (true) {
        auto it = firstItemAfterDate(dateIt, items);

        if (it == items.end())
            break;

        DateChatLogIdxPair pair;
        pair.date = dateIt;
        pair.idx = it->first;

        ret.push_back(std::move(pair));

        dateIt = dateIt.addDays(1);
        if (startDate.daysTo(dateIt) > maxDates && maxDates != 0)
            break;
    }

    return ret;
}

void SessionChatLog::insertMessageAtIdx(ChatLogIdx idx, ToxPk sender, QString senderName,
                                        ChatLogMessage message)
{
    auto item = ChatLogItem(sender, message);

    if (!senderName.isEmpty())
        item.setDisplayName(senderName);

    items.emplace(idx, std::move(item));
}

void SessionChatLog::insertFileAtIdx(ChatLogIdx idx, ToxPk sender, QString senderName, ChatLogFile file)
{
    auto item = ChatLogItem(sender, file);

    if (!senderName.isEmpty())
        item.setDisplayName(senderName);

    items.emplace(idx, std::move(item));
}

/**
 * @brief Inserts message data into the chatlog buffer
 * @note Owner of SessionChatLog is in charge of attaching this to the appropriate IMessageDispatcher
 */
void SessionChatLog::onMessageReceived(const ToxPk& sender, const Message& message)
{
    auto messageIdx = nextIdx++;

    ChatLogMessage chatLogMessage;
    chatLogMessage.isComplete = true;
    chatLogMessage.message = message;
    items.emplace(messageIdx, ChatLogItem(sender, chatLogMessage));

    emit this->itemUpdated(messageIdx);
}

/**
 * @brief Inserts message data into the chatlog buffer
 * @note Owner of SessionChatLog is in charge of attaching this to the appropriate IMessageDispatcher
 */
void SessionChatLog::onMessageSent(DispatchedMessageId id, const Message& message)
{
    auto messageIdx = nextIdx++;

    ChatLogMessage chatLogMessage;
    chatLogMessage.isComplete = false;
    chatLogMessage.message = message;
    items.emplace(messageIdx, ChatLogItem(coreIdHandler.getSelfPublicKey(), chatLogMessage));

    outgoingMessages.insert(id, messageIdx);

    emit this->itemUpdated(messageIdx);
}

/**
 * @brief Marks the associated message as complete and notifies any listeners
 * @note Owner of SessionChatLog is in charge of attaching this to the appropriate IMessageDispatcher
 */
void SessionChatLog::onMessageComplete(DispatchedMessageId id)
{
    auto chatLogIdxIt = outgoingMessages.find(id);

    if (chatLogIdxIt == outgoingMessages.end()) {
        qWarning() << "Failed to find outgoing message";
        return;
    }

    const auto& chatLogIdx = *chatLogIdxIt;
    auto messageIt = items.find(chatLogIdx);

    if (messageIt == items.end()) {
        qWarning() << "Failed to look up message in chat log";
        return;
    }

    messageIt->second.getContentAsMessage().isComplete = true;

    emit this->itemUpdated(messageIt->first);
}

/**
 * @brief Updates file state in the chatlog
 * @note The files need to be pre-filtered for the current chat since we do no validation
 * @note This should be attached to any CoreFile signal that fits the signature
 */
void SessionChatLog::onFileUpdated(const ToxPk& sender, const ToxFile& file)
{
    auto fileIt =
        std::find_if(currentFileTransfers.begin(), currentFileTransfers.end(),
                     [&](const CurrentFileTransfer& transfer) { return transfer.file == file; });

    ChatLogIdx messageIdx;
    if (fileIt == currentFileTransfers.end() && file.status == ToxFile::INITIALIZING) {
        assert(file.status == ToxFile::INITIALIZING);
        CurrentFileTransfer currentTransfer;
        currentTransfer.file = file;
        currentTransfer.idx = nextIdx++;
        currentFileTransfers.push_back(currentTransfer);

        const auto chatLogFile = ChatLogFile{QDateTime::currentDateTime(), file};
        items.emplace(currentTransfer.idx, ChatLogItem(sender, chatLogFile));
        messageIdx = currentTransfer.idx;
    } else if (fileIt != currentFileTransfers.end()) {
        messageIdx = fileIt->idx;
        fileIt->file = file;

        items.at(messageIdx).getContentAsFile().file = file;
    } else {
        // This may be a file unbroken message that we don't handle ATM
        return;
    }

    if (toxFileIsComplete(file.status)) {
        currentFileTransfers.erase(fileIt);
    }

    emit this->itemUpdated(messageIdx);
}

void SessionChatLog::onFileTransferRemotePausedUnpaused(const ToxPk& sender, const ToxFile& file,
                                                        bool /*paused*/)
{
    onFileUpdated(sender, file);
}

void SessionChatLog::onFileTransferBrokenUnbroken(const ToxPk& sender, const ToxFile& file,
                                                  bool /*broken*/)
{
    onFileUpdated(sender, file);
}
