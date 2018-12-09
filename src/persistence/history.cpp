/*
    Copyright © 2015-2018 by The qTox Project Contributors

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

#include <QDebug>
#include <cassert>

#include "history.h"
#include "profile.h"
#include "settings.h"
#include "db/rawdatabase.h"
#include "src/core/toxpk.h"

/**
 * @class History
 * @brief Interacts with the profile database to save the chat history.
 *
 * @var QHash<QString, int64_t> History::peers
 * @brief Maps Contact persistent id to unique IDs by index.
 * Caches mappings to speed up message saving.
 */

static constexpr int NUM_MESSAGES_DEFAULT = 100; // arbitrary number of messages loaded when not loading by date

/**
 * @brief Prepares the database to work with the history.
 * @param db This database will be prepared for use with the history.
 */
History::History(std::shared_ptr<RawDatabase> db)
    : db(db)
{
    if (!isValid()) {
        qWarning() << "Database not open, init failed";
        return;
    }

    db->execLater(
        "CREATE TABLE IF NOT EXISTS peers (id INTEGER PRIMARY KEY, public_key TEXT NOT NULL "
        "UNIQUE);"
        "CREATE TABLE IF NOT EXISTS aliases (id INTEGER PRIMARY KEY, owner INTEGER,"
        "display_name BLOB NOT NULL, UNIQUE(owner, display_name));"
        "CREATE TABLE IF NOT EXISTS history (id INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL, "
        "chat_id INTEGER NOT NULL, sender_alias INTEGER NOT NULL, "
        "message BLOB NOT NULL);"
        "CREATE TABLE IF NOT EXISTS faux_offline_pending (id INTEGER PRIMARY KEY);");

    // Cache our current peers
    db->execLater(RawDatabase::Query{"SELECT public_key, id FROM peers;",
                                     [this](const QVector<QVariant>& row) {
                                         peers[ToxPk{row[0].toByteArray()}] = row[1].toInt();
                                     }});
}

History::~History()
{
    if (!isValid()) {
        return;
    }

    // We could have execLater requests pending with a lambda attached,
    // so clear the pending transactions first
    db->sync();
}

/**
 * @brief Checks if the database was opened successfully
 * @return True if database if opened, false otherwise.
 */
bool History::isValid()
{
    return db && db->isOpen();
}

/**
 * @brief Checks if a contact has chat history
 * @param contactId
 * @return True if has, false otherwise.
 */
bool History::isHistoryExistence(const ToxPk& contactId)
{
    return !getChatHistoryDefaultNum(contactId).isEmpty();
}

/**
 * @brief Erases all the chat history from the database.
 */
void History::eraseHistory()
{
    if (!isValid()) {
        return;
    }

    db->execNow("DELETE FROM faux_offline_pending;"
                "DELETE FROM history;"
                "DELETE FROM aliases;"
                "DELETE FROM peers;"
                "VACUUM;");
}

/**
 * @brief Erases the chat history with one contact.
 * @param contactId Contact persistent id to erase.
 */
void History::removeContactHistory(const ToxPk& contactId)
{
    if (!isValid()) {
        return;
    }

    if (!peers.contains(contactId)) {
        return;
    }

    int64_t id = peers[contactId];

    QString queryText = QString(
        // remove select chat (friend or group)
        "DELETE FROM faux_offline_pending "
        "WHERE faux_offline_pending.id IN ( "
        "    SELECT faux_offline_pending.id FROM faux_offline_pending "
        "    LEFT JOIN history ON faux_offline_pending.id = history.id "
        "    WHERE chat_id=%1 "
        "); "
        "DELETE FROM history WHERE chat_id=%1; "
        // then if there are peers who no longer author any messages, we don't need their alias
        "DELETE FROM aliases WHERE "
        "aliases.id NOT IN (SELECT DISTINCT sender_alias FROM history); "
        // if it's a friend that now doesn't author any messages (no alias) or a group that now doesn't exist,
        // remove it as a peer
        "DELETE FROM peers WHERE "
        "peers.id NOT IN (SELECT DISTINCT owner FROM aliases UNION SELECT chat_id FROM history); "
        "VACUUM;")
        .arg(id);
    if (db->execNow(queryText)) {
        peers.remove(contactId);
    } else {
        qWarning() << "Failed to remove contact's history";
    }
}

