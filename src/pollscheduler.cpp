#include "pollscheduler.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QSet>

#include <algorithm>

namespace
{
int preferredFunction(const PointDefinition &point)
{
    if (point.block == QStringLiteral("PCS"))
        return point.readFunctions.contains(3) ? 3 : 4;
    if (point.block == QStringLiteral("Rack Control") ||
        point.block == QStringLiteral("Alarm parameters"))
        return point.readFunctions.contains(3) ? 3 : 4;
    return point.readFunctions.contains(4) ? 4 : 3;
}
}

QVector<PollFrame> PollPlan::splitRange(const QString &block, int function,
                                        int address, int count,
                                        int maxRegisters, int periodMs)
{
    QVector<PollFrame> frames;
    if (count <= 0 || maxRegisters <= 0 || address < 0 || address > 0xffff)
        return frames;

    int remaining = count;
    int current = address;
    while (remaining > 0 && current <= 0xffff)
    {
        const int frameCount = qMin(remaining, maxRegisters);
        if (current + frameCount - 1 > 0xffff)
            break;
        PollFrame frame;
        frame.block = block;
        frame.function = function;
        frame.address = current;
        frame.count = frameCount;
        frame.periodMs = qMax(1, periodMs);
        frames.append(frame);
        current += frameCount;
        remaining -= frameCount;
    }
    return frames;
}

QVector<PollFrame> PollPlan::fromPointTable(const PointTable &table,
                                           int maxRegisters, int periodMs,
                                           const QString &includeBlock,
                                           const QStringList &excludeBlocks)
{
    struct Range
    {
        QString block;
        int function = 4;
        int first = 0;
        int last = -1;
    };

    QVector<Range> ranges;
    for (const PointDefinition &point : table.points())
    {
        if ((!includeBlock.isEmpty() && point.block != includeBlock) ||
            excludeBlocks.contains(point.block))
            continue;
        // Alarm parameters are configuration/write points.  They are not
        // displayed by the acquisition pages and several BCU firmware
        // versions reject reads in this range (for example 0x6065) with
        // Illegal data address.  Do not put them in the background poll
        // plan; they remain available through explicit control operations.
        if (point.reserved || point.block == QStringLiteral("Alarm parameters") ||
            point.count <= 0 || point.readFunctions.isEmpty())
            continue;

        const int function = preferredFunction(point);
        Range *merged = nullptr;
        for (Range &range : ranges)
        {
            if (range.block == point.block && range.function == function &&
                point.address <= range.last + 1 && point.lastAddress() >= range.first - 1)
            {
                merged = &range;
                break;
            }
        }

        if (merged)
            merged->last = qMax(merged->last, point.lastAddress());
        else
        {
            Range range;
            range.block = point.block;
            range.function = function;
            range.first = point.address;
            range.last = point.lastAddress();
            ranges.append(range);
        }
    }

    std::sort(ranges.begin(), ranges.end(), [](const Range &left, const Range &right) {
        if (left.block != right.block)
            return left.block < right.block;
        if (left.function != right.function)
            return left.function < right.function;
        return left.first < right.first;
    });

    QVector<PollFrame> frames;
    for (const Range &range : ranges)
    {
        // BCU firmware is limited to ten registers per request. PCS follows
        // the normal Modbus limit, so keep its 16-register table in one frame.
        const int rangeMaxRegisters = range.block == QStringLiteral("PCS")
                ? qMax(maxRegisters, 16) : maxRegisters;
        frames += splitRange(range.block, range.function, range.first,
                             range.last - range.first + 1, rangeMaxRegisters, periodMs);
    }
    return frames;
}

PollWorker::PollWorker(IPollTransport *transport, QObject *parent) :
    QObject(parent),
    m_transport(transport),
    m_slave(1),
    m_maxRetries(1),
    m_nextIndex(0),
    m_running(false),
    m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(processNext()));
}

void PollWorker::startPolling(const QVector<PollFrame> &frames, int slave, int maxRetries)
{
    m_frames = frames;
    m_slave = slave;
    m_maxRetries = qBound(0, maxRetries, 5);
    m_nextIndex = 0;
    m_running = !m_frames.isEmpty();
    m_nextDueMs.fill(0, m_frames.size());
    m_failureStreaks.fill(0, m_frames.size());
    m_clock.restart();
    if (!m_running)
    {
        emit finished();
        return;
    }
    m_timer->start(0);
}

void PollWorker::stopPolling()
{
    m_running = false;
    m_timer->stop();
    emit finished();
}

