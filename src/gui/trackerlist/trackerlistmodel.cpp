/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2023  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2006  Christophe Dumez <chris@qbittorrent.org>
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

#include "trackerlistmodel.h"

#include <chrono>

#include <QColor>
#include <QList>
#include <QPointer>
#include <QScopeGuard>
#include <QTimer>
#include <memory>

#include "base/bittorrent/peerinfo.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrent.h"
#include "base/bittorrent/trackerentry.h"
#include "base/global.h"
#include "base/utils/misc.h"

using namespace std::chrono_literals;

const std::chrono::milliseconds ANNOUNCE_TIME_REFRESH_INTERVAL = 1s;

namespace
{
    const QString STR_WORKING = TrackerListModel::tr("Working");
    const QString STR_DISABLED = TrackerListModel::tr("Disabled");
    const QString STR_TORRENT_DISABLED = TrackerListModel::tr("Disabled for this torrent");
    const QString STR_PRIVATE_MSG = TrackerListModel::tr("This torrent is private");

    QString prettyCount(const int val)
    {
        return (val > -1) ? QString::number(val) : TrackerListModel::tr("N/A");
    }

    QString toString(const BitTorrent::TrackerEntry::Status status)
    {
        switch (status)
        {
        case BitTorrent::TrackerEntry::Status::Working:
            return TrackerListModel::tr("Working");
        case BitTorrent::TrackerEntry::Status::Updating:
            return TrackerListModel::tr("Updating...");
        case BitTorrent::TrackerEntry::Status::NotWorking:
            return TrackerListModel::tr("Not working");
        case BitTorrent::TrackerEntry::Status::TrackerError:
            return TrackerListModel::tr("Tracker error");
        case BitTorrent::TrackerEntry::Status::Unreachable:
            return TrackerListModel::tr("Unreachable");
        case BitTorrent::TrackerEntry::Status::NotContacted:
            return TrackerListModel::tr("Not contacted yet");
        }
        return TrackerListModel::tr("Invalid status!");
    }

    QString statusDHT(const BitTorrent::Torrent *torrent)
    {
        if (!torrent->session()->isDHTEnabled())
            return STR_DISABLED;

        if (torrent->isPrivate() || torrent->isDHTDisabled())
            return STR_TORRENT_DISABLED;

        return STR_WORKING;
    }

    QString statusPeX(const BitTorrent::Torrent *torrent)
    {
        if (!torrent->session()->isPeXEnabled())
            return STR_DISABLED;

        if (torrent->isPrivate() || torrent->isPEXDisabled())
            return STR_TORRENT_DISABLED;

        return STR_WORKING;
    }

    QString statusLSD(const BitTorrent::Torrent *torrent)
    {
        if (!torrent->session()->isLSDEnabled())
            return STR_DISABLED;

        if (torrent->isPrivate() || torrent->isLSDDisabled())
            return STR_TORRENT_DISABLED;

        return STR_WORKING;
    }
}

struct TrackerListModel::Item
{
    QString name {};
    int tier = -1;
    int btVersion = -1;
    BitTorrent::TrackerEntry::Status status = BitTorrent::TrackerEntry::Status::NotContacted;
    QString message {};

    int numPeers = -1;
    int numSeeds = -1;
    int numLeeches = -1;
    int numDownloaded = -1;

    QDateTime nextAnnounceTime {};
    QDateTime minAnnounceTime {};

    std::weak_ptr<Item> parentItem {};

    QList<std::shared_ptr<Item>> childItems {};
    QHash<std::pair<QString, int>, std::shared_ptr<Item>> childItemsByID {};

    explicit Item(const QStringView name)
        : name {name.toString()}
    {
    }

    explicit Item(const BitTorrent::TrackerEntry &trackerEntry)
        : name {trackerEntry.url}
    {
        fillFrom(trackerEntry);
    }

