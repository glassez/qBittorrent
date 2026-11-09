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

#include "apicontroller.h"

#include <QMetaMethod>
#include <QMetaObject>

using namespace Qt::Literals::StringLiterals;

APIController::APIController(IApplication *app, QObject *parent)
    : ApplicationComponent(app, parent)
{
}

nonstd::expected<APIResult, APIError> APIController::run(const QString &action, const APIParams &params)
{
    const QByteArray methodName = action.toLatin1() + "Action";
    const QMetaObject *thisMetaObject = metaObject();
    for (int indexOfMethod = 0; indexOfMethod < thisMetaObject->methodCount(); ++indexOfMethod)
    {
        QMetaMethod metaMethod = thisMetaObject->method(indexOfMethod);
        if (metaMethod.name() == methodName)
        {
            if (const auto checkResult = checkRequiredParams(action, params); !checkResult)
            {
                return nonstd::make_unexpected(APIError(APIErrorType::BadParams
                        , tr("Missing required parameters: %1").arg(checkResult.error().join(u", "))));
            }

            APIResult result;
            bool invoked = false;
            switch (metaMethod.parameterCount())
            {
            case 0:
                invoked = metaMethod.invoke(this, qReturnArg(result));
                break;
            case 1:
                invoked = metaMethod.invoke(this, qReturnArg(result), params);
                break;
            }

            Q_ASSERT_X(invoked, Q_FUNC_INFO, "Action method has no valid signature.");
            return result;
        }
    }

    return nonstd::make_unexpected(APIError(APIErrorType::NotFound, tr("Endpoint does not exist")));
}

void APIController::initializeRequiredParams(QHash<QString, QStringList> requiredParams)
{
    Q_ASSERT(m_requiredParams.isEmpty());
    m_requiredParams = std::move(requiredParams);
}

nonstd::expected<void, QStringList> APIController::checkRequiredParams(const QString &action, const APIParams &params)
{
    const QStringList requiredParams = m_requiredParams.value(action);

    QStringList missingParams;
    missingParams.reserve(requiredParams.size());

    for (const QString &requiredParam : requiredParams)
    {
        if (!params.contains(requiredParam))
            missingParams.append(requiredParam);
    }

    if (!missingParams.isEmpty())
        return nonstd::make_unexpected(missingParams);

    return {};
}
