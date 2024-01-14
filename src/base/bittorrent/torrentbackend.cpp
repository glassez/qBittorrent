/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2024  Vladimir Golovnev <glassez@yandex.ru>
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

#include "torrentbackend.h"

#include <libtorrent/announce_entry.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <QDebug>
#include <QList>
#include <QUrl>

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

BitTorrent::TorrentBackend::TorrentBackend(lt::session *ltSession, lt::torrent_handle ltTorrentHandle, QObject *parent)
    : QObject(parent)
    , m_ltSession {ltSession}
    , m_ltTorrentHandle {std::move(ltTorrentHandle)}
{
}

BitTorrent::TorrentBackend::~TorrentBackend()
{
    qDebug() << Q_FUNC_INFO;
}

lt::torrent_handle BitTorrent::TorrentBackend::ltTorrentHandle() const
{
    const QReadLocker locker {&m_torrentHandleLock};
    return m_ltTorrentHandle;
}

void BitTorrent::TorrentBackend::start(const TorrentOperatingMode mode)
{
    m_ltTorrentHandle.clear_error();
    m_ltTorrentHandle.unset_flags(lt::torrent_flags::upload_mode);

    if (mode == TorrentOperatingMode::Forced)
    {
        m_ltTorrentHandle.unset_flags(lt::torrent_flags::auto_managed);
        m_ltTorrentHandle.resume();
    }
    else
    {
        m_ltTorrentHandle.set_flags(lt::torrent_flags::auto_managed);
    }
}

void BitTorrent::TorrentBackend::stop()
{
    m_ltTorrentHandle.unset_flags(lt::torrent_flags::auto_managed);
    m_ltTorrentHandle.pause();
}

void BitTorrent::TorrentBackend::forceRecheck()
{
    m_ltTorrentHandle.force_recheck();
}

void BitTorrent::TorrentBackend::forceReannounce(const int index)
{
    m_ltTorrentHandle.force_reannounce(0, index);
}

void BitTorrent::TorrentBackend::forceDHTAnnounce()
{
    m_ltTorrentHandle.force_dht_announce();
}

void BitTorrent::TorrentBackend::addTrackers(const QList<TrackerEntry> &trackers)
{
    for (const TrackerEntry &tracker : trackers)
        m_ltTorrentHandle.add_tracker(makeLTAnnounceEntry(tracker.url, tracker.tier));
}

void BitTorrent::TorrentBackend::replaceTrackers(const QList<TrackerEntry> &trackers)
{
    std::vector<lt::announce_entry> ltAnnounceEntries;
    ltAnnounceEntries.reserve(trackers.size());
    for (const TrackerEntry &tracker : trackers)
        ltAnnounceEntries.emplace_back(makeLTAnnounceEntry(tracker.url, tracker.tier));
    m_ltTorrentHandle.replace_trackers(ltAnnounceEntries);
}

void BitTorrent::TorrentBackend::addUrlSeeds(const QList<QUrl> &urlSeeds)
{
    for (const QUrl &url : urlSeeds)
        m_ltTorrentHandle.add_url_seed(url.toString().toStdString());
}

void BitTorrent::TorrentBackend::removeUrlSeeds(const QList<QUrl> &urlSeeds)
{
    for (const QUrl &url : urlSeeds)
        m_ltTorrentHandle.remove_url_seed(url.toString().toStdString());
}

void BitTorrent::TorrentBackend::connectPeer(const PeerAddress &peerAddress)
{
    try
    {
        lt::error_code ec;
        const lt::address addr = lt::make_address(peerAddress.ip.toString().toStdString(), ec);
        if (ec)
            throw lt::system_error(ec);

        m_ltTorrentHandle.connect_peer({addr, peerAddress.port});
    }
    catch (const lt::system_error &)
    {
    }
}

void BitTorrent::TorrentBackend::clearPeers()
{
    m_ltTorrentHandle.clear_peers();
}

