/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2015  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2006  Christophe Dumez <chris@qbittorrent.org>
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

#include "torrentimpl.h"

#include <algorithm>
#include <memory>
#include <type_traits>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

#include <libtorrent/address.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/storage_defs.hpp>
#include <libtorrent/time.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/write_resume_data.hpp>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QUrl>

#include "base/global.h"
#include "base/logger.h"
#include "base/preferences.h"
#include "base/profile.h"
#include "base/utils/fs.h"
#include "base/utils/string.h"
#include "common.h"
#include "downloadpriority.h"
#include "ltqhash.h"
#include "ltunderlyingtype.h"
#include "peeraddress.h"
#include "peerinfo.h"
#include "session.h"
#include "trackerentry.h"

using namespace BitTorrent;

namespace
{
    std::vector<lt::download_priority_t> toLTDownloadPriorities(const QVector<DownloadPriority> &priorities)
    {
        std::vector<lt::download_priority_t> out;
        out.reserve(priorities.size());

        std::transform(priorities.cbegin(), priorities.cend()
                       , std::back_inserter(out), [](const DownloadPriority priority)
        {
            return static_cast<lt::download_priority_t>(
                        static_cast<LTUnderlyingType<lt::download_priority_t>>(priority));
        });
        return out;
    }
}

// TorrentImpl

TorrentImpl::TorrentImpl(Session *session, lt::session *nativeSession
                         , const lt::torrent_handle &nativeHandle
                         , std::shared_ptr<PersistentDataStorage> persistentDataStorage)
    : QObject {session}
    , m_session {session}
    , m_nativeSession {nativeSession}
    , m_nativeHandle {nativeHandle}
    , m_hash {m_nativeHandle.info_hash()}
    , m_persistentDataStorage {persistentDataStorage}
    , m_name {persistentDataStorage.get(), PersistentDataStorage::ItemID::Name}
    , m_savePath {persistentDataStorage.get(), PersistentDataStorage::ItemID::SavePath}
    , m_category {persistentDataStorage.get(), PersistentDataStorage::ItemID::Category}
    , m_tags {persistentDataStorage.get(), PersistentDataStorage::ItemID::Tags}
    , m_ratioLimit {persistentDataStorage.get(), PersistentDataStorage::ItemID::RatioLimit}
    , m_seedingTimeLimit {persistentDataStorage.get(), PersistentDataStorage::ItemID::SeedingTimeLimit}
    , m_hasSeedStatus {persistentDataStorage.get(), PersistentDataStorage::ItemID::HasSeedStatus}
    , m_hasFirstLastPiecePriority {persistentDataStorage.get(), PersistentDataStorage::ItemID::HasFirstLastPiecePriority}
    , m_isStopped {persistentDataStorage.get(), PersistentDataStorage::ItemID::IsStopped}
    , m_operatingMode {persistentDataStorage.get(), PersistentDataStorage::ItemID::OperatingMode}
    , m_contentLayout {persistentDataStorage.get(), PersistentDataStorage::ItemID::ContentLayout}
    , m_useAutoTMM {m_savePath.get().isEmpty()}
    , m_disableDHT {persistentDataStorage->loadData(PersistentDataStorage::ItemID::DisableDHT).toBool()}
    , m_disableLSD {persistentDataStorage->loadData(PersistentDataStorage::ItemID::DisableLSD).toBool()}
    , m_disablePEX {persistentDataStorage->loadData(PersistentDataStorage::ItemID::DisablePEX).toBool()}
    , m_superSeeding {persistentDataStorage->loadData(PersistentDataStorage::ItemID::SuperSeeding).toBool()}
    , m_sequentialDownload {persistentDataStorage->loadData(PersistentDataStorage::ItemID::SequentialDownload).toBool()}
    , m_stopWhenReady {persistentDataStorage->loadData(PersistentDataStorage::ItemID::StopWhenReady).toBool()}
    , m_activeTime {persistentDataStorage->loadData(PersistentDataStorage::ItemID::ActiveTime).toInt()}
    , m_finishedTime {persistentDataStorage->loadData(PersistentDataStorage::ItemID::FinishedTime).toInt()}
    , m_seedingTime {persistentDataStorage->loadData(PersistentDataStorage::ItemID::SeedingTime).toInt()}
    , m_numComplete {persistentDataStorage->loadData(PersistentDataStorage::ItemID::NumComplete).toInt()}
    , m_numIncomplete {persistentDataStorage->loadData(PersistentDataStorage::ItemID::NumIncomplete).toInt()}
    , m_addedTime {QDateTime::fromSecsSinceEpoch(persistentDataStorage->loadData(PersistentDataStorage::ItemID::AddedTime).value<std::time_t>())}
    , m_completedTime {QDateTime::fromSecsSinceEpoch(persistentDataStorage->loadData(PersistentDataStorage::ItemID::CompletedTime).value<std::time_t>())}
    , m_lastSeenComplete {QDateTime::fromSecsSinceEpoch(persistentDataStorage->loadData(PersistentDataStorage::ItemID::LastSeenComplete).value<std::time_t>())}
    , m_lastDownload {QDateTime::fromSecsSinceEpoch(persistentDataStorage->loadData(PersistentDataStorage::ItemID::LastDownload).value<std::time_t>())}
    , m_lastUpload {QDateTime::fromSecsSinceEpoch(persistentDataStorage->loadData(PersistentDataStorage::ItemID::LastUpload).value<std::time_t>())}
    , m_totalDownloaded {persistentDataStorage->loadData(PersistentDataStorage::ItemID::TotalDownloaded).value<std::int64_t>()}
    , m_totalUploaded {persistentDataStorage->loadData(PersistentDataStorage::ItemID::TotalUploaded).value<std::int64_t>()}
    , m_storageLocation {QString::fromStdString(persistentDataStorage->loadData(PersistentDataStorage::ItemID::StorageLocation).value<std::string>())}
{

    if (m_persistentDataStorage->getLTTorrentInfo())
    {
        // Initialize it only if torrent is added with metadata.
        // Otherwise it should be initialized in "Metadata received" handler.
        m_torrentInfo = TorrentInfo {m_nativeHandle.torrent_file()};
    }

    // TODO: Try to avoid updateStatus() here.
    updateStatus();

    if (hasMetadata())
        applyFirstLastPiecePriority(m_hasFirstLastPiecePriority);

    // TODO: Remove the following upgrade code in v.4.4
    // == BEGIN UPGRADE CODE ==
    const QString spath = actualStorageLocation();
    for (int i = 0; i < filesCount(); ++i)
    {
        const QString filepath = filePath(i);
        // Move "unwanted" files back to their original folder
        const QString parentRelPath = Utils::Fs::branchPath(filepath);
        if (QDir(parentRelPath).dirName() == ".unwanted")
        {
            const QString oldName = Utils::Fs::fileName(filepath);
            const QString newRelPath = Utils::Fs::branchPath(parentRelPath);
            if (newRelPath.isEmpty())
                renameFile(i, oldName);
            else
                renameFile(i, QDir(newRelPath).filePath(oldName));

            // Remove .unwanted directory if empty
            qDebug() << "Attempting to remove \".unwanted\" folder at " << QDir(spath + '/' + newRelPath).absoluteFilePath(".unwanted");
            QDir(spath + '/' + newRelPath).rmdir(".unwanted");
        }
    }
    // == END UPGRADE CODE ==
}

TorrentImpl::~TorrentImpl() {}

bool TorrentImpl::isValid() const
{
    return m_nativeHandle.is_valid();
}

InfoHash TorrentImpl::hash() const
{
    return m_hash;
}

QString TorrentImpl::name() const
{
    if (!m_name.get().isEmpty())
        return m_name;

    if (hasMetadata())
        return m_torrentInfo.name();

    return m_hash;
}

QDateTime TorrentImpl::creationDate() const
{
    return m_torrentInfo.creationDate();
}

QString TorrentImpl::creator() const
{
    return m_torrentInfo.creator();
}

QString TorrentImpl::comment() const
{
    return m_torrentInfo.comment();
}

bool TorrentImpl::isPrivate() const
{
    return m_torrentInfo.isPrivate();
}

qlonglong TorrentImpl::totalSize() const
{
    return m_torrentInfo.totalSize();
}

// size without the "don't download" files
qlonglong TorrentImpl::wantedSize() const
{
    return m_wantedSize;
}

qlonglong TorrentImpl::completedSize() const
{
    return m_completedSize;
}

qlonglong TorrentImpl::pieceLength() const
{
    return m_torrentInfo.pieceLength();
}

qlonglong TorrentImpl::wastedSize() const
{
    return (m_failedBytes + m_redundantBytes);
}

