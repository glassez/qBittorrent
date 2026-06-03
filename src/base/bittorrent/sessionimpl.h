/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2015-2026  Vladimir Golovnev <glassez@yandex.ru>
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

#pragma once

#include <functional>
#include <utility>
#include <vector>

#include <libtorrent/fwd.hpp>
#include <libtorrent/portmap.hpp>
#include <libtorrent/torrent_handle.hpp>

#include <QtContainerFwd>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QPointer>
#include <QSet>
#include <QThreadPool>

#include "base/path.h"
#include "base/utils/thread.h"
#include "addtorrentparams.h"
#include "cachestatus.h"
#include "categoryoptions.h"
#include "session.h"
#include "sessionsettings.h"
#include "sessionstatus.h"
#include "torrentinfo.h"

class QString;
class QTimer;
class QUrl;

template <typename T> class QFuture;

class BandwidthScheduler;
class FileSearcher;
class FilterParserThread;
class FreeDiskSpaceChecker;
class NativeSessionExtension;

struct FileSearchResult;

namespace BitTorrent
{
    enum class MoveStorageMode;
    enum class MoveStorageContext;

    class InfoHash;
    class ResumeDataStorage;
    class Torrent;
    class TorrentContentRemover;
    class TorrentDescriptor;
    class TorrentImpl;
    class Tracker;

    struct LoadTorrentParams;
    struct TrackerEntry;

    struct SessionMetricIndices
    {
        struct
        {
            int hasIncomingConnections = -1;
            int sentPayloadBytes = -1;
            int recvPayloadBytes = -1;
            int sentBytes = -1;
            int recvBytes = -1;
            int sentIPOverheadBytes = -1;
            int recvIPOverheadBytes = -1;
            int sentTrackerBytes = -1;
            int recvTrackerBytes = -1;
            int recvRedundantBytes = -1;
            int recvFailedBytes = -1;
        } net;

        struct
        {
            int numPeersConnected = -1;
            int numPeersUpDisk = -1;
            int numPeersDownDisk = -1;
        } peer;

        struct
        {
            int dhtBytesIn = -1;
            int dhtBytesOut = -1;
            int dhtNodes = -1;
        } dht;

        struct
        {
            int diskBlocksInUse = -1;
            int numBlocksRead = -1;
#ifndef QBT_USES_LIBTORRENT2
            int numBlocksCacheHits = -1;
#endif
            int writeJobs = -1;
            int readJobs = -1;
            int hashJobs = -1;
            int queuedDiskJobs = -1;
            int diskJobTime = -1;
            int requestLatency = -1;
        } disk;

        struct
        {
            int numQueuedTrackerAnnounces = -1;
        } tracker;
    };

    class SessionImpl final : public Session
    {
        Q_OBJECT
        Q_DISABLE_COPY_MOVE(SessionImpl)

    public:
        const SessionSettings &settings() const override;
        void setSettings(SessionSettings settings) override;

        QStringList categories() const override;
        CategoryOptions categoryOptions(const QString &categoryName) const override;
        bool setCategoryOptions(const QString &categoryName, const CategoryOptions &options) override;
        Path categorySavePath(const QString &categoryName) const override;
        Path categorySavePath(const QString &categoryName, const CategoryOptions &options) const override;
        Path categoryDownloadPath(const QString &categoryName) const override;
        Path categoryDownloadPath(const QString &categoryName, const CategoryOptions &options) const override;
        ShareLimits categoryShareLimits(const QString &categoryName) const override;
        bool addCategory(const QString &name, const CategoryOptions &options = {}) override;
        bool removeCategory(const QString &name) override;

        Path suggestedSavePath(const QString &categoryName, std::optional<bool> useAutoTMM) const override;
        Path suggestedDownloadPath(const QString &categoryName, std::optional<bool> useAutoTMM) const override;

        TagSet tags() const override;
        bool hasTag(const Tag &tag) const override;
        bool addTag(const Tag &tag) override;
        bool removeTag(const Tag &tag) override;

        QString additionalTrackersFromURL() const override;

