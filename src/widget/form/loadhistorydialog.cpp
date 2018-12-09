/*
    Copyright © 2014-2018 by The qTox Project Contributors

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

#include "loadhistorydialog.h"
#include "ui_loadhistorydialog.h"
#include "src/nexus.h"
#include "src/persistence/history.h"
#include "src/persistence/profile.h"
#include <QDate>
#include <QTextCharFormat>
#include <QCalendarWidget>

LoadHistoryDialog::LoadHistoryDialog(const ToxPk& contactId, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::LoadHistoryDialog)
    , contactId(contactId)
{
    ui->setupUi(this);
    highlightDates(QDate::currentDate().year(), QDate::currentDate().month());
    connect(ui->fromDate, &QCalendarWidget::currentPageChanged, this,
            &LoadHistoryDialog::highlightDates);
}

LoadHistoryDialog::LoadHistoryDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::LoadHistoryDialog)
{
    ui->setupUi(this);
}

LoadHistoryDialog::~LoadHistoryDialog()
{
    delete ui;
}

QDateTime LoadHistoryDialog::getFromDate()
{
    QDateTime res(ui->fromDate->selectedDate());
    if (res.date().month() != ui->fromDate->monthShown()
        || res.date().year() != ui->fromDate->yearShown()) {
        QDate newDate(ui->fromDate->yearShown(), ui->fromDate->monthShown(), 1);
        res.setDate(newDate);
    }

    return res;
}

void LoadHistoryDialog::setTitle(const QString& title)
{
    setWindowTitle(title);
}

void LoadHistoryDialog::setInfoLabel(const QString& info)
{
    ui->fromLabel->setText(info);
}

void LoadHistoryDialog::highlightDates(int year, int month)
{
    History* history = Nexus::getProfile()->getHistory();
    QDate monthStart(year, month, 1);
    QDate monthEnd(year, month + 1, 1);
    QList<History::DateMessages> counts =
        history->getChatHistoryCounts(this->contactId, monthStart, monthEnd);

    QTextCharFormat format;
    format.setFontWeight(QFont::Bold);

    QCalendarWidget* calendar = ui->fromDate;
    for (History::DateMessages p : counts) {
        format.setToolTip(tr("%1 messages").arg(p.count));
        calendar->setDateTextFormat(monthStart.addDays(p.offsetDays), format);
    }
}
