/*
    Tool button which controls account's presence
    Copyright (C) 2011 Martin Klapetek <martin.klapetek@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <QAction>
#include <QPainter>
#include <QPixmap>

#include <KIcon>
#include <KLocale>

#include "accountbutton.h"

static Tp::ConnectionPresenceType accountPresenceTypes[] = { Tp::ConnectionPresenceTypeAvailable, Tp::ConnectionPresenceTypeAway,
Tp::ConnectionPresenceTypeAway, Tp::ConnectionPresenceTypeBusy,
Tp::ConnectionPresenceTypeBusy, Tp::ConnectionPresenceTypeExtendedAway,
Tp::ConnectionPresenceTypeHidden, Tp::ConnectionPresenceTypeOffline };

static const char *accountPresenceStatuses[] = { "available", "away", "brb", "busy",
"dnd", "xa", "hidden", "offline" };

AccountButton::AccountButton(const Tp::AccountPtr &account, QWidget* parent): QToolButton(parent)
{
    m_account = account;
    
    QString iconPath = account->iconName();
    
    //if the icon has not been set, we use the protocol icon    
    if(iconPath.isEmpty()) {
        iconPath = QString("im-%1").arg(account->protocolName());
    }
    
    setIcon(KIcon(iconPath));
    
    if(!account->isValid()) {
        //we paint a warning symbol in the right-bottom corner
        QPixmap pixmap = icon().pixmap(32, 32);
        QPainter painter(&pixmap);
        KIcon("dialog-error").paint(&painter, 15, 15, 16, 16);
        
        setIcon(KIcon(pixmap));
    }
    
    setToolTip(QString(account->displayName()));
    setMaximumWidth(24);
    
    setAutoRaise(true);
    setPopupMode(QToolButton::InstantPopup);
    setArrowType(Qt::NoArrow);
    
    QActionGroup *presenceActions = new QActionGroup(this);
    presenceActions->setExclusive(true);
    
    QAction *onlineAction =     new QAction(KIcon("user-online"), i18nc("@action:inmenu", "Available"), this);
    QAction *awayAction =       new QAction(KIcon("user-away"), i18nc("@action:inmenu", "Away"), this);
    QAction *brbAction =        new QAction(KIcon("user-busy"), i18nc("@action:inmenu", "Be right back"), this);
    QAction *busyAction =       new QAction(KIcon("user-busy"), i18nc("@action:inmenu", "Busy"), this);
    QAction *dndAction =        new QAction(KIcon("user-busy"), i18nc("@action:inmenu", "Do not disturb"), this);
    QAction *xaAction =         new QAction(KIcon("user-away-extended"), i18nc("@action:inmenu", "Extended Away"), this);
    QAction *invisibleAction =  new QAction(KIcon("user-invisible"), i18nc("@action:inmenu", "Invisible"), this);
    QAction *offlineAction =    new QAction(KIcon("user-offline"), i18nc("@action:inmenu", "Offline"), this);
    
    //let's set the indexes as data(), so we don't have to rely on putting the actions into indexed list/menu/etc
    onlineAction->setData(0);
    awayAction->setData(1);
    brbAction->setData(2);
    busyAction->setData(3);
    dndAction->setData(4);
    xaAction->setData(5);
    invisibleAction->setData(6);
    offlineAction->setData(7);
    
    presenceActions->addAction(onlineAction);
    presenceActions->addAction(awayAction);
    presenceActions->addAction(brbAction);
    presenceActions->addAction(busyAction);
    presenceActions->addAction(dndAction);
    presenceActions->addAction(xaAction);
    presenceActions->addAction(invisibleAction);
    presenceActions->addAction(offlineAction);
    
    addActions(presenceActions->actions());
    
    //make all the actions checkable
    foreach(QAction *a, actions())
    {
        a->setCheckable(true);
    }
    
    connect(this, SIGNAL(triggered(QAction*)),
            this, SLOT(setAccountStatus(QAction*)));
}

void AccountButton::setAccountStatus(QAction *action)
{
    int statusIndex = action->data().toInt();
    Q_ASSERT(statusIndex >= 0 && statusIndex <= 7);
    
    Tp::SimplePresence presence;
    presence.type = accountPresenceTypes[statusIndex];
    presence.status = QLatin1String(accountPresenceStatuses[statusIndex]);
    
    Q_ASSERT(!m_account.isNull());
    
    Tp::PendingOperation* presenceRequest = m_account->setRequestedPresence(presence);
}