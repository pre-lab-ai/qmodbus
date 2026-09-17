#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimeZone>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "../src/acquisitionstore.h"

namespace
{
bool require(bool condition, const QString &message)
{
    if (!condition)
        QTextStream(stderr) << "FAIL: " << message << "\n";
    return condition;
}

PointTable makeTable()
{
    PointTable table;
    const QByteArray json = QByteArrayLiteral(
        "{\"points\":[{"
        "\"display_name\":\"电池总电压\",\"key\":\"pack_voltage\","
        "\"block\":\"Rack Measure\",\"address\":528,\"count\":1,"
        "\"type\":\"u16\",\"attribute\":\"R\",\"unit\":\"V\","
        "\"scale\":0.1,\"read_functions\":[4],\"reserved\":false}]}" );
    QStringList errors;
    if (!table.loadJson(json, &errors))
        QTextStream(stderr) << errors.join(';') << "\n";
    return table;
}

PointTable makeArrayTable()
{
    PointTable table;
    const QByteArray json = QByteArrayLiteral(
        "{\"points\":[{"
        "\"display_name\":\"单体电压\",\"key\":\"cell_voltage\","
        "\"block\":\"Rack Detail\",\"address\":4097,\"count\":4,"
        "\"type\":\"u16\",\"attribute\":\"R\",\"unit\":\"mV\","
        "\"scale\":1,\"read_functions\":[4],\"reserved\":false}]}" );
    QStringList errors;
    if (!table.loadJson(json, &errors))
        QTextStream(stderr) << errors.join(';') << "\n";
    return table;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    QTemporaryDir directory;
    ok &= require(directory.isValid(), QStringLiteral("temporary directory"));
    const QString databasePath = directory.filePath(QStringLiteral("history.sqlite"));
    const QString csvPath = directory.filePath(QStringLiteral("history.csv"));

    PointTable table = makeTable();
    AcquisitionStore store;
    QString error;
    ok &= require(store.open(databasePath, &error), QStringLiteral("open store: %1").arg(error));

    PollResult success;
    success.frame = PollFrame{QStringLiteral("Rack Measure"), 4, 528, 1, 1000};
    success.values = QVector<quint16>() << 2500;
    success.success = true;
    success.attempts = 1;
    success.elapsedMs = 7;
    const QDateTime first = QDateTime::fromMSecsSinceEpoch(1700000000000LL, QTimeZone::UTC);
    ok &= require(store.recordPollResult(success, table, QStringLiteral("bcu-01"), 1, first, &error),
                  QStringLiteral("record successful poll: %1").arg(error));
    ok &= require(store.sampleCount(&error) == 1, QStringLiteral("one good sample"));
    ok &= require(store.communicationEventCount(&error) == 1, QStringLiteral("one communication event"));

    CommunicationEvent alarmEvent;
    alarmEvent.deviceId = QStringLiteral("bcu-01");
    alarmEvent.eventType = QStringLiteral("alarm_occurred");
    alarmEvent.severity = QStringLiteral("critical");
    alarmEvent.message = QStringLiteral("严重告警");
    alarmEvent.pointKey = QStringLiteral("extern_critical_alarm");
    alarmEvent.bit = 14;
    alarmEvent.rawValue = 0x4001;
    alarmEvent.timestampUtc = first;
    alarmEvent.function = 4;
    alarmEvent.address = 0x12;
    alarmEvent.count = 1;
    ok &= require(store.recordCommunicationEvent(alarmEvent, &error),
                  QStringLiteral("record structured alarm event: %1").arg(error));
    ok &= require(store.communicationEventCount(&error) == 2,
                  QStringLiteral("structured alarm event counted"));

    PollResult failure = success;
    failure.success = false;
    failure.values.clear();
    failure.attempts = 2;
    failure.error = QStringLiteral("simulated timeout");
    const QDateTime second = first.addSecs(1);
    ok &= require(store.recordPollResult(failure, table, QStringLiteral("bcu-01"), 1, second, &error),
                  QStringLiteral("record failed poll: %1").arg(error));

    PollResult recovered = success;
    recovered.attempts = 1;
    const QDateTime third = first.addSecs(2);
    ok &= require(store.recordPollResult(recovered, table, QStringLiteral("bcu-01"), 1, third, &error),
                  QStringLiteral("record recovered poll: %1").arg(error));
    ok &= require(store.sampleCount(&error) == 3, QStringLiteral("good, failed and recovered samples"));
    ok &= require(store.communicationEventCount(&error) == 4, QStringLiteral("alarm, failure and recovery events"));

    AuditEvent audit;
    audit.deviceId = QStringLiteral("bcu-01");
    audit.userId = QStringLiteral("operator");
    audit.pointKey = QStringLiteral("pack_voltage");
    audit.displayName = QStringLiteral("电池总电压");
    audit.oldValue = 250.0;
    audit.newValue = 251.0;
    audit.result = QStringLiteral("accepted");
    audit.function = 6;
    audit.address = 528;
    audit.count = 1;
    audit.timestampUtc = third;
    ok &= require(store.recordAuditEvent(audit, &error), QStringLiteral("record audit: %1").arg(error));
    ok &= require(store.auditEventCount(&error) == 1, QStringLiteral("one audit event"));
    ok &= require(store.integrityCheck(&error), QStringLiteral("sqlite integrity: %1").arg(error));
    ok &= require(store.exportCsv(csvPath, QDateTime(), QDateTime(), &error), QStringLiteral("export csv: %1").arg(error));

    QFile csv(csvPath);
    ok &= require(csv.open(QIODevice::ReadOnly | QIODevice::Text), QStringLiteral("open exported csv"));
    const QString csvText = QString::fromUtf8(csv.readAll());
    ok &= require(csvText.startsWith(QStringLiteral("format_version,device_id,slave_id,point_key,display_name")),
                  QStringLiteral("fixed csv header"));
    ok &= require(csvText.contains(QStringLiteral("电池总电压")) && csvText.contains(QStringLiteral("TIMEOUT")),
                  QStringLiteral("csv contains Chinese name and quality code"));
    store.close();

    QSqlDatabase auditCheck = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("audit_check"));
    auditCheck.setDatabaseName(databasePath);
    ok &= require(auditCheck.open(), QStringLiteral("open audit verification connection"));
    QSqlQuery auditQuery(auditCheck);
    ok &= require(auditQuery.exec(QStringLiteral("SELECT function, address, count FROM audit_events ORDER BY id DESC LIMIT 1")) &&
                  auditQuery.next() && auditQuery.value(0).toInt() == 6 &&
                  auditQuery.value(1).toInt() == 528 && auditQuery.value(2).toInt() == 1,
                  QStringLiteral("audit function/address/count persisted"));
    QSqlQuery alarmQuery(auditCheck);
    ok &= require(alarmQuery.exec(QStringLiteral("SELECT point_key, bit_index, raw_value FROM communication_events WHERE event_type = 'alarm_occurred'")) &&
                  alarmQuery.next() && alarmQuery.value(0).toString() == QStringLiteral("extern_critical_alarm") &&
                  alarmQuery.value(1).toInt() == 14 && alarmQuery.value(2).toUInt() == 0x4001,
                  QStringLiteral("alarm point, bit and raw register persisted"));
    auditCheck.close();
    QSqlDatabase::removeDatabase(QStringLiteral("audit_check"));

