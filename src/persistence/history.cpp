/*
    Copyright © 2015-2019 by The qTox Project Contributors

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

namespace {
static constexpr int SCHEMA_VERSION = 7;

bool createCurrentSchema(RawDatabase& db)
{
    QVector<RawDatabase::Query> queries;
    queries += RawDatabase::Query(QStringLiteral(
        "CREATE TABLE peers (id INTEGER PRIMARY KEY, "
        "public_key TEXT NOT NULL UNIQUE);"
        "CREATE TABLE aliases (id INTEGER PRIMARY KEY, "
        "owner INTEGER, "
        "display_name BLOB NOT NULL, "
        "UNIQUE(owner, display_name), "
        "FOREIGN KEY (owner) REFERENCES peers(id));"
        "CREATE TABLE history "
        "(id INTEGER PRIMARY KEY, "
        "message_type CHAR(1) NOT NULL DEFAULT 'T' CHECK (message_type in ('T','F','S')), "
        "timestamp INTEGER NOT NULL, "
        "chat_id INTEGER NOT NULL, "
        // Message subtypes want to reference the following as a foreign key. Foreign keys must be
        // guaranteed to be unique. Since an ID is already unique, id + message type is also unique
        "UNIQUE (id, message_type), "
        "FOREIGN KEY (chat_id) REFERENCES peers(id)); "
        "CREATE TABLE text_messages "
        "(id INTEGER PRIMARY KEY, "
        "message_type CHAR(1) NOT NULL CHECK (message_type = 'T'), "
        "sender_alias INTEGER NOT NULL, "
        // even though technically a message can be null for file transfer, we've opted
        // to just insert an empty string when there's no content, this moderately simplifies
        // implementation as currently our database doesn't have support for optional fields.
        // We would either have to insert "?" or "null" based on if message exists and then
        // ensure that our blob vector always has the right number of fields. Better to just
        // leave this as NOT NULL for now.
        "message BLOB NOT NULL, "
        "FOREIGN KEY (id, message_type) REFERENCES history(id, message_type), "
        "FOREIGN KEY (sender_alias) REFERENCES aliases(id)); "
        "CREATE TABLE file_transfers "
        "(id INTEGER PRIMARY KEY, "
        "message_type CHAR(1) NOT NULL CHECK (message_type = 'F'), "
        "sender_alias INTEGER NOT NULL, "
        "file_restart_id BLOB NOT NULL, "
        "file_name BLOB NOT NULL, "
        "file_path BLOB NOT NULL, "
        "file_hash BLOB NOT NULL, "
        "file_size INTEGER NOT NULL, "
        "direction INTEGER NOT NULL, "
        "file_state INTEGER NOT NULL, "
        "FOREIGN KEY (id, message_type) REFERENCES history(id, message_type), "
        "FOREIGN KEY (sender_alias) REFERENCES aliases(id)); "
        "CREATE TABLE system_messages "
        "(id INTEGER PRIMARY KEY, "
        "message_type CHAR(1) NOT NULL CHECK (message_type = 'S'), "
        "system_message_type INTEGER NOT NULL, "
        "arg1 BLOB, "
        "arg2 BLOB, "
        "arg3 BLOB, "
        "arg4 BLOB, "
        "FOREIGN KEY (id, message_type) REFERENCES history(id, message_type)); "
        "CREATE TABLE faux_offline_pending (id INTEGER PRIMARY KEY, "
        "required_extensions INTEGER NOT NULL DEFAULT 0, "
        "FOREIGN KEY (id) REFERENCES history(id));"
        "CREATE TABLE broken_messages (id INTEGER PRIMARY KEY, "
        "reason INTEGER NOT NULL DEFAULT 0, "
        "FOREIGN KEY (id) REFERENCES history(id));"));
    // sqlite doesn't support including the index as part of the CREATE TABLE statement, so add a second query
    queries += RawDatabase::Query(
        "CREATE INDEX chat_id_idx on history (chat_id);");
    queries += RawDatabase::Query(QStringLiteral("PRAGMA user_version = %1;").arg(SCHEMA_VERSION));
    return db.execNow(queries);
}

bool isNewDb(std::shared_ptr<RawDatabase>& db, bool& success)
{
    bool newDb;
    if (!db->execNow(RawDatabase::Query("SELECT COUNT(*) FROM sqlite_master;",
                                        [&](const QVector<QVariant>& row) {
                                            newDb = row[0].toLongLong() == 0;
                                        }))) {
        db.reset();
        success = false;
        return false;
    }
    success = true;
    return newDb;
}

bool dbSchema0to1(RawDatabase& db)
{
    QVector<RawDatabase::Query> queries;
    queries += RawDatabase::Query(QStringLiteral("CREATE TABLE file_transfers "
                                                 "(id INTEGER PRIMARY KEY, "
                                                 "chat_id INTEGER NOT NULL, "
                                                 "file_restart_id BLOB NOT NULL, "
                                                 "file_name BLOB NOT NULL, "
                                                 "file_path BLOB NOT NULL, "
                                                 "file_hash BLOB NOT NULL, "
                                                 "file_size INTEGER NOT NULL, "
                                                 "direction INTEGER NOT NULL, "
                                                 "file_state INTEGER NOT NULL);"));
    queries += RawDatabase::Query(QStringLiteral("ALTER TABLE history ADD file_id INTEGER;"));
    queries += RawDatabase::Query(QStringLiteral("PRAGMA user_version = 1;"));
    return db.execNow(queries);
}

bool dbSchema1to2(RawDatabase& db)
{
    // Any faux_offline_pending message, in a chat that has newer delivered
    // message is decided to be broken. It must be moved from
    // faux_offline_pending to broken_messages

    // the last non-pending message in each chat
    QString lastDeliveredQuery =
        QString("SELECT chat_id, MAX(history.id) FROM "
                "history JOIN peers chat ON chat_id = chat.id "
                "LEFT JOIN faux_offline_pending ON history.id = faux_offline_pending.id "
                "WHERE faux_offline_pending.id IS NULL "
                "GROUP BY chat_id;");

    QVector<RawDatabase::Query> upgradeQueries;
    upgradeQueries += RawDatabase::Query(QStringLiteral("CREATE TABLE broken_messages "
                                                        "(id INTEGER PRIMARY KEY);"));

    auto rowCallback = [&upgradeQueries](const QVector<QVariant>& row) {
        auto chatId = row[0].toLongLong();
        auto lastDeliveredHistoryId = row[1].toLongLong();

        upgradeQueries += QString("INSERT INTO broken_messages "
                                  "SELECT faux_offline_pending.id FROM "
                                  "history JOIN faux_offline_pending "
                                  "ON faux_offline_pending.id = history.id "
                                  "WHERE history.chat_id=%1 "
                                  "AND history.id < %2;")
                              .arg(chatId)
                              .arg(lastDeliveredHistoryId);
    };
    // note this doesn't modify the db, just generate new queries, so is safe
    // to run outside of our upgrade transaction
    if (!db.execNow({lastDeliveredQuery, rowCallback})) {
        return false;
    }

    upgradeQueries += QString("DELETE FROM faux_offline_pending "
                              "WHERE id in ("
                              "SELECT id FROM broken_messages);");

    upgradeQueries += RawDatabase::Query(QStringLiteral("PRAGMA user_version = 2;"));

    return db.execNow(upgradeQueries);
}

bool dbSchema2to3(RawDatabase& db)
{
    // Any faux_offline_pending message with the content "/me " are action
    // messages that qTox previously let a user enter, but that will cause an
    // action type message to be sent to toxcore, with 0 length, which will
    // always fail. They must be be moved from faux_offline_pending to broken_messages
    // to avoid qTox from erroring trying to send them on every connect

    const QString emptyActionMessageString = "/me ";

    QVector<RawDatabase::Query> upgradeQueries;
    upgradeQueries += RawDatabase::Query{QString("INSERT INTO broken_messages "
                                                 "SELECT faux_offline_pending.id FROM "
                                                 "history JOIN faux_offline_pending "
                                                 "ON faux_offline_pending.id = history.id "
                                                 "WHERE history.message = ?;"),
                                         {emptyActionMessageString.toUtf8()}};

    upgradeQueries += QString("DELETE FROM faux_offline_pending "
                              "WHERE id in ("
                              "SELECT id FROM broken_messages);");

    upgradeQueries += RawDatabase::Query(QStringLiteral("PRAGMA user_version = 3;"));

    return db.execNow(upgradeQueries);
}

bool dbSchema3to4(RawDatabase& db)
{
    QVector<RawDatabase::Query> upgradeQueries;
    upgradeQueries += RawDatabase::Query{QString(
        "CREATE INDEX chat_id_idx on history (chat_id);")};

    upgradeQueries += RawDatabase::Query(QStringLiteral("PRAGMA user_version = 4;"));

    return db.execNow(upgradeQueries);
}

void addForeignKeyToAlias(QVector<RawDatabase::Query>& queries)
{
    queries += RawDatabase::Query(QStringLiteral(
        "CREATE TABLE aliases_new (id INTEGER PRIMARY KEY, owner INTEGER, "
        "display_name BLOB NOT NULL, UNIQUE(owner, display_name), "
        "FOREIGN KEY (owner) REFERENCES peers(id));"));
    queries += RawDatabase::Query(QStringLiteral(
        "INSERT INTO aliases_new (id, owner, display_name) "
        "SELECT id, owner, display_name "
        "FROM aliases;"));
    queries += RawDatabase::Query(QStringLiteral("DROP TABLE aliases;"));
    queries += RawDatabase::Query(QStringLiteral("ALTER TABLE aliases_new RENAME TO aliases;"));
}

void addForeignKeyToHistory(QVector<RawDatabase::Query>& queries)
{
    queries += RawDatabase::Query(QStringLiteral(
        "CREATE TABLE history_new "
        "(id INTEGER PRIMARY KEY, "
        "timestamp INTEGER NOT NULL, "
        "chat_id INTEGER NOT NULL, "
        "sender_alias INTEGER NOT NULL, "
        "message BLOB NOT NULL, "
        "file_id INTEGER, "
        "FOREIGN KEY (file_id) REFERENCES file_transfers(id), "
        "FOREIGN KEY (chat_id) REFERENCES peers(id), "
        "FOREIGN KEY (sender_alias) REFERENCES aliases(id));"));
    queries += RawDatabase::Query(QStringLiteral(
        "INSERT INTO history_new (id, timestamp, chat_id, sender_alias, message, file_id) "
        "SELECT id, timestamp, chat_id, sender_alias, message, file_id "
        "FROM history;"));
    queries += RawDatabase::Query(QStringLiteral("DROP TABLE history;"));
    queries += RawDatabase::Query(QStringLiteral("ALTER TABLE history_new RENAME TO history;"));
}

void addForeignKeyToFauxOfflinePending(QVector<RawDatabase::Query>& queries)
{
    queries += RawDatabase::Query(QStringLiteral(
        "CREATE TABLE new_faux_offline_pending (id INTEGER PRIMARY KEY, "
        "FOREIGN KEY (id) REFERENCES history(id));"));
    queries += RawDatabase::Query(QStringLiteral(
        "INSERT INTO new_faux_offline_pending (id) "
        "SELECT id "
        "FROM faux_offline_pending;"));
    queries += RawDatabase::Query(QStringLiteral("DROP TABLE faux_offline_pending;"));
    queries += RawDatabase::Query(QStringLiteral("ALTER TABLE new_faux_offline_pending RENAME TO faux_offline_pending;"));
}

void addForeignKeyToBrokenMessages(QVector<RawDatabase::Query>& queries)
{
    queries += RawDatabase::Query(QStringLiteral(
        "CREATE TABLE new_broken_messages (id INTEGER PRIMARY KEY, "
        "FOREIGN KEY (id) REFERENCES history(id));"));
    queries += RawDatabase::Query(QStringLiteral(
        "INSERT INTO new_broken_messages (id) "
        "SELECT id "
        "FROM broken_messages;"));
    queries += RawDatabase::Query(QStringLiteral("DROP TABLE broken_messages;"));
    queries += RawDatabase::Query(QStringLiteral("ALTER TABLE new_broken_messages RENAME TO broken_messages;"));
}

bool dbSchema4to5(RawDatabase& db)
{
    // add foreign key contrains to database tables. sqlite doesn't support advanced alter table commands, so instead we
    // need to copy data to new tables with the foreign key contraints: http://www.sqlitetutorial.net/sqlite-alter-table/
    QVector<RawDatabase::Query> upgradeQueries;
    addForeignKeyToAlias(upgradeQueries);
    addForeignKeyToHistory(upgradeQueries);
    addForeignKeyToFauxOfflinePending(upgradeQueries);
    addForeignKeyToBrokenMessages(upgradeQueries);
    upgradeQueries += RawDatabase::Query(QStringLiteral("PRAGMA user_version = 5;"));
    auto transactionPass = db.execNow(upgradeQueries);
    if (transactionPass) {
        db.execNow("VACUUM;"); // after copying all the tables and deleting the old ones, our db file is half empty.
    }
    return transactionPass;
}

bool dbSchema5to6(RawDatabase& db)
{
    QVector<RawDatabase::Query> upgradeQueries;

    upgradeQueries += RawDatabase::Query{QString("ALTER TABLE faux_offline_pending "
                                                 "ADD COLUMN required_extensions INTEGER NOT NULL "
                                                 "DEFAULT 0;")};

    upgradeQueries += RawDatabase::Query{QString("ALTER TABLE broken_messages "
                                                 "ADD COLUMN reason INTEGER NOT NULL "
                                                 "DEFAULT 0;")};

    upgradeQueries += RawDatabase::Query(QStringLiteral("PRAGMA user_version = 6;"));
    return db.execNow(upgradeQueries);
}

bool dbSchema6to7(RawDatabase& db)
{
    QVector<RawDatabase::Query> upgradeQueries;

    // Cannot add UNIQUE(id, message_type) to history table without creating a new one. Create a new history table
    upgradeQueries += RawDatabase::Query(
        "CREATE TABLE history_new (id INTEGER PRIMARY KEY, message_type CHAR(1) NOT NULL DEFAULT "
        "'T' CHECK (message_type in ('T','F','S')), timestamp INTEGER NOT NULL, chat_id INTEGER "
        "NOT NULL, UNIQUE (id, message_type), FOREIGN KEY (chat_id) REFERENCES peers(id))");

    // Create new text_messages table. We will split messages out of history and insert them into this new table
    upgradeQueries += RawDatabase::Query(
        "CREATE TABLE text_messages (id INTEGER PRIMARY KEY, message_type CHAR(1) NOT NULL CHECK "
        "(message_type = 'T'), sender_alias INTEGER NOT NULL, message BLOB NOT NULL, FOREIGN KEY "
        "(id, message_type) REFERENCES history_new(id, message_type), FOREIGN KEY (sender_alias) "
        "REFERENCES aliases(id))");

    // Cannot add a FOREIGN KEY to the file_transfers table without creating a new one. Create a new file_transfers table
    upgradeQueries += RawDatabase::Query(
        "CREATE TABLE file_transfers_new (id INTEGER PRIMARY KEY, message_type CHAR(1) NOT NULL "
        "CHECK (message_type = 'F'), sender_alias INTEGER NOT NULL, file_restart_id BLOB NOT NULL, "
        "file_name BLOB NOT NULL, file_path BLOB NOT NULL, file_hash BLOB NOT NULL, file_size "
        "INTEGER NOT NULL, direction INTEGER NOT NULL, file_state INTEGER NOT NULL, FOREIGN KEY "
        "(id, message_type) REFERENCES history_new(id, message_type), FOREIGN KEY (sender_alias) "
        "REFERENCES aliases(id))");

    upgradeQueries +=
        RawDatabase::Query("INSERT INTO history_new SELECT id, 'T' AS message_type, timestamp, "
                           "chat_id FROM history WHERE history.file_id IS NULL");

    upgradeQueries +=
        RawDatabase::Query("INSERT INTO text_messages SELECT id, 'T' AS message_type, "
                           "sender_alias, message FROM history WHERE history.file_id IS NULL");

    upgradeQueries +=
        RawDatabase::Query("INSERT INTO history_new SELECT id, 'F' AS message_type, timestamp, "
                           "chat_id FROM history WHERE history.file_id IS NOT NULL");

    upgradeQueries += RawDatabase::Query(
        "INSERT INTO file_transfers_new (id, message_type, sender_alias, file_restart_id, "
        "file_name, file_path, file_hash, file_size, direction, file_state) SELECT history.id, 'F' "
        "as message_type, history.sender_alias, file_transfers.file_restart_id, "
        "file_transfers.file_name, file_transfers.file_path, file_transfers.file_hash, "
        "file_transfers.file_size, file_transfers.direction, file_transfers.file_state FROM "
        "history INNER JOIN file_transfers on history.file_id = file_transfers.id WHERE "
        "history.file_id IS NOT NULL");

    upgradeQueries += RawDatabase::Query(
        "CREATE TABLE system_messages (id INTEGER PRIMARY KEY, message_type CHAR(1) NOT NULL CHECK "
        "(message_type = 'S'), system_message_type INTEGER NOT NULL, arg1 BLOB, arg2 BLOB, arg3 BLOB, arg4 BLOB, "
        "FOREIGN KEY (id, message_type) REFERENCES history_new(id, message_type))");

    // faux_offline_pending needs to be re-created to reference the new history table
    upgradeQueries += RawDatabase::Query(
        "CREATE TABLE faux_offline_pending_new (id INTEGER PRIMARY KEY, required_extensions "
        "INTEGER NOT NULL DEFAULT 0, FOREIGN KEY (id) REFERENCES history_new(id))");
    upgradeQueries += RawDatabase::Query("INSERT INTO faux_offline_pending_new SELECT id, "
                                         "required_extensions FROM faux_offline_pending");
    upgradeQueries += RawDatabase::Query("DROP TABLE faux_offline_pending");
    upgradeQueries +=
        RawDatabase::Query("ALTER TABLE faux_offline_pending_new RENAME TO faux_offline_pending");

    // broken_messages needs to be re-created to reference the new history tablek
    upgradeQueries += RawDatabase::Query(
        "CREATE TABLE broken_messages_new (id INTEGER PRIMARY KEY, reason INTEGER NOT NULL DEFAULT "
        "0, FOREIGN KEY (id) REFERENCES history_new(id))");
    upgradeQueries += RawDatabase::Query(
        "INSERT INTO broken_messages_new SELECT id, reason FROM broken_messages");
    upgradeQueries += RawDatabase::Query("DROP TABLE broken_messages");
    upgradeQueries +=
        RawDatabase::Query("ALTER TABLE broken_messages_new RENAME TO broken_messages");

    // Everything referencing old history should now be gone
    upgradeQueries += RawDatabase::Query("DROP TABLE history");
    upgradeQueries += RawDatabase::Query("ALTER TABLE history_new RENAME TO history");

    // Drop file transfers late since history depends on it
    upgradeQueries += RawDatabase::Query("DROP TABLE file_transfers");
    upgradeQueries += RawDatabase::Query("ALTER TABLE file_transfers_new RENAME TO file_transfers");

    upgradeQueries += RawDatabase::Query("CREATE INDEX chat_id_idx on history (chat_id);");

    upgradeQueries += RawDatabase::Query(QStringLiteral("PRAGMA user_version = 7;"));

    return db.execNow(upgradeQueries);
}

/**
 * @brief Upgrade the db schema
 * @note On future alterations of the database all you have to do is bump the SCHEMA_VERSION
 * variable and add another case to the switch statement below. Make sure to fall through on each case.
 */
