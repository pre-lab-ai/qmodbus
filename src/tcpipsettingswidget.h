#ifndef TCPIPSETTINGSWIDGET_H
#define TCPIPSETTINGSWIDGET_H

#include <QWidget>
#include "imodbus.h"
#include "modbussession.h"

namespace Ui {
class TcpIpSettingsWidget;
}

class TcpIpSettingsWidget : public QWidget, public IModbus
{
    Q_OBJECT

public:
    TcpIpSettingsWidget(QWidget *parent = 0);
    ~TcpIpSettingsWidget();
    // IModbus interface
    virtual ModbusSession *session() { return &m_session; }
    virtual int setupModbusPort();
    bool tcpConnect();

protected:
    void changeModbusInterface(const QString& address, int portNbr);
    void releaseTcpModbus();
    void enableGuiItems(bool checked);

private slots:
    void on_cbEnabled_clicked(bool checked);

signals:
    void tcpPortActive(bool val);
    void connectionError(const QString &msg);

private:
    Ui::TcpIpSettingsWidget *ui;
    ModbusSession            m_session;
};

#endif // TCPIPSETTINGSWIDGET_H
