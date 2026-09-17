#include <QCoreApplication>
#include <QTextStream>
#include <QTimeZone>

#include "../src/controlvalidator.h"
#include "../src/alarmstate.h"

namespace
{
bool require(bool value, const QString &message)
{
    if (!value)
        QTextStream(stderr) << "FAIL: " << message << "\n";
    return value;
}

PointDefinition writable(const QString &definition = QString())
{
    PointDefinition point;
    point.key = QStringLiteral("command");
    point.displayName = QStringLiteral("Command");
    point.attribute = QStringLiteral("R/W");
    point.access = PointAccess::ReadWrite;
    point.type = PointDataType::U16;
    point.count = 1;
    point.writeFunctions << 6 << 16;
    point.definition = definition;
    return point;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    const PointDefinition point = writable(QStringLiteral("0x1: charging; 0x2: discharge; 0xff: invalid"));
    ok &= require(ControlValidator::validateRaw(point, 6, QVector<quint16>() << 1).accepted,
                  QStringLiteral("allowed command accepted"));
    ok &= require(!ControlValidator::validateRaw(point, 6, QVector<quint16>() << 3).accepted,
                  QStringLiteral("invalid enum rejected"));
    const PointDefinition decimalPoint = writable(QStringLiteral("1： Enable; 2： Disable"));
    ok &= require(ControlValidator::validateRaw(decimalPoint, 6, QVector<quint16>() << 2).accepted,
                  QStringLiteral("decimal command enum accepted"));
    ok &= require(!ControlValidator::validateRaw(decimalPoint, 6, QVector<quint16>() << 3).accepted,
                  QStringLiteral("decimal command enum rejected"));
    const PointDefinition duration = writable(QStringLiteral("0xffff: Invalid value"));
    ok &= require(ControlValidator::validateRaw(duration, 6, QVector<quint16>() << 10).accepted,
                  QStringLiteral("duration value accepted when definition only declares invalid sentinel"));
    ok &= require(!ControlValidator::validateRaw(duration, 6, QVector<quint16>() << 0xffff).accepted,
                  QStringLiteral("invalid duration sentinel rejected"));
    ok &= require(!ControlValidator::validateRaw(point, 3, QVector<quint16>() << 1).accepted,
                  QStringLiteral("unsupported function rejected"));
    ok &= require(!ControlValidator::validateRaw(point, 6, QVector<quint16>() << 1,
                                                 QStringLiteral("viewer")).accepted,
                  QStringLiteral("unauthorized role rejected"));

    PointDefinition readOnly = point;
    readOnly.access = PointAccess::ReadOnly;
    ok &= require(!ControlValidator::validateRaw(readOnly, 6, QVector<quint16>() << 1).accepted,
                  QStringLiteral("read-only point rejected"));

    AlarmStateModel alarms;
    const QDateTime t1 = QDateTime::fromMSecsSinceEpoch(1000, QTimeZone::UTC);
    const QDateTime t2 = t1.addSecs(1);
    const auto occurred = alarms.update(QStringLiteral("alarm_word"), QStringLiteral("Over voltage"), 2, true, t1);
    ok &= require(occurred.size() == 1 && occurred.first().event == QStringLiteral("occurred"),
                  QStringLiteral("alarm occurrence transition"));
    ok &= require(alarms.update(QStringLiteral("alarm_word"), QStringLiteral("Over voltage"), 2, true, t2).isEmpty(),
                  QStringLiteral("alarm hold has no duplicate event"));
    const auto recovered = alarms.update(QStringLiteral("alarm_word"), QStringLiteral("Over voltage"), 2, false, t2);
    ok &= require(recovered.size() == 1 && recovered.first().event == QStringLiteral("recovered"),
                  QStringLiteral("alarm recovery transition"));
    AlarmTransition acknowledgement;
    alarms.update(QStringLiteral("alarm_word"), QStringLiteral("Over voltage"), 2, true, t2);
    ok &= require(alarms.acknowledge(QStringLiteral("alarm_word:2"), t2, QStringLiteral("operator"), &acknowledgement) &&
                  acknowledgement.event == QStringLiteral("acknowledged") &&
                  acknowledgement.state.acknowledgedBy == QStringLiteral("operator"),
                  QStringLiteral("alarm acknowledgement"));
    return ok ? 0 : 1;
}
