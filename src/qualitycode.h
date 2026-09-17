#ifndef QUALITYCODE_H
#define QUALITYCODE_H

#include <QString>

enum class QualityCode
{
    Good,
    Timeout,
    Disconnected,
    ProtocolError,
    Partial,
    InvalidData,
    WriteFailed
};

inline QString qualityCodeToString(QualityCode code)
{
    switch (code)
    {
    case QualityCode::Good: return QStringLiteral("GOOD");
    case QualityCode::Timeout: return QStringLiteral("TIMEOUT");
    case QualityCode::Disconnected: return QStringLiteral("DISCONNECTED");
    case QualityCode::ProtocolError: return QStringLiteral("PROTOCOL_ERROR");
    case QualityCode::Partial: return QStringLiteral("PARTIAL");
    case QualityCode::InvalidData: return QStringLiteral("INVALID_DATA");
    case QualityCode::WriteFailed: return QStringLiteral("WRITE_FAILED");
    }
    return QStringLiteral("INVALID_DATA");
}

inline QualityCode qualityCodeFromPollError(const QString &error)
{
    const QString normalized = error.toLower();
    if (normalized.contains(QStringLiteral("timeout")) ||
        normalized.contains(QStringLiteral("timed out")))
        return QualityCode::Timeout;
    if (normalized.contains(QStringLiteral("not open")) ||
        normalized.contains(QStringLiteral("disconnected")))
        return QualityCode::Disconnected;
    return QualityCode::ProtocolError;
}

#endif // QUALITYCODE_H