    Item(const std::shared_ptr<Item> &parentItem, const BitTorrent::TrackerEntry::EndpointEntry &endpointEntry)
        : name {endpointEntry.name}
        , btVersion {endpointEntry.btVersion}
        , parentItem {parentItem}
    {
        fillFrom(endpointEntry);
    }

    void fillFrom(const BitTorrent::TrackerEntry &trackerEntry)
    {
        Q_ASSERT(parentItem.expired());
        Q_ASSERT(trackerEntry.url == name);

        tier = trackerEntry.tier;
        status = trackerEntry.status;
        message = trackerEntry.message;
        numPeers = trackerEntry.numPeers;
        numSeeds = trackerEntry.numSeeds;
        numLeeches = trackerEntry.numLeeches;
        numDownloaded = trackerEntry.numDownloaded;
        nextAnnounceTime = trackerEntry.nextAnnounceTime;
        minAnnounceTime = trackerEntry.minAnnounceTime;
    }

    void fillFrom(const BitTorrent::TrackerEntry::EndpointEntry &endpointEntry)
    {
        Q_ASSERT(!parentItem.expired());
        Q_ASSERT(endpointEntry.name == name);
        Q_ASSERT(endpointEntry.btVersion == btVersion);

        status = endpointEntry.status;
        message = endpointEntry.message;
        numPeers = endpointEntry.numPeers;
        numSeeds = endpointEntry.numSeeds;
        numLeeches = endpointEntry.numLeeches;
        numDownloaded = endpointEntry.numDownloaded;
        nextAnnounceTime = endpointEntry.nextAnnounceTime;
        minAnnounceTime = endpointEntry.minAnnounceTime;
    }
};

TrackerListModel::TrackerListModel(BitTorrent::Session *btSession, QObject *parent)
    : QAbstractItemModel(parent)
    , m_btSession {btSession}
    , m_announceRefreshTimer {new QTimer(this)}
{
    Q_ASSERT(m_btSession);

    m_announceRefreshTimer->setSingleShot(true);
    connect(m_announceRefreshTimer, &QTimer::timeout, this, &TrackerListModel::refreshAnnounceTimes);

    connect(m_btSession, &BitTorrent::Session::trackersAdded, this
            , [this](BitTorrent::Torrent *torrent, const QList<BitTorrent::TrackerEntry> &newTrackers)
    {
        if (torrent == m_torrent)
            onTrackersAdded(newTrackers);
    });
    connect(m_btSession, &BitTorrent::Session::trackersRemoved, this
            , [this](BitTorrent::Torrent *torrent, const QStringList &deletedTrackers)
    {
        if (torrent == m_torrent)
            onTrackersRemoved(deletedTrackers);
    });
    connect(m_btSession, &BitTorrent::Session::trackersChanged, this
            , [this](BitTorrent::Torrent *torrent)
    {
        if (torrent == m_torrent)
            onTrackersChanged();
    });
    connect(m_btSession, &BitTorrent::Session::trackerEntriesUpdated, this
            , [this](BitTorrent::Torrent *torrent, const QHash<QString, BitTorrent::TrackerEntry> &updatedTrackers)
    {
        if (torrent == m_torrent)
            onTrackersUpdated(updatedTrackers);
    });
}

TrackerListModel::~TrackerListModel() = default;

void TrackerListModel::setTorrent(BitTorrent::Torrent *torrent)
{
    beginResetModel();
    [[maybe_unused]] const auto modelResetGuard = qScopeGuard([this] { endResetModel(); });

    if (m_torrent)
    {
        m_items.clear();
        m_itemsByURL.clear();
    }

    m_torrent = torrent;

    if (m_torrent)
        populate();
    else
        m_announceRefreshTimer->stop();
}

BitTorrent::Torrent *TrackerListModel::torrent() const
{
    return m_torrent;
}

