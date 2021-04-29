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

#include "src/persistence/db/rawdatabase.h"
// normally we should only test public API instead of implementation,  but there's no reason to expose db schema
// upgrade externally, and the complexity of each version upgrade benefits from being individually testable
#include "src/persistence/history.cpp"

#include <QtTest/QtTest>
#include <QString>

#include <algorithm>
#include <memory>

struct SqliteMasterEntry {
    QString name;
    QString sql;
};

bool operator==(const SqliteMasterEntry& lhs, const SqliteMasterEntry& rhs)
{
    return lhs.name == rhs.name &&
        lhs.sql == rhs.sql;
}

class TestDbSchema : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testCreation();
    void testIsNewDb();
    void test0to1();
    void test1to2();
    void test2to3();
    void test3to4();
    void test4to5();
    void test5to6();
    void test6to7();
    void cleanupTestCase() const;

private:
    bool initSucess{false};
    void createSchemaAtVersion(std::shared_ptr<RawDatabase>, const std::vector<SqliteMasterEntry>& schema);
    void verifyDb(std::shared_ptr<RawDatabase> db, const std::vector<SqliteMasterEntry>& expectedSql);
};

const QString testFileList[] = {"testCreation.db", "testIsNewDbTrue.db", "testIsNewDbFalse.db",
                                "test0to1.db",     "test1to2.db",        "test2to3.db",
                                "test3to4.db",     "test4to5.db",        "test5to6.db",
                                "test6to7.db"};

// db schemas can be select with "SELECT name, sql FROM sqlite_master;" on the database.

const std::vector<SqliteMasterEntry> schema0 {
    {"aliases", "CREATE TABLE aliases (id INTEGER PRIMARY KEY, owner INTEGER, display_name BLOB NOT NULL, UNIQUE(owner, display_name))"},
    {"faux_offline_pending", "CREATE TABLE faux_offline_pending (id INTEGER PRIMARY KEY)"},
    {"history", "CREATE TABLE history (id INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL, chat_id INTEGER NOT NULL, sender_alias INTEGER NOT NULL, message BLOB NOT NULL)"},
    {"peers", "CREATE TABLE peers (id INTEGER PRIMARY KEY, public_key TEXT NOT NULL UNIQUE)"}
};

// added file transfer history
const std::vector<SqliteMasterEntry> schema1 {
    {"aliases", "CREATE TABLE aliases (id INTEGER PRIMARY KEY, owner INTEGER, display_name BLOB NOT NULL, UNIQUE(owner, display_name))"},
    {"faux_offline_pending", "CREATE TABLE faux_offline_pending (id INTEGER PRIMARY KEY)"},
    {"file_transfers", "CREATE TABLE file_transfers (id INTEGER PRIMARY KEY, chat_id INTEGER NOT NULL, file_restart_id BLOB NOT NULL, file_name BLOB NOT NULL, file_path BLOB NOT NULL, file_hash BLOB NOT NULL, file_size INTEGER NOT NULL, direction INTEGER NOT NULL, file_state INTEGER NOT NULL)"},
    {"history", "CREATE TABLE history (id INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL, chat_id INTEGER NOT NULL, sender_alias INTEGER NOT NULL, message BLOB NOT NULL, file_id INTEGER)"},
    {"peers", "CREATE TABLE peers (id INTEGER PRIMARY KEY, public_key TEXT NOT NULL UNIQUE)"}
};

// move stuck faux offline messages do a table of "broken" messages
const std::vector<SqliteMasterEntry> schema2 {
    {"aliases", "CREATE TABLE aliases (id INTEGER PRIMARY KEY, owner INTEGER, display_name BLOB NOT NULL, UNIQUE(owner, display_name))"},
    {"faux_offline_pending", "CREATE TABLE faux_offline_pending (id INTEGER PRIMARY KEY)"},
    {"file_transfers", "CREATE TABLE file_transfers (id INTEGER PRIMARY KEY, chat_id INTEGER NOT NULL, file_restart_id BLOB NOT NULL, file_name BLOB NOT NULL, file_path BLOB NOT NULL, file_hash BLOB NOT NULL, file_size INTEGER NOT NULL, direction INTEGER NOT NULL, file_state INTEGER NOT NULL)"},
    {"history", "CREATE TABLE history (id INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL, chat_id INTEGER NOT NULL, sender_alias INTEGER NOT NULL, message BLOB NOT NULL, file_id INTEGER)"},
    {"peers", "CREATE TABLE peers (id INTEGER PRIMARY KEY, public_key TEXT NOT NULL UNIQUE)"},
    {"broken_messages", "CREATE TABLE broken_messages (id INTEGER PRIMARY KEY)"}
};

