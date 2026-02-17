#include "s2-meter-dock.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QLinearGradient>
#include <cmath>
#include <algorithm>

// ตารางเทียบค่า dBFS เป็นตำแหน่ง LED ตามโจทย์
// เราใช้ Linear Interpolation ระหว่างจุดเหล่านี้
const std::vector<LedMapping> MAPPINGS = {
    {-60.0f, 0},  // Silence
    {-15.0f, 5},
    {-10.0f, 15},
    {-7.0f,  21},
    {-5.0f,  25},
    {-3.0f,  29},
    {-2.0f,  31},
    {-1.0f,  33},
    {0.0f,   35},
    {1.0f,   37},
    {2.0f,   39},
    {3.0f,   41},
    {4.0f,   43},
    {5.0f,   45},
    {6.0f,   47},
    {10.0f,  53}  // Max cap
};

// C wrapper เพื่อให้ plugin-main.c เรียกใช้
void s2_meter_add_dock(void) {
    obs_frontend_add_dock(new S2Dock());
}

// ----------------------------------------------------------------------------
// S2Dock Implementation
// ----------------------------------------------------------------------------
S2Dock::S2Dock(QWidget *parent) : QDockWidget(parent) {
    setWindowTitle("Sonifex S2 Meter (S2-ML53)");
    setWidget(new S2MeterWidget(this));
    setObjectName("S2MeterDock");
}

// ----------------------------------------------------------------------------
// S2MeterWidget Implementation
// ----------------------------------------------------------------------------

// Static callback wrapper
void VolmeterCallbackWrapper(void *param, const float *magnitude, const float *peak, const float *input_peak) {
    S2MeterWidget *widget = static_cast<S2MeterWidget*>(param);
    if (widget) {
        QMutexLocker locker(&widget->dataMutex);
        // แปลง Magnitude (0.0-1.0) เป็น dB
        // หาก source เป็น Mono, obs จะส่งมา channel เดียว เราจะ duplicate ไป L/R
        // หาก Stereo, data[0]=L, data[1]=R
        
        float l = magnitude[0];
        float r = magnitude[0]; // Default mono to both
        
        // ตรวจสอบจำนวน Channel จริง (ในที่นี้เราสมมติ Stereo ไว้ก่อนจาก callback array)
        // OBS volmeter callback ไม่ได้ส่ง channel count มาตรงๆ แต่เรา assume จาก source
        // แต่เพื่อความง่าย เราจะอ่านอย่างน้อย 2 ค่าถ้ามี
        
        // หมายเหตุ: Callback นี้ทำงานใน Audio Thread ต้องเร็วที่สุด
        widget->leftLevel = (l > 0.0f) ? 20.0f * log10f(l) : -100.0f;
        
        // ถ้า Source เป็น Stereo จริงๆ จะมี magnitude[1] (ต้องเช็คที่ source channel count อีกที แต่ทำใน UI Thread ยาก)
        // เพื่อความปลอดภัย เราอ่านค่า 2 ถ้ามันมีค่า หรือถ้าไม่มีมันจะเป็น 0
        // วิธีที่ดีกว่าคือเช็คตอน attach แต่ที่นี่เราจะ hack นิดหน่อยเพื่อความเร็ว
        // สมมติว่า callback ส่ง array ตามจำนวน channel ของ volmeter
        if (obs_volmeter_get_nr_channels(widget->volmeter) > 1) {
            r = magnitude[1];
        }
        widget->rightLevel = (r > 0.0f) ? 20.0f * log10f(r) : -100.0f;
    }
}

S2MeterWidget::S2MeterWidget(QWidget *parent) : QWidget(parent) {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(5);

    // Source Selector
    sourceSelect = new QComboBox(this);
    layout->addWidget(sourceSelect);
    
    // Area สำหรับวาด Meter (Spacer ดันให้เต็ม)
    layout->addStretch();

    // Setup Timer สำหรับ Animation (60 FPS)
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &S2MeterWidget::UpdateMeter);
    timer->start(16); // ~60fps

    // Setup Events
    connect(sourceSelect, SIGNAL(currentIndexChanged(int)), this, SLOT(SourceChanged(int)));
    
    // Initial Source Populate
    RefreshSources();
}