void TrackerListModel::populate()
{
    Q_ASSERT(m_torrent);

    const QList<BitTorrent::TrackerEntry> trackerEntries = m_torrent->trackers();
    m_items.reserve(trackerEntries.size() + STICKY_ROW_COUNT);

    m_items.emplaceBack(std::make_shared<Item>(u"** [DHT] **"));
    m_items.emplaceBack(std::make_shared<Item>(u"** [PeX] **"));
    m_items.emplaceBack(std::make_shared<Item>(u"** [LSD] **"));

    using TorrentPtr = QPointer<const BitTorrent::Torrent>;
    m_torrent->fetchPeerInfo([this, torrent = TorrentPtr(m_torrent)](const QList<BitTorrent::PeerInfo> &peers)
    {
       if (torrent != m_torrent)
           return;

       // XXX: libtorrent should provide this info...
       // Count peers from DHT, PeX, LSD
       uint seedsDHT = 0, seedsPeX = 0, seedsLSD = 0, peersDHT = 0, peersPeX = 0, peersLSD = 0;
       for (const BitTorrent::PeerInfo &peer : peers)
       {
            if (peer.isConnecting())
                continue;

            if (peer.isSeed())
            {
                if (peer.fromDHT())
                    ++seedsDHT;

                if (peer.fromPeX())
                    ++seedsPeX;

                if (peer.fromLSD())
                    ++seedsLSD;
            }
            else
            {
                if (peer.fromDHT())
                    ++peersDHT;

                if (peer.fromPeX())
                    ++peersPeX;

                if (peer.fromLSD())
                    ++peersLSD;
            }
        }

        m_items[ROW_DHT]->numSeeds = seedsDHT;
        m_items[ROW_DHT]->numLeeches = peersDHT;
        m_items[ROW_PEX]->numSeeds = seedsPeX;
        m_items[ROW_PEX]->numLeeches = peersPeX;
        m_items[ROW_LSD]->numSeeds = seedsLSD;
        m_items[ROW_LSD]->numLeeches = peersLSD;

        emit dataChanged(index(ROW_DHT, COL_SEEDS), index(ROW_LSD, COL_LEECHES));
    });

    if (m_torrent->isPrivate())
    {
        m_items[ROW_DHT]->message = STR_PRIVATE_MSG;
        m_items[ROW_PEX]->message = STR_PRIVATE_MSG;
        m_items[ROW_LSD]->message = STR_PRIVATE_MSG;
    }

    for (const BitTorrent::TrackerEntry &trackerEntry : trackerEntries)
        addTrackerItem(trackerEntry);

    m_announceTimestamp = QDateTime::currentDateTime();
    m_announceRefreshTimer->start(ANNOUNCE_TIME_REFRESH_INTERVAL);
}

std::shared_ptr<TrackerListModel::Item> TrackerListModel::createTrackerItem(const BitTorrent::TrackerEntry &trackerEntry)
{
    auto item = std::make_shared<Item>(trackerEntry);
    for (const auto &[id, endpointEntry] : trackerEntry.endpointEntries.asKeyValueRange())
    {
        const auto &childItem = item->childItems.emplaceBack(std::make_shared<Item>(item, endpointEntry));
        item->childItemsByID.insert(id, childItem);
    }

    return item;
}

void TrackerListModel::addTrackerItem(const BitTorrent::TrackerEntry &trackerEntry)
{
    auto item = m_items.emplaceBack(createTrackerItem(trackerEntry));
    m_itemsByURL.insert(item->name, item);
}

