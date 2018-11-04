/*
    Copyright © 2018 by The qTox Project Contributors

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

#include "src/net/bootstrapnodeupdater.h"

#include <QNetworkProxy>
#include <QSignalSpy>

#include <QtTest/QtTest>

class TestBootstrapNodesUpdater : public QObject
{
    Q_OBJECT
public:
    TestBootstrapNodesUpdater();
private slots:
    void testOnline();
};



TestBootstrapNodesUpdater::TestBootstrapNodesUpdater()
{
    qRegisterMetaType<QList<DhtServer>>("QList<DhtServer>");
}

void TestBootstrapNodesUpdater::testOnline()
{
    QNetworkProxy proxy{QNetworkProxy::ProxyType::NoProxy};
    BootstrapNodeUpdater updater{proxy};
    QSignalSpy spy(&updater, &BootstrapNodeUpdater::availableBootstrapNodes);

    updater.requestBootstrapNodes();

    spy.wait();
    QCOMPARE(spy.count(), 1); // make sure the signal was emitted exactly one time
}

QTEST_GUILESS_MAIN(TestBootstrapNodesUpdater)
#include "bsu_test.moc"
