#ifndef POINTMODEL_H
#define POINTMODEL_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

enum class PointDataType
{
    U16,
    I16,
    U32,
    I32,
    Float32,
    BitField,
    Reserved,
    Unknown
};

enum class PointAccess
{
    ReadOnly,
    ReadWrite,
    Unknown
};

struct PointBitField
{
    int bit = -1;
    QString key;
    QString description;
};

struct PointDefinition
{
    QString displayName;
    QString key;
    QString sourceKey;
    QString description;
    QString definition;
    QString comment;
    QString dataOrigin;
    QString sourceSignal;
    QString block;
    QString sourceAddress;
    QString unit;
    QString attribute;
    QString sourceAttribute;
    int address = 0;
    int count = 1;
    PointDataType type = PointDataType::Unknown;
    PointAccess access = PointAccess::Unknown;
    double scale = 1.0;
    double offset = 0.0;
    QVector<int> readFunctions;
    QVector<int> writeFunctions;
    QVector<PointBitField> bitFields;
    bool reserved = false;

    bool canWrite() const;
    int lastAddress() const;
    QVariant decode(const QVector<quint16> &raw) const;
};

class PointTable
{
public:
    bool load(const QString &fileName, QStringList *errors = nullptr);
    bool loadJson(const QByteArray &json, QStringList *errors = nullptr);
    void append(const PointTable &other);
    bool validate(QStringList *errors = nullptr) const;

    const QVector<PointDefinition> &points() const { return m_points; }
    const PointDefinition *findByKey(const QString &key) const;
    const PointDefinition *findByAddress(const QString &block, int address) const;
    QStringList displayNames() const;

private:
    QVector<PointDefinition> m_points;
};

PointDataType pointDataTypeFromString(const QString &value);
PointAccess pointAccessFromString(const QString &value);
QString pointDataTypeToString(PointDataType type);
QString pointAccessToString(PointAccess access);

#endif // POINTMODEL_H