void PollWorker::processNext()
{
    if (!m_running)
        return;

    const qint64 now = m_clock.elapsed();
    int selected = -1;
    qint64 earliestDue = 0;
    for (int offset = 0; offset < m_frames.size(); ++offset)
    {
        const int index = (m_nextIndex + offset) % m_frames.size();
        const qint64 due = m_nextDueMs.at(index);
        if (selected < 0 || due < earliestDue)
        {
            selected = index;
            earliestDue = due;
        }
    }

    if (selected < 0)
        return;
    if (earliestDue > now)
    {
        m_timer->start(static_cast<int>(qMin<qint64>(earliestDue - now, 1000)));
        return;
    }

    m_nextIndex = (selected + 1) % m_frames.size();
    const PollFrame frame = m_frames.at(selected);
    const PollResult result = execute(frame);
    emit resultReady(result);

    if (result.success)
        m_failureStreaks[selected] = 0;
    else
        m_failureStreaks[selected] = qMin(m_failureStreaks.at(selected) + 1, 5);
    const qint64 multiplier = static_cast<qint64>(1) << m_failureStreaks.at(selected);
    const qint64 delay = qMin<qint64>(static_cast<qint64>(frame.periodMs) * multiplier, 30000);
    m_nextDueMs[selected] = m_clock.elapsed() + delay;
    if (m_running)
        m_timer->start(0);
}

PollResult PollWorker::execute(const PollFrame &frame)
{
    PollResult result;
    result.frame = frame;
    QElapsedTimer timer;
    timer.start();

    if (!m_transport || !m_transport->isOpen())
    {
        result.error = QStringLiteral("transport is not open");
        result.elapsedMs = timer.elapsed();
        return result;
    }
    if (m_transport->setSlave(m_slave) < 0)
    {
        result.error = m_transport->lastError();
        if (result.error.isEmpty())
            result.error = QStringLiteral("failed to set slave %1").arg(m_slave);
        result.elapsedMs = timer.elapsed();
        return result;
    }

    QVector<quint16> values(frame.count);
    for (int attempt = 1; attempt <= m_maxRetries + 1; ++attempt)
    {
        result.attempts = attempt;
        const int received = frame.function == 3
            ? m_transport->readRegisters(frame.address, frame.count, values.data())
            : m_transport->readInputRegisters(frame.address, frame.count, values.data());
        if (received == frame.count)
        {
            result.success = true;
            result.values = values;
            result.error.clear();
            break;
        }
        result.error = m_transport->lastError();
        if (result.error.isEmpty())
            result.error = QStringLiteral("read returned %1 of %2 registers")
                               .arg(received).arg(frame.count);
    }
    result.elapsedMs = timer.elapsed();
    return result;
}

PollScheduler::PollScheduler(QObject *parent) :
    QObject(parent),
    m_transport(nullptr),
    m_slave(1),
    m_maxRetries(1),
    m_thread(nullptr),
    m_worker(nullptr)
{
    qRegisterMetaType<QVector<PollFrame> >("QVector<PollFrame>");
    qRegisterMetaType<PollFrame>("PollFrame");
    qRegisterMetaType<PollResult>("PollResult");
}

PollScheduler::~PollScheduler()
{
    stop();
}

void PollScheduler::setTransport(IPollTransport *transport)
{
    if (isRunning())
        stop();
    m_transport = transport;
}

void PollScheduler::setPlan(const QVector<PollFrame> &frames)
{
    if (!isRunning())
        m_frames = frames;
}

void PollScheduler::setSlave(int slave)
{
    m_slave = qBound(1, slave, 247);
}

void PollScheduler::setMaxRetries(int retries)
{
    m_maxRetries = qBound(0, retries, 5);
}

void PollScheduler::start()
{
    if (isRunning())
        return;
    if (!m_transport)
    {
        emit error(QStringLiteral("poll scheduler has no transport"));
        return;
    }
    if (m_frames.isEmpty())
    {
        emit error(QStringLiteral("poll scheduler has no frames"));
        return;
    }

    m_thread = new QThread(this);
    m_worker = new PollWorker(m_transport);
    m_worker->moveToThread(m_thread);
    connect(m_worker, SIGNAL(resultReady(PollResult)), this, SIGNAL(resultReady(PollResult)), Qt::QueuedConnection);
    connect(m_worker, SIGNAL(finished()), m_thread, SLOT(quit()));
    m_thread->start();
    QMetaObject::invokeMethod(m_worker, "startPolling", Qt::QueuedConnection,
                              Q_ARG(QVector<PollFrame>, m_frames),
                              Q_ARG(int, m_slave), Q_ARG(int, m_maxRetries));
}

void PollScheduler::stop()
{
    if (!m_thread)
        return;
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stopPolling", Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
    delete m_worker;
    m_worker = nullptr;
    delete m_thread;
    m_thread = nullptr;
}
