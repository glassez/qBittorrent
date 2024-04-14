/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2024  Vladimir Golovnev <glassez@yandex.ru>
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

#include "pluginsengine.h"

#include <lua/lua.hpp>
#include <LuaBridge/LuaBridge.h>
#include <LuaBridge/Vector.h>

#include <QtSystemDetection>
#if defined(Q_OS_WIN)
// #include <memory>
#include <windows.h>
#include <shellapi.h>
#elif defined(Q_OS_UNIX)
// #include <sys/resource.h>
#endif

#include <QDir>
#include <QProcess>

#include "base/3rdparty/expected.hpp"
#include "base/bittorrent/infohash.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrent.h"
#include "base/global.h"
#include "base/logger.h"
#include "base/path.h"
#include "base/profile.h"
#include "base/utils/io.h"
#include "base/utils/string.h"

namespace luabridge
{
    // LuaBridge Stack specialization for `QString`.
    template <>
    struct Stack<QString>
    {
        static Result push(lua_State *luaState, const QString &str)
        {
            lua_pushlstring(luaState, str.toUtf8().constData(), str.size());
            return {};
        }

        static TypeResult<QString> get(lua_State *luaState, int index)
        {
            size_t len;
            if (lua_type(luaState, index) == LUA_TSTRING)
            {
                const char *str = lua_tolstring(luaState, index, &len);
                return QString::fromUtf8(str, len);
            }

            // Lua reference manual:
            // If the value is a number, then lua_tolstring also changes the actual value in the stack
            // to a string. (This change confuses lua_next when lua_tolstring is applied to keys during
            // a table traversal.)
            lua_pushvalue(luaState, index);
            const char *str = lua_tolstring(luaState, -1, &len);
            const auto string = QString::fromUtf8(str, len);
            lua_pop(luaState, 1); // Pop the temporary string
            return string;
        }

        static bool isInstance(lua_State *luaState, int index)
        {
            return lua_type(luaState, index) == LUA_TSTRING;
        }
    };

    // LuaBridge Stack specialization for `Path`.
    template <>
    struct Stack<Path>
    {
        static Result push(lua_State *luaState, const Path &path)
        {
            Stack<QString>::push(luaState, path.toString());
            return {};
        }

        static TypeResult<Path> get(lua_State *luaState, int index)
        {
            return Path(Stack<QString>::get(luaState, index).valueOr(QString()));
        }

        static bool isInstance(lua_State *luaState, int index)
        {
            return lua_type(luaState, index) == LUA_TSTRING;
        }
    };
}

namespace
{
    namespace LuaFunctions
    {
        void log(const QString &message)
        {
            LogMsg(message);
        }

        bool exec(const QString &command)
        {
            // const QString logMsg = tr("Running external program. Torrent: \"%1\". Command: `%2`");
            // const QString logMsgError = tr("Failed to run external program. Torrent: \"%1\". Command: `%2`");

        // The processing sequenece is different for Windows and other OS, this is intentional
#if defined(Q_OS_WIN)
            const std::wstring programWStr = command.toStdWString();

            // Need to split arguments manually because QProcess::startDetached(QString)
            // will strip off empty parameters.
            // E.g. `python.exe "1" "" "3"` will become `python.exe "1" "3"`
            int argCount = 0;
            std::unique_ptr<LPWSTR[], decltype(&::LocalFree)> args {::CommandLineToArgvW(programWStr.c_str(), &argCount), ::LocalFree};

            if (argCount <= 0)
                return false;

            QStringList argList;
            for (int i = 1; i < argCount; ++i)
                argList += QString::fromWCharArray(args[i]);

            QProcess proc;
            proc.setProgram(QString::fromWCharArray(args[0]));
            proc.setArguments(argList);
            proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args)
            {
                if (true/*Preferences::instance()->isAutoRunConsoleEnabled()*/)
                {
                    args->flags |= CREATE_NEW_CONSOLE;
                    args->flags &= ~(CREATE_NO_WINDOW | DETACHED_PROCESS);
                }
                else
                {
                    args->flags |= CREATE_NO_WINDOW;
                    args->flags &= ~(CREATE_NEW_CONSOLE | DETACHED_PROCESS);
                }
                args->inheritHandles = false;
                args->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
                ::CloseHandle(args->startupInfo->hStdInput);
                ::CloseHandle(args->startupInfo->hStdOutput);
                ::CloseHandle(args->startupInfo->hStdError);
                args->startupInfo->hStdInput = nullptr;
                args->startupInfo->hStdOutput = nullptr;
                args->startupInfo->hStdError = nullptr;
            });

            if (proc.startDetached())
                return true;
#else // Q_OS_WIN
            QStringList args = Utils::String::splitCommand(command);

            if (args.isEmpty())
                return false;

