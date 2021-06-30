/*
    Copyright © 2014-2019 by The qTox Project Contributors

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

#pragma once

#include "genericsettings.h"
#include "src/widget/form/settingswidget.h"

#include <memory>

namespace Ui {
class UserInterfaceSettings;
}

class UserInterfaceForm : public GenericForm
{
    Q_OBJECT
public:
    explicit UserInterfaceForm(SettingsWidget* myParent);
    ~UserInterfaceForm();
    QString getFormName() final
    {
        return tr("User Interface");
    }

private slots:
    void on_smileyPackBrowser_currentIndexChanged(int index);
    void on_emoticonSize_editingFinished();
    void on_styleBrowser_currentIndexChanged(QString style);
    void on_timestamp_editTextChanged(const QString& format);
    void on_dateFormats_editTextChanged(const QString& format);
    void on_textStyleComboBox_currentTextChanged();
    void on_useEmoticons_stateChanged();
    void on_notify_stateChanged();
    void on_desktopNotify_stateChanged();
    void on_notifySound_stateChanged();
    void on_notifyHide_stateChanged(int);
    void on_busySound_stateChanged();
    void on_showWindow_stateChanged();
    void on_groupOnlyNotfiyWhenMentioned_stateChanged();
    void on_cbCompactLayout_stateChanged();
    void on_cbSeparateWindow_stateChanged();
    void on_cbDontGroupWindows_stateChanged();
    void on_cbGroupchatPosition_stateChanged();
    void on_themeColorCBox_currentIndexChanged(int);
    void on_cbShowIdenticons_stateChanged();
    void on_txtChatFont_currentFontChanged(const QFont& f);
    void on_txtChatFontSize_valueChanged(int arg1);
    void on_useNameColors_stateChanged(int value);
    void on_choiceExtraColor_clicked();
    void on_useExtraColors_stateChanged(int value);

private:
    void retranslateUi();
    void reloadSmileys();

private:
    QList<QLabel*> smileLabels;
    QList<std::shared_ptr<QIcon>> emoticonsIcons;
    SettingsWidget* parent;
    Ui::UserInterfaceSettings* bodyUI;
    const int MAX_FORMAT_LENGTH = 128;
};
