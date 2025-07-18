/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2025  Vladimir Golovnev <glassez@yandex.ru>
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

#include "torrentbackendimpl.h"

#include <libtorrent/announce_entry.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <QDebug>
#include <QList>
#include <QPromise>
#include <QUrl>

#include "base/executor.h"
#include "base/path.h"
#include "extensiondata.h"
#include "lttypecast.h"
#include "peeraddress.h"
#include "peerinfo.h"
#include "sslparameters.h"
#include "torrentinfo.h"
#include "trackerentry.h"

#ifndef QBT_USES_LIBTORRENT2
#include "customstorage.h"
#endif

namespace
{
    lt::announce_entry makeLTAnnounceEntry(const QString &url, const int tier)
    {
        lt::announce_entry entry {url.toStdString()};
        entry.tier = tier;
        return entry;
    }
}

BitTorrent::TorrentBackendImpl::TorrentBackendImpl(Executor *executor, lt::session *ltSession, lt::torrent_handle ltTorrentHandle, QObject *parent)
    : TorrentBackend(parent)
    , m_executor {executor}
    , m_ltSession {ltSession}
    , m_ltTorrentHandle {std::move(ltTorrentHandle)}
{
}

BitTorrent::TorrentBackendImpl::~TorrentBackendImpl()
{
    qDebug() << Q_FUNC_INFO;
}

lt::torrent_handle BitTorrent::TorrentBackendImpl::ltTorrentHandle() const
{
    const QReadLocker locker {&m_torrentHandleLock};
    return m_ltTorrentHandle;
}

BitTorrent::InfoHash BitTorrent::TorrentBackendImpl::infoHash() const
{
#ifdef QBT_USES_LIBTORRENT2
    return ltTorrentHandle().info_hashes();
#else
    return ltTorrentHandle().info_hash();
#endif
}

void BitTorrent::TorrentBackendImpl::start(const TorrentOperatingMode mode)
{
    m_executor->addJob([self = shared_from_this(), mode]
    {
        self->m_ltTorrentHandle.clear_error();
        self->m_ltTorrentHandle.unset_flags(lt::torrent_flags::upload_mode);

        if (mode == TorrentOperatingMode::Forced)
        {
            self->m_ltTorrentHandle.unset_flags(lt::torrent_flags::auto_managed);
            self->m_ltTorrentHandle.resume();
        }
        else
        {
            self->m_ltTorrentHandle.set_flags(lt::torrent_flags::auto_managed);
        }
    });
}

void BitTorrent::TorrentBackendImpl::stop()
{
    m_executor->addJob([self = shared_from_this()]
    {
        self->m_ltTorrentHandle.unset_flags(lt::torrent_flags::auto_managed);
        self->m_ltTorrentHandle.pause();
    });
}

void BitTorrent::TorrentBackendImpl::forceRecheck()
{
    m_executor->addJob([self = shared_from_this()]
    {
        self->m_ltTorrentHandle.force_recheck();
    });
}

void BitTorrent::TorrentBackendImpl::forceAnnounce(const int index, int seconds, const lt::reannounce_flags_t flags)
{
    m_executor->addJob([self = shared_from_this(), index, seconds, flags]
    {
        self->m_ltTorrentHandle.force_reannounce(seconds, index, flags);
    });
}

void BitTorrent::TorrentBackendImpl::forceDHTAnnounce()
{
    m_executor->addJob([self = shared_from_this()]
    {
        self->m_ltTorrentHandle.force_dht_announce();
    });
}

void BitTorrent::TorrentBackendImpl::addTrackers(QList<TrackerEntry> trackers)
{
    m_executor->addJob([self = shared_from_this(), trackers = std::move(trackers)]
    {
        for (const TrackerEntry &tracker : trackers)
            self->m_ltTorrentHandle.add_tracker(makeLTAnnounceEntry(tracker.url, tracker.tier));
    });
}

void BitTorrent::TorrentBackendImpl::replaceTrackers(QList<TrackerEntry> trackers)
{
    m_executor->addJob([self = shared_from_this(), trackers = std::move(trackers)]
    {
        std::vector<lt::announce_entry> ltAnnounceEntries;
        ltAnnounceEntries.reserve(trackers.size());
        for (const TrackerEntry &tracker : trackers)
            ltAnnounceEntries.emplace_back(makeLTAnnounceEntry(tracker.url, tracker.tier));
        self->m_ltTorrentHandle.replace_trackers(ltAnnounceEntries);
    });
}