bool dbSchemaUpgrade(std::shared_ptr<RawDatabase>& db)
{
    // If we're a new dB we can just make a new one and call it a day
    bool success = false;
    const bool newDb = isNewDb(db, success);
    if (!success) {
        qCritical() << "Failed to create current db schema";
        return false;
    }

    if (newDb) {
        if (!createCurrentSchema(*db)) {
            qCritical() << "Failed to create current db schema";
            return false;
        }
        qDebug() << "Database created at schema version" << SCHEMA_VERSION;
        return true;
    }

    // Otherwise we have to do upgrades from our current version to the latest version

    int64_t databaseSchemaVersion;

    if (!db->execNow(RawDatabase::Query("PRAGMA user_version", [&](const QVector<QVariant>& row) {
            databaseSchemaVersion = row[0].toLongLong();
        }))) {
        qCritical() << "History failed to read user_version";
        return false;
    }

    if (databaseSchemaVersion > SCHEMA_VERSION) {
        qWarning().nospace() << "Database version (" << databaseSchemaVersion
                             << ") is newer than we currently support (" << SCHEMA_VERSION
                             << "). Please upgrade qTox";
        // We don't know what future versions have done, we have to disable db access until we re-upgrade
        return false;
    } else if (databaseSchemaVersion == SCHEMA_VERSION) {
        // No work to do
        return true;
    }

    using DbSchemaUpgradeFn = bool (*)(RawDatabase&);
    std::vector<DbSchemaUpgradeFn> upgradeFns = {dbSchema0to1, dbSchema1to2, dbSchema2to3,
                                                 dbSchema3to4, dbSchema4to5, dbSchema5to6,
                                                 dbSchema6to7};

    assert(databaseSchemaVersion < static_cast<int>(upgradeFns.size()));
    assert(upgradeFns.size() == SCHEMA_VERSION);

    for (int64_t i = databaseSchemaVersion; i < static_cast<int>(upgradeFns.size()); ++i) {
        auto const newDbVersion = i + 1;
        if (!upgradeFns[i](*db)) {
            qCritical() << "Failed to upgrade db to schema version " << newDbVersion << " aborting";
            return false;
        }
        qDebug() << "Database upgraded incrementally to schema version " << newDbVersion;
    }

    qInfo() << "Database upgrade finished (databaseSchemaVersion" << databaseSchemaVersion << "->"
            << SCHEMA_VERSION << ")";
    return true;
}

