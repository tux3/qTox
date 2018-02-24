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

#include "widget.h"

#include <cassert>

#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QShortcut>
#include <QString>
#include <QSvgRenderer>
#include <QWindow>
#ifdef Q_OS_MAC
#include <QMenuBar>
#include <QSignalMapper>
#include <QWindow>
#endif

#include "circlewidget.h"
#include "contentdialog.h"
#include "contentlayout.h"
#include "friendlistwidget.h"
#include "friendwidget.h"
#include "groupwidget.h"
#include "src/model/groupinvite.h"
#include "maskablepixmapwidget.h"
#include "splitterrestorer.h"
#include "systemtrayicon.h"
#include "form/groupchatform.h"
#include "src/audio/audio.h"
#include "src/core/core.h"
#include "src/core/coreav.h"
#include "src/model/friend.h"
#include "src/friendlist.h"
#include "src/model/group.h"
#include "src/model/profile/profileinfo.h"
#include "src/grouplist.h"
#include "src/net/autoupdate.h"
#include "src/nexus.h"
#include "src/persistence/offlinemsgengine.h"
#include "src/persistence/profile.h"
#include "src/persistence/settings.h"
#include "src/platform/timer.h"
#include "src/widget/form/addfriendform.h"
#include "src/widget/form/chatform.h"
#include "src/widget/form/filesform.h"
#include "src/widget/form/groupinviteform.h"
#include "src/widget/form/profileform.h"
#include "src/widget/form/settingswidget.h"
#include "src/widget/gui.h"
#include "src/widget/style.h"
#include "src/widget/translator.h"
#include "tool/removefrienddialog.h"

bool toxActivateEventHandler(const QByteArray&)
{
    Widget* widget = Nexus::getDesktopGUI();
    if (!widget)
        return true;
    if (!widget->isActiveWindow())
        widget->forceShow();

    return true;
}

Widget* Widget::instance{nullptr};

Widget::Widget(QWidget* parent)
    : QMainWindow(parent)
    , icon{nullptr}
    , trayMenu{nullptr}
    , ui(new Ui::MainWindow)
    , activeChatroomWidget{nullptr}
    , eventFlag(false)
    , eventIcon(false)
{
    installEventFilter(this);
    QString locale = Settings::getInstance().getTranslation();
    Translator::translate(locale);
}

void Widget::init()
{
    ui->setupUi(this);

    QIcon themeIcon = QIcon::fromTheme("qtox");
    if (!themeIcon.isNull()) {
        setWindowIcon(themeIcon);
    }

    timer = new QTimer();
    timer->start(1000);
    offlineMsgTimer = new QTimer();
    // FIXME: ↓ make a proper fix instead of increasing timeout into ∞
    offlineMsgTimer->start(2 * 60 * 1000);

    icon_size = 15;

    actionShow = new QAction(this);
    connect(actionShow, &QAction::triggered, this, &Widget::forceShow);

    // Preparing icons and set their size
    statusOnline = new QAction(this);
    statusOnline->setIcon(prepareIcon(getStatusIconPath(Status::Online), icon_size, icon_size));
    connect(statusOnline, &QAction::triggered, this, &Widget::setStatusOnline);

    statusAway = new QAction(this);
    statusAway->setIcon(prepareIcon(getStatusIconPath(Status::Away), icon_size, icon_size));
    connect(statusAway, &QAction::triggered, this, &Widget::setStatusAway);

    statusBusy = new QAction(this);
    statusBusy->setIcon(prepareIcon(getStatusIconPath(Status::Busy), icon_size, icon_size));
    connect(statusBusy, &QAction::triggered, this, &Widget::setStatusBusy);

    actionLogout = new QAction(this);
    actionLogout->setIcon(prepareIcon(":/img/others/logout-icon.svg", icon_size, icon_size));

    actionQuit = new QAction(this);
#ifndef Q_OS_OSX
    actionQuit->setMenuRole(QAction::QuitRole);
#endif

    actionQuit->setIcon(prepareIcon(":/ui/rejectCall/rejectCall.svg", icon_size, icon_size));
    connect(actionQuit, &QAction::triggered, qApp, &QApplication::quit);

    layout()->setContentsMargins(0, 0, 0, 0);
    ui->friendList->setStyleSheet(
        Style::resolve(Style::getStylesheet(":/ui/friendList/friendList.css")));

    profilePicture = new MaskablePixmapWidget(this, QSize(40, 40), ":/img/avatar_mask.svg");
    profilePicture->setPixmap(QPixmap(":/img/contact_dark.svg"));
    profilePicture->setClickable(true);
    profilePicture->setObjectName("selfAvatar");
    profilePicture->setStyleSheet(Style::getStylesheet(":ui/window/profile.css"));
    ui->myProfile->insertWidget(0, profilePicture);
    ui->myProfile->insertSpacing(1, 7);

    filterMenu = new QMenu(this);
    filterGroup = new QActionGroup(this);
    filterDisplayGroup = new QActionGroup(this);

    filterDisplayName = new QAction(this);
    filterDisplayName->setCheckable(true);
    filterDisplayName->setChecked(true);
    filterDisplayGroup->addAction(filterDisplayName);
    filterMenu->addAction(filterDisplayName);
    filterDisplayActivity = new QAction(this);
    filterDisplayActivity->setCheckable(true);
    filterDisplayGroup->addAction(filterDisplayActivity);
    filterMenu->addAction(filterDisplayActivity);
    filterMenu->addSeparator();

    filterAllAction = new QAction(this);
    filterAllAction->setCheckable(true);
    filterAllAction->setChecked(true);
    filterGroup->addAction(filterAllAction);
    filterMenu->addAction(filterAllAction);
    filterOnlineAction = new QAction(this);
    filterOnlineAction->setCheckable(true);
    filterGroup->addAction(filterOnlineAction);
    filterMenu->addAction(filterOnlineAction);
    filterOfflineAction = new QAction(this);
    filterOfflineAction->setCheckable(true);
    filterGroup->addAction(filterOfflineAction);
    filterMenu->addAction(filterOfflineAction);
    filterFriendsAction = new QAction(this);
    filterFriendsAction->setCheckable(true);
    filterGroup->addAction(filterFriendsAction);
    filterMenu->addAction(filterFriendsAction);
    filterGroupsAction = new QAction(this);
    filterGroupsAction->setCheckable(true);
    filterGroup->addAction(filterGroupsAction);
    filterMenu->addAction(filterGroupsAction);

    ui->searchContactFilterBox->setMenu(filterMenu);

#ifndef Q_OS_MAC
    ui->statusHead->setStyleSheet(Style::getStylesheet(":/ui/window/statusPanel.css"));
#endif

    contactListWidget = new FriendListWidget(this, Settings::getInstance().getGroupchatPosition());
    ui->friendList->setWidget(contactListWidget);
    ui->friendList->setLayoutDirection(Qt::RightToLeft);
    ui->friendList->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->statusLabel->setEditable(true);

    ui->statusPanel->setStyleSheet(Style::getStylesheet(":/ui/window/statusPanel.css"));

    QMenu* statusButtonMenu = new QMenu(ui->statusButton);
    statusButtonMenu->addAction(statusOnline);
    statusButtonMenu->addAction(statusAway);
    statusButtonMenu->addAction(statusBusy);
    ui->statusButton->setMenu(statusButtonMenu);

    // disable proportional scaling
    ui->mainSplitter->setStretchFactor(0, 0);
    ui->mainSplitter->setStretchFactor(1, 1);

    onStatusSet(Status::Offline);

    // Disable some widgets until we're connected to the DHT
    ui->statusButton->setEnabled(false);

    Style::setThemeColor(Settings::getInstance().getThemeColor());
    reloadTheme();
    updateIcons();

    filesForm = new FilesForm();
    addFriendForm = new AddFriendForm;
    groupInviteForm = new GroupInviteForm;

    Core* core = Nexus::getCore();
    Profile* profile = Nexus::getProfile();
    profileInfo = new ProfileInfo(core, profile);
    profileForm = new ProfileForm(profileInfo);

    // connect logout tray menu action
    connect(actionLogout, &QAction::triggered, profileForm, &ProfileForm::onLogoutClicked);

    connect(profile, &Profile::selfAvatarChanged, profileForm, &ProfileForm::onSelfAvatarLoaded);

    const Settings& s = Settings::getInstance();

    connect(core, &Core::fileDownloadFinished, filesForm, &FilesForm::onFileDownloadComplete);
    connect(core, &Core::fileUploadFinished, filesForm, &FilesForm::onFileUploadComplete);
    connect(ui->addButton, &QPushButton::clicked, this, &Widget::onAddClicked);
    connect(ui->groupButton, &QPushButton::clicked, this, &Widget::onGroupClicked);
    connect(ui->transferButton, &QPushButton::clicked, this, &Widget::onTransferClicked);
    connect(ui->settingsButton, &QPushButton::clicked, this, &Widget::onShowSettings);
    connect(profilePicture, &MaskablePixmapWidget::clicked, this, &Widget::showProfile);
    connect(ui->nameLabel, &CroppingLabel::clicked, this, &Widget::showProfile);
    connect(ui->statusLabel, &CroppingLabel::editFinished, this, &Widget::onStatusMessageChanged);
    connect(ui->mainSplitter, &QSplitter::splitterMoved, this, &Widget::onSplitterMoved);
    connect(addFriendForm, &AddFriendForm::friendRequested, this, &Widget::friendRequested);
    connect(groupInviteForm, &GroupInviteForm::groupCreate, Core::getInstance(), &Core::createGroup);
    connect(timer, &QTimer::timeout, this, &Widget::onUserAwayCheck);
    connect(timer, &QTimer::timeout, this, &Widget::onEventIconTick);
    connect(timer, &QTimer::timeout, this, &Widget::onTryCreateTrayIcon);
    connect(offlineMsgTimer, &QTimer::timeout, this, &Widget::processOfflineMsgs);
    connect(ui->searchContactText, &QLineEdit::textChanged, this, &Widget::searchContacts);
    connect(filterGroup, &QActionGroup::triggered, this, &Widget::searchContacts);
    connect(filterDisplayGroup, &QActionGroup::triggered, this, &Widget::changeDisplayMode);
    connect(ui->friendList, &QWidget::customContextMenuRequested, this, &Widget::friendListContextMenu);

    // keyboard shortcuts
    new QShortcut(Qt::CTRL + Qt::Key_Q, this, SLOT(close()));
    new QShortcut(Qt::CTRL + Qt::SHIFT + Qt::Key_Tab, this, SLOT(previousContact()));
    new QShortcut(Qt::CTRL + Qt::Key_Tab, this, SLOT(nextContact()));
    new QShortcut(Qt::CTRL + Qt::Key_PageUp, this, SLOT(previousContact()));
    new QShortcut(Qt::CTRL + Qt::Key_PageDown, this, SLOT(nextContact()));
    new QShortcut(Qt::Key_F11, this, SLOT(toggleFullscreen()));

#ifdef Q_OS_MAC
    QMenuBar* globalMenu = Nexus::getInstance().globalMenuBar;
    QAction* windowMenu = Nexus::getInstance().windowMenu->menuAction();
    QAction* viewMenu = Nexus::getInstance().viewMenu->menuAction();
    QAction* frontAction = Nexus::getInstance().frontAction;

    fileMenu = globalMenu->insertMenu(viewMenu, new QMenu(this));

    editProfileAction = fileMenu->menu()->addAction(QString());
    connect(editProfileAction, &QAction::triggered, this, &Widget::showProfile);

    changeStatusMenu = fileMenu->menu()->addMenu(QString());
    fileMenu->menu()->addAction(changeStatusMenu->menuAction());
    changeStatusMenu->addAction(statusOnline);
    changeStatusMenu->addSeparator();
    changeStatusMenu->addAction(statusAway);
    changeStatusMenu->addAction(statusBusy);

    fileMenu->menu()->addSeparator();
    logoutAction = fileMenu->menu()->addAction(QString());
    connect(logoutAction, &QAction::triggered, [this]() { Nexus::getInstance().showLogin(); });

    editMenu = globalMenu->insertMenu(viewMenu, new QMenu(this));
    editMenu->menu()->addSeparator();

    viewMenu->menu()->insertMenu(Nexus::getInstance().fullscreenAction, filterMenu);

    viewMenu->menu()->insertSeparator(Nexus::getInstance().fullscreenAction);

    contactMenu = globalMenu->insertMenu(windowMenu, new QMenu(this));

    addContactAction = contactMenu->menu()->addAction(QString());
    connect(addContactAction, &QAction::triggered, this, &Widget::onAddClicked);

    nextConversationAction = new QAction(this);
    Nexus::getInstance().windowMenu->insertAction(frontAction, nextConversationAction);
    nextConversationAction->setShortcut(QKeySequence::SelectNextPage);
    connect(nextConversationAction, &QAction::triggered, [this]() {
        if (ContentDialog::current() == QApplication::activeWindow())
            ContentDialog::current()->cycleContacts(true);
        else if (QApplication::activeWindow() == this)
            cycleContacts(true);
    });

    previousConversationAction = new QAction(this);
    Nexus::getInstance().windowMenu->insertAction(frontAction, previousConversationAction);
    previousConversationAction->setShortcut(QKeySequence::SelectPreviousPage);
    connect(previousConversationAction, &QAction::triggered, [this] {
        if (ContentDialog::current() == QApplication::activeWindow())
            ContentDialog::current()->cycleContacts(false);
        else if (QApplication::activeWindow() == this)
            cycleContacts(false);
    });

    windowMenu->menu()->insertSeparator(frontAction);

    QAction* preferencesAction = viewMenu->menu()->addAction(QString());
    preferencesAction->setMenuRole(QAction::PreferencesRole);
    connect(preferencesAction, &QAction::triggered, this, &Widget::onShowSettings);

    QAction* aboutAction = viewMenu->menu()->addAction(QString());
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, [this]() {
        onShowSettings();
        settingsWidget->showAbout();
    });

    QMenu* dockChangeStatusMenu = new QMenu(tr("Status"), this);
    dockChangeStatusMenu->addAction(statusOnline);
    statusOnline->setIconVisibleInMenu(true);
    dockChangeStatusMenu->addSeparator();
    dockChangeStatusMenu->addAction(statusAway);
    dockChangeStatusMenu->addAction(statusBusy);
    Nexus::getInstance().dockMenu->addAction(dockChangeStatusMenu->menuAction());

    connect(this, &Widget::windowStateChanged, &Nexus::getInstance(), &Nexus::onWindowStateChanged);
