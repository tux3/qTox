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

#ifndef CONTACT_H
#define CONTACT_H

#include <QObject>
#include <QString>

#include <src/core/toxpk.h>

class Contact : public QObject
{
    Q_OBJECT
public:
    virtual ~Contact() = 0;

    virtual void setName(const QString& name) = 0;
    virtual QString getDisplayedName() const = 0;
    virtual uint32_t getId() const = 0;
    virtual const ToxPk getPersistentId() const = 0;

    virtual void setEventFlag(bool flag) = 0;
    virtual bool getEventFlag() const = 0;

signals:
    void displayedNameChanged(const QString& newName);
};

#endif // CONTACT_H
