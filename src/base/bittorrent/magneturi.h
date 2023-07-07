/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2015, 2023  Vladimir Golovnev <glassez@yandex.ru>
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

#include <libtorrent/add_torrent_params.hpp>

#include <QString>
#include <QUrl>
#include <QVector>

#include "base/3rdparty/expected.hpp"
#include "infohash.h"
#include "torrentdescriptor.h"
#include "trackerentry.h"

namespace BitTorrent
{
    class MagnetURI final : public TorrentDescriptor
    {
    public:
        static nonstd::expected<std::shared_ptr<MagnetURI>, QString> parse(const QString &uri) noexcept;

        InfoHash infoHash() const override;
        QString name() const override;
        QVector<TrackerEntry> trackers() const override;
        QVector<QUrl> urlSeeds() const override;

        QString url() const;

        lt::add_torrent_params ltAddTorrentParams() const override;

    private:
        explicit MagnetURI(const QString &uri);

        Type type() const override;

        QString m_uri;
        InfoHash m_infoHash;
        QString m_name;
        QVector<TrackerEntry> m_trackers;
        QVector<QUrl> m_urlSeeds;
        lt::add_torrent_params m_ltAddTorrentParams;
    };
}
