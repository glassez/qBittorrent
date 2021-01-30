/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2021  Vladimir Golovnev <glassez@yandex.ru>
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

#include "persistentdatastorage.h"

#include <libtorrent/bdecode.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>

#include <QDateTime>
#include <QDebug>
#include <QSet>

#include "base/profile.h"
#include "base/utils/fs.h"
#include "base/utils/string.h"
#include "session.h"
#include "torrentimpl.h"

namespace
{
    lt::entry::list_type setToEntryList(const QSet<QString> &input)
    {
         lt::entry::list_type entryList;
        for (const QString &setValue : input)
            entryList.emplace_back(setValue.toStdString());
        return entryList;
    }

    template <typename LTStr>
    QString fromLTString(const LTStr &str)
    {
        return QString::fromUtf8(str.data(), static_cast<int>(str.size()));
    }

    void applyFlag(lt::torrent_flags_t &currentFlags, const lt::torrent_flags_t flag, bool value)
    {
        value ? (currentFlags |= flag) : (currentFlags &= ~flag);
    }
}

BitTorrent::PersistentDataStorage::PersistentDataStorage(const QString &torrentID)
    : m_torrentID {torrentID}
{
}

QString BitTorrent::PersistentDataStorage::torrentID() const
{
    return m_torrentID;
}

bool BitTorrent::PersistentDataStorage::hasChangedData() const
{
    return m_hasChangedData;
}

void BitTorrent::PersistentDataStorage::setDataChanged()
{
    if (!m_hasChangedData)
    {
        m_hasChangedData = true;
        emit dataChanged();
    }
}

void BitTorrent::PersistentDataStorage::acceptChangedData()
{
    m_hasChangedData = false;
}

BitTorrent::BencodeDataStorage::BencodeDataStorage(const QString &torrentID, const LoadTorrentParams &data)
    : PersistentDataStorage {torrentID}
    , m_nativeParams {data.ltAddTorrentParams}
    , m_nativeTorrentInfo {std::move(m_nativeParams.ti)}
    , m_name {data.name}
    , m_category {data.category}
    , m_tags {data.tags}
    , m_savePath {data.savePath}
    , m_contentLayout {data.contentLayout}
    , m_operatingMode {(data.forced ? TorrentOperatingMode::Forced : TorrentOperatingMode::AutoManaged)}
    , m_firstLastPiecePriority {data.firstLastPiecePriority}
    , m_hasSeedStatus {data.hasSeedStatus}
    , m_isStopped {data.paused}
    , m_ratioLimit {data.ratioLimit}
    , m_seedingTimeLimit {data.seedingTimeLimit}
{
}

