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

#include "sessionbackendimpl.h"

#include <libtorrent/alert_types.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/session.hpp>

#include "base/algorithm.h"
#include "base/executor.h"
#include "nativesessionextension.h"
#include "torrentbackendimpl.h"

namespace
{
    using namespace BitTorrent;

#ifdef QBT_USES_LIBTORRENT2
    template <typename T>
    concept HasInfoHashMember = requires (T t) { { t.info_hashes } -> std::convertible_to<InfoHash>; };

    template <typename T>
    concept HasInfoHashMemberFn = requires (T t) { { t.info_hashes() } -> std::convertible_to<InfoHash>; };

    template <HasInfoHashMember T>
    InfoHash getInfoHash(const T &t) { return t.info_hashes; }

    template <HasInfoHashMemberFn T>
    InfoHash getInfoHash(const T &t) { return t.info_hashes(); }

    InfoHash getInfoHash(const lt::add_torrent_params &addTorrentParams)
    {
        const bool hasMetadata = (addTorrentParams.ti && addTorrentParams.ti->is_valid());
        return hasMetadata ? getInfoHash(*addTorrentParams.ti) : InfoHash(addTorrentParams.info_hashes);
    }
#else
    template <typename T>
    concept HasInfoHashMember = requires (T t) { { t.info_hash } -> std::convertible_to<InfoHash>; };

    template <typename T>
    concept HasInfoHashMemberFn = requires (T t) { { t.info_hash() } -> std::convertible_to<InfoHash>; };

    template <HasInfoHashMember T>
    InfoHash getInfoHash(const T &t) { return t.info_hash; }

    template <HasInfoHashMemberFn T>
    InfoHash getInfoHash(const T &t) { return t.info_hash(); }

    InfoHash getInfoHash(const lt::add_torrent_params &addTorrentParams)
    {
        const bool hasMetadata = (addTorrentParams.ti && addTorrentParams.ti->is_valid());
        return hasMetadata ? getInfoHash(*addTorrentParams.ti) : InfoHash(addTorrentParams.info_hash);
    }
#endif
}

BitTorrent::SessionBackend *BitTorrent::SessionBackend::create(Executor *executor, lt::session *ltSession)
{
    return new SessionBackendImpl(executor, ltSession);
}

BitTorrent::SessionBackendImpl::SessionBackendImpl(Executor *executor, lt::session *ltSession, QObject *parent)
    : SessionBackend(parent)
    , m_executor {executor}
    , m_ltSession {ltSession}
{
    auto nativeSessionExtension = std::make_shared<NativeSessionExtension>();
    m_ltSession->add_extension(nativeSessionExtension);
    m_nativeSessionExtension = nativeSessionExtension.get();

    m_ltSession->set_alert_notify([this]
    {
        QMetaObject::invokeMethod(this, &SessionBackendImpl::readAlerts);
    });
}

std::shared_ptr<BitTorrent::TorrentBackend> BitTorrent::SessionBackendImpl::createTorrentBackend(lt::torrent_handle ltTorrentHandle)
{
    const auto torrent = std::make_shared<TorrentBackendImpl>(m_executor, m_ltSession, std::move(ltTorrentHandle));
    m_torrents.insert(torrent->infoHash().toTorrentID(), torrent);
    return torrent;
}

void BitTorrent::SessionBackendImpl::getPendingAlerts(std::vector<lt::alert *> &alerts, const lt::time_duration time) const
{
    if (time > lt::time_duration::zero())
        m_ltSession->wait_for_alert(time);

    alerts.clear();
    m_ltSession->pop_alerts(&alerts);
}

bool BitTorrent::SessionBackendImpl::isSessionListening() const
{
    return m_nativeSessionExtension->isSessionListening();
}

lt::session_proxy *BitTorrent::SessionBackendImpl::abort()
{
    auto *nativeSessionProxy = new lt::session_proxy(m_ltSession->abort());
    delete m_ltSession;
    return nativeSessionProxy;
}

void BitTorrent::SessionBackendImpl::pause()
{
    m_executor->addJob([this]
    {
        m_ltSession->pause();
    });
}

