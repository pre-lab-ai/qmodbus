#ifndef MODBUSSESSION_H
#define MODBUSSESSION_H

#include <QString>

#include "modbus.h"
#include "pollscheduler.h"
#include "controltransaction.h"

class ModbusSession
{
public:
    ModbusSession();
    ~ModbusSession();

    ModbusSession(const ModbusSession&) = delete;
    ModbusSession& operator=(const ModbusSession&) = delete;

    bool openTcp(const QString& address, int port);
    bool openRtu(const QString& port, int baud, char parity, int dataBits, int stopBits);
    bool openAscii(const QString& port, int baud, char parity, int dataBits, int stopBits);
    bool reconnect();
    void setMonitorCallbacks(modbus_monitor_add_item_fnc_t addItem,
                             modbus_monitor_raw_data_fnc_t rawData);
    void close();

    bool isOpen() const { return m_context != NULL; }
    modbus_t* raw() const { return m_context; }
    QString lastError() const;

    int setSlave(int slave);
    int readBits(int addr, int count, uint8_t* dest);
    int readInputBits(int addr, int count, uint8_t* dest);
    int readRegisters(int addr, int count, uint16_t* dest);
    int readInputRegisters(int addr, int count, uint16_t* dest);
    int writeBit(int addr, int value);
    int writeRegister(int addr, int value);
    int writeBits(int addr, int count, const uint8_t* data);
    int writeRegisters(int addr, int count, const uint16_t* data);
    void poll();

private:
    enum class ConnectionType { None, Tcp, Rtu, Ascii };
    bool adoptAndConnect(modbus_t* context);

    modbus_t* m_context;
    ConnectionType m_connectionType = ConnectionType::None;
    QString m_endpoint;
    int m_port = 0;
    int m_baud = 0;
    char m_parity = 'N';
    int m_dataBits = 8;
    int m_stopBits = 1;
    int m_slave = 1;
    modbus_monitor_add_item_fnc_t m_monitorAddItem = nullptr;
    modbus_monitor_raw_data_fnc_t m_monitorRawData = nullptr;
};

class ModbusSessionTransport : public IPollTransport, public IControlTransport
{
public:
    explicit ModbusSessionTransport(ModbusSession *session = nullptr) : m_session(session) {}

    void setSession(ModbusSession *session) { m_session = session; }
    bool isOpen() const override;
    int setSlave(int slave) override;
    int writeRegister(int address, quint16 value) override;
    int readRegisters(int address, int count, quint16 *destination) override;
    int readInputRegisters(int address, int count, quint16 *destination) override;
    int writeRegisters(int address, int count, const quint16 *values) override;
    QString lastError() const override;

private:
    ModbusSession *m_session;
};

#endif
