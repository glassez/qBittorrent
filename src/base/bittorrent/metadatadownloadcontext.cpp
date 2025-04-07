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

#include "metadatadownloadcontext.h"

#include "metadatadownloadhandlerimpl.h"

BitTorrent::MetadataDownloadContext::MetadataDownloadContext(MetadataDownloadHandlerImpl *handler, QObject *parent)
    : QObject(parent)
{
    addHandler(handler);
}

lt::torrent_handle BitTorrent::MetadataDownloadContext::torrentHandle() const
{
    return m_torrentHandle;
}

void BitTorrent::MetadataDownloadContext::setTorrentHandle(lt::torrent_handle torrentHandle)
{
    m_torrentHandle = std::move(torrentHandle);
}

bool BitTorrent::MetadataDownloadContext::isFinished() const
{
    return m_isFinished;
}

std::optional<BitTorrent::MetadataDownloadResult> BitTorrent::MetadataDownloadContext::result() const
{
    return m_result;
}

void BitTorrent::MetadataDownloadContext::addHandler(MetadataDownloadHandlerImpl *handler)
{
    Q_ASSERT(handler);
    Q_ASSERT(!m_handlers.contains(handler));
    m_handlers.insert(handler);
    handler->setContext(this);
    connect(handler, &QObject::destroyed, this, [this, handler]
    {
        m_handlers.remove(handler);
        if (m_handlers.isEmpty())
            emit unreferenced();
    });
}

void BitTorrent::MetadataDownloadContext::setMetadata(TorrentInfo torrentInfo)
{
    Q_ASSERT(!m_isFinished);
    if (m_isFinished) [[unlikely]]
        return;

    finish({MetadataDownloadResult::Succeeded, std::move(torrentInfo)});
}

void BitTorrent::MetadataDownloadContext::setError()
{
    finish({MetadataDownloadResult::Failed, {}});
}

void BitTorrent::MetadataDownloadContext::finish(MetadataDownloadResult result)
{
    if (m_isFinished) [[unlikely]]
        return;

    m_torrentHandle = {};
    m_result = std::move(result);
    m_isFinished = true;
}
