/*
 * This file is part of telepathy-contactslist-prototype
 *
 * Copyright (C) 2009-2010 Collabora Ltd. <info@collabora.co.uk>
 *   @Author George Goldberg <george.goldberg@collabora.co.uk>
 * Copyright (C) 2011 Martin Klapetek <martin.klapetek@gmail.com>
 * Copyright (C) 2011 Keith Rusler <xzekecomax@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "main-widget.h"

#include <QtGui/QSortFilterProxyModel>
#include <QtGui/QPainter>
#include <QtGui/QMenu>
#include <QtGui/QLabel>
#include <QtGui/QCheckBox>
#include <QtGui/QPushButton>
#include <QtGui/QToolButton>
#include <QtCore/QWeakPointer>
#include <QWidgetAction>
#include <QCloseEvent>

#include <TelepathyQt4/PendingReady>
#include <TelepathyQt4/PendingChannelRequest>
#include <TelepathyQt4/PendingContacts>
#include <TelepathyQt4/ClientRegistrar>
#include <TelepathyQt4/Constants>
#include <TelepathyQt4/ContactManager>

#include <KDebug>
#include <KDialog>
#include <KIO/Job>
#include <KUser>
#include <KMenu>
#include <KMessageBox>
#include <KSettings/Dialog>
#include <KSharedConfig>
#include <KFileDialog>
#include <KInputDialog>
#include <KStandardShortcut>
#include <KNotification>

#include "ui_main-widget.h"
#include "account-button.h"
#include "contact-overlays.h"
#include "contact-delegate.h"
#include "contact-delegate-compact.h"
#include "fetch-avatar-job.h"
#include "contact-list-application.h"

#include "dialogs/add-contact-dialog.h"
#include "dialogs/join-chat-room-dialog.h"
#include "dialogs/remove-contact-dialog.h"
#include "dialogs/contact-info.h"


#include "models/groups-model.h"
#include "models/contact-model-item.h"
#include "models/groups-model-item.h"
#include "models/accounts-model.h"
#include "models/accounts-filter-model.h"
#include "models/proxy-tree-node.h"

#define PREFERRED_TEXTCHAT_HANDLER "org.freedesktop.Telepathy.Client.KDE.TextUi"
#define PREFERRED_FILETRANSFER_HANDLER "org.freedesktop.Telepathy.Client.KDE.FileTransfer"
#define PREFERRED_AUDIO_VIDEO_HANDLER "org.freedesktop.Telepathy.Client.KDE.CallUi"

bool kde_tp_filter_contacts_by_publication_status(const Tp::ContactPtr &contact)
{
    return contact->publishState() == Tp::Contact::PresenceStateAsk;
}

MainWidget::MainWidget(QWidget *parent)
    : KMainWindow(parent),
    m_model(0),
    m_modelFilter(0)
{
    Tp::registerTypes();
    KUser user;

    setupUi(this);
    m_filterBar->hide();
    setWindowIcon(KIcon("telepathy-kde"));

    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup guiConfigGroup(config, "GUI");

    m_userAccountNameLabel->setText(user.property(KUser::FullName).isNull() ?
        user.loginName() : user.property(KUser::FullName).toString()
    );

    m_userAccountIconButton->setPopupMode(QToolButton::InstantPopup);

    m_avatarButtonMenu = new KMenu(m_userAccountIconButton);

    QToolButton *loadFromFileButton = new QToolButton(this);
    loadFromFileButton->setIcon(KIcon("document-open-folder"));
    loadFromFileButton->setIconSize(QSize(48, 48));
    loadFromFileButton->setText(i18n("Load from file..."));
    loadFromFileButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    QWidgetAction *loadFromFileAction = new QWidgetAction(this);
    loadFromFileAction->setDefaultWidget(loadFromFileButton);

    connect(loadFromFileButton, SIGNAL(clicked(bool)),
            loadFromFileAction, SIGNAL(triggered(bool)));

    connect(loadFromFileAction, SIGNAL(triggered(bool)),
            this, SLOT(loadAvatarFromFile()));

    m_avatarButtonMenu->addAction(loadFromFileAction);

    m_userAccountIconButton->setMenu(m_avatarButtonMenu);

    m_addContactAction = new KAction(KIcon("list-add-user"), QString(), this);
    m_addContactAction->setToolTip(i18n("Add new contacts.."));

    m_toolBar->addAction(m_addContactAction);

    m_groupContactsAction = new KAction(KIcon("user-group-properties"), QString(), this);
    m_groupContactsAction->setCheckable(true);
    m_groupContactsAction->setChecked(true);
    //TODO: Toggle the tooltip with the button? eg. once its Show, after click its Hide .. ?
    m_groupContactsAction->setToolTip(i18n("Show/Hide groups"));

    m_toolBar->addAction(m_groupContactsAction);

    m_showOfflineAction = new KAction(KIcon("meeting-attending-tentative"), QString(), this);
    m_showOfflineAction->setCheckable(true);
    m_showOfflineAction->setChecked(false);
    m_showOfflineAction->setToolTip(i18n("Hide/Show offline users"));

    m_toolBar->addAction(m_showOfflineAction);

    m_sortByPresenceAction = new KAction(KIcon("view-sort-ascending"), QString(), this);
    m_sortByPresenceAction->setCheckable(true);
    m_sortByPresenceAction->setChecked(false);
    m_sortByPresenceAction->setToolTip(i18n("Sort by presence"));

    m_toolBar->addAction(m_sortByPresenceAction);

    m_searchContactAction = new KAction(KIcon("edit-find-user"), QString(), this );
    m_searchContactAction->setShortcut(KStandardShortcut::find());
    m_searchContactAction->setCheckable(true);
    m_searchContactAction->setChecked(false);
    m_searchContactAction->setToolTip(i18n("Find contact"));

    m_toolBar->addAction(m_searchContactAction);

    QWidget *toolBarSpacer = new QWidget(this);
    toolBarSpacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

    m_toolBar->addWidget(toolBarSpacer);

    QToolButton *settingsButton = new QToolButton(this);
    settingsButton->setIcon(KIcon("configure"));
    settingsButton->setPopupMode(QToolButton::InstantPopup);

    KMenu *settingsButtonMenu = new KMenu(settingsButton);
    settingsButtonMenu->addAction(i18n("Configure accounts..."), this, SLOT(showSettingsKCM()));

    QActionGroup *delegateTypeGroup = new QActionGroup(this);
    delegateTypeGroup->setExclusive(true);

    KMenu *setDelegateTypeMenu = new KMenu(settingsButtonMenu);
    setDelegateTypeMenu->setTitle(i18n("Contact list type"));
    delegateTypeGroup->addAction(setDelegateTypeMenu->addAction(i18n("Use full list"),
                                                                this, SLOT(onSwitchToFullView())));
    delegateTypeGroup->actions().last()->setCheckable(true);

    if (guiConfigGroup.readEntry("selected_delegate", "full") == QLatin1String("full")) {
        delegateTypeGroup->actions().last()->setChecked(true);
    }

    delegateTypeGroup->addAction(setDelegateTypeMenu->addAction(i18n("Use compact list"),
                                                                this, SLOT(onSwitchToCompactView())));
    delegateTypeGroup->actions().last()->setCheckable(true);

    if (guiConfigGroup.readEntry("selected_delegate", "full") == QLatin1String("compact")) {
        delegateTypeGroup->actions().last()->setChecked(true);
    }

    settingsButtonMenu->addMenu(setDelegateTypeMenu);

    settingsButtonMenu->addAction(i18n("Join chat room"), this, SLOT(onJoinChatRoomRequested()));
    settingsButtonMenu->addSeparator();
    settingsButtonMenu->addMenu(helpMenu());

    settingsButton->setMenu(settingsButtonMenu);

    m_toolBar->addWidget(settingsButton);

    // Start setting up the Telepathy AccountManager.
    Tp::AccountFactoryPtr  accountFactory = Tp::AccountFactory::create(QDBusConnection::sessionBus(),
                                                                       Tp::Features() << Tp::Account::FeatureCore
                                                                       << Tp::Account::FeatureAvatar
                                                                       << Tp::Account::FeatureCapabilities
                                                                       << Tp::Account::FeatureProtocolInfo
                                                                       << Tp::Account::FeatureProfile);

    Tp::ConnectionFactoryPtr connectionFactory = Tp::ConnectionFactory::create(QDBusConnection::sessionBus(),
                                                                               Tp::Features() << Tp::Connection::FeatureCore
                                                                               << Tp::Connection::FeatureRosterGroups
                                                                               << Tp::Connection::FeatureRoster
                                                                               << Tp::Connection::FeatureSelfContact);

    Tp::ContactFactoryPtr contactFactory = Tp::ContactFactory::create(Tp::Features()  << Tp::Contact::FeatureAlias
                                                                      << Tp::Contact::FeatureAvatarData
                                                                      << Tp::Contact::FeatureSimplePresence
                                                                      << Tp::Contact::FeatureCapabilities);

    Tp::ChannelFactoryPtr channelFactory = Tp::ChannelFactory::create(QDBusConnection::sessionBus());

    m_accountManager = Tp::AccountManager::create(QDBusConnection::sessionBus(),
                                                  accountFactory,
                                                  connectionFactory,
                                                  channelFactory,
                                                  contactFactory);

    connect(m_accountManager->becomeReady(),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(onAccountManagerReady(Tp::PendingOperation*)));

    connect(m_accountManager.data(), SIGNAL(newAccount(Tp::AccountPtr)),
            this, SLOT(onNewAccountAdded(Tp::AccountPtr)));

    m_delegate = new ContactDelegate(this);
    m_compactDelegate = new ContactDelegateCompact(this);

    m_contactsListView->header()->hide();
    m_contactsListView->setRootIsDecorated(false);
    m_contactsListView->setSortingEnabled(true);
    m_contactsListView->setContextMenuPolicy(Qt::CustomContextMenu);
    if (guiConfigGroup.readEntry("selected_delegate", "full") == QLatin1String("compact")) {
        m_contactsListView->setItemDelegate(m_compactDelegate);
    } else {
        m_contactsListView->setItemDelegate(m_delegate);
    }
    m_contactsListView->setIndentation(0);
    m_contactsListView->setMouseTracking(true);
    m_contactsListView->setExpandsOnDoubleClick(false); //the expanding/collapsing is handled manually

    addOverlayButtons();

    emit enableOverlays(guiConfigGroup.readEntry("selected_delegate", "full") == QLatin1String("full"));

    connect(m_contactsListView, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(onCustomContextMenuRequested(QPoint)));

    connect(m_contactsListView, SIGNAL(clicked(QModelIndex)),
            this, SLOT(onContactListClicked(QModelIndex)));

    connect(m_contactsListView, SIGNAL(doubleClicked(QModelIndex)),
            this, SLOT(onContactListDoubleClick(QModelIndex)));

    connect(m_delegate, SIGNAL(repaintItem(QModelIndex)),
            m_contactsListView->viewport(), SLOT(repaint())); //update(QModelIndex)

    connect(m_addContactAction, SIGNAL(triggered(bool)),
            this, SLOT(onAddContactRequest()));

    connect(m_groupContactsAction, SIGNAL(triggered(bool)),
            this, SLOT(onGroupContacts(bool)));

    connect(m_searchContactAction, SIGNAL(triggered(bool)),
            this, SLOT(toggleSearchWidget(bool)));

    connect(m_presenceMessageEdit, SIGNAL(returnPressed(QString)),
            this, SLOT(setCustomPresenceMessage(QString)));

    if (guiConfigGroup.readEntry("pin_filterbar", true)) {
        toggleSearchWidget(true);
        m_searchContactAction->setChecked(true);
    }
}

MainWidget::~MainWidget()
{
    //save the state of the filter bar, pinned or not
    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup configGroup(config, "GUI");
    configGroup.writeEntry("pin_filterbar", m_searchContactAction->isChecked());
    configGroup.writeEntry("use_groups", m_groupContactsAction->isChecked());
    configGroup.writeEntry("show_offline", m_showOfflineAction->isChecked());
    configGroup.writeEntry("sort_by_presence", m_sortByPresenceAction->isChecked());
    configGroup.config()->sync();
}

void MainWidget::onAccountManagerReady(Tp::PendingOperation* op)
{
    if (op->isError()) {
        kDebug() << op->errorName();
        kDebug() << op->errorMessage();
    }

    m_model = new AccountsModel(m_accountManager, this);
    m_groupsModel = new GroupsModel(m_model, this);
    m_modelFilter = new AccountsFilterModel(this);
    if (m_groupContactsAction->isChecked()) {
        m_modelFilter->setSourceModel(m_groupsModel);
    } else {
        m_modelFilter->setSourceModel(m_model);
    }
    m_modelFilter->setDynamicSortFilter(true);
    m_modelFilter->showOfflineUsers(m_showOfflineAction->isChecked());
    m_modelFilter->clearFilterString();
    m_modelFilter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_modelFilter->setSortRole(Qt::DisplayRole);
    m_modelFilter->setSortByPresence(m_sortByPresenceAction->isChecked());
    m_contactsListView->setModel(m_modelFilter);
    m_contactsListView->setSortingEnabled(true);
    m_contactsListView->sortByColumn(0, Qt::AscendingOrder);

    connect(m_showOfflineAction, SIGNAL(toggled(bool)),
            m_modelFilter, SLOT(showOfflineUsers(bool)));

    connect(m_filterBar, SIGNAL(filterChanged(QString)),
            m_modelFilter, SLOT(setFilterString(QString)));

    connect(m_filterBar, SIGNAL(closeRequest()),
            m_modelFilter, SLOT(clearFilterString()));

    connect(m_filterBar, SIGNAL(closeRequest()),
            m_filterBar, SLOT(hide()));

    connect(m_filterBar, SIGNAL(closeRequest()),
            m_searchContactAction, SLOT(toggle()));

    connect(m_sortByPresenceAction, SIGNAL(toggled(bool)),
            m_modelFilter, SLOT(setSortByPresence(bool)));

    connect(m_modelFilter, SIGNAL(rowsAboutToBeRemoved(QModelIndex, int, int)),
        m_delegate, SLOT(contactRemoved(QModelIndex,int,int)));

    m_accountButtonsLayout->insertStretch(-1);

    QList<Tp::AccountPtr> accounts = m_accountManager->allAccounts();

    if(accounts.count() == 0) {
        KDialog *dialog = new KDialog(this);
        dialog->setCaption(i18n("No Accounts Found"));
        dialog->setButtons(KDialog::Ok | KDialog::Cancel);
        dialog->setMainWidget(new QLabel(i18n("You have no IM accounts configured. Would you like to do that now?")));
        dialog->setButtonText(KDialog::Ok, i18n("Configure Accounts"));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setInitialSize(dialog->sizeHint());
        connect(dialog, SIGNAL(okClicked()), this, SLOT(showSettingsKCM()));
        connect(dialog, SIGNAL(okClicked()), dialog, SLOT(close()));
        connect(dialog, SIGNAL(cancelClicked()), dialog, SLOT(close()));
        dialog->show();
    }

    foreach (const Tp::AccountPtr &account, accounts) {
        onNewAccountAdded(account);
    }

    m_contactsListView->expandAll();

    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup guiConfigGroup(config, "GUI");

    bool useGroups = guiConfigGroup.readEntry("use_groups", true);
    onGroupContacts(useGroups);
    m_groupContactsAction->setChecked(useGroups);

    bool showOffline = guiConfigGroup.readEntry("show_offline", false);
    m_modelFilter->showOfflineUsers(showOffline);
    m_showOfflineAction->setChecked(showOffline);

    bool sortByPresence = guiConfigGroup.readEntry("sort_by_presence", true);
    m_modelFilter->setSortByPresence(sortByPresence);
    m_sortByPresenceAction->setChecked(sortByPresence);
}

void MainWidget::onAccountConnectionStatusChanged(Tp::ConnectionStatus status)
{
    kDebug() << "Connection status is" << status;

    Tp::AccountPtr account(qobject_cast< Tp::Account* >(sender()));
    QModelIndex index = m_model->index(qobject_cast<AccountsModelItem*>(m_model->accountItemForId(account->uniqueIdentifier())));

    switch (status) {
    case Tp::ConnectionStatusConnected:
        m_contactsListView->setExpanded(index, true);
        break;
    case Tp::ConnectionStatusDisconnected:
        handleConnectionError(account);
        break;
    case Tp::ConnectionStatusConnecting:
        m_contactsListView->setExpanded(index, false);
    default:
        break;
    }
}

void MainWidget::onNewAccountAdded(const Tp::AccountPtr& account)
{
    Q_ASSERT(account->isReady(Tp::Account::FeatureCore));

    if (account->connection()) {
        monitorPresence(account->connection());
    }

    connect(account.data(),
            SIGNAL(connectionChanged(Tp::ConnectionPtr)),
            this, SLOT(onConnectionChanged(Tp::ConnectionPtr)));

    connect(account.data(),
            SIGNAL(connectionStatusChanged(Tp::ConnectionStatus)),
            this, SLOT(onAccountConnectionStatusChanged(Tp::ConnectionStatus)));

    connect(account.data(), SIGNAL(stateChanged(bool)),
            this, SLOT(onAccountStateChanged(bool)));

    connect(account.data(),
            SIGNAL(removed()),
            this, SLOT(onAccountRemoved()));

    AccountButton *bt = new AccountButton(account, this);
    bt->setObjectName(account->uniqueIdentifier());
    bt->hide();

    m_accountButtonsLayout->insertWidget(m_accountButtonsLayout->count() - 1, bt);

    if(account->isEnabled()) {
        bt->show();
        loadAvatar(account);
    }

    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup avatarGroup(config, "Avatar");
    if (avatarGroup.readEntry("method", QString()) == QLatin1String("account")) {
        //this also updates the avatar if it was changed somewhere else
        selectAvatarFromAccount(avatarGroup.readEntry("source", QString()));
    }
}

void MainWidget::monitorPresence(const Tp::ConnectionPtr &connection)
{
    kDebug();
    connect(connection->contactManager().data(), SIGNAL(presencePublicationRequested(Tp::Contacts)),
            this, SLOT(onPresencePublicationRequested(Tp::Contacts)));

    connect(connection->contactManager().data(),
            SIGNAL(stateChanged(Tp::ContactListState)),
            this, SLOT(onContactManagerStateChanged(Tp::ContactListState)));
    onContactManagerStateChanged(connection->contactManager(),
                                 connection->contactManager()->state());
}

void MainWidget::onContactManagerStateChanged(Tp::ContactListState state)
{
    onContactManagerStateChanged(Tp::ContactManagerPtr(qobject_cast< Tp::ContactManager* >(sender())), state);
}

void MainWidget::onContactManagerStateChanged(const Tp::ContactManagerPtr &contactManager, Tp::ContactListState state)
{
    if (state == Tp::ContactListStateSuccess) {
        QFutureWatcher< Tp::ContactPtr > *watcher = new QFutureWatcher< Tp::ContactPtr >(this);
        connect(watcher, SIGNAL(finished()), this, SLOT(onAccountsPresenceStatusFiltered()));
        watcher->setFuture(QtConcurrent::filtered(contactManager->allKnownContacts(),
                                                kde_tp_filter_contacts_by_publication_status));

        kDebug() << "Watcher is on";
    }
}

void MainWidget::onAccountStateChanged(bool enabled)
{
    Tp::AccountPtr account(qobject_cast<Tp::Account*>(sender()));

    if(enabled) {
        findChild<AccountButton *>(account->uniqueIdentifier())->show();
    } else {
        findChild<AccountButton *>(account->uniqueIdentifier())->hide();
        showMessageToUser(i18n("Account %1 was disabled.", account->displayName()),
                          MainWidget::SystemMessageError);
    }
}

void MainWidget::onAccountRemoved()
{
    Tp::AccountPtr account(qobject_cast<Tp::Account*>(sender()));
    delete findChild<AccountButton *>(account->uniqueIdentifier());

    showMessageToUser(i18n("Account %1 was removed.", account->displayName()),
                      MainWidget::SystemMessageError);
}

void MainWidget::onConnectionChanged(const Tp::ConnectionPtr& connection)
{
    if(! connection.isNull()) {
        monitorPresence(connection);
    }
}

void MainWidget::onContactListClicked(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }

    if (index.data(AccountsModel::ItemRole).userType() == qMetaTypeId<AccountsModelItem*>()
        || index.data(AccountsModel::ItemRole).userType() == qMetaTypeId<GroupsModelItem*>()) {

        if (m_contactsListView->isExpanded(index)) {
            m_contactsListView->collapse(index);
        } else {
            m_contactsListView->expand(index);
        }
    }
}

void MainWidget::onContactListDoubleClick(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }

    if (index.data(AccountsModel::ItemRole).userType() == qMetaTypeId<ContactModelItem*>()) {
        kDebug() << "Text chat requested for index" << index;
        startTextChannel(index.data(AccountsModel::ItemRole).value<ContactModelItem*>());
    }
}

void MainWidget::startTextChannel(ContactModelItem *contactItem)
{
    Q_ASSERT(contactItem);
    Tp::ContactPtr contact = contactItem->contact();

    kDebug() << "Requesting chat for contact" << contact->alias();

    Tp::AccountPtr account = m_model->accountForContactItem(contactItem);

    Tp::PendingChannelRequest* channelRequest = account->ensureTextChat(contact,
                                                                        QDateTime::currentDateTime(),
                                                                        PREFERRED_TEXTCHAT_HANDLER);
    connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)),
            this, SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
}

void MainWidget::startAudioChannel(ContactModelItem *contactItem)
{
    Q_ASSERT(contactItem);
    Tp::ContactPtr contact = contactItem->contact();

    kDebug() << "Requesting audio for contact" << contact->alias();

    Tp::AccountPtr account = m_model->accountForContactItem(contactItem);

    Tp::PendingChannelRequest* channelRequest = account->ensureStreamedMediaAudioCall(contact,
                                                                        QDateTime::currentDateTime(),
                                                                        PREFERRED_AUDIO_VIDEO_HANDLER);
    connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)),
            this, SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
}

void MainWidget::startVideoChannel(ContactModelItem *contactItem)
{
    Q_ASSERT(contactItem);
    Tp::ContactPtr contact = contactItem->contact();

    kDebug() << "Requesting video for contact" << contact->alias();

    Tp::AccountPtr account = m_model->accountForContactItem(contactItem);

    Tp::PendingChannelRequest* channelRequest = account->ensureStreamedMediaVideoCall(contact, true,
                                                                        QDateTime::currentDateTime(),
                                                                        PREFERRED_AUDIO_VIDEO_HANDLER);
    connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)),
            this, SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
}


void MainWidget::startFileTransferChannel(ContactModelItem *contactItem)
{
    Q_ASSERT(contactItem);
    Tp::ContactPtr contact = contactItem->contact();

    kDebug() << "Requesting file transfer for contact" << contact->alias();

    Tp::AccountPtr account = m_model->accountForContactItem(contactItem);

    QString filename = KFileDialog::getOpenFileName(KUrl("kfiledialog:///FileTransferLastDirectory"),
                                                    QString(),
                                                    this,
                                                    i18n("Choose a file"));

    if (filename.isEmpty()) { // User hit cancel button
        return;
    }

    QFileInfo fileinfo(filename);

    kDebug() << "Filename:" << filename;
    kDebug() << "Content type:" << KMimeType::findByFileContent(filename)->name();
    // TODO Let the user set a description?

    Tp::FileTransferChannelCreationProperties fileTransferProperties(filename,
                                                                     KMimeType::findByFileContent(filename)->name());

    Tp::PendingChannelRequest* channelRequest = account->createFileTransfer(contact,
                                                                            fileTransferProperties,
                                                                            QDateTime::currentDateTime(),
                                                                            PREFERRED_FILETRANSFER_HANDLER);
    connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)), SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
}

void MainWidget::showMessageToUser(const QString& text, const MainWidget::SystemMessageType type)
{
    //The pointer is automatically deleted when the event is closed
    KNotification *notification;
    if (type == MainWidget::SystemMessageError) {
        notification = new KNotification("telepathyError", this);
    } else {
        notification = new KNotification("telepathyInfo", this);
    }

    KAboutData aboutData("ktelepathy",0,KLocalizedString(),0);
    notification->setComponentData(KComponentData(aboutData));

    notification->setText(text);
    notification->sendEvent();
}

void MainWidget::addOverlayButtons()
{
    TextChannelContactOverlay*  textOverlay  = new TextChannelContactOverlay(this);
    AudioChannelContactOverlay* audioOverlay = new AudioChannelContactOverlay(this);
    VideoChannelContactOverlay* videoOverlay = new VideoChannelContactOverlay(this);

    FileTransferContactOverlay* fileOverlay  = new FileTransferContactOverlay(this);

    m_delegate->installOverlay(textOverlay);
    m_delegate->installOverlay(audioOverlay);
    m_delegate->installOverlay(videoOverlay);
    m_delegate->installOverlay(fileOverlay);

    textOverlay->setView(m_contactsListView);
    textOverlay->setActive(true);

    audioOverlay->setView(m_contactsListView);
    audioOverlay->setActive(true);

    videoOverlay->setView(m_contactsListView);
    videoOverlay->setActive(true);

    fileOverlay->setView(m_contactsListView);
    fileOverlay->setActive(true);

    connect(textOverlay, SIGNAL(overlayActivated(QModelIndex)),
            m_delegate, SLOT(hideStatusMessageSlot(QModelIndex)));

    connect(textOverlay, SIGNAL(overlayHidden()),
            m_delegate, SLOT(reshowStatusMessageSlot()));

    connect(textOverlay, SIGNAL(activated(ContactModelItem*)),
            this, SLOT(startTextChannel(ContactModelItem*)));

    connect(fileOverlay, SIGNAL(activated(ContactModelItem*)),
            this, SLOT(startFileTransferChannel(ContactModelItem*)));

    connect(audioOverlay, SIGNAL(activated(ContactModelItem*)),
            this, SLOT(startAudioChannel(ContactModelItem*)));

    connect(videoOverlay, SIGNAL(activated(ContactModelItem*)),
            this, SLOT(startVideoChannel(ContactModelItem*)));

    connect(this, SIGNAL(enableOverlays(bool)),
            textOverlay, SLOT(setActive(bool)));

    connect(this, SIGNAL(enableOverlays(bool)),
            audioOverlay, SLOT(setActive(bool)));

    connect(this, SIGNAL(enableOverlays(bool)),
            videoOverlay, SLOT(setActive(bool)));

    connect(this, SIGNAL(enableOverlays(bool)),
            fileOverlay, SLOT(setActive(bool)));

}

void MainWidget::toggleSearchWidget(bool show)
{
    if(show) {
        m_filterBar->show();
    }
    else {
        m_modelFilter->clearFilterString();
        m_filterBar->clear();
        m_filterBar->hide();
    }
}

void MainWidget::onAddContactRequest() {
    QWeakPointer<AddContactDialog> dialog = new AddContactDialog(m_model, this);
    if (dialog.data()->exec() == QDialog::Accepted) {
    Tp::AccountPtr account = dialog.data()->account();
        QStringList identifiers = QStringList() << dialog.data()->screenName();
        Tp::PendingContacts* pendingContacts = account->connection()->contactManager()->contactsForIdentifiers(identifiers);
        connect(pendingContacts, SIGNAL(finished(Tp::PendingOperation*)), SLOT(onAddContactRequestFoundContacts(Tp::PendingOperation*)));
    }
    delete dialog.data();
}

void MainWidget::onAddContactRequestFoundContacts(Tp::PendingOperation *operation) {
    Tp::PendingContacts *pendingContacts = qobject_cast<Tp::PendingContacts*>(operation);

    if (! pendingContacts->isError()) {
        //request subscription
        pendingContacts->manager()->requestPresenceSubscription(pendingContacts->contacts());
    }
    else {
        kDebug() << pendingContacts->errorName();
        kDebug() << pendingContacts->errorMessage();
    }
}

void MainWidget::onCustomContextMenuRequested(const QPoint &pos)
{
    QModelIndex index = m_contactsListView->indexAt(pos);

    if (!index.isValid()) {
        return;
    }

    Tp::ContactPtr contact;
    QVariant item = index.data(AccountsModel::ItemRole);

    KMenu *menu = 0;

    if (item.userType() == qMetaTypeId<ContactModelItem*>()) {
        menu = contactContextMenu(index);
    } else if (item.userType() == qMetaTypeId<GroupsModelItem*>()) {
        menu = groupContextMenu(index);
    }

    if (menu) {
        menu->exec(QCursor::pos());
        menu->deleteLater();
    }
}

KMenu* MainWidget::contactContextMenu(const QModelIndex &index)
{
    if (!index.isValid()) {
        return 0;
    }

    Tp::ContactPtr contact = index.data(AccountsModel::ItemRole).value<ContactModelItem*>()->contact();

    if (contact.isNull()) {
        kDebug() << "Contact is nulled";
        return 0;
    }

    Tp::AccountPtr account = m_model->accountForContactItem(index.data(AccountsModel::ItemRole).value<ContactModelItem*>());

    if (account.isNull()) {
        kDebug() << "Account is nulled";
        return 0;
    }

    KMenu *menu = new KMenu();
    menu->addTitle(contact->alias());

    QAction* action = menu->addAction(i18n("Start Chat..."));
    action->setIcon(KIcon("mail-message-new"));
    action->setDisabled(true);
    connect(action, SIGNAL(triggered(bool)),
            SLOT(slotStartTextChat()));

    if (index.data(AccountsModel::TextChatCapabilityRole).toBool()) {
        action->setEnabled(true);
    }

    Tp::ConnectionPtr accountConnection = account->connection();
    if (accountConnection.isNull()) {
        kDebug() << "Account connection is nulled.";
        return 0;
    }

    action = menu->addAction(i18n("Start Audio Call..."));
    action->setIcon(KIcon("voicecall"));
    action->setDisabled(true);
    connect(action, SIGNAL(triggered(bool)),
            SLOT(slotStartAudioChat()));

    if (index.data(AccountsModel::AudioCallCapabilityRole).toBool()) {
        action->setEnabled(true);
    }

    action = menu->addAction(i18n("Start Video Call..."));
    action->setIcon(KIcon("webcamsend"));
    action->setDisabled(true);
    connect(action, SIGNAL(triggered(bool)),
            SLOT(slotStartVideoChat()));

    if (index.data(AccountsModel::VideoCallCapabilityRole).toBool()) {
        action->setEnabled(true);
    }

    action = menu->addAction(i18n("Send File..."));
    action->setIcon(KIcon("mail-attachment"));
    action->setDisabled(true);
    connect(action, SIGNAL(triggered(bool)),
            SLOT(slotStartFileTransfer()));

    if (index.data(AccountsModel::FileTransferCapabilityRole).toBool()) {
        action->setEnabled(true);
    }

    menu->addSeparator();

    // remove contact action
    QAction *removeAction = menu->addAction(KIcon("list-remove-user"), i18n("Remove Contact"));
    connect(removeAction, SIGNAL(triggered(bool)), this, SLOT(slotDeleteContact()));

    if (accountConnection->actualFeatures().contains(Tp::Connection::FeatureRosterGroups)) {
        QMenu* groupAddMenu = menu->addMenu(i18n("Move to Group"));

        QStringList groupList;
        QList<Tp::AccountPtr> accounts = m_accountManager->allAccounts();
        foreach (const Tp::AccountPtr &account, accounts) {
            if (!account->connection().isNull()) {
                groupList.append(account->connection()->contactManager()->allKnownGroups());
            }
        }

        groupList.removeDuplicates();

        QStringList currentGroups = contact->groups();

        foreach (const QString &group, currentGroups) {
            groupList.removeAll(group);
        }

        connect(groupAddMenu->addAction(i18n("Create New Group...")), SIGNAL(triggered(bool)),
                this, SLOT(onCreateNewGroup()));

        groupAddMenu->addSeparator();

        foreach (const QString &group, groupList) {
            connect(groupAddMenu->addAction(group), SIGNAL(triggered(bool)),
                    SLOT(slotAddContactToGroupTriggered()));
        }
    } else {
        kDebug() << "Unable to support Groups";
    }

    //menu->addSeparator();

    // TODO: Remove when Telepathy actually supports blocking.
    /*if (contact->isBlocked()) {
     * action = menu->addAction(i18n("Unblock User"));
     * connect(action, SIGNAL(triggered(bool)),
     *         SLOT(slotUnblockContactTriggered()));
} else {
    action = menu->addAction(i18n("Blocked"));
    connect(action, SIGNAL(triggered(bool)),
    SLOT(slotBlockContactTriggered()));
}*/

    menu->addSeparator();

    action = menu->addAction(i18n("Show Info..."));
    action->setIcon(KIcon(""));
    connect(action, SIGNAL(triggered()), SLOT(slotShowInfo()));

    return menu;
}