        bool isRestored() const override;

        bool isPaused() const override;
        void pause() override;
        void resume() override;

        void applyFilenameFilter(const PathList &files, QList<DownloadPriority> &priorities) override;

        int downloadSpeedLimit() const override;
        int uploadSpeedLimit() const override;

        QStringList bannedIPs() const override;
        void setBannedIPs(const QStringList &newList) override;

        bool isAltGlobalSpeedLimitEnabled() const override;
        void setAltGlobalSpeedLimitEnabled(bool enabled) override;

        void reannounceToAllTrackers() const override;

        Torrent *getTorrent(const TorrentID &id) const override;
        Torrent *findTorrent(const InfoHash &infoHash) const override;
        QList<Torrent *> torrents() const override;
        qsizetype torrentsCount() const override;
        const SessionStatus &status() const override;
        const CacheStatus &cacheStatus() const override;
        bool isListening() const override;

        void banIP(const QString &ip) override;

        bool isKnownTorrent(const InfoHash &infoHash) const override;
        bool addTorrent(const TorrentDescriptor &torrentDescr, const AddTorrentParams &params = {}) override;
        bool removeTorrent(const TorrentID &id, TorrentRemoveOption deleteOption = TorrentRemoveOption::KeepContent) override;
        bool downloadMetadata(const TorrentDescriptor &torrentDescr) override;
        bool cancelDownloadMetadata(const TorrentID &id) override;

        void increaseTorrentsQueuePos(const QList<TorrentID> &ids) override;
        void decreaseTorrentsQueuePos(const QList<TorrentID> &ids) override;
        void topTorrentsQueuePos(const QList<TorrentID> &ids) override;
        void bottomTorrentsQueuePos(const QList<TorrentID> &ids) override;

        QString lastExternalIPv4Address() const override;
        QString lastExternalIPv6Address() const override;

        qint64 freeDiskSpace() const override;

        // Torrent interface
        void handleTorrentResumeDataRequested(const TorrentImpl *torrent);
        void handleTorrentShareLimitChanged(TorrentImpl *torrent);
        void handleTorrentNameChanged(TorrentImpl *torrent);
        void handleTorrentSavePathChanged(TorrentImpl *torrent);
        void handleTorrentCategoryChanged(TorrentImpl *torrent, const QString &oldCategory);
        void handleTorrentTagAdded(TorrentImpl *torrent, const Tag &tag);
        void handleTorrentTagRemoved(TorrentImpl *torrent, const Tag &tag);
        void handleTorrentSavingModeChanged(TorrentImpl *torrent);
        void handleTorrentMetadataReceived(TorrentImpl *torrent);
        void handleTorrentStopped(TorrentImpl *torrent);
        void handleTorrentStarted(TorrentImpl *torrent);
        void handleTorrentChecked(TorrentImpl *torrent);
        void handleTorrentFinished(TorrentImpl *torrent);
        void handleTorrentTrackersAdded(TorrentImpl *torrent, const QList<TrackerEntry> &newTrackers);
        void handleTorrentTrackersRemoved(TorrentImpl *torrent, const QStringList &deletedTrackers);
        void handleTorrentTrackersReset(TorrentImpl *torrent, const QList<TrackerEntryStatus> &oldEntries, const QList<TrackerEntry> &newEntries);
        void handleTorrentUrlSeedsAdded(TorrentImpl *torrent, const QList<QUrl> &newUrlSeeds);
        void handleTorrentUrlSeedsRemoved(TorrentImpl *torrent, const QList<QUrl> &urlSeeds);
        void handleTorrentResumeDataReady(TorrentImpl *torrent, LoadTorrentParams data);
        void handleTorrentInfoHashChanged(TorrentImpl *torrent, const InfoHash &prevInfoHash);
        void handleTorrentContentFileRenamed(TorrentImpl *torrent, int index, const Path &oldFilePath);
        void handleTorrentContentFolderRenamed(const Path &newFolderPath, const Path &oldFolderPath, const QHash<int, Path> &renamedFiles);
        void handleTorrentContentFolderRenamingFailed(const Path &newFolderPath, const Path &oldFolderPath
                , const QHash<int, Path> &renamedFiles, const QList<int> &failedFileIndexes);
        void handleTorrentStorageMovingStateChanged(TorrentImpl *torrent);

