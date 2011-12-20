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

#include <TelepathyQt/PendingReady>
#include <TelepathyQt/PendingChannelRequest>
#include <TelepathyQt/PendingContacts>
#include <TelepathyQt/ClientRegistrar>
#include <TelepathyQt/Constants>
#include <TelepathyQt/ContactManager>

#include <KDebug>
#include <KDialog>
#include <KIO/Job>
#include <KUser>
#include <KMenu>
#include <KMessageBox>
#include <KProtocolInfo>
#include <KSettings/Dialog>
#include <KSharedConfig>
#include <KFileDialog>
#include <KInputDialog>
#include <KStandardShortcut>
#include <KNotification>

#include "ui_main-widget.h"
#include "account-buttons-panel.h"
#include "contact-overlays.h"
#include "contact-delegate.h"
#include "contact-delegate-compact.h"
#include "fetch-avatar-job.h"
#include "contact-list-application.h"

#include "dialogs/add-contact-dialog.h"
#include "dialogs/join-chat-room-dialog.h"

#include <KTelepathy/Models/groups-model.h>
#include <KTelepathy/Models/contact-model-item.h>
#include <KTelepathy/Models/groups-model-item.h>
#include <KTelepathy/Models/accounts-model.h>
#include <KTelepathy/Models/accounts-model-item.h>
#include <KTelepathy/Models/accounts-filter-model.h>
#include <KTelepathy/Models/proxy-tree-node.h>
#include <KTelepathy/text-parser.h>

#include "tooltips/tooltipmanager.h"
#include "context-menu.h"

#define PREFERRED_TEXTCHAT_HANDLER "org.freedesktop.Telepathy.Client.KDE.TextUi"
#define PREFERRED_FILETRANSFER_HANDLER "org.freedesktop.Telepathy.Client.KDE.FileTransfer"
#define PREFERRED_AUDIO_VIDEO_HANDLER "org.freedesktop.Telepathy.Client.KDE.CallUi"
#define PREFERRED_RFB_HANDLER "org.freedesktop.Telepathy.Client.krfb_rfb_handler"

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
    setAutoSaveSettings();

    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup guiConfigGroup(config, "GUI");

    m_userAccountNameLabel->setText(user.property(KUser::FullName).isNull() ?
        user.loginName() : user.property(KUser::FullName).toString()
    );

    m_avatarButton->setPopupMode(QToolButton::InstantPopup);

    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    m_addContactAction = new KAction(KIcon("list-add-user"), i18n("Add new contacts..."), this);

    m_toolBar->addAction(m_addContactAction);

    m_groupContactsAction = new KAction(KIcon("user-group-properties"), i18n("Show/Hide groups"), this);
    m_groupContactsAction->setCheckable(true);
    m_groupContactsAction->setChecked(true);
    //TODO: Toggle the tooltip with the button? eg. once its Show, after click its Hide .. ?

    m_toolBar->addAction(m_groupContactsAction);

    m_showOfflineAction = new KAction(KIcon("meeting-attending-tentative"), i18n("Hide/Show offline users"), this);
    m_showOfflineAction->setCheckable(true);
    m_showOfflineAction->setChecked(false);

    m_toolBar->addAction(m_showOfflineAction);

    m_sortByPresenceAction = new KDualAction(i18n("Sort by presence"), i18n("Sort by name"), this);
    m_sortByPresenceAction->setActiveIcon(KIcon("user-online"));
    m_sortByPresenceAction->setInactiveIcon(KIcon("view-sort-ascending"));

    m_toolBar->addAction(m_sortByPresenceAction);

    m_searchContactAction = new KAction(KIcon("edit-find-user"), i18n("Find contact"), this );
    m_searchContactAction->setShortcut(KStandardShortcut::find());
    m_searchContactAction->setCheckable(true);
    m_searchContactAction->setChecked(false);

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

