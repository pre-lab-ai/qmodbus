#include "pointmodel.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

#include <cstring>

namespace
{
QStringList *errorTarget(QStringList *errors, QStringList &local)
{
    return errors ? errors : &local;
}

void addError(QStringList *errors, const QString &message)
{
    if (errors)
        errors->append(message);
}

bool blockContains(const QString &block, int address, int lastAddress)
{
    if (block == QStringLiteral("Rack Signal"))
        return address >= 0x0001 && lastAddress <= 0x0200;
    if (block == QStringLiteral("Rack Measure"))
        return address >= 0x0201 && lastAddress <= 0x0400;
    if (block == QStringLiteral("Rack Control"))
        return (address >= 0x0401 && lastAddress <= 0x0800) ||
               (address >= 0x0900 && lastAddress <= 0x0901);
    if (block == QStringLiteral("Rack Diag"))
        return address >= 0x0801 && lastAddress <= 0x0B00 &&
               !(address <= 0x0901 && lastAddress >= 0x0900);
    if (block == QStringLiteral("Rack Detail"))
        return address >= 0x1000 && lastAddress <= 0x6000;
    if (block == QStringLiteral("Alarm parameters"))
        return address >= 0x6001 && lastAddress <= 0x7000;
    if (block == QStringLiteral("PCS"))
        return address >= 0x0000 && lastAddress <= 0x000F;
    return false;
}

QVariant decodeScalar(PointDataType type, const QVector<quint16> &raw, int index,
                      double scale, double offset)
{
    if (index < 0 || index >= raw.size())
        return QVariant();

    if (type == PointDataType::U16)
        return QVariant(raw.at(index) * scale + offset);
    if (type == PointDataType::I16)
        return QVariant(static_cast<qint16>(raw.at(index)) * scale + offset);
    if (type == PointDataType::U32 || type == PointDataType::I32 ||
        type == PointDataType::Float32)
    {
        if (index + 1 >= raw.size())
            return QVariant();
        const quint32 bits = (static_cast<quint32>(raw.at(index)) << 16) |
                             static_cast<quint32>(raw.at(index + 1));
        if (type == PointDataType::U32)
            return QVariant(static_cast<qulonglong>(bits) * scale + offset);
        if (type == PointDataType::I32)
            return QVariant(static_cast<qint32>(bits) * scale + offset);
        float value = 0.0f;
        memcpy(&value, &bits, sizeof(value));
        return QVariant(static_cast<double>(value) * scale + offset);
    }
    return QVariant();
}
}

PointDataType pointDataTypeFromString(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("u16"))
        return PointDataType::U16;
    if (normalized == QStringLiteral("int16") || normalized == QStringLiteral("i16"))
        return PointDataType::I16;
    if (normalized == QStringLiteral("u32"))
        return PointDataType::U32;
    if (normalized == QStringLiteral("int32") || normalized == QStringLiteral("i32"))
        return PointDataType::I32;
    if (normalized == QStringLiteral("float32") || normalized == QStringLiteral("float"))
        return PointDataType::Float32;
    if (normalized == QStringLiteral("bit") || normalized == QStringLiteral("bitfield"))
        return PointDataType::BitField;
    if (normalized == QStringLiteral("reserved") || normalized == QStringLiteral("x"))
        return PointDataType::Reserved;
    return PointDataType::Unknown;
}

PointAccess pointAccessFromString(const QString &value)
{
    const QString normalized = value.trimmed().toUpper();
    if (normalized == QStringLiteral("R"))
        return PointAccess::ReadOnly;
    if (normalized == QStringLiteral("R/W") || normalized == QStringLiteral("W/R"))
        return PointAccess::ReadWrite;
    return PointAccess::Unknown;
}

QString pointDataTypeToString(PointDataType type)
{
    switch (type)
    {
    case PointDataType::U16: return QStringLiteral("u16");
    case PointDataType::I16: return QStringLiteral("int16");
    case PointDataType::U32: return QStringLiteral("u32");
    case PointDataType::I32: return QStringLiteral("int32");
    case PointDataType::Float32: return QStringLiteral("float32");
    case PointDataType::BitField: return QStringLiteral("bitfield");
    case PointDataType::Reserved: return QStringLiteral("reserved");
    case PointDataType::Unknown: break;
    }
    return QStringLiteral("unknown");
}

