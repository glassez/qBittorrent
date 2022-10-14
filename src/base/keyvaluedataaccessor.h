/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2016-2022  Vladimir Golovnev <glassez@yandex.ru>
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

#include <QString>

#include "keyvaluedatastorage.h"

// This is a thin/handy wrapper over `KeyValueDataStorage`. Use it when store/load value
// rarely occurs, otherwise use `CachedKeyValueDataAccessor`.
template <typename T>
class KeyValueDataAccessor
{
public:
    explicit KeyValueDataAccessor(KeyValueDataStorage *storage, const QString &keyName)
        : m_storage {storage}
        , m_keyName {keyName}
    {
    }

    T get(const T &defaultValue = {}) const
    {
        return m_storage->loadValue(m_keyName, defaultValue);
    }

    void set(const T &value)
    {
        m_storage->storeValue(m_keyName, value);
    }

    operator T() const
    {
        return get();
    }

    KeyValueDataAccessor<T> &operator=(const T &value)
    {
        set(value);
        return *this;
    }

private:
    KeyValueDataStorage *m_storage = nullptr;
    const QString m_keyName;
};

template <typename T>
class CachedKeyValueDataAccessor
{
public:
    explicit CachedKeyValueDataAccessor(KeyValueDataStorage *storage, const QString &keyName, const T &defaultValue = {})
        : m_accessor {storage, keyName}
        , m_cache {m_accessor.get(defaultValue)}
    {
    }

    // The signature of the ProxyFunc should be equivalent to the following:
    // T proxyFunc(const T &a);
    template <typename ProxyFunc>
    explicit CachedKeyValueDataAccessor(KeyValueDataStorage *storage, const QString &keyName, const T &defaultValue, ProxyFunc &&proxyFunc)
        : m_accessor {storage, keyName}
        , m_cache {proxyFunc(m_accessor.get(defaultValue))}
    {
    }

    T get() const
    {
        return m_cache;
    }

    operator T() const
    {
        return get();
    }

    void set(const T &value)
    {
        if (m_cache != value)
        {
            m_accessor.set(value);
            m_cache = value;
        }
    }

    CachedKeyValueDataAccessor<T> &operator=(const T &value)
    {
        set(value);
        return *this;
    }

private:
    KeyValueDataAccessor<T> m_accessor;
    T m_cache;
};
