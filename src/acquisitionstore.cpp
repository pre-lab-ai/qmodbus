#include "acquisitionstore.h"

#include "modbus.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QTimeZone>
#include <QVariantList>

namespace
{
QString errorText(const QSqlError &error)
{
    return error.text().isEmpty() ? QStringLiteral("SQLite operation failed") : error.text();
}

void setError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}

QString variantJson(const QVariant &value)
{
    if (!value.isValid())
        return QStringLiteral("null");
    QJsonObject wrapper;
    wrapper.insert(QStringLiteral("value"), QJsonValue::fromVariant(value));
    return QString::fromUtf8(QJsonDocument(wrapper).toJson(QJsonDocument::Compact));
}

QString csvField(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    if (escaped.contains(QLatin1Char(',')) || escaped.contains(QLatin1Char('"')) ||
        escaped.contains(QLatin1Char('\n')) || escaped.contains(QLatin1Char('\r')))
        return QStringLiteral("\"") + escaped + QStringLiteral("\"");
    return escaped;
}

QString frameKey(const QString &deviceId, int slaveId, const PollFrame &frame)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(deviceId).arg(slaveId).arg(frame.block).arg(frame.function)
        .arg(frame.address).arg(frame.count);
}

QString nonNullString(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QVariant parseVariantJson(const QVariant &value)
{
    if (!value.isValid() || value.toString().isEmpty())
        return QVariant();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(value.toString().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return QVariant();
    return document.object().value(QStringLiteral("value")).toVariant();
}
}

AcquisitionStore::AcquisitionStore() :
    m_database(nullptr)
{
    static quint64 sequence = 0;
    m_connectionName = QStringLiteral("acquisition_store_%1").arg(++sequence);
}

AcquisitionStore::~AcquisitionStore()
{
    close();
}

bool AcquisitionStore::open(const QString &databasePath, QString *error)
{
    close();
    if (databasePath.isEmpty())
    {
        setError(error, QStringLiteral("database path is empty"));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    database.setDatabaseName(databasePath);
    if (!database.open())
    {
        setError(error, errorText(database.lastError()));
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }
    m_database = new QSqlDatabase(database);
    if (!initialize(error))
    {
        close();
        return false;
    }
    return true;
}

void AcquisitionStore::close()
{
    if (!m_database)
        return;
    m_database->close();
    delete m_database;
    m_database = nullptr;
    QSqlDatabase::removeDatabase(m_connectionName);
    m_frameFailures.clear();
}

bool AcquisitionStore::isOpen() const
{
    return m_database && m_database->isOpen();
}

bool AcquisitionStore::initialize(QString *error)
{
    if (!isOpen())
    {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    const QStringList statements = {
        QStringLiteral("PRAGMA foreign_keys = ON"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS samples ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "device_id TEXT NOT NULL, slave_id INTEGER NOT NULL,"
                       "point_key TEXT NOT NULL, display_name TEXT NOT NULL, block TEXT NOT NULL,"
                       "address INTEGER NOT NULL, raw_json TEXT, value_json TEXT, unit TEXT NOT NULL,"
                       "quality TEXT NOT NULL, timestamp_ms INTEGER NOT NULL, elapsed_ms INTEGER NOT NULL,"
                       "function INTEGER NOT NULL, error TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_samples_time ON samples(timestamp_ms)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_samples_point_time ON samples(point_key, timestamp_ms)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS communication_events ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, device_id TEXT NOT NULL, slave_id INTEGER NOT NULL,"
                       "event_type TEXT NOT NULL, severity TEXT NOT NULL, message TEXT NOT NULL,"
                       "point_key TEXT NOT NULL DEFAULT '', bit_index INTEGER NOT NULL DEFAULT -1,"
                       "raw_value INTEGER NOT NULL DEFAULT 0,"
                       "timestamp_ms INTEGER NOT NULL, function INTEGER NOT NULL, address INTEGER NOT NULL,"
                       "count INTEGER NOT NULL, attempts INTEGER NOT NULL, elapsed_ms INTEGER NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_communication_time ON communication_events(timestamp_ms)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS audit_events ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, device_id TEXT NOT NULL, user_id TEXT NOT NULL,"
                       "point_key TEXT NOT NULL, display_name TEXT NOT NULL, slave_id INTEGER NOT NULL,"
                       "old_value_json TEXT, new_value_json TEXT, result TEXT NOT NULL, message TEXT NOT NULL,"
                       "function INTEGER NOT NULL DEFAULT 0, address INTEGER NOT NULL DEFAULT 0,"
                       "count INTEGER NOT NULL DEFAULT 0, timestamp_ms INTEGER NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_audit_time ON audit_events(timestamp_ms)")
    };
    for (const QString &statement : statements)
    {
        QSqlQuery query(*m_database);
        if (!query.exec(statement))
        {
            setError(error, errorText(query.lastError()));
            return false;
        }
    }
    // Migrate databases created before function/address/count were persisted.
    const QStringList migrations = {
        QStringLiteral("ALTER TABLE communication_events ADD COLUMN point_key TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE communication_events ADD COLUMN bit_index INTEGER NOT NULL DEFAULT -1"),
        QStringLiteral("ALTER TABLE communication_events ADD COLUMN raw_value INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE audit_events ADD COLUMN function INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE audit_events ADD COLUMN address INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE audit_events ADD COLUMN count INTEGER NOT NULL DEFAULT 0")
    };
    for (const QString &statement : migrations)
    {
        QSqlQuery query(*m_database);
        if (!query.exec(statement) && !query.lastError().text().contains(QStringLiteral("duplicate column"), Qt::CaseInsensitive))
        {
            setError(error, errorText(query.lastError()));
            return false;
        }
    }
    QSqlQuery schemaQuery(*m_database);
    if (!schemaQuery.exec(QStringLiteral("PRAGMA user_version = 2")))
    {
        setError(error, errorText(schemaQuery.lastError()));
        return false;
    }
    return true;
}

bool AcquisitionStore::insertSample(const AcquisitionSample &sample, QString *error)
{
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("INSERT INTO samples (device_id, slave_id, point_key, display_name, block, "
                                 "address, raw_json, value_json, unit, quality, timestamp_ms, elapsed_ms, function, error) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(nonNullString(sample.deviceId));
    query.addBindValue(sample.slaveId);
    query.addBindValue(nonNullString(sample.pointKey));
    query.addBindValue(nonNullString(sample.displayName));
    query.addBindValue(nonNullString(sample.block));
    query.addBindValue(sample.address);
    query.addBindValue(sample.rawValue.isValid() ? variantJson(sample.rawValue) : QVariant());
    query.addBindValue(sample.engineeringValue.isValid() ? variantJson(sample.engineeringValue) : QVariant());
    query.addBindValue(nonNullString(sample.unit));
    query.addBindValue(qualityCodeToString(sample.quality));
    query.addBindValue(sample.timestampUtc.toUTC().toMSecsSinceEpoch());
    query.addBindValue(sample.elapsedMs);
    query.addBindValue(sample.function);
    query.addBindValue(nonNullString(sample.error));
    if (!query.exec())
    {
        setError(error, errorText(query.lastError()));
        return false;
    }
    return true;
}

bool AcquisitionStore::insertCommunicationEvent(const CommunicationEvent &event, QString *error)
{
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("INSERT INTO communication_events (device_id, slave_id, event_type, severity, message, "
                                 "point_key, bit_index, raw_value, timestamp_ms, function, address, count, attempts, elapsed_ms) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(nonNullString(event.deviceId));
    query.addBindValue(event.slaveId);
    query.addBindValue(nonNullString(event.eventType));
    query.addBindValue(nonNullString(event.severity));
    query.addBindValue(nonNullString(event.message));
    query.addBindValue(nonNullString(event.pointKey));
    query.addBindValue(event.bit);
    query.addBindValue(event.rawValue);
    query.addBindValue(event.timestampUtc.toUTC().toMSecsSinceEpoch());
    query.addBindValue(event.function);
    query.addBindValue(event.address);
    query.addBindValue(event.count);
    query.addBindValue(event.attempts);
    query.addBindValue(event.elapsedMs);
    if (!query.exec())
    {
        setError(error, errorText(query.lastError()));
        return false;
    }
    return true;
}

bool AcquisitionStore::insertAuditEvent(const AuditEvent &event, QString *error)
{
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("INSERT INTO audit_events (device_id, user_id, point_key, display_name, slave_id, "
                                 "old_value_json, new_value_json, result, message, function, address, count, timestamp_ms) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(nonNullString(event.deviceId));
    query.addBindValue(nonNullString(event.userId));
    query.addBindValue(nonNullString(event.pointKey));
    query.addBindValue(nonNullString(event.displayName));
    query.addBindValue(event.slaveId);
    query.addBindValue(event.oldValue.isValid() ? variantJson(event.oldValue) : QVariant());
    query.addBindValue(event.newValue.isValid() ? variantJson(event.newValue) : QVariant());
    query.addBindValue(nonNullString(event.result));
    query.addBindValue(nonNullString(event.message));
    query.addBindValue(event.function);
    query.addBindValue(event.address);
    query.addBindValue(event.count);
    query.addBindValue(event.timestampUtc.toUTC().toMSecsSinceEpoch());
    if (!query.exec())
    {
        setError(error, errorText(query.lastError()));
        return false;
    }
    return true;
}

bool AcquisitionStore::recordPollResult(const PollResult &result,
                                         const PointTable &pointTable,
                                         const QString &deviceId,
                                         int slaveId,
                                         const QDateTime &timestampUtc,
                                         QString *error)
{
    if (!isOpen())
    {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    const QDateTime timestamp = timestampUtc.isValid()
        ? timestampUtc.toUTC() : QDateTime::currentDateTimeUtc();
    const QString key = frameKey(deviceId, slaveId, result.frame);
    const bool hadFailure = m_frameFailures.value(key, false);

    if (!m_database->transaction())
    {
        setError(error, errorText(m_database->lastError()));
        return false;
    }

    const QualityCode quality = result.success ? QualityCode::Good : qualityCodeFromPollError(result.error);
    for (const PointDefinition &point : pointTable.points())
    {
        const bool pcsSlaveWrite = point.block == QStringLiteral("PCS") &&
                                    result.frame.function == MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
        if (point.reserved || point.block != result.frame.block ||
            (!pcsSlaveWrite && !point.readFunctions.contains(result.frame.function) &&
             !point.writeFunctions.contains(result.frame.function)) ||
            point.lastAddress() < result.frame.address ||
            point.address > result.frame.lastAddress())
            continue;

        AcquisitionSample sample;
        sample.deviceId = deviceId;
        sample.slaveId = slaveId;
        sample.pointKey = point.key;
        sample.displayName = point.displayName;
        sample.block = point.block;
        sample.address = point.address;
        sample.unit = point.unit;
        sample.quality = quality;
        sample.timestampUtc = timestamp;
        sample.elapsedMs = result.elapsedMs;
        sample.function = result.frame.function;
        sample.error = result.error;

        QVariant previousRawValue;
        QVariant previousEngineeringValue;
        QSqlQuery previous(*m_database);
        previous.prepare(QStringLiteral(
            "SELECT raw_json, value_json FROM samples WHERE device_id = ? AND slave_id = ? "
            "AND point_key = ? ORDER BY timestamp_ms DESC, id DESC LIMIT 1"));
        previous.addBindValue(deviceId);
        previous.addBindValue(slaveId);
        previous.addBindValue(point.key);
        if (previous.exec() && previous.next())
        {
            previousRawValue = parseVariantJson(previous.value(0));
            previousEngineeringValue = parseVariantJson(previous.value(1));
        }

        if (result.success)
        {
            // A large array point (for example 512 cell voltages) is read in
            // multiple small Modbus frames. Merge the current frame into the
            // latest persisted array so a restart can restore all cells.
            QVariantList rawList;
            rawList = previousRawValue.toList();
            while (rawList.size() < point.count)
                rawList.append(QVariant());

            const int overlapStart = qMax(point.address, result.frame.address);
            const int overlapEnd = qMin(point.lastAddress(), result.frame.lastAddress());
            for (int address = overlapStart; address <= overlapEnd; ++address)
            {
                const int frameOffset = address - result.frame.address;
                const int pointOffset = address - point.address;
                if (frameOffset >= 0 && frameOffset < result.values.size() &&
                    pointOffset >= 0 && pointOffset < rawList.size())
                    rawList[pointOffset] = static_cast<int>(result.values.at(frameOffset));
            }
            sample.rawValue = rawList;

            QVector<quint16> decodedRaw;
            decodedRaw.reserve(rawList.size());
            bool complete = true;
            for (const QVariant &value : rawList)
            {
                complete = complete && value.isValid();
                decodedRaw.append(value.isValid() ? static_cast<quint16>(value.toUInt()) : 0);
            }
            sample.engineeringValue = point.decode(decodedRaw);
            if (!sample.engineeringValue.isValid())
                sample.quality = QualityCode::InvalidData;
            else if (!complete)
                sample.quality = QualityCode::Partial;
        }
        else if (previousRawValue.isValid() || previousEngineeringValue.isValid())
        {
            // Keep the last known value visible while exposing the failed
            // communication through the quality/error columns.
            sample.rawValue = previousRawValue;
            sample.engineeringValue = previousEngineeringValue;
        }
        if (!insertSample(sample, error))
        {
            m_database->rollback();
            return false;
        }
    }

    CommunicationEvent event;
    event.deviceId = deviceId;
    event.slaveId = slaveId;
    event.eventType = result.success
        ? (hadFailure ? QStringLiteral("communication_recovered") : QStringLiteral("poll_success"))
        : QStringLiteral("poll_failure");
    event.severity = result.success ? QStringLiteral("info") : QStringLiteral("error");
    event.message = result.success ? QStringLiteral("poll frame completed") : result.error;
    event.timestampUtc = timestamp;
    event.function = result.frame.function;
    event.address = result.frame.address;
    event.count = result.frame.count;
    event.attempts = result.attempts;
    event.elapsedMs = result.elapsedMs;
    if (!insertCommunicationEvent(event, error))
    {
        m_database->rollback();
        return false;
    }

    if (!m_database->commit())
    {
        setError(error, errorText(m_database->lastError()));
        m_database->rollback();
        return false;
    }
    m_frameFailures.insert(key, !result.success);
    return true;
}

bool AcquisitionStore::recordCommunicationEvent(const CommunicationEvent &event, QString *error)
{
    if (!isOpen())
    {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    return insertCommunicationEvent(event, error);
}

bool AcquisitionStore::recordAuditEvent(const AuditEvent &event, QString *error)
{
    if (!isOpen())
    {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    return insertAuditEvent(event, error);
}

bool AcquisitionStore::exportCsv(const QString &filePath,
                                  const QDateTime &fromUtc,
                                  const QDateTime &toUtc,
                                  QString *error) const
{
    if (!isOpen())
    {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    QSqlQuery query(*m_database);
    QString sql = QStringLiteral("SELECT device_id, slave_id, point_key, display_name, block, address, raw_json, "
                                "value_json, unit, quality, timestamp_ms, elapsed_ms, function, error "
                                "FROM samples WHERE 1=1");
    if (fromUtc.isValid()) sql += QStringLiteral(" AND timestamp_ms >= ?");
    if (toUtc.isValid()) sql += QStringLiteral(" AND timestamp_ms <= ?");
    sql += QStringLiteral(" ORDER BY timestamp_ms, id");
    query.prepare(sql);
    if (fromUtc.isValid()) query.addBindValue(fromUtc.toUTC().toMSecsSinceEpoch());
    if (toUtc.isValid()) query.addBindValue(toUtc.toUTC().toMSecsSinceEpoch());
    if (!query.exec())
    {
        setError(error, errorText(query.lastError()));
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        setError(error, file.errorString());
        return false;
    }
    QTextStream stream(&file);
    stream << "format_version,device_id,slave_id,point_key,display_name,block,address,raw_value,engineering_value,unit,quality,timestamp_utc,elapsed_ms,function,error\n";
    while (query.next())
    {
        const qint64 timestampMs = query.value(10).toLongLong();
        const QString timestamp = QDateTime::fromMSecsSinceEpoch(timestampMs, QTimeZone::UTC).toString(Qt::ISODateWithMs);
        const QStringList fields = {
            QStringLiteral("1"), query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
            query.value(3).toString(), query.value(4).toString(), query.value(5).toString(), query.value(6).toString(),
            query.value(7).toString(), query.value(8).toString(), query.value(9).toString(), timestamp,
            query.value(11).toString(), query.value(12).toString(), query.value(13).toString()
        };
        for (int i = 0; i < fields.size(); ++i)
        {
            if (i) stream << ',';
            stream << csvField(fields.at(i));
        }
        stream << '\n';
    }
    if (!file.commit())
    {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

QVector<AcquisitionSample> AcquisitionStore::latestSamples(const QString &deviceId, int slaveId,
                                                             const QString &block, QString *error) const
{
    QVector<AcquisitionSample> samples;
    if (!isOpen())
    {
        setError(error, QStringLiteral("database is not open"));
        return samples;
    }
    QString sql = QStringLiteral(
        "SELECT s.device_id, s.slave_id, s.point_key, s.display_name, s.block, s.address, "
        "s.raw_json, s.value_json, s.unit, s.quality, s.timestamp_ms, s.elapsed_ms, s.function, s.error "
        "FROM samples s WHERE s.device_id = ? AND s.slave_id = ? "
        "AND s.id = (SELECT s2.id FROM samples s2 WHERE s2.device_id=s.device_id "
        "AND s2.slave_id=s.slave_id AND s2.point_key=s.point_key ORDER BY s2.timestamp_ms DESC, s2.id DESC LIMIT 1)");
    if (!block.isEmpty())
        sql += QStringLiteral(" AND s.block = ?");
    sql += QStringLiteral(" ORDER BY s.block, s.address, s.point_key");

    QSqlQuery query(*m_database);
    query.prepare(sql);
    query.addBindValue(deviceId);
    query.addBindValue(slaveId);
    if (!block.isEmpty())
        query.addBindValue(block);
    if (!query.exec())
    {
        setError(error, errorText(query.lastError()));
        return samples;
    }
    while (query.next())
    {
        AcquisitionSample sample;
        sample.deviceId = query.value(0).toString();
        sample.slaveId = query.value(1).toInt();
        sample.pointKey = query.value(2).toString();
        sample.displayName = query.value(3).toString();
        sample.block = query.value(4).toString();
        sample.address = query.value(5).toInt();
        sample.rawValue = parseVariantJson(query.value(6));
        sample.engineeringValue = parseVariantJson(query.value(7));
        sample.unit = query.value(8).toString();
        const QString quality = query.value(9).toString();
        if (quality == QStringLiteral("GOOD")) sample.quality = QualityCode::Good;
        else if (quality == QStringLiteral("TIMEOUT")) sample.quality = QualityCode::Timeout;
        else if (quality == QStringLiteral("DISCONNECTED")) sample.quality = QualityCode::Disconnected;
        else if (quality == QStringLiteral("PROTOCOL_ERROR")) sample.quality = QualityCode::ProtocolError;
        else if (quality == QStringLiteral("PARTIAL")) sample.quality = QualityCode::Partial;
        else if (quality == QStringLiteral("WRITE_FAILED")) sample.quality = QualityCode::WriteFailed;
        else sample.quality = QualityCode::InvalidData;
        sample.timestampUtc = QDateTime::fromMSecsSinceEpoch(query.value(10).toLongLong(), QTimeZone::UTC);
        sample.elapsedMs = query.value(11).toLongLong();
        sample.function = query.value(12).toInt();
        sample.error = query.value(13).toString();
        samples.append(sample);
    }
    return samples;
}

bool AcquisitionStore::pruneBefore(const QDateTime &cutoffUtc, QString *error)
{
    if (!isOpen())
    {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    if (!cutoffUtc.isValid())
    {
        setError(error, QStringLiteral("retention cutoff is invalid"));
        return false;
    }
    if (!m_database->transaction())
    {
        setError(error, errorText(m_database->lastError()));
        return false;
    }
    const qint64 cutoff = cutoffUtc.toUTC().toMSecsSinceEpoch();
    const QStringList statements = {
        QStringLiteral("DELETE FROM samples WHERE timestamp_ms < ?"),
        QStringLiteral("DELETE FROM communication_events WHERE timestamp_ms < ?"),
        QStringLiteral("DELETE FROM audit_events WHERE timestamp_ms < ?")
    };
    for (const QString &statement : statements)
    {
        QSqlQuery query(*m_database);
        query.prepare(statement);
        query.addBindValue(cutoff);
        if (!query.exec())
        {
            setError(error, errorText(query.lastError()));
            m_database->rollback();
            return false;
        }
    }
    if (!m_database->commit())
    {
        setError(error, errorText(m_database->lastError()));
        m_database->rollback();
        return false;
    }
    return true;
}

bool AcquisitionStore::integrityCheck(QString *error) const
{
    if (!isOpen())
    {
        setError(error, QStringLiteral("database is not open"));
        return false;
    }
    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("PRAGMA integrity_check")) || !query.next())
    {
        setError(error, errorText(query.lastError()));
        return false;
    }
    const QString result = query.value(0).toString();
    if (result != QStringLiteral("ok"))
    {
        setError(error, result);
        return false;
    }
    return true;
}

qint64 AcquisitionStore::sampleCount(QString *error) const
{
    if (!isOpen()) { setError(error, QStringLiteral("database is not open")); return -1; }
    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM samples")) || !query.next())
    { setError(error, errorText(query.lastError())); return -1; }
    return query.value(0).toLongLong();
}

qint64 AcquisitionStore::communicationEventCount(QString *error) const
{
    if (!isOpen()) { setError(error, QStringLiteral("database is not open")); return -1; }
    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM communication_events")) || !query.next())
    { setError(error, errorText(query.lastError())); return -1; }
    return query.value(0).toLongLong();
}

qint64 AcquisitionStore::auditEventCount(QString *error) const
{
    if (!isOpen()) { setError(error, QStringLiteral("database is not open")); return -1; }
    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM audit_events")) || !query.next())
    { setError(error, errorText(query.lastError())); return -1; }
    return query.value(0).toLongLong();
}
