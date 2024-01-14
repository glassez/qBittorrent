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

#include "sessionbackend.h"

#include <libtorrent/ip_filter.hpp>
#include <libtorrent/session.hpp>

#include "base/algorithm.h"
#include "base/executor.h"
#include "nativesessionextension.h"
#include "torrentbackend.h"

BitTorrent::SessionBackend::SessionBackend(Executor *executor, lt::session *ltSession, QObject *parent)
    : QObject(parent)
    , m_executor {executor}
    , m_ltSession {ltSession}
{
    auto nativeSessionExtension = std::make_shared<NativeSessionExtension>();
    m_ltSession->add_extension(nativeSessionExtension);
    m_nativeSessionExtension = nativeSessionExtension.get();
}

std::shared_ptr<BitTorrent::TorrentBackend> BitTorrent::SessionBackend::createTorrentBackend(lt::torrent_handle ltTorrentHandle) const
{
    return std::make_shared<TorrentBackend>(m_executor, m_ltSession, std::move(ltTorrentHandle));
}

void BitTorrent::SessionBackend::getPendingAlerts(std::vector<lt::alert *> &alerts, const lt::time_duration time) const
{
    if (time > lt::time_duration::zero())
        m_ltSession->wait_for_alert(time);

    alerts.clear();
    m_ltSession->pop_alerts(&alerts);
}

bool BitTorrent::SessionBackend::isSessionListening() const
{
    return m_nativeSessionExtension->isSessionListening();
}

lt::session_proxy *BitTorrent::SessionBackend::abort()
{
    auto *nativeSessionProxy = new lt::session_proxy(m_ltSession->abort());
    delete m_ltSession;
    return nativeSessionProxy;
}

void BitTorrent::SessionBackend::pause()
{
    m_executor->addJob([this]
    {
        m_ltSession->pause();
    });
}

void BitTorrent::SessionBackend::resume()
{
    m_executor->addJob([this]
    {
        m_ltSession->resume();
    });
}

void BitTorrent::SessionBackend::addTorrentAsync(lt::add_torrent_params ltAddTorrentParams)
{
    m_executor->addJob([this, ltAddTorrentParams = std::move(ltAddTorrentParams)]() mutable
    {
        m_ltSession->async_add_torrent(std::move(ltAddTorrentParams));
    });
}

void BitTorrent::SessionBackend::removeTorrent(lt::torrent_handle ltTorrentHandle)
{
    m_executor->addJob([this, ltTorrentHandle = std::move(ltTorrentHandle)]() mutable
    {
        m_ltSession->remove_torrent(std::move(ltTorrentHandle), lt::session::delete_partfile);
    });
}

void BitTorrent::SessionBackend::blockIP(boost::asio::ip::address addr)
{
    m_executor->addJob([this, addr = std::move(addr)]
    {
        lt::ip_filter filter = m_ltSession->get_ip_filter();
        filter.add_rule(addr, addr, lt::ip_filter::blocked);
        m_ltSession->set_ip_filter(std::move(filter));
    });
}

void BitTorrent::SessionBackend::setIPFilter(lt::ip_filter ipFilter)
{
    m_executor->addJob([this, ipFilter = std::move(ipFilter)]() mutable
    {
        m_ltSession->set_ip_filter(std::move(ipFilter));
    });
}

void BitTorrent::SessionBackend::setPeerFilters(lt::ip_filter classFilter, lt::peer_class_type_filter classTypeFilter)
{
    m_executor->addJob([this, classFilter = std::move(classFilter)
            , classTypeFilter = std::move(classTypeFilter)]() mutable
    {
        m_ltSession->set_peer_class_filter(std::move(classFilter));
        m_ltSession->set_peer_class_type_filter(std::move(classTypeFilter));
    });
}

void BitTorrent::SessionBackend::setPortMappingEnabled(const bool enabled)
{
    if (m_isPortMappingEnabled == enabled)
        return;

    m_isPortMappingEnabled = enabled;

    m_executor->addJob([this, enabled]
    {
        if (!enabled)
            m_mappedPorts.clear();

        lt::settings_pack settingsPack;
        settingsPack.set_bool(lt::settings_pack::enable_upnp, enabled);
        settingsPack.set_bool(lt::settings_pack::enable_natpmp, enabled);
        m_ltSession->apply_settings(std::move(settingsPack));
    });
}

void BitTorrent::SessionBackend::addMappedPorts(QSet<quint16> ports)
{
    if (!m_isPortMappingEnabled)
        return;

    m_executor->addJob([this, ports = std::move(ports)]
    {
        for (const quint16 port : ports)
        {
            if (!m_mappedPorts.contains(port))
                m_mappedPorts.insert(port, m_ltSession->add_port_mapping(lt::session::tcp, port, port));
        }
    });
}

void BitTorrent::SessionBackend::removeMappedPorts(QSet<quint16> ports)
{
    if (!m_isPortMappingEnabled)
        return;

    m_executor->addJob([this, ports = std::move(ports)]
    {
        Algorithm::removeIf(m_mappedPorts
                , [this, &ports](const quint16 port, const std::vector<lt::port_mapping_t> &handles)
        {
            if (!ports.contains(port))
                return false;

            for (const lt::port_mapping_t &handle : handles)
                m_ltSession->delete_port_mapping(handle);

            return true;
        });
    });
}

void BitTorrent::SessionBackend::applySettings(lt::settings_pack settingsPack)
{
    m_executor->addJob([this, settingsPack = std::move(settingsPack)]() mutable
    {
        m_ltSession->apply_settings(std::move(settingsPack));
    });
}

void BitTorrent::SessionBackend::postTorrentUpdates(const lt::status_flags_t flags)
{
    m_executor->addJob([this, flags]
    {
        m_ltSession->post_torrent_updates(flags);
    });
}

void BitTorrent::SessionBackend::postSessionStats()
{
    m_executor->addJob([this]
    {
        m_ltSession->post_session_stats();
    });
}
