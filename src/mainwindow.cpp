/*
 * mainwindow.cpp - implementation of MainWindow class
 *
 * Copyright (c) 2009-2013 Tobias Junghans / Electronic Design Chemnitz
 *
 * This file is part of QModBus - http://qmodbus.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include <QSettings>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QScrollBar>
#include <QSet>
#include <QMessageBox>
#include <QHeaderView>
#include <QtConcurrent/QtConcurrentRun>

#include <errno.h>

#include "mainwindow.h"
#include "BatchProcessor.h"
#include "modbus.h"
#include "modbus-private.h"

#include "ui_mainwindow.h"


const int DataTypeColumn = 0;
const int AddrColumn = 1;
const int DataColumn = 2;

extern MainWindow * globalMainWin;


MainWindow::MainWindow( QWidget * _parent ) :
	QMainWindow( _parent ),
	ui( new Ui::MainWindowClass ),
	m_session( NULL ),
	m_transport(nullptr),
	m_scheduler(this),
	m_businessView(nullptr),
	m_pcsView(nullptr),
	m_tcpActive(false),
	m_poll(false),
	m_dataRecordingEnabled(true),
	m_busMonitorColumnsManuallyResized(false),
	m_resizingBusMonitorColumns(false),
	m_rawDataLine(),
	m_controlWriteWatcher(nullptr),
	m_pendingControlSlave(0),
	m_pendingControlFunction(0),
	m_pendingControlSession(nullptr),
	m_slaveWriteFlushScheduled(false),
	m_slaveRawFlushScheduled(false)
{
	ui->setupUi(this);
	// Keep diagnostic widgets bounded during long-running acquisition. These
	// views are for recent traffic only; samples and events are persisted by
	// AcquisitionStore independently.
	ui->rawData->setMaximumBlockCount(1000);
	ui->rawData->setLineWrapMode(QPlainTextEdit::NoWrap);
	ui->rawDataAutoScroll->setChecked(false);
	ui->busMonAutoScroll->setChecked(false);
	connect(ui->rawDataAutoScroll, &QCheckBox::toggled,
			this, &MainWindow::onRawDataScrollToggled);
	connect(ui->busMonAutoScroll, &QCheckBox::toggled,
			this, &MainWindow::onBusMonitorScrollToggled);
	QHeaderView *busHeader = ui->busMonTable->horizontalHeader();
	busHeader->setSectionResizeMode(QHeaderView::Interactive);
	busHeader->setStretchLastSection(false);
	busHeader->setMinimumSectionSize(60);
	connect(busHeader, &QHeaderView::sectionResized, this,
			[this](int, int, int) {
				if (!m_resizingBusMonitorColumns)
					m_busMonitorColumnsManuallyResized = true;
			});
	resizeBusMonitorColumns();
	// Let the BCU/EMS page use the released vertical space while keeping
	// the register table visible at roughly half of its previous height.
	ui->verticalLayout->setStretch(0, 2);
	ui->verticalLayout->setStretch(2, 1);

	connect( ui->rtuSettingsWidget,   SIGNAL(serialPortActive(bool)), this, SLOT(onRtuPortActive(bool)));
	connect( ui->asciiSettingsWidget, SIGNAL(serialPortActive(bool)), this, SLOT(onAsciiPortActive(bool)));
	connect( ui->tcpSettingsWidget,   SIGNAL(tcpPortActive(bool)),    this, SLOT(onTcpPortActive(bool)));
	connect( ui->rtuSettingsWidget,   SIGNAL(slaveRegistersWritten(int,QVector<quint16>)),
			this, SLOT(onSlaveRegistersWritten(int,QVector<quint16>)));
	connect( ui->tcpSettingsWidget,   SIGNAL(slaveRegistersWritten(int,QVector<quint16>)),
			this, SLOT(onSlaveRegistersWritten(int,QVector<quint16>)));
	connect(ui->rtuSettingsWidget, &RtuSettingsWidget::slaveRawData,
			this, &MainWindow::onSlaveRawData);
	connect(ui->tcpSettingsWidget, &TcpIpSettingsWidget::slaveRawData,
			this, &MainWindow::onSlaveRawData);

	connect( ui->rtuSettingsWidget,   SIGNAL(connectionError(const QString&)), this, SLOT(setStatusError(const QString&)));
	connect( ui->asciiSettingsWidget, SIGNAL(connectionError(const QString&)), this, SLOT(setStatusError(const QString&)));
	connect( ui->tcpSettingsWidget,   SIGNAL(connectionError(const QString&)), this, SLOT(setStatusError(const QString&)));

	connect( ui->slaveID, SIGNAL( valueChanged( int ) ),
			this, SLOT( updateRequestPreview() ) );
	connect( ui->functionCode, SIGNAL( currentIndexChanged( int ) ),
			this, SLOT( updateRequestPreview() ) );
	connect( ui->startAddr, SIGNAL( valueChanged( int ) ),
			this, SLOT( updateRequestPreview() ) );
	connect( ui->numCoils, SIGNAL( valueChanged( int ) ),
			this, SLOT( updateRequestPreview() ) );

	connect( ui->functionCode, SIGNAL( currentIndexChanged( int ) ),
			this, SLOT( updateRegisterView() ) );
	connect( ui->numCoils, SIGNAL( valueChanged( int ) ),
			this, SLOT( updateRegisterView() ) );
	connect( ui->startAddr, SIGNAL( valueChanged( int ) ),
			this, SLOT( updateRegisterView() ) );

	connect( ui->sendBtn, SIGNAL( clicked() ),
			this, SLOT( onSendButtonPress() ) );

	connect( ui->clearBusMonTable, SIGNAL( clicked() ),
			this, SLOT( clearBusMonTable() ) );

	connect( ui->actionAbout_QModBus, SIGNAL( triggered() ),
			this, SLOT( aboutQModBus() ) );

	connect( ui->functionCode, SIGNAL( currentIndexChanged( int ) ),
		this, SLOT( enableHexView() ) );
	connect(&m_scheduler, &PollScheduler::resultReady, this, &MainWindow::onPollResult);
	connect(&m_scheduler, &PollScheduler::error, this, &MainWindow::onPollSchedulerError);


	updateRegisterView();
	updateRequestPreview();
	enableHexView();

	m_statusInd = new QWidget;
	m_statusInd->setFixedSize( 16, 16 );
	m_statusText = new QLabel;
	ui->statusBar->addWidget( m_statusInd );
	ui->statusBar->addWidget( m_statusText, 10 );
	resetStatus();

	QStringList pointTableErrors;
	if( !m_pointTable.load( QStringLiteral( ":/config/point_table.json" ), &pointTableErrors ) )
	{
		setStatusError( tr( "Point table invalid: %1" ).arg( pointTableErrors.join( "; " ) ) );
	}
	else
	{
		QStringList pcsErrors;
		if (!m_pcsPointTable.load(QStringLiteral(":/config/pcs_point_table.json"), &pcsErrors))
		{
			setStatusError(tr("PCS point table invalid: %1").arg(pcsErrors.join("; ")));
		}
		else
		{
			m_pointTable.append(m_pcsPointTable);
			QStringList combinedErrors;
			if (!m_pointTable.validate(&combinedErrors))
				setStatusError(tr("Combined point table invalid: %1").arg(combinedErrors.join("; ")));
		}
		m_statusText->setText( tr( "Point table loaded: %1 points" ).arg( m_pointTable.points().size() ) );
	}

	m_businessView = new BusinessViewWidget(this);
	m_businessView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_businessView->setExcludedBlocks({QStringLiteral("PCS")});
	m_businessView->setPointTable(m_pointTable);
	ui->tabWidget->insertTab(1, m_businessView, tr("BCU/EMS"));
	ui->tabWidget->setCurrentIndex(0);
	connect(m_businessView, &BusinessViewWidget::startAcquisitionRequested,
			this, &MainWindow::startBackgroundAcquisition);
	connect(m_businessView, &BusinessViewWidget::stopAcquisitionRequested,
			this, &MainWindow::stopBackgroundAcquisition);
	connect(m_businessView, &BusinessViewWidget::dataRecordingToggled,
			this, &MainWindow::setDataRecordingEnabled);
	connect(m_businessView, &BusinessViewWidget::controlWriteRequested,
			this, &MainWindow::onControlWriteRequested);
	connect(m_businessView, &BusinessViewWidget::alarmAcknowledgeRequested,
			this, &MainWindow::onAlarmAcknowledgeRequested);

	m_pcsView = new BusinessViewWidget(this);
	m_pcsView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_pcsView->setBlockFilter(QStringLiteral("PCS"));
	m_pcsView->setPassiveMode(true);
	m_pcsView->setPointTable(m_pointTable);
	ui->tabWidget->insertTab(2, m_pcsView, tr("BCU/PCS"));
	connect(m_pcsView, &BusinessViewWidget::startAcquisitionRequested,
			this, &MainWindow::startBackgroundAcquisition);
	connect(m_pcsView, &BusinessViewWidget::stopAcquisitionRequested,
			this, &MainWindow::stopBackgroundAcquisition);
	connect(m_pcsView, &BusinessViewWidget::dataRecordingToggled,
			this, &MainWindow::setDataRecordingEnabled);
	connect(m_pcsView, &BusinessViewWidget::controlWriteRequested,
			this, &MainWindow::onControlWriteRequested);

	// Keep mutable data outside the installation directory so upgrades do not
	// overwrite configuration or historical samples.
	const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (!dataDirectory.isEmpty())
	{
		QDir dataDir(dataDirectory);
		dataDir.mkpath(QStringLiteral("."));
		dataDir.mkpath(QStringLiteral("config"));
		dataDir.mkpath(QStringLiteral("diagnostics"));
		const QString databasePath = dataDir.filePath(QStringLiteral("acquisition.sqlite"));
		const QString legacyPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("acquisition.sqlite"));
		if (!QFileInfo::exists(databasePath) && QFileInfo::exists(legacyPath) &&
				QFileInfo(legacyPath).absoluteFilePath() != QFileInfo(databasePath).absoluteFilePath())
		{
			// Copy rather than remove the legacy file, making the first migration
			// recoverable if an old installation is still needed for rollback.
			if (!QFile::copy(legacyPath, databasePath))
				qWarning() << "unable to migrate legacy acquisition database:" << legacyPath;
			for (const QString &suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")})
				if (QFileInfo::exists(legacyPath + suffix) && !QFileInfo::exists(databasePath + suffix))
					QFile::copy(legacyPath + suffix, databasePath + suffix);
		}
		qInfo() << "ModbusPC data directory:" << dataDirectory;
		QString storeError;
		if (!m_acquisitionStore.open(databasePath, &storeError))
		{
			qWarning() << "acquisition database unavailable:" << storeError;
			setStatusError(tr("Acquisition database unavailable: %1").arg(storeError));
		}
		else
		{
			const int retentionDays = QSettings().value(QStringLiteral("acquisitionRetentionDays"), 0).toInt();
			if (retentionDays > 0 && !m_acquisitionStore.pruneBefore(
					QDateTime::currentDateTimeUtc().addDays(-retentionDays), &storeError))
				qWarning() << "acquisition retention failed:" << storeError;
			m_businessView->setSamples(m_acquisitionStore.latestSamples(
					QStringLiteral("local"), ui->slaveID->value(), QString(), &storeError));
		}
	}

	m_pollTimer = new QTimer( this );
	connect( m_pollTimer, SIGNAL(timeout()), this, SLOT(sendModbusRequest()));

	m_statusTimer = new QTimer( this );
	connect( m_statusTimer, SIGNAL(timeout()), this, SLOT(resetStatus()));
	m_statusTimer->setSingleShot(true);
}


MainWindow::~MainWindow()
{
	if (m_controlWriteWatcher)
		m_controlWriteWatcher->waitForFinished();
	delete ui;
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
	if( event->key() == Qt::Key_Control )
	{
		//set flag to request polling
		if( m_session != NULL && m_session->isOpen() )
			m_poll = true;

		if( ! m_pollTimer->isActive() )
			ui->sendBtn->setText( tr("Poll") );
	}
}

void MainWindow::keyReleaseEvent(QKeyEvent* event)
{
	if( event->key() == Qt::Key_Control )
	{
		m_poll = false;

		if( ! m_pollTimer->isActive() )
			ui->sendBtn->setText( tr("Send") );
	}
}

void MainWindow::onSendButtonPress( void )
{
	//if already polling then stop
	if( m_pollTimer->isActive() )
	{
		m_pollTimer->stop();
		ui->sendBtn->setText( tr("Send") );
	}
	else
	{
		//if polling requested then enable timer
		if( m_poll )
		{
			m_pollTimer->start( 1000 );
			ui->sendBtn->setText( tr("Stop") );
		}

		sendModbusRequest();
	}
}

void MainWindow::busMonitorAddItem( bool isRequest,
					uint8_t slave,
					uint8_t func,
					uint16_t addr,
					uint16_t nb,
					uint16_t expectedCRC,
					uint16_t actualCRC )
{
	if (!ui->busMonAutoScroll->isChecked())
		return;
	QTableWidget * bm = ui->busMonTable;
	constexpr int maxMonitorRows = 1000;
	if (bm->rowCount() >= maxMonitorRows)
		bm->removeRow(0);
	const int rowCount = bm->rowCount();
	bm->setRowCount( rowCount+1 );

	QTableWidgetItem * ioItem = new QTableWidgetItem( isRequest ? tr( "Req >>" ) : tr( "<< Resp" ) );
	QTableWidgetItem * slaveItem = new QTableWidgetItem( QString::number( slave ) );
	QTableWidgetItem * funcItem = new QTableWidgetItem( QString::number( func ) );
	QTableWidgetItem * addrItem = new QTableWidgetItem( QString::number( addr ) );
	QTableWidgetItem * numItem = new QTableWidgetItem( QString::number( nb ) );
	QTableWidgetItem * crcItem = new QTableWidgetItem;
	if( func > 127 )
	{
		addrItem->setText( QString() );
		numItem->setText( QString() );
		funcItem->setText( tr( "Exception (%1)" ).arg( func-128 ) );
		funcItem->setForeground( Qt::red );
	}
	else
	{
		if( expectedCRC == actualCRC )
		{
			crcItem->setText( QString::asprintf( "%.4x", actualCRC ) );
		}
		else
		{
			crcItem->setText( QString::asprintf( "%.4x (%.4x)", actualCRC, expectedCRC ) );
			crcItem->setForeground( Qt::red );
		}
	}
	ioItem->setFlags( ioItem->flags() & ~Qt::ItemIsEditable );
	slaveItem->setFlags( slaveItem->flags() & ~Qt::ItemIsEditable );
	funcItem->setFlags( funcItem->flags() & ~Qt::ItemIsEditable );
	addrItem->setFlags( addrItem->flags() & ~Qt::ItemIsEditable );
	numItem->setFlags( numItem->flags() & ~Qt::ItemIsEditable );
	crcItem->setFlags( crcItem->flags() & ~Qt::ItemIsEditable );
	bm->setItem( rowCount, 0, ioItem );
	bm->setItem( rowCount, 1, slaveItem );
	bm->setItem( rowCount, 2, funcItem );
	bm->setItem( rowCount, 3, addrItem );
	bm->setItem( rowCount, 4, numItem );
	bm->setItem( rowCount, 5, crcItem );
	// Resize only while the table is warming up or at coarse intervals. A
	// full ResizeToContents pass for every packet scales with all rows and can
	// starve the GUI during continuous acquisition.
	if (rowCount < 50 || (rowCount > 0 && rowCount % 50 == 0))
		resizeBusMonitorColumns();
	bm->verticalScrollBar()->setValue( 100000 );
}

void MainWindow::resizeBusMonitorColumns()
{
	if (m_busMonitorColumnsManuallyResized)
		return;

	QHeaderView *header = ui->busMonTable->horizontalHeader();
	m_resizingBusMonitorColumns = true;
	header->resizeSections(QHeaderView::ResizeToContents);
	m_resizingBusMonitorColumns = false;
}


void MainWindow::busMonitorRawData( uint8_t * data, uint8_t dataLen, bool addNewline, uint8_t direction )
{
	if (!ui->rawDataAutoScroll->isChecked())
	{
		m_rawDataLine.clear();
		return;
	}
	if( dataLen > 0 )
	{
		// libmodbus may deliver one frame in multiple chunks. Accumulate only
		// the current line, then append it once at frame completion; rebuilding
		// the complete QTextDocument for every chunk is quadratic over time.
		if (m_rawDataLine.isEmpty())
			m_rawDataLine = direction == SENT ? QStringLiteral("Req >> : ")
			                                 : QStringLiteral("<< Resp: ");
		for( int i = 0; i < dataLen; ++i )
			m_rawDataLine += QString::asprintf( "%.2x ", data[i] );
		if (addNewline)
		{
			ui->rawData->appendPlainText(m_rawDataLine);
			m_rawDataLine.clear();
			ui->rawData->verticalScrollBar()->setValue( 100000 );
		}
	}
}

// static
void MainWindow::stBusMonitorAddItem( modbus_t * modbus, uint8_t isRequest, uint8_t slave, uint8_t func, uint16_t addr, uint16_t nb, uint16_t expectedCRC, uint16_t actualCRC )
{
    Q_UNUSED(modbus);
    if (!globalMainWin)
        return;
    MainWindow *window = globalMainWin;
    // PollWorker executes Modbus I/O on a worker thread. Queue monitor UI
    // updates onto the GUI thread; touching QTableWidget from the worker can
    // corrupt Qt's model and cause an intermittent crash.
    QMetaObject::invokeMethod(window, [window, isRequest, slave, func, addr, nb,
                                       expectedCRC, actualCRC]() {
        if (globalMainWin == window)
            window->busMonitorAddItem(isRequest != 0, slave, func, addr, nb,
                                      expectedCRC, actualCRC);
    }, Qt::QueuedConnection);
}

// static
void MainWindow::stBusMonitorRawData( modbus_t * modbus, uint8_t * data, uint8_t dataLen, uint8_t addNewline , uint8_t direction)
{
    Q_UNUSED(modbus);
    if (!globalMainWin || !data || dataLen == 0)
        return;
    MainWindow *window = globalMainWin;
    const QByteArray bytes(reinterpret_cast<const char *>(data), dataLen);
    QMetaObject::invokeMethod(window, [window, bytes, addNewline, direction]() {
        if (globalMainWin == window)
            window->busMonitorRawData(reinterpret_cast<uint8_t *>(const_cast<char *>(bytes.constData())),
                                      static_cast<uint8_t>(bytes.size()), addNewline != 0, direction);
    }, Qt::QueuedConnection);
}

static QString descriptiveDataTypeName( int funcCode )
{
	switch( funcCode )
	{
		case MODBUS_FC_READ_COILS:
		case MODBUS_FC_WRITE_SINGLE_COIL:
		case MODBUS_FC_WRITE_MULTIPLE_COILS:
			return "Coil (binary)";
		case MODBUS_FC_READ_DISCRETE_INPUTS:
			return "Discrete Input (binary)";
		case MODBUS_FC_READ_HOLDING_REGISTERS:
		case MODBUS_FC_WRITE_SINGLE_REGISTER:
		case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
			return "Holding Register (16 bit)";
		case MODBUS_FC_READ_INPUT_REGISTERS:
			return "Input Register (16 bit)";
		default:
			break;
	}
	return "Unknown";
}


static inline QString embracedString( const QString & s )
{
	return s.section( '(', 1 ).section( ')', 0, 0 );
}


static inline int stringToHex( QString s )
{
	return s.replace( "0x", "" ).toInt( NULL, 16 );
}

static QString blockForRegisterRange(const PointTable &table, int function, int address, int count)
{
	const int lastAddress = address + count - 1;
	QString candidate;
	for (const PointDefinition &point : table.points())
	{
		if (point.reserved || !point.readFunctions.contains(function) ||
			point.lastAddress() < address || point.address > lastAddress)
			continue;
		if (candidate.isEmpty())
			candidate = point.block;
		else if (candidate != point.block)
			return QStringLiteral("Manual");
	}
	return candidate.isEmpty() ? QStringLiteral("Manual") : candidate;
}


void MainWindow::clearBusMonTable( void )
{
	ui->busMonTable->setRowCount( 0 );
}

void MainWindow::setDataRecordingEnabled(bool enabled)
{
	m_dataRecordingEnabled = enabled;
	if (m_statusText)
	{
		m_statusText->setText(enabled ? tr("Data recording enabled")
								  : tr("Data recording disabled"));
		m_statusInd->setStyleSheet(enabled ? "background: #0b0;" : "background: #aaa;");
		m_statusTimer->start(2000);
	}
}

void MainWindow::onRawDataScrollToggled(bool enabled)
{
	ui->rawDataAutoScroll->setText(enabled ? tr("Stop") : tr("Start"));
	if (enabled)
		ui->rawData->verticalScrollBar()->setValue(100000);
}

void MainWindow::onBusMonitorScrollToggled(bool enabled)
{
	ui->busMonAutoScroll->setText(enabled ? tr("Stop") : tr("Start"));
	if (enabled)
		ui->busMonTable->verticalScrollBar()->setValue(100000);
}


void MainWindow::updateRequestPreview( void )
{
	const int slave = ui->slaveID->value();
	const int func = stringToHex( embracedString(
						ui->functionCode->
							currentText() ) );
	const int addr = ui->startAddr->value();
	const int num = ui->numCoils->value();
	if( func == MODBUS_FC_WRITE_SINGLE_COIL || func == MODBUS_FC_WRITE_SINGLE_REGISTER )
	{
		ui->requestPreview->setText(
			QString::asprintf( "%.2x  %.2x  %.2x %.2x ",
					slave,
					func,
					addr >> 8,
					addr & 0xff ) );
	}
	else
	{
		ui->requestPreview->setText(
			QString::asprintf( "%.2x  %.2x  %.2x %.2x  %.2x %.2x",
					slave,
					func,
					addr >> 8,
					addr & 0xff,
					num >> 8,
					num & 0xff ) );
	}
}




void MainWindow::updateRegisterView( void )
{
	const int func = stringToHex( embracedString(
					ui->functionCode->currentText() ) );
	const QString dataType = descriptiveDataTypeName( func );
	const int addr = ui->startAddr->value();

	int rowCount = 0;
	switch( func )
	{
		case MODBUS_FC_WRITE_SINGLE_REGISTER:
		case MODBUS_FC_WRITE_SINGLE_COIL:
			ui->numCoils->setEnabled( false );
			rowCount = 1;
			break;
		case MODBUS_FC_WRITE_MULTIPLE_COILS:
		case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
			rowCount = ui->numCoils->value();
			ui->numCoils->setEnabled( true );
			break;
		default:
			ui->numCoils->setEnabled( true );
			break;
	}

	ui->regTable->setRowCount( rowCount );
	for( int i = 0; i < rowCount; ++i )
	{
		QTableWidgetItem * dtItem = new QTableWidgetItem( dataType );
		QTableWidgetItem * addrItem =
			new QTableWidgetItem( QString::number( addr+i ) );
		QTableWidgetItem * dataItem =
			new QTableWidgetItem( QString::number( 0 ) );
		dtItem->setFlags( dtItem->flags() & ~Qt::ItemIsEditable	);
		addrItem->setFlags( addrItem->flags() & ~Qt::ItemIsEditable );
		ui->regTable->setItem( i, DataTypeColumn, dtItem );
		ui->regTable->setItem( i, AddrColumn, addrItem );
		ui->regTable->setItem( i, DataColumn, dataItem );
	}

	if( rowCount > 0 )
		ui->regTable->resizeColumnToContents(0);
}


void MainWindow::enableHexView( void )
{
	const int func = stringToHex( embracedString(
					ui->functionCode->currentText() ) );

	bool b_enabled =
		func == MODBUS_FC_READ_HOLDING_REGISTERS ||
		func == MODBUS_FC_READ_INPUT_REGISTERS;

	ui->checkBoxHexData->setEnabled( b_enabled );
}


void MainWindow::sendModbusRequest( void )
{
	if (ui->rtuSettingsWidget->isSlaveMode() &&
		(!m_session || !m_session->isOpen() || m_session == ui->rtuSettingsWidget->session()))
	{
		setStatusError(tr("RTU Slave mode is passive; activate an independent Modbus client before sending a request."));
		return;
	}
	if (m_scheduler.isRunning())
	{
		setStatusError(tr("Stop background acquisition before sending a manual request"));
		return;
	}
	if (!ensureSessionConfigured())
	{
		setStatusError(tr("No active Modbus connection. Enable Active on the RTU, TCP or ASCII connection tab."));
		return;
	}

	const int slave = ui->slaveID->value();
	const int func = stringToHex( embracedString(
					ui->functionCode->currentText() ) );
	const int addr = ui->startAddr->value();
	int num = ui->numCoils->value();
	uint8_t dest[1024];
	uint16_t * dest16 = (uint16_t *) dest;

	memset( dest, 0, 1024 );

	int ret = -1;
	bool is16Bit = false;
	bool writeAccess = false;
	bool transactionAttempted = false;
	bool transactionVerified = false;
	QString transactionError;
	const PointDefinition *writePoint = nullptr;
	QVector<quint16> writeValues;
	const QString dataType = descriptiveDataTypeName( func );

	m_session->setSlave( slave );
	const bool writeFunction = func == MODBUS_FC_WRITE_SINGLE_COIL ||
		func == MODBUS_FC_WRITE_SINGLE_REGISTER ||
		func == MODBUS_FC_WRITE_MULTIPLE_COILS ||
		func == MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
	if (writeFunction)
	{
		writeValues.reserve(num);
		for (int i = 0; i < num; ++i)
		{
			QTableWidgetItem *item = ui->regTable->item(i, DataColumn);
			if (!item)
			{
				setStatusError(tr("Missing write value at row %1").arg(i + 1));
				return;
			}
			bool parsed = false;
			const QString text = item->text().trimmed();
			const uint parsedValue = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
				? text.mid(2).toUInt(&parsed, 16) : text.toUInt(&parsed, 10);
			if (!parsed || parsedValue > 0xffffu)
			{
				setStatusError(tr("Invalid write value at row %1").arg(i + 1));
				return;
			}
			writeValues.append(static_cast<quint16>(parsedValue));
		}
		for (const PointDefinition &candidate : m_pointTable.points())
		{
			if (candidate.canWrite() && candidate.writeFunctions.contains(func) &&
				candidate.address == addr && candidate.count == num)
			{
				writePoint = &candidate;
				break;
			}
		}
		if (!writePoint)
		{
			setStatusError(tr("Write address is not an allowed point-table control"));
			return;
		}
		const QString userRole = QSettings().value(QStringLiteral("operatorRole"),
											   QStringLiteral("operator")).toString();
		const ControlValidation validation = ControlValidator::validateRaw(*writePoint, func, writeValues, userRole);
		if (!validation.accepted)
		{
			setStatusError(validation.error);
			return;
		}
		QStringList values;
		for (quint16 value : writeValues)
			values.append(QString::number(value));
		const QString confirmation = tr("Write control '%1' (%2)\nAddress: 0x%3\nRaw value: %4\n\nConfirm this one-time command?")
			.arg(writePoint->displayName, writePoint->key)
			.arg(addr, 4, 16, QLatin1Char('0')).arg(values.join(QStringLiteral(", ")));
		if (QMessageBox::question(this, tr("Confirm control write"), confirmation,
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		{
			setStatusError(tr("Control write cancelled"));
			return;
		}
	}

	const auto executeReadRequest = [&]() {
		switch( func )
		{
			case MODBUS_FC_READ_COILS:
				ret = m_session->readBits( addr, num, dest );
				break;
			case MODBUS_FC_READ_DISCRETE_INPUTS:
				ret = m_session->readInputBits( addr, num, dest );
				break;
			case MODBUS_FC_READ_HOLDING_REGISTERS:
				ret = m_session->readRegisters( addr, num, dest16 );
				is16Bit = true;
				break;
			case MODBUS_FC_READ_INPUT_REGISTERS:
				ret = m_session->readInputRegisters( addr, num, dest16 );
				is16Bit = true;
				break;
			default:
				break;
		}
	};

	if (!writeFunction)
		executeReadRequest();

	// BCU may close an idle socket (FIN) before the next poll. Reopen and
	// retry reads once; never retry writes automatically because they may
	// trigger a physical control action twice.
	bool reconnectAttempted = false;
	if (!writeFunction && ret < 0 &&
			(errno == ECONNRESET || errno == EPIPE) && m_session->reconnect())
	{
		reconnectAttempted = true;
		executeReadRequest();
	}

	if (writeFunction)
	{
		switch( func )
		{
		case MODBUS_FC_WRITE_SINGLE_COIL:
		{
			const quint16 value = writeValues.value(0) ? 1 : 0;
			ret = m_session->writeBit( addr, value ? 1 : 0 );
			writeAccess = true;
			num = 1;
			break;
		}
		case MODBUS_FC_WRITE_SINGLE_REGISTER:
		{
			const ControlTransactionResult transaction = ControlTransaction::execute(
				m_transport, *writePoint, slave, func, writeValues,
				QSettings().value(QStringLiteral("operatorRole"), QStringLiteral("operator")).toString());
			transactionAttempted = true;
			transactionVerified = transaction.readbackVerified;
			transactionError = transaction.error;
			ret = transaction.accepted ? num : -1;
			writeAccess = true;
			num = 1;
			break;
		}

		case MODBUS_FC_WRITE_MULTIPLE_COILS:
		{
			uint8_t * data = new uint8_t[num];
			for( int i = 0; i < num; ++i )
			{
				data[i] = writeValues.at(i) ? 1 : 0;
			}
			ret = m_session->writeBits( addr, num, data );
			delete[] data;
			writeAccess = true;
			break;
		}
		case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
		{
			const ControlTransactionResult transaction = ControlTransaction::execute(
				m_transport, *writePoint, slave, func, writeValues,
				QSettings().value(QStringLiteral("operatorRole"), QStringLiteral("operator")).toString());
			transactionAttempted = true;
			transactionVerified = transaction.readbackVerified;
			transactionError = transaction.error;
			ret = transaction.accepted ? num : -1;
			writeAccess = true;
			break;
		}

		default:
			break;
		}
	}

	if (func == MODBUS_FC_READ_HOLDING_REGISTERS || func == MODBUS_FC_READ_INPUT_REGISTERS)
	{
		const QString persistenceError = ret < 0
			? QString::fromLocal8Bit(modbus_strerror(errno))
			: (ret == num ? QString() : QStringLiteral("read returned %1 of %2 registers").arg(ret).arg(num));
		recordRegisterPollResult(slave, func, addr, num, ret, dest16, persistenceError);
	}

	if( ret == num  )
	{
		if( writeAccess )
		{
			QString verificationError = transactionError;
			const bool verified = transactionAttempted
				? transactionVerified
				: verifyWriteReadback(addr, writeValues, &verificationError);
			recordWriteAudit(slave, func, addr, num, verified ? ret : -1, writeValues,
							 verified ? QString() : verificationError);
			if (!verified)
			{
				setStatusError(tr("Write readback verification failed: %1").arg(verificationError));
				return;
			}
			m_statusText->setText(
					tr( "Values successfully sent" ) );
			m_statusInd->setStyleSheet( "background: #0b0;" );
			m_statusTimer->start( 2000 );
		}
		else
		{
			bool b_hex = is16Bit && ui->checkBoxHexData->checkState() == Qt::Checked;
			QString qs_num;

			ui->regTable->setRowCount( num );
			for( int i = 0; i < num; ++i )
			{
				int data = is16Bit ? dest16[i] : dest[i];

				QTableWidgetItem * dtItem =
					new QTableWidgetItem( dataType );
				QTableWidgetItem * addrItem =
					new QTableWidgetItem(
						QString::number( addr+i ) );
				qs_num = QString::asprintf( b_hex ? "0x%04x" : "%d", data);
				QTableWidgetItem * dataItem =
					new QTableWidgetItem( qs_num );
				dtItem->setFlags( dtItem->flags() &
							~Qt::ItemIsEditable );
				addrItem->setFlags( addrItem->flags() &
							~Qt::ItemIsEditable );
				dataItem->setFlags( dataItem->flags() &
							~Qt::ItemIsEditable );

				ui->regTable->setItem( i, DataTypeColumn,
								dtItem );
				ui->regTable->setItem( i, AddrColumn,
								addrItem );
				ui->regTable->setItem( i, DataColumn,
								dataItem );
			}

			ui->regTable->resizeColumnToContents(0);
		}
	}
	else
	{
		if (writeAccess)
		{
			QString writeError = !transactionError.isEmpty()
				? transactionError
				: (ret < 0 ? QString::fromLocal8Bit(modbus_strerror(errno))
				: QStringLiteral("write returned %1 of %2 values").arg(ret).arg(num));
			writeError = writeError.trimmed();
			recordWriteAudit(slave, func, addr, num, ret, writeValues, writeError);
		}
		QString err;

		if( ret < 0 )
		{
			const int modbusError = errno;
			const bool connectionReset = modbusError == ECONNRESET || modbusError == EPIPE;
			const bool timeoutError =
#ifdef WIN32
				modbusError == WSAETIMEDOUT ||
#endif
				modbusError == ETIMEDOUT || modbusError == EAGAIN || modbusError == EWOULDBLOCK || modbusError == EIO;
			if (connectionReset && reconnectAttempted)
			{
				err = tr( "Connection reset by Modbus server; automatic reconnect retry failed." );
			}
			else if (connectionReset)
			{
				const bool reconnected = m_session && m_session->reconnect();
				err = reconnected
					? tr( "Connection reset by Modbus server; connection reopened. Retry the request." )
					: tr( "Connection reset by Modbus server; reconnect failed. Toggle Active to retry." );
			}
			else if (timeoutError)
			{
				err = tr( "I/O timeout: no response from Slave ID %1 (function 0x%2, address 0x%3)." )
					.arg(slave)
					.arg(func, 2, 16, QChar('0'))
					.arg(addr, 4, 16, QChar('0'));
			}
			else
			{
				err += tr( "Protocol error" );
				err += ": ";
				err += tr( "Slave threw exception '" );
				err += modbus_strerror( modbusError );
				err += tr( "' or function not implemented." );
			}
		}
		else
		{
			err += tr( "Protocol error" );
			err += ": ";
			err += tr( "Number of registers returned does not "
					"match number of registers requested!" );
		}

		if( err.size() > 0 )
			setStatusError( err );
	}
}

bool MainWindow::verifyWriteReadback(int address, const QVector<quint16> &expected, QString *error)
{
	if (!m_session || !m_session->isOpen() || expected.isEmpty())
	{
		if (error) *error = QStringLiteral("modbus session is not open");
		return false;
	}
	QVector<quint16> actual(expected.size());
	const int received = m_session->readRegisters(address, expected.size(), actual.data());
	if (received != expected.size())
	{
		if (error) *error = received < 0
			? QString::fromLocal8Bit(modbus_strerror(errno))
			: QStringLiteral("read back %1 of %2 registers").arg(received).arg(expected.size());
		return false;
	}
	for (int i = 0; i < expected.size(); ++i)
	{
		if (actual.at(i) != expected.at(i))
		{
			if (error) *error = QStringLiteral("address %1 expected %2, received %3")
				.arg(address + i).arg(expected.at(i)).arg(actual.at(i));
			return false;
		}
	}
	return true;
}

void MainWindow::recordWriteAudit(int slave, int function, int address, int count, int result,
							 const QVector<quint16> &newValues, const QString &message)
{
	if (!m_acquisitionStore.isOpen())
		return;

	QString storeError;
	const bool accepted = result == count;
	CommunicationEvent communication;
	communication.deviceId = QStringLiteral("local");
	communication.slaveId = slave;
	communication.eventType = accepted ? QStringLiteral("write_success") : QStringLiteral("write_failure");
	communication.severity = accepted ? QStringLiteral("info") : QStringLiteral("error");
	communication.message = accepted ? QStringLiteral("manual write completed") : message;
	communication.timestampUtc = QDateTime::currentDateTimeUtc();
	communication.function = function;
	communication.address = address;
	communication.count = count;
	communication.attempts = 1;
	QString communicationError;
	if (!m_acquisitionStore.recordCommunicationEvent(communication, &communicationError))
		qWarning() << "failed to persist write communication event:" << communicationError;
	QSet<QString> matched;
	const QString userRole = QSettings().value(QStringLiteral("operatorRole"),
			QStringLiteral("operator")).toString();
	for (const PointDefinition &point : m_pointTable.points())
	{
		if (!point.canWrite() || !point.writeFunctions.contains(function) ||
			point.address < address || point.lastAddress() > address + count - 1)
			continue;
		QVector<quint16> pointRaw;
		if (accepted)
			pointRaw = newValues.mid(point.address - address, point.count);
		AuditEvent event;
		event.deviceId = QStringLiteral("local");
		event.userId = userRole;
		event.pointKey = point.key;
		event.displayName = point.displayName;
		event.slaveId = slave;
		// Do not query the full acquisition history from the GUI thread. The
		// control result is already known; recording the new value is sufficient
		// for the audit trail and keeps completion responsive.
		event.oldValue = QVariant();
		event.newValue = accepted ? point.decode(pointRaw) : QVariant();
		event.result = accepted ? QStringLiteral("accepted") : QStringLiteral("failed");
		event.message = message;
		event.function = function;
		event.address = address;
		event.count = count;
		event.timestampUtc = QDateTime::currentDateTimeUtc();
		if (!m_acquisitionStore.recordAuditEvent(event, &storeError))
			qWarning() << "failed to persist write audit:" << storeError;
		matched.insert(point.key);
	}
	if (matched.isEmpty())
	{
		AuditEvent event;
		event.deviceId = QStringLiteral("local");
		event.userId = userRole;
		event.pointKey = QStringLiteral("manual_write_%1").arg(address);
		event.displayName = QStringLiteral("Manual write");
		event.slaveId = slave;
		if (accepted)
		{
			QVariantList values;
			for (quint16 value : newValues)
				values.append(static_cast<int>(value));
			event.newValue = values;
		}
		event.result = accepted ? QStringLiteral("accepted") : QStringLiteral("failed");
		event.message = message;
		event.function = function;
		event.address = address;
		event.count = count;
		event.timestampUtc = QDateTime::currentDateTimeUtc();
		if (!m_acquisitionStore.recordAuditEvent(event, &storeError))
			qWarning() << "failed to persist generic write audit:" << storeError;
	}
}

void MainWindow::recordRegisterPollResult(int slave, int function, int address, int count,
									 int received, const uint16_t *values, const QString &error)
{
	if (!m_dataRecordingEnabled || !m_acquisitionStore.isOpen() || count <= 0)
		return;

	PollResult result;
	result.frame.block = blockForRegisterRange(m_pointTable, function, address, count);
	result.frame.function = function;
	result.frame.address = address;
	result.frame.count = count;
	result.attempts = 1;
	result.success = received == count;
	result.error = error;
	if (result.success)
	{
		result.values.resize(count);
		for (int i = 0; i < count; ++i)
			result.values[i] = values[i];
	}

	QString storeError;
	if (!m_acquisitionStore.recordPollResult(result, m_pointTable, QStringLiteral("local"), slave,
										QDateTime::currentDateTimeUtc(), &storeError))
		qWarning() << "failed to persist acquisition result:" << storeError;
}

void MainWindow::configureScheduler(ModbusSession *session)
{
	stopBackgroundAcquisition();
	m_transport.setSession(session);
	m_scheduler.setTransport(&m_transport);
	// The BCU accepts at most 10 registers per Modbus request.  The generic
	// Modbus limit is larger, but using it here makes the BCU return an
	// exception and every affected point is shown as PROTOCOL_ERROR. PCS is
	// excluded here and gets its own plan when the BCU/PCS page is started.
	m_scheduler.setPlan(PollPlan::fromPointTable(m_pointTable, 10, 1000,
			QString(), {QStringLiteral("PCS")}));
	m_scheduler.setSlave(ui->slaveID->value());
	m_scheduler.setMaxRetries(1);
}

bool MainWindow::ensureSessionConfigured()
{
	if (m_session && m_session->isOpen())
		return true;

	const QVector<ModbusSession *> candidates = {
		ui->rtuSettingsWidget->session(),
		ui->asciiSettingsWidget->session(),
		ui->tcpSettingsWidget->session()
	};
	for (ModbusSession *candidate : candidates)
	{
		if (candidate && candidate->isOpen())
		{
			// Multiple independent protocol sessions are valid. Keep the most
			// recently selected one as the manual-request channel.
			m_session = candidate;
			m_transport.setSession(candidate);
			configureScheduler(candidate);
			return true;
		}
	}
	return false;
}

void MainWindow::startBackgroundAcquisition()
{
	if (!ensureSessionConfigured())
	{
		setStatusError(tr("No active Modbus connection. Enable Active on the RTU, TCP or ASCII connection tab."));
		return;
	}
	// PCS is a passive slave: BCU sends 0x10 writes and receives a reply.
	// Background acquisition only polls the BCU/EMS points.
	m_scheduler.setPlan(PollPlan::fromPointTable(m_pointTable, 10, 1000,
			QString(), {QStringLiteral("PCS")}));
	m_scheduler.setSlave(ui->slaveID->value());
	m_scheduler.start();
	if (m_businessView)
		m_businessView->setAcquisitionRunning(m_scheduler.isRunning());
	if (m_pcsView)
		m_pcsView->setAcquisitionRunning(m_scheduler.isRunning());
}

void MainWindow::stopBackgroundAcquisition()
{
	if (m_scheduler.isRunning())
		m_scheduler.stop();
	if (m_businessView)
		m_businessView->setAcquisitionRunning(false);
	if (m_pcsView)
		m_pcsView->setAcquisitionRunning(false);
}

void MainWindow::onControlWriteRequested(const QString &pointKey, const QString &valueText)
{
	if (m_controlWriteWatcher && m_controlWriteWatcher->isRunning())
	{
		setStatusError(tr("A control write is already in progress; wait for its result."));
		return;
	}
	if (ui->rtuSettingsWidget->isSlaveMode() &&
		(!m_session || !m_session->isOpen() || m_session == ui->rtuSettingsWidget->session()))
	{
		setStatusError(tr("RTU PCS Slave mode only responds to BCU writes; activate TCP Active to send Rack Control commands."));
		return;
	}
	if (m_scheduler.isRunning())
	{
		setStatusError(tr("Stop background acquisition before writing a control"));
		return;
	}
	if (!ensureSessionConfigured())
	{
		setStatusError(tr("No active Modbus connection. Enable Active on the RTU, TCP or ASCII connection tab."));
		return;
	}
	const PointDefinition *point = m_pointTable.findByKey(pointKey);
	if (!point || !point->canWrite())
	{
		setStatusError(tr("Control point is not writable: %1").arg(pointKey));
		return;
	}
	if (point->count != 1)
	{
		setStatusError(tr("Only single-register controls are supported by the control page"));
		return;
	}

    const QString text = valueText.trimmed();
    bool parsed = false;
	const uint value = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
		? text.mid(2).toUInt(&parsed, 16) : text.toUInt(&parsed, 10);
	if (!parsed || value > 0xffffu)
	{
		setStatusError(tr("Invalid control value: %1").arg(text));
		return;
	}

	const QString userRole = QSettings().value(QStringLiteral("operatorRole"),
			QStringLiteral("operator")).toString();
	const int function = point->writeFunctions.contains(MODBUS_FC_WRITE_SINGLE_REGISTER)
		? MODBUS_FC_WRITE_SINGLE_REGISTER : MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
	const QVector<quint16> raw{static_cast<quint16>(value)};
	const ControlValidation validation = ControlValidator::validateRaw(*point, function, raw, userRole);
	if (!validation.accepted)
	{
		setStatusError(validation.error);
		return;
	}
	const QString confirmation = tr("Write '%1' (%2)\nAddress: 0x%3\nValue: 0x%4\n\n"
			"This is a one-time control command. Confirm and verify readback?")
		.arg(point->displayName, point->key)
		.arg(point->address, 4, 16, QLatin1Char('0'))
		.arg(value, 4, 16, QLatin1Char('0'));
	if (QMessageBox::question(this, tr("Confirm control write"), confirmation,
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
	{
		setStatusError(tr("Control write cancelled"));
		return;
	}

	// Modbus I/O can wait for a device response. Keep it out of the GUI
	// thread so a missing or delayed response cannot make the whole window
	// appear hung. The scheduler is already stopped above, so this is the
	// only client transaction using the selected session while it runs.
	m_pendingControlPoint = *point;
	m_pendingControlSlave = ui->slaveID->value();
	m_pendingControlFunction = function;
	m_pendingControlValues = raw;
	ModbusSession *session = m_session;
	m_pendingControlSession = session;
	// Keep the monitor callbacks attached while the worker performs the
	// transaction. The request and response must remain visible in Bus Monitor;
	// acquisition is stopped above, so this single exchange cannot flood the UI.
	auto *watcher = new QFutureWatcher<ControlTransactionResult>(this);
	m_controlWriteWatcher = watcher;
	connect(watcher, &QFutureWatcher<ControlTransactionResult>::finished,
			this, &MainWindow::onControlWriteFinished);
	m_statusText->setText(tr("Sending control: %1").arg(point->displayName));
	m_statusInd->setStyleSheet("background: #da0;");
	watcher->setFuture(QtConcurrent::run([session, pointCopy = m_pendingControlPoint,
										 slave = m_pendingControlSlave,
										 function,
										 raw,
										 userRole]() {
		ModbusSessionTransport transport(session);
		const bool verifyReadback = pointCopy.block != QStringLiteral("Rack Control");
		return ControlTransaction::execute(transport, pointCopy, slave, function, raw,
										 userRole, verifyReadback);
	}));
}

void MainWindow::onControlWriteFinished()
{
	if (!m_controlWriteWatcher)
		return;
	const ControlTransactionResult transaction = m_controlWriteWatcher->result();
	const QString controlName = m_pendingControlPoint.displayName;
	if (!transaction.accepted)
	{
		setStatusError(tr("Control write failed: %1").arg(transaction.error));
	}
	else
	{
		const QString status = m_pendingControlPoint.block == QStringLiteral("Rack Control")
			? tr("Control write acknowledged: %1").arg(controlName)
			: tr("Control written and readback verified: %1").arg(controlName);
		m_statusText->setText(status);
		m_statusInd->setStyleSheet("background: #0b0;");
		m_statusTimer->start(3000);
	}
	recordWriteAudit(m_pendingControlSlave, m_pendingControlFunction,
			m_pendingControlPoint.address, m_pendingControlPoint.count,
			transaction.accepted ? m_pendingControlPoint.count : -1,
			m_pendingControlValues, transaction.error);
	m_pendingControlSession = nullptr;
	QFutureWatcher<ControlTransactionResult> *watcher = m_controlWriteWatcher;
	m_controlWriteWatcher = nullptr;
	watcher->deleteLater();
}

void MainWindow::onAlarmAcknowledgeRequested(const QString &sourceKey, int bit)
{
	const QString stateKey = QStringLiteral("%1:%2").arg(sourceKey).arg(bit);
	const QString userRole = QSettings().value(QStringLiteral("operatorRole"),
			QStringLiteral("operator")).toString();
	AlarmTransition transition;
	const QDateTime timestamp = QDateTime::currentDateTimeUtc();
	if (!m_alarmStates.acknowledge(stateKey, timestamp, userRole, &transition))
	{
		setStatusError(tr("Alarm is not active or has already been acknowledged"));
		return;
	}

	const PointDefinition *point = m_pointTable.findByKey(sourceKey);
	CommunicationEvent event;
	event.deviceId = QStringLiteral("local");
	event.slaveId = ui->slaveID->value();
	event.eventType = QStringLiteral("alarm_acknowledged");
	event.severity = transition.state.severity;
	event.message = transition.state.displayName + QStringLiteral(" [") + userRole + QStringLiteral("]");
	event.pointKey = sourceKey;
	event.bit = bit;
	event.timestampUtc = timestamp;
	if (point)
	{
		event.address = point->address;
		event.count = point->count;
		event.function = point->readFunctions.isEmpty() ? 4 : point->readFunctions.first();
		const QVector<AcquisitionSample> latest = m_acquisitionStore.latestSamples(
			QStringLiteral("local"), ui->slaveID->value(), point->block);
		for (const AcquisitionSample &sample : latest)
		{
			if (sample.pointKey == sourceKey)
			{
				const QVariantList raw = sample.rawValue.toList();
				if (!raw.isEmpty())
					event.rawValue = static_cast<quint16>(raw.first().toUInt());
				break;
			}
		}
	}
	QString error;
	if (m_dataRecordingEnabled && m_acquisitionStore.isOpen() &&
			!m_acquisitionStore.recordCommunicationEvent(event, &error))
		qWarning() << "failed to persist alarm acknowledgement:" << error;
	m_statusText->setText(tr("Alarm acknowledged by %1: %2").arg(userRole, transition.state.displayName));
	m_statusInd->setStyleSheet("background: #0b0;");
	m_statusTimer->start(3000);
}

void MainWindow::onPollResult(const PollResult &result)
{
	processAlarmTransitions(result);
	if (m_businessView)
		m_businessView->applyPollResult(result, m_pointTable, QStringLiteral("local"), ui->slaveID->value());
	if (m_pcsView)
		m_pcsView->applyPollResult(result, m_pointTable, QStringLiteral("local"), ui->slaveID->value());
	if (!result.success)
	{
		setStatusError(tr("Poll failed: function 0x%1, address 0x%2, count %3: %4")
			.arg(result.frame.function, 2, 16, QLatin1Char('0'))
			.arg(result.frame.address, 4, 16, QLatin1Char('0'))
			.arg(result.frame.count)
			.arg(result.error));
	}
	const QDateTime timestamp = QDateTime::currentDateTimeUtc();
	if (m_dataRecordingEnabled && m_acquisitionStore.isOpen() &&
		allowDatabaseWrite(QStringLiteral("local:%1:%2").arg(ui->slaveID->value()).arg(result.frame.block), timestamp))
	{
		QString storeError;
		if (!m_acquisitionStore.recordPollResult(result, m_pointTable, QStringLiteral("local"),
															ui->slaveID->value(), timestamp, &storeError))
			qWarning() << "failed to persist background poll:" << storeError;
	}
}

void MainWindow::processAlarmTransitions(const PollResult &result)
{
	if (!result.success || !m_dataRecordingEnabled || !m_acquisitionStore.isOpen())
		return;
	const QDateTime timestamp = QDateTime::currentDateTimeUtc();
	for (const PointDefinition &point : m_pointTable.points())
	{
		if (point.reserved || point.bitFields.isEmpty() || point.block != result.frame.block ||
			!point.readFunctions.contains(result.frame.function) ||
			point.address < result.frame.address || point.lastAddress() > result.frame.lastAddress())
			continue;
		const int offset = point.address - result.frame.address;
		if (offset < 0 || offset >= result.values.size())
			continue;
		const quint16 word = result.values.at(offset);
		for (const PointBitField &field : point.bitFields)
		{
			if (field.bit < 0 || field.bit > 15)
				continue;
			const bool active = (word & (static_cast<quint16>(1) << field.bit)) != 0;
			const QString key = field.key.isEmpty() ? QString::number(field.bit) : field.key;
			const QVector<AlarmTransition> transitions = m_alarmStates.update(
				point.key, point.displayName + QStringLiteral("/") + field.description,
				field.bit, active, timestamp);
			for (const AlarmTransition &transition : transitions)
			{
				CommunicationEvent event;
				event.deviceId = QStringLiteral("local");
				event.slaveId = ui->slaveID->value();
				event.eventType = QStringLiteral("alarm_") + transition.event;
				event.severity = transition.state.severity;
				event.message = transition.state.displayName + QStringLiteral(" [") + key + QStringLiteral("]");
				event.pointKey = point.key;
				event.bit = field.bit;
				event.rawValue = word;
				event.timestampUtc = timestamp;
				event.function = result.frame.function;
				event.address = point.address;
				event.count = point.count;
				event.attempts = result.attempts;
				event.elapsedMs = result.elapsedMs;
				QString error;
				if (!m_acquisitionStore.recordCommunicationEvent(event, &error))
					qWarning() << "failed to persist alarm event:" << error;
			}
		}
	}
}

void MainWindow::onPollSchedulerError(const QString &message)
{
	setStatusError(message);
}

void MainWindow::onSlaveRegistersWritten(int address, const QVector<quint16> &values)
{
    if (address < 0 || address > 0xffff || values.isEmpty())
        return;

    m_pendingSlaveWrite = PollResult();
    m_pendingSlaveWrite.frame.block = QStringLiteral("PCS");
    m_pendingSlaveWrite.frame.function = MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
    m_pendingSlaveWrite.frame.address = address;
    m_pendingSlaveWrite.frame.count = values.size();
    m_pendingSlaveWrite.values = values;
    m_pendingSlaveWrite.success = true;
    m_pendingSlaveWrite.attempts = 1;
    m_pendingSlaveWrite.elapsedMs = 0;
    if (m_slaveWriteFlushScheduled)
        return;
    m_slaveWriteFlushScheduled = true;
    QTimer::singleShot(100, this, &MainWindow::flushSlaveWrite);
}

void MainWindow::flushSlaveWrite()
{
    m_slaveWriteFlushScheduled = false;
    const PollResult result = m_pendingSlaveWrite;
    if (!result.success || result.values.isEmpty())
        return;
    if (m_pcsView)
        m_pcsView->applyPollResult(result, m_pointTable, QStringLiteral("bcu"), 1);
    const QDateTime timestamp = QDateTime::currentDateTimeUtc();
    if (m_dataRecordingEnabled && m_acquisitionStore.isOpen() &&
        allowDatabaseWrite(QStringLiteral("bcu:1:%1").arg(result.frame.block), timestamp))
    {
        QString storeError;
        if (!m_acquisitionStore.recordPollResult(result, m_pointTable,
                                                 QStringLiteral("bcu"), 1,
                                                 timestamp, &storeError))
            qWarning() << "failed to persist PCS slave write:" << storeError;
    }
    m_statusText->setText(tr("PCS received BCU write: 0x%1 (%2 registers)")
                          .arg(result.frame.address, 4, 16, QLatin1Char('0'))
                          .arg(result.values.size()));
    m_statusInd->setStyleSheet("background: #0b0;");
    m_statusTimer->start(2000);
}

void MainWindow::onSlaveRawData(const QByteArray &frame, bool outgoing)
{
    if (!ui->rawDataAutoScroll->isChecked() || frame.isEmpty())
        return;

    QString line = outgoing ? QStringLiteral("<< Resp: ")
                            : QStringLiteral("Req >> : ");
    for (const unsigned char byte : frame)
        line += QString::asprintf("%.2x ", byte);
    m_pendingSlaveRawText += line + QLatin1Char('\n');
    // Keep the pending UI payload bounded when the device is transmitting
    // continuously. The protocol frame is still handled by the responder;
    // this only limits diagnostic repaint work on the GUI thread.
    if (m_pendingSlaveRawText.size() > 64 * 1024)
        m_pendingSlaveRawText = m_pendingSlaveRawText.right(64 * 1024);
    if (!m_slaveRawFlushScheduled)
    {
        m_slaveRawFlushScheduled = true;
        QTimer::singleShot(100, this, &MainWindow::flushSlaveRawData);
    }
}

void MainWindow::flushSlaveRawData()
{
    m_slaveRawFlushScheduled = false;
    if (!ui->rawDataAutoScroll->isChecked() || m_pendingSlaveRawText.isEmpty())
    {
        m_pendingSlaveRawText.clear();
        return;
    }
    ui->rawData->appendPlainText(m_pendingSlaveRawText.trimmed());
    m_pendingSlaveRawText.clear();
    ui->rawData->verticalScrollBar()->setValue(100000);
}

void MainWindow::resetStatus( void )
{
	m_statusText->setText( tr( "Ready" ) );
	m_statusInd->setStyleSheet( "background: #aaa;" );
}

bool MainWindow::allowDatabaseWrite(const QString &key, const QDateTime &timestamp)
{
	const QDateTime previous = m_lastDatabaseWrite.value(key);
	if (previous.isValid() && previous.msecsTo(timestamp) < 1000)
		return false;
	m_lastDatabaseWrite.insert(key, timestamp);
	return true;
}

void MainWindow::pollForDataOnBus( void )
{
	// Client traffic is handled by explicit requests and PollScheduler.
	// Calling modbus_poll() here performs a blocking receive and can consume
	// the response belonging to Rack Control or a manual request.
}


void MainWindow::openBatchProcessor()
{
	BatchProcessor( this, m_session ).exec();
}


void MainWindow::aboutQModBus( void )
{
	AboutDialog( this ).exec();
}

void MainWindow::onRtuPortActive(bool active)
{
	if (active) {
		m_session = ui->rtuSettingsWidget->session();
		configureScheduler(m_session);
		if (m_session && m_session->isOpen()) {
			m_session->setMonitorCallbacks(MainWindow::stBusMonitorAddItem,
											 MainWindow::stBusMonitorRawData);
		}
	}
	else {
		if (m_session == ui->rtuSettingsWidget->session())
		{
			m_session = ui->tcpSettingsWidget->session()->isOpen()
				? ui->tcpSettingsWidget->session() : nullptr;
			m_transport.setSession(m_session);
		}
		if (!m_session)
			stopBackgroundAcquisition();
	}
}

void MainWindow::onAsciiPortActive(bool active)
{
    if (active) {
        m_session = ui->asciiSettingsWidget->session();
		configureScheduler(m_session);
        if (m_session && m_session->isOpen()) {
			m_session->setMonitorCallbacks(MainWindow::stBusMonitorAddItem,
											 MainWindow::stBusMonitorRawData);
        }
    }
    else {
		if (m_session == ui->asciiSettingsWidget->session())
		{
			m_session = ui->tcpSettingsWidget->session()->isOpen()
				? ui->tcpSettingsWidget->session() : nullptr;
			m_transport.setSession(m_session);
		}
		if (!m_session)
			stopBackgroundAcquisition();
    }
}

void MainWindow::onTcpPortActive(bool active)
{
	if (active) {
		m_session = ui->tcpSettingsWidget->session();
		configureScheduler(m_session);
		if (m_session && m_session->isOpen()) {
			m_session->setMonitorCallbacks(MainWindow::stBusMonitorAddItem,
											 MainWindow::stBusMonitorRawData);
		}
	}
	else {
		if (m_session == ui->tcpSettingsWidget->session())
		{
			m_session = ui->rtuSettingsWidget->session()->isOpen()
				? ui->rtuSettingsWidget->session() : nullptr;
			m_transport.setSession(m_session);
		}
		if (!m_session)
			stopBackgroundAcquisition();
	}
}

void MainWindow::setStatusError(const QString &msg)
{
    m_statusText->setText( msg );

    m_statusInd->setStyleSheet( "background: red;" );

    m_statusTimer->start( 2000 );
}
