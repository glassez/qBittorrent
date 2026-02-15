/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2014-2026  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2018  Mike Tzou (Chocobo1)
 * Copyright (C) 2006  Ishan Arora and Christophe Dumez <chris@qbittorrent.org>
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

#include "connection.h"

#include <QMetaObject>
#include <QTcpSocket>

#include "constants.h"
#include "environment.h"
#include "requestdispatcher.h"
#include "requesthandler.h"
#include "requestparser.h"
#include "responsewriter.h"
#include "utils.h"

using namespace Http;

Connection::Connection(QTcpSocket *socket, RequestDispatcher *requestDispatcher, QObject *parent)
    : QObject(parent)
    , m_socket {socket}
    , m_requestDispatcher {requestDispatcher}
{
    Q_ASSERT(socket);
    Q_ASSERT(requestDispatcher);

    m_socket->setParent(this);
    connect(m_socket, &QAbstractSocket::disconnected, this, &Connection::closed);

    // reserve common size for requests, don't use the max allowed size which is too big for
    // memory constrained platforms
    m_receivedData.reserve(1024 * 1024);

    // reset timer when there are activity
    m_idleTimer.start();
    connect(m_socket, &QIODevice::readyRead, this, [this]
    {
        m_idleTimer.start();
        read();
    });
    connect(m_socket, &QIODevice::bytesWritten, this, [this]
    {
        m_idleTimer.start();
    });
}

void Connection::read()
{
    if (m_isProcessingRequest)
        return;

    const qint64 bytesAvailable = m_socket->bytesAvailable();
    if (bytesAvailable > 0)
    {
        // reuse existing buffer and avoid unnecessary memory allocation/relocation
        const qsizetype previousSize = m_receivedData.size();
        m_receivedData.resize(previousSize + bytesAvailable);
        const qint64 bytesRead = m_socket->read((m_receivedData.data() + previousSize), bytesAvailable);
        if (bytesRead < 0) [[unlikely]]
        {
            m_socket->close();
            return;
        }

        if (bytesRead < bytesAvailable) [[unlikely]]
            m_receivedData.chop(bytesAvailable - bytesRead);
    }

    if (!m_receivedData.isEmpty())
    {
        if (!processRequest())
            return;
    }
}

bool Connection::processRequest()
{
    Q_ASSERT(!m_isProcessingRequest);
    if (m_isProcessingRequest) [[unlikely]]
        return false;

    const RequestParser::ParseResult result = RequestParser::parse(m_receivedData);
    switch (result.status)
    {
    case RequestParser::ParseStatus::OK:
        {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
            m_receivedData.slice(result.frameSize);
#else
            m_receivedData.remove(0, result.frameSize);
#endif

            const Environment env {m_socket->localAddress(), m_socket->localPort(), m_socket->peerAddress(), m_socket->peerPort()};

            auto *responseWriter = new ResponseWriter(*m_socket, result.request);
            if (RequestHandler *requestHandler = m_requestDispatcher->dispatchRequest(result.request, env, responseWriter))
            {
                connect(requestHandler, &RequestHandler::finished, this, [this, requestHandler]
                {
                    requestHandler->deleteLater();
                    m_isProcessingRequest = false;
                    read(); // try to fetch next request
                });
                m_isProcessingRequest = true;
                QMetaObject::invokeMethod(requestHandler, &RequestHandler::processRequest, Qt::QueuedConnection);
            }
            else
            {
                delete responseWriter;
                QMetaObject::invokeMethod(this, &Connection::read, Qt::QueuedConnection);
            }
        }
        break;

    case RequestParser::ParseStatus::Incomplete:
        if (m_receivedData.size() > (RequestParser::MAX_CONTENT_SIZE * 1.1))  // some margin for headers
            sendErrorResponse(413, "Payload Too Large");
        return false;

    case RequestParser::ParseStatus::BadMethod:
        sendErrorResponse(501, "Not Implemented");
        return false;

    case RequestParser::ParseStatus::BadRequest:
        sendErrorResponse(400, "Bad Request");
        return false;

    default:
        Q_UNREACHABLE();
        return false;
    }

    return true;
}

void Connection::sendErrorResponse(const quint16 statusCode, const QByteArray &statusText) const
{
    QByteArray buf;
    buf.reserve(1024);

    // Status Line
    buf.append("HTTP/1.1 ")  // TODO: depends on request
        .append(QByteArray::number(statusCode))
        .append(' ')
        .append(statusText)
        .append(CRLF);

    const HeaderMap headers {
        {HEADER_DATE, httpDate()},
        {HEADER_CONNECTION, u"close"_s}
    };

    for (auto i = headers.constBegin(); i != headers.constEnd(); ++i)
    {
        buf.append(i.key().toLatin1())
            .append(": ")
            .append(i.value().toLatin1())
            .append(CRLF);
    }

    // the first empty line
    buf.append(CRLF);

    m_socket->write(buf);
    m_socket->close();
}

bool Connection::hasExpired(const qint64 timeout) const
{
    return (m_socket->bytesAvailable() == 0)
        && (m_socket->bytesToWrite() == 0)
        && m_idleTimer.hasExpired(timeout);
}
