#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QTimer>
#include <QElapsedTimer>
#include <QStackedWidget>
#include <QProgressBar>
#include <QMessageBox>
#include <QGraphicsDropShadowEffect>
#include <QDateTime>
#include <QTime>
#include <QList>
#include <QPainter>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileDialog>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QSet>
#include <cmath>


#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QDialog>
#include <QScrollArea>
#include <QInputDialog>
#include <QGridLayout>
#include <QMap>

#include "xlsxreader.h"

// ── Structs ─────────────────────────────────────────────────────
struct OperatorInfo {
    QString id;
    QString nom;
    QString prenom;
};

struct ProductConfigEntry {
    QString partNumber;
    int cycleTimeSeconds = 0;
    int targetPerHour    = 0;
};

struct MachineInfo {
    QString name;
    QString idMachine;
    QString productionLine;
    QString project;
};

struct DowntimeRecord {
    QDateTime startDowntime;
    QDateTime endDowntime;
    qint64    durationSeconds = 0;
    QString   causeArret;   // "Panne machine" | "Problème câble" | "Fin de stock"
};

struct InspectionSession {
    QString   reference;   // = part_number dans product_config.csv
    QString   machine;
    // Cadence saisie par l'utilisateur (pièces/heure)
    int       cadencePerHour  = 0;
    // Résultats
    int       conformes       = 0;
    int       nonConformes    = 0;
    QDateTime startTime;
    QDateTime endTime;
    QList<DowntimeRecord> downtimes;
    // Informations opérateur
    QString   nomOperateur;
    QString   prenomOperateur;
    QString   idOperateur;

    // Calculés
    double durationMinutes() const {
        if (!startTime.isValid()) return 0;
        QDateTime end = endTime.isValid() ? endTime : QDateTime::currentDateTime();
        return startTime.secsTo(end) / 60.0;
    }
    int totalInspected()   const { return conformes + nonConformes; }
    double conformityPct() const {
        return totalInspected() > 0 ? (conformes * 100.0 / totalInspected()) : 0.0;
    }
    double actualCadencePerHour() const {
        double h = durationMinutes() / 60.0;
        return h > 0 ? totalInspected() / h : 0.0;
    }
    int    targetPerShift()  const { return qRound(cadencePerHour * 7.5); }
    double efficiencyPct()   const {
        return cadencePerHour > 0
            ? (actualCadencePerHour() / cadencePerHour) * 100.0 : 0.0;
    }
    qint64 totalDowntimeSecs() const {
        qint64 t = 0; for (const DowntimeRecord &d : downtimes) t += d.durationSeconds;
        return t;
    }
};

