#include "controltransaction.h"

ControlTransactionResult ControlTransaction::execute(IControlTransport &transport,
                                                     const PointDefinition &point,
                                                     int slave, int function,
                                                     const QVector<quint16> &raw,
                                                     const QString &userRole)
{
    ControlTransactionResult result;
    const ControlValidation validation = ControlValidator::validateRaw(point, function, raw, userRole);
    if (!validation.accepted)
    {
        result.error = validation.error;
        return result;
    }
    if (!transport.isOpen() || transport.setSlave(slave) < 0)
    {
        result.error = transport.lastError().isEmpty() ? QStringLiteral("transport is not open") : transport.lastError();
        return result;
    }
    const int written = function == 6 && point.count == 1
        ? transport.writeRegister(point.address, raw.first())
        : transport.writeRegisters(point.address, point.count, raw.constData());
    if (written != point.count)
    {
        result.error = transport.lastError().isEmpty()
            ? QStringLiteral("write returned %1 of %2 registers").arg(written).arg(point.count)
            : transport.lastError();
        return result;
    }
    result.written = true;
    QVector<quint16> readback(point.count);
    const int received = transport.readRegisters(point.address, point.count, readback.data());
    if (received != point.count)
    {
        result.error = transport.lastError().isEmpty()
            ? QStringLiteral("readback returned %1 of %2 registers").arg(received).arg(point.count)
            : transport.lastError();
        return result;
    }
    for (int i = 0; i < point.count; ++i)
    {
        if (readback.at(i) != raw.at(i))
        {
            result.error = QStringLiteral("readback mismatch at address %1").arg(point.address + i);
            return result;
        }
    }
    result.readbackVerified = true;
    result.accepted = true;
    return result;
}
