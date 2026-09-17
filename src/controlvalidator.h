#ifndef CONTROLVALIDATOR_H
#define CONTROLVALIDATOR_H

#include "pointmodel.h"

struct ControlValidation
{
    bool accepted = false;
    QString error;
};

class ControlValidator
{
public:
    static ControlValidation validateRaw(const PointDefinition &point, int function,
                                         const QVector<quint16> &raw,
                                         const QString &userRole = QStringLiteral("operator"));
};

#endif