QString TorrentImpl::currentTracker() const
{
    return m_currentTracker;
}

QString TorrentImpl::savePath(bool actual) const
{
    if (actual)
        return Utils::Fs::toUniformPath(actualStorageLocation());

    if (m_useAutoTMM)
         return  m_session->categorySavePath(m_category);

    return Utils::Fs::toUniformPath(m_savePath);
}

QString TorrentImpl::rootPath(bool actual) const
{
    if (!hasMetadata())
        return {};

    const QString firstFilePath = filePath(0);
    const int slashIndex = firstFilePath.indexOf('/');
    if (slashIndex >= 0)
        return QDir(savePath(actual)).absoluteFilePath(firstFilePath.left(slashIndex));
    else
        return QDir(savePath(actual)).absoluteFilePath(firstFilePath);
}

QString TorrentImpl::contentPath(const bool actual) const
{
    if (!hasMetadata())
        return {};

    if (filesCount() == 1)
        return QDir(savePath(actual)).absoluteFilePath(filePath(0));

    if (m_torrentInfo.hasRootFolder())
        return rootPath(actual);

    return savePath(actual);
}

bool TorrentImpl::isAutoTMMEnabled() const
{
    return m_useAutoTMM;
}

void TorrentImpl::setAutoTMMEnabled(bool enabled)
{
    if (m_useAutoTMM == enabled) return;

    m_useAutoTMM = enabled;
    m_session->handleTorrentSavingModeChanged(this);

    if (m_useAutoTMM)
        move_impl(m_session->categorySavePath(m_category), MoveStorageMode::Overwrite);
}

QString TorrentImpl::actualStorageLocation() const
{
    return m_storageLocation;
}

void TorrentImpl::setAutoManaged(const bool enable)
{
    if (enable)
        m_nativeHandle.set_flags(lt::torrent_flags::auto_managed);
    else
        m_nativeHandle.unset_flags(lt::torrent_flags::auto_managed);
}

QVector<TrackerEntry> TorrentImpl::trackers() const
{
    const std::vector<lt::announce_entry> nativeTrackers = m_nativeHandle.trackers();

    QVector<TrackerEntry> entries;
    entries.reserve(nativeTrackers.size());

    for (const lt::announce_entry &tracker : nativeTrackers)
        entries << tracker;

    return entries;
}

QHash<QString, TrackerInfo> TorrentImpl::trackerInfos() const
{
    return m_trackerInfos;
}

void TorrentImpl::addTrackers(const QVector<TrackerEntry> &trackers)
{
    QSet<TrackerEntry> currentTrackers;
    for (const lt::announce_entry &entry : m_nativeHandle.trackers())
        currentTrackers << entry;

    QVector<TrackerEntry> newTrackers;
    newTrackers.reserve(trackers.size());

    for (const TrackerEntry &tracker : trackers)
    {
        if (!currentTrackers.contains(tracker))
        {
            m_nativeHandle.add_tracker(tracker.nativeEntry());
            newTrackers << tracker;
        }
    }

    if (!newTrackers.isEmpty())
        m_session->handleTorrentTrackersAdded(this, newTrackers);
}

void TorrentImpl::replaceTrackers(const QVector<TrackerEntry> &trackers)
{
    QVector<TrackerEntry> currentTrackers = this->trackers();

    QVector<TrackerEntry> newTrackers;
    newTrackers.reserve(trackers.size());

    std::vector<lt::announce_entry> nativeTrackers;
    nativeTrackers.reserve(trackers.size());

    for (const TrackerEntry &tracker : trackers)
    {
        nativeTrackers.emplace_back(tracker.nativeEntry());

        if (!currentTrackers.removeOne(tracker))
            newTrackers << tracker;
    }

    m_nativeHandle.replace_trackers(nativeTrackers);

    if (newTrackers.isEmpty() && currentTrackers.isEmpty())
    {
        // when existing tracker reorders
        m_session->handleTorrentTrackersChanged(this);
    }
    else
    {
        if (!currentTrackers.isEmpty())
            m_session->handleTorrentTrackersRemoved(this, currentTrackers);

        if (!newTrackers.isEmpty())
            m_session->handleTorrentTrackersAdded(this, newTrackers);

        // Clear the peer list if it's a private torrent since
        // we do not want to keep connecting with peers from old tracker.
        if (isPrivate())
            clearPeers();
    }
}

QVector<QUrl> TorrentImpl::urlSeeds() const
{
    const std::set<std::string> currentSeeds = m_nativeHandle.url_seeds();

    QVector<QUrl> urlSeeds;
    urlSeeds.reserve(currentSeeds.size());

    for (const std::string &urlSeed : currentSeeds)
        urlSeeds.append(QUrl(urlSeed.c_str()));

    return urlSeeds;
}

void TorrentImpl::addUrlSeeds(const QVector<QUrl> &urlSeeds)
{
    const std::set<std::string> currentSeeds = m_nativeHandle.url_seeds();

    QVector<QUrl> addedUrlSeeds;
    addedUrlSeeds.reserve(urlSeeds.size());

    for (const QUrl &url : urlSeeds)
    {
        const std::string nativeUrl = url.toString().toStdString();
        if (currentSeeds.find(nativeUrl) == currentSeeds.end())
        {
            m_nativeHandle.add_url_seed(nativeUrl);
            addedUrlSeeds << url;
        }
    }

    if (!addedUrlSeeds.isEmpty())
        m_session->handleTorrentUrlSeedsAdded(this, addedUrlSeeds);
}

void TorrentImpl::removeUrlSeeds(const QVector<QUrl> &urlSeeds)
{
    const std::set<std::string> currentSeeds = m_nativeHandle.url_seeds();

    QVector<QUrl> removedUrlSeeds;
    removedUrlSeeds.reserve(urlSeeds.size());

    for (const QUrl &url : urlSeeds)
    {
        const std::string nativeUrl = url.toString().toStdString();
        if (currentSeeds.find(nativeUrl) != currentSeeds.end())
        {
            m_nativeHandle.remove_url_seed(nativeUrl);
            removedUrlSeeds << url;
        }
    }

    if (!removedUrlSeeds.isEmpty())
        m_session->handleTorrentUrlSeedsRemoved(this, removedUrlSeeds);
}

void TorrentImpl::clearPeers()
{
    m_nativeHandle.clear_peers();
}

bool TorrentImpl::connectPeer(const PeerAddress &peerAddress)
{
    lt::error_code ec;
    const lt::address addr = lt::make_address(peerAddress.ip.toString().toStdString(), ec);
    if (ec) return false;

    const lt::tcp::endpoint endpoint(addr, peerAddress.port);
    try
    {
        m_nativeHandle.connect_peer(endpoint);
    }
    catch (const lt::system_error &err)
    {
        LogMsg(tr("Failed to add peer \"%1\" to torrent \"%2\". Reason: %3")
            .arg(peerAddress.toString(), name(), QString::fromLocal8Bit(err.what())), Log::WARNING);
        return false;
    }

    LogMsg(tr("Peer \"%1\" is added to torrent \"%2\"").arg(peerAddress.toString(), name()));
    return true;
}

bool TorrentImpl::needSaveResumeData() const
{
//    if (m_isStopped && !m_isAutoManaged)
//        return false;
    // TODO: Remove the following log.
    if (m_isStopped)
        LogMsg("DEBUG: Stopped torrent needs to save resume data.");
    return m_nativeHandle.need_save_resume_data();
}

std::shared_ptr<PersistentDataStorage> TorrentImpl::persistentDataStorage() const
{
    return m_persistentDataStorage;
}

int TorrentImpl::filesCount() const
{
    return m_torrentInfo.filesCount();
}

int TorrentImpl::piecesCount() const
{
    return m_torrentInfo.piecesCount();
}

int TorrentImpl::piecesHave() const
{
    return m_existingPiecesCount;
}

qreal TorrentImpl::progress() const
{
    if (isChecking())
        return m_progress;

    if (m_wantedSize == 0)
        return 0.;

    if (m_completedSize == m_wantedSize)
        return 1.;

    const qreal progress = static_cast<qreal>(m_completedSize) / m_wantedSize;
    Q_ASSERT((progress >= 0.f) && (progress <= 1.f));
    return progress;
}

QString TorrentImpl::category() const
{
    return m_category;
}

bool TorrentImpl::belongsToCategory(const QString &category) const
{
    if (m_category.get().isEmpty()) return category.isEmpty();
    if (!Session::isValidCategoryName(category)) return false;

    if (m_category == category) return true;

    if (m_session->isSubcategoriesEnabled() && m_category.get().startsWith(category + '/'))
        return true;

    return false;
}