#endif

    contentLayout = nullptr;
    onSeparateWindowChanged(s.getSeparateWindow(), false);

    ui->addButton->setCheckable(true);
    ui->groupButton->setCheckable(true);
    ui->transferButton->setCheckable(true);
    ui->settingsButton->setCheckable(true);

    if (contentLayout) {
        onAddClicked();
    }

    // restore window state
    restoreGeometry(s.getWindowGeometry());
    restoreState(s.getWindowState());
    SplitterRestorer restorer(ui->mainSplitter);
    restorer.restore(s.getSplitterState(), size());

#if (AUTOUPDATE_ENABLED)
    if (s.getCheckUpdates())
        AutoUpdater::checkUpdatesAsyncInteractive();
#endif

    friendRequestsButton = nullptr;
    groupInvitesButton = nullptr;
    unreadGroupInvites = 0;

    connect(addFriendForm, &AddFriendForm::friendRequested, this, &Widget::friendRequestsUpdate);
    connect(addFriendForm, &AddFriendForm::friendRequestsSeen, this, &Widget::friendRequestsUpdate);
    connect(addFriendForm, &AddFriendForm::friendRequestAccepted, this, &Widget::friendRequestAccepted);
    connect(groupInviteForm, &GroupInviteForm::groupInvitesSeen, this, &Widget::groupInvitesClear);
    connect(groupInviteForm, &GroupInviteForm::groupInviteAccepted, this,
            &Widget::onGroupInviteAccepted);

    // settings
    connect(&s, &Settings::showSystemTrayChanged, this, &Widget::onSetShowSystemTray);
    connect(&s, &Settings::separateWindowChanged, this, &Widget::onSeparateWindowClicked);
    connect(&s, &Settings::compactLayoutChanged, contactListWidget,
            &FriendListWidget::onCompactChanged);
    connect(&s, &Settings::groupchatPositionChanged, contactListWidget,
            &FriendListWidget::onGroupchatPositionChanged);

    retranslateUi();
    Translator::registerHandler(std::bind(&Widget::retranslateUi, this), this);

    if (!s.getShowSystemTray()) {
        show();
    }

#ifdef Q_OS_MAC
    Nexus::getInstance().updateWindows();
#endif
}

bool Widget::eventFilter(QObject* obj, QEvent* event)
{
    QWindowStateChangeEvent* ce = nullptr;
    Qt::WindowStates state = windowState();

    switch (event->type()) {
    case QEvent::Close:
        // It's needed if user enable `Close to tray`
        wasMaximized = state.testFlag(Qt::WindowMaximized);
        break;

    case QEvent::WindowStateChange:
        ce = static_cast<QWindowStateChangeEvent*>(event);
        if (state.testFlag(Qt::WindowMinimized) && obj) {
            wasMaximized = ce->oldState().testFlag(Qt::WindowMaximized);
        }

#ifdef Q_OS_MAC
        emit windowStateChanged(windowState());
#endif
        break;
    default:
        break;
    }

    return false;
}

void Widget::updateIcons()
{
    if (!icon) {
        return;
    }

    QString status;
    if (eventIcon) {
        status = QStringLiteral("event");
    } else {
        status = ui->statusButton->property("status").toString();
        if (!status.length()) {
            status = QStringLiteral("offline");
        }
    }

    // Some builds of Qt appear to have a bug in icon loading:
    // QIcon::hasThemeIcon is sometimes unaware that the icon returned
    // from QIcon::fromTheme was a fallback icon, causing hasThemeIcon to
    // incorrectly return true.
    //
    // In qTox this leads to the tray and window icons using the static qTox logo
    // icon instead of an icon based on the current presence status.
    //
    // This workaround checks for an icon that definitely does not exist to
    // determine if hasThemeIcon can be trusted.
    //
    // On systems with the Qt bug, this workaround will always use our included
    // icons but user themes will be unable to override them.
    static bool checkedHasThemeIcon = false;
    static bool hasThemeIconBug = false;

    if (!checkedHasThemeIcon) {
        hasThemeIconBug = QIcon::hasThemeIcon("qtox-asjkdfhawjkeghdfjgh");
        checkedHasThemeIcon = true;

        if (hasThemeIconBug) {
            qDebug()
                << "Detected buggy QIcon::hasThemeIcon. Icon overrides from theme will be ignored.";
        }
    }

    QIcon ico;
    if (!hasThemeIconBug && QIcon::hasThemeIcon("qtox-" + status)) {
        ico = QIcon::fromTheme("qtox-" + status);
    } else {
        QString color = Settings::getInstance().getLightTrayIcon() ? "light" : "dark";
        QString path = ":/img/taskbar/" + color + "/taskbar_" + status + ".svg";
        QSvgRenderer renderer(path);

        // Prepare a QImage with desired characteritisc
        QImage image = QImage(250, 250, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        renderer.render(&painter);
        ico = QIcon(QPixmap::fromImage(image));
    }

    setWindowIcon(ico);
    if (icon) {
        icon->setIcon(ico);
    }
}

Widget::~Widget()
{
    QWidgetList windowList = QApplication::topLevelWidgets();

    for (QWidget* window : windowList) {
        if (window != this) {
            window->close();
        }
    }

    Translator::unregister(this);
#ifdef AUTOUPDATE_ENABLED
    AutoUpdater::abortUpdates();
#endif
    if (icon) {
        icon->hide();
    }

    for (Group* g : GroupList::getAllGroups()) {
        removeGroup(g, true);
    }

    for (Friend* f : FriendList::getAllFriends()) {
        removeFriend(f, true);
    }

    for (auto form : chatForms) {
        delete form;
    }

    for (auto form : groupChatForms) {
        delete form;
    }

    delete icon;
    delete profileForm;
    delete addFriendForm;
    delete groupInviteForm;
    delete filesForm;
    delete timer;
    delete offlineMsgTimer;
    delete contentLayout;

    FriendList::clear();
    GroupList::clear();
    delete trayMenu;
    delete ui;
    instance = nullptr;
}

/**
 * @brief Returns the singleton instance.
 */
Widget* Widget::getInstance()
{
    if (!instance) {
        instance = new Widget();
    }

    return instance;
}

/**
 * @brief Switches to the About settings page.
 */
void Widget::showUpdateDownloadProgress()
{
    onShowSettings();
    settingsWidget->showAbout();
}

void Widget::moveEvent(QMoveEvent* event)
{
    if (event->type() == QEvent::Move) {
        saveWindowGeometry();
        saveSplitterGeometry();
    }

    QWidget::moveEvent(event);
}

void Widget::closeEvent(QCloseEvent* event)
{
    if (Settings::getInstance().getShowSystemTray() && Settings::getInstance().getCloseToTray()) {
        QWidget::closeEvent(event);
    } else {
        if (autoAwayActive) {
            emit statusSet(Status::Online);
            autoAwayActive = false;
        }
        saveWindowGeometry();
        saveSplitterGeometry();
        QWidget::closeEvent(event);
        Nexus::getInstance().quit();
    }
}

void Widget::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange) {
        if (isMinimized() && Settings::getInstance().getShowSystemTray()
            && Settings::getInstance().getMinimizeToTray()) {
            this->hide();
        }
    }
}