//     KMenu *presenceChooser = new KMenu(settingsButton);
//     presenceChooser->setTitle(i18n("Control accounts presence"));
//
//     QActionGroup *presenceChooserGroup = new QActionGroup(this);
//     presenceChooserGroup->setExclusive(true);
//     presenceChooserGroup->addAction(presenceChooser->addAction(i18n("All at once"),
//                                                                this, SLOT(onUseGlobalPresenceTriggered())));
//
//     presenceChooserGroup->actions().last()->setCheckable(true);
//
    if (guiConfigGroup.readEntry("selected_presence_chooser", "global") == QLatin1String("global")) {
//         presenceChooserGroup->actions().last()->setChecked(true);
//         //hide account buttons and show global presence
         onUseGlobalPresenceTriggered();
    }
//
//     presenceChooserGroup->addAction(presenceChooser->addAction(i18n("Separately"),
//                                                                     this, SLOT(onUsePerAccountPresenceTriggered())));
//
//     presenceChooserGroup->actions().last()->setCheckable(true);
//
//     if (guiConfigGroup.readEntry("selected_presence_chooser", "global") == QLatin1String("per-account")) {
//         presenceChooserGroup->actions().last()->setChecked(true);
//         //hide global presence and use account buttons
//         onUsePerAccountPresenceTriggered();
//     }
//
//     settingsButtonMenu->addMenu(presenceChooser);

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
    m_contactsListView->setDragEnabled(true);
    m_contactsListView->viewport()->setAcceptDrops(true);
    m_contactsListView->setDropIndicatorShown(true);

    m_contextMenu = new ContextMenu(this);

    addOverlayButtons();

    emit enableOverlays(guiConfigGroup.readEntry("selected_delegate", "full") == QLatin1String("full"));

    new ToolTipManager(m_contactsListView);

    connect(m_contactsListView, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(onCustomContextMenuRequested(QPoint)));

    connect(m_contactsListView, SIGNAL(clicked(QModelIndex)),
            this, SLOT(onContactListClicked(QModelIndex)));

    connect(m_contactsListView, SIGNAL(doubleClicked(QModelIndex)),
            this, SLOT(onContactListDoubleClicked(QModelIndex)));

    connect(m_delegate, SIGNAL(repaintItem(QModelIndex)),
            m_contactsListView->viewport(), SLOT(repaint())); //update(QModelIndex)

    connect(m_addContactAction, SIGNAL(triggered(bool)),
            this, SLOT(onAddContactRequest()));

    connect(m_groupContactsAction, SIGNAL(triggered(bool)),
            this, SLOT(groupContacts(bool)));

    connect(m_searchContactAction, SIGNAL(triggered(bool)),
            this, SLOT(toggleSearchWidget(bool)));

    connect(m_avatarButton, SIGNAL(operationFinished(Tp::PendingOperation*)),
            this, SLOT(onGenericOperationFinished(Tp::PendingOperation*)));

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
    configGroup.writeEntry("sort_by_presence", m_sortByPresenceAction->isActive());
    configGroup.config()->sync();
}