MessageState getMessageState(bool isPending, bool isBroken)
{
    assert(!(isPending && isBroken));
    MessageState messageState;

    if (isPending) {
        messageState = MessageState::pending;
    } else if (isBroken) {
        messageState = MessageState::broken;
    } else {
        messageState = MessageState::complete;
    }
    return messageState;
}

QString generatePeerIdString(ToxPk const& pk)
{
    return QString("(SELECT id FROM peers WHERE public_key = '%1')").arg(pk.toString());

}

RawDatabase::Query generateEnsurePkInPeers(ToxPk const& pk)
{
    return RawDatabase::Query{QStringLiteral("INSERT OR IGNORE INTO peers (public_key) "
                                "VALUES ('%1')").arg(pk.toString())};
}

RawDatabase::Query generateUpdateAlias(ToxPk const& pk, QString const& dispName)
{
    return RawDatabase::Query(
            QString("INSERT OR IGNORE INTO aliases (owner, display_name) VALUES (%1, ?);").arg(generatePeerIdString(pk)),
            {dispName.toUtf8()});
}

RawDatabase::Query generateHistoryTableInsertion(char type, const QDateTime& time, const ToxPk& friendPk)
{
    return RawDatabase::Query(QString("INSERT INTO history (message_type, timestamp, chat_id) "
                                      "VALUES ('%1', %2, %3);")
                                  .arg(type)
                                  .arg(time.toMSecsSinceEpoch())
                                  .arg(generatePeerIdString(friendPk)));
}

