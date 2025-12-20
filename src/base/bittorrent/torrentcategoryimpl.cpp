/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2025  Vladimir Golovnev <glassez@yandex.ru>
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

#include "torrentcategoryimpl.h"

#include <ranges>

#include "base/bittorrent/torrentimpl.h"
#include "base/global.h"

BitTorrent::TorrentCategoryImpl::TorrentCategoryImpl(const QString &name, TorrentCategory *parent, const CategoryOptions &options)
    : TorrentCategory(parent)
    , m_parent {parent}
    , m_name {name}
    , m_fullName {parent ? (parent->name() + u'/' + name) : name}
    , m_options {options}
{
}

BitTorrent::TorrentCategoryImpl::~TorrentCategoryImpl()
{
}

QString BitTorrent::TorrentCategoryImpl::name() const
{
    return m_name;
}

QString BitTorrent::TorrentCategoryImpl::fullName() const
{
    return m_fullName;
}

Path BitTorrent::TorrentCategoryImpl::savePath() const
{

}

Path BitTorrent::TorrentCategoryImpl::downloadPath() const
{

}

qreal BitTorrent::TorrentCategoryImpl::ratioLimit() const
{

}

int BitTorrent::TorrentCategoryImpl::seedingTimeLimit() const
{

}

int BitTorrent::TorrentCategoryImpl::inactiveSeedingTimeLimit() const
{

}

bool BitTorrent::TorrentCategoryImpl::contains(const Torrent *torrent) const
{
    Q_ASSERT(torrent);
    if (!torrent)
        return false;

    if (m_torrents.contains(const_cast<Torrent *>(torrent)))
        return true;

    return std::ranges::any_of(m_subcategories, [torrent](const TorrentCategory *subcat)
    {
        return subcat->contains(torrent);
    });
}

bool BitTorrent::TorrentCategoryImpl::contains(const TorrentCategory *subcategory) const
{
    Q_ASSERT(subcategory);
    if (!subcategory)
        return false;

    if (m_subcategories.value(subcategory->name()) == subcategory)
        return true;

    return std::ranges::any_of(m_subcategories, [subcategory](const TorrentCategory *subcat)
    {
        return subcat->contains(subcategory);
    });
}

BitTorrent::ShareLimitAction BitTorrent::TorrentCategoryImpl::shareLimitAction() const
{

}

QSet<BitTorrent::Torrent *> BitTorrent::TorrentCategoryImpl::torrents() const
{
    return m_torrents;
}

QHash<QString, BitTorrent::TorrentCategory *> BitTorrent::TorrentCategoryImpl::subcategories() const
{
    return m_subcategories;
}

void BitTorrent::TorrentCategoryImpl::addTorrent(TorrentImpl *torrent)
{
    Q_ASSERT(torrent->category() == this);
    m_torrents.insert(torrent);
}
