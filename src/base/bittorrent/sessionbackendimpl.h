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

#include <QHash>
#include <QList>

#include "infohash.h"
#include "sessionbackend.h"

class NativeSessionExtension;

namespace BitTorrent
{
    class TorrentBackendImpl;

    class SessionBackendImpl final : public SessionBackend
    {
        Q_OBJECT
        Q_DISABLE_COPY_MOVE(SessionBackendImpl)

    public:
        SessionBackendImpl(Executor *executor, lt::session *ltSession, QObject *parent = nullptr);

        // Sync API
        std::shared_ptr<TorrentBackend> createTorrentBackend(lt::torrent_handle ltTorrentHandle) override;
        void getPendingAlerts(std::vector<lt::alert *> &alerts, const lt::time_duration time = lt::time_duration::zero()) const override;
        bool isSessionListening() const override;
        lt::session_proxy *abort() override;

        // Async API
        void pause() override;
        void resume() override;
        void addTorrentAsync(lt::add_torrent_params ltAddTorrentParams, AddTorrentAlertHandler handler) override;
        void removeTorrent(lt::torrent_handle ltTorrentHandle) override;
        void blockIP(boost::asio::ip::address addr) override;
        void setIPFilter(lt::ip_filter ipFilter) override;
        void setPeerFilters(lt::ip_filter classFilter, lt::peer_class_type_filter classTypeFilter) override;
        void setPortMappingEnabled(bool enabled) override;
        void addMappedPorts(QSet<quint16> ports) override;
        void removeMappedPorts(QSet<quint16> ports) override;
        void applySettings(lt::settings_pack settingsPack) override;
        void postTorrentUpdates(lt::status_flags_t flags = lt::status_flags_t::all()) override;
        void postSessionStats() override;

    private:
        std::shared_ptr<TorrentBackendImpl> getTorrent(const lt::torrent_handle &nativeHandle) const;
        void readAlerts();
        void handleAlert(lt::alert *alert);
        void handleAddTorrentAlert(const lt::add_torrent_alert *alert);
        void handleStateUpdateAlert(const lt::state_update_alert *alert);

        Executor *m_executor = nullptr;
        lt::session *m_ltSession = nullptr;
        NativeSessionExtension *m_nativeSessionExtension = nullptr;

        QHash<TorrentID, std::shared_ptr<TorrentBackendImpl>> m_torrents;

        bool m_isPortMappingEnabled = false;
        QHash<quint16, std::vector<lt::port_mapping_t>> m_mappedPorts;

        std::vector<lt::alert *> m_alerts;  // make it a class variable so it can preserve its allocated `capacity`

        QList<AddTorrentAlertHandler> m_addTorrentAlertHandlers;
    };
}
