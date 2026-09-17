#ifndef CONTROLTRANSACTION_H
#define CONTROLTRANSACTION_H

#include "controlvalidator.h"

class IControlTransport
{
public:
    virtual ~IControlTransport() = default;
    virtual bool isOpen() const = 0;
    virtual int setSlave(int slave) = 0;
    virtual int writeRegister(int address, quint16 value) = 0;
    virtual int writeRegisters(int address, int count, const quint16 *values) = 0;
    virtual int readRegisters(int address, int count, quint16 *values) = 0;
    virtual QString lastError() const = 0;
};

struct ControlTransactionResult
{
    bool accepted = false;
    bool written = false;
    bool readbackVerified = false;
    QString error;
};

class ControlTransaction
{
public:
    static ControlTransactionResult execute(IControlTransport &transport,
                                            const PointDefinition &point,
                                            int slave, int function,
                                            const QVector<quint16> &raw,
                                            const QString &userRole = QStringLiteral("operator"));
};

#endif