// ── Background widget ────────────────────────────────────────────
class BgWidget : public QWidget {
public:
    explicit BgWidget(QWidget *p = nullptr) : QWidget(p) {
        QStringList paths = {
            QCoreApplication::applicationDirPath() + "/lear_background.png",
            QCoreApplication::applicationDirPath() + "/../lear_background.png",
            QDir::currentPath() + "/lear_background.png",
            QDir::currentPath() + "/../lear_background.png",
            QCoreApplication::applicationDirPath() + "/../../lear_background.png",
            "c:/Users/farah/Downloads/inspection_industrielle_corrigee (2)/inspection_industrielle_modifie/lear_background.png"
        };
        for (const QString &path : paths) {
            if (QFile::exists(path)) {
                bgImage = QPixmap(path);
                if (!bgImage.isNull()) break;
            }
        }
    }
protected:
    void resizeEvent(QResizeEvent *ev) override {
        QWidget::resizeEvent(ev);
        rebuildCache();
    }
    void paintEvent(QPaintEvent *) override {
        rebuildCache();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        if (!cachedBg.isNull()) {
            p.drawPixmap(0, 0, cachedBg);
        } else {
            p.fillRect(rect(), QColor(6,10,18));
        }

        // Reflet pastel gris neutre (au lieu de rouge/bleu) + léger éclaircissement
        QRadialGradient r1(width()*.15,height()*.85,width()*.7);
        r1.setColorAt(0,QColor(200,202,208,35)); r1.setColorAt(1,Qt::transparent);
        p.fillRect(rect(),r1);
        QRadialGradient r2(width()*.85,height()*.15,width()*.6);
        r2.setColorAt(0,QColor(210,210,215,28)); r2.setColorAt(1,Qt::transparent);
        p.fillRect(rect(),r2);
        // Léger voile clair uniforme pour éclaircir la photo sans la cacher
        p.fillRect(rect(), QColor(190,192,198,22));
        // Vignette très légère, gris neutre (pas de teinte rouge) pour garder la lisibilité aux bords
        QRadialGradient v(width()/2.0,height()/2.0,width()*1.0);
        v.setColorAt(0,Qt::transparent); v.setColorAt(1,QColor(15,15,18,60));
        p.fillRect(rect(),v);
    }
private:
    void rebuildCache() {
        if (bgImage.isNull() || size().isEmpty()) { cachedBg = QPixmap(); return; }
        if (size() == cachedSize) return; // déjà à jour, rien à refaire
        QSize target = size();
        QSize scaled = bgImage.size().scaled(target, Qt::KeepAspectRatioByExpanding);
        QPixmap scaledPix = bgImage.scaled(scaled, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QRect srcRect((scaledPix.width()-target.width())/2,
                      (scaledPix.height()-target.height())/2,
                      target.width(), target.height());
        cachedBg = scaledPix.copy(srcRect);
        cachedSize = target;
    }
    QPixmap bgImage;
    QPixmap cachedBg;
    QSize cachedSize;
};

class PieChartWidget : public QWidget {
    Q_OBJECT
public:
    int conformes = 0;
    int nonConformes = 0;
    explicit PieChartWidget(QWidget *p = nullptr) : QWidget(p) {
        setMinimumSize(250, 250);
    }
    void updateData(int c, int nc) {
        conformes = c; nonConformes = nc;
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        int total = conformes + nonConformes;
        if (total == 0) return;
        
        QRect rect(20, 20, width()-40, height()-40);
        int side = qMin(rect.width(), rect.height());
        rect.setWidth(side); rect.setHeight(side);
        rect.moveCenter(QPoint(width()/2, height()/2));
        
        double cAngle = (double)conformes / total * 360.0;
        
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(39, 174, 96));
        p.drawPie(rect, 90 * 16, -cAngle * 16);
        
        p.setBrush(QColor(231, 76, 60));
        p.drawPie(rect, (90 - cAngle) * 16, -(360 - cAngle) * 16);
    }
};

// ── Main window ──────────────────────────────────────────────────
class MainWindow : public QMainWindow { Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onStartInspection();
    void onConforme();
    void onNonConforme();
    void onReset();
    void onNewSession();
    void onStartDowntime();
    void onStopDowntime();
    void tickDowntime();
    void onExportCSV();
    void onShowSessionTable();
    void onDeleteSession(int row);
    void onEditSessionReference(int row);
    void onDownloadHistoryCsv();
    void showMachineTable();
    void showProductionTable();
    void showBreakdownTable();
    void onAddMachine();
    void onEditMachine(int row);
    void onDeleteMachine(int row);
    void onEditBreakdownGroup(int row);
    void onDeleteBreakdownGroup(int row);


private:
    void setupUI();
    void setupStartPage();
    void setupInspectionPage();
    void applyStyles();
    void updateCounters();
    void updateCadenceDisplay();
    QString formatDur(qint64 s) const;
    QString dataDirPath() const;
    QString dataPath() const;
    void    saveSession(const InspectionSession &s);
    void    saveAllHistory(const QList<InspectionSession> &hist);
    void    saveSessionToDb(const InspectionSession &s);
    void    initDatabase();
    QList<InspectionSession> loadHistory() const;
    void    loadOperatorData();
    void    loadOperatorsFromCsv(const QString &path);
    void    loadOperatorsFromXlsx(const QString &path);
    void    parseOperatorRows(const QVector<QStringList> &rows);
    QString normalizeOperatorId(const QString &raw) const;
    void    loadProductConfig();
    void    populateRefCombo();
    QString machineInfoPath() const;
    void    loadMachineInfo();
    void    saveMachineInfo() const;
    void    syncMachinesFromHistory();
    QString productionRecordsJsonPath() const;
    QString breakdownJsonPath() const;
    QString shiftsJsonPath() const;
    void    writeShiftsJson() const;
    void    regenerateProductionRecordsJson() const;
    void    regenerateBreakdownJson() const;
    QString findDataFile(const QStringList &names) const;
    bool    lookupOperator(const QString &id, OperatorInfo &out) const;
    bool    lookupProduct(const QString &ref, ProductConfigEntry &out) const;
    void    exportCSV();
    void    exportXLSX();
    void    exportHTML();
    void    refreshMachineTable();
    void    refreshProductionTable();
    void    refreshBreakdownTable();
    void    exportTableToCsv(QTableWidget *table, const QString &defaultBaseName);
    void    updateTargetPreview();
    void    updateButtonStates();
    int     getShiftForTime(const QDateTime &dt) const;
    // ── Historique CSV auto-synchro ──
    QString histCsvPath() const;
    void    appendSessionToCsv(const InspectionSession &s, int sessionNumber);
    void    rebuildCsvFromHistory();
    void    pruneSeededPlaceholders();
    void    fitWindowToCurrentPage();
    // ── Cause arrêt ──
    QString askDowntimeCause();


