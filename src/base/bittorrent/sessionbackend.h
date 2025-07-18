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

#include <functional>
#include <memory>
#include <vector>

#include <libtorrent/address.hpp>
#include <libtorrent/fwd.hpp>
#include <libtorrent/portmap.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>

#include <QObject>
#include <QSet>

class Executor;

namespace BitTorrent
{
    class TorrentBackend;
    class TorrentID;

    class SessionBackend : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY_MOVE(SessionBackend)

    public:
        using AddTorrentAlertHandler = std::function<void (const lt::add_torrent_alert *alert)>;

        using QObject::QObject;

        // Sync API
        virtual std::shared_ptr<TorrentBackend> createTorrentBackend(lt::torrent_handle ltTorrentHandle) = 0;
        virtual void getPendingAlerts(std::vector<lt::alert *> &alerts, const lt::time_duration time = lt::time_duration::zero()) const = 0;
        virtual bool isSessionListening() const = 0;
        virtual lt::session_proxy *abort() = 0;

        // Async API
        virtual void pause() = 0;
        virtual void resume() = 0;
        virtual void addTorrentAsync(lt::add_torrent_params ltAddTorrentParams, AddTorrentAlertHandler handler) = 0;
        virtual void removeTorrent(lt::torrent_handle ltTorrentHandle) = 0;
        virtual void blockIP(boost::asio::ip::address addr) = 0;
        virtual void setIPFilter(lt::ip_filter ipFilter) = 0;
        virtual void setPeerFilters(lt::ip_filter classFilter, lt::peer_class_type_filter classTypeFilter) = 0;
        virtual void setPortMappingEnabled(bool enabled) = 0;
        virtual void addMappedPorts(QSet<quint16> ports) = 0;
        virtual void removeMappedPorts(QSet<quint16> ports) = 0;
        virtual void applySettings(lt::settings_pack settingsPack) = 0;
        virtual void postTorrentUpdates(lt::status_flags_t flags = lt::status_flags_t::all()) = 0;
        virtual void postSessionStats() = 0;

        static SessionBackend *create(Executor *executor, lt::session *ltSession);

    signals:
        void torrentsUpdated(const QList<TorrentID> &torrents);
    };
}
