/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2022  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2006-2012  Christophe Dumez <chris@qbittorrent.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 */

#include "torrentcontentmodelfolder.h"

#include <QVariant>

#include "base/global.h"

TorrentContentModelFolder::TorrentContentModelFolder(const QString &name)
{
    m_name = name;
}

TorrentContentModelFolder::~TorrentContentModelFolder()
{
    deleteAllChildren();
}

TorrentContentModelItem::ItemType TorrentContentModelFolder::itemType() const
{
    return ITEM_TYPE;
}

void TorrentContentModelFolder::deleteAllChildren()
{
    // use copy of m_childItems for qDeleteAll
    // to avoid collision when child removes
    // itself from parent children
    qDeleteAll(decltype(m_childItems)(m_childItems));
}

const QVector<TorrentContentModelItem *> &TorrentContentModelFolder::children() const
{
    return m_childItems;
}

void TorrentContentModelFolder::appendChild(TorrentContentModelItem *item)
{
    Q_ASSERT(item);
    Q_ASSERT(item->parent() != this);

    if (item->parent())
        item->parent()->removeChild(item);

    m_childItems.append(item);
    item->m_parentItem = this;
    // Update own size
    increaseSize(item->size());
}

void TorrentContentModelFolder::removeChild(TorrentContentModelItem *item)
{
    Q_ASSERT(item);
    Q_ASSERT(item->parent() == this);

    m_childItems.removeOne(item);
    item->m_parentItem = nullptr;
    decreaseSize(item->size());
}

TorrentContentModelItem *TorrentContentModelFolder::child(int row) const
{
    return m_childItems.value(row, nullptr);
}

TorrentContentModelItem *TorrentContentModelFolder::itemByName(const QString &name) const
{
    for (TorrentContentModelItem *child : asConst(m_childItems))
    {
        if (child->name() == name)
            return child;
    }

    return nullptr;
}

int TorrentContentModelFolder::childCount() const
{
    return m_childItems.count();
}

void TorrentContentModelFolder::updatePriority()
{
    Q_ASSERT(!m_childItems.isEmpty());

    // If all children have the same priority
    // then the folder should have the same
    // priority
    const BitTorrent::DownloadPriority prio = m_childItems.first()->priority();
    for (int i = 1; i < m_childItems.size(); ++i)
    {
        if (m_childItems.at(i)->priority() != prio)
        {
            setPriority(BitTorrent::DownloadPriority::Mixed);
            return;
        }
    }
    // All child items have the same priority
    // Update own if necessary
    setPriority(prio);
}

void TorrentContentModelFolder::setPriority(BitTorrent::DownloadPriority newPriority)
{
    if (m_priority == newPriority)
        return;

    m_priority = newPriority;

    // Update parent priority
    if (m_parentItem)
        m_parentItem->updatePriority();
}

void TorrentContentModelFolder::recalculateProgress()
{
    qreal tProgress = 0;
    qulonglong tSize = 0;
    qulonglong tRemaining = 0;
    for (TorrentContentModelItem *child : asConst(m_childItems))
    {
        if (child->priority() == BitTorrent::DownloadPriority::Ignored)
            continue;

        if (child->itemType() == FolderType)
            static_cast<TorrentContentModelFolder *>(child)->recalculateProgress();
        tProgress += child->progress() * child->size();
        tSize += child->size();
        tRemaining += child->remaining();
    }

    if (tSize > 0)
    {
        m_progress = tProgress / tSize;
        m_remaining = tRemaining;
        Q_ASSERT(m_progress <= 1.);
    }
}

void TorrentContentModelFolder::recalculateAvailability()
{
    qreal tAvailability = 0;
    qulonglong tSize = 0;
    bool foundAnyData = false;
    for (TorrentContentModelItem *child : asConst(m_childItems))
    {
        if (child->priority() == BitTorrent::DownloadPriority::Ignored)
            continue;

        if (child->itemType() == FolderType)
            static_cast<TorrentContentModelFolder *>(child)->recalculateAvailability();
        const qreal childAvailability = child->availability();
        if (childAvailability >= 0)
        { // -1 means "no data"
            tAvailability += childAvailability * child->size();
            foundAnyData = true;
        }
        tSize += child->size();
    }

    if ((tSize > 0) && foundAnyData)
    {
        m_availability = tAvailability / tSize;
        Q_ASSERT(m_availability <= 1.);
    }
    else
    {
        m_availability = -1.;
    }
}

void TorrentContentModelFolder::increaseSize(qulonglong delta)
{
    m_size += delta;
    if (m_parentItem)
        m_parentItem->increaseSize(delta);
}

void TorrentContentModelFolder::decreaseSize(qulonglong delta)
{
    m_size -= delta;
    if (m_parentItem)
        m_parentItem->decreaseSize(delta);
}
