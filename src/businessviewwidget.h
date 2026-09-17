#ifndef BUSINESSVIEWWIDGET_H
#define BUSINESSVIEWWIDGET_H

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTabBar>
#include <QSet>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

#include "acquisitionstore.h"

class BusinessViewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BusinessViewWidget(QWidget *parent = nullptr);

    void setPointTable(const PointTable &table);
    void setSamples(const QVector<AcquisitionSample> &samples);
    void applyPollResult(const PollResult &result, const PointTable &table,
                         const QString &deviceId, int slaveId);
    bool isAcquisitionRunning() const { return m_running; }

signals:
    void startAcquisitionRequested();
    void stopAcquisitionRequested();
    void dataRecordingToggled(bool enabled);
    void controlWriteRequested(const QString &pointKey, const QString &valueText);
    void alarmAcknowledgeRequested(const QString &sourceKey, int bit);

public slots:
    void setAcquisitionRunning(bool running);

private slots:
    void refreshRows();
    void onBlockTabChanged(int index);
    void toggleSignalDetails(int row, int column);
    void showSignalContextMenu(const QPoint &position);
    void onGlobalWriteToggled(bool enabled);

private:
    QString displayValue(const QVariant &value) const;
    QString displayHexValue(const AcquisitionSample &sample) const;
    QString displayPointValue(const PointDefinition &point,
                              const AcquisitionSample &sample) const;
    QString displayUnit(const PointDefinition &point,
                        const AcquisitionSample &sample) const;
    QString localizedDefinition(const PointDefinition &point) const;
    QString qualityText(QualityCode quality) const;
    void updateSummary();
    void scheduleRefresh(bool dataChanged);

    QTabBar *m_blockTabs;
    QPushButton *m_startButton;
    QPushButton *m_stopButton;
    QCheckBox *m_recordDataSwitch;
    QCheckBox *m_writeAllSwitch;
    QLabel *m_summary;
    QTableWidget *m_table;
    QHash<QString, AcquisitionSample> m_samples;
    QHash<QString, PointDefinition> m_pointDefinitions;
    QSet<QString> m_writableKeys;
    QSet<QString> m_pointWriteEnabled;
    QSet<QString> m_expandedSignalKeys;
    QStringList m_blocks;
    QStringList m_overviewKeys;
    bool m_running;
    QTimer *m_refreshTimer;
};

#endif // BUSINESSVIEWWIDGET_H