void BitTorrent::SessionBackendImpl::resume()
{
    m_executor->addJob([this]
    {
        m_ltSession->resume();
    });
}

void BitTorrent::SessionBackendImpl::addTorrentAsync(lt::add_torrent_params ltAddTorrentParams, AddTorrentAlertHandler handler)
{
    m_executor->addJob([this, ltAddTorrentParams = std::move(ltAddTorrentParams)]() mutable
    {
        m_ltSession->async_add_torrent(std::move(ltAddTorrentParams));
    });

    m_addTorrentAlertHandlers.append(std::move(handler));
}

void BitTorrent::SessionBackendImpl::removeTorrent(lt::torrent_handle ltTorrentHandle)
{
    m_executor->addJob([this, ltTorrentHandle = std::move(ltTorrentHandle)]() mutable
    {
        m_ltSession->remove_torrent(std::move(ltTorrentHandle), lt::session::delete_partfile);
    });
}

void BitTorrent::SessionBackendImpl::blockIP(boost::asio::ip::address addr)
{
    m_executor->addJob([this, addr = std::move(addr)]
    {
        lt::ip_filter filter = m_ltSession->get_ip_filter();
        filter.add_rule(addr, addr, lt::ip_filter::blocked);
        m_ltSession->set_ip_filter(std::move(filter));
    });
}

void BitTorrent::SessionBackendImpl::setIPFilter(lt::ip_filter ipFilter)
{
    m_executor->addJob([this, ipFilter = std::move(ipFilter)]() mutable
    {
        m_ltSession->set_ip_filter(std::move(ipFilter));
    });
}

void BitTorrent::SessionBackendImpl::setPeerFilters(lt::ip_filter classFilter, lt::peer_class_type_filter classTypeFilter)
{
    m_executor->addJob([this, classFilter = std::move(classFilter)
            , classTypeFilter = std::move(classTypeFilter)]() mutable
    {
        m_ltSession->set_peer_class_filter(std::move(classFilter));
        m_ltSession->set_peer_class_type_filter(std::move(classTypeFilter));
    });
}

void BitTorrent::SessionBackendImpl::setPortMappingEnabled(const bool enabled)
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

void BitTorrent::SessionBackendImpl::addMappedPorts(QSet<quint16> ports)
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

void BitTorrent::SessionBackendImpl::removeMappedPorts(QSet<quint16> ports)
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

void BitTorrent::SessionBackendImpl::applySettings(lt::settings_pack settingsPack)
{
    m_executor->addJob([this, settingsPack = std::move(settingsPack)]() mutable
    {
        m_ltSession->apply_settings(std::move(settingsPack));
    });
}

void BitTorrent::SessionBackendImpl::postTorrentUpdates(const lt::status_flags_t flags)
{
    m_executor->addJob([this, flags]
    {
        m_ltSession->post_torrent_updates(flags);
    });
}

void BitTorrent::SessionBackendImpl::postSessionStats()
{
    m_executor->addJob([this]
    {
        m_ltSession->post_session_stats();
    });
}

std::shared_ptr<BitTorrent::TorrentBackendImpl> BitTorrent::SessionBackendImpl::getTorrent(const lt::torrent_handle &nativeHandle) const
{
    return m_torrents.value(getInfoHash(nativeHandle).toTorrentID());
}

void BitTorrent::SessionBackendImpl::readAlerts()
{
    getPendingAlerts(m_alerts);

    // Q_ASSERT(m_loadedTorrents.isEmpty());
    // Q_ASSERT(m_receivedAddTorrentAlertsCount == 0);

    // if (!isRestored())
    //     m_loadedTorrents.reserve(MAX_PROCESSING_RESUMEDATA_COUNT);

    for (lt::alert *a : m_alerts)
    {
        try
        {
            handleAlert(a);
        }
        catch (const std::exception &exc)
        {
            qWarning() << "Caught exception in " << Q_FUNC_INFO << ": " << QString::fromStdString(exc.what());
        }
    }

    // if (m_receivedAddTorrentAlertsCount > 0)
    // {
    //     emit addTorrentAlertsReceived(m_receivedAddTorrentAlertsCount);
    //     m_receivedAddTorrentAlertsCount = 0;

    //     if (!m_loadedTorrents.isEmpty())
    //     {
    //         if (isRestored())
    //             m_torrentsQueueChanged = true;

    //         emit torrentsLoaded(m_loadedTorrents);
    //         m_loadedTorrents.clear();
    //     }
    // }

    // // Some torrents may become "finished" after different alerts handling.
    // processPendingFinishedTorrents();
}

