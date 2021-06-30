/*
    Copyright © 2014-2019 by The qTox Project Contributors

    This file is part of qTox, a Qt-based graphical interface for Tox.

    qTox is libre software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    qTox is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with qTox.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <QColor>
#include <QFont>

class QString;
class QWidget;

class Style
{
public:
    enum ColorPalette
    {
        TransferGood,
        TransferWait,
        TransferBad,
        TransferMiddle,
        MainText,
        NameActive,
        StatusActive,
        GroundExtra,
        GroundBase,
        Orange,
        Yellow,
        ThemeDark,
        ThemeMediumDark,
        ThemeMedium,
        ThemeLight,
        Action,
        Link,
        SearchHighlighted,
        SelectText
    };

    enum Font
    {
        ExtraBig,
        Big,
        BigBold,
        Medium,
        MediumBold,
        Small,
        SmallLight
    };

    enum MainTheme
    {
        Light,
        Dark
    };

    struct ThemeNameColor {
        MainTheme type;
        QString name;
        QColor color;
    };

    static QStringList getThemeColorNames();
    static const QString getStylesheet(const QString& filename, const QFont& baseFont = QFont());
    static const QString getImagePath(const QString& filename);
    static QString getThemeFolder();
    static QString getThemeName();
    static QColor getColor(ColorPalette entry);
    static QFont getFont(Font font);
    static const QString resolve(const QString& filename, const QFont& baseFont = QFont());
    static void repolish(QWidget* w);
    static void setThemeColor();
    static void setThemeColor(const QColor& color);
    static void setExtraThemeColor(const QColor& color);
    static void applyTheme();
    static QPixmap scaleSvgImage(const QString& path, uint32_t width, uint32_t height);
    static void initPalette();
    static void initDictColor();
    static QString getThemePath();

signals:
    void themeChanged();

private:
    Style();

private:
    static void init();
};
