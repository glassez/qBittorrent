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

#pragma once

#include <optional>
#include <type_traits>

#include <QFuture>
#include <QObject>
#include <QVariant>
#include <QVariantHash>

#include "base/concepts/qflags.h"
#include "base/concepts/stringable.h"
#include "utils/string.h"

// There are 2 ways for class `T` provide serialization support into `DataStorage`:
// 1. If the `T` state is intended for users to edit (via a text editor), then
//    `T` should satisfy `Stringable` concept
// 2. Otherwise, use `Q_DECLARE_METATYPE(T)` and let `QMetaType` handle the serialization
class DataStorage final : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DataStorage)

    DataStorage(const QString &storageName);
    ~DataStorage() override;

public:
    static void initInstance();
    static void freeInstance();
    static DataStorage *instance();

    QFuture<std::optional<QVariant>> fetchValue(const QString &key) const
    {
        return fetchValueImpl(key);
    }

    template <typename T>
    QFuture<std::optional<T>> fetchValue(const QString &key) const
    {
        if constexpr (Stringable<T>)
        {
            return fetchValue<QString>(key).then([](const std::optional<QString> &value) -> std::optional<T>
            {
                if (value)
                    return T {*value};
                return std::nullopt;
            });
        }
        else if constexpr (std::is_enum_v<T>)
        {
            return fetchValue<QString>(key).then([](const std::optional<QString> &value) -> std::optional<T>
            {
                if (value)
                    return Utils::String::toEnum<T>(*value);
                return std::nullopt;
            });
        }
        else if constexpr (IsQFlags<T>)
        {
            return fetchValue<typename T::Int>(key).then([](const std::optional<typename T::Int> &value) -> std::optional<T>
            {
                if (value)
                    return T {*value};
                return std::nullopt;
            });
        }
        else
        {
            return fetchValueImpl(key).then([](const std::optional<QVariant> &value) -> std::optional<T>
            {
                // check if retrieved value is convertible to T
                if (value && value->template canConvert<T>())
                    return value->template value<T>();
                return std::nullopt;
            });
        }
    }

    template <typename T>
    void storeValue(const QString &key, T &&value)
    {
        if constexpr (std::same_as<T, QVariant>)
            storeValueImpl(key, std::forward<T>(value));
        else if constexpr (Stringable<T>)
            storeValueImpl(key, std::forward<T>(value).toString());
        else if constexpr (std::is_enum_v<T>)
            storeValueImpl(key, Utils::String::fromEnum(std::forward<T>(value)));
        else if constexpr (IsQFlags<T>)
            storeValueImpl(key, static_cast<typename T::Int>(std::forward<T>(value)));
        else
            storeValueImpl(key, QVariant::fromValue(std::forward<T>(value)));
    }

    void removeValue(const QString &key);

private:
    QFuture<std::optional<QVariant>> fetchValueImpl(const QString &key) const;
    void storeValueImpl(const QString &key, const QVariant &value);
    void storeValueImpl(const QString &key, QVariant &&value);

    static DataStorage *m_instance;

    class Worker;
    Worker *m_asyncWorker = nullptr;
};