void Widget::resizeEvent(QResizeEvent* event)
{
    saveWindowGeometry();
    QMainWindow::resizeEvent(event);
}

QString Widget::getUsername()
{
    return Nexus::getCore()->getUsername();
}

void Widget::onSelfAvatarLoaded(const QPixmap& pic)
{
    profilePicture->setPixmap(pic);
}

void Widget::onConnected()
{
    ui->statusButton->setEnabled(true);
    emit statusSet(Nexus::getCore()->getStatus());
}

void Widget::onDisconnected()
{
    ui->statusButton->setEnabled(false);
    emit Core::getInstance()->statusSet(Status::Offline);
}

void Widget::onFailedToStartCore()
{
    QMessageBox critical(this);
    critical.setText(tr(
        "toxcore failed to start, the application will terminate after you close this message."));
    critical.setIcon(QMessageBox::Critical);
    critical.exec();
    Nexus::getInstance().quit();
}

void Widget::onBadProxyCore()
{
    Settings::getInstance().setProxyType(Settings::ProxyType::ptNone);
    QMessageBox critical(this);
    critical.setText(tr("toxcore failed to start with your proxy settings. "
                        "qTox cannot run; please modify your "
                        "settings and restart.",
                        "popup text"));
    critical.setIcon(QMessageBox::Critical);
    critical.exec();
    onShowSettings();
}

void Widget::onStatusSet(Status status)
{
    ui->statusButton->setProperty("status", getStatusTitle(status));
    ui->statusButton->setIcon(prepareIcon(getStatusIconPath(status), icon_size, icon_size));
    updateIcons();
}

void Widget::onSeparateWindowClicked(bool separate)
{
    onSeparateWindowChanged(separate, true);
}

void Widget::onSeparateWindowChanged(bool separate, bool clicked)
{
    if (!separate) {
        QWindowList windowList = QGuiApplication::topLevelWindows();

        for (QWindow* window : windowList) {
            if (window->objectName() == "detachedWindow") {
                window->close();
            }
        }

        QWidget* contentWidget = new QWidget(this);
        contentLayout = new ContentLayout(contentWidget);
        ui->mainSplitter->addWidget(contentWidget);

        setMinimumWidth(775);

        SplitterRestorer restorer(ui->mainSplitter);
        restorer.restore(Settings::getInstance().getSplitterState(), size());

        onShowSettings();
    } else {
        int width = ui->friendList->size().width();
        QSize size;
        QPoint pos;

        if (contentLayout) {
            pos = mapToGlobal(ui->mainSplitter->widget(1)->pos());
            size = ui->mainSplitter->widget(1)->size();
        }

        if (contentLayout) {
            contentLayout->clear();
            contentLayout->parentWidget()->setParent(0); // Remove from splitter.
            contentLayout->parentWidget()->hide();
            contentLayout->parentWidget()->deleteLater();
            contentLayout->deleteLater();
            contentLayout = nullptr;
        }

        setMinimumWidth(ui->tooliconsZone->sizeHint().width());

        if (clicked) {
            showNormal();
            resize(width, height());

            if (settingsWidget) {
                ContentLayout* contentLayout = createContentDialog((DialogType::SettingDialog));
                contentLayout->parentWidget()->resize(size);
                contentLayout->parentWidget()->move(pos);
                settingsWidget->show(contentLayout);
                setActiveToolMenuButton(ActiveToolMenuButton::None);
            }
        }

        setWindowTitle(QString());
        setActiveToolMenuButton(ActiveToolMenuButton::None);
    }
}

void Widget::setWindowTitle(const QString& title)
{
    if (title.isEmpty()) {
        QMainWindow::setWindowTitle(QApplication::applicationName());
    } else {
        QString tmp = title;
        /// <[^>]*> Regexp to remove HTML tags, in case someone used them in title
        QMainWindow::setWindowTitle(QApplication::applicationName() + QStringLiteral(" - ")
                                    + tmp.remove(QRegExp("<[^>]*>")));
    }
}

void Widget::forceShow()
{
    hide(); // Workaround to force minimized window to be restored
    show();
    activateWindow();
}

void Widget::onAddClicked()
{
    if (Settings::getInstance().getSeparateWindow()) {
        if (!addFriendForm->isShown()) {
            addFriendForm->show(createContentDialog(DialogType::AddDialog));
        }

        setActiveToolMenuButton(ActiveToolMenuButton::None);
    } else {
        hideMainForms(nullptr);
        addFriendForm->show(contentLayout);
        setWindowTitle(fromDialogType(DialogType::AddDialog));
        setActiveToolMenuButton(ActiveToolMenuButton::AddButton);
    }
}

void Widget::onGroupClicked()
{
    if (Settings::getInstance().getSeparateWindow()) {
        if (!groupInviteForm->isShown()) {
            groupInviteForm->show(createContentDialog(DialogType::GroupDialog));
        }

        setActiveToolMenuButton(ActiveToolMenuButton::None);
    } else {
        hideMainForms(nullptr);
        groupInviteForm->show(contentLayout);
        setWindowTitle(fromDialogType(DialogType::GroupDialog));
        setActiveToolMenuButton(ActiveToolMenuButton::GroupButton);
    }
}

void Widget::onTransferClicked()
{
    if (Settings::getInstance().getSeparateWindow()) {
        if (!filesForm->isShown()) {
            filesForm->show(createContentDialog(DialogType::TransferDialog));
        }

        setActiveToolMenuButton(ActiveToolMenuButton::None);
    } else {
        hideMainForms(nullptr);
        filesForm->show(contentLayout);
        setWindowTitle(fromDialogType(DialogType::TransferDialog));
        setActiveToolMenuButton(ActiveToolMenuButton::TransferButton);
    }
}

void Widget::confirmExecutableOpen(const QFileInfo& file)
{
    static const QStringList dangerousExtensions = {"app",  "bat",     "com",    "cpl",  "dmg",
                                                    "exe",  "hta",     "jar",    "js",   "jse",
                                                    "lnk",  "msc",     "msh",    "msh1", "msh1xml",
                                                    "msh2", "msh2xml", "mshxml", "msi",  "msp",
                                                    "pif",  "ps1",     "ps1xml", "ps2",  "ps2xml",
                                                    "psc1", "psc2",    "py",     "reg",  "scf",
                                                    "sh",   "src",     "vb",     "vbe",  "vbs",
                                                    "ws",   "wsc",     "wsf",    "wsh"};

    if (dangerousExtensions.contains(file.suffix())) {
        bool answer = GUI::askQuestion(tr("Executable file", "popup title"),
                                       tr("You have asked qTox to open an executable file. "
                                          "Executable files can potentially damage your computer. "
                                          "Are you sure want to open this file?",
                                          "popup text"),
                                       false, true);
        if (!answer) {
            return;
        }

        // The user wants to run this file, so make it executable and run it
        QFile(file.filePath())
            .setPermissions(file.permissions() | QFile::ExeOwner | QFile::ExeUser | QFile::ExeGroup
                            | QFile::ExeOther);
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(file.filePath()));
}

void Widget::onIconClick(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        if (isHidden() || isMinimized()) {
            if (wasMaximized) {
                showMaximized();
            } else {
                showNormal();
            }

            activateWindow();
        } else if (!isActiveWindow()) {
            activateWindow();
        } else {
            wasMaximized = isMaximized();
            hide();
        }
    } else if (reason == QSystemTrayIcon::Unknown) {
        if (isHidden()) {
            forceShow();
        }
    }
}

void Widget::onShowSettings()
{
    if (!settingsWidget) {
        settingsWidget = new SettingsWidget(this);
    }

    if (Settings::getInstance().getSeparateWindow()) {
        if (!settingsWidget->isShown()) {
            settingsWidget->show(createContentDialog(DialogType::SettingDialog));
        }

        setActiveToolMenuButton(ActiveToolMenuButton::None);
    } else {
        hideMainForms(nullptr);
        settingsWidget->show(contentLayout);
        setWindowTitle(fromDialogType(DialogType::SettingDialog));
        setActiveToolMenuButton(ActiveToolMenuButton::SettingButton);
    }
}

void Widget::showProfile() // onAvatarClicked, onUsernameClicked
{
    if (Settings::getInstance().getSeparateWindow()) {
        if (!profileForm->isShown()) {
            profileForm->show(createContentDialog(DialogType::ProfileDialog));
        }

        setActiveToolMenuButton(ActiveToolMenuButton::None);
    } else {
        hideMainForms(nullptr);
        profileForm->show(contentLayout);
        setWindowTitle(fromDialogType(DialogType::ProfileDialog));
        setActiveToolMenuButton(ActiveToolMenuButton::None);
    }
}

void Widget::hideMainForms(GenericChatroomWidget* chatroomWidget)
{
    setActiveToolMenuButton(ActiveToolMenuButton::None);

    if (contentLayout != nullptr) {
        contentLayout->clear();
    }

    if (activeChatroomWidget != nullptr) {
        activeChatroomWidget->setAsInactiveChatroom();
    }

    activeChatroomWidget = chatroomWidget;
}

