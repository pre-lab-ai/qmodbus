#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTextStream>

#include "../src/modbusresponder.h"
#include "../3rdparty/libmodbus/src/modbus.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool started = false;
    bool writeReceived = false;
    bool rawReceived = false;
    bool rawSent = false;
    QString responderError;
    QVector<quint16> received;
    QByteArray receivedFrame;
    QByteArray sentFrame;
    ModbusResponder responder;
    QObject::connect(&responder, &ModbusResponder::responderStarted,
                     [&started]() { started = true; });
    QObject::connect(&responder, &ModbusResponder::holdingRegistersWritten,
                     [&writeReceived, &received](int address, const QVector<quint16> &values) {
                         if (address == 0)
                         {
                             writeReceived = true;
                             received = values;
                         }
                     });
    QObject::connect(&responder, &ModbusResponder::rawDataReceived,
                     [&rawReceived, &receivedFrame](const QByteArray &frame) {
                         rawReceived = true;
                         receivedFrame = frame;
                     });
    QObject::connect(&responder, &ModbusResponder::rawDataSent,
                     [&rawSent, &sentFrame](const QByteArray &frame) {
                         rawSent = true;
                         sentFrame = frame;
                     });
    QObject::connect(&responder, &ModbusResponder::error,
                     [&responderError](const QString &message) { responderError = message; });

    // Use an address that is normally not assigned locally. Slave mode must
    // fall back to listening on all local IPv4 interfaces.
    responder.startTcp(QStringLiteral("192.0.2.1"), 15020, 1);
    QElapsedTimer timer;
    timer.start();
    while (!started && timer.elapsed() < 2000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }

    bool ok = started;
    modbus_t *client = modbus_new_tcp("127.0.0.1", 15020);
    if (!client || modbus_connect(client) == -1)
    {
        QTextStream(stderr) << "client connect failed: "
                            << (client ? QString::fromLocal8Bit(modbus_strerror(errno)) : QStringLiteral("null context"))
                            << " started=" << started << " responderError=" << responderError << "\n";
        ok = false;
    }
    else
    {
        modbus_set_slave(client, 1);
        uint16_t values[16];
        for (int i = 0; i < 16; ++i)
            values[i] = static_cast<uint16_t>(0x1000 + i);
        const int writeResult = modbus_write_registers(client, 0, 16, values);
        if (writeResult != 16)
            QTextStream(stderr) << "write failed result=" << writeResult << " error="
                                << QString::fromLocal8Bit(modbus_strerror(errno)) << "\n";
        ok = ok && writeResult == 16;
        timer.restart();
        while ((!writeReceived || !rawReceived || !rawSent) && timer.elapsed() < 1000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }
        ok = ok && writeReceived && received == QVector<quint16>(values, values + 16);
        ok = ok && rawReceived && rawSent && receivedFrame.size() == 45 && sentFrame.size() == 12;
        ok = ok && static_cast<unsigned char>(sentFrame.at(6)) == 1
             && static_cast<unsigned char>(sentFrame.at(7)) == MODBUS_FC_WRITE_MULTIPLE_REGISTERS
             && static_cast<unsigned char>(sentFrame.at(8)) == 0
             && static_cast<unsigned char>(sentFrame.at(9)) == 0
             && static_cast<unsigned char>(sentFrame.at(10)) == 0
             && static_cast<unsigned char>(sentFrame.at(11)) == 16;
    }
    if (client)
    {
        modbus_close(client);
        modbus_free(client);
    }
    responder.stop();
    if (!ok)
        QTextStream(stderr) << "Modbus responder test failed started=" << started
                            << " writeReceived=" << writeReceived
                            << " rawReceived=" << rawReceived
                            << " rawSent=" << rawSent
                            << " receivedFrameSize=" << receivedFrame.size()
                            << " sentFrameSize=" << sentFrame.size()
                            << " receivedCount=" << received.size()
                            << " responderError=" << responderError << "\n";
    return ok ? 0 : 1;
}