// move stuck 0-length action messages to the existing "broken_messages" table. Not a real schema upgrade.
const auto schema3 = schema2;

// create index in history table on chat_id to improve query speed. Not a real schema upgrade.
const std::vector<SqliteMasterEntry> schema4 {
    {"aliases", "CREATE TABLE aliases (id INTEGER PRIMARY KEY, owner INTEGER, display_name BLOB NOT NULL, UNIQUE(owner, display_name))"},
    {"faux_offline_pending", "CREATE TABLE faux_offline_pending (id INTEGER PRIMARY KEY)"},
    {"file_transfers", "CREATE TABLE file_transfers (id INTEGER PRIMARY KEY, chat_id INTEGER NOT NULL, file_restart_id BLOB NOT NULL, file_name BLOB NOT NULL, file_path BLOB NOT NULL, file_hash BLOB NOT NULL, file_size INTEGER NOT NULL, direction INTEGER NOT NULL, file_state INTEGER NOT NULL)"},
    {"history", "CREATE TABLE history (id INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL, chat_id INTEGER NOT NULL, sender_alias INTEGER NOT NULL, message BLOB NOT NULL, file_id INTEGER)"},
    {"peers", "CREATE TABLE peers (id INTEGER PRIMARY KEY, public_key TEXT NOT NULL UNIQUE)"},
    {"broken_messages", "CREATE TABLE broken_messages (id INTEGER PRIMARY KEY)"},
    {"chat_id_idx", "CREATE INDEX chat_id_idx on history (chat_id)"}
};

// added foreign keys
const std::vector<SqliteMasterEntry> schema5 {
    {"aliases", "CREATE TABLE aliases (id INTEGER PRIMARY KEY, owner INTEGER, display_name BLOB NOT NULL, UNIQUE(owner, display_name), FOREIGN KEY (owner) REFERENCES peers(id))"},
    {"faux_offline_pending", "CREATE TABLE faux_offline_pending (id INTEGER PRIMARY KEY, FOREIGN KEY (id) REFERENCES history(id))"},
    {"file_transfers", "CREATE TABLE file_transfers (id INTEGER PRIMARY KEY, chat_id INTEGER NOT NULL, file_restart_id BLOB NOT NULL, file_name BLOB NOT NULL, file_path BLOB NOT NULL, file_hash BLOB NOT NULL, file_size INTEGER NOT NULL, direction INTEGER NOT NULL, file_state INTEGER NOT NULL)"},
    {"history", "CREATE TABLE history (id INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL, chat_id INTEGER NOT NULL, sender_alias INTEGER NOT NULL, message BLOB NOT NULL, file_id INTEGER, FOREIGN KEY (file_id) REFERENCES file_transfers(id), FOREIGN KEY (chat_id) REFERENCES peers(id), FOREIGN KEY (sender_alias) REFERENCES aliases(id))"},
    {"peers", "CREATE TABLE peers (id INTEGER PRIMARY KEY, public_key TEXT NOT NULL UNIQUE)"},
    {"broken_messages", "CREATE TABLE broken_messages (id INTEGER PRIMARY KEY, FOREIGN KEY (id) REFERENCES history(id))"},
    {"chat_id_idx", "CREATE INDEX chat_id_idx on history (chat_id)"}
};