QSet<QString> TorrentImpl::tags() const
{
    return m_tags;
}

bool TorrentImpl::hasTag(const QString &tag) const
{
    return m_tags.get().contains(tag);
}

bool TorrentImpl::addTag(const QString &tag)
{
    if (!Session::isValidTag(tag))
        return false;

    if (!hasTag(tag))
    {
        if (!m_session->hasTag(tag))
        {
            if (!m_session->addTag(tag))
                return false;
        }

        m_tags = QSet<QString>(m_tags) << tag;
        m_session->handleTorrentTagAdded(this, tag);
        return true;
    }
    return false;
}

bool TorrentImpl::removeTag(const QString &tag)
{
    QSet<QString> tags = m_tags;
    if (tags.remove(tag))
    {
        m_tags = tags;
        m_session->handleTorrentTagRemoved(this, tag);
        return true;
    }
    return false;
}

void TorrentImpl::removeAllTags()
{
    for (const QString &tag : asConst(tags()))
        removeTag(tag);
}

QDateTime TorrentImpl::addedTime() const
{
    return m_addedTime;
}

qreal TorrentImpl::ratioLimit() const
{
    return m_ratioLimit;
}

int TorrentImpl::seedingTimeLimit() const
{
    return m_seedingTimeLimit;
}

QString TorrentImpl::filePath(int index) const
{
    return m_torrentInfo.filePath(index);
}

QString TorrentImpl::fileName(int index) const
{
    if (!hasMetadata()) return {};
    return Utils::Fs::fileName(filePath(index));
}

qlonglong TorrentImpl::fileSize(int index) const
{
    return m_torrentInfo.fileSize(index);
}

// Return a list of absolute paths corresponding
// to all files in a torrent
QStringList TorrentImpl::absoluteFilePaths() const
{
    if (!hasMetadata()) return {};

    const QDir saveDir(savePath(true));
    QStringList res;
    for (int i = 0; i < filesCount(); ++i)
        res << Utils::Fs::expandPathAbs(saveDir.absoluteFilePath(filePath(i)));
    return res;
}

QVector<DownloadPriority> TorrentImpl::filePriorities() const
{
    const std::vector<lt::download_priority_t> fp = m_nativeHandle.get_file_priorities();

    QVector<DownloadPriority> ret;
    std::transform(fp.cbegin(), fp.cend(), std::back_inserter(ret), [](lt::download_priority_t priority)
    {
        return static_cast<DownloadPriority>(static_cast<LTUnderlyingType<lt::download_priority_t>>(priority));
    });
    return ret;
}

TorrentInfo TorrentImpl::info() const
{
    return m_torrentInfo;
}

bool TorrentImpl::isStopped() const
{
    return m_isStopped;
}

bool TorrentImpl::isQueued() const
{
    // Torrent is Queued if it isn't in Stopped state but paused internally
    return (!isStopped() && m_isAutoManaged && m_isPaused);
}

bool TorrentImpl::isChecking() const
{
    return ((m_nativeState == lt::torrent_status::checking_files)
            || (m_nativeState == lt::torrent_status::checking_resume_data));
}

bool TorrentImpl::isDownloading() const
{
    return m_state == TorrentState::Downloading
            || m_state == TorrentState::DownloadingMetadata
            || m_state == TorrentState::StalledDownloading
            || m_state == TorrentState::CheckingDownloading
            || m_state == TorrentState::PausedDownloading
            || m_state == TorrentState::QueuedDownloading
            || m_state == TorrentState::ForcedDownloading;
}

bool TorrentImpl::isUploading() const
{
    return m_state == TorrentState::Uploading
            || m_state == TorrentState::StalledUploading
            || m_state == TorrentState::CheckingUploading
            || m_state == TorrentState::QueuedUploading
            || m_state == TorrentState::ForcedUploading;
}

bool TorrentImpl::isCompleted() const
{
    return m_state == TorrentState::Uploading
            || m_state == TorrentState::StalledUploading
            || m_state == TorrentState::CheckingUploading
            || m_state == TorrentState::PausedUploading
            || m_state == TorrentState::QueuedUploading
            || m_state == TorrentState::ForcedUploading;
}

bool TorrentImpl::isActive() const
{
    if (m_state == TorrentState::StalledDownloading)
        return (uploadPayloadRate() > 0);

    return m_state == TorrentState::DownloadingMetadata
            || m_state == TorrentState::Downloading
            || m_state == TorrentState::ForcedDownloading
            || m_state == TorrentState::Uploading
            || m_state == TorrentState::ForcedUploading
            || m_state == TorrentState::Moving;
}

bool TorrentImpl::isInactive() const
{
    return !isActive();
}

bool TorrentImpl::isErrored() const
{
    return m_state == TorrentState::MissingFiles
            || m_state == TorrentState::Error;
}

bool TorrentImpl::isSeed() const
{
    return ((m_nativeState == lt::torrent_status::finished)
            || (m_nativeState == lt::torrent_status::seeding));
}

bool TorrentImpl::isForced() const
{
    return (!isStopped() && (m_operatingMode == TorrentOperatingMode::Forced));
}

bool TorrentImpl::isSequentialDownload() const
{
    return m_sequentialDownload;
}

bool TorrentImpl::hasFirstLastPiecePriority() const
{
    return m_hasFirstLastPiecePriority;
}

TorrentState TorrentImpl::state() const
{
    return m_state;
}

void TorrentImpl::updateState()
{
    if (m_nativeState == lt::torrent_status::checking_resume_data)
    {
        m_state = TorrentState::CheckingResumeData;
    }
    else if (isMoveInProgress())
    {
        m_state = TorrentState::Moving;
    }
    else if (hasMissingFiles())
    {
        m_state = TorrentState::MissingFiles;
    }
    else if (hasError())
    {
        m_state = TorrentState::Error;
    }
    else if (!hasMetadata())
    {
        if (isStopped())
            m_state = TorrentState::PausedDownloading;
        else if (m_session->isQueueingSystemEnabled() && isQueued())
            m_state = TorrentState::QueuedDownloading;
        else
            m_state = TorrentState::DownloadingMetadata;
    }
    else if ((m_nativeState == lt::torrent_status::checking_files)
             && (!isStopped() || m_isAutoManaged || !m_isPaused))
    {
        // If the torrent is not just in the "checking" state, but is being actually checked
        m_state = m_hasSeedStatus ? TorrentState::CheckingUploading : TorrentState::CheckingDownloading;
    }
    else if (isSeed())
    {
        if (isStopped())
            m_state = TorrentState::PausedUploading;
        else if (m_session->isQueueingSystemEnabled() && isQueued())
            m_state = TorrentState::QueuedUploading;
        else if (isForced())
            m_state = TorrentState::ForcedUploading;
        else if (m_uploadPayloadRate > 0)
            m_state = TorrentState::Uploading;
        else
            m_state = TorrentState::StalledUploading;
    }
    else
    {
        if (isStopped())
            m_state = TorrentState::PausedDownloading;
        else if (m_session->isQueueingSystemEnabled() && isQueued())
            m_state = TorrentState::QueuedDownloading;
        else if (isForced())
            m_state = TorrentState::ForcedDownloading;
        else if (m_downloadPayloadRate > 0)
            m_state = TorrentState::Downloading;
        else
            m_state = TorrentState::StalledDownloading;
    }
}

bool TorrentImpl::hasMetadata() const
{
    return m_torrentInfo.isValid();
}

bool TorrentImpl::hasMissingFiles() const
{
    return m_hasMissingFiles;
}

bool TorrentImpl::hasError() const
{
    return static_cast<bool>(m_error);
}

bool TorrentImpl::hasFilteredPieces() const
{
    const std::vector<lt::download_priority_t> pp = m_nativeHandle.get_piece_priorities();
    return std::any_of(pp.cbegin(), pp.cend(), [](const lt::download_priority_t priority)
    {
        return (priority == lt::download_priority_t {0});
    });
}

int TorrentImpl::queuePosition() const
{
    return m_queuePosition;
}

QString TorrentImpl::error() const
{
    return QString::fromStdString(m_error.message());
}

qlonglong TorrentImpl::totalDownload() const
{
    return m_totalDownloaded;
}

qlonglong TorrentImpl::totalUpload() const
{
    return m_totalUploaded;
}

qlonglong TorrentImpl::activeTime() const
{
    return m_activeTime;
}

qlonglong TorrentImpl::finishedTime() const
{
    return m_finishedTime;
}

qlonglong TorrentImpl::seedingTime() const
{
    return m_seedingTime;
}

