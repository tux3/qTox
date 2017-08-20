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

#include "addfriendform.h"
#include "src/core/core.h"
#include "src/net/toxme.h"
#include "src/net/toxme.h"
#include "src/nexus.h"
#include "src/persistence/settings.h"
#include "src/widget/contentlayout.h"
#include "src/widget/gui.h"
#include "src/widget/tool/croppinglabel.h"
#include "src/widget/translator.h"
#include <QApplication>
#include <QClipboard>
#include <QErrorMessage>
#include <QFont>
#include <QFileDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalMapper>
#include <QTabWidget>
#include <QWindow>
#include <tox/tox.h>

/**
 * @var QString AddFriendForm::lastUsername
 * @brief Cached username so we can retranslate the invite message
 */

AddFriendForm::AddFriendForm()
{
    tabWidget = new QTabWidget();
    main = new QWidget(tabWidget), head = new QWidget();
    QFont bold;
    bold.setBold(true);
    headLabel.setFont(bold);
    toxIdLabel.setTextFormat(Qt::RichText);

    main->setLayout(&layout);
    layout.addWidget(&toxIdLabel);
    layout.addWidget(&toxId);
    layout.addWidget(&messageLabel);
    layout.addWidget(&message);
    layout.addWidget(&sendButton);
    tabWidget->addTab(main, QString());

    importContacts = new QWidget(tabWidget);
    importContacts->setLayout(&importContactsLayout);
    importFileLine.addWidget(&importFileLabel);
    importFileLine.addStretch();
    importFileLine.addWidget(&importFileButton);
    importContactsLayout.addLayout(&importFileLine);
    importContactsLayout.addWidget(&importMessageLabel);
    importContactsLayout.addWidget(&importMessage);
    importContactsLayout.addWidget(&importSendButton);
    tabWidget->addTab(importContacts, QString());

    QScrollArea* scrollArea = new QScrollArea(tabWidget);
    QWidget* requestWidget = new QWidget(tabWidget);
    scrollArea->setWidget(requestWidget);
    scrollArea->setWidgetResizable(true);
    requestsLayout = new QVBoxLayout(requestWidget);
    requestsLayout->addStretch(1);
    tabWidget->addTab(scrollArea, QString());

    head->setLayout(&headLayout);
    headLayout.addWidget(&headLabel);

    connect(&toxId, &QLineEdit::returnPressed, this, &AddFriendForm::onSendTriggered);
    connect(&toxId, &QLineEdit::textChanged, this, &AddFriendForm::onIdChanged);
    connect(tabWidget, &QTabWidget::currentChanged, this, &AddFriendForm::onCurrentChanged);
    connect(&sendButton, &QPushButton::clicked, this, &AddFriendForm::onSendTriggered);
    connect(&importSendButton, &QPushButton::clicked, this, &AddFriendForm::onImportSendClicked);
    connect(&importFileButton, &QPushButton::clicked, this, &AddFriendForm::onImportOpenClicked);
    connect(Nexus::getCore(), &Core::usernameSet, this, &AddFriendForm::onUsernameSet);

    // accessibility stuff
    toxIdLabel.setAccessibleDescription(
        tr("Tox ID, either 76 hexadecimal characters or name@example.com"));
    toxId.setAccessibleDescription(tr("Type in Tox ID of your friend"));
    messageLabel.setAccessibleDescription(tr("Friend request message"));
    message.setAccessibleDescription(tr(
        "Type message to send with the friend request or leave empty to send a default message"));
    message.setTabChangesFocus(true);

    retranslateUi();
    Translator::registerHandler(std::bind(&AddFriendForm::retranslateUi, this), this);

    const int size = Settings::getInstance().getFriendRequestSize();
    for (int i = 0; i < size; ++i) {
        Settings::Request request = Settings::getInstance().getFriendRequest(i);
        addFriendRequestWidget(request.address, request.message);
    }
}

AddFriendForm::~AddFriendForm()
{
    Translator::unregister(this);
    head->deleteLater();
    tabWidget->deleteLater();
}

bool AddFriendForm::isShown() const
{
    if (head->isVisible()) {
        head->window()->windowHandle()->alert(0);
        return true;
    }

    return false;
}

void AddFriendForm::show(ContentLayout* contentLayout)
{
    contentLayout->mainContent->layout()->addWidget(tabWidget);
    contentLayout->mainHead->layout()->addWidget(head);
    tabWidget->show();
    head->show();
    setIdFromClipboard();
    toxId.setFocus();

    // Fix #3421
    // Needed to update tab after opening window
    const int index = tabWidget->currentIndex();
    onCurrentChanged(index);
}

QString AddFriendForm::getMessage() const
{
    const QString msg = message.toPlainText();
    return !msg.isEmpty() ? msg : message.placeholderText();
}

QString AddFriendForm::getImportMessage() const
{
    const QString msg = importMessage.toPlainText();
    return !msg.isEmpty() ? msg : importMessage.placeholderText();
}