// added toxext extensions
const std::vector<SqliteMasterEntry> schema6 {
    {"aliases", "CREATE TABLE aliases (id INTEGER PRIMARY KEY, owner INTEGER, display_name BLOB NOT NULL, UNIQUE(owner, display_name), FOREIGN KEY (owner) REFERENCES peers(id))"},
    {"faux_offline_pending", "CREATE TABLE faux_offline_pending (id INTEGER PRIMARY KEY, required_extensions INTEGER NOT NULL DEFAULT 0, FOREIGN KEY (id) REFERENCES history(id))"},
    {"file_transfers", "CREATE TABLE file_transfers (id INTEGER PRIMARY KEY, chat_id INTEGER NOT NULL, file_restart_id BLOB NOT NULL, file_name BLOB NOT NULL, file_path BLOB NOT NULL, file_hash BLOB NOT NULL, file_size INTEGER NOT NULL, direction INTEGER NOT NULL, file_state INTEGER NOT NULL)"},
    {"history", "CREATE TABLE history (id INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL, chat_id INTEGER NOT NULL, sender_alias INTEGER NOT NULL, message BLOB NOT NULL, file_id INTEGER, FOREIGN KEY (file_id) REFERENCES file_transfers(id), FOREIGN KEY (chat_id) REFERENCES peers(id), FOREIGN KEY (sender_alias) REFERENCES aliases(id))"},
    {"peers", "CREATE TABLE peers (id INTEGER PRIMARY KEY, public_key TEXT NOT NULL UNIQUE)"},
    {"broken_messages", "CREATE TABLE broken_messages (id INTEGER PRIMARY KEY, reason INTEGER NOT NULL DEFAULT 0, FOREIGN KEY (id) REFERENCES history(id))"},
    {"chat_id_idx", "CREATE INDEX chat_id_idx on history (chat_id)"}
};

const std::vector<SqliteMasterEntry> schema7{
    {"aliases", "CREATE TABLE aliases (id INTEGER PRIMARY KEY, owner INTEGER, display_name BLOB "
                "NOT NULL, UNIQUE(owner, display_name), FOREIGN KEY (owner) REFERENCES peers(id))"},
    {"faux_offline_pending",
     "CREATE TABLE faux_offline_pending (id INTEGER PRIMARY KEY, required_extensions INTEGER NOT "
     "NULL DEFAULT 0, FOREIGN KEY (id) REFERENCES history(id))"},
    {"file_transfers",
     "CREATE TABLE file_transfers (id INTEGER PRIMARY KEY, message_type CHAR(1) NOT NULL CHECK "
     "(message_type = 'F'), sender_alias INTEGER NOT NULL, file_restart_id BLOB NOT NULL, "
     "file_name BLOB NOT NULL, file_path BLOB NOT NULL, file_hash BLOB NOT NULL, file_size INTEGER "
     "NOT NULL, direction INTEGER NOT NULL, file_state INTEGER NOT NULL, FOREIGN KEY (id, "
     "message_type) REFERENCES history(id, message_type), FOREIGN KEY (sender_alias) REFERENCES "
     "aliases(id))"},
    {"history",
     "CREATE TABLE history (id INTEGER PRIMARY KEY, message_type CHAR(1) NOT NULL DEFAULT 'T' "
     "CHECK (message_type in ('T','F','S')), timestamp INTEGER NOT NULL, chat_id INTEGER NOT NULL, "
     "UNIQUE (id, message_type), FOREIGN KEY (chat_id) REFERENCES peers(id))"},
    {"text_messages", "CREATE TABLE text_messages (id INTEGER PRIMARY KEY, message_type CHAR(1) "
                      "NOT NULL CHECK (message_type = 'T'), sender_alias INTEGER NOT NULL, message "
                      "BLOB NOT NULL, FOREIGN KEY (id, message_type) REFERENCES history(id, "
                      "message_type), FOREIGN KEY (sender_alias) REFERENCES aliases(id))"},
    {"peers", "CREATE TABLE peers (id INTEGER PRIMARY KEY, public_key TEXT NOT NULL UNIQUE)"},
    {"broken_messages", "CREATE TABLE broken_messages (id INTEGER PRIMARY KEY, reason INTEGER NOT "
                        "NULL DEFAULT 0, FOREIGN KEY (id) REFERENCES history(id))"},
    {"system_messages",
     "CREATE TABLE system_messages (id INTEGER PRIMARY KEY, message_type CHAR(1) NOT NULL CHECK "
     "(message_type = 'S'), system_message_type INTEGER NOT NULL, arg1 BLOB, arg2 BLOB, arg3 BLOB, arg4 BLOB, "
     "FOREIGN KEY (id, message_type) REFERENCES history(id, message_type))"},
    {"chat_id_idx", "CREATE INDEX chat_id_idx on history (chat_id)"}};


