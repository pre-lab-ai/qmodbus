#include "modbusresponder.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <cerrno>

namespace
{
quint16 modbusRtuCrc(const QByteArray &frame)
{
    quint16 crc = 0xffff;
    for (const unsigned char byte : frame)
    {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? static_cast<quint16>((crc >> 1) ^ 0xa001)
                             : static_cast<quint16>(crc >> 1);
    }
    return crc;
}

QByteArray confirmationFrame(const uint8_t *request, int length,
                             bool tcp, int unitOffset)
{
    if (!request || length < unitOffset + 6 ||
        request[unitOffset + 1] != MODBUS_FC_WRITE_MULTIPLE_REGISTERS)
        return {};

    const int pduLength = 6;
    if (tcp)
    {
        if (length < 12)
            return {};
        QByteArray response(reinterpret_cast<const char *>(request), 12);
        response[4] = 0;
        response[5] = static_cast<char>(pduLength);
        return response;
    }

    QByteArray response(reinterpret_cast<const char *>(request + unitOffset), pduLength);
    const quint16 crc = modbusRtuCrc(response);
    response.append(static_cast<char>(crc & 0xff));
    response.append(static_cast<char>((crc >> 8) & 0xff));
    return response;
}
}

ModbusResponder::ModbusResponder(QObject *parent) :
    QThread(parent)
{
    qRegisterMetaType<QVector<quint16>>();
}

ModbusResponder::~ModbusResponder()
{
    stop();
}

bool ModbusResponder::startTcp(const QString &bindAddress, int port, int slaveId)
{
    stop();
    m_transport = Transport::Tcp;
    m_bindAddress = bindAddress.trimmed();
    m_port = port;
    m_slaveId = qBound(1, slaveId, 247);
    m_stopRequested.store(false);
    start();
    return true;
}

bool ModbusResponder::startRtu(const QString &port, int baud, char parity,
                               int dataBits, int stopBits, int slaveId)
{
    stop();
    m_transport = Transport::Rtu;
    m_endpoint = port;
    m_baud = baud;
    m_parity = parity;
    m_dataBits = dataBits;
    m_stopBits = stopBits;
    m_slaveId = qBound(1, slaveId, 247);
    m_stopRequested.store(false);
    start();
    return true;
}

void ModbusResponder::stop()
{
    m_stopRequested.store(true);
    if (isRunning())
        wait(2500);
    m_active.store(false);
}

void ModbusResponder::run()
{
    m_lastRawSignal.start();
    m_lastWriteSignal.start();
    if (m_transport == Transport::Tcp)
        serveTcp();
    else if (m_transport == Transport::Rtu)
        serveRtu();
    m_active.store(false);
    emit responderStopped();
}

void ModbusResponder::serveTcp()
{
    QHostAddress address;
    if (!m_bindAddress.isEmpty() && m_bindAddress != QStringLiteral("0.0.0.0") &&
        !address.setAddress(m_bindAddress))
    {
        emit error(QStringLiteral("Invalid Modbus TCP listen address: %1").arg(m_bindAddress));
        return;
    }

    QTcpServer server;
    const QHostAddress requestedAddress = address.isNull() ? QHostAddress::AnyIPv4 : address;
    if (!server.listen(requestedAddress, static_cast<quint16>(m_port)))
    {
        // A configured client/host address may not be assigned to this PC.
        // For slave mode, listening on all local IPv4 interfaces is a safe
        // fallback; it still fails normally when the port is already used.
        if (!address.isNull())
        {
            server.close();
            if (!server.listen(QHostAddress::AnyIPv4, static_cast<quint16>(m_port)))
            {
                emit error(QStringLiteral("Could not listen as Modbus TCP slave: %1")
                           .arg(server.errorString()));
                return;
            }
        }
        else
        {
            emit error(QStringLiteral("Could not listen as Modbus TCP slave: %1")
                       .arg(server.errorString()));
            return;
        }
    }

    m_active.store(true);
    emit responderStarted();
    while (!m_stopRequested.load())
    {
        if (!server.waitForNewConnection(100))
            continue;
        while (server.hasPendingConnections() && !m_stopRequested.load())
        {
            QTcpSocket *client = server.nextPendingConnection();
            if (!client)
                continue;
            modbus_t *context = modbus_new_tcp("0.0.0.0", m_port);
            if (!context)
            {
                emit error(QStringLiteral("Could not create Modbus TCP slave context"));
                client->disconnectFromHost();
                delete client;
                continue;
            }
            modbus_set_socket(context, static_cast<int>(client->socketDescriptor()));
            modbus_set_slave(context, m_slaveId);
            modbus_set_byte_timeout(context, 0, 200000);
            modbus_set_response_timeout(context, 0, 200000);
            serve(context);
            modbus_close(context);
            modbus_free(context);
            // This worker deliberately has no Qt event loop, so deleteLater()
            // would retain every accepted socket until process exit.
            client->close();
            delete client;
        }
    }
    server.close();
}

