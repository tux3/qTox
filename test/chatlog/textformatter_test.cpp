/*
    Copyright © 2017 by The qTox Project Contributors

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

#include "src/chatlog/textformatter.h"
#include "test/common.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QVector>
#include <QVector>

#include <check.h>
#include <ctime>

using StringToString = QMap<QString, QString>;

static const StringToString signsToTags{{"*", "b"}, {"**", "b"}, {"/", "i"}};

static const StringToString
    commonWorkCases{// Basic
                    {QStringLiteral("%1a%1"), QStringLiteral("<%2>%1a%1</%2>")},
                    {QStringLiteral("%1aa%1"), QStringLiteral("<%2>%1aa%1</%2>")},
                    {QStringLiteral("%1aaa%1"), QStringLiteral("<%2>%1aaa%1</%2>")},

                    // Additional text from both sides
                    {QStringLiteral("aaa%1a%1"), QStringLiteral("aaa<%2>%1a%1</%2>")},
                    {QStringLiteral("%1a%1aaa"), QStringLiteral("<%2>%1a%1</%2>aaa")},

                    // Must allow same formatting more than one time, divided by two and more
                    // symbols due to QRegularExpressionIterator
                    {QStringLiteral("%1aaa%1 aaa %1aaa%1"),
                     QStringLiteral("<%2>%1aaa%1</%2> aaa <%2>%1aaa%1</%2>")}};

static const QVector<QString>
    commonExceptions{// No whitespaces near to formatting symbols from both sides
                     QStringLiteral("%1 a%1"), QStringLiteral("%1a %1"),

                     // No newlines
                     QStringLiteral("%1aa\n%1"),

                     // Only exact combinations of symbols must encapsulate formatting string
                     QStringLiteral("%1%1aaa%1"), QStringLiteral("%1aaa%1%1")};

static const StringToString singleSlash{
    // Must work with inserted tags
    {QStringLiteral("/aaa<b>aaa aaa</b>/"), QStringLiteral("<i>aaa<b>aaa aaa</b></i>")}};

static const StringToString doubleSign{
    {QStringLiteral("**aaa * aaa**"), QStringLiteral("<b>aaa * aaa</b>")}};

static const StringToString mixedFormatting{
    // Must allow mixed formatting if there is no tag overlap in result
    {QStringLiteral("aaa *aaa /aaa/ aaa*"), QStringLiteral("aaa <b>aaa <i>aaa</i> aaa</b>")},
    {QStringLiteral("aaa *aaa /aaa* aaa/"), QStringLiteral("aaa <b>aaa /aaa</b> aaa/")}};

static const StringToString multilineCode{
    // Must allow newlines
    {QStringLiteral("```int main()\n{\n    return 0;\n}```"),
     QStringLiteral("<font color=#595959><code>int main()\n{\n    return 0;\n}</code></font>")}};

static const StringToString urlCases {
    {QStringLiteral("https://github.com/qTox/qTox/issues/4233"),
     QStringLiteral("<a href=\"https://github.com/qTox/qTox/issues/4233\">"
                    "https://github.com/qTox/qTox/issues/4233</a>")},
    {QStringLiteral("No conflicts with /italic https://github.com/qTox/qTox/issues/4233 font/"),
     QStringLiteral("No conflicts with <i>italic "
                    "<a href=\"https://github.com/qTox/qTox/issues/4233\">"
                    "https://github.com/qTox/qTox/issues/4233</a> font</i>")},
    {QStringLiteral("www.youtube.com"),
     QStringLiteral("<a href=\"http://www.youtube.com\">www.youtube.com</a>")},
    {QStringLiteral("https://url.com/some*url/some*more*url/"),
     QStringLiteral("<a href=\"https://url.com/some*url/some*more*url/\">"
                    "https://url.com/some*url/some*more*url/</a>")},
    {QStringLiteral("https://url.com/some_url/some_more_url/"),
     QStringLiteral("<a href=\"https://url.com/some_url/some_more_url/\">"
                    "https://url.com/some_url/some_more_url/</a>")},
    {QStringLiteral("https://url.com/some~url/some~more~url/"),
     QStringLiteral("<a href=\"https://url.com/some~url/some~more~url/\">"
                    "https://url.com/some~url/some~more~url/</a>")},
    {QStringLiteral("https://url.com/some`url/some`more`url/"),
     QStringLiteral("<a href=\"https://url.com/some`url/some`more`url/\">"
                    "https://url.com/some`url/some`more`url/</a>")},
    // Test case from issue #4275
    {QStringLiteral("http://www.metacritic.com/game/pc/mass-effect-andromeda\n"
                    "http://www.metacritic.com/game/playstation-4/mass-effect-andromeda\n"
                    "http://www.metacritic.com/game/xbox-one/mass-effect-andromeda"),
     QStringLiteral("<a href=\"http://www.metacritic.com/game/pc/mass-effect-andromeda\">"
                    "http://www.metacritic.com/game/pc/mass-effect-andromeda</a>\n"
                    "<a href=\"http://www.metacritic.com/game/playstation-4/mass-effect-andromeda\""
                    ">http://www.metacritic.com/game/playstation-4/mass-effect-andromeda</a>\n"
                    "<a href=\"http://www.metacritic.com/game/xbox-one/mass-effect-andromeda\">"
                    "http://www.metacritic.com/game/xbox-one/mass-effect-andromeda</a>")},
    {QStringLiteral("http://site.com/part1/part2 "
                    "http://site.com/part3 "
                    "and one more time "
                    "www.site.com/part1/part2"),
     QStringLiteral("<a href=\"http://site.com/part1/part2\">http://site.com/part1/part2</a> "
                    "<a href=\"http://site.com/part3\">http://site.com/part3</a> "
                    "and one more time "
                    "<a href=\"http://www.site.com/part1/part2\">www.site.com/part1/part2</a>")},
};

/**
 * @brief Testing cases which are common for all types of formatting except multiline code
 * @param noSymbols True if it's not allowed to show formatting symbols
 * @param map Grouped cases
 * @param signs Combination of formatting symbols
 */