QVariant BitTorrent::BencodeDataStorage::loadData(ItemID itemID) const
{
    switch (itemID)
    {
    case ItemID::Name:
        return m_name;

    case ItemID::SavePath:
        return m_savePath;

    case ItemID::Category:
        return m_category;

    case ItemID::Tags:
        return QVariant::fromValue(m_tags);

    case ItemID::RatioLimit:
        return m_ratioLimit;

    case ItemID::SeedingTimeLimit:
        return m_seedingTimeLimit;

    case ItemID::HasSeedStatus:
        return m_hasSeedStatus;

    case ItemID::HasFirstLastPiecePriority:
        return m_firstLastPiecePriority;

    case ItemID::IsStopped:
        return m_isStopped;

    case ItemID::OperatingMode:
        return QVariant::fromValue(m_operatingMode);

    case ItemID::ContentLayout:
        return QVariant::fromValue(m_contentLayout);

    case ItemID::StorageMode:
        return QVariant::fromValue(m_nativeParams.storage_mode);

    case ItemID::StorageLocation:
        return QVariant::fromValue(m_nativeParams.save_path);

    case ItemID::TotalDownloaded:
        return m_nativeParams.total_downloaded;

    case ItemID::TotalUploaded:
        return m_nativeParams.total_uploaded;

    case ItemID::ActiveTime:
        return m_nativeParams.active_time;

    case ItemID::FinishedTime:
        return m_nativeParams.finished_time;

    case ItemID::SeedingTime:
        return m_nativeParams.seeding_time;

    case ItemID::LastSeenComplete:
        return QVariant::fromValue(m_nativeParams.last_seen_complete);

    case ItemID::LastDownload:
        return QVariant::fromValue(m_nativeParams.last_download);

    case ItemID::LastUpload:
        return QVariant::fromValue(m_nativeParams.last_upload);

    case ItemID::NumComplete:
        return m_nativeParams.num_complete;

    case ItemID::NumIncomplete:
        return m_nativeParams.num_incomplete;

    case ItemID::NumDownloaded:
        return m_nativeParams.num_downloaded;

    case ItemID::SeedMode:
        return static_cast<bool>(m_nativeParams.flags & lt::torrent_flags::seed_mode);

    case ItemID::UploadMode:
        return static_cast<bool>(m_nativeParams.flags & lt::torrent_flags::upload_mode);

    case ItemID::ShareMode:
        return static_cast<bool>(m_nativeParams.flags & lt::torrent_flags::share_mode);

    case ItemID::ApplyIpFilter:
        return static_cast<bool>(m_nativeParams.flags & lt::torrent_flags::apply_ip_filter);

    case ItemID::SuperSeeding:
        return static_cast<bool>(m_nativeParams.flags & lt::torrent_flags::super_seeding);

    case ItemID::SequentialDownload:
        return static_cast<bool>(m_nativeParams.flags & lt::torrent_flags::sequential_download);

    case ItemID::StopWhenReady:
        return static_cast<bool>(m_nativeParams.flags & lt::torrent_flags::stop_when_ready);

    case ItemID::DisableDHT:
        return static_cast<bool>(m_nativeParams.flags & lt::torrent_flags::disable_dht);

    case ItemID::DisableLSD:
        return static_cast<bool>(m_nativeParams.flags & lt::torrent_flags::disable_lsd);

    case ItemID::DisablePEX:
        return static_cast<bool>(m_nativeParams.flags & lt::torrent_flags::disable_pex);

    case ItemID::AddedTime:
        return QVariant::fromValue(m_nativeParams.added_time);

    case ItemID::CompletedTime:
        return QVariant::fromValue(m_nativeParams.completed_time);

    case ItemID::UploadRateLimit:
        return m_nativeParams.upload_limit;

    case ItemID::DownloadRateLimit:
        return m_nativeParams.download_limit;

    case ItemID::MaxConnections:
        return m_nativeParams.max_connections;

    case ItemID::MaxUploads:
        return m_nativeParams.max_uploads;

    case ItemID::MerkleTree:
        return QVariant::fromValue<Sha1HashVectorType>(m_nativeParams.merkle_tree);

    case ItemID::URLSeeds:
        return QVariant::fromValue<StringVectorType>(m_nativeParams.url_seeds);

    case ItemID::HTTPSeeds:
        return QVariant::fromValue<StringVectorType>(m_nativeParams.http_seeds);

    case ItemID::Pieces:
        return QVariant::fromValue<PiecesType>({m_nativeParams.have_pieces, m_nativeParams.verified_pieces});

    case ItemID::UnfinishedPieces:
        return QVariant::fromValue<PieceMapType>(m_nativeParams.unfinished_pieces);

    case ItemID::Trackers:
        return QVariant::fromValue<TrackersType>({m_nativeParams.trackers, m_nativeParams.tracker_tiers});

    case ItemID::RenamedFiles:
        return QVariant::fromValue<FileMapType>(m_nativeParams.renamed_files);

    case ItemID::Peers:
        return QVariant::fromValue<EndpointVectorType>(m_nativeParams.peers);

    case ItemID::BannedPeers:
        return QVariant::fromValue<EndpointVectorType>(m_nativeParams.banned_peers);

    case ItemID::FilePriorities:
        return QVariant::fromValue<PriorityVectorType>(m_nativeParams.file_priorities);

    case ItemID::PiecePriorities:
        return QVariant::fromValue<PriorityVectorType>(m_nativeParams.piece_priorities);

    default:
        Q_ASSERT(false);
    }

    return {};
}

