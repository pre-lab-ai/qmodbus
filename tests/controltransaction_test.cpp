#include <QCoreApplication>
#include <QHash>
#include <QTextStream>

#include "../src/controltransaction.h"

namespace
{
enum class Fault { None, Reject, Timeout, Mismatch, ShortWrite };

class FakeSlave : public IControlTransport
{
public:
    explicit FakeSlave(Fault fault = Fault::None) : m_fault(fault) {}
    bool isOpen() const override { return true; }
    int setSlave(int slave) override { m_slave = slave; return 0; }
    int writeRegister(int address, quint16 value) override
    {
        if (m_fault == Fault::Reject || m_fault == Fault::Timeout)
        {
            m_error = m_fault == Fault::Timeout ? QStringLiteral("timeout") : QStringLiteral("device rejected write");
            return -1;
        }
        if (m_fault == Fault::ShortWrite)
        {
            m_error = QStringLiteral("unexpected response length");
            return 0;
        }
        m_registers[address] = value;
        return 1;
    }
    int writeRegisters(int address, int count, const quint16 *values) override
    {
        if (m_fault == Fault::Reject || m_fault == Fault::Timeout)
        {
            m_error = m_fault == Fault::Timeout ? QStringLiteral("timeout") : QStringLiteral("device rejected write");
            return -1;
        }
        if (m_fault == Fault::ShortWrite)
        {
            m_error = QStringLiteral("unexpected response length");
            return count - 1;
        }
        for (int i = 0; i < count; ++i) m_registers[address + i] = values[i];
        return count;
    }
    int readRegisters(int address, int count, quint16 *values) override
    {
        ++m_readCount;
        if (m_fault == Fault::Timeout)
        {
            m_error = QStringLiteral("timeout");
            return -1;
        }
        for (int i = 0; i < count; ++i)
            values[i] = m_fault == Fault::Mismatch && i == 0 ? 0 : m_registers.value(address + i, 0);
        return count;
    }
    QString lastError() const override { return m_error; }
    int readCount() const { return m_readCount; }
private:
    Fault m_fault;
    int m_slave = 0;
    QHash<int, quint16> m_registers;
    int m_readCount = 0;
    QString m_error;
};

bool require(bool value, const QString &message)
{
    if (!value) QTextStream(stderr) << "FAIL: " << message << "\n";
    return value;
}

PointDefinition point()
{
    PointDefinition p;
    p.key = QStringLiteral("control");
    p.displayName = QStringLiteral("Control");
    p.address = 0x0466;
    p.count = 1;
    p.type = PointDataType::U16;
    p.access = PointAccess::ReadWrite;
    p.writeFunctions << 6 << 16;
    return p;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    const PointDefinition p = point();
    const QVector<quint16> raw = QVector<quint16>() << 2;
    FakeSlave normal(Fault::None);
    const auto success = ControlTransaction::execute(normal, p, 1, 6, raw);
    ok &= require(success.accepted && success.written && success.readbackVerified, QStringLiteral("successful write and readback"));
    const auto multipleSuccess = ControlTransaction::execute(normal, p, 1, 16, raw);
    ok &= require(multipleSuccess.accepted && multipleSuccess.readbackVerified,
                  QStringLiteral("multiple-register write and readback"));
    FakeSlave reject(Fault::Reject);
    ok &= require(!ControlTransaction::execute(reject, p, 1, 6, raw).accepted, QStringLiteral("device rejection"));
    FakeSlave timeout(Fault::Timeout);
    ok &= require(!ControlTransaction::execute(timeout, p, 1, 6, raw).accepted, QStringLiteral("timeout"));
    FakeSlave shortWrite(Fault::ShortWrite);
    ok &= require(!ControlTransaction::execute(shortWrite, p, 1, 6, raw).accepted,
                  QStringLiteral("unexpected response length rejected"));
    FakeSlave mismatch(Fault::Mismatch);
    const auto mismatchResult = ControlTransaction::execute(mismatch, p, 1, 6, raw);
    ok &= require(mismatchResult.written && !mismatchResult.readbackVerified && !mismatchResult.accepted,
                  QStringLiteral("readback mismatch"));
    FakeSlave commandMismatch(Fault::Mismatch);
    const auto commandResult = ControlTransaction::execute(commandMismatch, p, 1, 6, raw,
                                                           QStringLiteral("operator"), false);
    ok &= require(commandResult.accepted && commandResult.written && commandResult.readbackVerified,
                  QStringLiteral("one-shot command accepted without readback"));
    ok &= require(commandMismatch.readCount() == 0,
                  QStringLiteral("one-shot command skips readback request"));
    return ok ? 0 : 1;
}
