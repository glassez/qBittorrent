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

#include <memory>

#include <QReadWriteLock>

#include "torrentbackend.h"

class Executor;

namespace BitTorrent
{
    class TorrentBackendImpl final : public TorrentBackend, public std::enable_shared_from_this<TorrentBackendImpl>
    {
        Q_OBJECT
        Q_DISABLE_COPY_MOVE(TorrentBackendImpl)

    public:
        TorrentBackendImpl(Executor *executor, lt::session *ltSession, lt::torrent_handle ltTorrentHandle, QObject *parent = nullptr);
        ~TorrentBackendImpl() override;

        InfoHash infoHash() const override; // thread-safe

        void start(TorrentOperatingMode mode) override;
        void stop() override;
        void forceRecheck() override;
        void forceAnnounce(int index, int seconds = 0, lt::reannounce_flags_t flags = {}) override;
        void forceDHTAnnounce() override;
        void addTrackers(QList<TrackerEntry> trackers) override;
        void replaceTrackers(QList<TrackerEntry> trackers) override;
        void addUrlSeeds(QList<QUrl> urlSeeds) override;
        void removeUrlSeeds(QList<QUrl> urlSeeds) override;
        void connectPeer(PeerAddress peerAddress) override;
        void clearPeers() override;
        void setMaxConnections(int max) override;
        void setMaxUploads(int max) override;
        void setMetadata(TorrentInfo torrentInfo) override;
        void setSequentialDownload(bool enable) override;
        void setSuperSeeding(bool enable) override;
        void setDHTDisabled(bool enable) override;
        void setPEXDisabled(bool disable) override;
        void setLSDDisabled(bool disable) override;
        void setSSLParameters(SSLParameters sslParameters) override;
        void setDownloadLimit(int limit) override;
        void setUploadLimit(int limit) override;
        void flushCache() override;
        void renameFile(lt::file_index_t index, Path path) override;
        void prioritizeFiles(std::vector<lt::download_priority_t> filePriorities) override;
        void prioritizePieces(std::vector<lt::download_priority_t> piecePriorities) override;
        void queuePositionUp() override;
        void queuePositionDown() override;
        void queuePositionTop() override;
        void queuePositionBottom() override;
        void requestResumeData(lt::resume_data_flags_t flags) override;
        void reload(lt::add_torrent_params ltAddTorrentParams, bool isStopped, TorrentOperatingMode operatingMode) override;

        QFuture<QList<PeerInfo>> fetchPeerInfo() override;
        QFuture<QList<int>> fetchDownloadingPieces() override;
        QFuture<QList<int>> fetchPieceAvailability() override;
        QFuture<QList<QUrl>> fetchURLSeeds() override;
        QFuture<std::vector<lt::announce_entry>> fetchAnnounceEntries() override;
        QFuture<std::shared_ptr<const libtorrent::torrent_info>> fetchTorrentFileWithHashes() override;

        lt::torrent_handle ltTorrentHandle() const; // thread-safe

        void handleStateUpdated(const lt::torrent_status &nativeStatus);

    private:
        Executor *m_executor = nullptr;
        lt::session *m_ltSession = nullptr;
        lt::torrent_handle m_ltTorrentHandle;
        mutable QReadWriteLock m_torrentHandleLock;
    };
}
