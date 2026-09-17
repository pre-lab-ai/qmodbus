#include "businessviewwidget.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
struct DisplayRow
{
    AcquisitionSample sample;
    PointDefinition point;
    QString displayName;
    QString key;
    QString parentKey;
    int address = 0;
    bool bitRow = false;
    int bit = -1;
    QString bitDefinition;
};

QString translateBitText(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("^.*?\\.")), QString());
    const QVector<QPair<QString, QString>> replacements = {
        {QStringLiteral("Rack Vol High"), QStringLiteral("簇电压过高")},
        {QStringLiteral("Rack Vol Low"), QStringLiteral("簇电压过低")},
        {QStringLiteral("Cell Vol High"), QStringLiteral("单体电压过高")},
        {QStringLiteral("Cell Vol Diff High"), QStringLiteral("单体压差过高")},
        {QStringLiteral("Cell Temp Diff High"), QStringLiteral("单体温差过高")},
        {QStringLiteral("Cell Temp Diff"), QStringLiteral("单体温差")},
        {QStringLiteral("Chg dsg cell Vol Low"), QStringLiteral("充放电单体电压过低")},
        {QStringLiteral("DischargeCurrent High"), QStringLiteral("放电电流过高")},
        {QStringLiteral("Discharge Current High"), QStringLiteral("放电电流过高")},
        {QStringLiteral("Charge Current High"), QStringLiteral("充电电流过高")},
        {QStringLiteral("system temp High"), QStringLiteral("系统温度过高")},
        {QStringLiteral("system chg temp Low"), QStringLiteral("系统充电温度过低")},
        {QStringLiteral("Battery Rank Busbar Temp High"), QStringLiteral("电池簇铜排温度过高")},
        {QStringLiteral("Ins Low"), QStringLiteral("绝缘阻值过低")},
        {QStringLiteral("pack_vol_over_high"), QStringLiteral("电池包电压过高")},
        {QStringLiteral("pack_vol_over_low"), QStringLiteral("电池包电压过低")},
        {QStringLiteral("pack_vol_diff_over_high"), QStringLiteral("电池包压差过高")},
        {QStringLiteral("chg_power_over_high"), QStringLiteral("充电功率过高")},
        {QStringLiteral("dsg_power_over_high"), QStringLiteral("放电功率过高")},
        {QStringLiteral("Soh_over_low"), QStringLiteral("SOH过低")},
        {QStringLiteral("System idle cell low"), QStringLiteral("系统静置单体电压过低")},
        {QStringLiteral("System dsg and idle temp low"), QStringLiteral("系统放电及静置温度过低")},
        {QStringLiteral("pos_relay"), QStringLiteral("正极接触器")},
        {QStringLiteral("pre_relay"), QStringLiteral("预充接触器")},
        {QStringLiteral("neg_relay"), QStringLiteral("负极接触器")},
        {QStringLiteral("isolation_switch"), QStringLiteral("绝缘开关")},
        {QStringLiteral("fuse_switch"), QStringLiteral("熔断器开关")},
        {QStringLiteral("sample_chip_fault"), QStringLiteral("采样芯片故障")},
        {QStringLiteral("open_circuit_fault_of_fuse"), QStringLiteral("熔断器断路故障")},
        {QStringLiteral("thermal_runaway_fault"), QStringLiteral("热失控故障")},
        {QStringLiteral("communication_failure"), QStringLiteral("通信故障")},
        {QStringLiteral("reserve"), QStringLiteral("预留")},
        {QStringLiteral("fault"), QStringLiteral("故障")},
        {QStringLiteral("alarm"), QStringLiteral("告警")},
        {QStringLiteral("warning"), QStringLiteral("预警")}
    };
    for (const auto &replacement : replacements)
        text.replace(replacement.first, replacement.second, Qt::CaseInsensitive);
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}
}