QString pointAccessToString(PointAccess access)
{
    if (access == PointAccess::ReadOnly)
        return QStringLiteral("R");
    if (access == PointAccess::ReadWrite)
        return QStringLiteral("R/W");
    return QStringLiteral("unknown");
}

bool PointDefinition::canWrite() const
{
    return access == PointAccess::ReadWrite && !reserved && !writeFunctions.isEmpty();
}

int PointDefinition::lastAddress() const
{
    return address + count - 1;
}

QVariant PointDefinition::decode(const QVector<quint16> &raw) const
{
    if (reserved || type == PointDataType::Reserved || raw.size() < count)
        return QVariant();

    if (type == PointDataType::BitField)
    {
        if (raw.isEmpty())
            return QVariant();
        QVariantMap result;
        const quint16 value = raw.first();
        for (const PointBitField &field : bitFields)
            result.insert(field.key.isEmpty() ? QString::number(field.bit) : field.key,
                          (value & (static_cast<quint16>(1) << field.bit)) != 0);
        return result;
    }

    const bool twoRegisters = type == PointDataType::U32 || type == PointDataType::I32 ||
                              type == PointDataType::Float32;
    if (count == 1)
        return decodeScalar(type, raw, 0, scale, offset);

    QVariantList values;
    const int step = twoRegisters ? 2 : 1;
    for (int index = 0; index + step - 1 < count && index + step - 1 < raw.size(); index += step)
        values.append(decodeScalar(type, raw, index, scale, offset));
    return values;
}

bool PointTable::load(const QString &fileName, QStringList *errors)
{
    QStringList localErrors;
    QStringList *target = errorTarget(errors, localErrors);
    target->clear();

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        addError(target, QStringLiteral("cannot open point table: %1").arg(fileName));
        return false;
    }
    return loadJson(file.readAll(), target);
}

bool PointTable::loadJson(const QByteArray &json, QStringList *errors)
{
    QStringList localErrors;
    QStringList *target = errorTarget(errors, localErrors);
    target->clear();
    m_points.clear();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull() || !document.isObject())
    {
        addError(target, QStringLiteral("invalid point table JSON: %1").arg(parseError.errorString()));
        return false;
    }

    const QJsonArray points = document.object().value(QStringLiteral("points")).toArray();
    if (points.isEmpty())
        addError(target, QStringLiteral("point table contains no points"));

    for (const QJsonValue &value : points)
    {
        if (!value.isObject())
        {
            addError(target, QStringLiteral("point entry is not an object"));
            continue;
        }
        const QJsonObject object = value.toObject();
        PointDefinition point;
        point.displayName = object.value(QStringLiteral("display_name")).toString();
        point.key = object.value(QStringLiteral("key")).toString();
        point.sourceKey = object.value(QStringLiteral("source_key")).toString(point.key);
        point.description = object.value(QStringLiteral("description")).toString();
        point.definition = object.value(QStringLiteral("definition")).toString();
        point.comment = object.value(QStringLiteral("comment")).toString();
        point.dataOrigin = object.value(QStringLiteral("data_origin")).toString();
        point.sourceSignal = object.value(QStringLiteral("source_signal")).toString();
        point.block = object.value(QStringLiteral("block")).toString();
        point.sourceAddress = object.value(QStringLiteral("source_address")).toString();
        point.unit = object.value(QStringLiteral("unit")).toString();
        point.attribute = object.value(QStringLiteral("attribute")).toString();
        point.sourceAttribute = object.value(QStringLiteral("source_attribute")).toString(point.attribute);
        point.address = object.value(QStringLiteral("address")).toInt(-1);
        point.count = object.value(QStringLiteral("count")).toInt(0);
        point.type = pointDataTypeFromString(object.value(QStringLiteral("type")).toString());
        point.access = pointAccessFromString(point.attribute);
        point.scale = object.value(QStringLiteral("scale")).toDouble(1.0);
        point.offset = object.value(QStringLiteral("offset")).toDouble(0.0);
        point.reserved = object.value(QStringLiteral("reserved")).toBool(point.type == PointDataType::Reserved);

        const QJsonArray readFunctions = object.value(QStringLiteral("read_functions")).toArray();
        for (const QJsonValue &function : readFunctions)
            point.readFunctions.append(function.toInt());
        const QJsonArray writeFunctions = object.value(QStringLiteral("write_functions")).toArray();
        for (const QJsonValue &function : writeFunctions)
            point.writeFunctions.append(function.toInt());
        const QJsonArray bitFields = object.value(QStringLiteral("bit_fields")).toArray();
        for (const QJsonValue &bitValue : bitFields)
        {
            const QJsonObject bitObject = bitValue.toObject();
            PointBitField bit;
            bit.bit = bitObject.value(QStringLiteral("bit")).toInt(-1);
            bit.key = bitObject.value(QStringLiteral("key")).toString();
            bit.description = bitObject.value(QStringLiteral("description")).toString();
            point.bitFields.append(bit);
        }
        m_points.append(point);
    }

    if (!target->isEmpty())
        return false;
    return validate(target);
}

