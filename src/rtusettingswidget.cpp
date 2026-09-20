#include "serialsettingswidget.h"
#include "rtusettingswidget.h"
#include "ui_serialsettingswidget.h"
#include "modbus.h"
#include <QSerialPortInfo>

RtuSettingsWidget::RtuSettingsWidget(QWidget *parent) :
    SerialSettingsWidget(parent)
{
    connect(ui->slaveMode, &QCheckBox::toggled, this, &RtuSettingsWidget::setSlaveMode);
    connect(&m_responder, &ModbusResponder::responderStarted, this,
            [this]() { emit slavePortActive(true); });
    connect(&m_responder, &ModbusResponder::responderStopped, this,
            [this]() { emit slavePortActive(false); });
    connect(&m_responder, &ModbusResponder::holdingRegistersWritten, this,
            &RtuSettingsWidget::slaveRegistersWritten);
    connect(&m_responder, &ModbusResponder::rawDataReceived, this,
            [this](const QByteArray &frame) { emit slaveRawData(frame, false); });
    connect(&m_responder, &ModbusResponder::rawDataSent, this,
            [this](const QByteArray &frame) { emit slaveRawData(frame, true); });
    connect(&m_responder, &ModbusResponder::error, this,
            [this](const QString &message) {
                ui->slaveMode->setChecked(false);
                emit connectionError(message);
            });
}

RtuSettingsWidget::~RtuSettingsWidget()
{
    m_responder.stop();
}

void RtuSettingsWidget::setSlaveMode(bool enabled)
{
    if (!enabled)
    {
        m_responder.stop();
        return;
    }

    if (m_session.isOpen())
    {
        // A COM port cannot be opened by both roles. Switching to the
        // responder is the least surprising action when the operator selects
        // Slave mode after having enabled the client.
        m_session.close();
        ui->checkBox->blockSignals(true);
        ui->checkBox->setChecked(false);
        ui->checkBox->blockSignals(false);
        emit serialPortActive(false);
    }

    const int index = ui->serialPort->currentIndex();
    const QString portName = ui->serialPort->itemData(index).toString();
    if (portName.isEmpty())
    {
        ui->slaveMode->setChecked(false);
        emit connectionError(tr("No serial port selected for Modbus RTU slave."));
        return;
    }

    char parity = 'N';
    switch (ui->parity->currentIndex())
    {
    case 1: parity = 'O'; break;
    case 2: parity = 'E'; break;
    default: break;
    }
    QString endpoint = portName;
#ifdef Q_OS_WIN32
    if (endpoint.startsWith(QStringLiteral("COM")))
        endpoint = QStringLiteral("\\\\.\\") + endpoint;
#else
    const QSerialPortInfo info(portName);
    if (!info.isNull())
        endpoint = info.systemLocation();
#endif
    m_responder.startRtu(endpoint, ui->baud->currentText().toInt(), parity,
                         ui->dataBits->currentText().toInt(),
                         ui->stopBits->currentText().toInt(), 1);
}

void RtuSettingsWidget::changeModbusInterface(const QString& port, char parity)
{
    if( !m_session.openRtu( port,
            ui->baud->currentText().toInt(),
            parity,
            ui->dataBits->currentText().toInt(),
            ui->stopBits->currentText().toInt() ) )
    {
        emit connectionError( tr( "Could not connect serial port!" ) );
    }
}