void MainWidget::onAccountManagerReady(Tp::PendingOperation* op)
{
    if (op->isError()) {
        kDebug() << op->errorName();
        kDebug() << op->errorMessage();

        KMessageBox::error(this,
                           i18n("Something unexpected happened to the core part of your Instant Messaging system "
                                "and it couldn't be initialized. Try restarting the Contact List."),
                           i18n("IM system failed to initialize"));

        return;
    }

    m_model = new AccountsModel(m_accountManager, this);
    m_groupsModel = new GroupsModel(m_model, this);
    m_modelFilter = new AccountsFilterModel(this);
    m_modelFilter->setDynamicSortFilter(true);
    m_modelFilter->setShowOfflineUsers(m_showOfflineAction->isChecked());
    m_modelFilter->clearFilterString();
    m_modelFilter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_modelFilter->setSortRole(Qt::DisplayRole);
    m_modelFilter->setSortByPresence(m_sortByPresenceAction->isActive());
    if (m_groupContactsAction->isChecked()) {
        m_modelFilter->setSourceModel(m_groupsModel);
    } else {
        m_modelFilter->setSourceModel(m_model);
    }
    m_contactsListView->setModel(m_modelFilter);
    m_contactsListView->setSortingEnabled(true);
    m_contactsListView->sortByColumn(0, Qt::AscendingOrder);

    connect(m_modelFilter, SIGNAL(rowsInserted(QModelIndex,int,int)),
            this, SLOT(onNewGroupModelItemsInserted(QModelIndex,int,int)));

    connect(m_showOfflineAction, SIGNAL(toggled(bool)),
            m_modelFilter, SLOT(setShowOfflineUsers(bool)));

    connect(m_filterBar, SIGNAL(filterChanged(QString)),
            this, SLOT(onFilterStringChanged(QString)));

    connect(m_filterBar, SIGNAL(closeRequest()),
            m_filterBar, SLOT(hide()));

    connect(m_filterBar, SIGNAL(closeRequest()),
            m_searchContactAction, SLOT(trigger()));

    connect(m_sortByPresenceAction, SIGNAL(activeChanged(bool)),
            m_modelFilter, SLOT(setSortByPresence(bool)));

    connect(m_groupsModel, SIGNAL(operationFinished(Tp::PendingOperation*)),
            this, SLOT(onGenericOperationFinished(Tp::PendingOperation*)));

    m_avatarButton->initialize(m_model, m_accountManager);
    m_accountButtons->setAccountManager(m_accountManager);
    m_presenceChooser->setAccountManager(m_accountManager);

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
    groupContacts(useGroups);
    m_groupContactsAction->setChecked(useGroups);

    bool showOffline = guiConfigGroup.readEntry("show_offline", false);
    m_modelFilter->setShowOfflineUsers(showOffline);
    m_showOfflineAction->setChecked(showOffline);

    bool sortByPresence = guiConfigGroup.readEntry("sort_by_presence", true);
    m_modelFilter->setSortByPresence(sortByPresence);
    m_sortByPresenceAction->setActive(sortByPresence);
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

    connect(account.data(),
            SIGNAL(connectionStatusChanged(Tp::ConnectionStatus)),
            this, SLOT(onAccountConnectionStatusChanged(Tp::ConnectionStatus)));

    m_avatarButton->loadAvatar(account);
    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup avatarGroup(config, "Avatar");
    if (avatarGroup.readEntry("method", QString()) == QLatin1String("account")) {
        //this also updates the avatar if it was changed somewhere else
        m_avatarButton->selectAvatarFromAccount(avatarGroup.readEntry("source", QString()));
    }
}

void MainWidget::onContactListClicked(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }

    if (index.data(AccountsModel::ItemRole).userType() == qMetaTypeId<AccountsModelItem*>()
        || index.data(AccountsModel::ItemRole).userType() == qMetaTypeId<GroupsModelItem*>()) {

        KSharedConfigPtr config = KSharedConfig::openConfig(QLatin1String("ktelepathyrc"));
        KConfigGroup groupsConfig = config->group("GroupsState");

        if (m_contactsListView->isExpanded(index)) {
            m_contactsListView->collapse(index);
            groupsConfig.writeEntry(index.data(AccountsModel::IdRole).toString(), false);
        } else {
            m_contactsListView->expand(index);
            groupsConfig.writeEntry(index.data(AccountsModel::IdRole).toString(), true);
        }

        groupsConfig.config()->sync();
    }
}

void MainWidget::onContactListDoubleClicked(const QModelIndex& index)
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

    Tp::ChannelRequestHints hints;
    hints.setHint("org.kde.telepathy","forceRaiseWindow", QVariant(true));

    Tp::PendingChannelRequest* channelRequest = account->ensureTextChat(contact,
                                                                        QDateTime::currentDateTime(),
                                                                        PREFERRED_TEXTCHAT_HANDLER,
                                                                        hints);
    connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)),
            this, SLOT(onGenericOperationFinished(Tp::PendingOperation*)));
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
            this, SLOT(onGenericOperationFinished(Tp::PendingOperation*)));
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
            this, SLOT(onGenericOperationFinished(Tp::PendingOperation*)));
}

