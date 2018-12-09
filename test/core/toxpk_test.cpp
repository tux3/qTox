/*
    Copyright © 2017-2018 by The qTox Project Contributors

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

#include "src/core/toxid.h"

#include <QtTest/QtTest>
#include <QByteArray>
#include <QString>

const uint8_t testPkArray[32] = {0xC7, 0x71, 0x9C, 0x68, 0x08, 0xC1, 0x4B, 0x77, 0x34, 0x80, 0x04,
                                 0x95, 0x6D, 0x1D, 0x98, 0x04, 0x6C, 0xE0, 0x9A, 0x34, 0x37, 0x0E,
                                 0x76, 0x08, 0x15, 0x0E, 0xAD, 0x74, 0xC3, 0x81, 0x5D, 0x30};

const QString testStr =
    QStringLiteral("C7719C6808C14B77348004956D1D98046CE09A34370E7608150EAD74C3815D30");
const QByteArray testPk = QByteArray::fromHex(testStr.toLatin1());

const QString echoStr =
    QStringLiteral("76518406F6A9F2217E8DC487CC783C25CC16A15EB36FF32E335A235342C48A39");
const QByteArray echoPk = QByteArray::fromHex(echoStr.toLatin1());

class TestToxPk : public QObject
{
    Q_OBJECT
private slots:
    void toStringTest();
    void equalTest();
    void clearTest();
    void copyTest();
    void publicKeyTest();
    void hashableTest();
};

void TestToxPk::toStringTest()
{
    ToxPk pk(testPk);
    QVERIFY(testStr == pk.toString());
}

void TestToxPk::equalTest()
{
    ToxPk pk1(testPk);
    ToxPk pk2(testPk);
    ToxPk pk3(echoPk);
    QVERIFY(pk1 == pk2);
    QVERIFY(pk1 != pk3);
    QVERIFY(!(pk1 != pk2));
}

void TestToxPk::clearTest()
{
    ToxPk empty;
    ToxPk pk(testPk);
    QVERIFY(empty.isEmpty());
    QVERIFY(!pk.isEmpty());
}

void TestToxPk::copyTest()
{
    ToxPk src(testPk);
    ToxPk copy = src;
    QVERIFY(copy == src);
}

void TestToxPk::publicKeyTest()
{
    ToxPk pk(testPk);
    QVERIFY(testPk == pk.getKey());
    for (int i = 0; i < ToxPk::getPkSize(); i++) {
        QVERIFY(testPkArray[i] == pk.getBytes()[i]);
    }
}

void TestToxPk::hashableTest()
{
    ToxPk pk1{testPkArray};
    ToxPk pk2{testPk};
    QVERIFY(qHash(pk1) == qHash(pk2));
    ToxPk pk3{echoPk};
    QVERIFY(qHash(pk1) != qHash(pk3));
}

QTEST_GUILESS_MAIN(TestToxPk)
#include "toxpk_test.moc"