void AddFriendForm::setMode(Mode mode)
{
    tabWidget->setCurrentIndex(mode);
}

bool AddFriendForm::addFriendRequest(const QString& friendAddress, const QString& message)
{
    if (Settings::getInstance().addFriendRequest(friendAddress, message)) {
        addFriendRequestWidget(friendAddress, message);
        if (isShown()) {
            onCurrentChanged(tabWidget->currentIndex());
        }

        return true;
    }
    return false;
}

void AddFriendForm::onUsernameSet(const QString& username)
{
    lastUsername = username;
    retranslateUi();
}

static void AddFriendForm::addFriend(const QString& idText)
{
    ToxId friendId(idText);

    if (!friendId.isValid()) {
        friendId = Toxme::lookup(idText); // Try Toxme
        if (!friendId.isValid()) {
            GUI::showWarning(tr("Couldn't add friend"),
                             tr("%1 Tox ID is invalid or does not exist", "Toxme error")
                             .arg(idText));
            return;
        }
    }

    deleteFriendRequest(friendId);
    if (friendId == Core::getInstance()->getSelfId()) {
        GUI::showWarning(tr("Couldn't add friend"),
                         tr("You can't add yourself as a friend!",
                            "When trying to add your own Tox ID as friend"));
    } else {
        emit friendRequested(friendId, getMessage());
    }
}

void AddFriendForm::onSendTriggered()
{
    const QString idText = toxId.text().trimmed();
    addFriend(idText);

    this->toxId.clear();
    this->message.clear();
}

void AddFriendForm::onImportSendClicked()
{
    for (const QString& idText : contactsToImport) {
        addFriend(idText);
    }

    contactsToImport.clear();
    importMessage.clear();
    retranslateUi(); // Update the importFileLabel
}

static inline bool checkIsValidId(const QString& id)
{
    static const QRegularExpression dnsIdExpression("^\\S+@\\S+$");
    return ToxId::isToxId(id) || id.contains(dnsIdExpression);
}

void AddFriendForm::onImportOpenClicked()
{
    QString path = QFileDialog::getOpenFileName(tabWidget, tr("Open contact list"));
    if (path.isEmpty()) {
        return;
    }

    QFile contactFile(path);
    if (!contactFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        GUI::showWarning(tr("Couldn't open file"),
                         tr("Couldn't open the contact file",
                            "Error message when trying to open a contact list file to import"));
        return;
    }

    contactsToImport = QString::fromUtf8(contactFile.readAll()).split('\n');
    QMutableListIterator<QString> it(contactsToImport);
    while (it.hasNext()) {
        const QString id = it.value().trimmed();
        const bool valid = !id.isEmpty() && checkIsValidId(id);
        if (valid) {
            it.value() = id;
        } else {
            it.remove();
        }
        qDebug() << it.next();
    }

    if (contactsToImport.isEmpty()) {
        GUI::showWarning(tr("Invalid file"),
                         tr("We couldn't find any contacts to import in this file!"));
    }

    retranslateUi(); // Update the importFileLabel to show how many contacts we have
}

void AddFriendForm::onIdChanged(const QString& id)
{
    QString tId = id.trimmed();
    bool isValidId = tId.isEmpty() || checkIsValidId(tId);

    QString toxIdText(tr("Tox ID", "Tox ID of the person you're sending a friend request to"));
    QString toxIdComment(
        tr("either 76 hexadecimal characters or name@example.com", "Tox ID format description"));

    if (isValidId) {
        toxIdLabel.setText(toxIdText + QStringLiteral(" (") + toxIdComment + QStringLiteral(")"));
    } else {
        toxIdLabel.setText(toxIdText + QStringLiteral(" <font color='red'>(") + toxIdComment
                           + QStringLiteral(")</font>"));
    }

    toxId.setStyleSheet(isValidId ? QStringLiteral("")
                                  : QStringLiteral("QLineEdit { background-color: #FFC1C1; }"));
    toxId.setToolTip(isValidId ? QStringLiteral("") : tr("Invalid Tox ID format"));

    sendButton.setEnabled(isValidId && !tId.isEmpty());
}

void AddFriendForm::setIdFromClipboard()
{
    QClipboard* clipboard = QApplication::clipboard();
    QString id = clipboard->text().trimmed();
    const Core* core = Core::getInstance();
    if (core->isReady() && !id.isEmpty() && ToxId::isToxId(id) && ToxId(id) != core->getSelfId()) {
        toxId.setText(id);
    }
}

void AddFriendForm::deleteFriendRequest(const ToxId& toxId)
{
    int size = Settings::getInstance().getFriendRequestSize();
    for (int i = 0; i < size; ++i) {
        Settings::Request request = Settings::getInstance().getFriendRequest(i);
        if (toxId == ToxId(request.address)) {
            Settings::getInstance().removeFriendRequest(i);
            return;
        }
    }
}