void TestDbSchema::initTestCase()
{
    for (const auto& path : testFileList) {
        QVERIFY(!QFileInfo{path}.exists());
    }
    initSucess = true;
}

void TestDbSchema::cleanupTestCase() const
{
    if (!initSucess) {
        qWarning() << "init failed, skipping cleanup to avoid loss of data";
        return;
    }
    for (const auto& path : testFileList) {
        QFile::remove(path);
    }
}

void TestDbSchema::verifyDb(std::shared_ptr<RawDatabase> db, const std::vector<SqliteMasterEntry>& expectedSql)
{
    QVERIFY(db->execNow(RawDatabase::Query(QStringLiteral(
        "SELECT name, sql FROM sqlite_master;"),
        [&](const QVector<QVariant>& row) {
            const QString tableName = row[0].toString();
            if (row[1].isNull()) {
                // implicit indexes are automatically created for primary key constraints and unique constraints
                // so their existence is already covered by the table creation SQL
                return;
            }
            QString tableSql = row[1].toString();
            // table and column names can be quoted. UPDATE TEABLE automatically quotes the new names, but this
            // has no functional impact on the schema. Strip quotes for comparison so that our created schema
            // matches schema made from UPDATE TABLEs.
            const QString unquotedTableSql = tableSql.remove("\"");
            SqliteMasterEntry entry{tableName, unquotedTableSql};
            QVERIFY(std::find(expectedSql.begin(), expectedSql.end(), entry) != expectedSql.end());
        })));
}

void TestDbSchema::createSchemaAtVersion(std::shared_ptr<RawDatabase> db, const std::vector<SqliteMasterEntry>& schema)
{
    QVector<RawDatabase::Query> queries;
    for (auto const& entry : schema) {
        queries += entry.sql;
    }
    QVERIFY(db->execNow(queries));
}

void TestDbSchema::testCreation()
{
    QVector<RawDatabase::Query> queries;
    auto db = std::shared_ptr<RawDatabase>{new RawDatabase{"testCreation.db", {}, {}}};
    QVERIFY(createCurrentSchema(*db));
    verifyDb(db, schema7);
}

void TestDbSchema::testIsNewDb()
{
    auto db = std::shared_ptr<RawDatabase>{new RawDatabase{"testIsNewDbTrue.db", {}, {}}};
    bool success = false;
    bool newDb = isNewDb(db, success);
    QVERIFY(success);
    QVERIFY(newDb == true);
    db = std::shared_ptr<RawDatabase>{new RawDatabase{"testIsNewDbFalse.db", {}, {}}};
    createSchemaAtVersion(db, schema0);
    newDb = isNewDb(db, success);
    QVERIFY(success);
    QVERIFY(newDb == false);
}

void TestDbSchema::test0to1()
{
    auto db = std::shared_ptr<RawDatabase>{new RawDatabase{"test0to1.db", {}, {}}};
    createSchemaAtVersion(db, schema0);
    QVERIFY(dbSchema0to1(*db));
    verifyDb(db, schema1);
}

