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

#include "userinterfaceform.h"
#include "ui_userinterfacesettings.h"

#include <QDebug>
#include <QColorDialog>
#include <QDesktopWidget>
#include <QFileDialog>
#include <QFont>
#include <QMessageBox>
#include <QRegularExpressionValidator>
#include <QStyleFactory>
#include <QTime>
#include <QVector>

#include "src/core/core.h"
#include "src/core/coreav.h"
#include "src/persistence/profile.h"
#include "src/persistence/settings.h"
#include "src/persistence/smileypack.h"
#include "src/widget/form/settingswidget.h"
#include "src/widget/style.h"
#include "src/widget/tool/recursivesignalblocker.h"
#include "src/widget/translator.h"
#include "src/widget/widget.h"

/**
 * @class UserInterfaceForm
 *
 * This form contains all settings which change the UI appearance or behaviour.
 * It also contains the smiley configuration.
 */

/**
 * @brief Constructor of UserInterfaceForm.
 * @param myParent Setting widget which will contain this form as tab.
 *
 * Restores all controls from the settings.
 */
UserInterfaceForm::UserInterfaceForm(SettingsWidget* myParent)
    : GenericForm(QPixmap(":/img/settings/general.png"))
{
    parent = myParent;

    bodyUI = new Ui::UserInterfaceSettings;
    bodyUI->setupUi(this);

    // block all child signals during initialization
    const RecursiveSignalBlocker signalBlocker(this);

    Settings& s = Settings::getInstance();
    const QFont chatBaseFont = s.getChatMessageFont();
    bodyUI->txtChatFontSize->setValue(QFontInfo(chatBaseFont).pixelSize());
    bodyUI->txtChatFont->setCurrentFont(chatBaseFont);
    int index = static_cast<int>(s.getStylePreference());
    bodyUI->textStyleComboBox->setCurrentIndex(index);
    bodyUI->useNameColors->setChecked(s.getEnableGroupChatsColor());

    bodyUI->notify->setChecked(s.getNotify());
    // Note: UI is boolean inversed from settings to maintain setting file backwards compatibility
    bodyUI->groupOnlyNotfiyWhenMentioned->setChecked(!s.getGroupAlwaysNotify());
    bodyUI->groupOnlyNotfiyWhenMentioned->setEnabled(s.getNotify());
    bodyUI->notifySound->setChecked(s.getNotifySound());
    bodyUI->notifyHide->setChecked(s.getNotifyHide());
    bodyUI->notifySound->setEnabled(s.getNotify());
    bodyUI->busySound->setChecked(s.getBusySound());
    bodyUI->busySound->setEnabled(s.getNotifySound() && s.getNotify());
#if DESKTOP_NOTIFICATIONS
    bodyUI->desktopNotify->setChecked(s.getDesktopNotify());
    bodyUI->desktopNotify->setEnabled(s.getNotify());
#else
    bodyUI->desktopNotify->hide();
#endif

    bodyUI->showWindow->setChecked(s.getShowWindow());

    bodyUI->cbGroupchatPosition->setChecked(s.getGroupchatPosition());
    bodyUI->cbCompactLayout->setChecked(s.getCompactLayout());
    bodyUI->cbSeparateWindow->setChecked(s.getSeparateWindow());
    bodyUI->cbDontGroupWindows->setChecked(s.getDontGroupWindows());
    bodyUI->cbDontGroupWindows->setEnabled(s.getSeparateWindow());
    bodyUI->cbShowIdenticons->setChecked(s.getShowIdenticons());

    bodyUI->useEmoticons->setChecked(s.getUseEmoticons());
    for (auto entry : SmileyPack::listSmileyPacks())
        bodyUI->smileyPackBrowser->addItem(entry.first, entry.second);

    smileLabels = {bodyUI->smile1, bodyUI->smile2, bodyUI->smile3, bodyUI->smile4, bodyUI->smile5};

    int currentPack = bodyUI->smileyPackBrowser->findData(s.getSmileyPack());
    bodyUI->smileyPackBrowser->setCurrentIndex(currentPack);
    reloadSmileys();
    bodyUI->smileyPackBrowser->setEnabled(bodyUI->useEmoticons->isChecked());

    bodyUI->styleBrowser->addItem(tr("None"));
    bodyUI->styleBrowser->addItems(QStyleFactory::keys());

    QString style;
    if (QStyleFactory::keys().contains(s.getStyle()))
        style = s.getStyle();
    else
        style = tr("None");

    bodyUI->styleBrowser->setCurrentText(style);

    for (QString color : Style::getThemeColorNames())
        bodyUI->themeColorCBox->addItem(color);

    bodyUI->themeColorCBox->setCurrentIndex(s.getThemeColor());
    bodyUI->useExtraColors->setCheckState(static_cast<Qt::CheckState>(s.getStateExtraColors()));
    bodyUI->choiceExtraColor->setEnabled(s.getStateExtraColors() == Qt::Checked);

    bodyUI->emoticonSize->setValue(s.getEmojiFontPointSize());

    QLocale ql;
    QStringList timeFormats;
    timeFormats << ql.timeFormat(QLocale::ShortFormat) << ql.timeFormat(QLocale::LongFormat)
                << "hh:mm AP"
                << "hh:mm:ss AP"
                << "hh:mm:ss";
    timeFormats.removeDuplicates();
    bodyUI->timestamp->addItems(timeFormats);

    QRegularExpression re(QString("^[^\\n]{0,%0}$").arg(MAX_FORMAT_LENGTH));
    QRegularExpressionValidator* validator = new QRegularExpressionValidator(re, this);
    QString timeFormat = s.getTimestampFormat();

    if (!re.match(timeFormat).hasMatch())
        timeFormat = timeFormats[0];

    bodyUI->timestamp->setCurrentText(timeFormat);
    bodyUI->timestamp->setValidator(validator);
    on_timestamp_editTextChanged(timeFormat);

    QStringList dateFormats;
    dateFormats << QStringLiteral("yyyy-MM-dd") // ISO 8601
                                                // format strings from system locale
                << ql.dateFormat(QLocale::LongFormat) << ql.dateFormat(QLocale::ShortFormat)
                << ql.dateFormat(QLocale::NarrowFormat) << "dd-MM-yyyy"
                << "d-MM-yyyy"
                << "dddd dd-MM-yyyy"
                << "dddd d-MM";

    dateFormats.removeDuplicates();
    bodyUI->dateFormats->addItems(dateFormats);

    QString dateFormat = s.getDateFormat();
    if (!re.match(dateFormat).hasMatch())
        dateFormat = dateFormats[0];

    bodyUI->dateFormats->setCurrentText(dateFormat);
    bodyUI->dateFormats->setValidator(validator);
    on_dateFormats_editTextChanged(dateFormat);

    eventsInit();
    Translator::registerHandler(std::bind(&UserInterfaceForm::retranslateUi, this), this);
}