void Widget::setUsername(const QString& username)
{
    if (username.isEmpty()) {
        ui->nameLabel->setText(tr("Your name"));
        ui->nameLabel->setToolTip(tr("Your name"));
    } else {
        ui->nameLabel->setText(username);
        ui->nameLabel->setToolTip(
            Qt::convertFromPlainText(username, Qt::WhiteSpaceNormal)); // for overlength names
    }

    QString sanename = username;
    sanename.remove(QRegExp("[\\t\\n\\v\\f\\r\\x0000]"));
    nameMention = QRegExp("\\b" + QRegExp::escape(username) + "\\b", Qt::CaseInsensitive);
    sanitizedNameMention = nameMention;
}

void Widget::onStatusMessageChanged(const QString& newStatusMessage)
{
    // Keep old status message until Core tells us to set it.
    Nexus::getCore()->setStatusMessage(newStatusMessage);
}

void Widget::setStatusMessage(const QString& statusMessage)
{
    if (statusMessage.isEmpty()) {
        ui->statusLabel->setText(tr("Your status"));
        ui->statusLabel->setToolTip(tr("Your status"));
    } else {
        ui->statusLabel->setText(statusMessage);
        // escape HTML from tooltips and preserve newlines
        // TODO: move newspace preservance to a generic function
        ui->statusLabel->setToolTip("<p style='white-space:pre'>" + statusMessage.toHtmlEscaped()
                                    + "</p>");
    }
}

void Widget::reloadHistory()
{
    QDateTime weekAgo = QDateTime::currentDateTime().addDays(-7);
    for (auto f : FriendList::getAllFriends()) {
        chatForms[f->getId()]->loadHistory(weekAgo, true);
    }
}

void Widget::incomingNotification(uint32_t friendId)
{
    newFriendMessageAlert(friendId, false);

    Audio& audio = Audio::getInstance();
    audio.startLoop();
    audio.playMono16Sound(Audio::getSound(Audio::Sound::IncomingCall));
}

void Widget::outgoingNotification()
{
    Audio& audio = Audio::getInstance();
    audio.startLoop();
    audio.playMono16Sound(Audio::getSound(Audio::Sound::OutgoingCall));
}

/**
 * @brief Widget::onStopNotification Stop the notification sound.
 */
void Widget::onStopNotification()
{
    Audio::getInstance().stopLoop();
}

void Widget::onRejectCall(uint32_t friendId)
{
    CoreAV* const av = Core::getInstance()->getAv();
    av->cancelCall(friendId);
}

void Widget::addFriend(uint32_t friendId, const ToxPk& friendPk)
{
    Settings& s = Settings::getInstance();
    s.updateFriendAddress(friendPk.toString());

    Friend* newfriend = FriendList::addFriend(friendId, friendPk);
    bool compact = s.getCompactLayout();
    FriendWidget* widget = new FriendWidget(newfriend, compact);
    History* history = Nexus::getProfile()->getHistory();
    ChatForm* friendForm = new ChatForm(newfriend, history);

    friendWidgets[friendId] = widget;
    chatForms[friendId] = friendForm;

    QDate activityDate = s.getFriendActivity(friendPk);
    QDate chatDate = friendForm->getLatestDate();
    if (chatDate > activityDate && chatDate.isValid()) {
        s.setFriendActivity(friendPk, chatDate);
    }

    contactListWidget->addFriendWidget(widget, Status::Offline, s.getFriendCircleID(friendPk));

    connect(newfriend, &Friend::aliasChanged, this, &Widget::onFriendAliasChanged);
    connect(newfriend, &Friend::displayedNameChanged, this, &Widget::onFriendDisplayedNameChanged);

    connect(friendForm, &ChatForm::incomingNotification, this, &Widget::incomingNotification);
    connect(friendForm, &ChatForm::outgoingNotification, this, &Widget::outgoingNotification);
    connect(friendForm, &ChatForm::stopNotification, this, &Widget::onStopNotification);
    connect(friendForm, &ChatForm::rejectCall, this, &Widget::onRejectCall);

    connect(widget, &FriendWidget::newWindowOpened, this, &Widget::openNewDialog);
    connect(widget, &FriendWidget::chatroomWidgetClicked, this, &Widget::onChatroomWidgetClicked);
    connect(widget, &FriendWidget::chatroomWidgetClicked, friendForm, &ChatForm::focusInput);
    connect(widget, &FriendWidget::copyFriendIdToClipboard, this, &Widget::copyFriendIdToClipboard);
    connect(widget, &FriendWidget::contextMenuCalled, widget, &FriendWidget::onContextMenuCalled);
    connect(widget, SIGNAL(removeFriend(int)), this, SLOT(removeFriend(int)));

    Core* core = Core::getInstance();
    connect(core, &Core::friendAvatarChanged, widget, &FriendWidget::onAvatarChange);
    connect(core, &Core::friendAvatarRemoved, widget, &FriendWidget::onAvatarRemoved);

    // Try to get the avatar from the cache
    QPixmap avatar = Nexus::getProfile()->loadAvatar(friendPk);
    if (!avatar.isNull()) {
        friendForm->onAvatarChange(friendId, avatar);
        widget->onAvatarChange(friendId, avatar);
    }

    FilterCriteria filter = getFilterCriteria();
    widget->search(ui->searchContactText->text(), filterOffline(filter));

    updateFriendActivity(newfriend);
}

void Widget::addFriendFailed(const ToxPk&, const QString& errorInfo)
{
    QString info = QString(tr("Couldn't request friendship"));
    if (!errorInfo.isEmpty()) {
        info = info + QStringLiteral(": ") + errorInfo;
    }

    QMessageBox::critical(0, "Error", info);
}

void Widget::onFriendStatusChanged(int friendId, Status status)
{
    Friend* f = FriendList::findFriend(friendId);
    if (!f) {
        return;
    }

    bool isActualChange = f->getStatus() != status;

    FriendWidget* widget = friendWidgets[friendId];
    if (isActualChange) {
        if (f->getStatus() == Status::Offline) {
            contactListWidget->moveWidget(widget, Status::Online);
        } else if (status == Status::Offline) {
            contactListWidget->moveWidget(widget, Status::Offline);
        }
    }

    f->setStatus(status);
    widget->updateStatusLight();
    if (widget->isActive()) {
        setWindowTitle(widget->getTitle());
    }

    ContentDialog::updateFriendStatus(friendId);
}

void Widget::onFriendStatusMessageChanged(int friendId, const QString& message)
{
    Friend* f = FriendList::findFriend(friendId);
    if (!f) {
        return;
    }

    QString str = message;
    str.replace('\n', ' ').remove('\r').remove(QChar('\0'));
    f->setStatusMessage(str);

    friendWidgets[friendId]->setStatusMsg(str);
    chatForms[friendId]->setStatusMessage(str);

    ContentDialog::updateFriendStatusMessage(friendId, message);
}

void Widget::onFriendDisplayedNameChanged(const QString& displayed)
{
    Friend* f = qobject_cast<Friend*>(sender());
    FriendWidget* friendWidget = friendWidgets[f->getId()];

    if (friendWidget->isActive()) {
        GUI::setWindowTitle(displayed);
    }
}

void Widget::onFriendUsernameChanged(int friendId, const QString& username)
{
    Friend* f = FriendList::findFriend(friendId);
    if (!f) {
        return;
    }

    QString str = username;
    str.replace('\n', ' ').remove('\r').remove(QChar('\0'));
    f->setName(str);
}

void Widget::onFriendAliasChanged(uint32_t friendId, const QString& alias)
{
    Friend* f = qobject_cast<Friend*>(sender());
    FriendWidget* friendWidget = friendWidgets[f->getId()];

    // TODO(sudden6): why is the status updated here?
    Status status = f->getStatus();
    contactListWidget->moveWidget(friendWidget, status);
    FilterCriteria criteria = getFilterCriteria();
    bool filter = status == Status::Offline ? filterOffline(criteria) : filterOnline(criteria);
    friendWidget->searchName(ui->searchContactText->text(), filter);

    // TODO(sudden6): this seems to be a valid use case for the aliasChanged signal
    const ToxPk& pk = f->getPublicKey();
    Settings& s = Settings::getInstance();
    s.setFriendAlias(pk, alias);
    s.savePersonal();
}

void Widget::onChatroomWidgetClicked(GenericChatroomWidget* widget)
{
    openDialog(widget, false);
}

void Widget::openNewDialog(GenericChatroomWidget* widget)
{
    openDialog(widget, true);
}

void Widget::openDialog(GenericChatroomWidget* widget, bool newWindow)
{
    widget->resetEventFlags();
    widget->updateStatusLight();

    uint32_t id;
    GenericChatForm* form;
    const Friend* frnd = widget->getFriend();
    const Group* group = widget->getGroup();
    if (frnd) {
        id = frnd->getId();
        form = chatForms[id];
    } else {
        id = group->getId();
        form = groupChatForms[id];
    }

    bool chatFormIsSet;
    if (frnd) {
        ContentDialog::focusFriend(id);
        chatFormIsSet = ContentDialog::friendWidgetExists(id);
    } else {
        ContentDialog::focusGroup(id);
        chatFormIsSet = ContentDialog::groupWidgetExists(id);
    }

    if ((chatFormIsSet || form->isVisible()) && !newWindow) {
        return;
    }

    if (Settings::getInstance().getSeparateWindow() || newWindow) {
        ContentDialog* dialog = nullptr;

        if (!Settings::getInstance().getDontGroupWindows() && !newWindow) {
            dialog = ContentDialog::current();
        }

        if (dialog == nullptr) {
            dialog = createContentDialog();
        }

        dialog->show();

        if (frnd) {
            addFriendDialog(frnd, dialog);
        } else {
            Group* group = widget->getGroup();
            addGroupDialog(group, dialog);
        }

        dialog->raise();
        dialog->activateWindow();
    } else {
        hideMainForms(widget);
        if (frnd) {
            chatForms[frnd->getId()]->show(contentLayout);
        } else {
            groupChatForms[group->getId()]->show(contentLayout);
        }
        widget->setAsActiveChatroom();
        setWindowTitle(widget->getTitle());
    }
}

void Widget::onFriendMessageReceived(int friendId, const QString& message, bool isAction)
{
    Friend* f = FriendList::findFriend(friendId);
    if (!f) {
        return;
    }

    QDateTime timestamp = QDateTime::currentDateTime();
    Profile* profile = Nexus::getProfile();
    if (profile->isHistoryEnabled()) {
        QString publicKey = f->getPublicKey().toString();
        QString name = f->getDisplayedName();
        QString text = message;
        if (isAction) {
            text = ChatForm::ACTION_PREFIX + f->getDisplayedName() + " " + text;
        }
        profile->getHistory()->addNewMessage(publicKey, text, publicKey, timestamp, true, name);
    }

    newFriendMessageAlert(friendId);
}

