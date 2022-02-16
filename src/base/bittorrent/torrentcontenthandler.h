/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2022  Vladimir Golovnev <glassez@yandex.ru>
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

#include <QObject>

#include "abstractfilestorage.h"
#include "downloadpriority.h"

namespace BitTorrent
{
    class TorrentContentHandler
            : public QObject
            , public AbstractFileStorage
    {
        Q_OBJECT
        Q_DISABLE_COPY(TorrentContentHandler)

    public:
        using QObject::QObject;

        virtual bool hasMetadata() const = 0;

        virtual QVector<BitTorrent::DownloadPriority> filePriorities() const = 0;
        virtual DownloadPriority filePriority(int index) const = 0;
        virtual void setFilePriority(int index, DownloadPriority priority) = 0;

        virtual QVector<qreal> filesProgress() const = 0;

        /**
         * @brief fraction of file pieces that are available at least from one peer
         *
         * This is not the same as torrent availability, it is just a fraction of pieces
         * that can be downloaded right now. It varies between 0 to 1.
         */
        virtual QVector<qreal> availableFileFractions() const = 0;

    signals:
        void metadataReceived();
        void filePriorityChanged(int index, DownloadPriority priority);
        void fileRenamed(int index, const Path &path);
        void stateUpdated();
    };
}