void ModbusResponder::serveRtu()
{
    modbus_t *context = modbus_new_rtu(m_endpoint.toLatin1().constData(), m_baud,
                                       m_parity, m_dataBits, m_stopBits);
    if (!context)
    {
        emit error(QStringLiteral("Could not create Modbus RTU slave context"));
        return;
    }
    if (modbus_connect(context) == -1 || modbus_set_slave(context, m_slaveId) == -1)
    {
        emit error(QStringLiteral("Could not open Modbus RTU slave: %1")
                   .arg(QString::fromLocal8Bit(modbus_strerror(errno))));
        modbus_free(context);
        return;
    }
    modbus_set_byte_timeout(context, 0, 200000);
    modbus_set_response_timeout(context, 0, 200000);
    m_active.store(true);
    emit responderStarted();
    serve(context);
    modbus_close(context);
    modbus_free(context);
}

void ModbusResponder::serve(modbus_t *context)
{
    modbus_mapping_t *mapping = modbus_mapping_new(0, 0, 256, 0);
    if (!mapping)
    {
        emit error(QStringLiteral("Could not allocate Modbus slave register map"));
        return;
    }

    uint8_t request[MODBUS_TCP_MAX_ADU_LENGTH];
    while (!m_stopRequested.load())
    {
        const int length = modbus_receive(context, request);
        if (length <= 0)
            continue;
        // Protocol replies must remain immediate, but diagnostic signals are
        // deliberately rate limited so a noisy BCU cannot fill the GUI event
        // queue and make the application appear hung.
        if (m_lastRawSignal.elapsed() >= 100)
        {
            emit rawDataReceived(QByteArray(reinterpret_cast<const char *>(request), length));
            m_lastRawSignal.restart();
        }
        reportWrite(request, length);
        const int replyResult = modbus_reply(context, request, length, mapping);
        if (replyResult == -1 && !m_stopRequested.load())
            emit error(QStringLiteral("Modbus slave response failed: %1")
                       .arg(QString::fromLocal8Bit(modbus_strerror(errno))));
        else if (replyResult >= 0)
        {
            const int unitOffset = m_transport == Transport::Tcp ? 6 : 0;
            const QByteArray response = confirmationFrame(
                request, length, m_transport == Transport::Tcp, unitOffset);
            if (!response.isEmpty())
                emit rawDataSent(response);
        }
    }
    modbus_mapping_free(mapping);
}

void ModbusResponder::reportWrite(const uint8_t *request, int length)
{
    // libmodbus returns the complete ADU. The offset points at the unit id:
    // RTU starts with it at byte 0; TCP places it after the six-byte MBAP
    // transaction/protocol/length header at byte 6.
    const int offset = m_transport == Transport::Tcp ? 6 : 0;
    if (!request || length < offset + 8 || request[offset + 1] != MODBUS_FC_WRITE_MULTIPLE_REGISTERS)
        return;
    const int address = (static_cast<int>(request[offset + 2]) << 8) | request[offset + 3];
    const int count = (static_cast<int>(request[offset + 4]) << 8) | request[offset + 5];
    const int byteCount = request[offset + 6];
    if (address < 0 || address >= 256 || count <= 0 || count > 123 ||
        address + count > 256 ||
        byteCount != count * 2 || offset + 7 + byteCount > length)
        return;

    QVector<quint16> values;
    values.reserve(count);
    for (int i = 0; i < count; ++i)
        values.append(static_cast<quint16>((request[offset + 7 + i * 2] << 8) |
                                            request[offset + 8 + i * 2]));
    if (m_lastWriteSignal.elapsed() < 100)
        return;
    m_lastWriteSignal.restart();
    emit holdingRegistersWritten(address, values);
}