void Widget::onReceiptRecieved(int friendId, int receipt)
{
    Friend* f = FriendList::findFriend(friendId);
    if (!f) {
        return;
    }

    chatForms[friendId]->getOfflineMsgEngine()->dischargeReceipt(receipt);
}

void Widget::addFriendDialog(const Friend* frnd, ContentDialog* dialog)
{
    uint32_t friendId = frnd->getId();
    ContentDialog* contentDialog = ContentDialog::getFriendDialog(friendId);
    bool isSeparate = Settings::getInstance().getSeparateWindow();
    FriendWidget* widget = friendWidgets[friendId];
    bool isCurrent = activeChatroomWidget == widget;
    if (!contentDialog && !isSeparate && isCurrent) {
        onAddClicked();
    }

    ChatForm* form = chatForms[friendId];
    FriendWidget* friendWidget = dialog->addFriend(frnd, form);

    friendWidget->setStatusMsg(widget->getStatusMsg());

    connect(friendWidget, SIGNAL(removeFriend(int)), this, SLOT(removeFriend(int)));
    connect(friendWidget, &FriendWidget::copyFriendIdToClipboard, this,
            &Widget::copyFriendIdToClipboard);

    // Signal transmission from the created `friendWidget` (which shown in
    // ContentDialog) to the `widget` (which shown in main widget)
    // FIXME: emit should be removed
    connect(friendWidget, &FriendWidget::contextMenuCalled, widget,
            [=](QContextMenuEvent* event) { emit widget->contextMenuCalled(event); });

    connect(friendWidget, &FriendWidget::chatroomWidgetClicked,
            [=](GenericChatroomWidget* w) {
                Q_UNUSED(w);
                emit widget->chatroomWidgetClicked(widget);
            });
    connect(friendWidget, &FriendWidget::newWindowOpened,
            [=](GenericChatroomWidget* w) {
                Q_UNUSED(w);
                emit widget->newWindowOpened(widget);
            });
    // FIXME: emit should be removed
    emit widget->chatroomWidgetClicked(widget);

    Core* core = Core::getInstance();
    connect(core, &Core::friendAvatarChanged, friendWidget, &FriendWidget::onAvatarChange);
    connect(core, &Core::friendAvatarRemoved, friendWidget, &FriendWidget::onAvatarRemoved);

    QPixmap avatar = Nexus::getProfile()->loadAvatar(frnd->getPublicKey());
    if (!avatar.isNull()) {
        friendWidget->onAvatarChange(friendId, avatar);
    }
}

void Widget::addGroupDialog(Group* group, ContentDialog* dialog)
{
    int groupId = group->getId();
    ContentDialog* groupDialog = ContentDialog::getGroupDialog(groupId);
    bool separated = Settings::getInstance().getSeparateWindow();
    GroupWidget* widget = groupWidgets[groupId];
    bool isCurrentWindow = activeChatroomWidget == widget;
    if (!groupDialog && !separated && isCurrentWindow) {
        onAddClicked();
    }

    auto chatForm = groupChatForms[groupId];
    GroupWidget* groupWidget = dialog->addGroup(group, chatForm);
    connect(groupWidget, SIGNAL(removeGroup(int)), this, SLOT(removeGroup(int)));
    connect(groupWidget, &GroupWidget::chatroomWidgetClicked, chatForm, &GroupChatForm::focusInput);

    // Signal transmission from the created `groupWidget` (which shown in
    // ContentDialog) to the `widget` (which shown in main widget)
    // FIXME: emit should be removed
    connect(groupWidget, &GroupWidget::chatroomWidgetClicked,
            [=](GenericChatroomWidget* w) {
                Q_UNUSED(w);
                emit widget->chatroomWidgetClicked(widget);
            });

    connect(groupWidget, &GroupWidget::newWindowOpened,
            [=](GenericChatroomWidget* w) {
                Q_UNUSED(w);
                emit widget->newWindowOpened(widget);
            });

    // FIXME: emit should be removed
    emit widget->chatroomWidgetClicked(widget);
}

bool Widget::newFriendMessageAlert(int friendId, bool sound)
{
    bool hasActive;
    QWidget* currentWindow;
    ContentDialog* contentDialog = ContentDialog::getFriendDialog(friendId);
    Friend* f = FriendList::findFriend(friendId);

    if (contentDialog != nullptr) {
        currentWindow = contentDialog->window();
        hasActive = ContentDialog::isFriendWidgetActive(friendId);
    } else {
        if (Settings::getInstance().getSeparateWindow() && Settings::getInstance().getShowWindow()) {
            if (Settings::getInstance().getDontGroupWindows()) {
                contentDialog = createContentDialog();
            } else {
                contentDialog = ContentDialog::current();
                if (!contentDialog) {
                    contentDialog = createContentDialog();
                }
            }

            addFriendDialog(f, contentDialog);
            currentWindow = contentDialog->window();
            hasActive = ContentDialog::isFriendWidgetActive(friendId);
        } else {
            currentWindow = window();
            FriendWidget* widget = friendWidgets[friendId];
            hasActive = widget == activeChatroomWidget;
        }
    }

    if (newMessageAlert(currentWindow, hasActive, sound)) {
        FriendWidget* widget = friendWidgets[friendId];
        f->setEventFlag(true);
        widget->updateStatusLight();
        ui->friendList->trackWidget(widget);

        if (contentDialog == nullptr) {
            if (hasActive) {
                setWindowTitle(widget->getTitle());
            }
        } else {
            ContentDialog::updateFriendStatus(friendId);
        }

        return true;
    }

    return false;
}

bool Widget::newGroupMessageAlert(int groupId, bool notify)
{
    bool hasActive;
    QWidget* currentWindow;
    ContentDialog* contentDialog = ContentDialog::getGroupDialog(groupId);
    Group* g = GroupList::findGroup(groupId);
    GroupWidget* widget = groupWidgets[groupId];

    if (contentDialog != nullptr) {
        currentWindow = contentDialog->window();
        hasActive = ContentDialog::isGroupWidgetActive(groupId);
    } else {
        currentWindow = window();
        hasActive = widget == activeChatroomWidget;
    }

    if (!newMessageAlert(currentWindow, hasActive, true, notify)) {
        return false;
    }

    g->setEventFlag(true);
    widget->updateStatusLight();

    if (contentDialog == nullptr) {
        if (hasActive) {
            setWindowTitle(widget->getTitle());
        }
    } else {
        ContentDialog::updateGroupStatus(groupId);
    }

    return true;
}

QString Widget::fromDialogType(DialogType type)
{
    switch (type) {
    case DialogType::AddDialog:
        return tr("Add friend", "title of the window");
    case DialogType::GroupDialog:
        return tr("Group invites", "title of the window");
    case DialogType::TransferDialog:
        return tr("File transfers", "title of the window");
    case DialogType::SettingDialog:
        return tr("Settings", "title of the window");
    case DialogType::ProfileDialog:
        return tr("My profile", "title of the window");
    }

    assert(false);
    return QString();
}

bool Widget::newMessageAlert(QWidget* currentWindow, bool isActive, bool sound, bool notify)
{
    bool inactiveWindow = isMinimized() || !currentWindow->isActiveWindow();

    if (!inactiveWindow && isActive) {
        return false;
    }

    if (notify) {
        if (inactiveWindow) {
            QApplication::alert(currentWindow);
            eventFlag = true;
        }

        if (Settings::getInstance().getShowWindow()) {
            currentWindow->show();
            if (inactiveWindow && Settings::getInstance().getShowInFront()) {
                currentWindow->activateWindow();
            }
        }

        bool isBusy = Nexus::getCore()->getStatus() == Status::Busy;
        bool busySound = Settings::getInstance().getBusySound();
        bool notifySound = Settings::getInstance().getNotifySound();

        if (notifySound && sound && (!isBusy || busySound)) {
            QString soundPath = Audio::getSound(Audio::Sound::NewMessage);
            Audio::getInstance().playMono16Sound(soundPath);
        }
    }

    return true;
}

void Widget::onFriendRequestReceived(const ToxPk& friendPk, const QString& message)
{
    if (addFriendForm->addFriendRequest(friendPk.toString(), message)) {
        friendRequestsUpdate();
        newMessageAlert(window(), isActiveWindow(), true, true);
    }
}

void Widget::updateFriendActivity(const Friend* frnd)
{
    const ToxPk& pk = frnd->getPublicKey();
    QDate date = Settings::getInstance().getFriendActivity(pk);
    if (date != QDate::currentDate()) {
        // Update old activity before after new one. Store old date first.
        QDate oldDate = Settings::getInstance().getFriendActivity(pk);
        Settings::getInstance().setFriendActivity(pk, QDate::currentDate());
        FriendWidget* widget = friendWidgets[frnd->getId()];
        contactListWidget->moveWidget(widget, frnd->getStatus());
        contactListWidget->updateActivityDate(oldDate);
    }
}

void Widget::removeFriend(Friend* f, bool fake)
{
    if (!fake) {
        RemoveFriendDialog ask(this, f);
        ask.exec();

        if (!ask.accepted()) {
            return;
        }

        if (ask.removeHistory()) {
            Nexus::getProfile()->getHistory()->removeFriendHistory(f->getPublicKey().toString());
        }
    }

    const uint32_t friendId = f->getId();
    FriendWidget* widget = friendWidgets[friendId];
    widget->setAsInactiveChatroom();
    if (widget == activeChatroomWidget) {
        activeChatroomWidget = nullptr;
        onAddClicked();
    }

    contactListWidget->removeFriendWidget(widget);

    ContentDialog* lastDialog = ContentDialog::getFriendDialog(friendId);

    if (lastDialog != nullptr) {
        lastDialog->removeFriend(friendId);
    }

    FriendList::removeFriend(friendId, fake);
    Nexus::getCore()->removeFriend(friendId, fake);

    friendWidgets.remove(friendId);
    delete widget;
    delete f;
    if (contentLayout && contentLayout->mainHead->layout()->isEmpty()) {
        onAddClicked();
    }

    contactListWidget->reDraw();
}

void Widget::removeFriend(int friendId)
{
    removeFriend(FriendList::findFriend(friendId), false);
}