UserInterfaceForm::~UserInterfaceForm()
{
    Translator::unregister(this);
    delete bodyUI;
}

void UserInterfaceForm::on_styleBrowser_currentIndexChanged(QString style)
{
    if (bodyUI->styleBrowser->currentIndex() == 0)
        Settings::getInstance().setStyle("None");
    else
        Settings::getInstance().setStyle(style);

    this->setStyle(QStyleFactory::create(style));
    parent->setBodyHeadStyle(style);
}

void UserInterfaceForm::on_emoticonSize_editingFinished()
{
    Settings::getInstance().setEmojiFontPointSize(bodyUI->emoticonSize->value());
}

void UserInterfaceForm::on_timestamp_editTextChanged(const QString& format)
{
    QString timeExample = QTime::currentTime().toString(format);
    bodyUI->timeExample->setText(timeExample);

    Settings::getInstance().setTimestampFormat(format);
    QString locale = Settings::getInstance().getTranslation();
    Translator::translate(locale);
}

void UserInterfaceForm::on_dateFormats_editTextChanged(const QString& format)
{
    QString dateExample = QDate::currentDate().toString(format);
    bodyUI->dateExample->setText(dateExample);

    Settings::getInstance().setDateFormat(format);
    QString locale = Settings::getInstance().getTranslation();
    Translator::translate(locale);
}