/**
 * @brief Generate query to insert new message in database
 * @param contactId Contact persistent id to save.
 * @param message Message to save.
 * @param sender Sender to save.
 * @param time Time of message sending.
 * @param isSent True if message was already sent.
 * @param dispName Name, which should be displayed.
 * @param insertIdCallback Function, called after query execution.
 */
QVector<RawDatabase::Query>
History::generateNewMessageQueries(const ToxPk& contactId, const QString& message,
                                   const ToxPk& sender, const QDateTime& time, bool isSent,
                                   QString dispName, std::function<void(int64_t)> insertIdCallback)
{
    QVector<RawDatabase::Query> queries;

    // Get the db id of the peer we're chatting with
    int64_t peerId;
    if (peers.contains(contactId)) {
        peerId = peers[contactId];
    } else {
        if (peers.isEmpty()) {
            peerId = 0;
        } else {
            peerId = *std::max_element(peers.begin(), peers.end()) + 1;
        }

        peers[contactId] = peerId;
        queries += RawDatabase::Query(("INSERT INTO peers (id, public_key) "
                                       "VALUES (%1, '"
                                       + contactId.toString() + "');")
                                          .arg(peerId));
    }

    // Get the db id of the sender of the message
    int64_t senderId;
    if (peers.contains(sender)) {
        senderId = peers[sender];
    } else {
        if (peers.isEmpty()) {
            senderId = 0;
        } else {
            senderId = *std::max_element(peers.begin(), peers.end()) + 1;
        }

        peers[sender] = senderId;
        queries += RawDatabase::Query{("INSERT INTO peers (id, public_key) "
                                       "VALUES (%1, '"
                                       + sender.toString() + "');")
                                          .arg(senderId)};
    }

    queries += RawDatabase::Query(
        QString("INSERT OR IGNORE INTO aliases (owner, display_name) VALUES (%1, ?);").arg(senderId),
        {dispName.toUtf8()});

    // If the alias already existed, the insert will ignore the conflict and last_insert_rowid()
    // will return garbage,
    // so we have to check changes() and manually fetch the row ID in this case
    queries +=
        RawDatabase::Query(QString(
                               "INSERT INTO history (timestamp, chat_id, message, sender_alias) "
                               "VALUES (%1, %2, ?, ("
                               "  CASE WHEN changes() IS 0 THEN ("
                               "    SELECT id FROM aliases WHERE owner=%3 AND display_name=?)"
                               "  ELSE last_insert_rowid() END"
                               "));")
                               .arg(time.toMSecsSinceEpoch())
                               .arg(peerId)
                               .arg(senderId),
                           {message.toUtf8(), dispName.toUtf8()}, insertIdCallback);

    if (!isSent) {
        queries += RawDatabase::Query{"INSERT INTO faux_offline_pending (id) VALUES ("
                                      "    last_insert_rowid()"
                                      ");"};
    }

    return queries;
}

/**
 * @brief Saves a chat message in the database.
 * @param contactId Contact persistent key to save.
 * @param message Message to save.
 * @param sender Sender to save.
 * @param time Time of message sending.
 * @param isSent True if message was already sent.
 * @param dispName Name, which should be displayed.
 * @param insertIdCallback Function, called after query execution.
 */
void History::addNewMessage(const ToxPk& contactId, const QString& message, const ToxPk& sender,
                            const QDateTime& time, bool isSent, QString dispName,
                            const std::function<void(int64_t)>& insertIdCallback)
{
    if (!Settings::getInstance().getEnableLogging()) {
        qWarning() << "Blocked a message from being added to database while history is disabled";
        return;
    }
    if (!isValid()) {
        return;
    }

    db->execLater(generateNewMessageQueries(contactId, message, sender, time, isSent, dispName,
                                            insertIdCallback));
}

/**
 * @brief Fetches chat messages from the database.
 * @param contactId Contact persistent key to fetch.
 * @param from Start of period to fetch.
 * @param to End of period to fetch.
 * @return List of messages.
 */