void BitTorrent::BencodeDataStorage::storeData(PersistentDataStorage::ItemID itemID, const QVariant &value)
{
    qDebug() << Q_FUNC_INFO;

    switch (itemID)
    {
    case ItemID::Name:
        m_name = value.toString();
        break;

    case ItemID::SavePath:
        m_savePath = value.toString();
        break;

    case ItemID::Category:
        m_category = value.toString();
        break;

    case ItemID::Tags:
        m_tags = value.value<QSet<QString>>();
        break;

    case ItemID::RatioLimit:
        m_ratioLimit = value.toReal();
        break;

    case ItemID::SeedingTimeLimit:
        m_seedingTimeLimit = value.toInt();
        break;

    case ItemID::HasSeedStatus:
        m_hasSeedStatus = value.toBool();
        break;

    case ItemID::HasFirstLastPiecePriority:
        m_firstLastPiecePriority = value.toBool();
        break;

    case ItemID::IsStopped:
        m_isStopped = value.toBool();
        break;

    case ItemID::OperatingMode:
        m_operatingMode = value.value<TorrentOperatingMode>();
        break;

    case ItemID::ContentLayout:
        m_contentLayout = value.value<TorrentContentLayout>();
        break;

    case ItemID::StorageMode:
        m_nativeParams.storage_mode = value.value<lt::storage_mode_t>();
        break;

    case ItemID::StorageLocation:
        m_nativeParams.save_path = value.value<std::string>();
        break;

    case ItemID::TotalDownloaded:
        m_nativeParams.total_downloaded = value.value<std::int64_t>();
        break;

    case ItemID::TotalUploaded:
        m_nativeParams.total_uploaded = value.value<std::int64_t>();
        break;

    case ItemID::ActiveTime:
        m_nativeParams.active_time = value.toInt();
        break;

    case ItemID::FinishedTime:
        m_nativeParams.finished_time = value.toInt();
        break;

    case ItemID::SeedingTime:
        m_nativeParams.seeding_time = value.toInt();
        break;

    case ItemID::LastSeenComplete:
        m_nativeParams.last_seen_complete = value.value<std::time_t>();
        break;

    case ItemID::LastDownload:
        m_nativeParams.last_download = value.value<std::time_t>();
        break;

    case ItemID::LastUpload:
        m_nativeParams.last_upload = value.value<std::time_t>();
        break;

    case ItemID::NumComplete:
        m_nativeParams.num_complete = value.toInt();
        break;

    case ItemID::NumIncomplete:
        m_nativeParams.num_incomplete = value.toInt();
        break;

    case ItemID::NumDownloaded:
        m_nativeParams.num_downloaded = value.toInt();
        break;

    case ItemID::SeedMode:
        applyFlag(m_nativeParams.flags, lt::torrent_flags::seed_mode, value.toBool());
        break;

    case ItemID::UploadMode:
        applyFlag(m_nativeParams.flags, lt::torrent_flags::upload_mode, value.toBool());
        break;

    case ItemID::ShareMode:
        applyFlag(m_nativeParams.flags, lt::torrent_flags::share_mode, value.toBool());
        break;

    case ItemID::ApplyIpFilter:
        applyFlag(m_nativeParams.flags, lt::torrent_flags::apply_ip_filter, value.toBool());
        break;

    case ItemID::SuperSeeding:
        applyFlag(m_nativeParams.flags, lt::torrent_flags::super_seeding, value.toBool());
        break;

    case ItemID::SequentialDownload:
        applyFlag(m_nativeParams.flags, lt::torrent_flags::sequential_download, value.toBool());
        break;

    case ItemID::StopWhenReady:
        applyFlag(m_nativeParams.flags, lt::torrent_flags::stop_when_ready, value.toBool());
        break;

    case ItemID::DisableDHT:
        applyFlag(m_nativeParams.flags, lt::torrent_flags::disable_dht, value.toBool());
        break;

    case ItemID::DisableLSD:
        applyFlag(m_nativeParams.flags, lt::torrent_flags::disable_lsd, value.toBool());
        break;

    case ItemID::DisablePEX:
        applyFlag(m_nativeParams.flags, lt::torrent_flags::disable_pex, value.toBool());
        break;

    case ItemID::AddedTime:
        m_nativeParams.added_time = value.value<std::time_t>();
        break;

    case ItemID::CompletedTime:
        m_nativeParams.completed_time = value.value<std::time_t>();
        break;

    case ItemID::UploadRateLimit:
        m_nativeParams.upload_limit = value.toInt();
        break;

    case ItemID::DownloadRateLimit:
        m_nativeParams.download_limit = value.toInt();
        break;

    case ItemID::MaxConnections:
        m_nativeParams.max_connections = value.toInt();
        break;

    case ItemID::MaxUploads:
        m_nativeParams.max_uploads = value.toInt();
        break;

    case ItemID::MerkleTree:
        m_nativeParams.merkle_tree = value.value<Sha1HashVectorType>();
        break;

    case ItemID::URLSeeds:
        m_nativeParams.url_seeds = value.value<StringVectorType>();
        break;

    case ItemID::HTTPSeeds:
        m_nativeParams.http_seeds = value.value<StringVectorType>();
        break;

    case ItemID::Pieces:
        std::tie(m_nativeParams.have_pieces, m_nativeParams.verified_pieces) = value.value<PiecesType>();
        break;

    case ItemID::UnfinishedPieces:
        m_nativeParams.unfinished_pieces = value.value<PieceMapType>();
        break;

    case ItemID::Trackers:
        std::tie(m_nativeParams.trackers, m_nativeParams.tracker_tiers) = value.value<TrackersType>();
        break;

    case ItemID::RenamedFiles:
        m_nativeParams.renamed_files = value.value<FileMapType>();
        break;

    case ItemID::Peers:
       m_nativeParams.peers = value.value<EndpointVectorType>();
       break;

    case ItemID::BannedPeers:
       m_nativeParams.banned_peers = value.value<EndpointVectorType>();
       break;

    case ItemID::FilePriorities:
       m_nativeParams.file_priorities = value.value<PriorityVectorType>();
       break;

    case ItemID::PiecePriorities:
       m_nativeParams.piece_priorities = value.value<PriorityVectorType>();
       break;

    default:
        Q_ASSERT(false);
    }

    setDataChanged();
}