/**
 * @brief Generate query to insert new message in database
 * @param friendPk Friend publick key to save.
 * @param message Message to save.
 * @param sender Sender to save.
 * @param time Time of message sending.
 * @param isDelivered True if message was already delivered.
 * @param dispName Name, which should be displayed.
 * @param insertIdCallback Function, called after query execution.
 */
QVector<RawDatabase::Query>
generateNewTextMessageQueries(const ToxPk& friendPk, const QString& message, const ToxPk& sender,
                              const QDateTime& time, bool isDelivered, ExtensionSet extensionSet,
                              QString dispName, std::function<void(RowId)> insertIdCallback)
{
    QVector<RawDatabase::Query> queries;

    queries += generateEnsurePkInPeers(friendPk);
    queries += generateEnsurePkInPeers(sender);
    queries += generateUpdateAlias(sender, dispName);
    queries += generateHistoryTableInsertion('T', time, friendPk);

    queries += RawDatabase::Query(
        QString("INSERT INTO text_messages (id, message_type, sender_alias, message) "
                "VALUES ( "
                "    last_insert_rowid(), "
                "    'T', "
                "    (SELECT id FROM aliases WHERE owner=%1 and display_name=?), "
                "    ?"
                ");")
            .arg(generatePeerIdString(sender)),
        {dispName.toUtf8(), message.toUtf8()}, insertIdCallback);

    if (!isDelivered) {
        queries += RawDatabase::Query{
            QString("INSERT INTO faux_offline_pending (id, required_extensions) VALUES ("
                    "    last_insert_rowid(), %1"
                    ");")
                .arg(extensionSet.to_ulong())};
    }

    return queries;
}