BusinessViewWidget::BusinessViewWidget(QWidget *parent) :
    QWidget(parent),
    m_blockTabs(new QTabBar(this)),
    m_startButton(new QPushButton(tr("Start acquisition"), this)),
    m_stopButton(new QPushButton(tr("Stop acquisition"), this)),
    m_recordDataSwitch(new QCheckBox(tr("Record data"), this)),
    m_writeAllSwitch(new QCheckBox(tr("Enable all writes"), this)),
    m_summary(new QLabel(this)),
    m_table(new QTableWidget(this)),
    m_running(false),
    m_refreshTimer(new QTimer(this))
{
    auto *toolbar = new QHBoxLayout;
    m_recordDataSwitch->setChecked(true);
    toolbar->addWidget(m_recordDataSwitch);
    toolbar->addWidget(m_startButton);
    toolbar->addWidget(m_stopButton);
    toolbar->addWidget(m_writeAllSwitch);
    toolbar->addStretch();
    toolbar->addWidget(m_summary);

    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Address"), tr("Key"),
                                        tr("Value"), tr("Unit"), tr("Quality"),
                                        tr("Updated (Local)")});
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked |
                             QAbstractItemView::EditKeyPressed);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->setSortingEnabled(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    // A cell-voltage/temperature block may contain hundreds of rows and is
    // delivered in many 10-register Modbus frames. Rebuilding the whole
    // table for every frame can starve the GUI event loop, so coalesce UI
    // updates to a bounded rate while retaining every sample in memory.
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(200);
    connect(m_refreshTimer, &QTimer::timeout, this, &BusinessViewWidget::refreshRows);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(toolbar);
    m_blockTabs->setExpanding(false);
    m_blockTabs->setUsesScrollButtons(true);
    layout->addWidget(m_blockTabs);
    layout->addWidget(m_table, 1);
    connect(m_blockTabs, &QTabBar::currentChanged, this, &BusinessViewWidget::onBlockTabChanged);
    connect(m_table, &QTableWidget::cellClicked, this, &BusinessViewWidget::toggleSignalDetails);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &BusinessViewWidget::showSignalContextMenu);
    connect(m_startButton, &QPushButton::clicked, this, &BusinessViewWidget::startAcquisitionRequested);
    connect(m_stopButton, &QPushButton::clicked, this, &BusinessViewWidget::stopAcquisitionRequested);
    connect(m_recordDataSwitch, &QCheckBox::toggled,
            this, &BusinessViewWidget::dataRecordingToggled);
    connect(m_writeAllSwitch, &QCheckBox::toggled, this, &BusinessViewWidget::onGlobalWriteToggled);
    setAcquisitionRunning(false);
}

void BusinessViewWidget::setPointTable(const PointTable &table)
{
    m_samples.clear();
    m_pointDefinitions.clear();
    m_writableKeys.clear();
    m_pointWriteEnabled.clear();
    m_expandedSignalKeys.clear();
    m_blocks.clear();
    m_overviewKeys = {
        QStringLiteral("BCU_state"),
        QStringLiteral("current_state"),
        QStringLiteral("Heat management status"),
        QStringLiteral("BMS fault level"),
        QStringLiteral("max_cell_vol"),
        QStringLiteral("max_cell_vol_num"),
        QStringLiteral("min_cell_vol"),
        QStringLiteral("min_cell_vol_num"),
        QStringLiteral("max_cell_temp"),
        QStringLiteral("max_cell_temp_num"),
        QStringLiteral("min_cell_temp"),
        QStringLiteral("min_cell_temp_num"),
        QStringLiteral("display_SOC"),
        QStringLiteral("SOH"),
        QStringLiteral("rack_soe"),
        QStringLiteral("total_vol"),
        QStringLiteral("total_cur"),
        QStringLiteral("Insulation resistance"),
        QStringLiteral("HV_Box_max_temp"),
        QStringLiteral("max_allowed_chg_power"),
        QStringLiteral("max_allowed_dchg_power"),
        QStringLiteral("max_allowed_chg_cur_limit"),
        QStringLiteral("max_allowed_dchg_cur_limit")
    };
    for (const PointDefinition &point : table.points())
    {
        if (point.reserved)
            continue;
        if (point.block == QStringLiteral("Alarm parameters"))
            continue;
        m_pointDefinitions.insert(point.key, point);
        AcquisitionSample sample;
        sample.pointKey = point.key;
        sample.displayName = point.displayName;
        sample.block = point.block;
        sample.address = point.address;
        sample.unit = point.unit;
        sample.quality = QualityCode::InvalidData;
        m_samples.insert(point.key, sample);
        if (point.canWrite())
            m_writableKeys.insert(point.key);
        if (!m_blocks.contains(point.block))
            m_blocks.append(point.block);
    }
    m_blocks.sort();
    m_blockTabs->blockSignals(true);
    while (m_blockTabs->count() > 0)
        m_blockTabs->removeTab(0);
    m_blockTabs->addTab(tr("Overview"));
    for (const QString &block : m_blocks)
    {
        if (block == QStringLiteral("Rack Detail"))
        {
            int index = m_blockTabs->addTab(tr("Rack Detail - Cell Voltage"));
            m_blockTabs->setTabData(index, QStringLiteral("Rack Detail::Cell Voltage"));
            index = m_blockTabs->addTab(tr("Rack Detail - Cell Temperature"));
            m_blockTabs->setTabData(index, QStringLiteral("Rack Detail::Cell Temperature"));
            index = m_blockTabs->addTab(tr("Rack Detail - Auxiliary"));
            m_blockTabs->setTabData(index, QStringLiteral("Rack Detail::Auxiliary"));
        }
        else
        {
            const int index = m_blockTabs->addTab(block);
            m_blockTabs->setTabData(index, block);
        }
    }
    m_blockTabs->setCurrentIndex(0);
    m_blockTabs->blockSignals(false);
    m_writeAllSwitch->blockSignals(true);
    m_writeAllSwitch->setChecked(false);
    m_writeAllSwitch->blockSignals(false);
    refreshRows();
}

