#pragma once

#include "filetransfer.h"

#include <QByteArray>
#include <QString>

namespace WaffleFileTransport {

// r20 uses a printable ASCII envelope.  The old r18 transport began the wire
// frame with ASCII Record Separator (0x1e).  That byte is legal in QString,
// but AIM HTML/text normalization and some macOS/Qt paths can discard it before
// the IM reaches MainWindow, turning an internal file-control frame into visible
// Base64-looking chat text.  Keep the CPXFILE payload itself unchanged; only the
// ordinary-IM transport wrapper changes.
inline QString unsecuredPrefixV2()
{
    return QStringLiteral("[[WHFILE2:");
}

inline QString unsecuredSuffixV2()
{
    return QStringLiteral("]]");
}

inline QString legacyUnsecuredPrefixV1()
{
    return QString(QChar(0x1e)) + QStringLiteral("WHFILE1|");
}

inline QString legacyUnsecuredPrefixV1WithoutSeparator()
{
    // Compatibility with peers/transports that stripped the leading 0x1e.
    return QStringLiteral("WHFILE1|");
}

inline QString wrapUnsecured(const QString &filePayload)
{
    const QByteArray encoded = filePayload.toUtf8().toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return unsecuredPrefixV2() + QString::fromLatin1(encoded) + unsecuredSuffixV2();
}

inline bool decodeCandidate(const QString &encodedText, QString &filePayload)
{
    const QByteArray decoded = QByteArray::fromBase64(
        encodedText.toLatin1(), QByteArray::Base64UrlEncoding);
    if (decoded.isEmpty()) return false;
    const QString candidate = QString::fromUtf8(decoded);
    if (!CpxFileTransferManager::looksLikeMessage(candidate)) return false;
    filePayload = candidate;
    return true;
}

inline bool unwrapUnsecured(const QString &wireText, QString &filePayload)
{
    filePayload.clear();

    // Current transport: entirely printable ASCII and therefore safe through
    // AIM's HTML wrapper, UTF-8 conversion, and macOS Qt text handling.
    const QString v2Prefix = unsecuredPrefixV2();
    const QString v2Suffix = unsecuredSuffixV2();
    if (wireText.startsWith(v2Prefix) && wireText.endsWith(v2Suffix)
        && wireText.size() > v2Prefix.size() + v2Suffix.size()) {
        return decodeCandidate(
            wireText.mid(v2Prefix.size(),
                         wireText.size() - v2Prefix.size() - v2Suffix.size()),
            filePayload);
    }

    // r18 and earlier compatibility. Accept both the exact original framing and
    // the form observed when a transport strips the leading Record Separator.
    const QString v1Prefix = legacyUnsecuredPrefixV1();
    if (wireText.startsWith(v1Prefix)) {
        return decodeCandidate(wireText.mid(v1Prefix.size()), filePayload);
    }
    const QString strippedV1Prefix = legacyUnsecuredPrefixV1WithoutSeparator();
    if (wireText.startsWith(strippedV1Prefix)) {
        return decodeCandidate(wireText.mid(strippedV1Prefix.size()), filePayload);
    }

    return false;
}

inline QString transferId(const QString &filePayload)
{
    CpxFileTransferManager::Message message;
    if (!CpxFileTransferManager::parseMessage(filePayload, message, nullptr)) return {};
    return message.id;
}

} // namespace WaffleFileTransport