            for (QString &arg : args)
            {
                // strip redundant quotes
                if (arg.startsWith(u'"') && arg.endsWith(u'"'))
                    arg = arg.mid(1, (arg.size() - 2));
            }

            const QString exe = args.takeFirst();
            QProcess proc;
            proc.setProgram(exe);
            proc.setArguments(args);

            if (proc.startDetached())
                return true;
#endif
            return false;
        }
    }

    void registerClassTorrent(lua_State *luaState)
    {
        using Torrent = BitTorrent::Torrent;

        auto cls = luabridge::getGlobalNamespace(luaState).beginNamespace("qBittorrent").beginClass<Torrent>("Torrent");
        cls.addProperty("id", +[](const Torrent *torrent) { return torrent->id().toString(); });
        cls.addProperty("infoHashV1", +[](const Torrent *torrent) { return torrent->infoHash().v1().toString(); });
        cls.addProperty("infoHashV2", +[](const Torrent *torrent) { return torrent->infoHash().v2().toString(); });
        cls.addProperty("filesCount", +[](const Torrent *torrent) { return torrent->filesCount(); });
        cls.addProperty("totalSize", &Torrent::totalSize);
        cls.addProperty("name", &Torrent::name);
        cls.addProperty("savePath", &Torrent::savePath);
        cls.addProperty("downloadPath", &Torrent::downloadPath);
        cls.addProperty("rootPath", &Torrent::rootPath);
        cls.addProperty("contentPath", &Torrent::contentPath);
        cls.addProperty("category", &Torrent::category);
        cls.addProperty("tags", +[](const Torrent *torrent) -> std::vector<QString>
        {
            const auto tags = torrent->tags();
            return {tags.cbegin(), tags.cend()};
        });
        cls.addProperty("currentTracker", &Torrent::currentTracker);
        cls.addFunction("stop", &Torrent::stop);
    }

    nonstd::expected<LuaStatePrt, QString> loadPlugin(const Path &path)
    {
        LuaStatePrt luaState {luaL_newstate(), &lua_close};
        if (!luaState)
            nonstd::make_unexpected(PluginsEngine::tr("Failed to allocate Lua state."));

        const auto result = Utils::IO::readFile(path, 1024 * 1024);
        if (!result)
            nonstd::make_unexpected(result.error().message);

        if (luaL_loadstring(luaState.get(), result.value().constData()) != LUA_OK)
            nonstd::make_unexpected(PluginsEngine::tr("Lua error."));

//        luaL_requiref(luaState.get(), LUA_GNAME, luaopen_base, 1);
//        lua_pop(luaState.get(), 1);
        luaL_openlibs(luaState.get());

        if (lua_pcall(luaState.get(), 0, 0, 0) != LUA_OK)
            nonstd::make_unexpected(PluginsEngine::tr("Lua error."));

        using namespace luabridge;

        const LuaRef pluginName = getGlobal(luaState.get(), "name");
        if (!pluginName.isInstance<QString>())
            nonstd::make_unexpected(PluginsEngine::tr("Metadata is missing or invalid."));

        getGlobalNamespace(luaState.get())
            .beginNamespace("qBittorrent")
                .addFunction("log", LuaFunctions::log)
                .addFunction("exec", LuaFunctions::exec);

        registerClassTorrent(luaState.get());

        return luaState;
    }
}

PluginsEngine::PluginsEngine(QObject *parent)
    : QObject(parent)
{
    const QDir pluginsDir {(Profile::instance()->location(SpecialFolder::Data) / Path(u"plugins"_s)).data()};
    for (const QString &file : pluginsDir.entryList({u"*.lua"_s}))
    {
        if (auto result = loadPlugin(Path(pluginsDir.absoluteFilePath(file))))
        {
            m_plugins.push_back(std::move(result).value());
            LogMsg(tr("Loaded plugin. Name: %1.").arg(file.chopped(4)));
        }
        else
        {
            LogMsg(tr("Couldn't load plugin. File: %1. Reason: %2").arg(file, result.error()), Log::WARNING);
        }
    }

    connect(BitTorrent::Session::instance(), &BitTorrent::Session::torrentAdded, this, [this](BitTorrent::Torrent *torrent)
    {
        for (auto &plugin : m_plugins)
        {
            using namespace luabridge;
            try
            {
                const LuaRef func = getGlobal(plugin.get(), "onTorrentAdded");
                if (func.isFunction())
                    func(torrent);
            }
            catch (const LuaException &ex)
            {
                LogMsg(tr("Failed to call the plugin. Plugin: %1. Reason: %2").arg(u"Test"_s, QString::fromStdString(ex.what())), Log::WARNING);
            }
        }
    });
//    connect(BitTorrent::Session::instance(), &BitTorrent::Session::torrentFinished, this, &Application::torrentFinished);
}