void TrackerListModel::updateTrackerItem(const std::shared_ptr<Item> &item, const BitTorrent::TrackerEntry &trackerEntry)
{
    QSet<std::pair<QString, int>> endpointItemIDs;
    QHash<std::pair<QString, int>, std::shared_ptr<Item>> newEndpointItems;
    for (const auto &[id, endpointEntry] : trackerEntry.endpointEntries.asKeyValueRange())
    {
        endpointItemIDs.insert(id);

        if (auto currentItem = item->childItemsByID.value(id))
        {
            currentItem->fillFrom(endpointEntry);
        }
        else
        {
            newEndpointItems.emplace(id, std::make_shared<Item>(item, endpointEntry));
        }
    }

    const auto trackerRow = m_items.indexOf(item);
    const auto trackerIndex = index(trackerRow, 0);

    item->childItemsByID.removeIf([this, &item, &trackerIndex, &endpointItemIDs](const decltype(item->childItemsByID)::iterator &endpointItemIter)
    {
        const auto &endpointItemID = endpointItemIter.key();
        if (endpointItemIDs.contains(endpointItemID))
            return false;

        const auto &endpointItem = endpointItemIter.value();
        const auto endpointItemRow = item->childItems.indexOf(endpointItem);
        beginRemoveRows(trackerIndex, endpointItemRow, endpointItemRow);
        item->childItems.remove(endpointItemRow);
        endRemoveRows();
        return true;
    });

    emit dataChanged(index(0, 0, trackerIndex), index(0, (columnCount(trackerIndex) - 1), trackerIndex));

    if (!newEndpointItems.isEmpty())
    {
        const auto numRows = rowCount(trackerIndex);
        beginInsertRows(trackerIndex, numRows, (numRows + newEndpointItems.size() - 1));
        item->childItemsByID.insert(newEndpointItems);
        item->childItems.append(newEndpointItems.values());
        endInsertRows();
    }

    item->fillFrom(trackerEntry);
    emit dataChanged(trackerIndex, index(trackerRow, (columnCount() - 1)));
}

void TrackerListModel::refreshAnnounceTimes()
{
    if (!m_torrent)
        return;

    m_announceTimestamp = QDateTime::currentDateTime();
    emit dataChanged(index(0, COL_NEXT_ANNOUNCE), index((rowCount() - 1), COL_MIN_ANNOUNCE));
    for (int i = 0; i < rowCount(); ++i)
    {
        const QModelIndex parentIndex = index(i, 0);
        emit dataChanged(index(0, COL_NEXT_ANNOUNCE, parentIndex), index((rowCount(parentIndex) - 1), COL_MIN_ANNOUNCE, parentIndex));
    }

    m_announceRefreshTimer->start(ANNOUNCE_TIME_REFRESH_INTERVAL);
}

int TrackerListModel::columnCount([[maybe_unused]] const QModelIndex &parent) const
{
    return COL_COUNT;
}

int TrackerListModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_items.size();

    const auto *item = static_cast<Item *>(parent.internalPointer());
    Q_ASSERT(item);
    if (!item) [[unlikely]]
        return 0;

    return item->childItems.size();
}

QVariant TrackerListModel::headerData(const int section, const Qt::Orientation orientation, const int role) const
{
    if (orientation != Qt::Horizontal)
        return {};

    switch (role)
    {
    case Qt::DisplayRole:
        switch (section)
        {
        case COL_URL:
            return tr("URL/Announce endpoint");
        case COL_TIER:
            return tr("Tier");
        case COL_PROTOCOL:
            return tr("Protocol");
        case COL_STATUS:
            return tr("Status");
        case COL_PEERS:
            return tr("Peers");
        case COL_SEEDS:
            return tr("Seeds");
        case COL_LEECHES:
            return tr("Leeches");
        case COL_TIMES_DOWNLOADED:
            return tr("Times Downloaded");
        case COL_MSG:
            return tr("Message");
        case COL_NEXT_ANNOUNCE:
            return tr("Next announce");
        case COL_MIN_ANNOUNCE:
            return tr("Min announce");
        default:
            return {};
        }

    case Qt::TextAlignmentRole:
        switch (section)
        {
        case COL_TIER:
        case COL_PEERS:
        case COL_SEEDS:
        case COL_LEECHES:
        case COL_TIMES_DOWNLOADED:
        case COL_NEXT_ANNOUNCE:
        case COL_MIN_ANNOUNCE:
            return QVariant {Qt::AlignRight | Qt::AlignVCenter};
        default:
            return {};
        }

    default:
        return {};
    }
}