qlonglong TorrentImpl::eta() const
{
    if (isStopped()) return MAX_ETA;

    const SpeedSampleAvg speedAverage = m_speedMonitor.average();

    if (isSeed())
    {
        const qreal maxRatioValue = maxRatio();
        const int maxSeedingTimeValue = maxSeedingTime();
        if ((maxRatioValue < 0) && (maxSeedingTimeValue < 0)) return MAX_ETA;

        qlonglong ratioEta = MAX_ETA;

        if ((speedAverage.upload > 0) && (maxRatioValue >= 0))
        {

            qlonglong realDL = totalDownload();
            if (realDL <= 0)
                realDL = wantedSize();

            ratioEta = ((realDL * maxRatioValue) - totalUpload()) / speedAverage.upload;
        }

        qlonglong seedingTimeEta = MAX_ETA;

        if (maxSeedingTimeValue >= 0)
        {
            seedingTimeEta = (maxSeedingTimeValue * 60) - seedingTime();
            if (seedingTimeEta < 0)
                seedingTimeEta = 0;
        }

        return qMin(ratioEta, seedingTimeEta);
    }

    if (!speedAverage.download) return MAX_ETA;

    return (wantedSize() - completedSize()) / speedAverage.download;
}

QVector<qreal> TorrentImpl::filesProgress() const
{
    if (!hasMetadata())
        return {};

    std::vector<int64_t> fp;
    m_nativeHandle.file_progress(fp, lt::torrent_handle::piece_granularity);

    const int count = static_cast<int>(fp.size());
    QVector<qreal> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        const qlonglong size = fileSize(i);
        if ((size <= 0) || (fp[i] == size))
            result << 1;
        else
            result << (fp[i] / static_cast<qreal>(size));
    }

    return result;
}

int TorrentImpl::seedsCount() const
{
    return m_connectedSeedsCount;
}

int TorrentImpl::peersCount() const
{
    return m_connectedPeersCount;
}

int TorrentImpl::leechsCount() const
{
    return (m_connectedPeersCount - m_connectedSeedsCount);
}

int TorrentImpl::totalSeedsCount() const
{
    return (m_numComplete > 0) ? m_numComplete : m_seedsCount;
}

int TorrentImpl::totalPeersCount() const
{
    const int peers = m_numComplete + m_numIncomplete;
    return (peers > 0) ? peers : m_peersCount;
}

int TorrentImpl::totalLeechersCount() const
{
    return (m_numIncomplete > 0) ? m_numIncomplete : (m_peersCount - m_seedsCount);
}

int TorrentImpl::completeCount() const
{
    // additional info: https://github.com/qbittorrent/qBittorrent/pull/5300#issuecomment-267783646
    return m_numComplete;
}

int TorrentImpl::incompleteCount() const
{
    // additional info: https://github.com/qbittorrent/qBittorrent/pull/5300#issuecomment-267783646
    return m_numIncomplete;
}

QDateTime TorrentImpl::lastSeenComplete() const
{
    return m_lastSeenComplete;
}

QDateTime TorrentImpl::completedTime() const
{
    return m_completedTime;
}

qlonglong TorrentImpl::timeSinceUpload() const
{
    if (!m_lastUpload.isValid())
        return -1;
    return m_lastUpload.secsTo(QDateTime::currentDateTime());
}

qlonglong TorrentImpl::timeSinceDownload() const
{
    if (!m_lastDownload.isValid())
        return -1;
    return m_lastDownload.secsTo(QDateTime::currentDateTime());
}

qlonglong TorrentImpl::timeSinceActivity() const
{
    const qlonglong upTime = timeSinceUpload();
    const qlonglong downTime = timeSinceDownload();
    return ((upTime < 0) != (downTime < 0))
        ? std::max(upTime, downTime)
        : std::min(upTime, downTime);
}

int TorrentImpl::downloadLimit() const
{
    return m_nativeHandle.download_limit();
}

int TorrentImpl::uploadLimit() const
{
    return m_nativeHandle.upload_limit();
}

bool TorrentImpl::superSeeding() const
{
    return m_superSeeding;
}

bool TorrentImpl::isDHTDisabled() const
{
    return m_disableDHT;
}

bool TorrentImpl::isPEXDisabled() const
{
    return m_disablePEX;
}

bool TorrentImpl::isLSDDisabled() const
{
    return m_disableLSD;
}

QVector<PeerInfo> TorrentImpl::peers() const
{
    std::vector<lt::peer_info> nativePeers;
    m_nativeHandle.get_peer_info(nativePeers);

    QVector<PeerInfo> peers;
    peers.reserve(nativePeers.size());
    for (const lt::peer_info &peer : nativePeers)
        peers << PeerInfo(this, peer);
    return peers;
}

QBitArray TorrentImpl::pieces() const
{
    return m_pieces;
}

QBitArray TorrentImpl::downloadingPieces() const
{
    QBitArray result(piecesCount());

    std::vector<lt::partial_piece_info> queue;
    m_nativeHandle.get_download_queue(queue);

    for (const lt::partial_piece_info &info : queue)
        result.setBit(static_cast<LTUnderlyingType<lt::piece_index_t>>(info.piece_index));

    return result;
}

QVector<int> TorrentImpl::pieceAvailability() const
{
    std::vector<int> avail;
    m_nativeHandle.piece_availability(avail);

    return Vector::fromStdVector(avail);
}

qreal TorrentImpl::distributedCopies() const
{
    return m_distributedCopies;
}

qreal TorrentImpl::maxRatio() const
{
    if (m_ratioLimit == USE_GLOBAL_RATIO)
        return m_session->globalMaxRatio();

    return m_ratioLimit;
}

int TorrentImpl::maxSeedingTime() const
{
    if (m_seedingTimeLimit == USE_GLOBAL_SEEDING_TIME)
        return m_session->globalMaxSeedingMinutes();

    return m_seedingTimeLimit;
}

qreal TorrentImpl::realRatio() const
{
    const int64_t upload = m_totalUploaded;
    // special case for a seeder who lost its stats, also assume nobody will import a 99% done torrent
    const int64_t download = (m_totalDownloaded < (m_downloadedSize * 0.01))
        ? m_downloadedSize
        : m_totalDownloaded;

    if (download == 0)
        return (upload == 0) ? 0 : MAX_RATIO;

    const qreal ratio = upload / static_cast<qreal>(download);
    Q_ASSERT(ratio >= 0);
    return (ratio > MAX_RATIO) ? MAX_RATIO : ratio;
}

int TorrentImpl::uploadPayloadRate() const
{
    return m_uploadPayloadRate;
}

int TorrentImpl::downloadPayloadRate() const
{
    return m_downloadPayloadRate;
}

qlonglong TorrentImpl::totalPayloadUpload() const
{
    return m_totalPayloadUpload;
}

qlonglong TorrentImpl::totalPayloadDownload() const
{
    return m_totalPayloadDownload;
}

int TorrentImpl::connectionsCount() const
{
    return m_connectionsCount;
}

int TorrentImpl::connectionsLimit() const
{
    return m_connectionsLimit;
}

qlonglong TorrentImpl::nextAnnounce() const
{
    return m_nextAnnounce;
}

void TorrentImpl::setName(const QString &name)
{
    if (m_name != name)
    {
        m_name = name;
        m_session->handleTorrentNameChanged(this);
    }
}

bool TorrentImpl::setCategory(const QString &category)
{
    if (m_category != category)
    {
        if (!category.isEmpty() && !m_session->categories().contains(category))
            return false;

        const QString oldCategory = m_category;
        m_category = category;
        m_session->handleTorrentCategoryChanged(this, oldCategory);

        if (m_useAutoTMM)
        {
            if (!m_session->isDisableAutoTMMWhenCategoryChanged())
                move_impl(m_session->categorySavePath(m_category), MoveStorageMode::Overwrite);
            else
                setAutoTMMEnabled(false);
        }
    }

    return true;
}

void TorrentImpl::move(QString path)
{
    if (m_useAutoTMM)
    {
        m_useAutoTMM = false;
        m_session->handleTorrentSavingModeChanged(this);
    }

    path = Utils::Fs::toUniformPath(path.trimmed());
    if (path.isEmpty())
        path = m_session->defaultSavePath();
    if (!path.endsWith('/'))
        path += '/';

    move_impl(path, MoveStorageMode::KeepExistingFiles);
}

void TorrentImpl::move_impl(QString path, const MoveStorageMode mode)
{
    if (path == savePath()) return;
    path = Utils::Fs::toNativePath(path);

    if (!useTempPath())
    {
        moveStorage(path, mode);
    }
    else
    {
        m_savePath = path;
        m_session->handleTorrentSavePathChanged(this);
    }
}