void UserInterfaceForm::on_useEmoticons_stateChanged()
{
    Settings::getInstance().setUseEmoticons(bodyUI->useEmoticons->isChecked());
    bodyUI->smileyPackBrowser->setEnabled(bodyUI->useEmoticons->isChecked());
}

void UserInterfaceForm::on_textStyleComboBox_currentTextChanged()
{
    Settings::StyleType styleType =
        static_cast<Settings::StyleType>(bodyUI->textStyleComboBox->currentIndex());
    Settings::getInstance().setStylePreference(styleType);
}

void UserInterfaceForm::on_smileyPackBrowser_currentIndexChanged(int index)
{
    QString filename = bodyUI->smileyPackBrowser->itemData(index).toString();
    Settings::getInstance().setSmileyPack(filename);
    reloadSmileys();
}

/**
 * @brief Reload smileys and size information.
 */
void UserInterfaceForm::reloadSmileys()
{
    QList<QStringList> emoticons = SmileyPack::getInstance().getEmoticons();

    // sometimes there are no emoticons available, don't crash in this case
    if (emoticons.isEmpty()) {
        qDebug() << "reloadSmilies: No emoticons found";
        return;
    }

    QStringList smileys;
    for (int i = 0; i < emoticons.size(); ++i)
        smileys.push_front(emoticons.at(i).first());

    emoticonsIcons.clear();
    const QSize size(18, 18);
    for (int i = 0; i < smileLabels.size(); ++i) {
        std::shared_ptr<QIcon> icon = SmileyPack::getInstance().getAsIcon(smileys[i]);
        emoticonsIcons.append(icon);
        smileLabels[i]->setPixmap(icon->pixmap(size));
        smileLabels[i]->setToolTip(smileys[i]);
    }

    // set maximum size of emoji
    QDesktopWidget desktop;
    // 8 is the count of row and column in emoji's in widget
    const int sideSize = 8;
    int maxSide = qMin(desktop.geometry().height() / sideSize, desktop.geometry().width() / sideSize);
    QSize maxSize(maxSide, maxSide);

    QSize actualSize = emoticonsIcons.first()->actualSize(maxSize);
    bodyUI->emoticonSize->setMaximum(actualSize.width());
}

void UserInterfaceForm::on_notify_stateChanged()
{
    const bool notify = bodyUI->notify->isChecked();
    Settings::getInstance().setNotify(notify);
    bodyUI->groupOnlyNotfiyWhenMentioned->setEnabled(notify);
    bodyUI->notifySound->setEnabled(notify);
    bodyUI->busySound->setEnabled(notify && bodyUI->notifySound->isChecked());
    bodyUI->desktopNotify->setEnabled(notify);
}

void UserInterfaceForm::on_notifySound_stateChanged()
{
    const bool notify = bodyUI->notifySound->isChecked();
    Settings::getInstance().setNotifySound(notify);
    bodyUI->busySound->setEnabled(notify);
}

void UserInterfaceForm::on_desktopNotify_stateChanged()
{
    const bool notify = bodyUI->desktopNotify->isChecked();
    Settings::getInstance().setDesktopNotify(notify);
}

void UserInterfaceForm::on_busySound_stateChanged()
{
    Settings::getInstance().setBusySound(bodyUI->busySound->isChecked());
}

void UserInterfaceForm::on_showWindow_stateChanged()
{
    Settings::getInstance().setShowWindow(bodyUI->showWindow->isChecked());
}

