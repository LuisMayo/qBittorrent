/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2019  Prince Gupta <jagannatharjun11@gmail.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If
 * you modify file(s), you may extend this exception to your version of the
 * file(s), but you are not obligated to do so. If you do not wish to do so,
 * delete this exception statement from your version.
 */

#pragma once

#include <QObject>
#include <QString>
#include <QHash>

class UIThemeManager : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(UIThemeManager)

public:
    static void initInstance();
    static void freeInstance();
    static UIThemeManager *instance();

    void applyStyleSheet() const;

    QIcon getIcon(const QString &iconId) const;
    QIcon getIcon(const QString &iconId, const QString &fallback) const;
    QIcon getFlagIcon(const QString &countryIsoCode) const;
    QPixmap getScaledPixmap(const QString &iconId, const QWidget *widget, int baseHeight) const;
    QString getIconPath(const QString &iconId) const;

private:
    using IconMap = QHash<QString, QString>;

    static bool loadIconConfig(const QString &configFile, const QString &iconDir, IconMap &config);
    UIThemeManager(); // singleton clas

    static UIThemeManager *m_instance;
#if (defined(Q_OS_UNIX) && !defined(Q_OS_MACOS))
    bool m_useSystemTheme;
#endif
    QString m_flagsDir;
    IconMap m_iconMap;
    bool m_useCustomUITheme;
};
