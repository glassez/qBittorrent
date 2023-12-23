/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2023  Vladimir Golovnev <glassez@yandex.ru>
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

#include "requesthandler.h"

#include "base/http/types.h"

namespace Http
{
    struct RequestHandler::RequestItem
    {
        qint64 id;
        Request request;
        Environment env;
    };
}

qint64 Http::RequestHandler::processRequest(const Request &request, const Environment &env)
{
    const qint64 id = m_nextID;
    m_nextID = (m_nextID < std::numeric_limits<qint64>::max()) ? ++m_nextID : 0;

    m_queuedRequests.enqueue({id, request, env});
    if (m_queuedRequests.size() == 1)
        doProcessRequest(request, env);

    return id;
}

void Http::RequestHandler::onRequestProcessingDone(const Response &response)
{
    Q_ASSERT(!m_queuedRequests.isEmpty());
    if (m_queuedRequests.isEmpty()) [[unlikely]]
        return;

    const auto item = m_queuedRequests.dequeue();
    emit requestProcessingDone(item.id, response);

    if (!m_queuedRequests.isEmpty())
    {
        const auto &item = m_queuedRequests.head();
        doProcessRequest(item.request, item.env);
    }
}