    QStackedWidget *stack;

    // ── Start page ──
    BgWidget    *startPage;
    QComboBox   *refCombo;
    QComboBox   *machineCombo;
    QLineEdit   *idEdit;
    QLabel      *targetHourLbl = nullptr;
    QLabel      *targetShiftLbl = nullptr;
    QPushButton *startBtn;
    QPushButton *exportBtn;

    QMap<QString, OperatorInfo>     operators;
    QMap<QString, ProductConfigEntry> products;
    QList<MachineInfo>              machinesInfo;
    QVector<QVector<int>>           breakdownGroupSessions; // indices hist par ligne agrégée

    // ── Inspection page ──
    BgWidget    *inspPage;
    QLabel      *lbRef, *lbMachine;
    // NOUVEAU : labels opérateur dans inspection
    QLabel      *lbNomOp, *lbPrenomOp, *lbIdOp;
    QLabel      *lbDateTimeNow;
    QLabel      *lbProgress, *lbPercent;
    QProgressBar*progressBar;
    int progressGreenFlashValue = 0;
    QLabel      *lbConf, *lbNonConf, *lbRest;
    QLabel      *lbCadenceHour;
    QLabel      *lbCadenceShift;
    QLabel      *lbTargetHour;
    QLabel      *lbTargetShift;
    QLabel      *lbEfficiency;
    QPushButton *confBtn, *nonConfBtn;
    QPushButton *dtStartBtn, *dtStopBtn;
    QLabel      *dtStatusLbl, *dtTimerLbl, *dtHistoryLbl;
    QFrame      *dtStatusPill = nullptr; // side-bar de statut affiché en haut de la 2ème fenêtre
    QLabel      *dtStatusPillLbl = nullptr;
    QFrame      *dtPanel;
    QPushButton *resetBtn, *newSessBtn, *statsBtn;
    // NOUVEAU : bouton tableau session
    QPushButton *sessionTableBtn;

    // ── Stats page ──
    BgWidget       *statsPage;
    PieChartWidget *pieChart;
    QLabel         *statsRefLabel, *statsTotLabel, *statsConfLabel, *statsNonConfLabel, *statsRestLabel, *statsPctLabel;
    QPushButton    *statsReturnBtn;
    
    void setupStatsPage();
    void showStatsPage();

    InspectionSession        curSess;
    QList<DowntimeRecord>    dtRecords;
    DowntimeRecord           curDt;
    QTimer       *dtTimer;
    QElapsedTimer dtElapsed;
    bool          dtActive = false;
    qint64        dtSecs   = 0;

    QTimer *liveTimer;

    // NOUVEAU : tableau session en cours
    QDialog      *sessionTableDialog;
    QTableWidget *sessionTable;

    QDialog      *choiceDialog;
    QDialog      *machineTableDialog;
    QTableWidget *machineTable;
    QDialog      *productionTableDialog;
    QTableWidget *productionTable;
    QDialog      *breakdownTableDialog;
    QTableWidget *breakdownTable;

    // Cooldown
    QDateTime    lastDecisionTime;
    QTimer       *cooldownTimer;


    static const QStringList MACHINES;
};

#endif
