#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>

#include "../src/pointmodel.h"

namespace
{
bool require(bool condition, const QString &message)
{
    if (!condition)
        QTextStream(stderr) << "FAIL: " << message << "\n";
    return condition;
}

QByteArray jsonFor(const QJsonArray &points)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("points"), points);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QJsonObject point(const QString &key, const QString &block, int address,
                  int count, const QString &type, const QString &attribute,
                  double scale = 1.0)
{
    QJsonObject value;
    value.insert(QStringLiteral("display_name"), key);
    value.insert(QStringLiteral("key"), key);
    value.insert(QStringLiteral("block"), block);
    value.insert(QStringLiteral("source_address"), QStringLiteral("0x%1").arg(address, 4, 16, QLatin1Char('0')));
    value.insert(QStringLiteral("address"), address);
    value.insert(QStringLiteral("count"), count);
    value.insert(QStringLiteral("type"), type);
    value.insert(QStringLiteral("attribute"), attribute);
    value.insert(QStringLiteral("scale"), scale);
    value.insert(QStringLiteral("offset"), 0.0);
    value.insert(QStringLiteral("read_functions"), QJsonArray() << 3 << 4);
    value.insert(QStringLiteral("write_functions"), attribute == QStringLiteral("R/W") ? QJsonArray() << 6 << 16 : QJsonArray());
    value.insert(QStringLiteral("reserved"), type == QStringLiteral("reserved"));
    value.insert(QStringLiteral("bit_fields"), QJsonArray());
    return value;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    QJsonArray valid;
    valid.append(point(QStringLiteral("u16_value"), QStringLiteral("Rack Measure"), 0x0210, 1, QStringLiteral("u16"), QStringLiteral("R"), 1.0));
    valid.append(point(QStringLiteral("scaled_temp"), QStringLiteral("Rack Measure"), 0x0211, 1, QStringLiteral("int16"), QStringLiteral("R"), 0.1));
    valid.append(point(QStringLiteral("array_value"), QStringLiteral("Rack Detail"), 0x1401, 4, QStringLiteral("u16"), QStringLiteral("R")));
    QJsonObject bits = point(QStringLiteral("alarm_bits"), QStringLiteral("Rack Signal"), 0x0010, 1, QStringLiteral("bitfield"), QStringLiteral("R"));
    QJsonObject bit0;
    bit0.insert(QStringLiteral("bit"), 0);
    bit0.insert(QStringLiteral("key"), QStringLiteral("over_voltage"));
    bit0.insert(QStringLiteral("description"), QStringLiteral("Over voltage"));
    QJsonObject bit3;
    bit3.insert(QStringLiteral("bit"), 3);
    bit3.insert(QStringLiteral("key"), QStringLiteral("communication_fault"));
    bit3.insert(QStringLiteral("description"), QStringLiteral("Communication fault"));
    bits.insert(QStringLiteral("bit_fields"), QJsonArray() << bit0 << bit3);
    valid.append(bits);
    valid.append(point(QStringLiteral("reserved_area"), QStringLiteral("Rack Control"), 0x0405, 4, QStringLiteral("reserved"), QStringLiteral("R/W")));
    valid.append(point(QStringLiteral("BCU_sync_time_h16b"), QStringLiteral("Rack Control"), 0x0900, 1, QStringLiteral("u16"), QStringLiteral("R/W")));

    PointTable table;
    QStringList errors;
    ok &= require(table.loadJson(jsonFor(valid), &errors), QStringLiteral("valid point table should load: %1").arg(errors.join(';')));
    ok &= require(table.points().size() == 6, QStringLiteral("valid point count"));

    const PointDefinition *u16 = table.findByKey(QStringLiteral("u16_value"));
    ok &= require(u16 && u16->decode(QVector<quint16>() << 1234).toDouble() == 1234.0, QStringLiteral("u16 decode"));
    const PointDefinition *temp = table.findByKey(QStringLiteral("scaled_temp"));
    ok &= require(temp && qFuzzyCompare(temp->decode(QVector<quint16>() << static_cast<quint16>(-250)).toDouble(), -25.0), QStringLiteral("int16 scaling"));
    const PointDefinition *array = table.findByKey(QStringLiteral("array_value"));
    ok &= require(array && array->decode(QVector<quint16>() << 1 << 2 << 3 << 4).toList().size() == 4, QStringLiteral("array decode"));
    const PointDefinition *alarm = table.findByKey(QStringLiteral("alarm_bits"));
    const QVariantMap alarmValue = alarm ? alarm->decode(QVector<quint16>() << 1).toMap() : QVariantMap();
    ok &= require(alarmValue.value(QStringLiteral("over_voltage")).toBool() && !alarmValue.value(QStringLiteral("communication_fault")).toBool(), QStringLiteral("bit-field decode"));
    const PointDefinition *reserved = table.findByKey(QStringLiteral("reserved_area"));
    ok &= require(reserved && reserved->reserved && !reserved->decode(QVector<quint16>() << 1 << 2 << 3 << 4).isValid(), QStringLiteral("reserved area"));
    ok &= require(table.findByAddress(QStringLiteral("Rack Control"), 0x0900) == table.findByKey(QStringLiteral("BCU_sync_time_h16b")), QStringLiteral("control time-sync exception"));

    QJsonArray duplicate;
    duplicate.append(point(QStringLiteral("duplicate"), QStringLiteral("Rack Measure"), 0x0210, 1, QStringLiteral("u16"), QStringLiteral("R")));
    duplicate.append(point(QStringLiteral("duplicate"), QStringLiteral("Rack Measure"), 0x0211, 1, QStringLiteral("u16"), QStringLiteral("R")));
    PointTable duplicateTable;
    errors.clear();
    ok &= require(!duplicateTable.loadJson(jsonFor(duplicate), &errors) && errors.join(';').contains(QStringLiteral("duplicates key")), QStringLiteral("duplicate key rejection"));

    QJsonArray overlap;
    overlap.append(point(QStringLiteral("overlap_a"), QStringLiteral("Rack Measure"), 0x0210, 2, QStringLiteral("u16"), QStringLiteral("R")));
    overlap.append(point(QStringLiteral("overlap_b"), QStringLiteral("Rack Measure"), 0x0211, 1, QStringLiteral("u16"), QStringLiteral("R")));
    PointTable overlapTable;
    errors.clear();
    ok &= require(!overlapTable.loadJson(jsonFor(overlap), &errors) && errors.join(';').contains(QStringLiteral("overlap")), QStringLiteral("address overlap rejection"));

    if (argc > 1)
    {
        PointTable generated;
        errors.clear();
        const bool generatedLoaded = generated.load(QString::fromLocal8Bit(argv[1]), &errors);
        if (!generatedLoaded)
            QTextStream(stderr) << "generated errors: " << errors.join(';') << "\n";
        ok &= require(generatedLoaded, QStringLiteral("generated point table should load: %1").arg(errors.join(';')));
        ok &= require(generated.points().size() >= 200, QStringLiteral("generated point count"));
        ok &= require(generated.findByKey(QStringLiteral("BCU_sync_time_h16b")) != nullptr, QStringLiteral("generated control time-sync point"));
        ok &= require(!generated.displayNames().isEmpty() && !generated.displayNames().first().isEmpty(), QStringLiteral("generated Chinese display names"));
    }

    return ok ? 0 : 1;
}