void TestDbSchema::test1to2()
{
    /*
    Due to a long standing bug, faux offline message have been able to become stuck
    going back years. Because of recent fixes to history loading, faux offline
    messages will correctly all be sent on connection, but this causes an issue of
    long stuck messages suddenly being delivered to a friend, out of context,
    creating a confusing interaction. To work around this, this upgrade moves any
    faux offline messages in a chat that are older than the last successfully
    delivered message, indicating they were stuck, to a new table,
    `broken_messages`, preventing them from ever being sent in the future.

    https://github.com/qTox/qTox/issues/5776
    */

    auto db = std::shared_ptr<RawDatabase>{new RawDatabase{"test1to2.db", {}, {}}};
    createSchemaAtVersion(db, schema1);

    const QString myPk = "AC18841E56CCDEE16E93E10E6AB2765BE54277D67F1372921B5B418A6B330D3D";
    const QString friend1Pk = "FE34BC6D87B66E958C57BBF205F9B79B62BE0AB8A4EFC1F1BB9EC4D0D8FB0663";
    const QString friend2Pk = "2A1CBCE227549459C0C20F199DB86AD9BCC436D35BAA1825FFD4B9CA3290D200";

    QVector<RawDatabase::Query> queries;
    queries += QString("INSERT INTO peers (id, public_key) VALUES (%1, '%2')").arg(0).arg(myPk);
    queries += QString("INSERT INTO peers (id, public_key) VALUES (%1, '%2')").arg(1).arg(friend1Pk);
    queries += QString("INSERT INTO peers (id, public_key) VALUES (%1, '%2')").arg(2).arg(friend2Pk);

    // friend 1
    // first message in chat is pending - but the second is delivered. This message is "broken"
    queries += RawDatabase::Query{
        "INSERT INTO history (id, timestamp, chat_id, message, sender_alias) VALUES (1, 1, 1, ?, 0)",
        {"first message in chat, pending and stuck"}};
    queries += {"INSERT INTO faux_offline_pending (id) VALUES ("
                                        "    last_insert_rowid()"
                                        ");"};
    // second message is delivered, causing the first to be considered broken
    queries += RawDatabase::Query{
        "INSERT INTO history (id, timestamp, chat_id, message, sender_alias) VALUES (2, 2, 1, ?, 0)",
        {"second message in chat, delivered"}};

    // third message is pending - this is a normal pending message. It should be untouched.
    queries += RawDatabase::Query{
        "INSERT INTO history (id, timestamp, chat_id, message, sender_alias) VALUES (3, 3, 1, ?, 0)",
        {"third message in chat, pending"}};
    queries += {"INSERT INTO faux_offline_pending (id) VALUES ("
                                        "    last_insert_rowid()"
                                        ");"};

    // friend 2
    // first message is delivered.
    queries += RawDatabase::Query{
        "INSERT INTO history (id, timestamp, chat_id, message, sender_alias) VALUES (4, 4, 2, ?, 2)",
        {"first message by friend in chat, delivered"}};

    // second message is also delivered.
    queries += RawDatabase::Query{
        "INSERT INTO history (id, timestamp, chat_id, message, sender_alias) VALUES (5, 5, 2, ?, 0)",
        {"first message by us in chat, delivered"}};

    // third message is pending, but not broken since there are no delivered messages after it.
    queries += RawDatabase::Query{
        "INSERT INTO history (id, timestamp, chat_id, message, sender_alias) VALUES (6, 6, 2, ?, 0)",
        {"last message in chat, by us, pending"}};
    queries += {"INSERT INTO faux_offline_pending (id) VALUES ("
                                        "    last_insert_rowid()"
                                        ");"};

    QVERIFY(db->execNow(queries));
    QVERIFY(dbSchema1to2(*db));
    verifyDb(db, schema2);

    long brokenCount = -1;
    RawDatabase::Query brokenCountQuery = {"SELECT COUNT(*) FROM broken_messages;", [&](const QVector<QVariant>& row) {
        brokenCount = row[0].toLongLong();
    }};
    QVERIFY(db->execNow(brokenCountQuery));
    QVERIFY(brokenCount == 1); // only friend 1's first message is "broken"

    int fauxOfflineCount = -1;
    RawDatabase::Query fauxOfflineCountQuery = {"SELECT COUNT(*) FROM faux_offline_pending;", [&](const QVector<QVariant>& row) {
        fauxOfflineCount = row[0].toLongLong();
    }};
    QVERIFY(db->execNow(fauxOfflineCountQuery));
    // both friend 1's third message and friend 2's third message should still be pending.
    //The broken message should no longer be pending.
    QVERIFY(fauxOfflineCount == 2);

    int totalHisoryCount = -1;
    RawDatabase::Query totalHistoryCountQuery = {"SELECT COUNT(*) FROM history;", [&](const QVector<QVariant>& row) {
        totalHisoryCount = row[0].toLongLong();
    }};
    QVERIFY(db->execNow(totalHistoryCountQuery));
    QVERIFY(totalHisoryCount == 6); // all messages should still be in history.
}