    AcquisitionStore reopened;
    ok &= require(reopened.open(databasePath, &error), QStringLiteral("reopen store: %1").arg(error));
    ok &= require(reopened.sampleCount(&error) == 3, QStringLiteral("samples survive restart"));
    const QVector<AcquisitionSample> latest = reopened.latestSamples(QStringLiteral("bcu-01"), 1, QStringLiteral("Rack Measure"), &error);
    ok &= require(latest.size() == 1 && latest.first().engineeringValue.toDouble() == 250.0,
                  QStringLiteral("latest sample query returns engineering value"));
    ok &= require(reopened.pruneBefore(first.addSecs(1), &error), QStringLiteral("retention prune: %1").arg(error));
    ok &= require(reopened.sampleCount(&error) == 2, QStringLiteral("retention removes old samples"));
    ok &= require(reopened.integrityCheck(&error), QStringLiteral("reopened integrity: %1").arg(error));

    const QString arrayDatabasePath = directory.filePath(QStringLiteral("array.sqlite"));
    const PointTable arrayTable = makeArrayTable();
    AcquisitionStore arrayStore;
    ok &= require(arrayStore.open(arrayDatabasePath, &error),
                  QStringLiteral("open array store: %1").arg(error));
    PollResult arrayFrame1;
    arrayFrame1.frame = PollFrame{QStringLiteral("Rack Detail"), 4, 4097, 2, 1000};
    arrayFrame1.values = QVector<quint16>() << 3300 << 3310;
    arrayFrame1.success = true;
    arrayFrame1.attempts = 1;
    ok &= require(arrayStore.recordPollResult(arrayFrame1, arrayTable, QStringLiteral("bcu-array"), 1,
                                               first, &error),
                  QStringLiteral("record first array frame: %1").arg(error));
    PollResult arrayFrame2 = arrayFrame1;
    arrayFrame2.frame.address = 4099;
    arrayFrame2.values = QVector<quint16>() << 3320 << 3330;
    ok &= require(arrayStore.recordPollResult(arrayFrame2, arrayTable, QStringLiteral("bcu-array"), 1,
                                               first.addSecs(1), &error),
                  QStringLiteral("record second array frame: %1").arg(error));
    arrayStore.close();
    AcquisitionStore reopenedArray;
    ok &= require(reopenedArray.open(arrayDatabasePath, &error),
                  QStringLiteral("reopen array store: %1").arg(error));
    const QVector<AcquisitionSample> arrayLatest = reopenedArray.latestSamples(
        QStringLiteral("bcu-array"), 1, QStringLiteral("Rack Detail"), &error);
    const QVariantList arrayRaw = arrayLatest.isEmpty() ? QVariantList()
                                                         : arrayLatest.first().rawValue.toList();
    ok &= require(arrayRaw.size() == 4 && arrayRaw.at(0).toInt() == 3300 &&
                  arrayRaw.at(1).toInt() == 3310 && arrayRaw.at(2).toInt() == 3320 &&
                  arrayRaw.at(3).toInt() == 3330,
                  QStringLiteral("array frames merge and survive restart"));
    reopenedArray.close();

