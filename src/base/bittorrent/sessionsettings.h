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

#include <QString>
#include <QStringList>

#include "base/path.h"
#include "sharelimits.h"
#include "torrentcontentlayout.h"
#include "torrentcontentremoveoption.h"
#include "torrentstopcondition.h"

namespace BitTorrent
{
    // Using `Q_ENUM_NS()` without a wrapper namespace in our case is not advised
    // since `Q_NAMESPACE` cannot be used when the same namespace resides at different files.
    // https://www.kdab.com/new-qt-5-8-meta-object-support-namespaces/#comment-143779
    inline namespace SessionSettingsEnums
    {
        Q_NAMESPACE

        enum class BTProtocol : int
        {
            Both = 0,
            TCP = 1,
            UTP = 2
        };
        Q_ENUM_NS(BTProtocol)

        enum class ChokingAlgorithm : int
        {
            FixedSlots = 0,
            RateBased = 1
        };
        Q_ENUM_NS(ChokingAlgorithm)

        enum class DiskIOReadMode : int
        {
            DisableOSCache = 0,
            EnableOSCache = 1
        };
        Q_ENUM_NS(DiskIOReadMode)

        enum class DiskIOType : int
        {
            Default = 0,
            MMap = 1,
            Posix = 2,
            SimplePreadPwrite = 3,
        #if LIBTORRENT_VERSION_NUM >= 20100
            PreadPwrite = 4
        #endif
        };
        Q_ENUM_NS(DiskIOType)

        enum class DiskIOWriteMode : int
        {
            DisableOSCache = 0,
            EnableOSCache = 1,
        #ifdef QBT_USES_LIBTORRENT2
            WriteThrough = 2
        #endif
        };
        Q_ENUM_NS(DiskIOWriteMode)

        enum class MixedModeAlgorithm : int
        {
            TCP = 0,
            Proportional = 1
        };
        Q_ENUM_NS(MixedModeAlgorithm)

        enum class SeedChokingAlgorithm : int
        {
            RoundRobin = 0,
            FastestUpload = 1,
            AntiLeech = 2
        };
        Q_ENUM_NS(SeedChokingAlgorithm)

        enum class ResumeDataStorageType
        {
            Legacy,
            SQLite
        };
        Q_ENUM_NS(ResumeDataStorageType)
    }