void TestDbSchema::test2to3()
{
    auto db = std::shared_ptr<RawDatabase>{new RawDatabase{"test2to3.db", {}, {}}};
    createSchemaAtVersion(db, schema2);

    // since we don't enforce foreign key contraints in the db, we can stick in IDs to other tables
    // to avoid generating proper entries for peers and aliases tables, since they aren't actually
    // relevant for the test.

    QVector<RawDatabase::Query> queries;
    // pending message, should be moved out
    queries += RawDatabase::Query{
        "INSERT INTO history (id, timestamp, chat_id, message, sender_alias) VALUES (1, 1, 0, ?, 0)",
        {"/me "}};
    queries += {"INSERT INTO faux_offline_pending (id) VALUES ("
                                        "    last_insert_rowid()"
                                        ");"};

    // non pending message with the content "/me ". Maybe it was sent by a friend using a different client.
    queries += RawDatabase::Query{
        "INSERT INTO history (id, timestamp, chat_id, message, sender_alias) VALUES (2, 2, 0, ?, 2)",
        {"/me "}};

    // non pending message sent by us
    queries += RawDatabase::Query{
        "INSERT INTO history (id, timestamp, chat_id, message, sender_alias) VALUES (3, 3, 0, ?, 1)",
        {"a normal message"}};

    // pending normal message sent by us
    queries += RawDatabase::Query{
        "INSERT INTO history (id, timestamp, chat_id, message, sender_alias) VALUES (4, 3, 0, ?, 1)",
        {"a normal faux offline message"}};
    queries += {"INSERT INTO faux_offline_pending (id) VALUES ("
                                        "    last_insert_rowid()"
                                        ");"};
    QVERIFY(db->execNow(queries));
    QVERIFY(dbSchema2to3(*db));

    long brokenCount = -1;
    RawDatabase::Query brokenCountQuery = {"SELECT COUNT(*) FROM broken_messages;", [&](const QVector<QVariant>& row) {
        brokenCount = row[0].toLongLong();
    }};
    QVERIFY(db->execNow(brokenCountQuery));
    QVERIFY(brokenCount == 1);

    int fauxOfflineCount = -1;
    RawDatabase::Query fauxOfflineCountQuery = {"SELECT COUNT(*) FROM faux_offline_pending;", [&](const QVector<QVariant>& row) {
        fauxOfflineCount = row[0].toLongLong();
    }};
    QVERIFY(db->execNow(fauxOfflineCountQuery));
    QVERIFY(fauxOfflineCount == 1);

    int totalHisoryCount = -1;
    RawDatabase::Query totalHistoryCountQuery = {"SELECT COUNT(*) FROM history;", [&](const QVector<QVariant>& row) {
        totalHisoryCount = row[0].toLongLong();
    }};
    QVERIFY(db->execNow(totalHistoryCountQuery));
    QVERIFY(totalHisoryCount == 4);

    verifyDb(db, schema3);
}

void TestDbSchema::test3to4()
{
    auto db = std::shared_ptr<RawDatabase>{new RawDatabase{"test3to4.db", {}, {}}};
    createSchemaAtVersion(db, schema3);
    QVERIFY(dbSchema3to4(*db));
    verifyDb(db, schema4);
}

void TestDbSchema::test4to5()
{
    auto db = std::shared_ptr<RawDatabase>{new RawDatabase{"test4to5.db", {}, {}}};
    createSchemaAtVersion(db, schema4);
    QVERIFY(dbSchema4to5(*db));
    verifyDb(db, schema5);
}

void TestDbSchema::test5to6()
{
    auto db = std::shared_ptr<RawDatabase>{new RawDatabase{"test5to6.db", {}, {}}};
    createSchemaAtVersion(db, schema5);
    QVERIFY(dbSchema5to6(*db));
    verifyDb(db, schema6);
}

void TestDbSchema::test6to7()
{
    auto db = std::shared_ptr<RawDatabase>{new RawDatabase{"test6to7.db", {}, {}}};
    // foreign_keys are enabled by History constructor and required for this upgrade to work on older sqlite versions
    db->execNow(
        "PRAGMA foreign_keys = ON;");
    createSchemaAtVersion(db, schema6);
    QVERIFY(dbSchema6to7(*db));
    verifyDb(db, schema7);
}

QTEST_GUILESS_MAIN(TestDbSchema)
#include "dbschema_test.moc"