void TorrentImpl::forceReannounce(int index)
{
    m_nativeHandle.force_reannounce(0, index);
}

void TorrentImpl::forceDHTAnnounce()
{
    m_nativeHandle.force_dht_announce();
}

void TorrentImpl::forceRecheck()
{
    if (!hasMetadata()) return;

    m_nativeHandle.force_recheck();
    m_hasMissingFiles = false;
    m_unchecked = false;

    if (isStopped())
    {
        // When "force recheck" is applied on paused torrent, we temporarily resume it
        // (really we just allow libtorrent to resume it by enabling auto management for it).
        m_nativeHandle.set_flags(lt::torrent_flags::stop_when_ready | lt::torrent_flags::auto_managed);
    }
}

void TorrentImpl::setSequentialDownload(const bool enable)
{
    if (isSequentialDownload() == enable)
        return;

    m_sequentialDownload = enable;
    applyFlag(lt::torrent_flags::sequential_download, m_sequentialDownload);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::SequentialDownload, m_sequentialDownload);
}

void TorrentImpl::setFirstLastPiecePriority(const bool enabled)
{
    if (m_hasFirstLastPiecePriority == enabled)
        return;

    m_hasFirstLastPiecePriority = enabled;
    if (hasMetadata())
        applyFirstLastPiecePriority(enabled);

    LogMsg(tr("Download first and last piece first: %1, torrent: '%2'")
        .arg((enabled ? tr("On") : tr("Off")), name()));
}

void TorrentImpl::applyFirstLastPiecePriority(const bool enabled, const QVector<DownloadPriority> &updatedFilePrio)
{
    Q_ASSERT(hasMetadata());

    // Download first and last pieces first for every file in the torrent

    const std::vector<lt::download_priority_t> filePriorities = !updatedFilePrio.isEmpty() ? toLTDownloadPriorities(updatedFilePrio)
                                                                           : nativeHandle().get_file_priorities();
    std::vector<lt::download_priority_t> piecePriorities = nativeHandle().get_piece_priorities();

    // Updating file priorities is an async operation in libtorrent, when we just updated it and immediately query it
    // we might get the old/wrong values, so we rely on `updatedFilePrio` in this case.
    for (int index = 0; index < static_cast<int>(filePriorities.size()); ++index)
    {
        const lt::download_priority_t filePrio = filePriorities[index];
        if (filePrio <= lt::download_priority_t {0})
            continue;

        // Determine the priority to set
        const lt::download_priority_t newPrio = enabled ? lt::download_priority_t {7} : filePrio;
        const TorrentInfo::PieceRange extremities = info().filePieces(index);

        // worst case: AVI index = 1% of total file size (at the end of the file)
        const int nNumPieces = std::ceil(fileSize(index) * 0.01 / pieceLength());
        for (int i = 0; i < nNumPieces; ++i)
        {
            piecePriorities[extremities.first() + i] = newPrio;
            piecePriorities[extremities.last() - i] = newPrio;
        }
    }

    m_nativeHandle.prioritize_pieces(piecePriorities);
}

void TorrentImpl::fileSearchFinished(const QString &savePath, const QStringList &fileNames)
{
    endReceivedMetadataHandling(savePath, fileNames);
}

void TorrentImpl::endReceivedMetadataHandling(const QString &savePath, const QStringList &fileNames)
{
    m_storageLocation = Utils::Fs::toNativePath(savePath);

    FileMapType renamedFiles;
    for (int i = 0; i < fileNames.size(); ++i)
        renamedFiles[lt::file_index_t {i}] = fileNames[i].toStdString();
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::RenamedFiles
                                       , QVariant::fromValue(renamedFiles));

    m_persistentDataStorage->setLTTorrentInfo(m_nativeHandle.torrent_file());

    reload();

    // If first/last piece priority was specified when adding this torrent,
    // we should apply it now that we have metadata:
    if (m_hasFirstLastPiecePriority)
        applyFirstLastPiecePriority(true);

    m_maintenanceJob = MaintenanceJob::None;
    updateStatus();

    m_session->handleTorrentMetadataReceived(this);
}

void TorrentImpl::reload()
{
    const auto queuePos = m_nativeHandle.queue_position();

    m_nativeSession->remove_torrent(m_nativeHandle, lt::session::delete_partfile);
    m_nativeHandle = m_nativeSession->add_torrent(persistentDataStorage()->getLTAddTorrentParams());
    m_nativeHandle.queue_position_set(queuePos);

    m_torrentInfo = TorrentInfo {m_nativeHandle.torrent_file()};
}

void TorrentImpl::applyFlag(const lt::torrent_flags_t flag, bool value)
{
    m_nativeHandle.set_flags((value ? lt::torrent_flags::all : lt::torrent_flags_t {}), flag);
}

void TorrentImpl::pause()
{
    if (!m_isStopped)
    {
        m_isStopped = true;
        m_session->handleTorrentPaused(this);
    }

    if (m_maintenanceJob == MaintenanceJob::None)
    {
        setAutoManaged(false);
        m_nativeHandle.pause();

        m_speedMonitor.reset();
    }
}

void TorrentImpl::resume(const TorrentOperatingMode mode)
{
    if (hasError())
        m_nativeHandle.clear_error();

    m_operatingMode = mode;

    if (m_hasMissingFiles)
    {
        m_hasMissingFiles = false;
        m_isStopped = false;
        reload();
        updateStatus();
        return;
    }

    if (m_isStopped)
    {
        // Torrent may have been temporarily resumed to perform checking files
        // so we have to ensure it will not pause after checking is done.
        m_nativeHandle.unset_flags(lt::torrent_flags::stop_when_ready);

        m_isStopped = false;
        m_session->handleTorrentResumed(this);
    }

    if (m_maintenanceJob == MaintenanceJob::None)
    {
        setAutoManaged(m_operatingMode == TorrentOperatingMode::AutoManaged);
        if (m_operatingMode == TorrentOperatingMode::Forced)
            m_nativeHandle.resume();
    }
}

void TorrentImpl::moveStorage(const QString &newPath, const MoveStorageMode mode)
{
    if (m_session->addMoveTorrentStorageJob(this, newPath, mode))
    {
        m_storageIsMoving = true;
        updateStatus();
    }
}

void TorrentImpl::renameFile(const int index, const QString &path)
{
    const QString oldPath = filePath(index);
    m_oldPath[lt::file_index_t {index}].push_back(oldPath);
    ++m_renameCount;
    m_nativeHandle.rename_file(lt::file_index_t {index}, Utils::Fs::toNativePath(path).toStdString());
}

void TorrentImpl::handleMoveStorageJobFinished(const bool hasOutstandingJob)
{
    m_storageIsMoving = hasOutstandingJob;

    updateStatus();
    const QString newPath = m_storageLocation;
    if (!useTempPath() && (newPath != m_savePath))
    {
        m_savePath = newPath;
        m_session->handleTorrentSavePathChanged(this);
    }

    if (!m_storageIsMoving)
    {
        if (m_hasMissingFiles)
        {
            // it can be moved to the proper location
            m_hasMissingFiles = false;
            reload();
            updateStatus();
        }

        while ((m_renameCount == 0) && !m_moveFinishedTriggers.isEmpty())
            m_moveFinishedTriggers.takeFirst()();
    }
}

void TorrentImpl::handleTrackerReplyAlert(const lt::tracker_reply_alert *p)
{
    const QString trackerUrl(p->tracker_url());
    qDebug("Received a tracker reply from %s (Num_peers = %d)", qUtf8Printable(trackerUrl), p->num_peers);
    // Connection was successful now. Remove possible old errors
    m_trackerInfos[trackerUrl] = {{}, p->num_peers};

    m_session->handleTorrentTrackerReply(this, trackerUrl);
}

void TorrentImpl::handleTrackerWarningAlert(const lt::tracker_warning_alert *p)
{
    const QString trackerUrl = p->tracker_url();
    const QString message = p->warning_message();

    // Connection was successful now but there is a warning message
    m_trackerInfos[trackerUrl].lastMessage = message; // Store warning message

    m_session->handleTorrentTrackerWarning(this, trackerUrl);
}

void TorrentImpl::handleTrackerErrorAlert(const lt::tracker_error_alert *p)
{
    const QString trackerUrl = p->tracker_url();
    const QString message = p->error_message();

    m_trackerInfos[trackerUrl].lastMessage = message;

    // Starting with libtorrent 1.2.x each tracker has multiple local endpoints from which
    // an announce is attempted. Some endpoints might succeed while others might fail.
    // Emit the signal only if all endpoints have failed.
    const QVector<TrackerEntry> trackerList = trackers();
    const auto iter = std::find_if(trackerList.cbegin(), trackerList.cend(), [&trackerUrl](const TrackerEntry &entry)
    {
        return (entry.url() == trackerUrl);
    });
    if ((iter != trackerList.cend()) && (iter->status() == TrackerEntry::NotWorking))
        m_session->handleTorrentTrackerError(this, trackerUrl);
}