KMenu* MainWidget::groupContextMenu(const QModelIndex &index)
{
    if (!index.isValid()) {
        return 0;
    }

    GroupsModelItem *groupItem = index.data(AccountsModel::ItemRole).value<GroupsModelItem*>();

    Q_ASSERT(groupItem);

    KMenu *menu = new KMenu();
    menu->addTitle(groupItem->groupName());

    QAction *action = menu->addAction(i18n("Rename Group..."));
    action->setIcon(KIcon("edit-rename"));

    connect(action, SIGNAL(triggered(bool)),
            this, SLOT(onRenameGroup()));

    action = menu->addAction(i18n("Delete Group"));
    action->setIcon(KIcon("edit-delete"));

    connect(action, SIGNAL(triggered(bool)),
            this, SLOT(onDeleteGroup()));

    return menu;
}

void MainWidget::slotAddContactToGroupTriggered()
{
    QModelIndex index = m_contactsListView->currentIndex();
    ContactModelItem* contactItem = index.data(AccountsModel::ItemRole).value<ContactModelItem*>();

    Q_ASSERT(contactItem);
    Tp::ContactPtr contact =  contactItem->contact();

    QAction *action = qobject_cast<QAction*>(sender());
    if (!action) {
        kDebug() << "Invalid action";
        return;
    }

    const QStringList currentGroups = contact->groups();

    Tp::PendingOperation* operation = contact->addToGroup(action->text().remove('&'));

    if (operation) {
        connect(operation, SIGNAL(finished(Tp::PendingOperation*)),
                SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));

        foreach (const QString &group, currentGroups) {
            Tp::PendingOperation* operation = contact->removeFromGroup(group);
            connect(operation, SIGNAL(finished(Tp::PendingOperation*)),
                    SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
        }
    }
}

