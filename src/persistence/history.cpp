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

namespace {
static constexpr int SCHEMA_VERSION = 1;

void generateCurrentSchema(QVector<RawDatabase::Query>& queries)
{
    queries += RawDatabase::Query(QStringLiteral(
        "CREATE TABLE peers (id INTEGER PRIMARY KEY, "
        "public_key TEXT NOT NULL UNIQUE);"
        "CREATE TABLE aliases (id INTEGER PRIMARY KEY, "
        "owner INTEGER, "
        "display_name BLOB NOT NULL, "
        "UNIQUE(owner, display_name));"
        "CREATE TABLE history "
        "(id INTEGER PRIMARY KEY, "
        "timestamp INTEGER NOT NULL, "
        "chat_id INTEGER NOT NULL, "
        "sender_alias INTEGER NOT NULL, "
        // even though technically a message can be null for file transfer, we've opted
        // to just insert an empty string when there's no content, this moderately simplifies
        // implementating to leakon as currently our database doesn't have support for optional
        // fields. We would either have to insert "?" or "null" based on if message exists and then
        // ensure that our blob vector always has the right number of fields. Better to just
        // leave this as NOT NULL for now.
        "message BLOB NOT NULL, "
        "file_id INTEGER);"
        "CREATE TABLE file_transfers "
        "(id INTEGER PRIMARY KEY, "
        "chat_id INTEGER NOT NULL, "
        "file_restart_id BLOB NOT NULL, "
        "file_name BLOB NOT NULL, "
        "file_path BLOB NOT NULL, "
        "file_hash BLOB NOT NULL, "
        "file_size INTEGER NOT NULL, "
        "direction INTEGER NOT NULL, "
        "file_state INTEGER NOT NULL);"
        "CREATE TABLE faux_offline_pending (id INTEGER PRIMARY KEY);"));
}

bool isNewDb(std::shared_ptr<RawDatabase> db)
{
    bool newDb;
    if (!db->execNow(RawDatabase::Query("SELECT COUNT(*) FROM sqlite_master;",
                                        [&](const QVector<QVariant>& row) {
                                            newDb = row[0].toLongLong() == 0;
                                        }))) {
        db.reset();
        return false; // TODO: propogate error
    }
    return newDb;
}

void dbSchema0to1(std::shared_ptr<RawDatabase> db, QVector<RawDatabase::Query>& queries)
{
    queries +=
        RawDatabase::Query(QStringLiteral(
            "CREATE TABLE file_transfers "
            "(id INTEGER PRIMARY KEY, "
            "chat_id INTEGER NOT NULL, "
            "file_restart_id BLOB NOT NULL, "
            "file_name BLOB NOT NULL, "
            "file_path BLOB NOT NULL, "
            "file_hash BLOB NOT NULL, "
            "file_size INTEGER NOT NULL, "
            "direction INTEGER NOT NULL, "
            "file_state INTEGER NOT NULL);"));
    queries +=
        RawDatabase::Query(QStringLiteral("ALTER TABLE history ADD file_id INTEGER;"));
}

/**
* @brief Upgrade the db schema
* @note On future alterations of the database all you have to do is bump the SCHEMA_VERSION
* variable and add another case to the switch statement below. Make sure to fall through on each case.
*/
void dbSchemaUpgrade(std::shared_ptr<RawDatabase> db)
{
    int64_t databaseSchemaVersion;

    if (!db->execNow(RawDatabase::Query("PRAGMA user_version", [&](const QVector<QVariant>& row) {
            databaseSchemaVersion = row[0].toLongLong();
        }))) {
        qCritical() << "History failed to read user_version";
        db.reset();
        return;
    }

    if (databaseSchemaVersion > SCHEMA_VERSION) {
        qWarning() << "Database version is newer than we currently support. Please upgrade qTox";
        // We don't know what future versions have done, we have to disable db access until we re-upgrade
        db.reset();
        return;
    } else if (databaseSchemaVersion == SCHEMA_VERSION) {
        // No work to do
        return;
    }

    QVector<RawDatabase::Query> queries;
    // Make sure to handle the un-created case as well in the following upgrade code
    switch (databaseSchemaVersion) {
    case 0:
        // Note: 0 is a special version that is actually two versions.
        //   possibility 1) it is a newly created database and it neesds the current schema to be created.
        //   possibility 2) it is a old existing database, before version 1 and before we saved schema version,
        //       and need to be updated.
        if (isNewDb(db)) {
            generateCurrentSchema(queries);
            queries += RawDatabase::Query(QStringLiteral("PRAGMA user_version = %1;").arg(SCHEMA_VERSION));
            db->execLater(queries);
            qDebug() << "Database created at schema version" << SCHEMA_VERSION;
            break; // new db is the only case where we don't incrementally upgrade through each version
        } else {
            dbSchema0to1(db, queries);
        }
        // fallthrough
    // case 1:
    //    dbSchema1to2(queries);
    //    //fallthrough
    // etc.
    default:
        queries += RawDatabase::Query(QStringLiteral("PRAGMA user_version = %1;").arg(SCHEMA_VERSION));
        db->execLater(queries);
        qDebug() << "Database upgrade finished (databaseSchemaVersion" << databaseSchemaVersion
                << "->" << SCHEMA_VERSION << ")";
    }
}
} // namespace