static void commonTest(bool showSymbols, const StringToString map, const QString signs)
{
    for (QString key : map.keys()) {
        QString source = key.arg(signs);
        TextFormatter tf = TextFormatter(source);
        QString result = map[key].arg(showSymbols ? signs : "", signsToTags[signs]);
        ck_assert(tf.applyStyling(showSymbols) == result);
    }
}

/**
 * @brief Testing exception cases
 * @param signs Combination of formatting symbols
 */
static void commonExceptionsTest(const QString signs)
{
    for (QString source : commonExceptions) {
        TextFormatter tf = TextFormatter(source.arg(signs));
        ck_assert(tf.applyStyling(false) == source.arg(signs));
    }
}

/**
 * @brief Testing some uncommon, special cases
 * @param map Grouped cases
 */
static void specialTest(const StringToString map)
{
    for (QString key : map.keys()) {
        TextFormatter tf = TextFormatter(key);
        ck_assert(tf.applyStyling(false) == map[key]);
    }
}

START_TEST(singleSignNoSymbolsTest)
{
    commonTest(false, commonWorkCases, "*");
}
END_TEST

START_TEST(slashNoSymbolsTest)
{
    commonTest(false, commonWorkCases, "/");
}
END_TEST

START_TEST(doubleSignNoSymbolsTest)
{
    commonTest(false, commonWorkCases, "**");
}
END_TEST

START_TEST(singleSignWithSymbolsTest)
{
    commonTest(true, commonWorkCases, "*");
}
END_TEST

START_TEST(slashWithSymbolsTest)
{
    commonTest(true, commonWorkCases, "/");
}
END_TEST

START_TEST(doubleSignWithSymbolsTest)
{
    commonTest(true, commonWorkCases, "**");
}
END_TEST

START_TEST(singleSignExceptionsTest)
{
    commonExceptionsTest("*");
}
END_TEST

START_TEST(slashExceptionsTest)
{
    commonExceptionsTest("/");
}
END_TEST

START_TEST(doubleSignExceptionsTest)
{
    commonExceptionsTest("**");
}
END_TEST

START_TEST(slashSpecialTest)
{
    specialTest(singleSlash);
}
END_TEST

START_TEST(doubleSignSpecialTest)
{
    specialTest(doubleSign);
}
END_TEST

START_TEST(mixedFormattingTest)
{
    specialTest(mixedFormatting);
}
END_TEST

START_TEST(multilineCodeTest)
{
    specialTest(multilineCode);
}
END_TEST

START_TEST(urlTest)
{
    specialTest(urlCases);
}
END_TEST

static Suite* textFormatterSuite(void)
{
    Suite* s = suite_create("TextFormatter");

    DEFTESTCASE(singleSignNoSymbols);
    DEFTESTCASE(slashNoSymbols);
    DEFTESTCASE(doubleSignNoSymbols);
    DEFTESTCASE(singleSignWithSymbols);
    DEFTESTCASE(slashWithSymbols);
    DEFTESTCASE(doubleSignWithSymbols);
    DEFTESTCASE(singleSignExceptions);
    DEFTESTCASE(slashExceptions);
    DEFTESTCASE(doubleSignExceptions);
    DEFTESTCASE(slashSpecial);
    DEFTESTCASE(doubleSignSpecial);
    DEFTESTCASE(mixedFormatting);
    DEFTESTCASE(multilineCode);
    DEFTESTCASE(url);

    return s;
}

int main(int argc, char* argv[])
{
    srand((unsigned int)time(NULL));

    Suite* tf = textFormatterSuite();
    SRunner* runner = srunner_create(tf);
    srunner_run_all(runner, CK_NORMAL);

    int res = srunner_ntests_failed(runner);
    srunner_free(runner);

    return res;
}