void Widget::clearContactsList()
{
    assert(QThread::currentThread() == qApp->thread());

    QList<Friend*> friends = FriendList::getAllFriends();
    for (Friend* f : friends) {
        removeFriend(f, true);
    }

    QList<Group*> groups = GroupList::getAllGroups();
    for (Group* g : groups) {
        removeGroup(g, true);
    }
}

void Widget::onDialogShown(GenericChatroomWidget* widget)
{
    widget->resetEventFlags();
    widget->updateStatusLight();

    ui->friendList->updateTracking(widget);
    resetIcon();
}

void Widget::onFriendDialogShown(const Friend* f)
{
    int friendId = f->getId();
    onDialogShown(friendWidgets[friendId]);
}

void Widget::onGroupDialogShown(Group* g)
{
    uint32_t groupId = g->getId();
    onDialogShown(groupWidgets[groupId]);
}

void Widget::toggleFullscreen()
{
    if (windowState().testFlag(Qt::WindowFullScreen)) {
        setWindowState(windowState() & ~Qt::WindowFullScreen);
    } else {
        setWindowState(windowState() | Qt::WindowFullScreen);
    }
}

ContentDialog* Widget::createContentDialog() const
{
    ContentDialog* contentDialog = new ContentDialog();
    connect(contentDialog, &ContentDialog::friendDialogShown, this, &Widget::onFriendDialogShown);
    connect(contentDialog, &ContentDialog::groupDialogShown, this, &Widget::onGroupDialogShown);
    connect(Core::getInstance(), &Core::usernameSet, contentDialog, &ContentDialog::setUsername);

    Settings& s = Settings::getInstance();
    connect(&s, &Settings::groupchatPositionChanged, contentDialog, &ContentDialog::reorderLayouts);

#ifdef Q_OS_MAC
    Nexus& n = Nexus::getInstance();
    connect(contentDialog, &ContentDialog::destroyed, &n, &Nexus::updateWindowsClosed);
    connect(contentDialog, &ContentDialog::windowStateChanged, &n, &Nexus::onWindowStateChanged);
    connect(contentDialog->windowHandle(), &QWindow::windowTitleChanged, &n, &Nexus::updateWindows);
    n.updateWindows();
#endif

    return contentDialog;
}

ContentLayout* Widget::createContentDialog(DialogType type) const
{
    class Dialog : public ActivateDialog
    {
    public:
        explicit Dialog(DialogType type)
            : ActivateDialog(nullptr, Qt::Window)
            , type(type)
        {
            restoreGeometry(Settings::getInstance().getDialogSettingsGeometry());
            Translator::registerHandler(std::bind(&Dialog::retranslateUi, this), this);
            retranslateUi();
            setWindowIcon(QIcon(":/img/icons/qtox.svg"));

            connect(Core::getInstance(), &Core::usernameSet, this, &Dialog::retranslateUi);
        }

        ~Dialog()
        {
            Translator::unregister(this);
        }

    public slots:

        void retranslateUi()
        {
            setWindowTitle(Core::getInstance()->getUsername() + QStringLiteral(" - ")
                           + Widget::fromDialogType(type));
        }

    protected:
        void resizeEvent(QResizeEvent* event) override
        {
            Settings::getInstance().setDialogSettingsGeometry(saveGeometry());
            QDialog::resizeEvent(event);
        }

        void moveEvent(QMoveEvent* event) override
        {
            Settings::getInstance().setDialogSettingsGeometry(saveGeometry());
            QDialog::moveEvent(event);
        }

    private:
        DialogType type;
    };

    Dialog* dialog = new Dialog(type);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    ContentLayout* contentLayoutDialog = new ContentLayout(dialog);

    dialog->setObjectName("detached");
    dialog->setLayout(contentLayoutDialog);
    dialog->layout()->setMargin(0);
    dialog->layout()->setSpacing(0);
    dialog->setMinimumSize(720, 400);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();

#ifdef Q_OS_MAC
    connect(dialog, &Dialog::destroyed, &Nexus::getInstance(), &Nexus::updateWindowsClosed);
    connect(dialog, &ActivateDialog::windowStateChanged, &Nexus::getInstance(),
            &Nexus::updateWindowsStates);
    connect(dialog->windowHandle(), &QWindow::windowTitleChanged, &Nexus::getInstance(),
            &Nexus::updateWindows);
    Nexus::getInstance().updateWindows();
#endif

    return contentLayoutDialog;
}

void Widget::copyFriendIdToClipboard(int friendId)
{
    Friend* f = FriendList::findFriend(friendId);
    if (f != nullptr) {
        QClipboard* clipboard = QApplication::clipboard();
        const ToxPk& pk = Nexus::getCore()->getFriendPublicKey(f->getId());
        clipboard->setText(pk.toString(), QClipboard::Clipboard);
    }
}

void Widget::onGroupInviteReceived(const GroupInvite& inviteInfo)
{
    const uint32_t friendId = inviteInfo.getFriendId();
    const Friend* f = FriendList::findFriend(friendId);
    updateFriendActivity(f);

    const uint8_t confType = inviteInfo.getType();
    if (confType == TOX_CONFERENCE_TYPE_TEXT || confType == TOX_CONFERENCE_TYPE_AV) {
        if (Settings::getInstance().getAutoGroupInvite(f->getPublicKey())) {
            onGroupInviteAccepted(inviteInfo);
        } else {
            if (!groupInviteForm->addGroupInvite(inviteInfo)) {
                return;
            }

            ++unreadGroupInvites;
            groupInvitesUpdate();
            newMessageAlert(window(), isActiveWindow(), true, true);
        }
    } else {
        qWarning() << "onGroupInviteReceived: Unknown groupchat type:" << confType;
        return;
    }
}

void Widget::onGroupInviteAccepted(const GroupInvite& inviteInfo)
{
    const uint32_t groupId = Core::getInstance()->joinGroupchat(inviteInfo);
    if (groupId == std::numeric_limits<uint32_t>::max()) {
        qWarning() << "onGroupInviteAccepted: Unable to accept group invite";
        return;
    }
}

void Widget::onGroupMessageReceived(int groupnumber, int peernumber, const QString& message,
                                    bool isAction)
{
    Group* g = GroupList::findGroup(groupnumber);
    if (!g) {
        return;
    }

    const Core* core = Core::getInstance();
    ToxPk author = core->getGroupPeerPk(groupnumber, peernumber);
    bool isSelf = author == core->getSelfId().getPublicKey();

    const Settings& s = Settings::getInstance();
    if (s.getBlackList().contains(author.toString())) {
        qDebug() << "onGroupMessageReceived: Filtered:" << author.toString();
        return;
    }

    const auto mention = message.contains(nameMention) || message.contains(sanitizedNameMention);
    const auto targeted = !isSelf && mention;
    const auto groupId = g->getId();
    const auto date = QDateTime::currentDateTime();
    auto form = groupChatForms[groupId];
    if (targeted && !isAction) {
        form->addAlertMessage(author, message, date);
    } else {
        form->addMessage(author, message, date, isAction);
    }

    newGroupMessageAlert(groupId, targeted || Settings::getInstance().getGroupAlwaysNotify());
}

void Widget::onGroupPeerlistChanged(int groupnumber)
{
#if TOX_VERSION_IS_API_COMPATIBLE(0, 2, 0)
    Group* g = GroupList::findGroup(groupnumber);
    if (!g) {
        qDebug() << "onGroupNamelistChanged: Group " << groupnumber << " not found, creating it";
        g = createGroup(groupnumber);
        if (!g) {
            return;
        }
    }
    g->regeneratePeerList();
#endif
}

void Widget::onGroupPeerNameChanged(int groupnumber, int peernumber, const QString &newName)
{
#if TOX_VERSION_IS_API_COMPATIBLE(0, 2, 0)
    Group* g = GroupList::findGroup(groupnumber);
    if (!g) {
        qDebug() << "onGroupNamelistChanged: Group " << groupnumber << " not found, creating it";
        g = createGroup(groupnumber);
        if (!g) {
            return;
        }
    }

    QString setName = newName;
    if (newName.isEmpty()) {
        setName = tr("<Empty>", "Placeholder when someone's name in a group chat is empty");
    }

    g->updatePeer(peernumber, setName);
#endif
}

/**
 * @deprecated Remove after dropping support for toxcore 0.1.x
 */
void Widget::onGroupNamelistChangedOld(int groupnumber, int peernumber, uint8_t Change)
{
#if !(TOX_VERSION_IS_API_COMPATIBLE(0, 2, 0))
    Group* g = GroupList::findGroup(groupnumber);
    if (!g) {
        qDebug() << "onGroupNamelistChanged: Group " << groupnumber << " not found, creating it";
        g = createGroup(groupnumber);
        if (!g) {
            return;
        }
    }

    TOX_CONFERENCE_STATE_CHANGE change = static_cast<TOX_CONFERENCE_STATE_CHANGE>(Change);
    if (change == TOX_CONFERENCE_STATE_CHANGE_PEER_JOIN) {
        g->regeneratePeerList();
    } else if (change == TOX_CONFERENCE_STATE_CHANGE_PEER_EXIT) {
        g->regeneratePeerList();
    } else if (change == TOX_CONFERENCE_STATE_CHANGE_PEER_NAME_CHANGE) // core overwrites old name
                                                                       // before telling us it
                                                                       // changed...
    {
        QString name = Nexus::getCore()->getGroupPeerName(groupnumber, peernumber);
        if (name.isEmpty())
            name = tr("<Empty>", "Placeholder when someone's name in a group chat is empty");

        g->updatePeer(peernumber, name);
    }
#endif
}

void Widget::onGroupTitleChanged(int groupnumber, const QString& author, const QString& title)
{
    Group* g = GroupList::findGroup(groupnumber);
    if (!g) {
        return;
    }

    GroupWidget* widget = groupWidgets[groupnumber];
    if (widget->isActive()) {
        GUI::setWindowTitle(title);
    }

    g->onTitleChanged(author, title);
    FilterCriteria filter = getFilterCriteria();
    widget->searchName(ui->searchContactText->text(), filterGroups(filter));
}

void Widget::onGroupPeerAudioPlaying(int groupnumber, int peernumber)
{
    Group* g = GroupList::findGroup(groupnumber);
    if (!g) {
        return;
    }

    auto form = groupChatForms[g->getId()];
    form->peerAudioPlaying(peernumber);
}

