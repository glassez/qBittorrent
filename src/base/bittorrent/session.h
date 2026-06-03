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

#include <libtorrent/version.hpp>

#include <QtContainerFwd>
#include <QObject>

#include "base/pathfwd.h"
#include "base/tagset.h"
#include "addtorrenterror.h"
#include "addtorrentparams.h"
#include "categoryoptions.h"
#include "sharelimits.h"
#include "trackerentry.h"
#include "trackerentrystatus.h"

class QString;

namespace BitTorrent
{
    class InfoHash;
    class Torrent;
    class TorrentDescriptor;
    class TorrentID;
    class TorrentInfo;
    struct CacheStatus;
    struct SessionSettings;
    struct SessionStatus;

    enum class TorrentRemoveOption
    {
        KeepContent,
        RemoveContent
    };

    class Session : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY_MOVE(Session)

    public:
        static void initInstance();
        static void freeInstance();
        static Session *instance();

        using QObject::QObject;

        static bool isValidCategoryName(const QString &name);
        static QString subcategoryName(const QString &category);
        static QString parentCategoryName(const QString &category);
        // returns category itself and all top level categories
        static QStringList expandCategory(const QString &category);

        virtual const SessionSettings &settings() const = 0;
        virtual void setSettings(SessionSettings settings) = 0;

        virtual QStringList categories() const = 0;
        virtual CategoryOptions categoryOptions(const QString &categoryName) const = 0;
        virtual bool setCategoryOptions(const QString &categoryName, const CategoryOptions &options) = 0;
        virtual Path categorySavePath(const QString &categoryName) const = 0;
        virtual Path categorySavePath(const QString &categoryName, const CategoryOptions &options) const = 0;
        virtual Path categoryDownloadPath(const QString &categoryName) const = 0;
        virtual Path categoryDownloadPath(const QString &categoryName, const CategoryOptions &options) const = 0;
        virtual ShareLimits categoryShareLimits(const QString &categoryName) const = 0;
        virtual bool addCategory(const QString &name, const CategoryOptions &options = {}) = 0;
        virtual bool removeCategory(const QString &name) = 0;

        virtual Path suggestedSavePath(const QString &categoryName, std::optional<bool> useAutoTMM) const = 0;
        virtual Path suggestedDownloadPath(const QString &categoryName, std::optional<bool> useAutoTMM) const = 0;

        virtual TagSet tags() const = 0;
        virtual bool hasTag(const Tag &tag) const = 0;
        virtual bool addTag(const Tag &tag) = 0;
        virtual bool removeTag(const Tag &tag) = 0;

        // Torrent Management Mode subsystem (TMM)
        //
        // Each torrent can be either in Manual mode or in Automatic mode
        // In Manual Mode various torrent properties are set explicitly(eg save path)
        // In Automatic Mode various torrent properties are set implicitly(eg save path)
        //     based on the associated category.
        // In Automatic Mode torrent save path can be changed in following cases:
        //     1. Default save path changed
        //     2. Torrent category save path changed
        //     3. Torrent category changed
        //     (unless otherwise is specified)

        virtual QString additionalTrackersFromURL() const = 0;

        virtual bool isRestored() const = 0;

        virtual bool isPaused() const = 0;
        virtual void pause() = 0;
        virtual void resume() = 0;

        virtual void applyFilenameFilter(const PathList &files, QList<DownloadPriority> &priorities) = 0;

        virtual int downloadSpeedLimit() const = 0;
        virtual int uploadSpeedLimit() const = 0;

        virtual QStringList bannedIPs() const = 0;
        virtual void setBannedIPs(const QStringList &newList) = 0;

        virtual bool isAltGlobalSpeedLimitEnabled() const = 0;
        virtual void setAltGlobalSpeedLimitEnabled(bool enabled) = 0;

        virtual void reannounceToAllTrackers() const = 0;

        virtual Torrent *getTorrent(const TorrentID &id) const = 0;
        virtual Torrent *findTorrent(const InfoHash &infoHash) const = 0;
        virtual QList<Torrent *> torrents() const = 0;
        virtual qsizetype torrentsCount() const = 0;
        virtual const SessionStatus &status() const = 0;
        virtual const CacheStatus &cacheStatus() const = 0;
        virtual bool isListening() const = 0;

