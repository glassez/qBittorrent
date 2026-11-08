/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2018  Vladimir Golovnev <glassez@yandex.ru>
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

#include "apicontroller.h"

class RSSController final : public APIController
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(RSSController)

public:
    using APIController::APIController;

private slots:
    APIResult addFolderAction(const APIParams &params);
    APIResult addFeedAction(const APIParams &params);
    APIResult setFeedURLAction(const APIParams &params);
    APIResult setFeedRefreshIntervalAction(const APIParams &params);
    APIResult removeItemAction(const APIParams &params);
    APIResult moveItemAction(const APIParams &params);
    APIResult itemsAction(const APIParams &params);
    APIResult markAsReadAction(const APIParams &params);
    APIResult refreshItemAction(const APIParams &params);
    APIResult setRuleAction(const APIParams &params);
    APIResult exportRulesAction();
    APIResult importRulesAction(const APIParams &params);
    APIResult renameRuleAction(const APIParams &params);
    APIResult removeRuleAction(const APIParams &params);
    APIResult cloneRuleAction(const APIParams &params);
    APIResult rulesAction();
    APIResult matchingArticlesAction(const APIParams &params);
};
