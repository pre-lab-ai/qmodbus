/*
 * mainwindow.h - header file for MainWindow class
 *
 * Copyright (c) 2009-2014 Tobias Junghans / Electronic Design Chemnitz
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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>

#include "modbus.h"
#include "modbussession.h"
#include "pointmodel.h"
#include "acquisitionstore.h"
#include "businessviewwidget.h"
#include "controlvalidator.h"
#include "alarmstate.h"
#include "ui_about.h"


#define SENT		0
#define RECEIVED	1

class AboutDialog : public QDialog, public Ui::AboutDialog
{
public:
    AboutDialog( QWidget * _parent ) :
        QDialog( _parent )
    {
        setupUi( this );
        aboutTextLabel->setText(
            aboutTextLabel->text().arg( "0.1.0" ) );
    }
} ;



namespace Ui
{
    class MainWindowClass;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow( QWidget * parent = 0 );
    ~MainWindow();

    const PointTable &pointTable() const { return m_pointTable; }

    void busMonitorAddItem( bool isRequest,
                uint8_t slave,
                uint8_t func,
                uint16_t addr,
                uint16_t nb,
                uint16_t expectedCRC,
                uint16_t actualCRC );
    void busMonitorRawData( uint8_t * data, uint8_t dataLen, bool addNewline, uint8_t direction );

    static void stBusMonitorAddItem( modbus_t * modbus,
            uint8_t isOut, uint8_t slave, uint8_t func, uint16_t addr,
            uint16_t nb, uint16_t expectedCRC, uint16_t actualCRC );
    static void stBusMonitorRawData( modbus_t * modbus, uint8_t * data,
            uint8_t dataLen, uint8_t addNewline,uint8_t );


private slots:
    void clearBusMonTable( void );
    void updateRequestPreview( void );
    void updateRegisterView( void );
    void enableHexView( void );
    void sendModbusRequest( void );
    void onSendButtonPress( void );
    void pollForDataOnBus( void );
    void openBatchProcessor();
    void aboutQModBus( void );
    void onRtuPortActive(bool active);
    void onAsciiPortActive(bool active);
    void onTcpPortActive(bool active);
    void resetStatus( void );
    void setStatusError(const QString &msg);
    void onPollResult(const PollResult &result);
    void onPollSchedulerError(const QString &message);
    void startBackgroundAcquisition();
    void stopBackgroundAcquisition();
    void setDataRecordingEnabled(bool enabled);
    void onRawDataScrollToggled(bool enabled);
    void onBusMonitorScrollToggled(bool enabled);
    void onControlWriteRequested(const QString &pointKey, const QString &valueText);
    void onAlarmAcknowledgeRequested(const QString &sourceKey, int bit);

private:
    void keyPressEvent(QKeyEvent* event);
    void keyReleaseEvent(QKeyEvent* event);
    void recordRegisterPollResult(int slave, int function, int address, int count,
                                  int received, const uint16_t *values, const QString &error);
    void configureScheduler(ModbusSession *session);
    void recordWriteAudit(int slave, int function, int address, int count, int result,
                          const QVector<quint16> &newValues, const QString &message);
    bool verifyWriteReadback(int address, const QVector<quint16> &expected, QString *error);
    void processAlarmTransitions(const PollResult &result);
    bool ensureSessionConfigured();
    void resizeBusMonitorColumns();

    Ui::MainWindowClass * ui;
    ModbusSession * m_session;
    QWidget * m_statusInd;
    QLabel * m_statusText;
    QTimer * m_pollTimer;
    QTimer * m_statusTimer;
    PointTable m_pointTable;
    AcquisitionStore m_acquisitionStore;
    ModbusSessionTransport m_transport;
    PollScheduler m_scheduler;
    BusinessViewWidget *m_businessView;
    AlarmStateModel m_alarmStates;
    bool m_tcpActive;
    bool m_poll;
    bool m_dataRecordingEnabled;
    bool m_busMonitorColumnsManuallyResized;
    bool m_resizingBusMonitorColumns;
    QString m_rawDataLine;
};

#endif // MAINWINDOW_H
