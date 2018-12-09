#ifndef DELETECONTACTDIALOG_H
#define DELETECONTACTDIALOG_H


#include "ui_removecontactdialog.h"
#include "src/model/contact.h"
#include <QDialog>


class RemoveContactDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RemoveContactDialog(QWidget* parent, const Contact* contact);

    inline bool removeHistory()
    {
        return ui.removeHistory->isChecked();
    }

    inline bool accepted()
    {
        return _accepted;
    }

public slots:
    void onAccepted();

protected:
    Ui_RemoveContactDialog ui;
    bool _accepted = false;
};

#endif // DELETECONTACTDIALOG_H
