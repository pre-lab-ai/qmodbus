#include "controlvalidator.h"

#include <QSet>
#include <QRegularExpression>

namespace
{
struct CommandConstraints
{
    QSet<quint16> allowed;
    QSet<quint16> invalid;
};

CommandConstraints commandValues(const PointDefinition &point)
{
    CommandConstraints constraints;
    const QString definition = point.definition;
    const QRegularExpression expression(
        QStringLiteral("(?:^|[;<>,\\s])((?:0x[0-9a-fA-F]{1,4})|(?:\\d{1,5}))\\s*[:：-]\\s*([^;,<>]+)"));
    QRegularExpressionMatchIterator it = expression.globalMatch(definition);
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        const QString token = match.captured(1);
        bool ok = false;
        const uint value = token.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
            ? token.mid(2).toUInt(&ok, 16)
            : token.toUInt(&ok, 10);
        if (ok && value <= 0xffffu)
        {
            const QString label = match.captured(2).trimmed();
            const bool invalid = label.contains(QStringLiteral("invalid"), Qt::CaseInsensitive) ||
                                 label.contains(QStringLiteral("无效"));
            if (invalid)
                constraints.invalid.insert(static_cast<quint16>(value));
            else
                constraints.allowed.insert(static_cast<quint16>(value));
        }
    }
    return constraints;
}
}

ControlValidation ControlValidator::validateRaw(const PointDefinition &point, int function,
                                                 const QVector<quint16> &raw,
                                                 const QString &userRole)
{
    ControlValidation result;
    const QString role = userRole.trimmed().toLower();
    if (role != QStringLiteral("operator") && role != QStringLiteral("engineer") &&
        role != QStringLiteral("administrator"))
    {
        result.error = QStringLiteral("user role '%1' is not authorized to write controls").arg(userRole);
        return result;
    }
    if (!point.canWrite())
    {
        result.error = QStringLiteral("point '%1' is read-only or reserved").arg(point.key);
        return result;
    }
    if (!point.writeFunctions.contains(function))
    {
        result.error = QStringLiteral("function 0x%1 is not allowed for '%2'")
                           .arg(function, 2, 16, QLatin1Char('0')).arg(point.key);
        return result;
    }
    if (raw.size() != point.count)
    {
        result.error = QStringLiteral("point '%1' requires %2 register(s)")
                           .arg(point.key).arg(point.count);
        return result;
    }
    if (point.type == PointDataType::U16)
    {
        const CommandConstraints constraints = commandValues(point);
        if (constraints.invalid.contains(raw.first()))
        {
            result.error = QStringLiteral("value %1 is marked invalid for '%2'")
                               .arg(raw.first()).arg(point.key);
            return result;
        }
        if (!constraints.allowed.isEmpty() && !constraints.allowed.contains(raw.first()))
        {
            result.error = QStringLiteral("value %1 is outside the command enum for '%2'")
                               .arg(raw.first()).arg(point.key);
            return result;
        }
    }
    if (point.type == PointDataType::I16)
    {
        Q_UNUSED(raw.first());
    }
    result.accepted = true;
    return result;
}