void BitTorrent::SessionBackendImpl::handleAlert(lt::alert *alert)
{
    switch (alert->type())
    {
// #ifdef QBT_USES_LIBTORRENT2
//     case lt::file_prio_alert::alert_type:
//         handleFilePrioAlert(static_cast<const lt::file_prio_alert *>(alert));
//         break;
// #endif
//     case lt::file_renamed_alert::alert_type:
//         handleFileRenamedAlert(static_cast<const lt::file_renamed_alert *>(alert));
//         break;
//     case lt::file_rename_failed_alert::alert_type:
//         handleFileRenameFailedAlert(static_cast<const lt::file_rename_failed_alert *>(alert));
//         break;
//     case lt::file_completed_alert::alert_type:
//         handleFileCompletedAlert(static_cast<const lt::file_completed_alert *>(alert));
//         break;
//     case lt::file_error_alert::alert_type:
//         handleFileErrorAlert(static_cast<const lt::file_error_alert *>(alert));
//         break;
//     case lt::torrent_finished_alert::alert_type:
//         handleTorrentFinishedAlert(static_cast<const lt::torrent_finished_alert *>(alert));
//         break;
//     case lt::save_resume_data_alert::alert_type:
//         handleSaveResumeDataAlert(static_cast<lt::save_resume_data_alert *>(alert));
//         break;
//     case lt::save_resume_data_failed_alert::alert_type:
//         handleSaveResumeDataFailedAlert(static_cast<const lt::save_resume_data_failed_alert *>(alert));
//         break;
//     case lt::metadata_received_alert::alert_type:
//         handleMetadataReceivedAlert(static_cast<const lt::metadata_received_alert *>(alert));
//         break;
//     case lt::fastresume_rejected_alert::alert_type:
//         handleFastResumeRejectedAlert(static_cast<const lt::fastresume_rejected_alert *>(alert));
//         break;
//     case lt::torrent_checked_alert::alert_type:
//         handleTorrentCheckedAlert(static_cast<const lt::torrent_checked_alert *>(alert));
//         break;
//     case lt::performance_alert::alert_type:
//         handlePerformanceAlert(static_cast<const lt::performance_alert *>(alert));
//         break;
    case lt::state_update_alert::alert_type:
        handleStateUpdateAlert(static_cast<const lt::state_update_alert *>(alert));
        break;
//     case lt::session_error_alert::alert_type:
//         handleSessionErrorAlert(static_cast<const lt::session_error_alert *>(alert));
//         break;
//     case lt::session_stats_alert::alert_type:
//         handleSessionStatsAlert(static_cast<const lt::session_stats_alert *>(alert));
//         break;
//     case lt::tracker_announce_alert::alert_type:
//     case lt::tracker_error_alert::alert_type:
//     case lt::tracker_reply_alert::alert_type:
//     case lt::tracker_warning_alert::alert_type:
//         handleTrackerAlert(static_cast<const lt::tracker_alert *>(alert));
//         break;
    case lt::add_torrent_alert::alert_type:
        handleAddTorrentAlert(static_cast<const lt::add_torrent_alert *>(alert));
        break;
//     case lt::torrent_removed_alert::alert_type:
//         handleTorrentRemovedAlert(static_cast<const lt::torrent_removed_alert *>(alert));
//         break;
//     case lt::torrent_deleted_alert::alert_type:
//         handleTorrentDeletedAlert(static_cast<const lt::torrent_deleted_alert *>(alert));
//         break;
//     case lt::torrent_delete_failed_alert::alert_type:
//         handleTorrentDeleteFailedAlert(static_cast<const lt::torrent_delete_failed_alert *>(alert));
//         break;
//     case lt::torrent_need_cert_alert::alert_type:
//         handleTorrentNeedCertAlert(static_cast<const lt::torrent_need_cert_alert *>(alert));
//         break;
//     case lt::portmap_error_alert::alert_type:
//         handlePortmapWarningAlert(static_cast<const lt::portmap_error_alert *>(alert));
//         break;
//     case lt::portmap_alert::alert_type:
//         handlePortmapAlert(static_cast<const lt::portmap_alert *>(alert));
//         break;
//     case lt::peer_blocked_alert::alert_type:
//         handlePeerBlockedAlert(static_cast<const lt::peer_blocked_alert *>(alert));
//         break;
//     case lt::peer_ban_alert::alert_type:
//         handlePeerBanAlert(static_cast<const lt::peer_ban_alert *>(alert));
//         break;
//     case lt::url_seed_alert::alert_type:
//         handleUrlSeedAlert(static_cast<const lt::url_seed_alert *>(alert));
//         break;
//     case lt::listen_succeeded_alert::alert_type:
//         handleListenSucceededAlert(static_cast<const lt::listen_succeeded_alert *>(alert));
//         break;
//     case lt::listen_failed_alert::alert_type:
//         handleListenFailedAlert(static_cast<const lt::listen_failed_alert *>(alert));
//         break;
//     case lt::external_ip_alert::alert_type:
//         handleExternalIPAlert(static_cast<const lt::external_ip_alert *>(alert));
//         break;
//     case lt::alerts_dropped_alert::alert_type:
//         handleAlertsDroppedAlert(static_cast<const lt::alerts_dropped_alert *>(alert));
//         break;
//     case lt::storage_moved_alert::alert_type:
//         handleStorageMovedAlert(static_cast<const lt::storage_moved_alert *>(alert));
//         break;
//     case lt::storage_moved_failed_alert::alert_type:
//         handleStorageMovedFailedAlert(static_cast<const lt::storage_moved_failed_alert *>(alert));
//         break;
//     case lt::socks5_alert::alert_type:
//         handleSocks5Alert(static_cast<const lt::socks5_alert *>(alert));
//         break;
//     case lt::i2p_alert::alert_type:
//         handleI2PAlert(static_cast<const lt::i2p_alert *>(alert));
//         break;
// #ifdef QBT_USES_LIBTORRENT2
//     case lt::torrent_conflict_alert::alert_type:
//         handleTorrentConflictAlert(static_cast<const lt::torrent_conflict_alert *>(alert));
//         break;
// #endif
    }
}

