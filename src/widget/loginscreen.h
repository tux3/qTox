/*
    Copyright © 2015 by The qTox Project

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


#ifndef LOGINSCREEN_H
#define LOGINSCREEN_H

#include <QWidget>
#include <QShortcut>

namespace Ui {
class LoginScreen;
}

class LoginScreen : public QWidget
{
    Q_OBJECT

public:
    explicit LoginScreen(QWidget *parent = 0);
    ~LoginScreen();
    void reset(); ///< Resets the UI, clears all fields

private slots:
    void onAutoLoginToggled(int state);
    void onLoginUsernameSelected(const QString& name);
    void onPasswordEdited();

    // Buttons to change page
    void onNewProfilePageClicked();
    void onLoginPageClicked();
    void onImportProfileClicked();

    // Buttons to submit form
    void onCreateNewProfile();
    void onLogin();

private:
    void retranslateUi();

private:
    Ui::LoginScreen *ui;
    QShortcut quitShortcut;
};

#endif // LOGINSCREEN_H