void PointTable::append(const PointTable &other)
{
    m_points += other.m_points;
}

bool PointTable::validate(QStringList *errors) const
{
    QStringList localErrors;
    QStringList *target = errorTarget(errors, localErrors);
    target->clear();
    QSet<QString> keys;

    for (int i = 0; i < m_points.size(); ++i)
    {
        const PointDefinition &point = m_points.at(i);
        const QString prefix = QStringLiteral("point[%1]").arg(i);
        if (point.displayName.trimmed().isEmpty())
            addError(target, prefix + QStringLiteral(" has empty display_name"));
        if (point.key.trimmed().isEmpty())
            addError(target, prefix + QStringLiteral(" has empty key"));
        if (keys.contains(point.key))
            addError(target, prefix + QStringLiteral(" duplicates key '%1'").arg(point.key));
        keys.insert(point.key);
        if (point.address < 0 || point.address > 0xffff || point.count <= 0 ||
            point.lastAddress() > 0xffff)
            addError(target, prefix + QStringLiteral(" has invalid address/count"));
        if (point.type == PointDataType::Unknown)
            addError(target, prefix + QStringLiteral(" has unknown type"));
        if (point.access == PointAccess::Unknown)
            addError(target, prefix + QStringLiteral(" has invalid attribute '%1'").arg(point.attribute));
        if (!blockContains(point.block, point.address, point.lastAddress()))
            addError(target, prefix + QStringLiteral(" address %1 is outside block %2")
                     .arg(point.sourceAddress).arg(point.block));
        for (const PointBitField &bit : point.bitFields)
        {
            if (bit.bit < 0 || bit.bit > 15)
                addError(target, prefix + QStringLiteral(" has invalid bit index %1").arg(bit.bit));
        }
    }

    for (int i = 0; i < m_points.size(); ++i)
    {
        const PointDefinition &left = m_points.at(i);
        for (int j = i + 1; j < m_points.size(); ++j)
        {
            const PointDefinition &right = m_points.at(j);
            if (left.block != right.block || left.reserved || right.reserved)
                continue;
            if (left.address <= right.lastAddress() && right.address <= left.lastAddress())
                addError(target, QStringLiteral("points '%1' and '%2' overlap")
                         .arg(left.key).arg(right.key));
        }
    }
    return target->isEmpty();
}

const PointDefinition *PointTable::findByKey(const QString &key) const
{
    for (const PointDefinition &point : m_points)
        if (point.key == key)
            return &point;
    return nullptr;
}

const PointDefinition *PointTable::findByAddress(const QString &block, int address) const
{
    for (const PointDefinition &point : m_points)
        if (point.block == block && address >= point.address && address <= point.lastAddress())
            return &point;
    return nullptr;
}

QStringList PointTable::displayNames() const
{
    QStringList result;
    for (const PointDefinition &point : m_points)
        result.append(point.displayName);
    return result;
}