        bool addMoveTorrentStorageJob(TorrentImpl *torrent, const Path &newPath, MoveStorageMode mode, MoveStorageContext context);

        lt::torrent_handle reloadTorrent(const lt::torrent_handle &currentHandle, lt::add_torrent_params params);

        QFuture<FileSearchResult> findIncompleteFiles(const Path &savePath, const Path &downloadPath, const PathList &filePaths = {}) const;

        void enablePortMapping();
        void disablePortMapping();
        void addMappedPorts(const QSet<quint16> &ports);
        void removeMappedPorts(const QSet<quint16> &ports);

        template <typename Func>
        void invoke(Func &&func)
        {
            QMetaObject::invokeMethod(this, std::forward<Func>(func), Qt::QueuedConnection);
        }

        template <typename Func>
        void invokeAsync(Func &&func)
        {
            m_asyncWorker->start(std::forward<Func>(func));
        }

    signals:
        void addTorrentAlertsReceived(qsizetype count);

    private slots:
        void configureDeferred();
        void readAlerts();
        void enqueueRefresh();
        void generateResumeData();
        void handleIPFilterParsed(int ruleCount);
        void handleIPFilterError();
        void torrentContentRemovingFinished(const QString &torrentName, const QString &errorMessage);

    private:
        struct ResumeSessionContext;

        struct MoveStorageJob
        {
            lt::torrent_handle torrentHandle;
            Path path;
            MoveStorageMode mode {};
            MoveStorageContext context {};
        };

        struct RemovingTorrentData
        {
            QString name;
            Path contentStoragePath;
            PathList fileNames;
            TorrentRemoveOption removeOption {};
        };

        explicit SessionImpl(QObject *parent = nullptr);
        ~SessionImpl();

        // Session configuration
        void loadSettings();
        void storeSettings();
        void configure();
        void configureComponents();
        void initializeNativeSession();
        lt::settings_pack loadLTSettings() const;
        void applyNetworkInterfacesSettings(lt::settings_pack &settingsPack) const;
        void configurePeerClasses();
        void initMetrics();
        void applyBandwidthLimits();
        void processBannedIPs(lt::ip_filter &filter);
        QStringList getListeningIPs() const;
        void enableTracker(bool enable);
        void enableBandwidthScheduler();
        void populateAdditionalTrackers();
        void enableIPFilter();
        void disableIPFilter();
        void processTorrentShareLimits(TorrentImpl *torrent);
        void populateExcludedFileNamesRegExpList();
        void prepareStartup();
        void handleLoadedResumeData(ResumeSessionContext *context);
        void processNextResumeData(ResumeSessionContext *context);
        void endStartup(ResumeSessionContext *context);

        LoadTorrentParams initLoadTorrentParams(const AddTorrentParams &addTorrentParams);
        bool addTorrent_impl(const TorrentDescriptor &source, const AddTorrentParams &addTorrentParams);

        void updateShareLimitsTimer();
        void exportTorrentFile(const Torrent *torrent, const Path &folderPath);

