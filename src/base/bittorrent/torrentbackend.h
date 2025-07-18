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

#pragma once

#include <vector>

#include <libtorrent/fwd.hpp>
#include <libtorrent/torrent_handle.hpp>

#include <QtContainerFwd>
#include <QFuture>
#include <QObject>

#include "base/pathfwd.h"
#include "infohash.h"
#include "torrentoperatingmode.h"

class QUrl;

namespace BitTorrent
{
    class PeerInfo;
    class TorrentInfo;
    struct PeerAddress;
    struct SSLParameters;
    struct TrackerEntry;

    class TorrentBackend : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY_MOVE(TorrentBackend)

    public:
        using QObject::QObject;

        virtual InfoHash infoHash() const = 0; // thread-safe

        virtual void start(TorrentOperatingMode mode) = 0;
        virtual void stop() = 0;
        virtual void forceRecheck() = 0;
        virtual void forceAnnounce(int index, int seconds = 0, lt::reannounce_flags_t flags = {}) = 0;
        virtual void forceDHTAnnounce() = 0;
        virtual void addTrackers(QList<TrackerEntry> trackers) = 0;
        virtual void replaceTrackers(QList<TrackerEntry> trackers) = 0;
        virtual void addUrlSeeds(QList<QUrl> urlSeeds) = 0;
        virtual void removeUrlSeeds(QList<QUrl> urlSeeds) = 0;
        virtual void connectPeer(PeerAddress peerAddress) = 0;
        virtual void clearPeers() = 0;
        virtual void setMaxConnections(int max) = 0;
        virtual void setMaxUploads(int max) = 0;
        virtual void setMetadata(TorrentInfo torrentInfo) = 0;
        virtual void setSequentialDownload(bool enable) = 0;
        virtual void setSuperSeeding(bool enable) = 0;
        virtual void setDHTDisabled(bool enable) = 0;
        virtual void setPEXDisabled(bool disable) = 0;
        virtual void setLSDDisabled(bool disable) = 0;
        virtual void setSSLParameters(SSLParameters sslParameters) = 0;
        virtual void setDownloadLimit(int limit) = 0;
        virtual void setUploadLimit(int limit) = 0;
        virtual void flushCache() = 0;
        virtual void renameFile(lt::file_index_t index, Path path) = 0;
        virtual void prioritizeFiles(std::vector<lt::download_priority_t> filePriorities) = 0;
        virtual void prioritizePieces(std::vector<lt::download_priority_t> piecePriorities) = 0;
        virtual void queuePositionUp() = 0;
        virtual void queuePositionDown() = 0;
        virtual void queuePositionTop() = 0;
        virtual void queuePositionBottom() = 0;
        virtual void requestResumeData(lt::resume_data_flags_t flags) = 0;
        virtual void reload(lt::add_torrent_params ltAddTorrentParams, bool isStopped, TorrentOperatingMode operatingMode) = 0;

        virtual QFuture<QList<PeerInfo>> fetchPeerInfo() = 0;
        virtual QFuture<QList<int>> fetchDownloadingPieces() = 0;
        virtual QFuture<QList<int>> fetchPieceAvailability() = 0;
        virtual QFuture<QList<QUrl>> fetchURLSeeds() = 0;
        virtual QFuture<std::vector<lt::announce_entry>> fetchAnnounceEntries() = 0;
        virtual QFuture<std::shared_ptr<const libtorrent::torrent_info>> fetchTorrentFileWithHashes() = 0;

    signals:
        void stateUpdated(const lt::torrent_status &torrentStatus);
        void reloaded(const lt::torrent_status &torrentStatus);
    };
}