QList<History::HistMessage> History::getChatHistoryFromDate(const ToxPk& contactId, const QDateTime& from,
                                                    const QDateTime& to)
{
    if (!isValid()) {
        return {};
    }
    return getChatHistory(contactId, from, to, 0);
}

/**
 * @brief Fetches the latest set amount of messages from the database.
 * @param contactId Contact persistent id to fetch.
 * @return List of messages.
 */
QList<History::HistMessage> History::getChatHistoryDefaultNum(const ToxPk& contactId)
{
    if (!isValid()) {
        return {};
    }
    return getChatHistory(contactId, QDateTime::fromMSecsSinceEpoch(0), QDateTime::currentDateTime(), NUM_MESSAGES_DEFAULT);
}


/**
 * @brief Fetches chat messages counts for each day from the database.
 * @param contactId Contact persistent id to fetch.
 * @param from Start of period to fetch.
 * @param to End of period to fetch.
 * @return List of structs containing days offset and message count for that day.
 */
QList<History::DateMessages> History::getChatHistoryCounts(const ToxPk& contactId, const QDate& from,
                                                           const QDate& to)
{
    if (!isValid()) {
        return {};
    }
    QDateTime fromTime(from);
    QDateTime toTime(to);

    QList<DateMessages> counts;

    auto rowCallback = [&counts](const QVector<QVariant>& row) {
        DateMessages app;
        app.count = row[0].toUInt();
        app.offsetDays = row[1].toUInt();
        counts.append(app);
    };

    QString queryText =
        QString("SELECT COUNT(history.id), ((timestamp / 1000 / 60 / 60 / 24) - %4 ) AS day "
                "FROM history "
                "JOIN peers chat ON chat_id = chat.id "
                "WHERE timestamp BETWEEN %1 AND %2 AND chat.public_key='%3'"
                "GROUP BY day;")
            .arg(fromTime.toMSecsSinceEpoch())
            .arg(toTime.toMSecsSinceEpoch())
            .arg(contactId.toString())
            .arg(QDateTime::fromMSecsSinceEpoch(0).daysTo(fromTime));

    db->execNow({queryText, rowCallback});

    return counts;
}

/**
 * @brief Search phrase in chat messages
 * @param contactId Contact persistent id
 * @param from a date message where need to start a search
 * @param phrase what need to find
 * @param parameter for search
 * @return date of the message where the phrase was found
 */
QDateTime History::getDateWhereFindPhrase(const ToxPk& contactId, const QDateTime& from, QString phrase, const ParameterSearch& parameter)
{
    QDateTime result;
    auto rowCallback = [&result](const QVector<QVariant>& row) {
        result = QDateTime::fromMSecsSinceEpoch(row[0].toLongLong());
    };

    phrase.replace("'", "''");

    QString message;

    switch (parameter.filter) {
    case FilterSearch::Register:
        message = QStringLiteral("message LIKE '%%1%'").arg(phrase);
        break;
    case FilterSearch::WordsOnly:
        message = QStringLiteral("message REGEXP '%1'").arg(SearchExtraFunctions::generateFilterWordsOnly(phrase).toLower());
        break;
    case FilterSearch::RegisterAndWordsOnly:
        message = QStringLiteral("REGEXPSENSITIVE(message, '%1')").arg(SearchExtraFunctions::generateFilterWordsOnly(phrase));
        break;
    case FilterSearch::Regular:
        message = QStringLiteral("message REGEXP '%1'").arg(phrase);
        break;
    case FilterSearch::RegisterAndRegular:
        message = QStringLiteral("REGEXPSENSITIVE(message '%1')").arg(phrase);
        break;
    default:
        message = QStringLiteral("LOWER(message) LIKE '%%1%'").arg(phrase.toLower());
        break;
    }

    QDateTime date = from;
    if (parameter.period == PeriodSearch::AfterDate || parameter.period == PeriodSearch::BeforeDate) {
        date = QDateTime(parameter.date);
    }

    QString period;
    switch (parameter.period) {
    case PeriodSearch::WithTheFirst:
        period = QStringLiteral("ORDER BY timestamp ASC LIMIT 1;");
        break;
    case PeriodSearch::AfterDate:
        period = QStringLiteral("AND timestamp > '%1' ORDER BY timestamp ASC LIMIT 1;").arg(date.toMSecsSinceEpoch());
        break;
    case PeriodSearch::BeforeDate:
        period = QStringLiteral("AND timestamp < '%1' ORDER BY timestamp DESC LIMIT 1;").arg(date.toMSecsSinceEpoch());
        break;
    default:
        period = QStringLiteral("AND timestamp < '%1' ORDER BY timestamp DESC LIMIT 1;").arg(date.toMSecsSinceEpoch());
        break;
    }

    QString queryText =
        QStringLiteral("SELECT timestamp "
                "FROM history "
                "LEFT JOIN faux_offline_pending ON history.id = faux_offline_pending.id "
                "JOIN peers chat ON chat_id = chat.id "
                "WHERE chat.public_key='%1' "
                "AND %2 "
                "%3")
            .arg(contactId.toString())
            .arg(message)
            .arg(period);

    db->execNow({queryText, rowCallback});

    return result;
}

