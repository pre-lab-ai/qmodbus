#include "tcpipsettingswidget.h"
#include "ui_tcpipsettingswidget.h"
#include <QIntValidator>
#include <QStringList>
#include <QDebug>

TcpIpSettingsWidget::TcpIpSettingsWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TcpIpSettingsWidget)
{
    ui->setupUi(this);
    ui->edPort->setValidator(new QIntValidator(1, 65535, this));
    // Connection parameters must be editable before Active is enabled.
    enableGuiItems(true);
}

TcpIpSettingsWidget::~TcpIpSettingsWidget()
{
    releaseTcpModbus();
    delete ui;
}

int TcpIpSettingsWidget::setupModbusPort()
{
    return 0;
}

void TcpIpSettingsWidget::changeModbusInterface(const QString &address, int portNbr)
{
    releaseTcpModbus();

    if( !m_session.openTcp( address, portNbr ) )
    {
        emit connectionError( tr( "Could not connect to TCP/IP port: %1" ).arg(m_session.lastError()) );
    }
}

void TcpIpSettingsWidget::releaseTcpModbus()
{
    m_session.close();
}

void TcpIpSettingsWidget::enableGuiItems(bool checked)
{
    ui->edPort->setEnabled(checked);
    ui->edNetworkAddress->setEnabled(checked);
}

void TcpIpSettingsWidget::on_cbEnabled_clicked(bool checked)
{
    if( checked )
    {
        // Lock the endpoint while Active is being established so it cannot
        // change underneath the TCP session. Validation/connect failures
        // below restore editability for correction and retry.
        enableGuiItems(false);
        const bool connected = tcpConnect();
        if (!connected)
        {
            // Active reflects an opened session, not only the user's click.
            // Keep the connection fields editable after validation or
            // connection failure so the operator can correct the endpoint
            // and activate again without restarting the application.
            ui->cbEnabled->setChecked(false);
            enableGuiItems(true);
        }
        else
        {
            enableGuiItems(false);
        }
        emit tcpPortActive( connected );
    }
    else
    {
        releaseTcpModbus();
        enableGuiItems(true);
        emit tcpPortActive( false );
    }
}

bool TcpIpSettingsWidget::tcpConnect()
{
    if( m_session.isOpen() )
        return true;

    const QString address = ui->edNetworkAddress->text().trimmed();
    int portNbr = ui->edPort->text().toInt();
    const QStringList octets = address.split(QChar('.'));
    bool validAddress = octets.size() == 4 && !address.startsWith(QChar('.')) && !address.endsWith(QChar('.'));
    for (const QString &octet : octets)
    {
        bool ok = false;
        const int value = octet.toInt(&ok);
        validAddress = validAddress && ok && value >= 0 && value <= 255;
    }
    if (!validAddress)
    {
        emit connectionError(tr("Please enter a valid Network Address."));
        return false;
    }
    if (portNbr < 1 || portNbr > 65535)
    {
        emit connectionError(tr("Please enter a valid TCP port (1-65535)."));
        return false;
    }

    changeModbusInterface(address, portNbr);
    return m_session.isOpen();
}
