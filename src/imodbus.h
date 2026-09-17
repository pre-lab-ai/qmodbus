#ifndef IMODBUS_H
#define IMODBUS_H

class ModbusSession;

class IModbus
{
public:
	virtual ModbusSession* session() = 0;
	virtual int        setupModbusPort() = 0;
};

#endif // IMODBUS_H