void TorrentImpl::handleTorrentCheckedAlert(const lt::torrent_checked_alert *p)
{
    Q_UNUSED(p);
    qDebug("\"%s\" have just finished checking.", qUtf8Printable(name()));


    if (!hasMetadata())
    {
        // The torrent is checked due to metadata received, but we should not process
        // this event until the torrent is reloaded using the received metadata.
        return;
    }

    if (m_fastresumeDataRejected && !m_hasMissingFiles)
        m_fastresumeDataRejected = false;

    updateStatus();

    if (!m_hasMissingFiles)
    {
        if ((progress() < 1.0) && (wantedSize() > 0))
            m_hasSeedStatus = false;
        else if (progress() == 1.0)
            m_hasSeedStatus = true;

        adjustActualSavePath();
        manageIncompleteFiles();
    }

    m_session->handleTorrentChecked(this);
}

void TorrentImpl::handleTorrentFinishedAlert(const lt::torrent_finished_alert *p)
{
    Q_UNUSED(p);
    qDebug("Got a torrent finished alert for \"%s\"", qUtf8Printable(name()));
    qDebug("Torrent has seed status: %s", m_hasSeedStatus ? "yes" : "no");
    m_hasMissingFiles = false;
    if (m_hasSeedStatus) return;

    updateStatus();
    m_hasSeedStatus = true;

    adjustActualSavePath();
    manageIncompleteFiles();

    const bool recheckTorrentsOnCompletion = Preferences::instance()->recheckTorrentsOnCompletion();
    if (isMoveInProgress() || (m_renameCount > 0))
    {
        if (recheckTorrentsOnCompletion)
            m_moveFinishedTriggers.append([this]() { forceRecheck(); });
        m_moveFinishedTriggers.append([this]() { m_session->handleTorrentFinished(this); });
    }
    else
    {
        if (recheckTorrentsOnCompletion && m_unchecked)
            forceRecheck();
        m_session->handleTorrentFinished(this);
    }
}

void TorrentImpl::handleTorrentPausedAlert(const lt::torrent_paused_alert *p)
{
    Q_UNUSED(p);
}

void TorrentImpl::handleTorrentResumedAlert(const lt::torrent_resumed_alert *p)
{
    Q_UNUSED(p);
}

void TorrentImpl::updateResumeData(const lt::add_torrent_params &params)
{
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::DisableDHT
                                       , static_cast<bool>(params.flags & lt::torrent_flags::disable_dht));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::DisableLSD
                                       , static_cast<bool>(params.flags & lt::torrent_flags::disable_lsd));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::DisablePEX
                                       , static_cast<bool>(params.flags & lt::torrent_flags::disable_pex));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::SuperSeeding
                                       , static_cast<bool>(params.flags & lt::torrent_flags::super_seeding));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::SequentialDownload
                                       , static_cast<bool>(params.flags & lt::torrent_flags::sequential_download));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::StopWhenReady
                                       , static_cast<bool>(params.flags & lt::torrent_flags::stop_when_ready));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::SeedMode
                                       , static_cast<bool>(params.flags & lt::torrent_flags::seed_mode));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::UploadMode
                                       , static_cast<bool>(params.flags & lt::torrent_flags::upload_mode));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::ShareMode
                                       , static_cast<bool>(params.flags & lt::torrent_flags::share_mode));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::ApplyIpFilter
                                       , static_cast<bool>(params.flags & lt::torrent_flags::apply_ip_filter));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::StorageMode
                                       , QVariant::fromValue(params.storage_mode));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::NumDownloaded
                                       , params.num_downloaded);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::UploadRateLimit
                                       , params.upload_limit);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::DownloadRateLimit
                                       , params.download_limit);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::MaxConnections
                                       , params.max_connections);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::MaxUploads
                                       , params.max_uploads);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::ActiveTime
                                       , params.active_time);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::FinishedTime
                                       , params.finished_time);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::SeedingTime
                                       , params.seeding_time);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::NumComplete
                                       , params.num_complete);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::NumIncomplete
                                       , params.num_incomplete);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::TotalDownloaded
                                       , QVariant::fromValue(params.total_downloaded));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::TotalUploaded
                                       , QVariant::fromValue(params.total_uploaded));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::AddedTime
                                       , QVariant::fromValue(params.added_time));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::LastSeenComplete
                                       , QVariant::fromValue(params.last_seen_complete));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::LastDownload
                                       , QVariant::fromValue(params.last_download));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::LastUpload
                                       , QVariant::fromValue(params.last_upload));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::CompletedTime
                                       , QVariant::fromValue(params.completed_time));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::StorageLocation
                                       , QVariant::fromValue(params.save_path));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::MerkleTree
                                       , QVariant::fromValue<Sha1HashVectorType>(params.merkle_tree));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::URLSeeds
                                       , QVariant::fromValue<StringVectorType>(params.url_seeds));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::HTTPSeeds
                                       , QVariant::fromValue<StringVectorType>(params.http_seeds));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::Trackers
                                       , QVariant::fromValue<TrackersType>({params.trackers, params.tracker_tiers}));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::RenamedFiles
                                       , QVariant::fromValue<FileMapType>(params.renamed_files));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::Peers
                                       , QVariant::fromValue<EndpointVectorType>(params.peers));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::BannedPeers
                                       , QVariant::fromValue<EndpointVectorType>(params.banned_peers));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::FilePriorities
                                       , QVariant::fromValue<PriorityVectorType>(params.file_priorities));
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::PiecePriorities
                                       , QVariant::fromValue<PriorityVectorType>(params.piece_priorities));

    if (!m_hasMissingFiles)
    {
        m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::Pieces
                                           , QVariant::fromValue<PiecesType>({params.have_pieces, params.verified_pieces}));
        m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::UnfinishedPieces
                                           , QVariant::fromValue<PieceMapType>(params.unfinished_pieces));
    }
}

void TorrentImpl::handleFastResumeRejectedAlert(const lt::fastresume_rejected_alert *p)
{
    m_fastresumeDataRejected = true;

    if (p->error.value() == lt::errors::mismatching_file_size)
    {
        // Mismatching file size (files were probably moved)
        m_hasMissingFiles = true;
        LogMsg(tr("File sizes mismatch for torrent '%1'. Cannot proceed further.").arg(name()), Log::CRITICAL);
    }
    else
    {
        LogMsg(tr("Fast resume data was rejected for torrent '%1'. Reason: %2. Checking again...")
            .arg(name(), QString::fromStdString(p->message())), Log::WARNING);
    }
}

void TorrentImpl::handleFileRenamedAlert(const lt::file_renamed_alert *p)
{
    // Remove empty leftover folders
    // For example renaming "a/b/c" to "d/b/c", then folders "a/b" and "a" will
    // be removed if they are empty
    const QString oldFilePath = m_oldPath[p->index].takeFirst();
    const QString newFilePath = Utils::Fs::toUniformPath(p->new_name());

    if (m_oldPath[p->index].isEmpty())
        m_oldPath.remove(p->index);

    QVector<QStringRef> oldPathParts = oldFilePath.splitRef('/', QString::SkipEmptyParts);
    oldPathParts.removeLast();  // drop file name part
    QVector<QStringRef> newPathParts = newFilePath.splitRef('/', QString::SkipEmptyParts);
    newPathParts.removeLast();  // drop file name part

#if defined(Q_OS_WIN)
    const Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity caseSensitivity = Qt::CaseSensitive;
#endif

    int pathIdx = 0;
    while ((pathIdx < oldPathParts.size()) && (pathIdx < newPathParts.size()))
    {
        if (oldPathParts[pathIdx].compare(newPathParts[pathIdx], caseSensitivity) != 0)
            break;
        ++pathIdx;
    }

    for (int i = (oldPathParts.size() - 1); i >= pathIdx; --i)
    {
        QDir().rmdir(savePath() + Utils::String::join(oldPathParts, QLatin1String("/")));
        oldPathParts.removeLast();
    }

    --m_renameCount;
    while (!isMoveInProgress() && (m_renameCount == 0) && !m_moveFinishedTriggers.isEmpty())
        m_moveFinishedTriggers.takeFirst()();

    if (isStopped() && (m_renameCount == 0))
    {
        // otherwise the new path will not be saved
        const lt::file_storage &files = m_nativeHandle.torrent_file()->files();
        const lt::file_storage &origFiles = m_nativeHandle.torrent_file()->orig_files();
        FileMapType renamedFiles;
        for (const auto i : files.file_range())
        {
            const std::string filePath = files.file_path(i);
            if (filePath != origFiles.file_path(i))
                renamedFiles[i] = filePath;
        }
        m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::RenamedFiles, QVariant::fromValue(renamedFiles));
    }
}