void BitTorrent::SessionBackendImpl::handleAddTorrentAlert(const lt::add_torrent_alert *alert)
{
    // ++m_receivedAddTorrentAlertsCount;

    // FIXME: Need to handle torrent reloading!
    Q_ASSERT(!m_addTorrentAlertHandlers.isEmpty());
    if (m_addTorrentAlertHandlers.isEmpty()) [[unlikely]]
        return;

    if (const AddTorrentAlertHandler handleAlert = m_addTorrentAlertHandlers.takeFirst())
        handleAlert(alert);
}

void BitTorrent::SessionBackendImpl::handleStateUpdateAlert(const libtorrent::state_update_alert *alert)
{
    QList<TorrentID> updatedTorrents;
    updatedTorrents.reserve(static_cast<decltype(updatedTorrents)::size_type>(alert->status.size()));

    for (const lt::torrent_status &status : alert->status)
    {
        const std::shared_ptr<BitTorrent::TorrentBackendImpl> torrent = getTorrent(status.handle);
        if (!torrent)
            continue;

        // Since libtorrent alerts are handled asynchronously there can be obsolete
        // "state update" event reached here after torrent was reloaded in libtorrent.
        // Just discard such events.
        if (status.handle != torrent->ltTorrentHandle()) [[unlikely]]
            continue;

        torrent->handleStateUpdated(status);
        updatedTorrents.append(torrent->infoHash().toTorrentID());
    }

    if (!updatedTorrents.isEmpty())
        emit torrentsUpdated(updatedTorrents);
}
