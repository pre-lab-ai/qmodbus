#ifndef ACQUISITIONSTORE_H
#define ACQUISITIONSTORE_H

#include <QDateTime>
#include <QHash>
#include <QVariant>

#include "pollscheduler.h"
#include "qualitycode.h"

struct AcquisitionSample
{
    QString deviceId;
    int slaveId = 1;
    QString pointKey;
    QString displayName;
    QString block;
    int address = 0;
    QVariant rawValue;
    QVariant engineeringValue;
    QString unit;
    QualityCode quality = QualityCode::InvalidData;
    QDateTime timestampUtc;
    qint64 elapsedMs = 0;
    int function = 4;
    QString error;
};

struct CommunicationEvent
{
    QString deviceId;
    int slaveId = 1;
    QString eventType;
    QString severity;
    QString message;
    QString pointKey;
    int bit = -1;
    quint16 rawValue = 0;
    QDateTime timestampUtc;
    int function = 4;
    int address = 0;
    int count = 0;
    int attempts = 0;
    qint64 elapsedMs = 0;
};

struct AuditEvent
{
    QString deviceId;
    QString userId;
    QString pointKey;
    QString displayName;
    int slaveId = 1;
    QVariant oldValue;
    QVariant newValue;
    QString result;
    QString message;
    int function = 0;
    int address = 0;
    int count = 0;
    QDateTime timestampUtc;
};

class AcquisitionStore
{
public:
    AcquisitionStore();
    ~AcquisitionStore();

    AcquisitionStore(const AcquisitionStore &) = delete;
    AcquisitionStore &operator=(const AcquisitionStore &) = delete;

    bool open(const QString &databasePath, QString *error = nullptr);
    void close();
    bool isOpen() const;

    bool recordPollResult(const PollResult &result,
                          const PointTable &pointTable,
                          const QString &deviceId,
                          int slaveId,
                          const QDateTime &timestampUtc = QDateTime(),
                          QString *error = nullptr);
    bool recordCommunicationEvent(const CommunicationEvent &event,
                                  QString *error = nullptr);
    bool recordAuditEvent(const AuditEvent &event, QString *error = nullptr);

    bool exportCsv(const QString &filePath,
                   const QDateTime &fromUtc = QDateTime(),
                   const QDateTime &toUtc = QDateTime(),
                   QString *error = nullptr) const;
    QVector<AcquisitionSample> latestSamples(const QString &deviceId, int slaveId,
                                             const QString &block = QString(),
                                             QString *error = nullptr) const;
    bool pruneBefore(const QDateTime &cutoffUtc, QString *error = nullptr);
    bool integrityCheck(QString *error = nullptr) const;
    qint64 sampleCount(QString *error = nullptr) const;
    qint64 communicationEventCount(QString *error = nullptr) const;
    qint64 auditEventCount(QString *error = nullptr) const;

private:
    bool initialize(QString *error);
    bool insertSample(const AcquisitionSample &sample, QString *error);
    bool insertCommunicationEvent(const CommunicationEvent &event, QString *error);
    bool insertAuditEvent(const AuditEvent &event, QString *error);

    QString m_connectionName;
    class QSqlDatabase *m_database;
    QHash<QString, bool> m_frameFailures;
};

#endif // ACQUISITIONSTORE_H
