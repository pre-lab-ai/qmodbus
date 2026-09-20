#ifndef SERIALSETTINGSWIDGET_H
#define SERIALSETTINGSWIDGET_H

#include <QWidget>
#include "imodbus.h"
#include "modbussession.h"

namespace Ui {
class SerialSettingsWidget;
}


class SerialSettingsWidget : public QWidget, public IModbus
{
	Q_OBJECT

public:
	SerialSettingsWidget(QWidget *parent = 0);
	virtual ~SerialSettingsWidget();

	virtual ModbusSession* session() { return &m_session; }

	virtual int setupModbusPort();
	int setupModbusPort(bool activate);

protected:
	virtual void changeModbusInterface(const QString &port, char parity) = 0;
	void releaseSerialModbus();
	void enableGuiItems(bool checked);

    Ui::SerialSettingsWidget *ui;
    ModbusSession             m_session;

signals:
	void serialPortActive(bool active);
	void connectionError(const QString &msg);

public slots:
	void changeSerialPort(int);

private slots:
	void on_checkBox_clicked(bool checked);

private:

};

#endif // SERIALSETTINGSWIDGET_H
