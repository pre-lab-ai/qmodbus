#ifndef POLLSCHEDULER_H
#define POLLSCHEDULER_H

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVector>

#include "pointmodel.h"

struct PollFrame
{
    QString block;
    int function = 4;
    int address = 0;
    int count = 0;
    int periodMs = 1000;

    int lastAddress() const { return address + count - 1; }
};

struct PollResult
{
    PollFrame frame;
    QVector<quint16> values;
    bool success = false;
    int attempts = 0;
    qint64 elapsedMs = 0;
    QString error;
};

Q_DECLARE_METATYPE(PollFrame)
Q_DECLARE_METATYPE(PollResult)

class IPollTransport
{
public:
    virtual ~IPollTransport() {}
    virtual bool isOpen() const = 0;
    virtual int setSlave(int slave) = 0;
    virtual int readRegisters(int address, int count, quint16 *destination) = 0;
    virtual int readInputRegisters(int address, int count, quint16 *destination) = 0;
    virtual QString lastError() const = 0;
};

class PollPlan
{
public:
    static QVector<PollFrame> fromPointTable(const PointTable &table,
                                               int maxRegisters = 120,
                                               int periodMs = 1000);
    static QVector<PollFrame> splitRange(const QString &block, int function,
                                         int address, int count,
                                         int maxRegisters = 120,
                                         int periodMs = 1000);
};

class PollWorker : public QObject
{
    Q_OBJECT

public:
    explicit PollWorker(IPollTransport *transport, QObject *parent = nullptr);

public slots:
    void startPolling(const QVector<PollFrame> &frames, int slave, int maxRetries);
    void stopPolling();

signals:
    void resultReady(const PollResult &result);
    void finished();

private slots:
    void processNext();

private:
    PollResult execute(const PollFrame &frame);

    IPollTransport *m_transport;
    QVector<PollFrame> m_frames;
    QVector<qint64> m_nextDueMs;
    QVector<int> m_failureStreaks;
    QElapsedTimer m_clock;
    int m_slave;
    int m_maxRetries;
    int m_nextIndex;
    bool m_running;
    QTimer *m_timer;
};

class PollScheduler : public QObject
{
    Q_OBJECT

public:
    explicit PollScheduler(QObject *parent = nullptr);
    ~PollScheduler();

    void setTransport(IPollTransport *transport);
    void setPlan(const QVector<PollFrame> &frames);
    void setSlave(int slave);
    void setMaxRetries(int retries);
    QVector<PollFrame> plan() const { return m_frames; }
    bool isRunning() const { return m_thread != nullptr; }

public slots:
    void start();
    void stop();

signals:
    void resultReady(const PollResult &result);
    void error(const QString &message);

private:
    IPollTransport *m_transport;
    QVector<PollFrame> m_frames;
    int m_slave;
    int m_maxRetries;
    QThread *m_thread;
    PollWorker *m_worker;
};

#endif // POLLSCHEDULER_H