void BitTorrent::TorrentBackendImpl::addUrlSeeds(QList<QUrl> urlSeeds)
{
    m_executor->addJob([self = shared_from_this(), urlSeeds = std::move(urlSeeds)]
    {
        for (const QUrl &url : urlSeeds)
            self->m_ltTorrentHandle.add_url_seed(url.toString().toStdString());
    });
}

void BitTorrent::TorrentBackendImpl::removeUrlSeeds(QList<QUrl> urlSeeds)
{
    m_executor->addJob([self = shared_from_this(), urlSeeds = std::move(urlSeeds)]
    {
        for (const QUrl &url : urlSeeds)
            self->m_ltTorrentHandle.remove_url_seed(url.toString().toStdString());
    });
}

void BitTorrent::TorrentBackendImpl::connectPeer(PeerAddress peerAddress)
{
    m_executor->addJob([self = shared_from_this(), peerAddress = std::move(peerAddress)]
    {
        try
        {
            lt::error_code ec;
            const lt::address addr = lt::make_address(peerAddress.ip.toString().toStdString(), ec);
            if (ec)
                throw lt::system_error(ec);

            self->m_ltTorrentHandle.connect_peer({addr, peerAddress.port});
        }
        catch (const lt::system_error &) {}
    });
}

void BitTorrent::TorrentBackendImpl::clearPeers()
{
    m_executor->addJob([self = shared_from_this()]
    {
        self->m_ltTorrentHandle.clear_peers();
    });
}

void BitTorrent::TorrentBackendImpl::setMaxConnections(const int max)
{
    m_executor->addJob([self = shared_from_this(), max]
    {
        self->m_ltTorrentHandle.set_max_connections(max);
    });
}

void BitTorrent::TorrentBackendImpl::setMaxUploads(const int max)
{
    m_executor->addJob([self = shared_from_this(), max]
    {
        self->m_ltTorrentHandle.set_max_uploads(max);
    });
}

void BitTorrent::TorrentBackendImpl::setMetadata(TorrentInfo torrentInfo)
{
    m_executor->addJob([self = shared_from_this(), torrentInfo = std::move(torrentInfo)]
    {
        try
        {
#ifdef QBT_USES_LIBTORRENT2
            self->m_ltTorrentHandle.set_metadata(torrentInfo.nativeInfo()->info_section());
#else
            const std::shared_ptr<lt::torrent_info> nativeInfo = torrentInfo.nativeInfo();
            self->m_ltTorrentHandle.set_metadata(lt::span<const char>(nativeInfo->metadata().get(), nativeInfo->metadata_size()));
#endif
        }
        catch (const std::exception &) {}
    });
}

void BitTorrent::TorrentBackendImpl::setSequentialDownload(const bool enable)
{
    m_executor->addJob([self = shared_from_this(), enable]
    {
        if (enable)
            self->m_ltTorrentHandle.set_flags(lt::torrent_flags::sequential_download);
        else
            self->m_ltTorrentHandle.unset_flags(lt::torrent_flags::sequential_download);
    });
}

void BitTorrent::TorrentBackendImpl::setSuperSeeding(const bool enable)
{
    m_executor->addJob([self = shared_from_this(), enable]
    {
        if (enable)
            self->m_ltTorrentHandle.set_flags(lt::torrent_flags::super_seeding);
        else
            self->m_ltTorrentHandle.unset_flags(lt::torrent_flags::super_seeding);
    });
}

void BitTorrent::TorrentBackendImpl::setDHTDisabled(const bool disable)
{
    m_executor->addJob([self = shared_from_this(), disable]
    {
        if (disable)
            self->m_ltTorrentHandle.set_flags(lt::torrent_flags::disable_dht);
        else
            self->m_ltTorrentHandle.unset_flags(lt::torrent_flags::disable_dht);
    });
}

void BitTorrent::TorrentBackendImpl::setPEXDisabled(const bool disable)
{
    m_executor->addJob([self = shared_from_this(), disable]
    {
        if (disable)
            self->m_ltTorrentHandle.set_flags(lt::torrent_flags::disable_pex);
        else
            self->m_ltTorrentHandle.unset_flags(lt::torrent_flags::disable_pex);
    });
}

void BitTorrent::TorrentBackendImpl::setLSDDisabled(const bool disable)
{
    m_executor->addJob([self = shared_from_this(), disable]
    {
        if (disable)
            self->m_ltTorrentHandle.set_flags(lt::torrent_flags::disable_lsd);
        else
            self->m_ltTorrentHandle.unset_flags(lt::torrent_flags::disable_lsd);
    });
}