        void handleAlert(lt::alert *alert);
        void handleAddTorrentAlert(const lt::add_torrent_alert *alert);
        void handleStateUpdateAlert(const lt::state_update_alert *alert);
        void handleMetadataReceivedAlert(const lt::metadata_received_alert *alert);
        void handleFileErrorAlert(const lt::file_error_alert *alert);
        void handleTorrentRemovedAlert(const lt::torrent_removed_alert *alert);
        void handleTorrentDeletedAlert(const lt::torrent_deleted_alert *alert);
        void handleTorrentDeleteFailedAlert(const lt::torrent_delete_failed_alert *alert);
        void handleTorrentNeedCertAlert(const lt::torrent_need_cert_alert *alert);
        void handlePortmapWarningAlert(const lt::portmap_error_alert *alert);
        void handlePortmapAlert(const lt::portmap_alert *alert);
        void handlePeerBlockedAlert(const lt::peer_blocked_alert *alert);
        void handlePeerBanAlert(const lt::peer_ban_alert *alert);
        void handleUrlSeedAlert(const lt::url_seed_alert *alert);
        void handleListenSucceededAlert(const lt::listen_succeeded_alert *alert);
        void handleListenFailedAlert(const lt::listen_failed_alert *alert);
        void handleExternalIPAlert(const lt::external_ip_alert *alert);
        void handleSessionErrorAlert(const lt::session_error_alert *alert) const;
        void handleSessionStatsAlert(const lt::session_stats_alert *alert);
        void handleAlertsDroppedAlert(const lt::alerts_dropped_alert *alert) const;
        void handleStorageMovedAlert(const lt::storage_moved_alert *alert);
        void handleStorageMovedFailedAlert(const lt::storage_moved_failed_alert *alert);
        void handleSocks5Alert(const lt::socks5_alert *alert) const;
        void handleI2PAlert(const lt::i2p_alert *alert) const;
        void handleTrackerAlert(const lt::tracker_alert *alert);
#ifdef QBT_USES_LIBTORRENT2
        void handleTorrentConflictAlert(const lt::torrent_conflict_alert *alert);
        void handleFilePrioAlert(const lt::file_prio_alert *alert);
#endif
        void handleFastResumeRejectedAlert(const lt::fastresume_rejected_alert *alert);
        void handleFileCompletedAlert(const lt::file_completed_alert *alert);
        void handleFileRenamedAlert(const lt::file_renamed_alert *alert);
        void handleFileRenameFailedAlert(const lt::file_rename_failed_alert *alert);
        void handlePerformanceAlert(const lt::performance_alert *alert) const;
        void handleSaveResumeDataAlert(lt::save_resume_data_alert *alert);
        void handleSaveResumeDataFailedAlert(const lt::save_resume_data_failed_alert *alert);
        void handleTorrentCheckedAlert(const lt::torrent_checked_alert *alert);
        void handleTorrentFinishedAlert(const lt::torrent_finished_alert *alert);

        TorrentImpl *createTorrent(const lt::torrent_handle &nativeHandle, LoadTorrentParams params);
        TorrentImpl *getTorrent(const lt::torrent_handle &nativeHandle) const;
        QList<TorrentImpl *> getQueuedTorrentsByID(const QList<TorrentID> &torrentIDs) const;

        void saveResumeData();
        void saveTorrentsQueue();
        void removeTorrentsQueue();

        void populateAdditionalTrackersFromURL();

        void fetchPendingAlerts(lt::time_duration time = lt::time_duration::zero());
        void endAlertSequence(int alertType, qsizetype alertCount);

        void moveTorrentStorage(const MoveStorageJob &job) const;
        void handleMoveTorrentStorageJobFinished(const Path &newPath);
        void processPendingFinishedTorrents();

        void loadCategories();
        void storeCategories() const;
        void upgradeCategories();
        DownloadPathOption resolveCategoryDownloadPathOption(const QString &categoryName, const std::optional<DownloadPathOption> &option) const;

        void saveStatistics() const;
        void loadStatistics();

        void updateTrackerEntryStatuses(lt::torrent_handle torrentHandle);

        void handleRemovedTorrent(const TorrentID &torrentID, const QString &partfileRemoveError = {});

        void setAdditionalTrackersFromURL(const QString &trackers);
        void updateTrackersFromURL();
        void updateTrackersFromFile();

        void handleSavePathChanged();
        void handleDownloadPathChanged(bool wasDownloadPathEnabled, const Path &oldDownloadPath);

        SessionSettings m_settings;

        lt::session *m_nativeSession = nullptr;
        NativeSessionExtension *m_nativeSessionExtension = nullptr;

        bool m_deferredConfigureScheduled = false;
        bool m_IPFilteringConfigured = false;
        mutable bool m_isListenInterfaceConfigured = false;

        QString m_additionalTrackersFromURL;
        QTimer *m_updateTrackersFromURLTimer = nullptr;