void BitTorrent::TorrentBackend::setMetadata(const TorrentInfo &torrentInfo)
{
    try
    {
#ifdef QBT_USES_LIBTORRENT2
        m_ltTorrentHandle.set_metadata(torrentInfo.nativeInfo()->info_section());
#else
        const std::shared_ptr<lt::torrent_info> nativeInfo = torrentInfo.nativeInfo();
        m_ltTorrentHandle.set_metadata(lt::span<const char>(nativeInfo->metadata().get(), nativeInfo->metadata_size()));
#endif
    }
    catch (const std::exception &) {}
}

void BitTorrent::TorrentBackend::setSequentialDownload(const bool enable)
{
    if (enable)
        m_ltTorrentHandle.set_flags(lt::torrent_flags::sequential_download);
    else
        m_ltTorrentHandle.unset_flags(lt::torrent_flags::sequential_download);
}

void BitTorrent::TorrentBackend::setSuperSeeding(const bool enable)
{
    if (enable)
        m_ltTorrentHandle.set_flags(lt::torrent_flags::super_seeding);
    else
        m_ltTorrentHandle.unset_flags(lt::torrent_flags::super_seeding);
}

void BitTorrent::TorrentBackend::setDHTDisabled(const bool disable)
{
    if (disable)
        m_ltTorrentHandle.set_flags(lt::torrent_flags::disable_dht);
    else
        m_ltTorrentHandle.unset_flags(lt::torrent_flags::disable_dht);
}

void BitTorrent::TorrentBackend::setPEXDisabled(const bool disable)
{
    if (disable)
        m_ltTorrentHandle.set_flags(lt::torrent_flags::disable_pex);
    else
        m_ltTorrentHandle.unset_flags(lt::torrent_flags::disable_pex);
}

void BitTorrent::TorrentBackend::setLSDDisabled(const bool disable)
{
    if (disable)
        m_ltTorrentHandle.set_flags(lt::torrent_flags::disable_lsd);
    else
        m_ltTorrentHandle.unset_flags(lt::torrent_flags::disable_lsd);
}

void BitTorrent::TorrentBackend::setSSLParameters(const SSLParameters &sslParameters)
{
    m_ltTorrentHandle.set_ssl_certificate_buffer(sslParameters.certificate.toPem().toStdString()
            , sslParameters.privateKey.toPem().toStdString(), sslParameters.dhParams.toStdString());
}

void BitTorrent::TorrentBackend::setDownloadLimit(const int limit)
{
    m_ltTorrentHandle.set_download_limit(limit);
}

void BitTorrent::TorrentBackend::setUploadLimit(const int limit)
{
    m_ltTorrentHandle.set_upload_limit(limit);
}

void BitTorrent::TorrentBackend::flushCache()
{
    m_ltTorrentHandle.flush_cache();
}

void BitTorrent::TorrentBackend::renameFile(const lt::file_index_t index, const Path &path)
{
    m_ltTorrentHandle.rename_file(index, path.toString().toStdString());
}

void BitTorrent::TorrentBackend::prioritizeFiles(const std::vector<lt::download_priority_t> &filePriorities)
{
    m_ltTorrentHandle.prioritize_files(filePriorities);
}

void BitTorrent::TorrentBackend::prioritizePieces(const std::vector<lt::download_priority_t> &piecePriorities)
{
    m_ltTorrentHandle.prioritize_pieces(piecePriorities);
}

void BitTorrent::TorrentBackend::requestResumeData(const lt::resume_data_flags_t flags)
{
    m_ltTorrentHandle.save_resume_data(flags);
}