    struct SessionSettings
    {
        QString DHTBootstrapNodes;
        bool isDHTEnabled = false;
        bool isLSDEnabled = false;
        bool isPeXEnabled = false;
        bool isIPFilteringEnabled = false;
        bool isTrackerFilteringEnabled = false;
        Path IPFilterFile;
        bool announceToAllTrackers = false;
        bool announceToAllTiers = false;
        int asyncIOThreads = 0;
        int hashingThreads = 0;
        int filePoolSize = 0;
        int checkingMemUsage = 0;
        int diskCacheSize = 0;
        int diskCacheTTL = 0;
        qint64 diskQueueSize = 0;
        DiskIOType diskIOType;
        DiskIOReadMode diskIOReadMode;
        DiskIOWriteMode diskIOWriteMode;
        bool coalesceReadWriteEnabled = false;
        bool usePieceExtentAffinity = false;
        bool isSuggestMode = false;
        int sendBufferWatermark = 0;
        int sendBufferLowWatermark = 0;
        int sendBufferWatermarkFactor = 0;
        int connectionSpeed = 0;
        bool isSeedingOutgoingConnectionsEnabled = false;
        int socketSendBufferSize = 0;
        int socketReceiveBufferSize = 0;
        int socketBacklogSize = 0;
        bool isAnonymousModeEnabled = false;
        bool isQueueingEnabled = false;
        int maxActiveDownloads = 0;
        int maxActiveUploads = 0;
        int maxActiveTorrents = 0;
        bool ignoreSlowTorrentsForQueueing = false;
        int downloadRateForSlowTorrents = 0;
        int uploadRateForSlowTorrents = 0;
        int slowTorrentsInactivityTimer = 0;
        int outgoingPortsMin = 0;
        int outgoingPortsMax = 0;
        int UPnPLeaseDuration = 0;
        int peerDSCP = 0;
        bool ignoreLimitsOnLAN = false;
        bool includeOverheadInLimits = false;
        QString announceIP;
        int announcePort = 0;
        int maxConcurrentHTTPAnnounces = 0;
        bool isReannounceWhenAddressChangedEnabled = false;
        int stopTrackerTimeout = 0;
        int maxConnections = 0;
        int maxUploads = 0;
        int maxConnectionsPerTorrent = 0;
        int maxUploadsPerTorrent = 0;
        BTProtocol btProtocol = BTProtocol::Both;
        bool isUTPRateLimited = false;
        MixedModeAlgorithm utpMixedMode = MixedModeAlgorithm::TCP;
        int hostnameCacheTTL = 0;
        bool IDNSupportEnabled = false;
        bool multiConnectionsPerIpEnabled = false;
        bool validateHTTPSTrackerCertificate = false;
        bool SSRFMitigationEnabled = false;
        bool blockPeersOnPrivilegedPorts = false;
        bool isAddTrackersEnabled = false;
        QString additionalTrackers;
        bool isAddTrackersFromURLEnabled = false;
        QString additionalTrackersURL;
        bool isAddTorrentToQueueTop = false;
        bool isAddTorrentStopped = false;
        TorrentStopCondition torrentStopCondition = TorrentStopCondition::None;
        TorrentContentLayout torrentContentLayout = TorrentContentLayout::Original;
        bool isAppendExtensionEnabled = false;
        bool isUnwantedFolderEnabled = false;
        int refreshInterval = 0;
        bool isPreallocationEnabled = false;
        Path torrentExportDirectory;
        Path finishedTorrentExportDirectory;
        qint64 globalDownloadSpeedLimit = 0;
        qint64 globalUploadSpeedLimit = 0;
        qint64 altGlobalDownloadSpeedLimit = 0;
        qint64 altGlobalUploadSpeedLimit = 0;
        bool isBandwidthSchedulerEnabled = false;
        bool isPerformanceWarningEnabled = false;
        int saveResumeDataInterval = 0;
        std::chrono::minutes saveStatisticsInterval;
        int shutdownTimeout = 0;
        int port = 0;
        bool sslEnabled = false;
        int sslPort = 0;
        QString networkInterface;
        QString networkInterfaceName;
        QString networkInterfaceAddress;
        int encryption = 0;
        int maxActiveCheckingTorrents = 0;
        bool isProxyPeerConnectionsEnabled = false;
        ChokingAlgorithm chokingAlgorithm = ChokingAlgorithm::FixedSlots;
        SeedChokingAlgorithm seedChokingAlgorithm = SeedChokingAlgorithm::RoundRobin;
        ShareLimits shareLimits;
        Path savePath;
        Path downloadPath;
        bool isDownloadPathEnabled = false;
        bool useCategoryPathsInManualMode = false;
        bool isAutoTMMDisabledByDefault = false;
        bool isDisableAutoTMMWhenCategoryChanged = false;
        bool isDisableAutoTMMWhenDefaultSavePathChanged = false;
        bool isDisableAutoTMMWhenCategorySavePathChanged = false;
        bool isTrackerEnabled = false;
        int peerTurnover = 0;
        int peerTurnoverCutoff = 0;
        int peerTurnoverInterval = 0;
        int requestQueueSize = 0;
        bool isExcludedFileNamesEnabled = false;
        QStringList excludedFileNames;
        ResumeDataStorageType resumeDataStorageType = ResumeDataStorageType::Legacy;
        bool isMergeTrackersEnabled = false;
        bool isI2PEnabled = false;
        QString I2PAddress;
        int I2PPort = 0;
        bool I2PMixedMode = false;
        int I2PInboundQuantity = 0;
        int I2POutboundQuantity = 0;
        int I2PInboundLength = 0;
        int I2POutboundLength = 0;
        TorrentContentRemoveOption torrentContentRemoveOption = TorrentContentRemoveOption::Delete;
        bool startPaused = false;
    };
}
