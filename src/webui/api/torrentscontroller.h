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
    APIResult countAction();
    APIResult infoAction(const APIParams &params);
    APIResult propertiesAction(const APIParams &params);
    APIResult trackersAction(const APIParams &params);
    APIResult webseedsAction(const APIParams &params);
    APIResult addWebSeedsAction(const APIParams &params);
    APIResult editWebSeedAction(const APIParams &params);
    APIResult removeWebSeedsAction(const APIParams &params);
    APIResult filesAction(const APIParams &params);
    APIResult pieceHashesAction(const APIParams &params);
    APIResult pieceStatesAction(const APIParams &params);
    APIResult pieceAvailabilityAction(const APIParams &params);
    APIResult startAction(const APIParams &params);
    APIResult stopAction(const APIParams &params);
    APIResult recheckAction(const APIParams &params);
    APIResult reannounceAction(const APIParams &params);
    APIResult renameAction(const APIParams &params);
    APIResult setCommentAction(const APIParams &params);
    APIResult setCategoryAction(const APIParams &params);
    APIResult createCategoryAction(const APIParams &params);
    APIResult editCategoryAction(const APIParams &params);
    APIResult removeCategoriesAction(const APIParams &params);
    APIResult categoriesAction();
    APIResult addTagsAction(const APIParams &params);
    APIResult setTagsAction(const APIParams &params);
    APIResult removeTagsAction(const APIParams &params);
    APIResult createTagsAction(const APIParams &params);
    APIResult deleteTagsAction(const APIParams &params);
    APIResult tagsAction();
    APIResult addAction(const APIParams &params);
    APIResult deleteAction(const APIParams &params);
    APIResult addTrackersAction(const APIParams &params);
    APIResult editTrackerAction(const APIParams &params);
    APIResult removeTrackersAction(const APIParams &params);
    APIResult addPeersAction(const APIParams &params);
    APIResult filePrioAction(const APIParams &params);
    APIResult uploadLimitAction(const APIParams &params);
    APIResult downloadLimitAction(const APIParams &params);
    APIResult setUploadLimitAction(const APIParams &params);
    APIResult setDownloadLimitAction(const APIParams &params);
    APIResult setShareLimitsAction(const APIParams &params);
    APIResult increasePrioAction(const APIParams &params);
    APIResult decreasePrioAction(const APIParams &params);
    APIResult topPrioAction(const APIParams &params);
    APIResult bottomPrioAction(const APIParams &params);
    APIResult setLocationAction(const APIParams &params);
    APIResult setSavePathAction(const APIParams &params);
    APIResult setDownloadPathAction(const APIParams &params);
    APIResult setAutoManagementAction(const APIParams &params);
    APIResult setSuperSeedingAction(const APIParams &params);
    APIResult setForceStartAction(const APIParams &params);
    APIResult toggleSequentialDownloadAction(const APIParams &params);
    APIResult toggleFirstLastPiecePrioAction(const APIParams &params);
    APIResult renameFileAction(const APIParams &params);
    APIResult renameFolderAction(const APIParams &params);
    APIResult exportAction(const APIParams &params);
    APIResult SSLParametersAction(const APIParams &params);
    APIResult setSSLParametersAction(const APIParams &params);
    APIResult fetchMetadataAction(const APIParams &params);
    APIResult cancelFetchMetadataAction(const APIParams &params);
    APIResult parseMetadataAction(const APIParams &params);
    APIResult saveMetadataAction(const APIParams &params);
    APIResult downloadFileAction(const APIParams &params);

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
