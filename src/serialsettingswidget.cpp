#include <QSettings>
#include <QSerialPortInfo>
#include "serialsettingswidget.h"
#include "ui_serialsettingswidget.h"

SerialSettingsWidget::SerialSettingsWidget(QWidget *parent) :
	QWidget(parent),
	ui(new Ui::SerialSettingsWidget)
{
	ui->setupUi(this);
	setupModbusPort(false);
	enableGuiItems(true);
}

SerialSettingsWidget::~SerialSettingsWidget()
{
	releaseSerialModbus();
	delete ui;
}

int SerialSettingsWidget::setupModbusPort()
{
	return setupModbusPort(true);
}

int SerialSettingsWidget::setupModbusPort(bool activate)
{
	QSettings s;

	int portIndex = 0;
	int i = 0;
    ui->serialPort->disconnect();
    ui->serialPort->clear();
	const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
	foreach( const QSerialPortInfo &port, ports )
	{
		QString displayName = port.portName();
		if( !port.description().isEmpty() )
			displayName += QStringLiteral( " (" ) + port.description() + QStringLiteral( ")" );
		ui->serialPort->addItem( displayName, port.portName() );
		const QString savedName = s.value( "serialinterface" ).toString();
		if( port.portName() == savedName || displayName == savedName )
		{
			portIndex = i;
		}
		++i;
	}
	ui->serialPort->setCurrentIndex( portIndex );

	const int baudIndex = ui->baud->findText(s.value("serialbaudrate", "9600").toString());
	const int parityIndex = ui->parity->findText(s.value("serialparity", "none").toString());
	const int stopBitsIndex = ui->stopBits->findText(s.value("serialstopbits", "1").toString());
	const int dataBitsIndex = ui->dataBits->findText(s.value("serialdatabits", "8").toString());
	ui->baud->setCurrentIndex(baudIndex >= 0 ? baudIndex : ui->baud->findText("9600"));
	ui->parity->setCurrentIndex(parityIndex >= 0 ? parityIndex : ui->parity->findText("none"));
	ui->stopBits->setCurrentIndex(stopBitsIndex >= 0 ? stopBitsIndex : ui->stopBits->findText("1"));
	ui->dataBits->setCurrentIndex(dataBitsIndex >= 0 ? dataBitsIndex : ui->dataBits->findText("8"));

	if (activate) {
		connect( ui->serialPort, SIGNAL( currentIndexChanged( int ) ),
				this, SLOT( changeSerialPort( int ) ) );
		connect( ui->baud, SIGNAL( currentIndexChanged( int ) ),
				this, SLOT( changeSerialPort( int ) ) );
		connect( ui->dataBits, SIGNAL( currentIndexChanged( int ) ),
				this, SLOT( changeSerialPort( int ) ) );
		connect( ui->stopBits, SIGNAL( currentIndexChanged( int ) ),
				this, SLOT( changeSerialPort( int ) ) );
		connect( ui->parity, SIGNAL( currentIndexChanged( int ) ),
				this, SLOT( changeSerialPort( int ) ) );

		changeSerialPort( portIndex );
	}
	return portIndex;
}

void SerialSettingsWidget::releaseSerialModbus()
{
	m_session.close();
}

static inline QString embracedString( const QString & s )
{
    return s.section( '(', 1 ).section( ')', 0, 0 );
}


void SerialSettingsWidget::changeSerialPort( int )
{
	const int iface = ui->serialPort->currentIndex();

	const QString portName = ui->serialPort->itemData( iface ).toString();
	QSerialPortInfo portInfo( portName );
	if( iface >= 0 && !portName.isEmpty() && !portInfo.isNull() )
	{
		QSettings settings;
		settings.setValue( "serialinterface", portName );
		settings.setValue( "serialbaudrate", ui->baud->currentText() );
		settings.setValue( "serialparity", ui->parity->currentText() );
		settings.setValue( "serialdatabits", ui->dataBits->currentText() );
		settings.setValue( "serialstopbits", ui->stopBits->currentText() );
#ifdef Q_OS_WIN32
		QString port = portInfo.portName();

		// is it a serial port in the range COM1 .. COM9?
		if ( port.startsWith( "COM" ) )
		{
			// use windows communication device name "\\.\COMn"
			port = "\\\\.\\" + port;
		}
#else
		const QString port = portInfo.systemLocation();
#endif

		char parity;
		switch( ui->parity->currentIndex() )
		{
			case 1: parity = 'O'; break;
			case 2: parity = 'E'; break;
			default:
			case 0: parity = 'N'; break;
		}

		changeModbusInterface(port, parity);

		emit serialPortActive(true);
	}
	else
	{
		emit connectionError( tr( "No serial port found" ) );
	}
}


void SerialSettingsWidget::enableGuiItems(bool checked)
{
	ui->serialPort->setEnabled(checked);
	ui->baud->setEnabled(checked);
	ui->dataBits->setEnabled(checked);
	ui->stopBits->setEnabled(checked);
	ui->parity->setEnabled(checked);
}

void SerialSettingsWidget::on_checkBox_clicked(bool checked)
{
	if (checked) {
		setupModbusPort();
	}
	else {
		releaseSerialModbus();
	}
	enableGuiItems(!checked);
	emit serialPortActive(checked);
}
