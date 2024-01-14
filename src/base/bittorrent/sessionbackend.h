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

#include <memory>
#include <vector>

#include <libtorrent/address.hpp>
#include <libtorrent/fwd.hpp>
#include <libtorrent/portmap.hpp>
#include <libtorrent/torrent_handle.hpp>

#include <QHash>
#include <QObject>
#include <QPromise>
#include <QSet>

class NativeSessionExtension;

namespace BitTorrent
{
    class TorrentBackend;

    class SessionBackend final : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY_MOVE(SessionBackend)

    public:
        SessionBackend(lt::session *ltSession, QObject *parent = nullptr);

        // Sync API
        TorrentBackend *createTorrentBackend(lt::torrent_handle ltTorrentHandle) const;
        std::vector<lt::alert *> getPendingAlerts(const lt::time_duration time = lt::time_duration::zero()) const;
        bool isSessionListening() const;
        lt::session_proxy *abort();

        // Async API
        void pause();
        void resume();
        void addTorrentAsync(lt::add_torrent_params ltAddTorrentParams);
        void removeTorrent(const lt::torrent_handle &ltTorrentHandle);
        void blockIP(const lt::address &addr);
        void setIPFilter(const lt::ip_filter &ipFilter);
        void setPeerFilters(const lt::ip_filter &classFilter, const lt::peer_class_type_filter &classTypeFilter);
        void setPortMappingEnabled(bool enabled);
        void addMappedPorts(const QSet<quint16> &ports);
        void removeMappedPorts(const QSet<quint16> &ports);
        void applySettings(lt::settings_pack settingsPack);
        void postTorrentUpdates(lt::status_flags_t flags = lt::status_flags_t::all());
        void postSessionStats();

    signals:
        void alertsReady();

    private:
        lt::session *m_ltSession = nullptr;
        NativeSessionExtension *m_nativeSessionExtension = nullptr;

        bool m_isPortMappingEnabled = false;
        QHash<quint16, std::vector<lt::port_mapping_t>> m_mappedPorts;
    };
}