QVariant TrackerListModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || (index.column() < 0) || (index.column() >= COL_COUNT))
        return {};

    const QModelIndex &parent = index.parent();
    const bool isEndpoint = parent.isValid();
    if (isEndpoint && ((parent.row() < 0) || (parent.row() >= m_items.size())))
        return {};

    const auto &items = !isEndpoint ? m_items : m_items.at(parent.row())->childItems;
    if ((index.row() < 0) || (index.row() >= items.size()))
        return {};

    const std::shared_ptr<Item> &item = items.at(index.row());

    const auto secsToNextAnnounce = std::max<int64_t>(0, m_announceTimestamp.secsTo(item->nextAnnounceTime));
    const auto secsToMinAnnounce = std::max<int64_t>(0, m_announceTimestamp.secsTo(item->minAnnounceTime));

    switch (role)
    {
    case Qt::TextAlignmentRole:
        switch (index.column())
        {
        case COL_TIER:
        case COL_PROTOCOL:
        case COL_PEERS:
        case COL_SEEDS:
        case COL_LEECHES:
        case COL_TIMES_DOWNLOADED:
        case COL_NEXT_ANNOUNCE:
        case COL_MIN_ANNOUNCE:
            return QVariant {Qt::AlignRight | Qt::AlignVCenter};
        default:
            return {};
        }

    case Qt::ForegroundRole:
        // TODO: Make me configurable via UI Theme
        if (!index.parent().isValid() && (index.row() < STICKY_ROW_COUNT))
            return QColorConstants::Svg::grey;
        return {};

    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        switch (index.column())
        {
        case COL_URL:
            return item->name;
        case COL_TIER:
            return (isEndpoint || (index.row() < STICKY_ROW_COUNT)) ? QString() : QString::number(item->tier);
        case COL_PROTOCOL:
            return isEndpoint ? tr("v%1").arg(item->btVersion) : QString();
        case COL_STATUS:
            if (isEndpoint)
                return toString(item->status);
            if (index.row() == ROW_DHT)
                return statusDHT(m_torrent);
            if (index.row() == ROW_PEX)
                return statusPeX(m_torrent);
            if (index.row() == ROW_LSD)
                return statusLSD(m_torrent);
            return toString(item->status);
        case COL_PEERS:
            return prettyCount(item->numPeers);
        case COL_SEEDS:
            return prettyCount(item->numSeeds);
        case COL_LEECHES:
            return prettyCount(item->numLeeches);
        case COL_TIMES_DOWNLOADED:
            return prettyCount(item->numDownloaded);
        case COL_MSG:
            return item->message;
        case COL_NEXT_ANNOUNCE:
            return Utils::Misc::userFriendlyDuration(secsToNextAnnounce, -1, Utils::Misc::TimeResolution::Seconds);
        case COL_MIN_ANNOUNCE:
            return Utils::Misc::userFriendlyDuration(secsToMinAnnounce, -1, Utils::Misc::TimeResolution::Seconds);
        default:
            return {};
        }

    case Roles::UnderlyingDataRole:
        switch (index.column())
        {
        case COL_URL:
            return item->name;
        case COL_TIER:
            return isEndpoint ? -1 : item->tier;
        case COL_PROTOCOL:
            return isEndpoint ? item->btVersion : -1;
        case COL_STATUS:
            return static_cast<int>(item->status);
        case COL_PEERS:
            return item->numPeers;
        case COL_SEEDS:
            return item->numSeeds;
        case COL_LEECHES:
            return item->numLeeches;
        case COL_TIMES_DOWNLOADED:
            return item->numDownloaded;
        case COL_MSG:
            return item->message;
        case COL_NEXT_ANNOUNCE:
            return QVariant::fromValue(secsToNextAnnounce);
        case COL_MIN_ANNOUNCE:
            return QVariant::fromValue(secsToMinAnnounce);
        default:
            return {};
        }

    default:
        break;
    }

    return {};
}