        bool m_isRestored = false;
        bool m_isPaused = false;

        bool m_wasPexEnabled = false;

        int m_numResumeData = 0;
        QList<TrackerEntry> m_additionalTrackerEntries;
        QList<TrackerEntry> m_additionalTrackerEntriesFromURL;
        QList<QRegularExpression> m_excludedFileNamesRegExpList;

        // Statistics
        mutable QElapsedTimer m_statisticsLastUpdateTimer;
        mutable bool m_isStatisticsDirty = false;
        qint64 m_previouslyUploaded = 0;
        qint64 m_previouslyDownloaded = 0;

        bool m_torrentsQueueChanged = false;
        bool m_needSaveTorrentsQueue = false;
        bool m_refreshEnqueued = false;
        QTimer *m_seedingLimitTimer = nullptr;
        QTimer *m_resumeDataTimer = nullptr;
        // IP filtering
        QPointer<FilterParserThread> m_filterParser;
        QPointer<BandwidthScheduler> m_bwScheduler;
        // Tracker
        QPointer<Tracker> m_tracker;

        Utils::Thread::UniquePtr m_ioThread;
        QThreadPool *m_asyncWorker = nullptr;
        ResumeDataStorage *m_resumeDataStorage = nullptr;
        FileSearcher *m_fileSearcher = nullptr;
        TorrentContentRemover *m_torrentContentRemover = nullptr;

        using AddTorrentAlertHandler = std::function<void (const lt::add_torrent_alert *alert)>;
        QList<AddTorrentAlertHandler> m_addTorrentAlertHandlers;

        QHash<TorrentID, lt::torrent_handle> m_downloadedMetadata;

        QHash<TorrentID, TorrentImpl *> m_torrents;
        QHash<TorrentID, TorrentImpl *> m_hybridTorrentsByAltID;
        QHash<TorrentID, RemovingTorrentData> m_removingTorrents;
        QHash<TorrentID, TorrentID> m_changedTorrentIDs;
        QMap<QString, CategoryOptions> m_categories;
        TagSet m_tags;

        std::vector<lt::alert *> m_alerts;  // make it a class variable so it can preserve its allocated `capacity`
        qsizetype m_receivedAddTorrentAlertsCount = 0;
        QList<Torrent *> m_loadedTorrents;

        // This field holds amounts of peers reported by trackers in their responses to announces
        // (torrent.tracker_name.tracker_local_endpoint.protocol_version.num_peers)
        QHash<lt::torrent_handle, QHash<std::string, QHash<lt::tcp::endpoint, QMap<int, int>>>> m_updatedTrackerStatuses;
        QMutex m_updatedTrackerStatusesMutex;

        // I/O errored torrents
        QSet<TorrentID> m_recentErroredTorrents;
        QTimer *m_recentErroredTorrentsTimer = nullptr;

        SessionMetricIndices m_metricIndices;
        lt::time_point m_statsLastTimestamp = lt::clock_type::now();

        SessionStatus m_status;
        CacheStatus m_cacheStatus;

        QList<MoveStorageJob> m_moveStorageQueue;

        QString m_lastExternalIPv4Address;
        QString m_lastExternalIPv6Address;

        bool m_needUpgradeDownloadPath = false;

        // All port mapping related routines are invoked from working thread
        // so there are no synchronization used. If multithreaded access is
        // ever required, synchronization should also be provided.
        bool m_isPortMappingEnabled = false;
        QHash<quint16, std::vector<lt::port_mapping_t>> m_mappedPorts;

        QElapsedTimer m_wakeupCheckTimestamp;

        QList<TorrentImpl *> m_pendingFinishedTorrents;

        FreeDiskSpaceChecker *m_freeDiskSpaceChecker = nullptr;
        QTimer *m_freeDiskSpaceCheckingTimer = nullptr;
        qint64 m_freeDiskSpace = -1;

        friend void Session::initInstance();
        friend void Session::freeInstance();
        friend Session *Session::instance();
        static Session *m_instance;
    };
}
