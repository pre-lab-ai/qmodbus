#include "serialsettingswidget.h"
#include "rtusettingswidget.h"
#include "ui_serialsettingswidget.h"
#include "modbus.h"

RtuSettingsWidget::RtuSettingsWidget(QWidget *parent) :
    SerialSettingsWidget(parent)
{
}

RtuSettingsWidget::~RtuSettingsWidget()
{
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
