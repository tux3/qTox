#include "aboutuser.h"
#include "ui_aboutuser.h"
#include "src/persistence/settings.h"
#include "src/persistence/profile.h"
#include "src/nexus.h"

#include <QDir>
#include <QFileDialog>
#include <QMessageBox>

AboutUser::AboutUser(ToxId &toxId, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutUser)
{
    ui->setupUi(this);
    ui->label_4->hide();
    ui->aliases->hide();

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &AboutUser::onAcceptedClicked);
    connect(ui->autoacceptfile, &QCheckBox::clicked, this, &AboutUser::onAutoAcceptDirClicked);
    connect(ui->autoacceptcall, &QCheckBox::clicked, this, &AboutUser::onAutoAcceptCallClicked);
    connect(ui->selectSaveDir, &QPushButton::clicked, this,  &AboutUser::onSelectDirClicked);
    connect(ui->removeHistory, &QPushButton::clicked, this, &AboutUser::onRemoveHistoryClicked);

    this->toxId = toxId;
    QString dir = Settings::getInstance().getAutoAcceptDir(this->toxId);
    ui->autoacceptfile->setChecked(!dir.isEmpty());

    ui->autoacceptcall->setChecked(Settings::getInstance().getAutoAcceptCall(this->toxId));

    ui->selectSaveDir->setEnabled(ui->autoacceptfile->isChecked());

    if(ui->autoacceptfile->isChecked())
        ui->selectSaveDir->setText(Settings::getInstance().getAutoAcceptDir(this->toxId));
}

void AboutUser::setFriend(Friend *f)
{
    this->setWindowTitle(f->getDisplayedName());
    ui->userName->setText(f->getDisplayedName());
    ui->publicKey->setText(QString(f->getToxId().toString()));
    ui->publicKey->setCursorPosition(0); //scroll textline to left
    ui->note->setPlainText(Settings::getInstance().getContactNote(f->getToxId()));

    QPixmap avatar = Nexus::getProfile()->loadAvatar(f->getToxId().toString());
    ui->statusMessage->setText(f->getStatusMessage());
    if(!avatar.isNull()) {
        ui->avatar->setPixmap(avatar);
    } else {
        ui->avatar->setPixmap(QPixmap(":/img/contact_dark.svg"));
    }

}

void AboutUser::onAutoAcceptDirClicked()
{
    QString dir;
    if (!ui->autoacceptfile->isChecked())
    {
        dir = QDir::homePath();
        ui->autoacceptfile->setChecked(false);
        Settings::getInstance().setAutoAcceptDir(this->toxId, "");
        ui->selectSaveDir->setText(tr("Auto accept for this contact is disabled"));
    }
    else if (ui->autoacceptfile->isChecked())
    {
        dir = QFileDialog::getExistingDirectory(this, tr("Choose an auto accept directory",
                                                         "popup title"), dir);
        if(dir.isEmpty())
        {
            ui->autoacceptfile->setChecked(false);
            return; // user canellced
        }
        Settings::getInstance().setAutoAcceptDir(this->toxId, dir);
        ui->selectSaveDir->setText(Settings::getInstance().getAutoAcceptDir(this->toxId));
    }
    Settings::getInstance().saveGlobal();
    ui->selectSaveDir->setEnabled(ui->autoacceptfile->isChecked());
}

void AboutUser::onAutoAcceptCallClicked()
{
    Settings::getInstance().setAutoAcceptCall(this->toxId,ui->autoacceptcall->isChecked());
    Settings::getInstance().saveGlobal();
}

void AboutUser::onSelectDirClicked()
{
    QString dir;
    dir = QFileDialog::getExistingDirectory(this, tr("Choose an auto accept directory",
                                                     "popup title"), dir);
    ui->autoacceptfile->setChecked(true);
    Settings::getInstance().setAutoAcceptDir(this->toxId, dir);
    Settings::getInstance().savePersonal();
}

/**
 * @brief AboutUser::onAcceptedClicked When users clicks the bottom OK button,
 *          save all settings
 */
void AboutUser::onAcceptedClicked()
{
    Settings::getInstance().setContactNote(ui->publicKey->text(), ui->note->toPlainText());
    Settings::getInstance().saveGlobal();
}

void AboutUser::onRemoveHistoryClicked()
{
    History* history = Nexus::getProfile()->getHistory();
    if (history)
        history->removeFriendHistory(toxId.publicKey);
    QMessageBox::information(this,
                                     tr("History removed"),
                                     tr("Chat history with %1 removed!").arg(ui->userName->text().toHtmlEscaped()),
                                     QMessageBox::Ok);
}

AboutUser::~AboutUser()
{
    delete ui;
}