void MainWidget::onCreateNewGroup()
{
    QString newGroupName = KInputDialog::getText(i18n("New Group Name"), i18n("Please enter the new group name"));

    QModelIndex index = m_contactsListView->currentIndex();
    ContactModelItem *contactItem = index.data(AccountsModel::ItemRole).value<ContactModelItem*>();

    Q_ASSERT(contactItem);
    Tp::ContactPtr contact =  contactItem->contact();
    Tp::PendingOperation *operation = contact->addToGroup(newGroupName);

    connect(operation, SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
}

void MainWidget::onRenameGroup()
{
    QModelIndex index = m_contactsListView->currentIndex();

    GroupsModelItem *groupItem = index.data(AccountsModel::ItemRole).value<GroupsModelItem*>();

    Q_ASSERT(groupItem);

    QString newGroupName = KInputDialog::getText(i18n("New Group Name"), i18n("Please enter the new group name"), groupItem->groupName());

    for(int i = 0; i < groupItem->size(); i++) {
        Tp::ContactPtr contact = qobject_cast<ProxyTreeNode*>(groupItem->childAt(i))
                                                             ->data(AccountsModel::ItemRole).value<ContactModelItem*>()->contact();
        Q_ASSERT(contact);

        Tp::PendingOperation *operation = contact->addToGroup(newGroupName);
        connect(operation, SIGNAL(finished(Tp::PendingOperation*)),
                SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));

        operation = contact->removeFromGroup(groupItem->groupName());
        connect(operation, SIGNAL(finished(Tp::PendingOperation*)),
                SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
    }
}

void MainWidget::onDeleteGroup()
{
    QModelIndex index = m_contactsListView->currentIndex();

    GroupsModelItem *groupItem = index.data(AccountsModel::ItemRole).value<GroupsModelItem*>();

    if (KMessageBox::warningContinueCancel(this,
                                           i18n("Do you really want to remove group %1?\n\n"
                                                "Note that all contacts will be moved to group 'Ungrouped'", groupItem->groupName()),
                                           i18n("Remove Group")) == KMessageBox::Continue) {

        for(int i = 0; i < groupItem->size(); i++) {
            Tp::ContactPtr contact = qobject_cast<ProxyTreeNode*>(groupItem->childAt(i))
                                                                 ->data(AccountsModel::ItemRole).value<ContactModelItem*>()->contact();
            Q_ASSERT(contact);

            Tp::PendingOperation *operation = contact->removeFromGroup(groupItem->groupName());
            connect(operation, SIGNAL(finished(Tp::PendingOperation*)),
                    SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
        }

        foreach (const Tp::AccountPtr &account, m_accountManager->allAccounts()) {
            if (account->connection()) {
                Tp::PendingOperation *operation = account->connection()->contactManager()->removeGroup(groupItem->groupName());
                connect(operation, SIGNAL(finished(Tp::PendingOperation*)),
                        SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
            }
        }
    }
}


void MainWidget::slotBlockContactTriggered()
{
    QModelIndex index = m_contactsListView->currentIndex();
    ContactModelItem* contactItem = index.data(AccountsModel::ItemRole).value<ContactModelItem*>();

    Q_ASSERT(contactItem);
    Tp::ContactPtr contact =  contactItem->contact();

    Tp::PendingOperation *operation = contact->block(true);
    connect(operation, SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
}

void MainWidget::slotDeleteContact()
{
    QModelIndex index = m_contactsListView->currentIndex();
    ContactModelItem* contactItem = index.data(AccountsModel::ItemRole).value<ContactModelItem*>();

    Q_ASSERT(contactItem);
    Tp::ContactPtr contact =  contactItem->contact();

    QList<Tp::ContactPtr>contactList;
    contactList.append(contact);

    // ask for confirmation
    QWeakPointer<RemoveContactDialog> removeDialog = new RemoveContactDialog(contact, this);

    if (removeDialog.data()->exec() == QDialog::Accepted) {
        // remove from contact list
        Tp::PendingOperation *deleteOp = contact->manager()->removeContacts(contactList);
        connect(deleteOp, SIGNAL(finished(Tp::PendingOperation*)), this, SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));

        if (removeDialog.data()->blockContact()) {
            // block contact
            Tp::PendingOperation *blockOp = contact->manager()->blockContacts(contactList);
            connect(blockOp, SIGNAL(finished(Tp::PendingOperation*)), this, SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
        }
    }

    delete removeDialog.data();
}

void MainWidget::slotGenericOperationFinished(Tp::PendingOperation* operation)
{
    if (operation->isError()) {
        QString errorMsg(operation->errorName() + ": " + operation->errorMessage());
        showMessageToUser(errorMsg, SystemMessageError);
    }
}

void MainWidget::slotShowInfo()
{
    QModelIndex index = m_contactsListView->currentIndex();
    if (!index.isValid()) {
        kDebug() << "Invalid index provided.";
        return;
    }

    ContactModelItem* item = index.data(AccountsModel::ItemRole).value<ContactModelItem*>();
    if (item) {
        showInfo(item);
    }
}

void MainWidget::showInfo(ContactModelItem *contactItem)
{
    ContactInfo contactInfoDialog(contactItem->contact(), this);
    contactInfoDialog.exec();
}

void MainWidget::slotStartTextChat()
{
    QModelIndex index = m_contactsListView->currentIndex();
    if (!index.isValid()) {
        kDebug() << "Invalid index provided.";
        return;
    }

    ContactModelItem* item = index.data(AccountsModel::ItemRole).value<ContactModelItem*>();
    if (item) {
        startTextChannel(item);
    }
}

void MainWidget::slotStartAudioChat()
{
    QModelIndex index = m_contactsListView->currentIndex();
    if (!index.isValid()) {
        kDebug() << "Invalid index provided.";
        return;
    }

    ContactModelItem* item = index.data(AccountsModel::ItemRole).value<ContactModelItem*>();
    if (item) {
        startAudioChannel(item);
    }
}

void MainWidget::slotStartVideoChat()
{
    QModelIndex index = m_contactsListView->currentIndex();
    if (!index.isValid()) {
        kDebug() << "Invalid index provided.";
        return;
    }
    ContactModelItem* item = index.data(AccountsModel::ItemRole).value<ContactModelItem*>();
    if (item) {
        startVideoChannel(item);
    }
}

void MainWidget::slotStartFileTransfer()
{
    QModelIndex index = m_contactsListView->currentIndex();
    if (!index.isValid()) {
        kDebug() << "Invalid index provided.";
        return;
    }

    ContactModelItem* item = index.data(AccountsModel::ItemRole).value<ContactModelItem*>();
    if (item) {
        startFileTransferChannel(item);
    }
}

void MainWidget::slotUnblockContactTriggered()
{
    QModelIndex index = m_contactsListView->currentIndex();
    ContactModelItem* item = index.data(AccountsModel::ItemRole).value<ContactModelItem*>();
    Q_ASSERT(item);

    Tp::ContactPtr contact = item->contact();

    Tp::PendingOperation *operation = contact->block(false);
    connect(operation, SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
}


void MainWidget::setCustomPresenceMessage(const QString& message)
{
    for (int i = 0; i < m_accountButtonsLayout->count() - 1; i++) {
        qobject_cast<AccountButton*>(m_accountButtonsLayout->itemAt(i)->widget())->setCustomPresenceMessage(message);

    }

    m_presenceMessageEdit->clearFocus();
}

void MainWidget::showSettingsKCM()
{
    KSettings::Dialog *dialog = new KSettings::Dialog(this);

    dialog->addModule("kcm_telepathy_accounts");

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->exec();
}

void MainWidget::loadAvatar(const Tp::AccountPtr &account)
{
    if (!account->avatar().avatarData.isEmpty()) {
        QIcon icon;
        Tp::Avatar avatar = account->avatar();
        icon.addPixmap(QPixmap::fromImage(QImage::fromData(avatar.avatarData)).scaled(48, 48));

        QToolButton *avatarButton = new QToolButton(this);
        avatarButton->setIcon(icon);
        avatarButton->setIconSize(QSize(48, 48));
        avatarButton->setText(i18nc("String in menu saying Use avatar from account X",
                                    "Use from %1", account->displayName()));
        avatarButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

        QWidgetAction *avatarAction = new QWidgetAction(m_avatarButtonMenu);
        avatarAction->setDefaultWidget(avatarButton);
        avatarAction->setData(account->uniqueIdentifier());

        connect(avatarButton, SIGNAL(clicked(bool)),
                avatarAction, SIGNAL(triggered(bool)));

        connect(avatarAction, SIGNAL(triggered(bool)),
                this, SLOT(selectAvatarFromAccount()));


        m_avatarButtonMenu->addAction(avatarAction);
    }
}

void MainWidget::selectAvatarFromAccount()
{
    selectAvatarFromAccount(qobject_cast<QWidgetAction*>(sender())->data().toString());
}

void MainWidget::selectAvatarFromAccount(const QString &accountUID)
{
    if (accountUID.isEmpty()) {
        kDebug() << "Supplied accountUID is empty, aborting...";
        return;
    }

    if (m_model->accountItemForId(accountUID) == 0) {
        kDebug() << "Chosen account ID does not exist, aborting..";

        //no point of keeping the config if the previously set account ID does not exist
        KSharedConfigPtr config = KGlobal::config();
        KConfigGroup avatarGroup(config, "Avatar");
        avatarGroup.deleteGroup();
        avatarGroup.config()->sync();

        return;
    }

    Tp::Avatar avatar = qobject_cast<AccountsModelItem*>(m_model->accountItemForId(accountUID))->data(AccountsModel::AvatarRole).value<Tp::Avatar>();

    foreach (const Tp::AccountPtr &account, m_accountManager->allAccounts()) {
        //don't set the avatar for the account from where it was taken
        if (account->uniqueIdentifier() == accountUID) {
            continue;
        }

        account->setAvatar(avatar);
    }

    //add the selected avatar as the icon of avatar button
    QIcon icon;
    icon.addPixmap(QPixmap::fromImage(QImage::fromData(avatar.avatarData)).scaled(48, 48));
    m_userAccountIconButton->setIcon(icon);

    m_avatarButtonMenu->close();

    //save the selected account into config
    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup avatarGroup(config, "Avatar");
    avatarGroup.writeEntry("method", "account");
    avatarGroup.writeEntry("source", accountUID);
    avatarGroup.config()->sync();

}

void MainWidget::loadAvatarFromFile()
{
    if (m_accountManager->allAccounts().isEmpty()) {
        int returnCode = KMessageBox::warningYesNo(this,
                                           i18nc("Dialog text", "You have no accounts set. Would you like to set one now?"),
                                           i18nc("Dialog caption", "No accounts set"));

        if (returnCode == KMessageBox::Yes) {
            showSettingsKCM();
            loadAvatarFromFile();
        } else {
            return;
        }
    } else {
        KUrl fileUrl = KFileDialog::getImageOpenUrl(KUrl(), this,
                                     i18n("Please choose your avatar"));

        if (!fileUrl.isEmpty()) {
            FetchAvatarJob *job = new FetchAvatarJob(fileUrl, this);

            connect(job, SIGNAL(result(KJob*)),
                    this, SLOT(onAvatarFetched(KJob*)));

            job->start();
        } else {
            return;
        }
    }
}

void MainWidget::onAvatarFetched(KJob *job)
{
    if (job->error()) {
        KMessageBox::error(this, job->errorString());
        return;
    }

    //this should never be true, but better one "if" than a crash
    if (m_accountManager->allAccounts().isEmpty()) {
        int returnCode = KMessageBox::warningYesNo(this,
                                                   i18nc("Dialog text", "You have no accounts set. Would you like to set one now?"),
                                                   i18nc("Dialog caption", "No accounts set"));

        if (returnCode == KMessageBox::Yes) {
            showSettingsKCM();
        } else {
            return;
        }
    } else {

        FetchAvatarJob *fetchJob = qobject_cast< FetchAvatarJob* >(job);

        Q_ASSERT(fetchJob);

        foreach (const Tp::AccountPtr &account, m_accountManager->allAccounts()) {
            Tp::PendingOperation *op = account->setAvatar(fetchJob->avatar());

            //connect for eventual error displaying
            connect(op, SIGNAL(finished(Tp::PendingOperation*)),
                    this, SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
        }

        //add the selected avatar to the avatar button
        QIcon icon;
        icon.addPixmap(QPixmap::fromImage(QImage::fromData(fetchJob->avatar().avatarData)).scaled(48, 48));
        m_userAccountIconButton->setIcon(icon);

        //since all the accounts will have the same avatar,
        //we take simply the first in AM and use this in config
        KSharedConfigPtr config = KGlobal::config();
        KConfigGroup avatarGroup(config, "Avatar");
        avatarGroup.writeEntry("method", "account");
        avatarGroup.writeEntry("source", m_accountManager->allAccounts().first()->uniqueIdentifier());
        avatarGroup.config()->sync();
    }
}

void MainWidget::onAccountsPresenceStatusFiltered()
{
    kDebug() << "Watcher is here";
    QFutureWatcher< Tp::ContactPtr > *watcher = dynamic_cast< QFutureWatcher< Tp::ContactPtr > * >(sender());
    kDebug() << "Watcher is casted";
    Tp::Contacts contacts = watcher->future().results().toSet();
    kDebug() << "Watcher is used";
    if (!contacts.isEmpty()) {
        onPresencePublicationRequested(contacts);
    }
    watcher->deleteLater();
}

void MainWidget::onPresencePublicationRequested(const Tp::Contacts& contacts)
{
    foreach (const Tp::ContactPtr &contact, contacts) {
        Tp::ContactManagerPtr manager = contact->manager();
        Tp::PendingOperation *op = 0;

        if (contact->subscriptionState() == Tp::Contact::PresenceStateYes) {
            op = manager->authorizePresencePublication(QList< Tp::ContactPtr >() << contact);
        } else if (KMessageBox::questionYesNo(this, i18n("The contact %1 added you to their contact list. "
                                                  "Do you want to allow this person to see your presence "
                                                  "and add them to your contact list?", contact->id()),
                                       i18n("Subscription request")) == KMessageBox::Yes) {

            op = manager->authorizePresencePublication(QList< Tp::ContactPtr >() << contact);

            if (manager->canRequestPresenceSubscription() && contact->subscriptionState() == Tp::Contact::PresenceStateNo) {
                manager->requestPresenceSubscription(QList< Tp::ContactPtr >() << contact);
            }
        }

        if (op) {
            connect(op, SIGNAL(finished(Tp::PendingOperation*)),
                    SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
        }
    }
}

void MainWidget::handleConnectionError(const Tp::AccountPtr& account)
{
    QString connectionError = account->connectionError();

    // ignore user disconnect
    if (connectionError == "org.freedesktop.Telepathy.Error.Cancelled") {
        return;
    }

    Tp::ConnectionStatusReason reason = account->connectionStatusReason();

    if (reason == Tp::ConnectionStatusReasonAuthenticationFailed) {
        showMessageToUser(i18n("Could not connect %1. Authentication failed (is your password correct?)", account->displayName()), MainWidget::SystemMessageError);
    } else if (reason == Tp::ConnectionStatusReasonNetworkError) {
        showMessageToUser(i18n("Could not connect %1. There was a network error, check your connection", account->displayName()), MainWidget::SystemMessageError);
    } else {
        // other errors
        showMessageToUser(i18n("An unexpected error has occurred with %1: '%2'", account->displayName(), account->connectionError()), MainWidget::SystemMessageError);
    }
}

void MainWidget::onGroupContacts(bool enabled)
{
    if (enabled) {
        m_modelFilter->setSourceModel(m_groupsModel);
    } else {
        m_modelFilter->setSourceModel(m_model);
    }
}

void MainWidget::onJoinChatRoomRequested()
{
    QWeakPointer<JoinChatRoomDialog> dialog = new JoinChatRoomDialog(m_accountManager);

    if (dialog.data()->exec() == QDialog::Accepted) {
        Tp::AccountPtr account = dialog.data()->selectedAccount();

        // check account validity. Should NEVER be invalid
        if (!account.isNull()) {
            // ensure chat room
            Tp::PendingChannelRequest *channelRequest = account->ensureTextChatroom(dialog.data()->selectedChatRoom(),
                                                                                    QDateTime::currentDateTime(),
                                                                                    PREFERRED_TEXTCHAT_HANDLER);
            connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)), SLOT(slotGenericOperationFinished(Tp::PendingOperation*)));
        }
    }

    delete dialog.data();
}

void MainWidget::onSwitchToFullView()
{
    m_contactsListView->setItemDelegate(m_delegate);
    m_contactsListView->doItemsLayout();

    emit enableOverlays(true);

    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup guiConfigGroup(config, "GUI");
    guiConfigGroup.writeEntry("selected_delegate", "full");
    guiConfigGroup.config()->sync();
}

void MainWidget::onSwitchToCompactView()
{
    m_contactsListView->setItemDelegate(m_compactDelegate);
    m_contactsListView->doItemsLayout();

    emit enableOverlays(false);

    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup guiConfigGroup(config, "GUI");
    guiConfigGroup.writeEntry("selected_delegate", "compact");
    guiConfigGroup.config()->sync();
}

void MainWidget::closeEvent(QCloseEvent* e)
{
    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup generalConfigGroup(config, "General");
    KConfigGroup notifyConigGroup(config, "Notification Messages");

    ContactListApplication *app = qobject_cast<ContactListApplication*>(kapp);
    if (!app->isShuttingDown()) {    
        //the standard KMessageBox control saves "true" if you select the checkbox, therefore the reversed var name
        bool dontCheckForPlasmoid = notifyConigGroup.readEntry("dont_check_for_plasmoid", false);

        if (isAnyAccountOnline() && !dontCheckForPlasmoid) {
            if (!isPresencePlasmoidPresent()) {
                switch (KMessageBox::warningYesNoCancel(this,
                        i18n("You do not have any other presence controls active (a Presence widget for example).\n"
                            "Do you want to stay online or would you rather go offline?"),
                        i18n("No Other Presence Controls Found"),
                        KGuiItem(i18n("Stay Online"), KIcon("user-online")),
                        KGuiItem(i18n("Go Offline"), KIcon("user-offline")),
                        KStandardGuiItem::cancel(),
                        QString("dont_check_for_plasmoid"))) {

                    case KMessageBox::No:
                        generalConfigGroup.writeEntry("go_offline_when_closing", true);
                        goOffline();
                        break;
                    case KMessageBox::Cancel:
                        e->ignore();
                        return;
                }
            }
        } else if (isAnyAccountOnline() && dontCheckForPlasmoid) {
            bool shouldGoOffline = generalConfigGroup.readEntry("go_offline_when_closing", false);
            if (shouldGoOffline) {
                goOffline();
            }
        }
        
        generalConfigGroup.config()->sync();
    }
    
    KMainWindow::closeEvent(e);
}

bool MainWidget::isPresencePlasmoidPresent() const
{
    QDBusInterface plasmoidOnDbus("org.kde.Telepathy.PresenceEngineActive", "/PresenceEngineActive");

    if (plasmoidOnDbus.isValid()) {
        return true;
    } else {
        return false;
    }
}

void MainWidget::goOffline()
{
    kDebug() << "Setting all accounts offline...";
    foreach (const Tp::AccountPtr &account, m_accountManager->allAccounts()) {
        if (account->isEnabled() && account->isValid()) {
            account->setRequestedPresence(Tp::Presence::offline());
        }
    }
}

bool MainWidget::isAnyAccountOnline() const
{
    foreach (const Tp::AccountPtr &account, m_accountManager->allAccounts()) {
        if (account->isEnabled() && account->isValid() && account->isOnline()) {
            return true;
        }
    }

    return false;
}

#include "main-widget.moc"