void UserInterfaceForm::on_groupOnlyNotfiyWhenMentioned_stateChanged()
{
    // Note: UI is boolean inversed from settings to maintain setting file backwards compatibility
    Settings::getInstance().setGroupAlwaysNotify(!bodyUI->groupOnlyNotfiyWhenMentioned->isChecked());
}

void UserInterfaceForm::on_cbCompactLayout_stateChanged()
{
    Settings::getInstance().setCompactLayout(bodyUI->cbCompactLayout->isChecked());
}

void UserInterfaceForm::on_cbSeparateWindow_stateChanged()
{
    bool checked = bodyUI->cbSeparateWindow->isChecked();
    bodyUI->cbDontGroupWindows->setEnabled(checked);
    Settings::getInstance().setSeparateWindow(checked);
}

void UserInterfaceForm::on_cbDontGroupWindows_stateChanged()
{
    Settings::getInstance().setDontGroupWindows(bodyUI->cbDontGroupWindows->isChecked());
}

void UserInterfaceForm::on_cbGroupchatPosition_stateChanged()
{
    Settings::getInstance().setGroupchatPosition(bodyUI->cbGroupchatPosition->isChecked());
}

void UserInterfaceForm::on_cbShowIdenticons_stateChanged()
{
    Settings::getInstance().setShowIdenticons(bodyUI->cbShowIdenticons->isChecked());
}

void UserInterfaceForm::on_themeColorCBox_currentIndexChanged(int)
{
    int index = bodyUI->themeColorCBox->currentIndex();
    Settings::getInstance().setThemeColor(index);
}

/**
 * @brief Retranslate all elements in the form.
 */
void UserInterfaceForm::retranslateUi()
{
    // Block signals during translation to prevent settings change
    RecursiveSignalBlocker signalBlocker{this};

    bodyUI->retranslateUi(this);

    // Restore text style index once translation is complete
    bodyUI->textStyleComboBox->setCurrentIndex(
        static_cast<int>(Settings::getInstance().getStylePreference()));

    QStringList colorThemes(Style::getThemeColorNames());
    for (int i = 0; i < colorThemes.size(); ++i) {
        bodyUI->themeColorCBox->setItemText(i, colorThemes[i]);
    }

    bodyUI->styleBrowser->setItemText(0, tr("None"));
}

void UserInterfaceForm::on_txtChatFont_currentFontChanged(const QFont& f)
{
    QFont tmpFont = f;
    const int px = bodyUI->txtChatFontSize->value();

    if (QFontInfo(tmpFont).pixelSize() != px)
        tmpFont.setPixelSize(px);

    Settings::getInstance().setChatMessageFont(tmpFont);
}

void UserInterfaceForm::on_txtChatFontSize_valueChanged(int px)
{
    Settings& s = Settings::getInstance();
    QFont tmpFont = s.getChatMessageFont();
    const int fontSize = QFontInfo(tmpFont).pixelSize();

    if (px != fontSize) {
        tmpFont.setPixelSize(px);
        s.setChatMessageFont(tmpFont);
    }
}

void UserInterfaceForm::on_useNameColors_stateChanged(int value)
{
    Settings::getInstance().setEnableGroupChatsColor(value);
}

void UserInterfaceForm::on_choiceExtraColor_clicked()
{
    QColor color(Settings::getInstance().getExtraColor());
    color = QColorDialog::getColor(color, this);
    Settings::getInstance().setExtraColor(color.name());
}

void UserInterfaceForm::on_useExtraColors_stateChanged(int value)
{
    if (value == Qt::Checked) {
        bodyUI->choiceExtraColor->setEnabled(true);
    } else {
        bodyUI->choiceExtraColor->setEnabled(false);
    }

    Settings::getInstance().setStateExtraColors(value);
}

void UserInterfaceForm::on_notifyHide_stateChanged(int value)
{
    Settings::getInstance().setNotifyHide(value);
}

