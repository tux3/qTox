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

#ifndef LOADHISTORYDIALOG_H
#define LOADHISTORYDIALOG_H

#include "src/core/toxpk.h"
#include <QDateTime>
#include <QDialog>

namespace Ui {
class LoadHistoryDialog;
}

class LoadHistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoadHistoryDialog(const ToxPk& contactId, QWidget* parent = nullptr);
    explicit LoadHistoryDialog(QWidget* parent = nullptr);
    ~LoadHistoryDialog();

    QDateTime getFromDate();
    void setTitle(const QString& title);
    void setInfoLabel(const QString& info);

public slots:
    void highlightDates(int year, int month);

private:
    Ui::LoadHistoryDialog* ui;
    const ToxPk contactId;
};

#endif // LOADHISTORYDIALOG_H
