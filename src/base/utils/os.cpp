/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2020  Mike Tzou (Chocobo1)
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

#include "os.h"

namespace
{
#ifdef Q_OS_WIN
    QString makeProfileID(const Path &profilePath, const QString &profileName)
    {
        return profilePath.isEmpty()
                ? profileName
                : profileName + u'@' + Utils::Fs::toValidFileName(profilePath.data(), {});
    }
#endif
}

#ifdef Q_OS_WIN
    bool hasWindowsStartupEntry() const
    {
        const QString profileName = Profile::instance()->profileName();
        const Path profilePath = Profile::instance()->rootPath();
        const QString profileID = makeProfileID(profilePath, profileName);
        const QSettings settings {u"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"_s, QSettings::NativeFormat};

        return settings.contains(profileID);
    }

    void setWindowsStartupEntry(const bool b)
    {
        const QString profileName = Profile::instance()->profileName();
        const Path profilePath = Profile::instance()->rootPath();
        const QString profileID = makeProfileID(profilePath, profileName);
        QSettings settings {u"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"_s, QSettings::NativeFormat};
        if (b)
        {
            const QString configuration = Profile::instance()->configurationName();

            const auto cmd = uR"("%1" "--profile=%2" "--configuration=%3")"_s
                                 .arg(Path(qApp->applicationFilePath()).toString(), profilePath.toString(), configuration);
            settings.setValue(profileID, cmd);
        }
        else
        {
            settings.remove(profileID);
        }
    }
#endif
