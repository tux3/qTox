/*
    Copyright © 2014-2015 by The qTox Project Contributors

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

#ifndef PRIVACYFORM_H
#define PRIVACYFORM_H

#include "genericsettings.h"

namespace Ui {
class PrivacySettings;
}

class PrivacyForm : public GenericForm
{
    Q_OBJECT
public:
    PrivacyForm();
    ~PrivacyForm();
    virtual QString getFormName() final override
    {
        return tr("Privacy");
    }

private slots:
    void on_cbKeepHistory_stateChanged();
    void on_cbTypingNotification_stateChanged();
    void on_nospamLineEdit_editingFinished();
    void on_randomNosapamButton_clicked();
    void on_nospamLineEdit_textChanged();
    void on_blackListTextEdit_textChanged();
    virtual void showEvent(QShowEvent*) final override;

private:
    void retranslateUi();

private:
    Ui::PrivacySettings* bodyUI;
};

#endif
