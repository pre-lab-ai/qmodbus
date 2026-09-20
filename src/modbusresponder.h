#ifndef MODBUSRESPONDER_H
#define MODBUSRESPONDER_H

#include <QThread>
#include <QElapsedTimer>
#include <QVector>

#include <atomic>

#include "modbus.h"

class ModbusResponder : public QThread
{
    Q_OBJECT

public:
    explicit ModbusResponder(QObject *parent = nullptr);
    ~ModbusResponder() override;

    bool startTcp(const QString &bindAddress, int port, int slaveId);
    bool startRtu(const QString &port, int baud, char parity,
                  int dataBits, int stopBits, int slaveId);
    void stop();
    bool isActive() const { return m_active.load(); }

signals:
    void responderStarted();
    void responderStopped();
    void error(const QString &message);
    void holdingRegistersWritten(int address, const QVector<quint16> &values);
    void rawDataReceived(const QByteArray &frame);
    void rawDataSent(const QByteArray &frame);

protected:
    void run() override;

private:
    enum class Transport { None, Tcp, Rtu };

    void serve(modbus_t *context);
    void serveTcp();
    void serveRtu();
    void reportWrite(const uint8_t *request, int length);

    Transport m_transport = Transport::None;
    QString m_endpoint;
    QString m_bindAddress;
    int m_port = 0;
    int m_baud = 0;
    char m_parity = 'N';
    int m_dataBits = 8;
    int m_stopBits = 1;
    int m_slaveId = 1;
    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_active{false};
    QElapsedTimer m_lastRawSignal;
    QElapsedTimer m_lastWriteSignal;
};

#endif