QModelIndex TrackerListModel::index(const int row, const int column, const QModelIndex &parent) const
{
    if ((column < 0) || (column >= columnCount()))
        return {};

    if ((row < 0) || (row >= rowCount(parent)))
        return {};

    const std::shared_ptr<Item> item = parent.isValid() ? m_items.value(parent.row())->childItems.value(row) : m_items.value(row);
    return createIndex(row, column, item.get());
}

QModelIndex TrackerListModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};

    const auto *item = static_cast<Item *>(index.internalPointer());
    Q_ASSERT(item);
    if (!item) [[unlikely]]
        return {};

    const std::shared_ptr<Item> parentItem = item->parentItem.lock();
    if (!parentItem)
        return {};

    // From https://doc.qt.io/qt-6/qabstractitemmodel.html#parent:
    // A common convention used in models that expose tree data structures is that only items
    // in the first column have children. For that case, when reimplementing this function in
    // a subclass the column of the returned QModelIndex would be 0.
    return createIndex(m_items.indexOf(parentItem), 0, parentItem.get());
}

void TrackerListModel::onTrackersAdded(const QList<BitTorrent::TrackerEntry> &newTrackers)
{
    const auto row = rowCount();
    beginInsertRows({}, row, (row + newTrackers.size() - 1));
    for (const BitTorrent::TrackerEntry &trackerEntry : newTrackers)
        addTrackerItem(trackerEntry);
    endInsertRows();
}

void TrackerListModel::onTrackersRemoved(const QStringList &deletedTrackers)
{
    for (const QString &trackerURL : deletedTrackers)
    {
        const auto item = m_itemsByURL.take(trackerURL);
        const auto row = m_items.indexOf(item);
        if (row >= 0)
        {
            beginRemoveRows({}, row, row);
            m_items.removeOne(item);
            endRemoveRows();
        }
    }
}

void TrackerListModel::onTrackersChanged()
{
    QSet<QString> trackerItemIDs;
    for (int i = 0; i < STICKY_ROW_COUNT; ++i)
        trackerItemIDs.insert(m_items.at(i)->name);

    QHash<QString, std::shared_ptr<Item>> newTrackerItems;
    for (const BitTorrent::TrackerEntry &trackerEntry : m_torrent->trackers())
    {
        trackerItemIDs.insert(trackerEntry.url);

        const auto &currentItem = m_itemsByURL[trackerEntry.url];
        if (currentItem)
        {
            updateTrackerItem(currentItem, trackerEntry);
        }
        else
        {
            newTrackerItems.emplace(trackerEntry.url, createTrackerItem(trackerEntry));
        }
    }

    m_itemsByURL.removeIf([this, &trackerItemIDs](const decltype(m_itemsByURL)::iterator &trackerItemIter)
    {
        const auto &trackerItemID = trackerItemIter.key();
        if (trackerItemIDs.contains(trackerItemID))
            return false;

        const auto &trackerItem = trackerItemIter.value();
        const auto trackerItemRow = m_items.indexOf(trackerItem);
        beginRemoveRows({}, trackerItemRow, trackerItemRow);
        m_items.remove(trackerItemRow);
        endRemoveRows();
        return true;
    });

    emit dataChanged(index(0, 0), index(0, (columnCount() - 1)));

    if (!newTrackerItems.isEmpty())
    {
        const auto numRows = rowCount();
        beginInsertRows({}, numRows, (numRows + newTrackerItems.size() - 1));
        m_itemsByURL.insert(newTrackerItems);
        m_items.append(newTrackerItems.values());
        endInsertRows();
    }
}

void TrackerListModel::onTrackersUpdated(const QHash<QString, BitTorrent::TrackerEntry> &updatedTrackers)
{
    for (const auto &[url, entry] : updatedTrackers.asKeyValueRange())
    {
        auto item = m_itemsByURL.value(url);
        if (item) [[likely]]
            updateTrackerItem(item, entry);
    }
}