void AddFriendForm::onFriendRequestAccepted()
{
    QPushButton* acceptButton = static_cast<QPushButton*>(sender());
    QWidget* friendWidget = acceptButton->parentWidget();
    int index = requestsLayout->indexOf(friendWidget);
    removeFriendRequestWidget(friendWidget);
    Settings::Request request =
        Settings::getInstance().getFriendRequest(requestsLayout->count() - index - 1);
    emit friendRequestAccepted(ToxId(request.address).getPublicKey());
    Settings::getInstance().removeFriendRequest(requestsLayout->count() - index - 1);
    Settings::getInstance().savePersonal();
}

void AddFriendForm::onFriendRequestRejected()
{
    QPushButton* rejectButton = static_cast<QPushButton*>(sender());
    QWidget* friendWidget = rejectButton->parentWidget();
    int index = requestsLayout->indexOf(friendWidget);
    removeFriendRequestWidget(friendWidget);
    Settings::getInstance().removeFriendRequest(requestsLayout->count() - index - 1);
    Settings::getInstance().savePersonal();
}

void AddFriendForm::onCurrentChanged(int index)
{
    if (index == FriendRequest && Settings::getInstance().getUnreadFriendRequests() != 0) {
        Settings::getInstance().clearUnreadFriendRequests();
        Settings::getInstance().savePersonal();
        emit friendRequestsSeen();
    }
}

void AddFriendForm::retranslateUi()
{
    headLabel.setText(tr("Add Friends"));
    static const QString messageLabelText = tr("Message",
                                               "The message you send in friend requests");
    messageLabel.setText(messageLabelText);
    importMessageLabel.setText(messageLabelText);
    importFileButton.setText(tr("Open", "Button to choose a file with a list of contacts to import"));
    importSendButton.setText(tr("Send friend requests"));
    sendButton.setText(tr("Send friend request"));
    message.setPlaceholderText(tr("%1 here! Tox me maybe?", "Default message in friend requests if "
                                                            "the field is left blank. Write "
                                                            "something appropriate!")
                                   .arg(lastUsername));
    importMessage.setPlaceholderText(message.placeholderText());

    if (contactsToImport.isEmpty()) {
        importFileLabel.setText(tr("Import a list of contacts, one Tox ID per line"));
    } else {
        importFileLabel.setText(tr("Ready to import %n contact(s), click send to confirm",
                                   "Shows the number of contacts we're about to import from a file"
                                   " (at least one)", contactsToImport.size()));
    }

    onIdChanged(toxId.text());

    tabWidget->setTabText(0, tr("Add a friend"));
    tabWidget->setTabText(1, tr("Import contacts"));
    tabWidget->setTabText(2, tr("Friend requests"));

    for (QPushButton* acceptButton : acceptButtons) {
        retranslateAcceptButton(acceptButton);
    }

    for (QPushButton* rejectButton : rejectButtons) {
        retranslateRejectButton(rejectButton);
    }
}

void AddFriendForm::addFriendRequestWidget(const QString& friendAddress, const QString& message)
{
    QWidget* friendWidget = new QWidget(tabWidget);
    QHBoxLayout* friendLayout = new QHBoxLayout(friendWidget);
    QVBoxLayout* horLayout = new QVBoxLayout();
    horLayout->setMargin(0);
    friendLayout->addLayout(horLayout);

    CroppingLabel* friendLabel = new CroppingLabel(friendWidget);
    friendLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    friendLabel->setText("<b>" + friendAddress + "</b>");
    horLayout->addWidget(friendLabel);

    QLabel* messageLabel = new QLabel(message);
    // allow to select text, but treat links as plaintext to prevent phishing
    messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    messageLabel->setTextFormat(Qt::PlainText);
    messageLabel->setWordWrap(true);
    horLayout->addWidget(messageLabel, 1);

    QPushButton* acceptButton = new QPushButton(friendWidget);
    acceptButtons.append(acceptButton);
    connect(acceptButton, &QPushButton::released, this, &AddFriendForm::onFriendRequestAccepted);
    friendLayout->addWidget(acceptButton);
    retranslateAcceptButton(acceptButton);

    QPushButton* rejectButton = new QPushButton(friendWidget);
    rejectButtons.append(rejectButton);
    connect(rejectButton, &QPushButton::released, this, &AddFriendForm::onFriendRequestRejected);
    friendLayout->addWidget(rejectButton);
    retranslateRejectButton(rejectButton);

    requestsLayout->insertWidget(0, friendWidget);
}

void AddFriendForm::removeFriendRequestWidget(QWidget* friendWidget)
{
    int index = requestsLayout->indexOf(friendWidget);
    requestsLayout->removeWidget(friendWidget);
    acceptButtons.removeAt(index);
    rejectButtons.removeAt(index);
    friendWidget->deleteLater();
}

void AddFriendForm::retranslateAcceptButton(QPushButton* acceptButton)
{
    acceptButton->setText(tr("Accept"));
}

void AddFriendForm::retranslateRejectButton(QPushButton* rejectButton)
{
    rejectButton->setText(tr("Reject"));
}