void MainWidget::startDesktopSharing(ContactModelItem* contactItem)
{
    Q_ASSERT(contactItem);
    Tp::ContactPtr contact = contactItem->contact();

    kDebug() << "Requesting desktop sharing for contact" << contact->alias();

    Tp::AccountPtr account = m_model->accountForContactItem(contactItem);

    Tp::PendingChannelRequest* channelRequest = account->createStreamTube(contact,
            QLatin1String("rfb"), QDateTime::currentDateTime(), PREFERRED_RFB_HANDLER);

    connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)),
            this, SLOT(onGenericOperationFinished(Tp::PendingOperation*)));
}

void MainWidget::startFileTransferChannel(ContactModelItem *contactItem)
{
    Q_ASSERT(contactItem);
    Tp::ContactPtr contact = contactItem->contact();

    kDebug() << "Requesting file transfer for contact" << contact->alias();

    Tp::AccountPtr account = m_model->accountForContactItem(contactItem);

    QStringList filenames = KFileDialog::getOpenFileNames(KUrl("kfiledialog:///FileTransferLastDirectory"),
                                                    QString(),
                                                    this,
                                                    i18n("Choose files to send to %1", contact->alias()));

    if (filenames.isEmpty()) { // User hit cancel button
        return;
    }

    QDateTime now = QDateTime::currentDateTime();
    Q_FOREACH(QString filename, filenames) {
        QFileInfo fileinfo(filename);

        kDebug() << "Filename:" << filename;
        kDebug() << "Content type:" << KMimeType::findByFileContent(filename)->name();

        Tp::FileTransferChannelCreationProperties fileTransferProperties(filename,
                                                                         KMimeType::findByFileContent(filename)->name());

        Tp::PendingChannelRequest* channelRequest = account->createFileTransfer(contact,
                                                                                fileTransferProperties,
                                                                                now,
                                                                                PREFERRED_FILETRANSFER_HANDLER);
        connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)), SLOT(onGenericOperationFinished(Tp::PendingOperation*)));
    }
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
    DesktopSharingContactOverlay *desktopOverlay = new DesktopSharingContactOverlay(this);

    m_delegate->installOverlay(textOverlay);
    m_delegate->installOverlay(audioOverlay);
    m_delegate->installOverlay(videoOverlay);
    m_delegate->installOverlay(fileOverlay);
    m_delegate->installOverlay(desktopOverlay);

    textOverlay->setView(m_contactsListView);
    textOverlay->setActive(true);

    audioOverlay->setView(m_contactsListView);
    audioOverlay->setActive(true);

    videoOverlay->setView(m_contactsListView);
    videoOverlay->setActive(true);

    fileOverlay->setView(m_contactsListView);
    fileOverlay->setActive(true);

    desktopOverlay->setView(m_contactsListView);
    desktopOverlay->setActive(true);

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

    connect(desktopOverlay, SIGNAL(activated(ContactModelItem*)),
            this, SLOT(startDesktopSharing(ContactModelItem*)));


    connect(this, SIGNAL(enableOverlays(bool)),
            textOverlay, SLOT(setActive(bool)));

    connect(this, SIGNAL(enableOverlays(bool)),
            audioOverlay, SLOT(setActive(bool)));

    connect(this, SIGNAL(enableOverlays(bool)),
            videoOverlay, SLOT(setActive(bool)));

    connect(this, SIGNAL(enableOverlays(bool)),
            fileOverlay, SLOT(setActive(bool)));

    connect(this, SIGNAL(enableOverlays(bool)),
            desktopOverlay, SLOT(setActive(bool)));
}

void MainWidget::toggleSearchWidget(bool show)
{
    if(show) {
        m_filterBar->show();
    }
    else {
        m_modelFilter->clearFilterStringAndHideOfflineUsers(m_showOfflineAction->isChecked());
        m_filterBar->clear();
        m_filterBar->hide();
    }
}