/**
 * @class History
 * @brief Interacts with the profile database to save the chat history.
 *
 * @var QHash<QString, int64_t> History::peers
 * @brief Maps friend public keys to unique IDs by index.
 * Caches mappings to speed up message saving.
 */

static constexpr int NUM_MESSAGES_DEFAULT =
    100; // arbitrary number of messages loaded when not loading by date

FileDbInsertionData::FileDbInsertionData()
{
    static int id = qRegisterMetaType<FileDbInsertionData>();
    (void)id;
}

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

    dbSchemaUpgrade(db);

    // dbSchemaUpgrade may have put us in an invalid state
    if (!isValid()) {
        return;
    }

    connect(this, &History::fileInsertionReady, this, &History::onFileInsertionReady);
    connect(this, &History::fileInserted, this, &History::onFileInserted);

    // Cache our current peers
    db->execLater(RawDatabase::Query{"SELECT public_key, id FROM peers;",
                                     [this](const QVector<QVariant>& row) {
                                         peers[row[0].toString()] = row[1].toInt();
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
 * @brief Checks if a friend has chat history
 * @param friendPk
 * @return True if has, false otherwise.
 */
bool History::historyExists(const ToxPk& friendPk)
{
    return !getMessagesForFriend(friendPk, 0, 1).empty();
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
                "DELETE FROM file_transfers;"
                "VACUUM;");
}

/**
 * @brief Erases the chat history with one friend.
 * @param friendPk Friend public key to erase.
 */
void History::removeFriendHistory(const QString& friendPk)
{
    if (!isValid()) {
        return;
    }

    if (!peers.contains(friendPk)) {
        return;
    }

    int64_t id = peers[friendPk];

    QString queryText = QString("DELETE FROM faux_offline_pending "
                                "WHERE faux_offline_pending.id IN ( "
                                "    SELECT faux_offline_pending.id FROM faux_offline_pending "
                                "    LEFT JOIN history ON faux_offline_pending.id = history.id "
                                "    WHERE chat_id=%1 "
                                "); "
                                "DELETE FROM history WHERE chat_id=%1; "
                                "DELETE FROM aliases WHERE owner=%1; "
                                "DELETE FROM peers WHERE id=%1; "
                                "DELETE FROM file_transfers WHERE chat_id=%1;"
                                "VACUUM;")
                            .arg(id);

    if (db->execNow(queryText)) {
        peers.remove(friendPk);
    } else {
        qWarning() << "Failed to remove friend's history";
    }
}

/**
 * @brief Generate query to insert new message in database
 * @param friendPk Friend publick key to save.
 * @param message Message to save.
 * @param sender Sender to save.
 * @param time Time of message sending.
 * @param isSent True if message was already sent.
 * @param dispName Name, which should be displayed.
 * @param insertIdCallback Function, called after query execution.
 */
QVector<RawDatabase::Query>
History::generateNewMessageQueries(const QString& friendPk, const QString& message,
                                   const QString& sender, const QDateTime& time, bool isSent,
                                   QString dispName, std::function<void(RowId)> insertIdCallback)
{
    QVector<RawDatabase::Query> queries;

    // Get the db id of the peer we're chatting with
    int64_t peerId;
    if (peers.contains(friendPk)) {
        peerId = (peers)[friendPk];
    } else {
        if (peers.isEmpty()) {
            peerId = 0;
        } else {
            peerId = *std::max_element(peers.begin(), peers.end()) + 1;
        }

        (peers)[friendPk] = peerId;
        queries += RawDatabase::Query(("INSERT INTO peers (id, public_key) "
                                       "VALUES (%1, '"
                                       + friendPk + "');")
                                          .arg(peerId));
    }

    // Get the db id of the sender of the message
    int64_t senderId;
    if (peers.contains(sender)) {
        senderId = (peers)[sender];
    } else {
        if (peers.isEmpty()) {
            senderId = 0;
        } else {
            senderId = *std::max_element(peers.begin(), peers.end()) + 1;
        }

        (peers)[sender] = senderId;
        queries += RawDatabase::Query{("INSERT INTO peers (id, public_key) "
                                       "VALUES (%1, '"
                                       + sender + "');")
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

void History::onFileInsertionReady(FileDbInsertionData data)
{

    QVector<RawDatabase::Query> queries;
    std::weak_ptr<History> weakThis = shared_from_this();

    // peerId is guaranteed to be inserted since we just used it in addNewMessage
    auto peerId = peers[data.friendPk];
    // Copy to pass into labmda for later
    auto fileId = data.fileId;
    queries +=
        RawDatabase::Query(QStringLiteral(
                               "INSERT INTO file_transfers (chat_id, file_restart_id, "
                               "file_path, file_name, file_hash, file_size, direction, file_state) "
                               "VALUES (%1, ?, ?, ?, ?, %2, %3, %4);")
                               .arg(peerId)
                               .arg(data.size)
                               .arg(static_cast<int>(data.direction))
                               .arg(ToxFile::CANCELED),
                           {data.fileId.toUtf8(), data.filePath.toUtf8(), data.fileName.toUtf8(), QByteArray()},
                           [weakThis, fileId](RowId id) {
                               auto pThis = weakThis.lock();
                               if (pThis) {
                                   emit pThis->fileInserted(id, fileId);
                               }
                           });


    queries += RawDatabase::Query(QStringLiteral("UPDATE history "
                                                 "SET file_id = (last_insert_rowid()) "
                                                 "WHERE id = %1")
                                      .arg(data.historyId.get()));

    db->execLater(queries);
}

void History::onFileInserted(RowId dbId, QString fileId)
{
    auto& fileInfo = fileInfos[fileId];
    if (fileInfo.finished) {
        db->execLater(
            generateFileFinished(dbId, fileInfo.success, fileInfo.filePath, fileInfo.fileHash));
        fileInfos.remove(fileId);
    } else {
        fileInfo.finished = false;
        fileInfo.fileId = dbId;
    }
}

RawDatabase::Query History::generateFileFinished(RowId id, bool success, const QString& filePath,
                                                 const QByteArray& fileHash)
{
    auto file_state = success ? ToxFile::FINISHED : ToxFile::CANCELED;
    if (filePath.length()) {
        return RawDatabase::Query(QStringLiteral("UPDATE file_transfers "
                                                 "SET file_state = %1, file_path = ?, file_hash = ?"
                                                 "WHERE id = %2")
                                      .arg(file_state)
                                      .arg(id.get()),
                                  {filePath.toUtf8(), fileHash});
    } else {
        return RawDatabase::Query(QStringLiteral("UPDATE file_transfers "
                                                 "SET finished = %1 "
                                                 "WHERE id = %2")
                                      .arg(file_state)
                                      .arg(id.get()));
    }
}

void History::addNewFileMessage(const QString& friendPk, const QString& fileId,
                                const QString& fileName, const QString& filePath, int64_t size,
                                const QString& sender, const QDateTime& time, QString const& dispName)
{
    // This is an incredibly far from an optimal way of implementing this,
    // but given the frequency that people are going to be initiating a file
    // transfer we can probably live with it.

    // Since both inserting an alias for a user and inserting a file transfer
    // will generate new ids, there is no good way to inject both new ids into the
    // history query without refactoring our RawDatabase::Query and processor loops.

    // What we will do instead is chain callbacks to try to get reasonable behavior.
    // We can call the generateNewMessageQueries() fn to insert a message with an empty
    // message in it, and get the id with the callbck. Once we have the id we can ammend
    // the data to have our newly inserted file_id as well

    ToxFile::FileDirection direction;
    if (sender == friendPk) {
        direction = ToxFile::RECEIVING;
    } else {
        direction = ToxFile::SENDING;
    }

    std::weak_ptr<History> weakThis = shared_from_this();
    FileDbInsertionData insertionData;
    insertionData.friendPk = friendPk;
    insertionData.fileId = fileId;
    insertionData.fileName = fileName;
    insertionData.filePath = filePath;
    insertionData.size = size;
    insertionData.direction = direction;

    auto insertFileTransferFn = [weakThis, insertionData](RowId messageId) {
        auto insertionDataRw = std::move(insertionData);

        insertionDataRw.historyId = messageId;

        auto thisPtr = weakThis.lock();
        if (thisPtr)
            emit thisPtr->fileInsertionReady(std::move(insertionDataRw));
    };

    addNewMessage(friendPk, "", sender, time, true, dispName, insertFileTransferFn);
}

/**
 * @brief Saves a chat message in the database.
 * @param friendPk Friend publick key to save.
 * @param message Message to save.
 * @param sender Sender to save.
 * @param time Time of message sending.
 * @param isSent True if message was already sent.
 * @param dispName Name, which should be displayed.
 * @param insertIdCallback Function, called after query execution.
 */
void History::addNewMessage(const QString& friendPk, const QString& message, const QString& sender,
                            const QDateTime& time, bool isSent, QString dispName,
                            const std::function<void(RowId)>& insertIdCallback)
{
    if (!Settings::getInstance().getEnableLogging()) {
        qWarning() << "Blocked a message from being added to database while history is disabled";
        return;
    }
    if (!isValid()) {
        return;
    }

    db->execLater(generateNewMessageQueries(friendPk, message, sender, time, isSent, dispName,
                                            insertIdCallback));
}

void History::setFileFinished(const QString& fileId, bool success, const QString& filePath,
                              const QByteArray& fileHash)
{
    auto& fileInfo = fileInfos[fileId];
    if (fileInfo.fileId.get() == -1) {
        fileInfo.finished = true;
        fileInfo.success = success;
        fileInfo.filePath = filePath;
        fileInfo.fileHash = fileHash;
    } else {
        db->execLater(generateFileFinished(fileInfo.fileId, success, filePath, fileHash));
    }

    fileInfos.remove(fileId);
}

size_t History::getNumMessagesForFriend(const ToxPk& friendPk)
{
    return getNumMessagesForFriendBeforeDate(friendPk,
                                             // Maximum possible time
                                             QDateTime::fromMSecsSinceEpoch(
                                                 std::numeric_limits<int64_t>::max()));
}

size_t History::getNumMessagesForFriendBeforeDate(const ToxPk& friendPk, const QDateTime& date)
{
    QString queryText = QString("SELECT COUNT(history.id) "
                                "FROM history "
                                "JOIN peers chat ON chat_id = chat.id "
                                "WHERE chat.public_key='%1'"
                                "AND timestamp < %2;")
                            .arg(friendPk.toString())
                            .arg(date.toMSecsSinceEpoch());

    size_t numMessages = 0;
    auto rowCallback = [&numMessages](const QVector<QVariant>& row) {
        numMessages = row[0].toLongLong();
    };

    db->execNow({queryText, rowCallback});

    return numMessages;
}

QList<History::HistMessage> History::getMessagesForFriend(const ToxPk& friendPk, size_t firstIdx,
                                                          size_t lastIdx)
{
    QList<HistMessage> messages;

    // Don't forget to update the rowCallback if you change the selected columns!
    QString queryText =
        QString("SELECT history.id, faux_offline_pending.id, timestamp, "
                "chat.public_key, aliases.display_name, sender.public_key, "
                "message, file_transfers.file_restart_id, "
                "file_transfers.file_path, file_transfers.file_name, "
                "file_transfers.file_size, file_transfers.direction, "
                "file_transfers.file_state FROM history "
                "LEFT JOIN faux_offline_pending ON history.id = faux_offline_pending.id "
                "JOIN peers chat ON history.chat_id = chat.id "
                "JOIN aliases ON sender_alias = aliases.id "
                "JOIN peers sender ON aliases.owner = sender.id "
                "LEFT JOIN file_transfers ON history.file_id = file_transfers.id "
                "WHERE chat.public_key='%1' "
                "LIMIT %2 OFFSET %3;")
            .arg(friendPk.toString())
            .arg(lastIdx - firstIdx)
            .arg(firstIdx);

    auto rowCallback = [&messages](const QVector<QVariant>& row) {
        // dispName and message could have null bytes, QString::fromUtf8
        // truncates on null bytes so we strip them
        auto id = RowId{row[0].toLongLong()};
        auto isOfflineMessage = row[1].isNull();
        auto timestamp = QDateTime::fromMSecsSinceEpoch(row[2].toLongLong());
        auto friend_key = row[3].toString();
        auto display_name = QString::fromUtf8(row[4].toByteArray().replace('\0', ""));
        auto sender_key = row[5].toString();
        if (row[7].isNull()) {
            messages += {id,           isOfflineMessage, timestamp,        friend_key,
                         display_name, sender_key,       row[6].toString()};
        } else {
            ToxFile file;
            file.fileKind = TOX_FILE_KIND_DATA;
            file.resumeFileId = row[7].toString().toUtf8();
            file.filePath = row[8].toString();
            file.fileName = row[9].toString();
            file.filesize = row[10].toLongLong();
            file.direction = static_cast<ToxFile::FileDirection>(row[11].toLongLong());
            file.status = static_cast<ToxFile::FileStatus>(row[12].toInt());
            messages +=
                {id, isOfflineMessage, timestamp, friend_key, display_name, sender_key, file};
        }
    };

    db->execNow({queryText, rowCallback});

    return messages;
}

QList<History::HistMessage> History::getUnsentMessagesForFriend(const ToxPk& friendPk)
{
    auto queryText =
        QString("SELECT history.id, faux_offline_pending.id, timestamp, chat.public_key, "
                "aliases.display_name, sender.public_key, message "
                "FROM history "
                "JOIN faux_offline_pending ON history.id = faux_offline_pending.id "
                "JOIN peers chat on chat.public_key = '%1' "
                "JOIN aliases on sender_alias = aliases.id "
                "JOIN peers sender on aliases.owner = sender.id;")
            .arg(friendPk.toString());

    QList<History::HistMessage> ret;
    auto rowCallback = [&ret](const QVector<QVariant>& row) {
        // dispName and message could have null bytes, QString::fromUtf8
        // truncates on null bytes so we strip them
        auto id = RowId{row[0].toLongLong()};
        auto isOfflineMessage = row[1].isNull();
        auto timestamp = QDateTime::fromMSecsSinceEpoch(row[2].toLongLong());
        auto friend_key = row[3].toString();
        auto display_name = QString::fromUtf8(row[4].toByteArray().replace('\0', ""));
        auto sender_key = row[5].toString();
        if (row[6].isNull()) {
            ret += {id,           isOfflineMessage, timestamp,        friend_key,
                    display_name, sender_key,       row[6].toString()};
        }
    };

    db->execNow({queryText, rowCallback});

    return ret;
}

/**
 * @brief Search phrase in chat messages
 * @param friendPk Friend public key
 * @param from a date message where need to start a search
 * @param phrase what need to find
 * @param parameter for search
 * @return date of the message where the phrase was found
 */
QDateTime History::getDateWhereFindPhrase(const QString& friendPk, const QDateTime& from,
                                          QString phrase, const ParameterSearch& parameter)
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
        message = QStringLiteral("message REGEXP '%1'")
                      .arg(SearchExtraFunctions::generateFilterWordsOnly(phrase).toLower());
        break;
    case FilterSearch::RegisterAndWordsOnly:
        message = QStringLiteral("REGEXPSENSITIVE(message, '%1')")
                      .arg(SearchExtraFunctions::generateFilterWordsOnly(phrase));
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

    if (!date.isValid()) {
        date = QDateTime::currentDateTime();
    }

    if (parameter.period == PeriodSearch::AfterDate || parameter.period == PeriodSearch::BeforeDate) {
        date = QDateTime(parameter.date);
    }

    QString period;
    switch (parameter.period) {
    case PeriodSearch::WithTheFirst:
        period = QStringLiteral("ORDER BY timestamp ASC LIMIT 1;");
        break;
    case PeriodSearch::AfterDate:
        period = QStringLiteral("AND timestamp > '%1' ORDER BY timestamp ASC LIMIT 1;")
                     .arg(date.toMSecsSinceEpoch());
        break;
    case PeriodSearch::BeforeDate:
        period = QStringLiteral("AND timestamp < '%1' ORDER BY timestamp DESC LIMIT 1;")
                     .arg(date.toMSecsSinceEpoch());
        break;
    default:
        period = QStringLiteral("AND timestamp < '%1' ORDER BY timestamp DESC LIMIT 1;")
                     .arg(date.toMSecsSinceEpoch());
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
            .arg(friendPk)
            .arg(message)
            .arg(period);

    db->execNow({queryText, rowCallback});

    return result;
}

QList<History::DateIdx> History::getNumMessagesForFriendBeforeDateBoundaries(const ToxPk& friendPk,
                                                                             const QDate& from,
                                                                             size_t maxNum)
{
    auto friendPkString = friendPk.toString();

    // No guarantee that this is the most efficient way to do this...
    // We want to count messages that happened for a friend before a
    // certain date. We do this by re-joining our table a second time
    // but this time with the only filter being that our id is less than
    // the ID of the corresponding row in the table that is grouped by day
    auto countMessagesForFriend =
        QString("SELECT COUNT(*) - 1 " // Count - 1 corresponds to 0 indexed message id for friend
                "FROM history countHistory "            // Import unfiltered table as countHistory
                "JOIN peers chat ON chat_id = chat.id " // link chat_id to chat.id
                "WHERE chat.public_key = '%1'"          // filter this conversation
                "AND countHistory.id <= history.id") // and filter that our unfiltered table history id only has elements up to history.id
            .arg(friendPkString);

    auto limitString = (maxNum) ? QString("LIMIT %1").arg(maxNum) : QString("");

    auto queryString = QString("SELECT (%1), (timestamp / 1000 / 60 / 60 / 24) AS day "
                               "FROM history "
                               "JOIN peers chat ON chat_id = chat.id "
                               "WHERE chat.public_key = '%2' "
                               "AND timestamp >= %3 "
                               "GROUP by day "
                               "%4;")
                           .arg(countMessagesForFriend)
                           .arg(friendPkString)
                           .arg(QDateTime(from).toMSecsSinceEpoch())
                           .arg(limitString);

    QList<DateIdx> dateIdxs;
    auto rowCallback = [&dateIdxs](const QVector<QVariant>& row) {
        DateIdx dateIdx;
        dateIdx.numMessagesIn = row[0].toLongLong();
        dateIdx.date =
            QDateTime::fromMSecsSinceEpoch(row[1].toLongLong() * 24 * 60 * 60 * 1000).date();
        dateIdxs.append(dateIdx);
    };

    db->execNow({queryString, rowCallback});

    return dateIdxs;
}

/**
 * @brief Marks a message as sent.
 * Removing message from the faux-offline pending messages list.
 *
 * @param id Message ID.
 */
void History::markAsSent(RowId messageId)
{
    if (!isValid()) {
        return;
    }

    db->execLater(QString("DELETE FROM faux_offline_pending WHERE id=%1;").arg(messageId.get()));
}