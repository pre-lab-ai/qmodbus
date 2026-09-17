#include "serialsettingswidget.h"
#include "asciisettingswidget.h"
#include "ui_serialsettingswidget.h"
#include "modbus.h"

AsciiSettingsWidget::AsciiSettingsWidget(QWidget *parent) :
    SerialSettingsWidget(parent)
{
}

AsciiSettingsWidget::~AsciiSettingsWidget()
{
}

void AsciiSettingsWidget::changeModbusInterface(const QString& port, char parity)
{
	if( !m_session.openAscii( port,
			ui->baud->currentText().toInt(),
			parity,
			ui->dataBits->currentText().toInt(),
			ui->stopBits->currentText().toInt() ) )
    {
        emit connectionError( tr( "Could not connect serial port!" ) );
		return;
	}

 //   modbus_ascii_set_serial_mode( m_serialModbus,
 //           ui->checkBoxRs485->isChecked() ? MODBUS_ASCII_RS485 : MODBUS_ASCII_RS232 );


}