void Widget::removeGroup(Group* g, bool fake)
{
    GroupWidget* widget = groupWidgets[g->getId()];
    widget->setAsInactiveChatroom();
    if (static_cast<GenericChatroomWidget*>(widget) == activeChatroomWidget) {
        activeChatroomWidget = nullptr;
        onAddClicked();
    }

    int groupId = g->getId();
    GroupList::removeGroup(groupId, fake);
    ContentDialog* contentDialog = ContentDialog::getGroupDialog(groupId);
    if (contentDialog != nullptr) {
        contentDialog->removeGroup(groupId);
    }

    groupWidgets.remove(groupId);

    Nexus::getCore()->removeGroup(groupId, fake);
    contactListWidget->removeGroupWidget(widget);

    delete g;
    if (contentLayout && contentLayout->mainHead->layout()->isEmpty()) {
        onAddClicked();
    }

    contactListWidget->reDraw();
}

void Widget::removeGroup(int groupId)
{
    removeGroup(GroupList::findGroup(groupId));
}

Group* Widget::createGroup(int groupId)
{
    Group* g = GroupList::findGroup(groupId);
    if (g) {
        qWarning() << "Group already exists";
        return g;
    }

    const auto groupName = tr("Groupchat #%1").arg(groupId);
    Core* core = Nexus::getCore();
    CoreAV* coreAv = core->getAv();

    bool enabled = coreAv->isGroupAvEnabled(groupId);
    Group* newgroup = GroupList::addGroup(groupId, groupName, enabled, core->getUsername());
    bool compact = Settings::getInstance().getCompactLayout();
    GroupWidget* widget = new GroupWidget(groupId, groupName, compact);
    groupWidgets[groupId] = widget;

    auto form = new GroupChatForm(newgroup);
    groupChatForms[groupId] = form;

    contactListWidget->addGroupWidget(widget);
    widget->updateStatusLight();
    contactListWidget->activateWindow();

    connect(widget, &GroupWidget::chatroomWidgetClicked, this, &Widget::onChatroomWidgetClicked);
    connect(widget, &GroupWidget::newWindowOpened, this, &Widget::openNewDialog);
    connect(widget, SIGNAL(removeGroup(int)), this, SLOT(removeGroup(int)));
    connect(widget, &GroupWidget::chatroomWidgetClicked, form, &ChatForm::focusInput);
    connect(form, &GroupChatForm::sendMessage, core, &Core::sendGroupMessage);
    connect(form, &GroupChatForm::sendAction, core, &Core::sendGroupAction);
    connect(newgroup, &Group::titleChangedByUser, core, &Core::changeGroupTitle);
    connect(core, &Core::usernameSet, newgroup, &Group::setSelfName);

    FilterCriteria filter = getFilterCriteria();
    widget->searchName(ui->searchContactText->text(), filterGroups(filter));

    return newgroup;
}

void Widget::onEmptyGroupCreated(int groupId)
{
    Group* group = createGroup(groupId);
    if (!group) {
        return;
    }

    // Only rename group if groups are visible.
    if (Widget::getInstance()->groupsVisible()) {
        groupWidgets[groupId]->editName();
    }
}

/**
 * @brief Used to reset the blinking icon.
 */
void Widget::resetIcon()
{
    eventIcon = false;
    eventFlag = false;
    updateIcons();
}

bool Widget::event(QEvent* e)
{
    switch (e->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        focusChatInput();
        break;
    case QEvent::Paint:
        ui->friendList->updateVisualTracking();
        break;
    case QEvent::WindowActivate:
        if (activeChatroomWidget) {
            activeChatroomWidget->resetEventFlags();
            activeChatroomWidget->updateStatusLight();
            setWindowTitle(activeChatroomWidget->getTitle());
        }

        if (eventFlag) {
            resetIcon();
        }

        focusChatInput();

#ifdef Q_OS_MAC
        emit windowStateChanged(windowState());

    case QEvent::WindowStateChange:
        Nexus::getInstance().updateWindowsStates();
#endif
        break;
    default:
        break;
    }

    return QMainWindow::event(e);
}

void Widget::onUserAwayCheck()
{
#ifdef QTOX_PLATFORM_EXT
    uint32_t autoAwayTime = Settings::getInstance().getAutoAwayTime() * 60 * 1000;
    bool online = ui->statusButton->property("status").toString() == "online";
    bool away = autoAwayTime && Platform::getIdleTime() >= autoAwayTime;

    if (online && away) {
        qDebug() << "auto away activated at" << QTime::currentTime().toString();
        emit statusSet(Status::Away);
        autoAwayActive = true;
    } else if (autoAwayActive && !away) {
        qDebug() << "auto away deactivated at" << QTime::currentTime().toString();
        emit statusSet(Status::Online);
        autoAwayActive = false;
    }
#endif
}

void Widget::onEventIconTick()
{
    if (eventFlag) {
        eventIcon ^= true;
        updateIcons();
    }
}

void Widget::onTryCreateTrayIcon()
{
    static int32_t tries = 15;
    if (!icon && tries--) {
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            icon = new SystemTrayIcon();
            updateIcons();
            trayMenu = new QMenu(this);

            // adding activate to the top, avoids accidentally clicking quit
            trayMenu->addAction(actionShow);
            trayMenu->addSeparator();
            trayMenu->addAction(statusOnline);
            trayMenu->addAction(statusAway);
            trayMenu->addAction(statusBusy);
            trayMenu->addSeparator();
            trayMenu->addAction(actionLogout);
            trayMenu->addAction(actionQuit);
            icon->setContextMenu(trayMenu);

            // don't activate qTox widget on tray icon click in Unity backend (see #3419)
            if (icon->backend() != SystrayBackendType::Unity)
                connect(icon, &SystemTrayIcon::activated, this, &Widget::onIconClick);

            if (Settings::getInstance().getShowSystemTray()) {
                icon->show();
                setHidden(Settings::getInstance().getAutostartInTray());
            } else {
                show();
            }

#ifdef Q_OS_MAC
            qt_mac_set_dock_menu(Nexus::getInstance().dockMenu);
#endif
        } else if (!isVisible()) {
            show();
        }
    } else {
        disconnect(timer, &QTimer::timeout, this, &Widget::onTryCreateTrayIcon);
        if (!icon) {
            qWarning() << "No system tray detected!";
            show();
        }
    }
}

void Widget::setStatusOnline()
{
    if (!ui->statusButton->isEnabled()) {
        return;
    }

    Nexus::getCore()->setStatus(Status::Online);
}

void Widget::setStatusAway()
{
    if (!ui->statusButton->isEnabled()) {
        return;
    }

    Nexus::getCore()->setStatus(Status::Away);
}

void Widget::setStatusBusy()
{
    if (!ui->statusButton->isEnabled()) {
        return;
    }

    Nexus::getCore()->setStatus(Status::Busy);
}

void Widget::onMessageSendResult(uint32_t friendId, const QString& message, int messageId)
{
    Q_UNUSED(message)
    Q_UNUSED(messageId)
    Friend* f = FriendList::findFriend(friendId);
    if (!f) {
        return;
    }
}

void Widget::onGroupSendFailed(int groupId)
{
    Group* g = GroupList::findGroup(groupId);
    if (!g) {
        return;
    }

    const auto message = tr("Message failed to send");
    const auto curTime = QDateTime::currentDateTime();
    auto form = groupChatForms[g->getId()];
    form->addSystemInfoMessage(message, ChatMessage::INFO, curTime);
}

void Widget::onFriendTypingChanged(int friendId, bool isTyping)
{
    Friend* f = FriendList::findFriend(friendId);
    if (!f) {
        return;
    }

    chatForms[friendId]->setFriendTyping(isTyping);
}

void Widget::onSetShowSystemTray(bool newValue)
{
    if (icon) {
        icon->setVisible(newValue);
    }
}

void Widget::saveWindowGeometry()
{
    Settings::getInstance().setWindowGeometry(saveGeometry());
    Settings::getInstance().setWindowState(saveState());
}

void Widget::saveSplitterGeometry()
{
    if (!Settings::getInstance().getSeparateWindow()) {
        Settings::getInstance().setSplitterState(ui->mainSplitter->saveState());
    }
}

void Widget::onSplitterMoved(int pos, int index)
{
    Q_UNUSED(pos);
    Q_UNUSED(index);
    saveSplitterGeometry();
}

void Widget::cycleContacts(bool forward)
{
    contactListWidget->cycleContacts(activeChatroomWidget, forward);
}

bool Widget::filterGroups(FilterCriteria index)
{
    switch (index) {
    case FilterCriteria::Offline:
    case FilterCriteria::Friends:
        return true;
    default:
        return false;
    }
}

bool Widget::filterOffline(FilterCriteria index)
{
    switch (index) {
    case FilterCriteria::Online:
    case FilterCriteria::Groups:
        return true;
    default:
        return false;
    }
}

bool Widget::filterOnline(FilterCriteria index)
{
    switch (index) {
    case FilterCriteria::Offline:
    case FilterCriteria::Groups:
        return true;
    default:
        return false;
    }
}

void Widget::processOfflineMsgs()
{
    if (OfflineMsgEngine::globalMutex.tryLock()) {
        QList<Friend*> frnds = FriendList::getAllFriends();
        for (Friend* f : frnds) {
            chatForms[f->getId()]->getOfflineMsgEngine()->deliverOfflineMsgs();
        }

        OfflineMsgEngine::globalMutex.unlock();
    }
}

void Widget::clearAllReceipts()
{
    QList<Friend*> frnds = FriendList::getAllFriends();
    for (Friend* f : frnds) {
        chatForms[f->getId()]->getOfflineMsgEngine()->removeAllReceipts();
    }
}

void Widget::reloadTheme()
{
    this->setStyleSheet(Style::getStylesheet(":/ui/window/general.css"));
    QString statusPanelStyle = Style::getStylesheet(":/ui/window/statusPanel.css");
    ui->tooliconsZone->setStyleSheet(Style::getStylesheet(":/ui/tooliconsZone/tooliconsZone.css"));
    ui->statusPanel->setStyleSheet(statusPanelStyle);
    ui->statusHead->setStyleSheet(statusPanelStyle);
    ui->friendList->setStyleSheet(Style::getStylesheet(":/ui/friendList/friendList.css"));
    ui->statusButton->setStyleSheet(Style::getStylesheet(":/ui/statusButton/statusButton.css"));
    contactListWidget->reDraw();

    for (Friend* f : FriendList::getAllFriends()) {
        uint32_t friendId = f->getId();
        friendWidgets[friendId]->reloadTheme();
    }

    for (Group* g : GroupList::getAllGroups()) {
        uint32_t groupId = g->getId();
        groupWidgets[groupId]->reloadTheme();
    }
}

