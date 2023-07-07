/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2015-2023  Vladimir Golovnev <glassez@yandex.ru>
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

#include "torrentfile.h"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/write_resume_data.hpp>

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QUrl>

#include "base/global.h"
#include "base/utils/io.h"
#include "common.h"
#include "infohash.h"
#include "trackerentry.h"

namespace
{
    constexpr lt::load_torrent_limits loadTorrentLimits()
    {
        lt::load_torrent_limits limits;
        limits.max_buffer_size = MAX_TORRENT_SIZE;
        limits.max_decode_depth = BENCODE_DEPTH_LIMIT;
        limits.max_decode_tokens = BENCODE_TOKEN_LIMIT;

        return limits;
    }
}

nonstd::expected<std::shared_ptr<BitTorrent::TorrentFile>, QString>
BitTorrent::TorrentFile::load(const QByteArray &data) noexcept
try
{
    return std::shared_ptr<TorrentFile>(new TorrentFile(data));
}
catch (const lt::system_error &err)
{
    return nonstd::make_unexpected(QString::fromLocal8Bit(err.what()));
}

nonstd::expected<std::shared_ptr<BitTorrent::TorrentFile>, QString>
BitTorrent::TorrentFile::loadFromFile(const Path &path) noexcept
try
{
    return std::shared_ptr<TorrentFile>(new TorrentFile(path));
}
catch (const lt::system_error &err)
{
    return nonstd::make_unexpected(QString::fromLocal8Bit(err.what()));
}

nonstd::expected<void, QString> BitTorrent::TorrentFile::saveToFile(const Path &path) const
try
{
    const lt::entry torrentEntry = lt::write_torrent_file(m_ltAddTorrentParams);
    const nonstd::expected<void, QString> result = Utils::IO::saveToFile(path, torrentEntry);
    if (!result)
        return result.get_unexpected();

    return {};
}
catch (const lt::system_error &err)
{
    return nonstd::make_unexpected(QString::fromLocal8Bit(err.what()));
}

BitTorrent::TorrentDescriptor::Type BitTorrent::TorrentFile::type() const
{
    return TorrentDescriptor::TorrentFile;
}

BitTorrent::TorrentFile::TorrentFile(lt::add_torrent_params ltAddTorrentParams)
    : m_ltAddTorrentParams {std::move(ltAddTorrentParams)}
    , m_info {*m_ltAddTorrentParams.ti}
{
}

BitTorrent::TorrentFile::TorrentFile(Path source)
    : TorrentFile(lt::load_torrent_file(source.toString().toStdString(), loadTorrentLimits()))
{
    m_source = std::move(source);
}

BitTorrent::TorrentFile::TorrentFile(const QByteArray &data)
    : TorrentFile(lt::load_torrent_buffer(lt::span<const char>(data.data(), data.size()), loadTorrentLimits()))
{
}

BitTorrent::InfoHash BitTorrent::TorrentFile::infoHash() const
{
#ifdef QBT_USES_LIBTORRENT2
    return m_ltAddTorrentParams.info_hashes;
#else
    return m_ltAddTorrentParams.info_hash;
#endif
}

QString BitTorrent::TorrentFile::name() const
{
    return m_info.name();
}

QDateTime BitTorrent::TorrentFile::creationDate() const
{
    return ((m_ltAddTorrentParams.ti->creation_date() != 0)
            ? QDateTime::fromSecsSinceEpoch(m_ltAddTorrentParams.ti->creation_date()) : QDateTime());
}

QString BitTorrent::TorrentFile::creator() const
{
    return QString::fromStdString(m_ltAddTorrentParams.ti->creator());
}

QString BitTorrent::TorrentFile::comment() const
{
    return QString::fromStdString(m_ltAddTorrentParams.ti->comment());
}

const BitTorrent::TorrentInfo &BitTorrent::TorrentFile::info() const
{
    return m_info;
}

QVector<BitTorrent::TrackerEntry> BitTorrent::TorrentFile::trackers() const
{
    QVector<TrackerEntry> ret;
    ret.reserve(static_cast<decltype(ret)::size_type>(m_ltAddTorrentParams.trackers.size()));
    std::size_t i = 0;
    for (const std::string &tracker : m_ltAddTorrentParams.trackers)
        ret.append({QString::fromStdString(tracker), m_ltAddTorrentParams.tracker_tiers[i++]});

    return ret;
}

QVector<QUrl> BitTorrent::TorrentFile::urlSeeds() const
{
    // TODO: Check it!!!

    QVector<QUrl> urlSeeds;
    urlSeeds.reserve(static_cast<decltype(urlSeeds)::size_type>(m_ltAddTorrentParams.url_seeds.size()));

    for (const std::string &nativeURLSeed : m_ltAddTorrentParams.url_seeds)
        urlSeeds.append(QUrl(QString::fromStdString(nativeURLSeed)));

    return urlSeeds;
}

const Path &BitTorrent::TorrentFile::source() const
{
    return m_source;
}

libtorrent::add_torrent_params BitTorrent::TorrentFile::ltAddTorrentParams() const
{
    return m_ltAddTorrentParams;
}