void TorrentImpl::handleFileRenameFailedAlert(const lt::file_rename_failed_alert *p)
{
    LogMsg(tr("File rename failed. Torrent: \"%1\", file: \"%2\", reason: \"%3\"")
        .arg(name(), filePath(static_cast<LTUnderlyingType<lt::file_index_t>>(p->index))
             , QString::fromLocal8Bit(p->error.message().c_str())), Log::WARNING);

    m_oldPath[p->index].removeFirst();
    if (m_oldPath[p->index].isEmpty())
        m_oldPath.remove(p->index);

    --m_renameCount;
    while (!isMoveInProgress() && (m_renameCount == 0) && !m_moveFinishedTriggers.isEmpty())
        m_moveFinishedTriggers.takeFirst()();

    if (isStopped() && (m_renameCount == 0))
    {
        // otherwise the new path will not be saved
        const lt::file_storage &files = m_nativeHandle.torrent_file()->files();
        const lt::file_storage &origFiles = m_nativeHandle.torrent_file()->orig_files();
        FileMapType renamedFiles;
        for (const auto i : files.file_range())
        {
            const std::string filePath = files.file_path(i);
            if (filePath != origFiles.file_path(i))
                renamedFiles[i] = filePath;
        }
        m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::RenamedFiles, QVariant::fromValue(renamedFiles));
    }
}

void TorrentImpl::handleFileCompletedAlert(const lt::file_completed_alert *p)
{
    qDebug("A file completed download in torrent \"%s\"", qUtf8Printable(name()));
    if (m_session->isAppendExtensionEnabled())
    {
        QString name = filePath(static_cast<LTUnderlyingType<lt::file_index_t>>(p->index));
        if (name.endsWith(QB_EXT))
        {
            const QString oldName = name;
            name.chop(QB_EXT.size());
            qDebug("Renaming %s to %s", qUtf8Printable(oldName), qUtf8Printable(name));
            renameFile(static_cast<LTUnderlyingType<lt::file_index_t>>(p->index), name);
        }
    }
}

void TorrentImpl::handleMetadataReceivedAlert(const lt::metadata_received_alert *p)
{
    Q_UNUSED(p);

    qDebug("Metadata received for torrent %s.", qUtf8Printable(name()));

    m_maintenanceJob = MaintenanceJob::HandleMetadata;
    TorrentInfo metadata = TorrentInfo {m_nativeHandle.torrent_file()};
    metadata.setContentLayout(m_contentLayout);
    m_session->findIncompleteFiles(metadata, savePath(false));
}

void TorrentImpl::handlePerformanceAlert(const lt::performance_alert *p) const
{
    LogMsg((tr("Performance alert: ") + QString::fromStdString(p->message()))
           , Log::INFO);
}

void TorrentImpl::handleTempPathChanged()
{
    adjustActualSavePath();
}

void TorrentImpl::handleCategorySavePathChanged()
{
    if (m_useAutoTMM)
        move_impl(m_session->categorySavePath(m_category), MoveStorageMode::Overwrite);
}

void TorrentImpl::handleAppendExtensionToggled()
{
    if (!hasMetadata()) return;

    manageIncompleteFiles();
}

void TorrentImpl::handleAlert(const lt::alert *a)
{
    switch (a->type())
    {
    case lt::file_renamed_alert::alert_type:
        handleFileRenamedAlert(static_cast<const lt::file_renamed_alert*>(a));
        break;
    case lt::file_rename_failed_alert::alert_type:
        handleFileRenameFailedAlert(static_cast<const lt::file_rename_failed_alert*>(a));
        break;
    case lt::file_completed_alert::alert_type:
        handleFileCompletedAlert(static_cast<const lt::file_completed_alert*>(a));
        break;
    case lt::torrent_finished_alert::alert_type:
        handleTorrentFinishedAlert(static_cast<const lt::torrent_finished_alert*>(a));
        break;
    case lt::torrent_paused_alert::alert_type:
        handleTorrentPausedAlert(static_cast<const lt::torrent_paused_alert*>(a));
        break;
    case lt::torrent_resumed_alert::alert_type:
        handleTorrentResumedAlert(static_cast<const lt::torrent_resumed_alert*>(a));
        break;
    case lt::tracker_error_alert::alert_type:
        handleTrackerErrorAlert(static_cast<const lt::tracker_error_alert*>(a));
        break;
    case lt::tracker_reply_alert::alert_type:
        handleTrackerReplyAlert(static_cast<const lt::tracker_reply_alert*>(a));
        break;
    case lt::tracker_warning_alert::alert_type:
        handleTrackerWarningAlert(static_cast<const lt::tracker_warning_alert*>(a));
        break;
    case lt::metadata_received_alert::alert_type:
        handleMetadataReceivedAlert(static_cast<const lt::metadata_received_alert*>(a));
        break;
    case lt::fastresume_rejected_alert::alert_type:
        handleFastResumeRejectedAlert(static_cast<const lt::fastresume_rejected_alert*>(a));
        break;
    case lt::torrent_checked_alert::alert_type:
        handleTorrentCheckedAlert(static_cast<const lt::torrent_checked_alert*>(a));
        break;
    case lt::performance_alert::alert_type:
        handlePerformanceAlert(static_cast<const lt::performance_alert*>(a));
        break;
    }
}

void TorrentImpl::manageIncompleteFiles()
{
    const bool isAppendExtensionEnabled = m_session->isAppendExtensionEnabled();
    const QVector<qreal> fp = filesProgress();
    if (fp.size() != filesCount())
    {
        qDebug() << "skip manageIncompleteFiles because of invalid torrent meta-data or empty file-progress";
        return;
    }

    for (int i = 0; i < filesCount(); ++i)
    {
        QString name = filePath(i);
        if (isAppendExtensionEnabled && (fileSize(i) > 0) && (fp[i] < 1))
        {
            if (!name.endsWith(QB_EXT, Qt::CaseInsensitive))
            {
                const QString newName = name + QB_EXT;
                qDebug() << "Renaming" << name << "to" << newName;
                renameFile(i, newName);
            }
        }
        else
        {
            if (name.endsWith(QB_EXT, Qt::CaseInsensitive))
            {
                const QString oldName = name;
                name.chop(QB_EXT.size());
                qDebug() << "Renaming" << oldName << "to" << name;
                renameFile(i, name);
            }
        }
    }
}

void TorrentImpl::adjustActualSavePath()
{
    if (!isMoveInProgress())
        adjustActualSavePath_impl();
    else
        m_moveFinishedTriggers.append([this]() { adjustActualSavePath_impl(); });
}

void TorrentImpl::adjustActualSavePath_impl()
{
    const bool needUseTempDir = useTempPath();
    const QDir tempDir {m_session->torrentTempPath(info())};
    const QDir currentDir {actualStorageLocation()};
    const QDir targetDir {needUseTempDir ? tempDir : QDir {savePath()}};

    if (targetDir == currentDir) return;

    if (!needUseTempDir)
    {
        if ((currentDir == tempDir) && (currentDir != QDir {m_session->tempPath()}))
        {
            // torrent without root folder still has it in its temporary save path
            // so its temp path isn't equal to temp path root
            const QString currentDirPath = currentDir.absolutePath();
            m_moveFinishedTriggers.append([currentDirPath]
            {
                qDebug() << "Removing torrent temp folder:" << currentDirPath;
                Utils::Fs::smartRemoveEmptyFolderTree(currentDirPath);
            });
        }
    }

    moveStorage(Utils::Fs::toNativePath(targetDir.absolutePath()), MoveStorageMode::Overwrite);
}

lt::torrent_handle TorrentImpl::nativeHandle() const
{
    return m_nativeHandle;
}

bool TorrentImpl::isMoveInProgress() const
{
    return m_storageIsMoving;
}

bool TorrentImpl::useTempPath() const
{
    return m_session->isTempPathEnabled() && !(isSeed() || m_hasSeedStatus);
}

void TorrentImpl::updateStatus()
{
    updateStatus(m_nativeHandle.status());
}