void BitTorrent::TorrentBackendImpl::setSSLParameters(SSLParameters sslParameters)
{
    m_executor->addJob([self = shared_from_this(), sslParameters = std::move(sslParameters)]
    {
        self->m_ltTorrentHandle.set_ssl_certificate_buffer(sslParameters.certificate.toPem().toStdString()
                , sslParameters.privateKey.toPem().toStdString(), sslParameters.dhParams.toStdString());
    });
}

void BitTorrent::TorrentBackendImpl::setDownloadLimit(const int limit)
{
    m_executor->addJob([self = shared_from_this(), limit]
    {
        self->m_ltTorrentHandle.set_download_limit(limit);
    });
}

void BitTorrent::TorrentBackendImpl::setUploadLimit(const int limit)
{
    m_executor->addJob([self = shared_from_this(), limit]
    {
        self->m_ltTorrentHandle.set_upload_limit(limit);
    });
}

void BitTorrent::TorrentBackendImpl::flushCache()
{
    m_executor->addJob([self = shared_from_this()]
    {
        self->m_ltTorrentHandle.flush_cache();
    });
}

void BitTorrent::TorrentBackendImpl::renameFile(const lt::file_index_t index, Path path)
{
    m_executor->addJob([self = shared_from_this(), index, path = std::move(path)]
    {
        self->m_ltTorrentHandle.rename_file(index, path.toString().toStdString());
    });
}

void BitTorrent::TorrentBackendImpl::prioritizeFiles(std::vector<lt::download_priority_t> filePriorities)
{
    m_executor->addJob([self = shared_from_this(), filePriorities = std::move(filePriorities)]
    {
        self->m_ltTorrentHandle.prioritize_files(filePriorities);
    });
}

void BitTorrent::TorrentBackendImpl::prioritizePieces(std::vector<lt::download_priority_t> piecePriorities)
{
    m_executor->addJob([self = shared_from_this(), piecePriorities = std::move(piecePriorities)]
    {
        self->m_ltTorrentHandle.prioritize_pieces(piecePriorities);
    });
}

void BitTorrent::TorrentBackendImpl::queuePositionUp()
{
    m_executor->addJob([self = shared_from_this()]
    {
        self->m_ltTorrentHandle.queue_position_up();
    });
}

void BitTorrent::TorrentBackendImpl::queuePositionDown()
{
    m_executor->addJob([self = shared_from_this()]
    {
        self->m_ltTorrentHandle.queue_position_down();
    });
}

void BitTorrent::TorrentBackendImpl::queuePositionTop()
{
    m_executor->addJob([self = shared_from_this()]
    {
        self->m_ltTorrentHandle.queue_position_top();
    });
}

void BitTorrent::TorrentBackendImpl::queuePositionBottom()
{
    m_executor->addJob([self = shared_from_this()]
    {
        self->m_ltTorrentHandle.queue_position_bottom();
    });
}

void BitTorrent::TorrentBackendImpl::requestResumeData(const lt::resume_data_flags_t flags)
{
    m_executor->addJob([self = shared_from_this(), flags]
    {
        self->m_ltTorrentHandle.save_resume_data(flags);
    });
}