S2MeterWidget::~S2MeterWidget() {
    if (volmeter) {
        obs_volmeter_remove_callback(volmeter, VolmeterCallbackWrapper, this);
        obs_volmeter_destroy(volmeter);
    }
    if (source) {
        obs_source_release(source);
    }
}

void S2MeterWidget::RefreshSources() {
    sourceSelect->blockSignals(true);
    sourceSelect->clear();
    sourceSelect->addItem("Select Audio Source...", QVariant(""));
    
    struct obs_frontend_source_list sources = {};
    obs_frontend_get_scenes(&sources); // หรือใช้ get_sources ทั่วไปก็ได้
    // แต่โจทย์บอก "Output Track" ปกติเราจะ Monitor ที่ Master (Program)
    // แต่ OBS API ให้ Volmeter จับที่ Source. เราจะเพิ่ม "Global Audio" sources
    
    // เพิ่ม Global Output (Monitoring/Master) ไม่ได้ตรงๆ ต้องผ่าน Source
    // ดังนั้นเราจะ List ทุก Source ที่มี Audio
    obs_frontend_source_list_free(&sources);
    
    // Enumerate all sources
    auto enum_cb = [](void *param, obs_source_t *source) {
        QComboBox *box = static_cast<QComboBox*>(param);
        uint32_t flags = obs_source_get_output_flags(source);
        if (flags & OBS_SOURCE_AUDIO) {
            const char *name = obs_source_get_name(source);
            box->addItem(name, QString(name));
        }
        return true;
    };
    obs_enum_sources(enum_cb, sourceSelect);
    
    sourceSelect->blockSignals(false);
}

void S2MeterWidget::SourceChanged(int index) {
    QString name = sourceSelect->itemData(index).toString();
    AttachToSource(name.toUtf8().constData());
}

void S2MeterWidget::AttachToSource(const char *name) {
    if (volmeter) {
        obs_volmeter_remove_callback(volmeter, VolmeterCallbackWrapper, this);
        obs_volmeter_destroy(volmeter);
        volmeter = nullptr;
    }
    if (source) {
        obs_source_release(source);
        source = nullptr;
    }

    if (!name || strlen(name) == 0) return;

    source = obs_get_source_by_name(name);
    if (source) {
        volmeter = obs_volmeter_create(OBS_FADER_LOG);
        obs_volmeter_set_update_interval(volmeter, 50); // Fast update
        obs_volmeter_add_callback(volmeter, VolmeterCallbackWrapper, this);
        if (!obs_volmeter_attach_source(volmeter, source)) {
             // Failed to attach
        }
    }
}

// ฟังก์ชันแปลง dB เป็น LED Index (1-53)
int S2MeterWidget::GetLedFromDb(float db) {
    if (db < -60.0f) return 0;
    
    for (size_t i = 0; i < MAPPINGS.size() - 1; i++) {
        if (db >= MAPPINGS[i].db && db <= MAPPINGS[i+1].db) {
            float rangeDb = MAPPINGS[i+1].db - MAPPINGS[i].db;
            float rangeLed = (float)(MAPPINGS[i+1].ledIndex - MAPPINGS[i].ledIndex);
            float progress = (db - MAPPINGS[i].db) / rangeDb;
            return MAPPINGS[i].ledIndex + (int)(progress * rangeLed);
        }
    }
    return (db > 6.0f) ? 53 : 0;
}

void S2MeterWidget::UpdateMeter() {
    // Decay Logic (Fast Peak simulation)
    {
        QMutexLocker locker(&dataMutex);
        
        // Decay Left
        if (leftLevel > leftDisplay) leftDisplay = leftLevel;
        else leftDisplay -= DECAY_RATE; // Decay ลงเรื่อยๆ
        
        // Decay Right
        if (rightLevel > rightDisplay) rightDisplay = rightLevel;
        else rightDisplay -= DECAY_RATE;
        
        // Floor at -60
        if (leftDisplay < -60.0f) leftDisplay = -60.0f;
        if (rightDisplay < -60.0f) rightDisplay = -60.0f;
    }
    update(); // Trigger paintEvent
}

