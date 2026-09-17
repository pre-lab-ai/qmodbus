#include "alarmstate.h"

QVector<AlarmTransition> AlarmStateModel::update(const QString &sourceKey, const QString &displayName,
                                                 int bit, bool active, const QDateTime &timestampUtc,
                                                 const QString &severity)
{
    const QString key = QStringLiteral("%1:%2").arg(sourceKey).arg(bit);
    AlarmState &state = m_states[key];
    QVector<AlarmTransition> transitions;
    if (state.key.isEmpty())
    {
        state.key = key;
        state.displayName = displayName;
        state.bit = bit;
        state.severity = severity;
    }
    if (active && !state.active)
    {
        state.active = true;
        state.acknowledged = false;
        state.firstOccurredUtc = timestampUtc;
        state.lastOccurredUtc = timestampUtc;
        state.recoveredUtc = QDateTime();
        transitions.append({QStringLiteral("occurred"), state});
    }
    else if (active)
    {
        state.lastOccurredUtc = timestampUtc;
    }
    else if (!active && state.active)
    {
        state.active = false;
        state.recoveredUtc = timestampUtc;
        transitions.append({QStringLiteral("recovered"), state});
    }
    return transitions;
}

bool AlarmStateModel::acknowledge(const QString &key, const QDateTime &timestampUtc,
                                  const QString &userId, AlarmTransition *transition)
{
    auto it = m_states.find(key);
    if (it == m_states.end() || !it->active || it->acknowledged)
        return false;
    it->acknowledged = true;
    it->acknowledgedUtc = timestampUtc.toUTC();
    it->acknowledgedBy = userId;
    if (transition)
        *transition = {QStringLiteral("acknowledged"), it.value()};
    return true;
}
