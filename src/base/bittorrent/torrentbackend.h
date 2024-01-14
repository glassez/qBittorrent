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

#pragma once

#include <vector>

#include <libtorrent/fwd.hpp>
#include <libtorrent/torrent_handle.hpp>

#include <QtContainerFwd>
#include <QObject>
#include <QPromise>
#include <QReadWriteLock>

#include "base/pathfwd.h"
#include "torrent.h"
// TODO: Move TorrentOperatingMode to separate header!

class QUrl;

namespace BitTorrent
{
    class PeerInfo;
    class TorrentInfo;
    struct PeerAddress;
    struct SSLParameters;
    struct TrackerEntry;

    class TorrentBackend final : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY_MOVE(TorrentBackend)

    public:
        TorrentBackend(lt::session *ltSession, lt::torrent_handle ltTorrentHandle, QObject *parent = nullptr);
        ~TorrentBackend() override;

        lt::torrent_handle ltTorrentHandle() const; // thread-safe

        void start(TorrentOperatingMode mode);
        void stop();
        void forceRecheck();
        void forceReannounce(int index);
        void forceDHTAnnounce();
        void addTrackers(const QList<TrackerEntry> &trackers);
        void replaceTrackers(const QList<TrackerEntry> &trackers);
        void addUrlSeeds(const QList<QUrl> &urlSeeds);
        void removeUrlSeeds(const QList<QUrl> &urlSeeds);
        void connectPeer(const PeerAddress &peerAddress);
        void clearPeers();
        void setMetadata(const TorrentInfo &torrentInfo);
        void setSequentialDownload(bool enable);
        void setSuperSeeding(bool enable);
        void setDHTDisabled(bool enable);
        void setPEXDisabled(bool disable);
        void setLSDDisabled(bool disable);
        void setSSLParameters(const SSLParameters &sslParameters);
        void setDownloadLimit(int limit);
        void setUploadLimit(int limit);
        void flushCache();
        void renameFile(lt::file_index_t index, const Path &path);
        void prioritizeFiles(const std::vector<lt::download_priority_t> &filePriorities);
        void prioritizePieces(const std::vector<lt::download_priority_t> &piecePriorities);
        void requestResumeData(lt::resume_data_flags_t flags);
        void reload(const lt::add_torrent_params &ltAddTorrentParams, bool isStopped, TorrentOperatingMode operatingMode);

        void fetchPeerInfo(QPromise<QList<PeerInfo>> promise);
        void fetchDownloadingPieces(QPromise<QList<int>> promise);
        void fetchPieceAvailability(QPromise<QList<int>> promise);
        void fetchURLSeeds(QPromise<QList<QUrl>> promise);
        void fetchAnnounceEntries(QPromise<std::vector<lt::announce_entry>> promise);
        void fetchTorrentFileWithHashes(QPromise<std::shared_ptr<const lt::torrent_info>> promise);

    signals:
        void reloaded(const lt::torrent_status &torrentStatus);

    private:
        lt::session *m_ltSession;
        lt::torrent_handle m_ltTorrentHandle;
        mutable QReadWriteLock m_torrentHandleLock;
    };
}
