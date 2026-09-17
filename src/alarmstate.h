#ifndef ALARMSTATE_H
#define ALARMSTATE_H

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

struct AlarmState
{
    QString key;
    QString displayName;
    int bit = -1;
    bool active = false;
    bool acknowledged = false;
    QDateTime firstOccurredUtc;
    QDateTime lastOccurredUtc;
    QDateTime recoveredUtc;
    QDateTime acknowledgedUtc;
    QString acknowledgedBy;
    QString severity;
};

struct AlarmTransition
{
    QString event;
    AlarmState state;
};

class AlarmStateModel
{
public:
    QVector<AlarmTransition> update(const QString &sourceKey, const QString &displayName,
                                    int bit, bool active, const QDateTime &timestampUtc,
                                    const QString &severity = QStringLiteral("warning"));
    bool acknowledge(const QString &key, const QDateTime &timestampUtc,
                     const QString &userId, AlarmTransition *transition = nullptr);
    const QHash<QString, AlarmState> &states() const { return m_states; }

private:
    QHash<QString, AlarmState> m_states;
};

#endif