void Widget::nextContact()
{
    cycleContacts(true);
}

void Widget::previousContact()
{
    cycleContacts(false);
}

QString Widget::getStatusIconPath(Status status)
{
    switch (status) {
    case Status::Online:
        return ":/img/status/online.svg";
    case Status::Away:
        return ":/img/status/away.svg";
    case Status::Busy:
        return ":/img/status/busy.svg";
    case Status::Offline:
        return ":/img/status/offline.svg";
    }
    qWarning() << "Status unknown";
    assert(false);
    return QString{};
}

// Preparing needed to set correct size of icons for GTK tray backend
inline QIcon Widget::prepareIcon(QString path, int w, int h)
{
#ifdef Q_OS_LINUX

    QString desktop = getenv("XDG_CURRENT_DESKTOP");
    if (desktop.isEmpty()) {
        desktop = getenv("DESKTOP_SESSION");
    }

    desktop = desktop.toLower();
    if (desktop == "xfce" || desktop.contains("gnome") || desktop == "mate"
        || desktop == "x-cinnamon") {
        if (w > 0 && h > 0) {
            QSvgRenderer renderer(path);

            QPixmap pm(w, h);
            pm.fill(Qt::transparent);
            QPainter painter(&pm);
            renderer.render(&painter, pm.rect());

            return QIcon(pm);
        }
    }
#endif
    return QIcon(path);
}

QPixmap Widget::getStatusIconPixmap(QString path, uint32_t w, uint32_t h)
{
    QPixmap pix(w, h);
    pix.load(path);
    return pix;
}

QString Widget::getStatusTitle(Status status)
{
    switch (status) {
    case Status::Online:
        return QStringLiteral("online");
    case Status::Away:
        return QStringLiteral("away");
    case Status::Busy:
        return QStringLiteral("busy");
    case Status::Offline:
        return QStringLiteral("offline");
    }

    assert(false);
    return QStringLiteral("");
}

Status Widget::getStatusFromString(QString status)
{
    if (status == QStringLiteral("online"))
        return Status::Online;
    else if (status == QStringLiteral("away"))
        return Status::Away;
    else if (status == QStringLiteral("busy"))
        return Status::Busy;
    else
        return Status::Offline;
}

void Widget::searchContacts()
{
    QString searchString = ui->searchContactText->text();
    FilterCriteria filter = getFilterCriteria();

    contactListWidget->searchChatrooms(searchString, filterOnline(filter), filterOffline(filter),
                                       filterGroups(filter));

    updateFilterText();

    contactListWidget->reDraw();
}

void Widget::changeDisplayMode()
{
    filterDisplayGroup->setEnabled(false);

    if (filterDisplayGroup->checkedAction() == filterDisplayActivity) {
        contactListWidget->setMode(FriendListWidget::Activity);
    } else if (filterDisplayGroup->checkedAction() == filterDisplayName) {
        contactListWidget->setMode(FriendListWidget::Name);
    }

    searchContacts();
    filterDisplayGroup->setEnabled(true);

    updateFilterText();
}

void Widget::updateFilterText()
{
    QString action = filterDisplayGroup->checkedAction()->text();
    QString text = filterGroup->checkedAction()->text();
    text = action + QStringLiteral(" | ") + text;
    ui->searchContactFilterBox->setText(text);
}

Widget::FilterCriteria Widget::getFilterCriteria() const
{
    QAction* checked = filterGroup->checkedAction();

    if (checked == filterOnlineAction)
        return FilterCriteria::Online;
    else if (checked == filterOfflineAction)
        return FilterCriteria::Offline;
    else if (checked == filterFriendsAction)
        return FilterCriteria::Friends;
    else if (checked == filterGroupsAction)
        return FilterCriteria::Groups;

    return FilterCriteria::All;
}

void Widget::searchCircle(CircleWidget* circleWidget)
{
    FilterCriteria filter = getFilterCriteria();
    QString text = ui->searchContactText->text();
    circleWidget->search(text, true, filterOnline(filter), filterOffline(filter));
}

void Widget::searchItem(GenericChatItemWidget* chatItem, GenericChatItemWidget::ItemType type)
{
    bool hide;
    FilterCriteria filter = getFilterCriteria();
    switch (type) {
    case GenericChatItemWidget::GroupItem:
        hide = filterGroups(filter);
        break;
    default:
        hide = true;
    }

    chatItem->searchName(ui->searchContactText->text(), hide);
}

bool Widget::groupsVisible() const
{
    FilterCriteria filter = getFilterCriteria();
    return !filterGroups(filter);
}

void Widget::friendListContextMenu(const QPoint& pos)
{
    QMenu menu(this);
    QAction* createGroupAction = menu.addAction(tr("Create new group..."));
    QAction* addCircleAction = menu.addAction(tr("Add new circle..."));
    QAction* chosenAction = menu.exec(ui->friendList->mapToGlobal(pos));

    if (chosenAction == addCircleAction) {
        contactListWidget->addCircleWidget();
    } else if (chosenAction == createGroupAction) {
        Nexus::getCore()->createGroup();
    }
}

void Widget::friendRequestsUpdate()
{
    unsigned int unreadFriendRequests = Settings::getInstance().getUnreadFriendRequests();

    if (unreadFriendRequests == 0) {
        delete friendRequestsButton;
        friendRequestsButton = nullptr;
    } else if (!friendRequestsButton) {
        friendRequestsButton = new QPushButton(this);
        friendRequestsButton->setObjectName("green");
        ui->statusLayout->insertWidget(2, friendRequestsButton);

        connect(friendRequestsButton, &QPushButton::released, [this]() {
            onAddClicked();
            addFriendForm->setMode(AddFriendForm::Mode::FriendRequest);
        });
    }

    if (friendRequestsButton) {
        friendRequestsButton->setText(tr("%n New Friend Request(s)", "", unreadFriendRequests));
    }
}

void Widget::groupInvitesUpdate()
{
    if (unreadGroupInvites == 0) {
        delete groupInvitesButton;
        groupInvitesButton = nullptr;
    } else if (!groupInvitesButton) {
        groupInvitesButton = new QPushButton(this);
        groupInvitesButton->setObjectName("green");
        ui->statusLayout->insertWidget(2, groupInvitesButton);

        connect(groupInvitesButton, &QPushButton::released, this, &Widget::onGroupClicked);
    }

    if (groupInvitesButton) {
        groupInvitesButton->setText(tr("%n New Group Invite(s)", "", unreadGroupInvites));
    }
}

void Widget::groupInvitesClear()
{
    unreadGroupInvites = 0;
    groupInvitesUpdate();
}

void Widget::setActiveToolMenuButton(ActiveToolMenuButton newActiveButton)
{
    ui->addButton->setChecked(newActiveButton == ActiveToolMenuButton::AddButton);
    ui->addButton->setDisabled(newActiveButton == ActiveToolMenuButton::AddButton);
    ui->groupButton->setChecked(newActiveButton == ActiveToolMenuButton::GroupButton);
    ui->groupButton->setDisabled(newActiveButton == ActiveToolMenuButton::GroupButton);
    ui->transferButton->setChecked(newActiveButton == ActiveToolMenuButton::TransferButton);
    ui->transferButton->setDisabled(newActiveButton == ActiveToolMenuButton::TransferButton);
    ui->settingsButton->setChecked(newActiveButton == ActiveToolMenuButton::SettingButton);
    ui->settingsButton->setDisabled(newActiveButton == ActiveToolMenuButton::SettingButton);
}

void Widget::retranslateUi()
{
    Core* core = Nexus::getCore();
    ui->retranslateUi(this);
    setUsername(core->getUsername());
    setStatusMessage(core->getStatusMessage());

    filterDisplayName->setText(tr("By Name"));
    filterDisplayActivity->setText(tr("By Activity"));
    filterAllAction->setText(tr("All"));
    filterOnlineAction->setText(tr("Online"));
    filterOfflineAction->setText(tr("Offline"));
    filterFriendsAction->setText(tr("Friends"));
    filterGroupsAction->setText(tr("Groups"));
    ui->searchContactText->setPlaceholderText(tr("Search Contacts"));
    updateFilterText();

    ui->searchContactText->setPlaceholderText(tr("Search Contacts"));
    statusOnline->setText(tr("Online", "Button to set your status to 'Online'"));
    statusAway->setText(tr("Away", "Button to set your status to 'Away'"));
    statusBusy->setText(tr("Busy", "Button to set your status to 'Busy'"));
    actionLogout->setText(tr("Logout", "Tray action menu to logout user"));
    actionQuit->setText(tr("Exit", "Tray action menu to exit tox"));
    actionShow->setText(tr("Show", "Tray action menu to show qTox window"));

    if (!Settings::getInstance().getSeparateWindow() && (settingsWidget && settingsWidget->isShown())) {
        setWindowTitle(fromDialogType(DialogType::SettingDialog));
    }

    friendRequestsUpdate();
    groupInvitesUpdate();


#ifdef Q_OS_MAC
    Nexus::getInstance().retranslateUi();

    filterMenu->menuAction()->setText(tr("Filter..."));

    fileMenu->setText(tr("File"));
    editMenu->setText(tr("Edit"));
    contactMenu->setText(tr("Contacts"));
    changeStatusMenu->menuAction()->setText(tr("Change Status"));
    editProfileAction->setText(tr("Edit Profile"));
    logoutAction->setText(tr("Log out"));
    addContactAction->setText(tr("Add Contact..."));
    nextConversationAction->setText(tr("Next Conversation"));
    previousConversationAction->setText(tr("Previous Conversation"));
#endif
}

void Widget::focusChatInput()
{
    if (activeChatroomWidget) {
        if (const Friend* f = activeChatroomWidget->getFriend()) {
            chatForms[f->getId()]->focusInput();
        } else if (Group* g = activeChatroomWidget->getGroup()) {
            groupChatForms[g->getId()]->focusInput();
        }
    }
}