/**
 * @brief get start date of correspondence
 * @param contactId Contact persistent id
 * @return start date of correspondence
 */
QDateTime History::getStartDateChatHistory(const ToxPk &contactId)
{
    QDateTime result;
    auto rowCallback = [&result](const QVector<QVariant>& row) {
        result = QDateTime::fromMSecsSinceEpoch(row[0].toLongLong());
    };

    QString queryText =
            QStringLiteral("SELECT timestamp "
                    "FROM history "
                    "LEFT JOIN faux_offline_pending ON history.id = faux_offline_pending.id "
                    "JOIN peers chat ON chat_id = chat.id "
                    "WHERE chat.public_key='%1' ORDER BY timestamp ASC LIMIT 1;")
            .arg(contactId.toString());

    db->execNow({queryText, rowCallback});

    return result;
}

/**
 * @brief Marks a message as sent.
 * Removing message from the faux-offline pending messages list.
 *
 * @param id Message ID.
 */
void History::markAsSent(qint64 messageId)
{
    if (!isValid()) {
        return;
    }

    db->execLater(QString("DELETE FROM faux_offline_pending WHERE id=%1;").arg(messageId));
}


/**
 * @brief Fetches chat messages from the database.
 * @param contactId Contact persistent id to fetch.
 * @param from Start of period to fetch.
 * @param to End of period to fetch.
 * @param numMessages max number of messages to fetch.
 * @return List of messages.
 */
QList<History::HistMessage> History::getChatHistory(const ToxPk& contactId, const QDateTime& from,
                                                    const QDateTime& to, int numMessages)
{
    QList<HistMessage> messages;

    auto rowCallback = [&messages](const QVector<QVariant>& row) {
        // dispName and message could have null bytes, QString::fromUtf8
        // truncates on null bytes so we strip them
        messages += {row[0].toLongLong(),
                     row[1].isNull(),
                     QDateTime::fromMSecsSinceEpoch(row[2].toLongLong()),
                     row[3].toString(),
                     QString::fromUtf8(row[4].toByteArray().replace('\0', "")),
                     row[5].toString(),
                     QString::fromUtf8(row[6].toByteArray().replace('\0', ""))};
    };

    // Don't forget to update the rowCallback if you change the selected columns!
    QString queryText =
        QString("SELECT history.id, faux_offline_pending.id, timestamp, "
                "chat.public_key, aliases.display_name, sender.public_key, "
                "message FROM history "
                "LEFT JOIN faux_offline_pending ON history.id = faux_offline_pending.id "
                "JOIN peers chat ON chat_id = chat.id "
                "JOIN aliases ON sender_alias = aliases.id "
                "JOIN peers sender ON aliases.owner = sender.id "
                "WHERE timestamp BETWEEN %1 AND %2 AND chat.public_key='%3'")
            .arg(from.toMSecsSinceEpoch())
            .arg(to.toMSecsSinceEpoch())
            .arg(contactId.toString());
    if (numMessages) {
        queryText = "SELECT * FROM (" + queryText +
                QString(" ORDER BY history.id DESC limit %1) AS T1 ORDER BY T1.id ASC;").arg(numMessages);
    } else {
        queryText = queryText + ";";
    }

    db->execNow({queryText, rowCallback});

    return messages;
}