void BitTorrent::TorrentBackend::reload(const lt::add_torrent_params &ltAddTorrentParams
        , const bool isStopped, const TorrentOperatingMode operatingMode)
{
    const auto queuePos = m_ltTorrentHandle.queue_position();

    m_ltSession->remove_torrent(m_ltTorrentHandle, lt::session::delete_partfile);

    lt::add_torrent_params p = ltAddTorrentParams;
    p.flags |= lt::torrent_flags::update_subscribe
            | lt::torrent_flags::override_trackers
            | lt::torrent_flags::override_web_seeds;

    if (isStopped)
    {
        p.flags |= lt::torrent_flags::paused;
        p.flags &= ~lt::torrent_flags::auto_managed;
    }
    else if (operatingMode == TorrentOperatingMode::AutoManaged)
    {
        p.flags |= (lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
    }
    else
    {
        p.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
    }

    auto *const extensionData = new ExtensionData;
    p.userdata = LTClientData(extensionData);
#ifndef QBT_USES_LIBTORRENT2
    p.storage = customStorageConstructor;
#endif

    QWriteLocker locker {&m_torrentHandleLock};
    m_ltTorrentHandle = m_ltSession->add_torrent(p);
    locker.unlock();

    if (queuePos >= lt::queue_position_t {})
        m_ltTorrentHandle.queue_position_set(queuePos);

    lt::torrent_status torrentStatus = extensionData->status;
    torrentStatus.queue_position = queuePos;

    emit reloaded(torrentStatus);
}

void BitTorrent::TorrentBackend::fetchPeerInfo(QPromise<QList<PeerInfo>> promise)
{
    promise.start();

    std::vector<lt::peer_info> nativePeers;
    m_ltTorrentHandle.get_peer_info(nativePeers);

    QList<PeerInfo> peers;
    peers.reserve(static_cast<decltype(peers)::size_type>(nativePeers.size()));

    for (const lt::peer_info &peer : nativePeers)
        peers.append(PeerInfo(peer));

    promise.addResult(peers);
    promise.finish();
}

void BitTorrent::TorrentBackend::fetchDownloadingPieces(QPromise<QList<int>> promise)
{
    promise.start();

    std::vector<lt::partial_piece_info> queue;
    m_ltTorrentHandle.get_download_queue(queue);

    QList<int> result;
    result.reserve(static_cast<qsizetype>(queue.size()));
    for (const lt::partial_piece_info &info : queue)
        result.append(LT::toUnderlyingType(info.piece_index));

    promise.addResult(result);
    promise.finish();
}

void BitTorrent::TorrentBackend::fetchPieceAvailability(QPromise<QList<int>> promise)
{
    promise.start();

    std::vector<int> avail;
    m_ltTorrentHandle.piece_availability(avail);

    promise.addResult(QList<int>(avail.cbegin(), avail.cend()));
    promise.finish();
}

void BitTorrent::TorrentBackend::fetchURLSeeds(QPromise<QList<QUrl>> promise)
{
    promise.start();

    const std::set<std::string> currentSeeds = m_ltTorrentHandle.url_seeds();

    QList<QUrl> urlSeeds;
    urlSeeds.reserve(static_cast<decltype(urlSeeds)::size_type>(currentSeeds.size()));
    for (const std::string &urlSeed : currentSeeds)
        urlSeeds.append(QString::fromStdString(urlSeed));

    promise.addResult(urlSeeds);
    promise.finish();
}

void BitTorrent::TorrentBackend::fetchAnnounceEntries(QPromise<std::vector<lt::announce_entry>> promise)
{
    promise.start();
    promise.addResult(m_ltTorrentHandle.trackers());
    promise.finish();
}

void BitTorrent::TorrentBackend::fetchTorrentFileWithHashes(QPromise<std::shared_ptr<const lt::torrent_info>> promise)
{
    promise.start();

#ifdef QBT_USES_LIBTORRENT2
    const std::shared_ptr<const lt::torrent_info> completeTorrentInfo = m_ltTorrentHandle.torrent_file_with_hashes();
    const std::shared_ptr<const lt::torrent_info> torrentInfo = (completeTorrentInfo ? completeTorrentInfo : m_ltTorrentHandle.torrent_file());
#else
    const std::shared_ptr<const lt::torrent_info> torrentInfo = m_ltTorrentHandle.torrent_file();
#endif

    promise.addResult(torrentInfo);
    promise.finish();
}