    const QString legacyPath = directory.filePath(QStringLiteral("legacy.sqlite"));
    QSqlDatabase legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("legacy_setup"));
    legacy.setDatabaseName(legacyPath);
    ok &= require(legacy.open(), QStringLiteral("open legacy database"));
    QSqlQuery legacyQuery(legacy);
    ok &= require(legacyQuery.exec(QStringLiteral(
        "CREATE TABLE communication_events (id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "device_id TEXT NOT NULL, slave_id INTEGER NOT NULL, event_type TEXT NOT NULL, "
        "severity TEXT NOT NULL, message TEXT NOT NULL, timestamp_ms INTEGER NOT NULL, "
        "function INTEGER NOT NULL, address INTEGER NOT NULL, count INTEGER NOT NULL, "
        "attempts INTEGER NOT NULL, elapsed_ms INTEGER NOT NULL)")),
        QStringLiteral("create legacy communication table"));
    legacy.close();
    QSqlDatabase::removeDatabase(QStringLiteral("legacy_setup"));
    AcquisitionStore migrated;
    ok &= require(migrated.open(legacyPath, &error), QStringLiteral("migrate legacy database: %1").arg(error));
    CommunicationEvent migratedEvent;
    migratedEvent.deviceId = QStringLiteral("bcu-01");
    migratedEvent.eventType = QStringLiteral("alarm_acknowledged");
    migratedEvent.severity = QStringLiteral("critical");
    migratedEvent.message = QStringLiteral("ack");
    migratedEvent.pointKey = QStringLiteral("extern_critical_alarm");
    migratedEvent.bit = 0;
    migratedEvent.rawValue = 1;
    migratedEvent.timestampUtc = first;
    ok &= require(migrated.recordCommunicationEvent(migratedEvent, &error),
                  QStringLiteral("write migrated event: %1").arg(error));
    migrated.close();
    return ok ? 0 : 1;
}