void BitTorrent::TorrentBackendImpl::reload(lt::add_torrent_params ltAddTorrentParams
        , const bool isStopped, const TorrentOperatingMode operatingMode)
{
    ltAddTorrentParams.flags |= lt::torrent_flags::update_subscribe
            | lt::torrent_flags::override_trackers
            | lt::torrent_flags::override_web_seeds;

    if (isStopped)
    {
        ltAddTorrentParams.flags |= lt::torrent_flags::paused;
        ltAddTorrentParams.flags &= ~lt::torrent_flags::auto_managed;
    }
    else if (operatingMode == TorrentOperatingMode::AutoManaged)
    {
        ltAddTorrentParams.flags |= (lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
    }
    else
    {
        ltAddTorrentParams.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
    }

    m_executor->addJob([self = shared_from_this()
            , ltAddTorrentParams = std::move(ltAddTorrentParams)]() mutable
    {
        lt::torrent_handle ltTorrentHandle = self->m_ltTorrentHandle;
        lt::session *ltSession = self->m_ltSession;

        const auto queuePos = ltTorrentHandle.queue_position();

        ltSession->remove_torrent(ltTorrentHandle, lt::session::delete_partfile);

        auto *const extensionData = new ExtensionData;
        ltAddTorrentParams.userdata = LTClientData(extensionData);
    #ifndef QBT_USES_LIBTORRENT2
        ltAddTorrentParams.storage = customStorageConstructor;
    #endif

        QWriteLocker locker {&self->m_torrentHandleLock};
        ltTorrentHandle = ltSession->add_torrent(ltAddTorrentParams);
        locker.unlock();

        if (queuePos >= lt::queue_position_t {})
            ltTorrentHandle.queue_position_set(queuePos);

        lt::torrent_status torrentStatus = extensionData->status;
        torrentStatus.queue_position = queuePos;

        emit self->reloaded(torrentStatus);
    });
}

QFuture<QList<BitTorrent::PeerInfo>> BitTorrent::TorrentBackendImpl::fetchPeerInfo()
{
    auto promise = std::make_shared<QPromise<QList<PeerInfo>>>();

    m_executor->addJob([self = shared_from_this(), promise]
    {
        promise->start();

        std::vector<lt::peer_info> nativePeers;
        self->m_ltTorrentHandle.get_peer_info(nativePeers);

        QList<PeerInfo> peers;
        peers.reserve(static_cast<decltype(peers)::size_type>(nativePeers.size()));

        for (const lt::peer_info &peer : nativePeers)
            peers.append(PeerInfo(peer));

        promise->addResult(peers);
        promise->finish();
    });

    return promise->future();
}

QFuture<QList<int>> BitTorrent::TorrentBackendImpl::fetchDownloadingPieces()
{
    auto promise = std::make_shared<QPromise<QList<int>>>();

    m_executor->addJob([self = shared_from_this(), promise]
    {
        promise->start();

        std::vector<lt::partial_piece_info> queue;
        self->m_ltTorrentHandle.get_download_queue(queue);

        QList<int> result;
        result.reserve(static_cast<qsizetype>(queue.size()));
        for (const lt::partial_piece_info &info : queue)
            result.append(LT::toUnderlyingType(info.piece_index));

        promise->addResult(result);
        promise->finish();
    });

    return promise->future();
}

QFuture<QList<int>> BitTorrent::TorrentBackendImpl::fetchPieceAvailability()
{
    auto promise = std::make_shared<QPromise<QList<int>>>();

    m_executor->addJob([self = shared_from_this(), promise]
    {
        promise->start();

        std::vector<int> avail;
        self->m_ltTorrentHandle.piece_availability(avail);

        promise->addResult(QList<int>(avail.cbegin(), avail.cend()));
        promise->finish();
    });

    return promise->future();
}

QFuture<QList<QUrl>> BitTorrent::TorrentBackendImpl::fetchURLSeeds()
{
    auto promise = std::make_shared<QPromise<QList<QUrl>>>();

    m_executor->addJob([self = shared_from_this(), promise]
    {
        promise->start();

        const std::set<std::string> currentSeeds = self->m_ltTorrentHandle.url_seeds();

        QList<QUrl> urlSeeds;
        urlSeeds.reserve(static_cast<decltype(urlSeeds)::size_type>(currentSeeds.size()));
        for (const std::string &urlSeed : currentSeeds)
            urlSeeds.append(QString::fromStdString(urlSeed));

        promise->addResult(urlSeeds);
        promise->finish();
    });

    return promise->future();
}

QFuture<std::vector<lt::announce_entry>> BitTorrent::TorrentBackendImpl::fetchAnnounceEntries()
{
    auto promise = std::make_shared<QPromise<std::vector<lt::announce_entry>>>();

    m_executor->addJob([self = shared_from_this(), promise]
    {
        promise->start();
        promise->addResult(self->m_ltTorrentHandle.trackers());
        promise->finish();
    });

    return promise->future();
}

void BitTorrent::TorrentBackendImpl::handleStateUpdated(const lt::torrent_status &nativeStatus)
{
    emit stateUpdated(nativeStatus);
}

QFuture<std::shared_ptr<const lt::torrent_info>> BitTorrent::TorrentBackendImpl::fetchTorrentFileWithHashes()
{
    auto promise = std::make_shared<QPromise<std::shared_ptr<const lt::torrent_info>>>();

    m_executor->addJob([self = shared_from_this(), promise]
    {
        promise->start();

#ifdef QBT_USES_LIBTORRENT2
        const std::shared_ptr<const lt::torrent_info> completeTorrentInfo = self->m_ltTorrentHandle.torrent_file_with_hashes();
        const std::shared_ptr<const lt::torrent_info> torrentInfo =
                (completeTorrentInfo ? completeTorrentInfo : self->m_ltTorrentHandle.torrent_file());
#else
        const std::shared_ptr<const lt::torrent_info> torrentInfo = self->m_ltTorrentHandle.torrent_file();
#endif

        promise->addResult(torrentInfo);
        promise->finish();
    });

    return promise->future();
}