void BusinessViewWidget::setSamples(const QVector<AcquisitionSample> &samples)
{
    for (const AcquisitionSample &sample : samples)
    {
        if (sample.block == QStringLiteral("Alarm parameters"))
            continue;
        m_samples.insert(sample.pointKey, sample);
    }
    refreshRows();
}

void BusinessViewWidget::scheduleRefresh(bool dataChanged)
{
    // Value changes need near-real-time presentation, while unchanged
    // samples only need an occasional timestamp/quality refresh. Restart a
    // long pending timer immediately when a change is detected, but do not
    // restart the one-second timer for every Modbus frame.
    if (dataChanged)
    {
        if (!m_refreshTimer->isActive() || m_refreshTimer->interval() != 1000)
        {
            m_refreshTimer->stop();
            m_refreshTimer->start(1000);
        }
    }
    else if (!m_refreshTimer->isActive())
    {
        m_refreshTimer->start(30000);
    }
}

void BusinessViewWidget::applyPollResult(const PollResult &result, const PointTable &table,
                                         const QString &deviceId, int slaveId)
{
    const QDateTime timestamp = QDateTime::currentDateTimeUtc();
    const QualityCode quality = result.success ? QualityCode::Good : qualityCodeFromPollError(result.error);
    bool frameMatched = false;
    bool dataChanged = false;
    for (const PointDefinition &point : table.points())
    {
        if (point.reserved || point.block != result.frame.block ||
            point.block == QStringLiteral("Alarm parameters") ||
            !point.readFunctions.contains(result.frame.function) ||
            point.lastAddress() < result.frame.address || point.address > result.frame.lastAddress())
            continue;
        frameMatched = true;
        AcquisitionSample sample;
        sample.deviceId = deviceId;
        sample.slaveId = slaveId;
        sample.pointKey = point.key;
        sample.displayName = point.displayName;
        sample.block = point.block;
        sample.address = point.address;
        sample.unit = point.unit;
        sample.quality = quality;
        sample.timestampUtc = timestamp;
        sample.elapsedMs = result.elapsedMs;
        sample.function = result.frame.function;
        sample.error = result.error;
        if (result.success)
        {
            QVariantList rawList = m_samples.value(point.key).rawValue.toList();
            while (rawList.size() < point.count)
                rawList.append(QVariant());
            const int overlapStart = qMax(point.address, result.frame.address);
            const int overlapEnd = qMin(point.lastAddress(), result.frame.lastAddress());
            for (int address = overlapStart; address <= overlapEnd; ++address)
            {
                const int frameOffset = address - result.frame.address;
                const int pointOffset = address - point.address;
                if (frameOffset >= 0 && frameOffset < result.values.size())
                    rawList[pointOffset] = static_cast<int>(result.values.at(frameOffset));
            }
            sample.rawValue = rawList;
            QVector<quint16> decodedRaw;
            decodedRaw.reserve(rawList.size());
            bool complete = true;
            for (const QVariant &value : rawList)
            {
                complete = complete && value.isValid();
                decodedRaw.append(value.isValid() ? static_cast<quint16>(value.toUInt()) : 0);
            }
            sample.engineeringValue = point.decode(decodedRaw);
            if (!sample.engineeringValue.isValid())
                sample.quality = QualityCode::InvalidData;
            else if (!complete)
                sample.quality = QualityCode::Partial;
        }
        const AcquisitionSample previous = m_samples.value(sample.pointKey);
        if (previous.rawValue != sample.rawValue ||
            previous.engineeringValue != sample.engineeringValue ||
            previous.quality != sample.quality ||
            previous.error != sample.error)
            dataChanged = true;
        m_samples.insert(sample.pointKey, sample);
    }
    if (frameMatched)
        scheduleRefresh(dataChanged);
}

void BusinessViewWidget::setAcquisitionRunning(bool running)
{
    m_running = running;
    m_startButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
    // Manual control writes share the Modbus session with the poll scheduler.
    // Keep all write controls unavailable while background acquisition runs.
    m_writeAllSwitch->setEnabled(!running);
    refreshRows();
    updateSummary();
}

