#pragma once

#include "backend.h"
#include <QStringList>

#ifndef WAFFLEHOUSE_FEATURE_OSCAR
#define WAFFLEHOUSE_FEATURE_OSCAR 1
#endif
#ifndef WAFFLEHOUSE_FEATURE_IRC
#define WAFFLEHOUSE_FEATURE_IRC 1
#endif
#ifndef WAFFLEHOUSE_FEATURE_TELNET
#define WAFFLEHOUSE_FEATURE_TELNET 1
#endif
#ifndef WAFFLEHOUSE_FEATURE_SIP
#define WAFFLEHOUSE_FEATURE_SIP 1
#endif
#ifndef WAFFLEHOUSE_FEATURE_MEDIA
#define WAFFLEHOUSE_FEATURE_MEDIA 1
#endif

#ifndef WAFFLEHOUSE_FEATURE_XMPP
#define WAFFLEHOUSE_FEATURE_XMPP 1
#endif
#ifndef WAFFLEHOUSE_FEATURE_SSH
#define WAFFLEHOUSE_FEATURE_SSH 1
#endif
#ifndef WAFFLEHOUSE_FEATURE_NNTP
#define WAFFLEHOUSE_FEATURE_NNTP 1
#endif
#ifndef WAFFLEHOUSE_FEATURE_MOSH
#define WAFFLEHOUSE_FEATURE_MOSH 1
#endif
#ifndef WAFFLEHOUSE_FEATURE_GOPHER
#define WAFFLEHOUSE_FEATURE_GOPHER 1
#endif
#ifndef WAFFLEHOUSE_FEATURE_GEMINI
#define WAFFLEHOUSE_FEATURE_GEMINI 1
#endif

namespace BuildFeatures {
inline constexpr bool Oscar = WAFFLEHOUSE_FEATURE_OSCAR != 0;
inline constexpr bool Irc = WAFFLEHOUSE_FEATURE_IRC != 0;
inline constexpr bool Telnet = WAFFLEHOUSE_FEATURE_TELNET != 0;
inline constexpr bool Sip = WAFFLEHOUSE_FEATURE_SIP != 0;
inline constexpr bool Media = WAFFLEHOUSE_FEATURE_MEDIA != 0;
inline constexpr bool Xmpp = WAFFLEHOUSE_FEATURE_XMPP != 0;
inline constexpr bool Ssh = WAFFLEHOUSE_FEATURE_SSH != 0;
inline constexpr bool Nntp = WAFFLEHOUSE_FEATURE_NNTP != 0;
inline constexpr bool Mosh = WAFFLEHOUSE_FEATURE_MOSH != 0;
inline constexpr bool Gopher = WAFFLEHOUSE_FEATURE_GOPHER != 0;
inline constexpr bool Gemini = WAFFLEHOUSE_FEATURE_GEMINI != 0;

inline bool protocolEnabled(ConnectionSettings::Protocol protocol)
{
    switch (protocol) {
    case ConnectionSettings::Protocol::Oscar: return Oscar;
    case ConnectionSettings::Protocol::Irc: return Irc;
    case ConnectionSettings::Protocol::Telnet: return Telnet;
    case ConnectionSettings::Protocol::Sip: return Sip;
    case ConnectionSettings::Protocol::Xmpp: return Xmpp;
    case ConnectionSettings::Protocol::Ssh: return Ssh;
    case ConnectionSettings::Protocol::Nntp: return Nntp;
    case ConnectionSettings::Protocol::Unknown: return false;
    }
    return false;
}

inline QStringList enabledProtocolNames(bool includeMedia = true)
{
    QStringList out;
    if (Oscar) out << QStringLiteral("AIM/OSCAR");
    if (Irc) out << QStringLiteral("IRC");
    if (Telnet) out << QStringLiteral("TELNET/BBS");
    if (Sip) out << QStringLiteral("SIP/VOIP");
    if (Xmpp) out << QStringLiteral("XMPP/JABBER");
    if (Ssh) out << QStringLiteral("SSH");
    if (Nntp) out << QStringLiteral("NNTP/USENET");
    if (includeMedia && Media) out << QStringLiteral("MEDIA/RADIO");
    if (includeMedia && Gopher) out << QStringLiteral("GOPHER");
    if (includeMedia && Gemini) out << QStringLiteral("GEMINI");
    if (includeMedia && Mosh) out << QStringLiteral("MOSH");
    return out;
}
}
