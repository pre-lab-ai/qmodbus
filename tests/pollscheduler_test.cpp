#include <QCoreApplication>
#include <QTimer>
#include <QTextStream>

#include "../src/pollscheduler.h"

class FakeTransport : public IPollTransport
{
public:
    bool open = true;
    int setSlaveCalls = 0;
    int readCalls = 0;
    int failFirstReads = 0;
    QString errorText;
    QList<PollFrame> calls;

    bool isOpen() const override { return open; }
    int setSlave(int) override { ++setSlaveCalls; return open ? 0 : -1; }
    int readRegisters(int address, int count, quint16 *destination) override
    {
        ++readCalls;
        PollFrame frame;
        frame.function = 3;
        frame.address = address;
        frame.count = count;
        calls.append(frame);
        if (failFirstReads > 0)
        {
            --failFirstReads;
            errorText = QStringLiteral("simulated timeout");
            return -1;
        }
        for (int i = 0; i < count; ++i)
            destination[i] = static_cast<quint16>(address + i);
        return count;
    }
    int readInputRegisters(int address, int count, quint16 *destination) override
    {
        ++readCalls;
        PollFrame frame;
        frame.function = 4;
        frame.address = address;
        frame.count = count;
        calls.append(frame);
        for (int i = 0; i < count; ++i)
            destination[i] = static_cast<quint16>(address + i);
        return count;
    }
    QString lastError() const override { return errorText; }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    auto require = [&ok](bool value, const QString &message) {
        if (!value)
            QTextStream(stderr) << "FAIL: " << message << "\n";
        ok = ok && value;
    };

    const QVector<PollFrame> split = PollPlan::splitRange(QStringLiteral("Rack Detail"), 4, 0x1401, 121, 120, 20);
    require(split.size() == 2, QStringLiteral("121 registers split into two frames"));
    require(split.at(0).count == 120 && split.at(1).count == 1, QStringLiteral("120/1 frame boundary"));
    require(split.at(1).address == 0x1401 + 120, QStringLiteral("split address continuity"));
    require(PollPlan::splitRange(QStringLiteral("Rack Detail"), 4, 0x1401, 120).size() == 1,
            QStringLiteral("120 registers stay in one frame"));
    const QVector<PollFrame> split125 = PollPlan::splitRange(QStringLiteral("Rack Detail"), 4, 0x1401, 125);
    require(split125.size() == 2 && split125.at(0).count == 120 && split125.at(1).count == 5,
            QStringLiteral("125 registers split into 120/5"));

    PointTable table;
    QStringList errors;
    const QByteArray json = QByteArrayLiteral(
        "{\"points\":["
        "{\"display_name\":\"a\",\"key\":\"a\",\"block\":\"Rack Measure\",\"address\":528,\"count\":2,\"type\":\"u16\",\"attribute\":\"R\",\"read_functions\":[4],\"reserved\":false},"
        "{\"display_name\":\"b\",\"key\":\"b\",\"block\":\"Rack Measure\",\"address\":530,\"count\":2,\"type\":\"u16\",\"attribute\":\"R\",\"read_functions\":[4],\"reserved\":false}]}" );
    require(table.loadJson(json, &errors), QStringLiteral("minimal table loads: %1").arg(errors.join(';')));
    const QVector<PollFrame> plan = PollPlan::fromPointTable(table, 120, 20);
    require(plan.size() == 1 && plan.first().address == 528 && plan.first().count == 4,
            QStringLiteral("contiguous points merge into one frame"));

    PointTable pcsTable;
    errors.clear();
    const QByteArray pcsJson = QByteArrayLiteral(
        "{\"points\":["
        "{\"display_name\":\"pcs\",\"key\":\"pcs\",\"block\":\"PCS\",\"address\":0,\"count\":16,\"type\":\"u16\",\"attribute\":\"R\",\"read_functions\":[3],\"reserved\":false}]}" );
    require(pcsTable.loadJson(pcsJson, &errors),
            QStringLiteral("PCS table loads: %1").arg(errors.join(';')));
    const QVector<PollFrame> pcsPlan = PollPlan::fromPointTable(pcsTable, 10, 20,
                                                                  QStringLiteral("PCS"));
    require(pcsPlan.size() == 1 && pcsPlan.first().function == 3 &&
            pcsPlan.first().address == 0 && pcsPlan.first().count == 16,
            QStringLiteral("PCS registers use one function 03 frame"));

    FakeTransport transport;
    transport.failFirstReads = 1;
    PollScheduler scheduler;
    scheduler.setTransport(&transport);
    scheduler.setPlan(QVector<PollFrame>() << PollFrame{QStringLiteral("Rack Measure"), 3, 0x0210, 1, 20});
    scheduler.setSlave(2);
    scheduler.setMaxRetries(1);
    int results = 0;
    bool resultSuccess = false;
    int attempts = 0;
    QObject::connect(&scheduler, &PollScheduler::resultReady, [&](const PollResult &result) {
        ++results;
        resultSuccess = result.success;
        attempts = result.attempts;
        if (results >= 1)
            scheduler.stop();
    });
    scheduler.start();
    QTimer::singleShot(500, &app, &QCoreApplication::quit);
    app.exec();
    require(results >= 1, QStringLiteral("worker produced a result"));
    require(resultSuccess && attempts == 2, QStringLiteral("one retry recovers simulated failure"));
    require(transport.readCalls == 2, QStringLiteral("transport calls are serialized and retried once"));
    require(!scheduler.isRunning(), QStringLiteral("scheduler stopped cleanly"));

    if (argc > 1)
    {
        PointTable generated;
        errors.clear();
        require(generated.load(QString::fromLocal8Bit(argv[1]), &errors),
                QStringLiteral("generated point table loads: %1").arg(errors.join(';')));
        const QVector<PollFrame> generatedPlan = PollPlan::fromPointTable(generated, 120, 1000);
        require(!generatedPlan.isEmpty(), QStringLiteral("generated plan is not empty"));
        for (const PollFrame &frame : generatedPlan)
        {
            require(frame.block != QStringLiteral("Alarm parameters"),
                    QStringLiteral("alarm parameters are excluded from background polling"));
            require(frame.count > 0 && frame.count <= 120, QStringLiteral("generated frame respects 120 register limit"));
        }
    }

    return ok ? 0 : 1;
}