        virtual void banIP(const QString &ip) = 0;

        virtual bool isKnownTorrent(const InfoHash &infoHash) const = 0;
        virtual bool addTorrent(const TorrentDescriptor &torrentDescr, const AddTorrentParams &params = {}) = 0;
        virtual bool removeTorrent(const TorrentID &id, TorrentRemoveOption deleteOption = TorrentRemoveOption::KeepContent) = 0;
        virtual bool downloadMetadata(const TorrentDescriptor &torrentDescr) = 0;
        virtual bool cancelDownloadMetadata(const TorrentID &id) = 0;

        virtual void increaseTorrentsQueuePos(const QList<TorrentID> &ids) = 0;
        virtual void decreaseTorrentsQueuePos(const QList<TorrentID> &ids) = 0;
        virtual void topTorrentsQueuePos(const QList<TorrentID> &ids) = 0;
        virtual void bottomTorrentsQueuePos(const QList<TorrentID> &ids) = 0;

        virtual QString lastExternalIPv4Address() const = 0;
        virtual QString lastExternalIPv6Address() const = 0;

        virtual qint64 freeDiskSpace() const = 0;

    signals:
        void startupProgressUpdated(int progress);
        void addTorrentFailed(const InfoHash &infoHash, const AddTorrentError &reason);
        void allTorrentsFinished();
        void categoryAdded(const QString &categoryName);
        void categoryRemoved(const QString &categoryName);
        void categoryOptionsChanged(const QString &categoryName);
        void fullDiskError(Torrent *torrent, const QString &msg);
        void IPFilterParsed(bool error, int ruleCount);
        void metadataDownloaded(const TorrentInfo &info);
        void restored();
        void paused();
        void resumed();
        void speedLimitModeChanged(bool alternative);
        void statsUpdated();
        void subcategoriesSupportChanged();
        void tagAdded(const Tag &tag);
        void tagRemoved(const Tag &tag);
        void torrentAboutToBeRemoved(Torrent *torrent);
        void torrentAdded(Torrent *torrent);
        void torrentCategoryChanged(Torrent *torrent, const QString &oldCategory);
        void torrentFinished(Torrent *torrent);
        void torrentFinishedChecking(Torrent *torrent);
        void torrentMetadataReceived(Torrent *torrent);
        void torrentStopped(Torrent *torrent);
        void torrentStarted(Torrent *torrent);
        void torrentSavePathChanged(Torrent *torrent);
        void torrentSavingModeChanged(Torrent *torrent);
        void torrentsLoaded(const QList<Torrent *> &torrents);
        void torrentsUpdated(const QList<Torrent *> &torrents);
        void torrentTagAdded(Torrent *torrent, const Tag &tag);
        void torrentTagRemoved(Torrent *torrent, const Tag &tag);
        void torrentContentFileRenamed(Torrent *torrent, int index, const Path &oldFilePath);
        void torrentContentFolderRenamed(const Path &newFolderPath, const Path &oldFolderPath, const QHash<int, Path> &renamedFiles);
        void torrentContentFolderRenamingFailed(const Path &newFolderPath, const Path &oldFolderPath
                , const QHash<int, Path> &renamedFiles, const QList<int> &failedFileIndexes);
        void trackerError(Torrent *torrent, const QString &tracker);
        void trackersAdded(Torrent *torrent, const QList<TrackerEntry> &trackers);
        void trackersReset(Torrent *torrent, const QList<TrackerEntryStatus> &oldEntries, const QList<TrackerEntry> &newEntries);
        void trackersRemoved(Torrent *torrent, const QStringList &trackers);
        void trackerSuccess(Torrent *torrent, const QString &tracker);
        void trackerWarning(Torrent *torrent, const QString &tracker);
        void trackerEntryStatusesUpdated(Torrent *torrent, const QHash<QString, TrackerEntryStatus> &updatedTrackers);
        void freeDiskSpaceChecked(qint64 result);
    };
}
