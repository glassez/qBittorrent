/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2018-2026  Vladimir Golovnev <glassez@yandex.ru>
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
#include <QSet>

#include "base/bittorrent/torrentdescriptor.h"
#include "apicontroller.h"

class QByteArray;

namespace BitTorrent
{
    class InfoHash;
    class TorrentID;
    class TorrentInfo;
}

namespace Net
{
    struct DownloadResult;
}

class TorrentsController : public APIController
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(TorrentsController)

public:
    explicit TorrentsController(IApplication *app, QObject *parent = nullptr);

private slots:
    void countAction();
    void infoAction(const StringMap &params);
    void propertiesAction(const StringMap &params);
    void trackersAction(const StringMap &params);
    void webseedsAction(const StringMap &params);
    void addWebSeedsAction(const StringMap &params);
    void editWebSeedAction(const StringMap &params);
    void removeWebSeedsAction(const StringMap &params);
    void filesAction(const StringMap &params);
    void pieceHashesAction(const StringMap &params);
    void pieceStatesAction(const StringMap &params);
    void pieceAvailabilityAction(const StringMap &params);
    void startAction(const StringMap &params);
    void stopAction(const StringMap &params);
    void recheckAction(const StringMap &params);
    void reannounceAction(const StringMap &params);
    void renameAction(const StringMap &params);
    void setCommentAction(const StringMap &params);
    void setCategoryAction(const StringMap &params);
    void createCategoryAction(const StringMap &params);
    void editCategoryAction(const StringMap &params);
    void removeCategoriesAction(const StringMap &params);
    void categoriesAction();
    void addTagsAction(const StringMap &params);
    void setTagsAction(const StringMap &params);
    void removeTagsAction(const StringMap &params);
    void createTagsAction(const StringMap &params);
    void deleteTagsAction(const StringMap &params);
    void tagsAction();
    void addAction(const StringMap &params, const DataList &data);
    void deleteAction(const StringMap &params);
    void addTrackersAction(const StringMap &params);
    void editTrackerAction(const StringMap &params);
    void removeTrackersAction(const StringMap &params);
    void addPeersAction(const StringMap &params);
    void filePrioAction(const StringMap &params);
    void uploadLimitAction(const StringMap &params);
    void downloadLimitAction(const StringMap &params);
    void setUploadLimitAction(const StringMap &params);
    void setDownloadLimitAction(const StringMap &params);
    void setShareLimitsAction(const StringMap &params);
    void increasePrioAction(const StringMap &params);
    void decreasePrioAction(const StringMap &params);
    void topPrioAction(const StringMap &params);
    void bottomPrioAction(const StringMap &params);
    void setLocationAction(const StringMap &params);
    void setSavePathAction(const StringMap &params);
    void setDownloadPathAction(const StringMap &params);
    void setAutoManagementAction(const StringMap &params);
    void setSuperSeedingAction(const StringMap &params);
    void setForceStartAction(const StringMap &params);
    void toggleSequentialDownloadAction(const StringMap &params);
    void toggleFirstLastPiecePrioAction(const StringMap &params);
    void renameFileAction(const StringMap &params);
    void renameFolderAction(const StringMap &params);
    void exportAction(const StringMap &params);
    void SSLParametersAction(const StringMap &params);
    void setSSLParametersAction(const StringMap &params);
    void fetchMetadataAction(const StringMap &params);
    void cancelFetchMetadataAction(const StringMap &params);
    void parseMetadataAction(const StringMap &params, const DataList &data);
    void saveMetadataAction(const StringMap &params);
    void downloadFileAction(const StringMap &params);

private:
    void onDownloadFinished(const Net::DownloadResult &result);
    void onMetadataDownloaded(const BitTorrent::TorrentInfo &info);
    void onSearchPluginTorrentDownloaded(const QString &source, const QString &data);
    void cacheTorrentFile(const QString &source, const QByteArray &data);
    void cacheMagnetURI(const QString &source, const BitTorrent::TorrentDescriptor &torrentDescr);

    QHash<QString, BitTorrent::InfoHash> m_torrentSourceCache;
    QHash<BitTorrent::TorrentID, BitTorrent::TorrentDescriptor> m_torrentMetadataCache;
    QSet<QString> m_requestedTorrentSource;
    QSet<QString> m_cancelledTorrentSource;
    QSet<QString> m_invalidTorrentSource;
};
