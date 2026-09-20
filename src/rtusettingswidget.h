#ifndef RTUSETTINGSWIDGET_H
#define RTUSETTINGSWIDGET_H

#include <QWidget>
#include "imodbus.h"
#include "modbus.h"
#include "serialsettingswidget.h"
#include "modbusresponder.h"

class RtuSettingsWidget : public SerialSettingsWidget
{
	Q_OBJECT

public:
	RtuSettingsWidget(QWidget *parent = 0);
	virtual ~RtuSettingsWidget();
	bool isSlaveMode() const { return m_responder.isActive(); }

protected:
	virtual void changeModbusInterface(const QString &port, char parity);

private slots:
	void setSlaveMode(bool enabled);

signals:
	void slavePortActive(bool active);
	void slaveRegistersWritten(int address, const QVector<quint16> &values);
	void slaveRawData(const QByteArray &frame, bool outgoing);

private:
	ModbusResponder m_responder;
};

#endif // RTUSETTINGSWIDGET_H