void MainWidget::onAddContactRequest() {
    QWeakPointer<AddContactDialog> dialog = new AddContactDialog(m_model, this);
    if (dialog.data()->exec() == QDialog::Accepted) {
        Tp::AccountPtr account = dialog.data()->account();
        if (account.isNull()) {
            KMessageBox::error(this,
                               i18n("Seems like you forgot to select an account. Also do not forget to connect it first."),
                               i18n("No Account Selected"));
        }
        else if (account->connection().isNull()) {
            KMessageBox::error(this,
                               i18n("An error we did not anticipate just happened and so the contact could not be added. Sorry."),
                               i18n("Account Error"));
        } else {
            QStringList identifiers = QStringList() << dialog.data()->screenName();
            Tp::PendingContacts* pendingContacts = account->connection()->contactManager()->contactsForIdentifiers(identifiers);
            connect(pendingContacts, SIGNAL(finished(Tp::PendingOperation*)), SLOT(onAddContactRequestFoundContacts(Tp::PendingOperation*)));
        }
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
        menu = m_contextMenu->contactContextMenu(index);
    } else if (item.userType() == qMetaTypeId<GroupsModelItem*>()) {
        menu = m_contextMenu->groupContextMenu(index);
    }

    if (menu) {
        menu->exec(QCursor::pos());
        menu->deleteLater();
    }
}



void MainWidget::onFilterStringChanged(const QString &str)
{
    m_modelFilter->setShowOfflineUsers(!str.isEmpty());
    m_modelFilter->setFilterString(str);
}

void MainWidget::onGenericOperationFinished(Tp::PendingOperation* operation)
{
    if (operation->isError()) {
        QString errorMsg(operation->errorName() + ": " + operation->errorMessage());
        showMessageToUser(errorMsg, SystemMessageError);
    }
}

void MainWidget::showSettingsKCM()
{
    KSettings::Dialog *dialog = new KSettings::Dialog(this);

    KService::Ptr tpAccKcm = KService::serviceByDesktopName("kcm_telepathy_accounts");

    if (!tpAccKcm) {
        KMessageBox::error(this,
                           i18n("It appears you do not have the IM Accounts control module installed. Please install telepathy-accounts-kcm package."),
                           i18n("IM Accounts KCM Plugin Is Not Installed"));
    }

    dialog->addModule("kcm_telepathy_accounts");

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->exec();
}

///Was moved to telepathy-kded-module
// void MainWidget::handleConnectionError(const Tp::AccountPtr& account)
// {
// }

void MainWidget::groupContacts(bool enabled)
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
            Tp::ChannelRequestHints hints;
            hints.setHint("org.kde.telepathy","forceRaiseWindow", QVariant(true));

            Tp::PendingChannelRequest *channelRequest = account->ensureTextChatroom(dialog.data()->selectedChatRoom(),
                                                                                    QDateTime::currentDateTime(),
                                                                                    PREFERRED_TEXTCHAT_HANDLER,
                                                                                    hints);

            connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)), SLOT(onGenericOperationFinished(Tp::PendingOperation*)));
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
    QDBusInterface plasmoidOnDbus("org.kde.Telepathy.PresenceAppletActive", "/PresenceAppletActive");

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

void MainWidget::onNewGroupModelItemsInserted(const QModelIndex& index, int start, int end)
{
    Q_UNUSED(start);
    Q_UNUSED(end);
    if (!index.isValid()) {
        return;
    }

    //if there is no parent, we deal with top-level item that we want to expand/collapse, ie. group or account
    if (!index.parent().isValid()) {
        KSharedConfigPtr config = KSharedConfig::openConfig(QLatin1String("ktelepathyrc"));
        KConfigGroup groupsConfig = config->group("GroupsState");

        //we're probably dealing with group item, so let's check if it is expanded first
        if (!m_contactsListView->isExpanded(index)) {
            //if it's not expanded, check the config if we should expand it or not
            if (groupsConfig.readEntry(index.data(AccountsModel::IdRole).toString(), false)) {
                m_contactsListView->expand(index);
            }
        }
    }
}

void MainWidget::onUseGlobalPresenceTriggered()
{
    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup configGroup(config, "GUI");

    m_presenceChooser->show();
    m_accountButtons->hide();

    configGroup.writeEntry("selected_presence_chooser", "global");

    configGroup.config()->sync();
}

void MainWidget::onUsePerAccountPresenceTriggered()
{
    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup configGroup(config, "GUI");

    m_presenceChooser->hide();
    m_accountButtons->show();

    configGroup.writeEntry("selected_presence_chooser", "per-account");

    configGroup.config()->sync();
}

#include "main-widget.moc"
