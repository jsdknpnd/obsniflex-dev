#pragma once

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QDockWidget>
#include <QWidget>
#include <QTimer>
#include <QComboBox>
#include <QVector>
#include <QMutex>

// ส่วนติดต่อกับ C (เพื่อให้ plugin-main.c เรียกใช้ได้)
#ifdef __cplusplus
extern "C" {
#endif

void s2_meter_add_dock(void);

#ifdef __cplusplus
}
#endif

// โครงสร้างข้อมูลสำหรับ Mapping dB -> LED
struct LedMapping {
    float db;
    int ledIndex;
};

class S2MeterWidget : public QWidget {
    Q_OBJECT

public:
    explicit S2MeterWidget(QWidget *parent = nullptr);
    ~S2MeterWidget();

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void UpdateMeter();
    void SourceChanged(int index);
    void RefreshSources();

private:
    obs_volmeter_t *volmeter = nullptr;
    obs_source_t *source = nullptr;
    QComboBox *sourceSelect;
    QTimer *timer;
    
    // Meter State
    float leftLevel = -100.0f;
    float rightLevel = -100.0f;
    
    // Decay simulation
    float leftDisplay = -100.0f;
    float rightDisplay = -100.0f;

    // Constants
    const int TOTAL_LEDS = 53;
    const float DECAY_RATE = 2.5f; // dB per frame (Fast Decay)

    // Helper functions
    void DrawChannel(QPainter &p, int x, int y, int width, int height, float levelDb, const QString &label);
    int GetLedFromDb(float db);
    void AttachToSource(const char *name);
    
    static void OBSVolmeterCallback(void *param, const float *magnitude, const float *peak, const float *input_peak);
    
    QMutex dataMutex;
    friend void VolmeterCallbackWrapper(void *param, const float *magnitude, const float *peak, const float *input_peak);
};

class S2Dock : public QDockWidget {
    Q_OBJECT
public:
    S2Dock(QWidget *parent = nullptr);
};