void TorrentImpl::updateStatus(const lt::torrent_status &nativeStatus)
{
    // Operational data
    m_isPaused = static_cast<bool>(nativeStatus.flags & lt::torrent_flags::paused);
    m_isAutoManaged = static_cast<bool>(nativeStatus.flags & lt::torrent_flags::auto_managed);
    m_disableDHT = static_cast<bool>(nativeStatus.flags & lt::torrent_flags::disable_dht);
    m_disableLSD = static_cast<bool>(nativeStatus.flags & lt::torrent_flags::disable_lsd);
    m_disablePEX = static_cast<bool>(nativeStatus.flags & lt::torrent_flags::disable_pex);
    m_superSeeding = static_cast<bool>(nativeStatus.flags & lt::torrent_flags::super_seeding);
    m_sequentialDownload = static_cast<bool>(nativeStatus.flags & lt::torrent_flags::sequential_download);
    m_stopWhenReady = static_cast<bool>(nativeStatus.flags & lt::torrent_flags::stop_when_ready);
    m_activeTime = lt::total_seconds(nativeStatus.active_duration);
    m_finishedTime = lt::total_seconds(nativeStatus.finished_duration);
    m_seedingTime = lt::total_seconds(nativeStatus.seeding_duration);
    m_numComplete = nativeStatus.num_complete;
    m_numIncomplete = nativeStatus.num_incomplete;
    m_totalDownloaded = nativeStatus.all_time_download;
    m_totalUploaded = nativeStatus.all_time_upload;
    m_addedTime = QDateTime::fromSecsSinceEpoch(nativeStatus.added_time);
    m_lastSeenComplete = QDateTime::fromSecsSinceEpoch(nativeStatus.last_seen_complete);
    m_lastDownload = QDateTime::fromSecsSinceEpoch(lt::total_seconds(nativeStatus.last_download.time_since_epoch()));
    m_lastUpload = QDateTime::fromSecsSinceEpoch(lt::total_seconds(nativeStatus.last_upload.time_since_epoch()));
    m_completedTime = QDateTime::fromSecsSinceEpoch(nativeStatus.completed_time);
    m_storageLocation = QString::fromStdString(nativeStatus.save_path);

    m_wantedSize = nativeStatus.total_wanted;
    m_completedSize = nativeStatus.total_wanted_done;
    m_downloadedSize = nativeStatus.total_done;
    m_failedBytes = nativeStatus.total_failed_bytes;
    m_redundantBytes = nativeStatus.total_redundant_bytes;
    m_totalPayloadDownload = nativeStatus.total_payload_download;
    m_totalPayloadUpload = nativeStatus.total_payload_upload;
    m_nextAnnounce = lt::total_seconds(nativeStatus.next_announce);
    m_existingPiecesCount = nativeStatus.num_pieces;
    m_downloadPayloadRate = nativeStatus.download_payload_rate;
    m_uploadPayloadRate = nativeStatus.upload_payload_rate;
    m_queuePosition = static_cast<int>(nativeStatus.queue_position);
    m_connectedSeedsCount = nativeStatus.num_seeds;
    m_connectedPeersCount = nativeStatus.num_peers;
    m_seedsCount = nativeStatus.list_seeds;
    m_peersCount = nativeStatus.list_peers;
    m_connectionsCount = nativeStatus.num_connections;
    m_connectionsLimit = nativeStatus.connections_limit;
    m_progress = nativeStatus.progress;
    m_distributedCopies = nativeStatus.distributed_copies;
    m_nativeState = nativeStatus.state;
    m_currentTracker = QString::fromStdString(nativeStatus.current_tracker);
    m_error = nativeStatus.errc;

    {
        m_pieces.resize(nativeStatus.pieces.size());
        int i = 0;
        for (bool bit : nativeStatus.pieces)
            m_pieces[i++] = bit;
    }

    updateState();

    m_speedMonitor.addSample({nativeStatus.download_payload_rate
                              , nativeStatus.upload_payload_rate});

    if (hasMetadata())
    {
        // NOTE: Don't change the order of these conditionals!
        // Otherwise it will not work properly since torrent can be CheckingDownloading.
        if (isChecking())
            m_unchecked = false;
        else if (isDownloading())
            m_unchecked = true;
    }
}

void TorrentImpl::setRatioLimit(qreal limit)
{
    if (limit < USE_GLOBAL_RATIO)
        limit = NO_RATIO_LIMIT;
    else if (limit > MAX_RATIO)
        limit = MAX_RATIO;

    if (m_ratioLimit != limit)
    {
        m_ratioLimit = limit;
        m_session->handleTorrentShareLimitChanged(this);
    }
}

void TorrentImpl::setSeedingTimeLimit(int limit)
{
    if (limit < USE_GLOBAL_SEEDING_TIME)
        limit = NO_SEEDING_TIME_LIMIT;
    else if (limit > MAX_SEEDING_TIME)
        limit = MAX_SEEDING_TIME;

    if (m_seedingTimeLimit != limit)
    {
        m_seedingTimeLimit = limit;
        m_session->handleTorrentShareLimitChanged(this);
    }
}

void TorrentImpl::setUploadLimit(const int limit)
{
    if (limit == uploadLimit())
        return;

    m_nativeHandle.set_upload_limit(limit);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::UploadRateLimit, uploadLimit());
}

void TorrentImpl::setDownloadLimit(const int limit)
{
    if (limit == downloadLimit())
        return;

    m_nativeHandle.set_download_limit(limit);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::DownloadRateLimit, downloadLimit());
}

void TorrentImpl::setSuperSeeding(const bool enable)
{
    if (enable == superSeeding())
        return;

    m_superSeeding = enable;
    applyFlag(lt::torrent_flags::super_seeding, m_superSeeding);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::SuperSeeding, m_superSeeding);
}

void TorrentImpl::setDHTDisabled(const bool disable)
{
    if (disable == isDHTDisabled())
        return;

    m_disableDHT = disable;
    applyFlag(lt::torrent_flags::disable_dht, m_disableDHT);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::DisableDHT, m_disableDHT);
}

void TorrentImpl::setPEXDisabled(const bool disable)
{
    if (disable == isPEXDisabled())
        return;

    m_disablePEX = disable;
    applyFlag(lt::torrent_flags::disable_pex, m_disablePEX);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::DisablePEX, m_disablePEX);
}

void TorrentImpl::setLSDDisabled(const bool disable)
{
    if (disable == isLSDDisabled())
        return;

    m_disableLSD = disable;
    applyFlag(lt::torrent_flags::disable_lsd, m_disableLSD);
    m_persistentDataStorage->storeData(PersistentDataStorage::ItemID::DisableLSD, m_disableLSD);
}

void TorrentImpl::flushCache() const
{
    m_nativeHandle.flush_cache();
}

QString TorrentImpl::createMagnetURI() const
{
    return QString::fromStdString(lt::make_magnet_uri(m_nativeHandle));
}

void TorrentImpl::prioritizeFiles(const QVector<DownloadPriority> &priorities)
{
    if (!hasMetadata()) return;
    if (priorities.size() != filesCount()) return;

    // Reset 'm_hasSeedStatus' if needed in order to react again to
    // 'torrent_finished_alert' and eg show tray notifications
    const QVector<qreal> progress = filesProgress();
    const QVector<DownloadPriority> oldPriorities = filePriorities();
    for (int i = 0; i < oldPriorities.size(); ++i)
    {
        if ((oldPriorities[i] == DownloadPriority::Ignored)
            && (priorities[i] > DownloadPriority::Ignored)
            && (progress[i] < 1.0))
        {
            m_hasSeedStatus = false;
            break;
        }
    }

    qDebug() << Q_FUNC_INFO << "Changing files priorities...";
    m_nativeHandle.prioritize_files(toLTDownloadPriorities(priorities));

    // Restore first/last piece first option if necessary
    if (m_hasFirstLastPiecePriority)
        applyFirstLastPiecePriority(true, priorities);
}

QVector<qreal> TorrentImpl::availableFileFractions() const
{
    const int filesCount = this->filesCount();
    if (filesCount <= 0) return {};

    const QVector<int> piecesAvailability = pieceAvailability();
    // libtorrent returns empty array for seeding only torrents
    if (piecesAvailability.empty()) return QVector<qreal>(filesCount, -1);

    QVector<qreal> res;
    res.reserve(filesCount);
    const TorrentInfo info = this->info();
    for (int i = 0; i < filesCount; ++i)
    {
        const TorrentInfo::PieceRange filePieces = info.filePieces(i);

        int availablePieces = 0;
        for (const int piece : filePieces)
            availablePieces += (piecesAvailability[piece] > 0) ? 1 : 0;

        const qreal availability = filePieces.isEmpty()
            ? 1  // the file has no pieces, so it is available by default
            : static_cast<qreal>(availablePieces) / filePieces.size();
        res.push_back(availability);
    }
    return res;
}