lt::add_torrent_params BitTorrent::BencodeDataStorage::getLTAddTorrentParams() const
{
    lt::add_torrent_params result {m_nativeParams};

    result.ti = std::const_pointer_cast<lt::torrent_info>(m_nativeTorrentInfo);
//    result.flags |= lt::torrent_flags::update_subscribe
//            | lt::torrent_flags::override_trackers
//            | lt::torrent_flags::override_web_seeds;

    if (m_isStopped)
    {
        result.flags |= lt::torrent_flags::paused;
        result.flags &= ~lt::torrent_flags::auto_managed;
    }
    else if (m_operatingMode == TorrentOperatingMode::AutoManaged)
    {
        result.flags |= (lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
    }
    else
    {
        result.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
    }

    return result;
}

std::shared_ptr<const lt::torrent_info> BitTorrent::BencodeDataStorage::getLTTorrentInfo() const
{
    return m_nativeTorrentInfo;
}

void BitTorrent::BencodeDataStorage::setLTTorrentInfo(std::shared_ptr<const lt::torrent_info> torrentInfo)
{
    Q_ASSERT(!m_nativeTorrentInfo);
    m_nativeTorrentInfo = torrentInfo;
}

std::shared_ptr<lt::entry> BitTorrent::BencodeDataStorage::takeChangedData()
{
    if (m_isStopped)
    {
        m_nativeParams.flags |= lt::torrent_flags::paused;
        m_nativeParams.flags &= ~lt::torrent_flags::auto_managed;
    }
    else
    {
        // Torrent can be actually "running" but temporarily "paused" to perform some
        // service jobs behind the scenes so we need to restore it as "running"
        if (m_operatingMode == TorrentOperatingMode::AutoManaged)
        {
            m_nativeParams.flags |= lt::torrent_flags::auto_managed;
        }
        else
        {
            m_nativeParams.flags &= ~lt::torrent_flags::paused;
            m_nativeParams.flags &= ~lt::torrent_flags::auto_managed;
        }
    }

    auto resumeDataPtr = std::make_shared<lt::entry>(lt::write_resume_data(m_nativeParams));
    lt::entry &resumeData = *resumeDataPtr;

    resumeData["save_path"] = Profile::instance()->toPortablePath(
                QString::fromStdString(m_nativeParams.save_path)).toStdString();
    resumeData["qBt-savePath"] = Profile::instance()->toPortablePath(m_savePath).toStdString();
    resumeData["qBt-ratioLimit"] = static_cast<int>(m_ratioLimit * 1000);
    resumeData["qBt-seedingTimeLimit"] = m_seedingTimeLimit;
    resumeData["qBt-category"] = m_category.toStdString();
    resumeData["qBt-tags"] = setToEntryList(m_tags);
    resumeData["qBt-name"] = m_name.toStdString();
    resumeData["qBt-seedStatus"] = m_hasSeedStatus;
    resumeData["qBt-contentLayout"] = Utils::String::fromEnum(m_contentLayout).toStdString();
    resumeData["qBt-firstLastPiecePriority"] = m_firstLastPiecePriority;

    acceptChangedData();

    return resumeDataPtr;
}