void BusinessViewWidget::onGlobalWriteToggled(bool enabled)
{
    if (enabled)
        m_pointWriteEnabled = m_writableKeys;
    else
        m_pointWriteEnabled.clear();
    refreshRows();
}

QString BusinessViewWidget::displayValue(const QVariant &value) const
{
    if (!value.isValid())
        return QString();
    if (value.typeId() == QMetaType::QString)
        return value.toString();
    const QJsonDocument document = QJsonDocument::fromVariant(value);
    if (!document.isNull())
        return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    return value.toString();
}

QString BusinessViewWidget::displayHexValue(const AcquisitionSample &sample) const
{
    if (!sample.engineeringValue.isValid() && !sample.rawValue.isValid())
        return QString();
    const QVariantList rawValues = sample.rawValue.toList();
    const quint32 value = rawValues.isEmpty()
        ? static_cast<quint32>(sample.engineeringValue.toUInt())
        : rawValues.first().toUInt();
    return QString::asprintf("0x%04X", static_cast<unsigned int>(value & 0xFFFFu));
}

QString BusinessViewWidget::localizedDefinition(const PointDefinition &point) const
{
    if (!point.bitFields.isEmpty())
    {
        QStringList definitions;
        for (int bit = 0; bit < 16; ++bit)
        {
            const auto field = std::find_if(point.bitFields.cbegin(), point.bitFields.cend(),
                                            [bit](const PointBitField &candidate) {
                                                return candidate.bit == bit;
                                            });
            definitions.append(QStringLiteral("Bit%1：%2").arg(bit)
                               .arg(field == point.bitFields.cend()
                                        ? QStringLiteral("预留")
                                        : translateBitText(field->description)));
        }
        return definitions.join(QStringLiteral("；"));
    }
    static const QHash<QString, QString> explicitDefinitions = {
        {QStringLiteral("functional_safety_warn"), QStringLiteral("0：正常；1：故障")},
        {QStringLiteral("extern_critical_alarm"), QStringLiteral("0：正常；1：故障")},
        {QStringLiteral("extern_alarm"), QStringLiteral("0：正常；1：告警")},
        {QStringLiteral("extern_warn"), QStringLiteral("0：正常；1：预警")},
        {QStringLiteral("extern_critical_alarm_2"), QStringLiteral("0：正常；1：故障")},
        {QStringLiteral("extern_alarm_2"), QStringLiteral("0：正常；1：告警")},
        {QStringLiteral("extern_warn_2"), QStringLiteral("0：正常；1：预警")},
        {QStringLiteral("pre_power_stage"), QStringLiteral("0：空闲；1/2：启动；3：成功；4：失败")},
        {QStringLiteral("BCU_state"), QStringLiteral("0：空闲；1：禁止充电；2：禁止放电；3：待机；4：停止")},
        {QStringLiteral("current_state"), QStringLiteral("0：空闲；1：放电；2：充电")},
        {QStringLiteral("Heat management status"), QStringLiteral("0：关闭；1：内循环；2：制冷；3：制热")},
        {QStringLiteral("BMS fault level"), QStringLiteral("0：正常；1：一级告警；2：二级告警；3：三级告警")}
    };
    const auto explicitIt = explicitDefinitions.constFind(point.key);
    if (explicitIt != explicitDefinitions.cend())
        return explicitIt.value();

    QString text = point.definition.trimmed();
    if (text.isEmpty())
        return QStringLiteral("无");
    text.replace(QStringLiteral("<br>"), QStringLiteral("；"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("Normal"), QStringLiteral("正常"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("Fault"), QStringLiteral("故障"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("preAlarm"), QStringLiteral("预警"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("Alarm"), QStringLiteral("告警"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("idle"), QStringLiteral("空闲"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("start"), QStringLiteral("启动"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("success"), QStringLiteral("成功"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("failure"), QStringLiteral("失败"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("inc every second"), QStringLiteral("每秒递增"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("reserve"), QStringLiteral("预留"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("full"), QStringLiteral("充满"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("empty"), QStringLiteral("放空"), Qt::CaseInsensitive);
    text = translateBitText(text);
    text.replace(QStringLiteral("bit"), QStringLiteral("位"), Qt::CaseInsensitive);
    text.replace(QRegularExpression(QStringLiteral("\\s*[:：]\\s*")), QStringLiteral("："));
    text.replace(QRegularExpression(QStringLiteral("\\s*;\\s*")), QStringLiteral("；"));
    return text;
}

QString BusinessViewWidget::displayPointValue(const PointDefinition &point,
                                              const AcquisitionSample &sample) const
{
    if (!sample.engineeringValue.isValid())
        return QString();

    const QVariantList rawValues = sample.rawValue.toList();
    const quint32 raw = rawValues.isEmpty() ? static_cast<quint32>(sample.engineeringValue.toDouble())
                                            : rawValues.first().toUInt();
    if (point.key == QStringLiteral("pre_power_stage"))
    {
        if (raw == 0) return QStringLiteral("空闲");
        if (raw == 1 || raw == 2) return QStringLiteral("启动");
        if (raw == 3) return QStringLiteral("成功");
        if (raw == 4) return QStringLiteral("失败");
    }
    if (point.key == QStringLiteral("BCU_state"))
        return QStringList({QStringLiteral("空闲"), QStringLiteral("禁止充电"),
                            QStringLiteral("禁止放电"), QStringLiteral("待机"),
                            QStringLiteral("停止")}).value(static_cast<int>(raw), displayValue(sample.engineeringValue));
    if (point.key == QStringLiteral("current_state"))
        return QStringList({QStringLiteral("空闲"), QStringLiteral("放电"), QStringLiteral("充电")})
            .value(static_cast<int>(raw), displayValue(sample.engineeringValue));
    if (point.key == QStringLiteral("Heat management status"))
        return QStringList({QStringLiteral("关闭"), QStringLiteral("内循环"), QStringLiteral("制冷"), QStringLiteral("制热")})
            .value(static_cast<int>(raw), displayValue(sample.engineeringValue));
    if (point.key == QStringLiteral("BMS fault level"))
        return QStringList({QStringLiteral("正常"), QStringLiteral("一级告警"), QStringLiteral("二级告警"), QStringLiteral("三级告警")})
            .value(static_cast<int>(raw), displayValue(sample.engineeringValue));

    if (!point.bitFields.isEmpty())
    {
        QStringList active;
        for (const PointBitField &field : point.bitFields)
        {
            if (field.bit < 0 || !(raw & (static_cast<quint32>(1) << field.bit)))
                continue;
            active.append(QStringLiteral("Bit%1：%2").arg(field.bit).arg(translateBitText(field.description)));
        }
        return active.isEmpty() ? QStringLiteral("无") : active.join(QStringLiteral("、"));
    }

    if (point.definition.contains(QStringLiteral("bit"), Qt::CaseInsensitive))
    {
        QRegularExpression bitExpression(QStringLiteral("(?:bit|Bit)(\\d+)\\s*[:：]\\s*([^;<>]+)"));
        QStringList active;
        QRegularExpressionMatchIterator iterator = bitExpression.globalMatch(point.definition);
        while (iterator.hasNext())
        {
            const QRegularExpressionMatch match = iterator.next();
            const int bit = match.captured(1).toInt();
            if (raw & (static_cast<quint32>(1) << bit))
            {
                QString label = match.captured(2).trimmed();
                label.replace(QStringLiteral("1 full"), QStringLiteral("充满"), Qt::CaseInsensitive);
                label.replace(QStringLiteral("1 empty"), QStringLiteral("放空"), Qt::CaseInsensitive);
                label.replace(QStringLiteral("BCU_conactor_state.pos_relay"), QStringLiteral("正极继电器闭合"), Qt::CaseInsensitive);
                active.append(label);
            }
        }
        return active.isEmpty() ? QStringLiteral("无") : active.join(QStringLiteral("、"));
    }

    QRegularExpression enumExpression(QStringLiteral("(0x[0-9A-Fa-f]+|\\d+)\\s*[:：-]\\s*([^;,<>]+)"));
    QRegularExpressionMatchIterator iterator = enumExpression.globalMatch(point.definition);
    while (iterator.hasNext())
    {
        const QRegularExpressionMatch match = iterator.next();
        bool parsed = false;
        const uint value = match.captured(1).startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
                ? match.captured(1).mid(2).toUInt(&parsed, 16)
                : match.captured(1).toUInt(&parsed, 10);
        if (parsed && raw == value)
        {
            PointDefinition definition = point;
            definition.definition = match.captured(1) + QStringLiteral(":") + match.captured(2);
            QString result = localizedDefinition(definition);
            const int separator = result.indexOf(QChar(':'));
            return separator >= 0 ? result.mid(separator + 1).trimmed() : result;
        }
    }
    return displayValue(sample.engineeringValue);
}

QString BusinessViewWidget::displayUnit(const PointDefinition &point,
                                        const AcquisitionSample &sample) const
{
    // Temperature indexes and bit/status fields are not temperature values.
    const QString key = point.key.toLower();
    if (point.type == PointDataType::BitField || key.endsWith(QStringLiteral("_num")) ||
        point.displayName.contains(QStringLiteral("编号")))
        return sample.unit;

    const QString signalText = key + QStringLiteral(" ") + point.displayName.toLower();
    if (signalText.contains(QStringLiteral("温度")) ||
        signalText.contains(QStringLiteral("temp")) ||
        signalText.contains(QStringLiteral("temperature")))
        return QStringLiteral("℃");
    return sample.unit;
}

QString BusinessViewWidget::qualityText(QualityCode quality) const
{
    return qualityCodeToString(quality);
}

void BusinessViewWidget::refreshRows()
{
    const bool overview = m_blockTabs->currentIndex() == 0;
    QString filter = overview
        ? QStringLiteral("All blocks")
        : m_blockTabs->tabData(m_blockTabs->currentIndex()).toString();
    const bool detailVoltage = filter == QStringLiteral("Rack Detail::Cell Voltage");
    const bool detailTemperature = filter == QStringLiteral("Rack Detail::Cell Temperature");
    const bool detailAuxiliary = filter == QStringLiteral("Rack Detail::Auxiliary");
    if (detailVoltage || detailTemperature || detailAuxiliary)
        filter = QStringLiteral("Rack Detail");
    const bool showDefinition = filter == QStringLiteral("Rack Signal");
    const bool showControl = filter == QStringLiteral("Rack Control");
    const int desiredColumnCount = showDefinition ? 8 : (showControl ? 11 : 7);
    const bool resizeForContent = m_table->columnCount() != desiredColumnCount ||
                                  m_table->rowCount() == 0;
    m_table->setColumnCount(desiredColumnCount);
    if (showDefinition)
    {
        m_table->setHorizontalHeaderLabels({tr("Name"), tr("Address"), tr("Key"), tr("Value"),
                                             tr("Unit"), tr("Quality"), tr("Updated (Local)"),
                                             tr("Definition")});
    }
    else if (showControl)
    {
        m_table->setHorizontalHeaderLabels({tr("Name"), tr("Address"), tr("Key"),
                                             tr("Access"), tr("Write enable"), tr("Action"),
                                             tr("Value"), tr("Unit"), tr("Quality"),
                                             tr("Updated (Local)"), tr("Definition")});
    }
    else
    {
        m_table->setHorizontalHeaderLabels({tr("Name"), tr("Address"), tr("Key"), tr("Value"),
                                             tr("Unit"), tr("Quality"), tr("Updated (Local)")});
    }
    m_table->setRowCount(0);
    int good = 0;
    int visible = 0;
    QVector<DisplayRow> rows;
    rows.reserve(m_samples.size());
    for (auto it = m_samples.cbegin(); it != m_samples.cend(); ++it)
    {
        const AcquisitionSample &sample = it.value();
        const PointDefinition point = m_pointDefinitions.value(sample.pointKey);
        if (overview && !m_overviewKeys.contains(sample.pointKey))
            continue;
        if (!overview && sample.block != filter)
            continue;
        if (filter == QStringLiteral("Rack Control") && !m_writableKeys.contains(sample.pointKey))
            continue;

        const bool isVoltageArray = sample.pointKey == QStringLiteral("single_vol_n{512}");
        const bool isTemperatureArray = sample.pointKey == QStringLiteral("bat_temp_n{512}");
        if (detailVoltage || detailTemperature || detailAuxiliary)
        {
            if (detailVoltage && !isVoltageArray)
                continue;
            if (detailTemperature && !isTemperatureArray)
                continue;
            if (detailAuxiliary && (isVoltageArray || isTemperatureArray))
                continue;
        }

        if (showDefinition && !point.bitFields.isEmpty())
        {
            DisplayRow parent;
            parent.sample = sample;
            parent.point = point;
            parent.displayName = QStringLiteral("[%1] %2")
                .arg(m_expandedSignalKeys.contains(point.key) ? QStringLiteral("-")
                                                               : QStringLiteral("+"))
                .arg(sample.displayName);
            parent.key = sample.pointKey;
            parent.parentKey = point.key;
            parent.address = sample.address;
            rows.append(parent);
            if (m_expandedSignalKeys.contains(point.key))
            {
                for (const PointBitField &field : point.bitFields)
                {
                    const QString definition = translateBitText(field.description);
                    if (definition.contains(QStringLiteral("预留")))
                        continue;
                    DisplayRow child;
                    child.sample = sample;
                    child.point = point;
                    child.displayName = QStringLiteral("    %1").arg(definition);
                    child.key = QStringLiteral("%1.bit%2").arg(point.key).arg(field.bit);
                    child.parentKey = point.key;
                    child.address = sample.address;
                    child.bitRow = true;
                    child.bit = field.bit;
                    child.bitDefinition = QStringLiteral("Bit%1：%2").arg(field.bit).arg(definition);
                    rows.append(child);
                }
            }
            continue;
        }

        if ((detailVoltage && isVoltageArray) || (detailTemperature && isTemperatureArray))
        {
            const QVariantList rawValues = sample.rawValue.toList();
            const QVariantList engineeringValues = sample.engineeringValue.toList();
            for (int index = 0; index < point.count; ++index)
            {
                DisplayRow row;
                row.sample = sample;
                row.point = point;
                row.point.key = QStringLiteral("%1_%2").arg(point.key).arg(index + 1, 3, 10, QChar('0'));
                row.point.displayName = QStringLiteral("%1%2").arg(point.displayName)
                    .arg(index + 1, 3, 10, QChar('0'));
                row.point.address = point.address + index;
                row.point.count = 1;
                row.sample.pointKey = row.point.key;
                row.sample.displayName = row.point.displayName;
                row.sample.address = row.point.address;
                row.sample.rawValue = index < rawValues.size()
                    ? QVariantList{rawValues.at(index)} : QVariant();
                row.sample.engineeringValue = index < engineeringValues.size()
                    ? engineeringValues.at(index) : QVariant();
                row.displayName = row.point.displayName;
                row.key = row.point.key;
                row.address = row.point.address;
                rows.append(row);
            }
        }
        else
        {
            DisplayRow row;
            row.sample = sample;
            row.point = point;
            row.displayName = sample.displayName;
            row.key = sample.pointKey;
            row.parentKey = sample.pointKey;
            row.address = sample.address;
            rows.append(row);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const DisplayRow &left, const DisplayRow &right) {
        if (left.address != right.address)
            return left.address < right.address;

        // Keep a register's parent row and bit details together. Bit rows must
        // use their numeric bit index, otherwise string ordering puts bit10
        // before bit2.
        const QString leftGroup = left.parentKey.isEmpty() ? left.key : left.parentKey;
        const QString rightGroup = right.parentKey.isEmpty() ? right.key : right.parentKey;
        if (leftGroup != rightGroup)
            return leftGroup < rightGroup;
        if (left.bitRow != right.bitRow)
            return !left.bitRow;
        if (left.bitRow && right.bitRow && left.bit != right.bit)
            return left.bit < right.bit;
        return left.key < right.key;
    });
    for (const DisplayRow &rowData : rows)
    {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        AcquisitionSample sample = rowData.sample;
        const PointDefinition &point = rowData.point;
        if (rowData.bitRow)
        {
            const QVariantList rawValues = rowData.sample.rawValue.toList();
            const quint32 raw = rawValues.isEmpty() ? rowData.sample.engineeringValue.toUInt()
                                                    : rawValues.first().toUInt();
            const quint16 bitValue = (rowData.bit >= 0 && (raw & (static_cast<quint32>(1) << rowData.bit)))
                ? 1 : 0;
            sample.rawValue = QVariantList{static_cast<int>(bitValue)};
            sample.engineeringValue = static_cast<int>(bitValue);
        }
        const bool controlValueEditable = showControl && !m_running && !rowData.bitRow && point.canWrite() &&
                                          m_pointWriteEnabled.contains(point.key);
        const QString valueText = (showDefinition || showControl) ? displayHexValue(sample)
                                                                    : displayPointValue(point, sample);
        const QString timestampText = sample.timestampUtc.isValid()
            ? sample.timestampUtc.toLocalTime().toString(Qt::ISODateWithMs) : QString();
        const QStringList columns = showControl
            ? QStringList{
                rowData.displayName, QString::asprintf("0x%04X", rowData.address), rowData.key,
                point.attribute, QString(), QString(), valueText, displayUnit(point, sample),
                qualityText(sample.quality), timestampText, localizedDefinition(point)}
            : QStringList{
                rowData.displayName, QString::asprintf("0x%04X", rowData.address), rowData.key,
                valueText, displayUnit(point, sample), qualityText(sample.quality), timestampText};
        const int valueColumn = showControl ? 6 : 3;
        for (int column = 0; column < columns.size(); ++column)
        {
            auto *item = new QTableWidgetItem(columns.at(column));
            if (column != valueColumn || !controlValueEditable)
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            m_table->setItem(row, column, item);
        }
        if (showDefinition)
            m_table->setItem(row, 7, new QTableWidgetItem(
                rowData.bitRow ? rowData.bitDefinition : localizedDefinition(point)));
        else if (showControl)
        {
            const bool writable = !rowData.bitRow && point.canWrite();
            const bool writeEnabled = controlValueEditable;
            auto *writeSwitch = new QCheckBox(m_table);
            writeSwitch->setChecked(writeEnabled);
            writeSwitch->setEnabled(writable && !m_running);
            const QString pointKey = rowData.key;
            connect(writeSwitch, &QCheckBox::toggled, this,
                    [this, pointKey](bool enabled) {
                        if (enabled)
                            m_pointWriteEnabled.insert(pointKey);
                        else
                            m_pointWriteEnabled.remove(pointKey);
                        bool allEnabled = !m_writableKeys.isEmpty();
                        for (const QString &key : m_writableKeys)
                            allEnabled = allEnabled && m_pointWriteEnabled.contains(key);
                        m_writeAllSwitch->blockSignals(true);
                        m_writeAllSwitch->setChecked(allEnabled);
                        m_writeAllSwitch->blockSignals(false);
                        refreshRows();
                    });
            m_table->setCellWidget(row, 4, writeSwitch);
            auto *writeButton = new QPushButton(tr("Write"), m_table);
            writeButton->setEnabled(writeEnabled && !m_running);
            connect(writeButton, &QPushButton::clicked, this,
                    [this, pointKey]() {
                        for (int currentRow = 0; currentRow < m_table->rowCount(); ++currentRow)
                        {
                            QTableWidgetItem *keyItem = m_table->item(currentRow, 2);
                            if (!keyItem || keyItem->text() != pointKey)
                                continue;
                            QTableWidgetItem *valueItem = m_table->item(currentRow, 6);
                            emit controlWriteRequested(pointKey,
                                                       valueItem ? valueItem->text().trimmed() : QString());
                            break;
                        }
                    });
            m_table->setCellWidget(row, 5, writeButton);
        }
        if (sample.quality == QualityCode::Good)
            ++good;
        ++visible;
    }
    updateSummary();
    if (visible > 0 && resizeForContent)
        m_table->resizeColumnsToContents();
    Q_UNUSED(good);
}

void BusinessViewWidget::toggleSignalDetails(int row, int column)
{
    Q_UNUSED(column);
    if (m_blockTabs->currentIndex() <= 0 ||
        m_blockTabs->tabData(m_blockTabs->currentIndex()).toString() != QStringLiteral("Rack Signal"))
        return;
    QTableWidgetItem *keyItem = m_table->item(row, 2);
    if (!keyItem)
        return;
    const QString key = keyItem->text();
    const PointDefinition point = m_pointDefinitions.value(key);
    if (point.bitFields.isEmpty())
        return;
    if (m_expandedSignalKeys.contains(key))
        m_expandedSignalKeys.remove(key);
    else
        m_expandedSignalKeys.insert(key);
    refreshRows();
}

void BusinessViewWidget::showSignalContextMenu(const QPoint &position)
{
    if (m_blockTabs->currentIndex() <= 0 ||
        m_blockTabs->tabData(m_blockTabs->currentIndex()).toString() != QStringLiteral("Rack Signal"))
        return;
    const int row = m_table->rowAt(position.y());
    if (row < 0)
        return;
    QTableWidgetItem *keyItem = m_table->item(row, 2);
    QTableWidgetItem *valueItem = m_table->item(row, 3);
    if (!keyItem || !valueItem || !keyItem->text().contains(QStringLiteral(".bit")) ||
        valueItem->text() != QStringLiteral("0x0001"))
        return;
    const QString key = keyItem->text();
    const int marker = key.lastIndexOf(QStringLiteral(".bit"));
    bool parsed = false;
    const int bit = key.mid(marker + 4).toInt(&parsed);
    if (!parsed || marker <= 0 || bit < 0 || bit > 15)
        return;
    QMenu menu(this);
    QAction *acknowledge = menu.addAction(tr("Acknowledge alarm"));
    if (menu.exec(m_table->viewport()->mapToGlobal(position)) == acknowledge)
        emit alarmAcknowledgeRequested(key.left(marker), bit);
}

void BusinessViewWidget::onBlockTabChanged(int index)
{
    Q_UNUSED(index);
    refreshRows();
}

void BusinessViewWidget::updateSummary()
{
    int good = 0;
    int known = 0;
    for (const AcquisitionSample &sample : m_samples)
    {
        if (sample.timestampUtc.isValid())
            ++known;
        if (sample.quality == QualityCode::Good)
            ++good;
    }
    m_summary->setText(tr("%1 points, %2 good, %3")
                      .arg(m_samples.size()).arg(good)
                      .arg(m_running ? tr("running") : tr("stopped")));
    Q_UNUSED(known);
}