void S2MeterWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 1. Background (S2 Style Grey)
    p.fillRect(rect(), QColor(60, 65, 75)); // Grey-Blueish
    
    // Header Text
    p.setPen(Qt::white);
    QFont font = p.font();
    font.setBold(true);
    font.setPointSize(16);
    p.setFont(font);
    p.drawText(rect(), Qt::AlignTop | Qt::AlignHCenter, "SONIFEX");
    
    font.setPointSize(8);
    p.setFont(font);
    int topMargin = 40;
    
    int meterHeight = 40;
    int gap = 30;
    
    // Draw Meters
    DrawChannel(p, 20, topMargin, width() - 40, meterHeight, leftDisplay, "LEFT");
    DrawChannel(p, 20, topMargin + meterHeight + gap, width() - 40, meterHeight, rightDisplay, "RIGHT");

    // S2 Logo Placeholder (Bottom)
    QRect logoRect(width()/2 - 40, height() - 50, 80, 40);
    QLinearGradient grad(logoRect.topLeft(), logoRect.bottomRight());
    grad.setColorAt(0, QColor(0, 100, 200));
    grad.setColorAt(1, QColor(0, 0, 100));
    p.setBrush(grad);
    p.setPen(Qt::NoPen);
    p.drawEllipse(logoRect);
    p.setPen(Qt::black);
    font.setPointSize(14);
    p.setFont(font);
    p.drawText(logoRect, Qt::AlignCenter, "S2");
}

void S2MeterWidget::DrawChannel(QPainter &p, int x, int y, int w, int h, float levelDb, const QString &label) {
    // Label L/R
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(10);
    p.setFont(f);
    p.drawText(x - 15, y + h/2 + 5, label);

    // Calculate Geometry for 53 LEDs
    float ledWidth = (float)w / 53.0f;
    float ledGap = 1.0f;
    float actualLedW = ledWidth - ledGap;
    
    int litLeds = GetLedFromDb(levelDb);

    // Draw 53 LEDs
    for (int i = 1; i <= 53; i++) {
        float lx = x + (i-1) * ledWidth;
        
        QColor c;
        // Color Rules:
        // 1-26 Green, 27-42 Yellow, 43-53 Red
        if (i <= 26) c = QColor(100, 255, 100);       // Green
        else if (i <= 42) c = QColor(255, 255, 0);    // Yellow
        else c = QColor(255, 50, 50);                 // Red

        // Dim if not lit
        if (i > litLeds) {
            c = c.darker(400); // Off state
        } else {
            // Slight gradient/gloss for lit LEDs
            c = c.lighter(110);
        }

        p.fillRect(QRectF(lx, y, actualLedW, h), c);
        
        // PPM Labels (Top)
        // 1=3, 2=11, 3=19, 4=27, 5=35, 6=43, 7=51
        QString ppmText = "";
        if (i == 3) ppmText = "1";
        else if (i == 11) ppmText = "2";
        else if (i == 19) ppmText = "3";
        else if (i == 27) ppmText = "4";
        else if (i == 35) ppmText = "5";
        else if (i == 43) ppmText = "6";
        else if (i == 51) ppmText = "7";
        
        if (!ppmText.isEmpty()) {
            p.setPen(Qt::white);
            QFont sf = p.font();
            sf.setPointSize(7);
            p.setFont(sf);
            p.drawText(QRectF(lx - 5, y - 15, 20, 15), Qt::AlignCenter, ppmText);
        }

        // VU Labels (Bottom)
        // -15=5, -10=15, -7=21, -5=25, -3=29, -2=31, -1=33, 0=35, 1=37, 2=39, 3=41, 4=43, 5=45, 6=47
        QString vuText = "";
        if (i == 5) vuText = "-15";
        else if (i == 15) vuText = "-10";
        else if (i == 21) vuText = "-7";
        else if (i == 25) vuText = "-5";
        else if (i == 29) vuText = "-3";
        else if (i == 35) vuText = "0";
        else if (i == 41) vuText = "3"; // Shortened list for clarity
        else if (i == 47) vuText = "6";
        
        if (!vuText.isEmpty()) {
            p.setPen(Qt::white);
            QFont sf = p.font();
            sf.setPointSize(7);
            p.setFont(sf);
            p.drawText(QRectF(lx - 10, y + h + 2, 20, 15), Qt::AlignCenter, vuText);
        }
    }
}