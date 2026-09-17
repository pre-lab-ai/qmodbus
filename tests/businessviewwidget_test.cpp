#include <QApplication>
#include <QTableWidget>
#include <QTabBar>
#include <QTextStream>
#include <algorithm>

#include "../src/businessviewwidget.h"

namespace
{
bool require(bool condition, const QString &message)
{
    if (!condition)
        QTextStream(stderr) << "FAIL: " << message << "\n";
    return condition;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    PointTable table;
    QStringList errors;
    bool ok = table.load(QStringLiteral(":/config/point_table.json"), &errors);
    ok &= require(ok, QStringLiteral("load point table: %1").arg(errors.join(';')));

    BusinessViewWidget widget;
    widget.setPointTable(table);
    const auto tabs = widget.findChildren<QTabBar *>();
    ok &= require(!tabs.isEmpty(), QStringLiteral("business block tabs exist"));
    if (!tabs.isEmpty())
        ok &= require(tabs.first()->count() == 8,
                      QStringLiteral("overview plus control, detail, measure, diag and signal tabs"));
    ok &= require(widget.findChildren<QLabel *>().indexOf(nullptr) < 0,
                  QStringLiteral("business view labels are available"));
    ok &= require(widget.findChildren<QTableWidget *>().size() == 1,
                  QStringLiteral("details table and selection summary are removed"));

    const auto tables = widget.findChildren<QTableWidget *>();
    QTableWidget *mainTable = nullptr;
    for (QTableWidget *candidate : tables)
        if (candidate->columnCount() == 7) { mainTable = candidate; break; }
    ok &= require(mainTable != nullptr, QStringLiteral("business table exists"));
    if (mainTable)
    {
        const QStringList overviewKeys = {
            QStringLiteral("BCU_state"), QStringLiteral("current_state"),
            QStringLiteral("Heat management status"), QStringLiteral("BMS fault level"),
            QStringLiteral("max_cell_vol"), QStringLiteral("max_cell_vol_num"),
            QStringLiteral("min_cell_vol"), QStringLiteral("min_cell_vol_num"),
            QStringLiteral("max_cell_temp"), QStringLiteral("max_cell_temp_num"),
            QStringLiteral("min_cell_temp"), QStringLiteral("min_cell_temp_num"),
            QStringLiteral("display_SOC"), QStringLiteral("SOH"), QStringLiteral("rack_soe"),
            QStringLiteral("total_vol"), QStringLiteral("total_cur"),
            QStringLiteral("Insulation resistance"), QStringLiteral("HV_Box_max_temp"),
            QStringLiteral("max_allowed_chg_power"), QStringLiteral("max_allowed_dchg_power"),
            QStringLiteral("max_allowed_chg_cur_limit"), QStringLiteral("max_allowed_dchg_cur_limit")
        };
        const int visiblePoints = std::count_if(table.points().cbegin(), table.points().cend(),
                                                [&overviewKeys](const PointDefinition &point) {
                                                    return overviewKeys.contains(point.key);
                                                });
        ok &= require(mainTable->rowCount() == visiblePoints,
                      QStringLiteral("overview only shows required key points"));
        ok &= require(mainTable->columnCount() == 7 &&
                      mainTable->horizontalHeaderItem(0)->text() == QStringLiteral("Name") &&
                      mainTable->horizontalHeaderItem(1)->text() == QStringLiteral("Address"),
                      QStringLiteral("name is first column and block column removed"));
        ok &= require(mainTable->item(0, 0) && !mainTable->item(0, 0)->text().isEmpty(),
                      QStringLiteral("Chinese display name is present"));
        ok &= require(mainTable->item(0, 1) && mainTable->item(0, 1)->text().startsWith(QStringLiteral("0x")),
                      QStringLiteral("address is displayed as hexadecimal"));
        ok &= require(mainTable->item(0, 1)->text() <= mainTable->item(1, 1)->text(),
                      QStringLiteral("addresses are sorted ascending"));
    }

    PollResult result;
    result.frame = PollFrame{QStringLiteral("Rack Measure"), 4, 0x0210, 1, 1000};
    result.values = QVector<quint16>() << 2500;
    result.success = true;
    result.attempts = 1;
    widget.applyPollResult(result, table, QStringLiteral("local"), 1);
    int signalIndex = -1;
    int voltageIndex = -1;
    int temperatureIndex = -1;
    int controlIndex = -1;
    int diagIndex = -1;
    if (!tabs.isEmpty())
    {
        for (int index = 0; index < tabs.first()->count(); ++index)
        {
            if (tabs.first()->tabText(index) == QStringLiteral("Rack Signal"))
                signalIndex = index;
            if (tabs.first()->tabText(index) == QStringLiteral("Rack Detail - Cell Voltage"))
                voltageIndex = index;
            if (tabs.first()->tabText(index) == QStringLiteral("Rack Detail - Cell Temperature"))
                temperatureIndex = index;
            if (tabs.first()->tabText(index) == QStringLiteral("Rack Control"))
                controlIndex = index;
            if (tabs.first()->tabText(index) == QStringLiteral("Rack Diag"))
                diagIndex = index;
        }
        tabs.first()->setCurrentIndex(signalIndex);
        ok &= require(mainTable->columnCount() == 8,
                      QStringLiteral("rack signal includes definition column"));
        ok &= require(mainTable->horizontalHeaderItem(7)->text() == QStringLiteral("Definition"),
                      QStringLiteral("definition header is present"));
    }
    AcquisitionSample state;
    state.pointKey = QStringLiteral("current_state");
    state.displayName = QStringLiteral("充放电状态");
    state.block = QStringLiteral("Rack Signal");
    state.address = 0x001D;
    state.rawValue = QVariantList{2};
    state.engineeringValue = 2.0;
    state.quality = QualityCode::Good;
    widget.setSamples({state});
    if (!tabs.isEmpty())
        tabs.first()->setCurrentIndex(signalIndex);
    bool foundCharge = false;
    for (int row = 0; mainTable && row < mainTable->rowCount(); ++row)
        if (mainTable->item(row, 2) && mainTable->item(row, 2)->text() == QStringLiteral("current_state"))
            foundCharge = mainTable->item(row, 3)->text() == QStringLiteral("0x0002") &&
                          mainTable->item(row, 7)->text() == QStringLiteral("0：空闲；1：放电；2：充电");
    ok &= require(foundCharge, QStringLiteral("rack signal enum value and definition are localized"));

    AcquisitionSample alarm;
    alarm.pointKey = QStringLiteral("extern_critical_alarm");
    alarm.displayName = QStringLiteral("严重告警");
    alarm.block = QStringLiteral("Rack Signal");
    alarm.address = 0x0012;
    alarm.rawValue = QVariantList{static_cast<int>(0x5001)};
    alarm.engineeringValue = static_cast<double>(0x5001);
    alarm.quality = QualityCode::Good;
    widget.setSamples({alarm});
    if (!tabs.isEmpty())
        tabs.first()->setCurrentIndex(signalIndex);
    bool foundBits = false;
    int alarmRow = -1;
    for (int row = 0; mainTable && row < mainTable->rowCount(); ++row)
    {
        if (mainTable->item(row, 2) && mainTable->item(row, 2)->text() == QStringLiteral("extern_critical_alarm"))
        {
            alarmRow = row;
            foundBits = mainTable->item(row, 3)->text() == QStringLiteral("0x5001") &&
                        mainTable->item(row, 7)->text().contains(QStringLiteral("Bit14"));
        }
    }
    ok &= require(foundBits, QStringLiteral("rack signal bit values and all bit definitions are displayed"));
    const int collapsedSignalRows = mainTable ? mainTable->rowCount() : 0;
    if (alarmRow >= 0)
    {
        QMetaObject::invokeMethod(&widget, "toggleSignalDetails", Qt::DirectConnection,
                                  Q_ARG(int, alarmRow), Q_ARG(int, 0));
        bool foundBit0 = false;
        bool foundBit12 = false;
        for (int row = 0; mainTable && row < mainTable->rowCount(); ++row)
        {
            if (mainTable->item(row, 2) && mainTable->item(row, 2)->text() == QStringLiteral("extern_critical_alarm.bit0"))
                foundBit0 = mainTable->item(row, 3)->text() == QStringLiteral("0x0001");
            if (mainTable->item(row, 2) && mainTable->item(row, 2)->text() == QStringLiteral("extern_critical_alarm.bit12"))
                foundBit12 = mainTable->item(row, 3)->text() == QStringLiteral("0x0001");
        }
        ok &= require(mainTable->rowCount() > collapsedSignalRows && foundBit0 && foundBit12,
                      QStringLiteral("rack signal expands active non-reserved bits as hex rows"));
        QStringList bitKeys;
        for (int row = alarmRow + 1; mainTable && row < mainTable->rowCount(); ++row)
        {
            const QString key = mainTable->item(row, 2) ? mainTable->item(row, 2)->text() : QString();
            if (!key.startsWith(QStringLiteral("extern_critical_alarm.bit")))
                break;
            bitKeys.append(key);
        }
        ok &= require(bitKeys == QStringList({
                          QStringLiteral("extern_critical_alarm.bit0"),
                          QStringLiteral("extern_critical_alarm.bit1"),
                          QStringLiteral("extern_critical_alarm.bit2"),
                          QStringLiteral("extern_critical_alarm.bit3"),
                          QStringLiteral("extern_critical_alarm.bit4"),
                          QStringLiteral("extern_critical_alarm.bit5"),
                          QStringLiteral("extern_critical_alarm.bit6"),
                          QStringLiteral("extern_critical_alarm.bit7"),
                          QStringLiteral("extern_critical_alarm.bit10"),
                          QStringLiteral("extern_critical_alarm.bit12"),
                          QStringLiteral("extern_critical_alarm.bit13"),
                          QStringLiteral("extern_critical_alarm.bit14")
                      }),
                      QStringLiteral("rack signal bit rows are ordered by numeric bit index"));
        QMetaObject::invokeMethod(&widget, "toggleSignalDetails", Qt::DirectConnection,
                                  Q_ARG(int, alarmRow), Q_ARG(int, 0));
        ok &= require(mainTable->rowCount() == collapsedSignalRows,
                      QStringLiteral("rack signal bit details can be collapsed"));
    }

    auto tableHasKey = [mainTable](const QString &key) {
        for (int row = 0; mainTable && row < mainTable->rowCount(); ++row)
            if (mainTable->item(row, 2) && mainTable->item(row, 2)->text() == key)
                return true;
        return false;
    };
    if (!tabs.isEmpty() && controlIndex >= 0)
    {
        tabs.first()->setCurrentIndex(controlIndex);
        ok &= require(mainTable->columnCount() == 11 &&
                      mainTable->horizontalHeaderItem(3)->text() == QStringLiteral("Access") &&
                      mainTable->horizontalHeaderItem(4)->text() == QStringLiteral("Write enable") &&
                      mainTable->horizontalHeaderItem(5)->text() == QStringLiteral("Action") &&
                      mainTable->horizontalHeaderItem(6)->text() == QStringLiteral("Value") &&
                      mainTable->horizontalHeaderItem(10)->text() == QStringLiteral("Definition"),
                      QStringLiteral("rack control exposes definition, access, write enable and action columns"));
        ok &= require(tableHasKey(QStringLiteral("start_insulation_sampleing")) &&
                      tableHasKey(QStringLiteral("Upper and lower high pressure control")) &&
                      tableHasKey(QStringLiteral("BCU_reset")) &&
                      tableHasKey(QStringLiteral("clear_all_abnormal_event")),
                      QStringLiteral("rack control commands are present"));
        bool hasWriteAction = false;
        for (int row = 0; row < mainTable->rowCount(); ++row)
        {
            if (mainTable->item(row, 2) &&
                mainTable->item(row, 2)->text() == QStringLiteral("BCU_reset"))
            {
                hasWriteAction = qobject_cast<QPushButton *>(mainTable->cellWidget(row, 5)) != nullptr;
                break;
            }
        }
        ok &= require(hasWriteAction, QStringLiteral("rack control exposes a write action"));
        bool hasWriteSwitch = false;
        for (int row = 0; row < mainTable->rowCount(); ++row)
        {
            if (mainTable->item(row, 2) &&
                mainTable->item(row, 2)->text() == QStringLiteral("BCU_reset"))
            {
                auto *writeSwitch = qobject_cast<QCheckBox *>(mainTable->cellWidget(row, 4));
                auto *valueItem = mainTable->item(row, 6);
                auto *writeButton = qobject_cast<QPushButton *>(mainTable->cellWidget(row, 5));
                hasWriteSwitch = writeSwitch && !writeSwitch->isChecked() &&
                                 valueItem && !(valueItem->flags() & Qt::ItemIsEditable) &&
                                 writeButton && !writeButton->isEnabled();
                break;
            }
        }
        ok &= require(hasWriteSwitch, QStringLiteral("writable controls start in read-only UI mode"));
        for (QCheckBox *checkBox : widget.findChildren<QCheckBox *>())
            if (checkBox->text() == QStringLiteral("Enable all writes"))
                checkBox->setChecked(true);
        QCoreApplication::processEvents();
        bool enabledWrite = false;
        for (int row = 0; row < mainTable->rowCount(); ++row)
        {
            if (mainTable->item(row, 2) &&
                mainTable->item(row, 2)->text() == QStringLiteral("BCU_reset"))
            {
                auto *writeButton = qobject_cast<QPushButton *>(mainTable->cellWidget(row, 5));
                enabledWrite = mainTable->item(row, 6) &&
                               (mainTable->item(row, 6)->flags() & Qt::ItemIsEditable) &&
                               writeButton && writeButton->isEnabled();
                break;
            }
        }
        ok &= require(enabledWrite, QStringLiteral("global write switch enables value editing and action"));
    }
    if (!tabs.isEmpty() && diagIndex >= 0)
    {
        tabs.first()->setCurrentIndex(diagIndex);
        ok &= require(tableHasKey(QStringLiteral("BCU_software_project_num")) &&
                      tableHasKey(QStringLiteral("BCU_software_main_version_num")) &&
                      tableHasKey(QStringLiteral("BCU_software_sub_version_num")) &&
                      tableHasKey(QStringLiteral("BCU_software_main_revise_num")),
                      QStringLiteral("rack diagnostic software version points are present"));
    }

    if (!tabs.isEmpty() && voltageIndex >= 0)
    {
        tabs.first()->setCurrentIndex(voltageIndex);
        ok &= require(mainTable->rowCount() == 260,
                      QStringLiteral("cell voltage page uses configured count"));
        ok &= require(mainTable->item(0, 0)->text() == QStringLiteral("单体电压001") &&
                      mainTable->item(0, 1)->text() == QStringLiteral("0x1401") &&
                      mainTable->item(259, 0)->text() == QStringLiteral("单体电压260") &&
                      mainTable->item(259, 1)->text() == QStringLiteral("0x1504"),
                      QStringLiteral("cell voltage names and addresses are sequential"));
        PollResult cellResult;
        cellResult.frame = PollFrame{QStringLiteral("Rack Detail"), 4, 0x1401, 2, 1000};
        cellResult.values = QVector<quint16>() << 3300 << 3310;
        cellResult.success = true;
        widget.applyPollResult(cellResult, table, QStringLiteral("local"), 1);
        QMetaObject::invokeMethod(&widget, "refreshRows", Qt::DirectConnection);
        ok &= require(mainTable->item(0, 3)->text() == QStringLiteral("3300") &&
                      mainTable->item(1, 3)->text() == QStringLiteral("3310"),
                      QStringLiteral("partial detail poll frame updates individual cells"));
    }
    if (!tabs.isEmpty() && temperatureIndex >= 0)
    {
        tabs.first()->setCurrentIndex(temperatureIndex);
        ok &= require(mainTable->rowCount() == 140,
                      QStringLiteral("cell temperature page uses configured count"));
        ok &= require(mainTable->item(0, 0)->text() == QStringLiteral("电池温度001") &&
                      mainTable->item(0, 1)->text() == QStringLiteral("0x1801") &&
                      mainTable->item(139, 0)->text() == QStringLiteral("电池温度140") &&
                      mainTable->item(139, 1)->text() == QStringLiteral("0x188C"),
                      QStringLiteral("cell temperature names and addresses are sequential"));
    }
    return ok ? 0 : 1;
}