QVector<RawDatabase::Query> generateNewSystemMessageQueries(const ToxPk& friendPk,
                                                            const QDateTime& time,
                                                            const SystemMessage& systemMessage)
{
    QVector<RawDatabase::Query> queries;

    queries += generateEnsurePkInPeers(friendPk);
    queries += generateHistoryTableInsertion('S', time, friendPk);

    QVector<QByteArray> blobs;
    std::transform(systemMessage.args.begin(), systemMessage.args.end(), std::back_inserter(blobs),
                   [](const QString& s) { return s.toUtf8(); });

    queries += RawDatabase::Query(QString("INSERT INTO system_messages (id, message_type, "
                                          "system_message_type, arg1, arg2, arg3, arg4)"
                                          "VALUES (last_insert_rowid(), 'S', %1, ?, ?, ?, ?)")
                                      .arg(static_cast<int>(systemMessage.messageType)),
                                  blobs);

    return queries;
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

FileDbInsertionData::FileDbInsertionData()
{
    static int id = qRegisterMetaType<FileDbInsertionData>();
    (void)id;
}

/**
 * @brief Prepares the database to work with the history.
 * @param db This database will be prepared for use with the history.
 */
History::History(std::shared_ptr<RawDatabase> db_)
    : db(db_)
{
    if (!isValid()) {
        qWarning() << "Database not open, init failed";
        return;
    }

    // foreign key support is not enabled by default, so needs to be enabled on every connection
    // support was added in sqlite 3.6.19, which is qTox's minimum supported version
    db->execNow(
        "PRAGMA foreign_keys = ON;");

    const auto upgradeSucceeded = dbSchemaUpgrade(db);

    // dbSchemaUpgrade may have put us in an invalid state
    if (!upgradeSucceeded) {
        db.reset();
        return;
    }

    connect(this, &History::fileInserted, this, &History::onFileInserted);
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
    if (historyAccessBlocked()) {
        return false;
    }

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
                "DELETE FROM broken_messages;"
                "DELETE FROM text_messages;"
                "DELETE FROM file_transfers;"
                "DELETE FROM system_messages;"
                "DELETE FROM history;"
                "DELETE FROM aliases;"
                "DELETE FROM peers;"
                "VACUUM;");
}

/**
 * @brief Erases the chat history with one friend.
 * @param friendPk Friend public key to erase.
 */
