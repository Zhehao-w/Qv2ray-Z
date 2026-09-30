#pragma once

#include <QString>

namespace Qv2ray::utils::windows
{
    inline QString QuoteCommandLineArgument(const QString &argument)
    {
        QString result = QStringLiteral("\"");
        int backslashes = 0;

        for (const auto ch : argument)
        {
            if (ch == QLatin1Char('\\'))
            {
                ++backslashes;
                continue;
            }

            if (ch == QLatin1Char('"'))
            {
                result += QString(backslashes * 2 + 1, QLatin1Char('\\'));
                result += ch;
                backslashes = 0;
                continue;
            }

            if (backslashes > 0)
            {
                result += QString(backslashes, QLatin1Char('\\'));
                backslashes = 0;
            }
            result += ch;
        }

        if (backslashes > 0)
            result += QString(backslashes * 2, QLatin1Char('\\'));

        result += QLatin1Char('"');
        return result;
    }

    inline QString BuildUrlProtocolCommand(const QString &applicationPath)
    {
        return QuoteCommandLineArgument(applicationPath) + QLatin1Char(' ') + QuoteCommandLineArgument(QStringLiteral("%1"));
    }
} // namespace Qv2ray::utils::windows
