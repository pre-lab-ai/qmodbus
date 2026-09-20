#include "modbussession.h"

#include <cerrno>

ModbusSession::ModbusSession() :
    m_context(NULL)
{
}

ModbusSession::~ModbusSession()
{
    close();
}

bool ModbusSession::adoptAndConnect(modbus_t* context)
{
    close();
    if( context == NULL )
        return false;

    if( modbus_connect(context) == -1 )
    {
        const int savedErrno = errno;
        modbus_free(context);
        errno = savedErrno;
        return false;
    }

    m_context = context;
    setMonitorCallbacks(m_monitorAddItem, m_monitorRawData);
    return true;
}

void ModbusSession::setMonitorCallbacks(modbus_monitor_add_item_fnc_t addItem,
                                         modbus_monitor_raw_data_fnc_t rawData)
{
    m_monitorAddItem = addItem;
    m_monitorRawData = rawData;
    if (!m_context)
        return;
    modbus_register_monitor_add_item_fnc(m_context, m_monitorAddItem);
    modbus_register_monitor_raw_data_fnc(m_context, m_monitorRawData);
}

bool ModbusSession::openTcp(const QString& address, int port)
{
    m_connectionType = ConnectionType::Tcp;
    m_endpoint = address;
    m_port = port;
    return adoptAndConnect(modbus_new_tcp(address.toLatin1().constData(), port));
}

bool ModbusSession::openRtu(const QString& port, int baud, char parity, int dataBits, int stopBits)
{
    m_connectionType = ConnectionType::Rtu;
    m_endpoint = port;
    m_baud = baud;
    m_parity = parity;
    m_dataBits = dataBits;
    m_stopBits = stopBits;
    return adoptAndConnect(modbus_new_rtu(port.toLatin1().constData(), baud, parity, dataBits, stopBits));
}

bool ModbusSession::openAscii(const QString& port, int baud, char parity, int dataBits, int stopBits)
{
    m_connectionType = ConnectionType::Ascii;
    m_endpoint = port;
    m_baud = baud;
    m_parity = parity;
    m_dataBits = dataBits;
    m_stopBits = stopBits;
    return adoptAndConnect(modbus_new_ascii(port.toLatin1().constData(), baud, parity, dataBits, stopBits));
}

bool ModbusSession::reconnect()
{
    modbus_t *context = NULL;
    switch (m_connectionType)
    {
    case ConnectionType::Tcp:
        context = modbus_new_tcp(m_endpoint.toLatin1().constData(), m_port);
        break;
    case ConnectionType::Rtu:
        context = modbus_new_rtu(m_endpoint.toLatin1().constData(), m_baud,
                                 m_parity, m_dataBits, m_stopBits);
        break;
    case ConnectionType::Ascii:
        context = modbus_new_ascii(m_endpoint.toLatin1().constData(), m_baud,
                                   m_parity, m_dataBits, m_stopBits);
        break;
    case ConnectionType::None:
        return false;
    }
    if (!adoptAndConnect(context))
        return false;
    return setSlave(m_slave) == 0;
}

QString ModbusSession::lastError() const
{
    return QString::fromLocal8Bit(modbus_strerror(errno));
}

void ModbusSession::close()
{
    if( m_context != NULL )
    {
        modbus_close(m_context);
        modbus_free(m_context);
        m_context = NULL;
    }
}

int ModbusSession::setSlave(int slave)
{
    if (!m_context)
        return -1;
    const int result = modbus_set_slave(m_context, slave);
    if (result == 0)
        m_slave = slave;
    return result;
}

int ModbusSession::readBits(int addr, int count, uint8_t* dest)
{
    return m_context ? modbus_read_bits(m_context, addr, count, dest) : -1;
}

int ModbusSession::readInputBits(int addr, int count, uint8_t* dest)
{
    return m_context ? modbus_read_input_bits(m_context, addr, count, dest) : -1;
}

int ModbusSession::readRegisters(int addr, int count, uint16_t* dest)
{
    return m_context ? modbus_read_registers(m_context, addr, count, dest) : -1;
}

int ModbusSession::readInputRegisters(int addr, int count, uint16_t* dest)
{
    return m_context ? modbus_read_input_registers(m_context, addr, count, dest) : -1;
}

int ModbusSession::writeBit(int addr, int value)
{
    return m_context ? modbus_write_bit(m_context, addr, value) : -1;
}

int ModbusSession::writeRegister(int addr, int value)
{
    return m_context ? modbus_write_register(m_context, addr, value) : -1;
}

int ModbusSession::writeBits(int addr, int count, const uint8_t* data)
{
    return m_context ? modbus_write_bits(m_context, addr, count, data) : -1;
}

int ModbusSession::writeRegisters(int addr, int count, const uint16_t* data)
{
    return m_context ? modbus_write_registers(m_context, addr, count, data) : -1;
}

void ModbusSession::poll()
{
    if( m_context != NULL )
        modbus_poll(m_context);
}

bool ModbusSessionTransport::isOpen() const
{
    return m_session && m_session->isOpen();
}

int ModbusSessionTransport::setSlave(int slave)
{
    return m_session ? m_session->setSlave(slave) : -1;
}

int ModbusSessionTransport::writeRegister(int address, quint16 value)
{
    if (!m_session)
        return -1;
    int result = m_session->writeRegister(address, value);
    if (result < 0 && (errno == ECONNRESET || errno == EPIPE || errno == ECONNABORTED) &&
        m_session->reconnect())
        result = m_session->writeRegister(address, value);
    return result;
}

int ModbusSessionTransport::readRegisters(int address, int count, quint16 *destination)
{
	if (!m_session)
		return -1;
	int result = m_session->readRegisters(address, count, destination);
	if (result < 0 && (errno == ECONNRESET || errno == EPIPE) && m_session->reconnect())
		result = m_session->readRegisters(address, count, destination);
	return result;
}

int ModbusSessionTransport::readInputRegisters(int address, int count, quint16 *destination)
{
	if (!m_session)
		return -1;
	int result = m_session->readInputRegisters(address, count, destination);
	if (result < 0 && (errno == ECONNRESET || errno == EPIPE) && m_session->reconnect())
		result = m_session->readInputRegisters(address, count, destination);
	return result;
}

int ModbusSessionTransport::writeRegisters(int address, int count, const quint16 *values)
{
    if (!m_session)
        return -1;
    int result = m_session->writeRegisters(address, count, values);
    if (result < 0 && (errno == ECONNRESET || errno == EPIPE || errno == ECONNABORTED) &&
        m_session->reconnect())
        result = m_session->writeRegisters(address, count, values);
    return result;
}

QString ModbusSessionTransport::lastError() const
{
    if (!m_session)
        return QStringLiteral("modbus session is not configured");
    return QString::fromLocal8Bit(modbus_strerror(errno));
}