void History::removeFriendHistory(const ToxPk& friendPk)
{
    if (!isValid()) {
        return;
    }

    QString queryText = QString("DELETE FROM faux_offline_pending "
                                "WHERE faux_offline_pending.id IN ( "
                                "    SELECT faux_offline_pending.id FROM faux_offline_pending "
                                "    LEFT JOIN history ON faux_offline_pending.id = history.id "
                                "    WHERE chat_id=%1 "
                                "); "
                                "DELETE FROM broken_messages "
                                "WHERE broken_messages.id IN ( "
                                "    SELECT broken_messages.id FROM broken_messages "
                                "    LEFT JOIN history ON broken_messages.id = history.id "
                                "    WHERE chat_id=%1 "
                                "); "
                                "DELETE FROM text_messages "
                                "WHERE id IN ("
                                "   SELECT id from history "
                                "   WHERE message_type = 'T' AND chat_id=%1);"
                                "DELETE FROM file_transfers "
                                "WHERE id IN ( "
                                "    SELECT id from history "
                                "    WHERE message_type = 'F' AND chat_id=%1);"
                                "DELETE FROM system_messages "
                                "WHERE id IN ( "
                                "   SELECT id from history "
                                "   WHERE message_type = 'S' AND chat_id=%1);"
                                "DELETE FROM history WHERE chat_id=%1; "
                                "DELETE FROM aliases WHERE owner=%1; "
                                "DELETE FROM peers WHERE id=%1; "
                                "VACUUM;")
                            .arg(generatePeerIdString(friendPk));

    if (!db->execNow(queryText)) {
        qWarning() << "Failed to remove friend's history";
    }
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

QVector<RawDatabase::Query>
History::generateNewFileTransferQueries(const ToxPk& friendPk, const ToxPk& sender,
                                        const QDateTime& time, const QString& dispName,
                                        const FileDbInsertionData& insertionData)
{
    QVector<RawDatabase::Query> queries;

    queries += generateEnsurePkInPeers(friendPk);
    queries += generateEnsurePkInPeers(sender);
    queries += generateUpdateAlias(sender, dispName);
    queries += generateHistoryTableInsertion('F', time, friendPk);

    std::weak_ptr<History> weakThis = shared_from_this();
    auto fileId = insertionData.fileId;

    queries +=
        RawDatabase::Query(QString(
                               "INSERT INTO file_transfers "
                               "    (id, message_type, sender_alias, "
                               "    file_restart_id, file_name, file_path, "
                               "    file_hash, file_size, direction, file_state) "
                               "VALUES ( "
                               "    last_insert_rowid(), "
                               "    'F', "
                               "    (SELECT id FROM aliases WHERE owner=%1 AND display_name=?), "
                               "    ?, "
                               "    ?, "
                               "    ?, "
                               "    ?, "
                               "    %2, "
                               "    %3, "
                               "    %4 "
                               ");")
                               .arg(generatePeerIdString(sender))
                               .arg(insertionData.size)
                               .arg(insertionData.direction)
                               .arg(ToxFile::CANCELED),
                           {dispName.toUtf8(), insertionData.fileId.toUtf8(),
                            insertionData.fileName.toUtf8(), insertionData.filePath.toUtf8(),
                            QByteArray()},
                           [weakThis, fileId](RowId id) {
                               auto pThis = weakThis.lock();
                               if (pThis)
                                   emit pThis->fileInserted(id, fileId);
                           });

    return queries;
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

void History::addNewFileMessage(const ToxPk& friendPk, const QString& fileId,
                                const QString& fileName, const QString& filePath, int64_t size,
                                const ToxPk& sender, const QDateTime& time, QString const& dispName)
{
    if (historyAccessBlocked()) {
        return;
    }

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

    auto queries = generateNewFileTransferQueries(friendPk, sender, time, dispName, insertionData);

    db->execLater(queries);
}

void History::addNewSystemMessage(const ToxPk& friendPk, const QDateTime& time,
                                  const SystemMessage& systemMessage)
{
    if (historyAccessBlocked())
        return;

    const auto queries = generateNewSystemMessageQueries(friendPk, time, systemMessage);

    db->execLater(queries);
}

/**
 * @brief Saves a chat message in the database.
 * @param friendPk Friend publick key to save.
 * @param message Message to save.
 * @param sender Sender to save.
 * @param time Time of message sending.
 * @param isDelivered True if message was already delivered.
 * @param dispName Name, which should be displayed.
 * @param insertIdCallback Function, called after query execution.
 */
void History::addNewMessage(const ToxPk& friendPk, const QString& message, const ToxPk& sender,
                            const QDateTime& time, bool isDelivered, ExtensionSet extensionSet,
                            QString dispName, const std::function<void(RowId)>& insertIdCallback)
{
    if (historyAccessBlocked()) {
        return;
    }

    db->execLater(generateNewTextMessageQueries(friendPk, message, sender, time, isDelivered,
                                                extensionSet, dispName, insertIdCallback));
}

void History::setFileFinished(const QString& fileId, bool success, const QString& filePath,
                              const QByteArray& fileHash)
{
    if (historyAccessBlocked()) {
        return;
    }

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
    if (historyAccessBlocked()) {
        return 0;
    }

    return getNumMessagesForFriendBeforeDate(friendPk, QDateTime());
}

size_t History::getNumMessagesForFriendBeforeDate(const ToxPk& friendPk, const QDateTime& date)
{
    if (historyAccessBlocked()) {
        return 0;
    }

    QString queryText = QString("SELECT COUNT(history.id) "
                                "FROM history "
                                "JOIN peers chat ON chat_id = chat.id "
                                "WHERE chat.public_key='%1'")
                            .arg(friendPk.toString());

    if (date.isNull()) {
        queryText += ";";
    } else {
        queryText += QString(" AND timestamp < %1;").arg(date.toMSecsSinceEpoch());
    }

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
    if (historyAccessBlocked()) {
        return {};
    }

    QList<HistMessage> messages;

    // Don't forget to update the rowCallback if you change the selected columns!
    QString queryText =
        QString(
            "SELECT history.id, history.message_type, history.timestamp, faux_offline_pending.id, "
            "    faux_offline_pending.required_extensions, broken_messages.id, text_messages.message, "
            "    file_restart_id, file_name, file_path, file_size, file_transfers.direction, "
            "    file_state, peers.public_key as sender_key, aliases.display_name, "
            "    system_messages.system_message_type, system_messages.arg1, system_messages.arg2, "
            "    system_messages.arg3, system_messages.arg4 "
            "FROM history "
            "LEFT JOIN text_messages ON history.id = text_messages.id "
            "LEFT JOIN file_transfers ON history.id = file_transfers.id "
            "LEFT JOIN system_messages ON system_messages.id == history.id "
            "LEFT JOIN aliases ON text_messages.sender_alias = aliases.id OR "
            "file_transfers.sender_alias = aliases.id "
            "LEFT JOIN peers ON aliases.owner = peers.id "
            "LEFT JOIN faux_offline_pending ON faux_offline_pending.id = history.id "
            "LEFT JOIN broken_messages ON broken_messages.id = history.id "
            "WHERE history.chat_id = %1 "
            "LIMIT %2 OFFSET %3;")
            .arg(generatePeerIdString(friendPk))
            .arg(lastIdx - firstIdx)
            .arg(firstIdx);

    auto rowCallback = [&friendPk, &messages](const QVector<QVariant>& row) {
        // If the select statement is changed please update these constants
        constexpr auto messageOffset = 6;
        constexpr auto fileOffset = 7;
        constexpr auto senderOffset = 13;
        constexpr auto systemOffset = 15;

        auto it = row.begin();

        const auto id = RowId{(*it++).toLongLong()};
        const auto messageType = (*it++).toString();
        const auto timestamp = QDateTime::fromMSecsSinceEpoch((*it++).toLongLong());
        const auto isPending = !(*it++).isNull();
        // If NULL this should just reutrn 0 which is an empty extension set, good enough for now
        const auto requiredExtensions = ExtensionSet((*it++).toLongLong());
        const auto isBroken = !(*it++).isNull();
        const auto messageState = getMessageState(isPending, isBroken);

        // Intentionally arrange query so message types are at the end so we don't have to think
        // about the iterator jumping around after handling the different types.
        assert(messageType.size() == 1);
        switch (messageType[0].toLatin1()) {
        case 'T': {
            it = std::next(row.begin(), messageOffset);
            assert(!it->isNull());
            const auto messageContent = (*it++).toString();
            it = std::next(row.begin(), senderOffset);
            const auto senderKey = (*it++).toString();
            const auto senderName = QString::fromUtf8((*it++).toByteArray().replace('\0', ""));
            messages += HistMessage(id, messageState, requiredExtensions, timestamp,
                                    friendPk.toString(), senderName, senderKey, messageContent);
            break;
        }
        case 'F': {
            it = std::next(row.begin(), fileOffset);
            assert(!it->isNull());
            ToxFile file;
            file.fileKind = TOX_FILE_KIND_DATA;
            file.resumeFileId = (*it++).toString().toUtf8();
            file.fileName = (*it++).toString();
            file.filePath = (*it++).toString();
            file.filesize = (*it++).toLongLong();
            file.direction = static_cast<ToxFile::FileDirection>((*it++).toLongLong());
            file.status = static_cast<ToxFile::FileStatus>((*it++).toLongLong());
            it = std::next(row.begin(), senderOffset);
            const auto senderKey = (*it++).toString();
            const auto senderName = QString::fromUtf8((*it++).toByteArray().replace('\0', ""));
            messages += HistMessage(id, messageState, timestamp, friendPk.toString(), senderName,
                                    senderKey, file);
            break;
        }
        default:
        case 'S':
            it = std::next(row.begin(), systemOffset);
            assert(!it->isNull());
            SystemMessage systemMessage;
            systemMessage.messageType = static_cast<SystemMessageType>((*it++).toLongLong());

            auto argEnd = std::next(it, systemMessage.args.size());
            std::transform(it, argEnd, systemMessage.args.begin(), [](const QVariant& arg) {
                return QString::fromUtf8(arg.toByteArray().replace('\0', ""));
            });
            it = argEnd;

            messages += HistMessage(id, timestamp, friendPk.toString(), systemMessage);
            break;
        }
    };

    db->execNow({queryText, rowCallback});

    return messages;
}

QList<History::HistMessage> History::getUndeliveredMessagesForFriend(const ToxPk& friendPk)
{
    if (historyAccessBlocked()) {
        return {};
    }

    auto queryText =
        QString(
            "SELECT history.id, history.timestamp, faux_offline_pending.id, "
            "    faux_offline_pending.required_extensions, broken_messages.id, text_messages.message, "
            "    peers.public_key as sender_key, aliases.display_name "
            "FROM history "
            "JOIN text_messages ON history.id = text_messages.id "
            "JOIN aliases ON text_messages.sender_alias = aliases.id "
            "JOIN peers ON aliases.owner = peers.id "
            "JOIN faux_offline_pending ON faux_offline_pending.id = history.id "
            "LEFT JOIN broken_messages ON broken_messages.id = history.id "
            "WHERE history.chat_id = %1 AND history.message_type = 'T';")
            .arg(generatePeerIdString(friendPk));

    QList<History::HistMessage> ret;
    auto rowCallback = [&friendPk, &ret](const QVector<QVariant>& row) {
        auto it = row.begin();
        // dispName and message could have null bytes, QString::fromUtf8
        // truncates on null bytes so we strip them
        auto id = RowId{(*it++).toLongLong()};
        auto timestamp = QDateTime::fromMSecsSinceEpoch((*it++).toLongLong());
        auto isPending = !(*it++).isNull();
        auto extensionSet = ExtensionSet((*it++).toLongLong());
        auto isBroken = !(*it++).isNull();
        auto messageContent = (*it++).toString();
        auto senderKey = (*it++).toString();
        auto displayName = QString::fromUtf8((*it++).toByteArray().replace('\0', ""));

        MessageState messageState = getMessageState(isPending, isBroken);

        ret += {id,          messageState, extensionSet,  timestamp, friendPk.toString(),
                displayName, senderKey,    messageContent};
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
QDateTime History::getDateWhereFindPhrase(const ToxPk& friendPk, const QDateTime& from,
                                          QString phrase, const ParameterSearch& parameter)
{
    if (historyAccessBlocked()) {
        return QDateTime();
    }

    QDateTime result;
    auto rowCallback = [&result](const QVector<QVariant>& row) {
        result = QDateTime::fromMSecsSinceEpoch(row[0].toLongLong());
    };

    phrase.replace("'", "''");

    QString message;

    switch (parameter.filter) {
    case FilterSearch::Register:
        message = QStringLiteral("text_messages.message LIKE '%%1%'").arg(phrase);
        break;
    case FilterSearch::WordsOnly:
        message = QStringLiteral("text_messages.message REGEXP '%1'")
                      .arg(SearchExtraFunctions::generateFilterWordsOnly(phrase).toLower());
        break;
    case FilterSearch::RegisterAndWordsOnly:
        message = QStringLiteral("REGEXPSENSITIVE(text_messages.message, '%1')")
                      .arg(SearchExtraFunctions::generateFilterWordsOnly(phrase));
        break;
    case FilterSearch::Regular:
        message = QStringLiteral("text_messages.message REGEXP '%1'").arg(phrase);
        break;
    case FilterSearch::RegisterAndRegular:
        message = QStringLiteral("REGEXPSENSITIVE(text_messages.message '%1')").arg(phrase);
        break;
    default:
        message = QStringLiteral("LOWER(text_messages.message) LIKE '%%1%'").arg(phrase.toLower());
        break;
    }

    QDateTime time = from;

    if (!time.isValid()) {
        time = QDateTime::currentDateTime();
    }

    if (parameter.period == PeriodSearch::AfterDate || parameter.period == PeriodSearch::BeforeDate) {
        time = parameter.time;
    }

    QString period;
    switch (parameter.period) {
    case PeriodSearch::WithTheFirst:
        period = QStringLiteral("ORDER BY timestamp ASC LIMIT 1;");
        break;
    case PeriodSearch::AfterDate:
        period = QStringLiteral("AND timestamp > '%1' ORDER BY timestamp ASC LIMIT 1;")
                     .arg(time.toMSecsSinceEpoch());
        break;
    case PeriodSearch::BeforeDate:
        period = QStringLiteral("AND timestamp < '%1' ORDER BY timestamp DESC LIMIT 1;")
                     .arg(time.toMSecsSinceEpoch());
        break;
    default:
        period = QStringLiteral("AND timestamp < '%1' ORDER BY timestamp DESC LIMIT 1;")
                     .arg(time.toMSecsSinceEpoch());
        break;
    }

    QString queryText =
        QStringLiteral("SELECT timestamp "
                       "FROM history "
                       "JOIN peers chat ON chat_id = chat.id "
                       "JOIN text_messages ON history.id = text_messages.id "
                       "WHERE chat.public_key='%1' "
                       "AND %2 "
                       "%3")
            .arg(friendPk.toString())
            .arg(message)
            .arg(period);

    db->execNow({queryText, rowCallback});

    return result;
}

/**
 * @brief Gets date boundaries in conversation with friendPk. History doesn't model conversation indexes,
 * but we can count messages between us and friendPk to effectively give us an index. This function
 * returns how many messages have happened between us <-> friendPk each time the date changes
 * @param[in] friendPk ToxPk of conversation to retrieve
 * @param[in] from Start date to look from
 * @param[in] maxNum Maximum number of date boundaries to retrieve
 * @note This API may seem a little strange, why not use QDate from and QDate to? The intent is to
 * have an API that can be used to get the first item after a date (for search) and to get a list
 * of date changes (for loadHistory). We could write two separate queries but the query is fairly
 * intricate compared to our other ones so reducing duplication of it is preferable.
 */
QList<History::DateIdx> History::getNumMessagesForFriendBeforeDateBoundaries(const ToxPk& friendPk,
                                                                             const QDate& from,
                                                                             size_t maxNum)
{
    if (historyAccessBlocked()) {
        return {};
    }

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
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
                           .arg(QDateTime(from.startOfDay()).toMSecsSinceEpoch())
#else
                           .arg(QDateTime(from).toMSecsSinceEpoch())
#endif
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
 * @brief Marks a message as delivered.
 * Removing message from the faux-offline pending messages list.
 *
 * @param id Message ID.
 */
void History::markAsDelivered(RowId messageId)
{
    if (historyAccessBlocked()) {
        return;
    }

    db->execLater(QString("DELETE FROM faux_offline_pending WHERE id=%1;").arg(messageId.get()));
}

/**
* @brief Determines if history access should be blocked
* @return True if history should not be accessed
*/
bool History::historyAccessBlocked()
{
    if (!Settings::getInstance().getEnableLogging()) {
        assert(false);
        qCritical() << "Blocked history access while history is disabled";
        return true;
    }

    if (!isValid()) {
        return true;
    }

    return false;
}

void History::markAsBroken(RowId messageId, BrokenMessageReason reason)
{
    if (!isValid()) {
        return;
    }

    QVector<RawDatabase::Query> queries;
    queries += RawDatabase::Query(QString("DELETE FROM faux_offline_pending WHERE id=%1;").arg(messageId.get()));
    queries += RawDatabase::Query(QString("INSERT INTO broken_messages (id, reason) "
                                          "VALUES (%1, %2);")
                                          .arg(messageId.get())
                                          .arg(static_cast<int64_t>(reason)));

    db->execLater(queries);
}
