#include "mainwindow.h"
#include <QApplication>
#include <QScreen>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>


const QStringList MainWindow::MACHINES = {
    "M01","M02","M03",
    "M04","M05","M06",
    "M07","M08",
    "M09","M10",
    "M11"
};

// ═══════════════════════════════════════════════════════════════
//  Persistence
// ═══════════════════════════════════════════════════════════════
// Tous les fichiers générés/utilisés par l'application (json, csv) sont
// centralisés dans un dossier "data" à côté de l'exécutable, plutôt que
// directement dans le dossier de build (ex: .../debug/).
QString MainWindow::dataDirPath() const {
    QString d = QCoreApplication::applicationDirPath() + "/data";
    QDir().mkpath(d);
    return d;
}

QString MainWindow::dataPath() const {
    QString path = findDataFile({"sessions.json"});
    if (!path.isEmpty()) return path;
    // Pas encore trouvé : on le crée dans le dossier data/
    return dataDirPath() + "/sessions.json";
}

void MainWindow::saveSession(const InspectionSession &s) {
    auto hist = loadHistory();
    hist.append(s);
    QJsonArray arr;
    for (const auto &h : std::as_const(hist)) {
        QJsonObject o;
        o["reference"]      = h.reference;
        o["nomOperateur"]   = h.nomOperateur;
        o["prenomOperateur"]= h.prenomOperateur;
        o["idOperateur"]    = h.idOperateur;
        o["machine"]        = h.machine;
        o["cadencePerHour"] = h.cadencePerHour;
        o["conformes"]      = h.conformes;
        o["nonConformes"]   = h.nonConformes;
        o["startTime"]      = h.startTime.toString(Qt::ISODate);
        o["endTime"]        = h.endTime.toString(Qt::ISODate);
        QJsonArray da;
        for (const auto &d : h.downtimes) {
            QJsonObject dobj;
            dobj["start"] = d.startDowntime.toString(Qt::ISODate);
            dobj["end"]   = d.endDowntime.toString(Qt::ISODate);
            dobj["dur"]   = d.durationSeconds;
            dobj["cause"] = d.causeArret;
            da.append(dobj);
        }
        o["downtimes"] = da;
        arr.append(o);
    }
    QFile f(dataPath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson());
}

void MainWindow::saveAllHistory(const QList<InspectionSession> &hist) {
    QJsonArray arr;
    for (const auto &h : hist) {
        QJsonObject o;
        o["reference"]      = h.reference;
        o["nomOperateur"]   = h.nomOperateur;
        o["prenomOperateur"]= h.prenomOperateur;
        o["idOperateur"]    = h.idOperateur;
        o["machine"]        = h.machine;
        o["cadencePerHour"] = h.cadencePerHour;
        o["conformes"]      = h.conformes;
        o["nonConformes"]   = h.nonConformes;
        o["startTime"]      = h.startTime.toString(Qt::ISODate);
        o["endTime"]        = h.endTime.toString(Qt::ISODate);
        QJsonArray da;
        for (const auto &d : h.downtimes) {
            QJsonObject dobj;
            dobj["start"] = d.startDowntime.toString(Qt::ISODate);
            dobj["end"]   = d.endDowntime.toString(Qt::ISODate);
            dobj["dur"]   = d.durationSeconds;
            dobj["cause"] = d.causeArret;
            da.append(dobj);
        }
        o["downtimes"] = da;
        arr.append(o);
    }
    QFile f(dataPath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson());
}

QString MainWindow::findDataFile(const QStringList &names) const {
    QStringList dirs;
    dirs << dataDirPath();
    dirs << QFileInfo(__FILE__).absolutePath();
    dirs << QCoreApplication::applicationDirPath();
    dirs << QCoreApplication::applicationDirPath() + "/..";
    dirs << QDir::currentPath();
    dirs << QCoreApplication::applicationDirPath() + "/../..";
    dirs.removeDuplicates();
    for (const QString &dir : dirs) {
        for (const QString &name : names) {
            const QString path = QDir(dir).filePath(name);
            if (QFile::exists(path))
                return path;
        }
    }
    return {};
}

static QStringList splitCsvLine(const QString &line) {
    QChar delim = line.contains(';') ? QChar(';') : QChar(',');
    QStringList parts;
    QString cur;
    bool inQuotes = false;
    for (const QChar &c : line) {
        if (c == '"') { inQuotes = !inQuotes; continue; }
        if (c == delim && !inQuotes) { parts << cur.trimmed(); cur.clear(); }
        else cur += c;
    }
    parts << cur.trimmed();
    return parts;
}

void MainWindow::loadOperatorData() {
    operators.clear();
    const QString csvPath = findDataFile({"Operators.csv", "operators.csv", "operator.csv"});
    if (!csvPath.isEmpty()) {
        loadOperatorsFromCsv(csvPath);
        if (!operators.isEmpty()) return;
    }
    
    const QString xlsxPath = findDataFile({"Operators.xlsx", "operators.xlsx", "operator.xlsx"});
    if (!xlsxPath.isEmpty()) {
        loadOperatorsFromXlsx(xlsxPath);
    }
}

QString MainWindow::normalizeOperatorId(const QString &raw) const {
    QString s = raw.trimmed();
    // Si Excel exporte "123.0", on ne garde que "123"
    if (s.contains('.')) {
        s = s.split('.').first();
    }

    QString digits;
    for (const QChar &c : s) {
        if (c.isDigit())
            digits += c;
    }
    if (digits.isEmpty()) return QString();
    
    while (digits.length() < 7)
        digits.prepend('0');
    
    if (digits.length() > 7)
        digits = digits.right(7);
        
    return digits;
}

void MainWindow::parseOperatorRows(const QVector<QStringList> &rows) {
    if (rows.isEmpty()) return;

    int idCol = 0, nomCol = 1, prenomCol = 2;
    int startRow = 0;
    const QStringList &first = rows.first();
    bool firstIsHeader = false;
    for (const QString &cell : first) {
        const QString h = cell.toLower();
        if (h.contains("id") || h.contains("nom") || h.contains("prenom")
            || h.contains("prénom") || h.contains("matricule") || h.contains("employ")) {
            firstIsHeader = true;
            break;
        }
    }
    if (firstIsHeader) {
        startRow = 1;
        idCol = nomCol = prenomCol = -1;
        for (int i = 0; i < first.size(); ++i) {
            const QString h = first.at(i).toLower().trimmed().remove(' ').remove('_').remove('"');
            if ((h == "aid" || h.contains("id") || h.contains("matricule") || h.contains("badge") || h.contains("employe")
                 || h.contains("operateur")) && idCol < 0)
                idCol = i;
            else if ((h.contains("nickname") || h.contains("nom") || h == "name" || h == "last") && !h.contains("prenom")
                     && !h.contains("prénom") && nomCol < 0)
                nomCol = i;
            else if (h.contains("prenom") || h.contains("prénom") || h.contains("firstname") || h == "first" || (h == "aname" && nomCol != i))
                prenomCol = i;
        }
        if (idCol < 0) idCol = 0;
        if (nomCol < 0) nomCol = (first.size() > 1) ? 1 : 0;
        if (prenomCol < 0) prenomCol = (first.size() > 2) ? 2 : (first.size() > 1 ? 1 : 0);
    } else {
        // Pas de header ? On force les colonnes A, B, C
        idCol = 0; nomCol = 1; prenomCol = 2;
    }

    for (int r = startRow; r < rows.size(); ++r) {
        const QStringList &cols = rows.at(r);
        if (cols.size() <= idCol) continue;
        const QString rawId = cols.at(idCol).trimmed();
        if (rawId.isEmpty()) continue;
        const QString id = normalizeOperatorId(rawId);
        OperatorInfo op;
        op.id = id;
        op.nom = nomCol >= 0 && nomCol < cols.size() ? cols.at(nomCol).trimmed() : QString();
        op.prenom = prenomCol >= 0 && prenomCol < cols.size() ? cols.at(prenomCol).trimmed() : QString();
        operators.insert(id, op);
    }
    qDebug() << "Chargé" << operators.size() << "opérateurs.";
}

void MainWindow::loadOperatorsFromCsv(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    QVector<QStringList> rows;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        rows.append(splitCsvLine(line));
    }
    parseOperatorRows(rows);
}

void MainWindow::loadOperatorsFromXlsx(const QString &path) {
    XlsxSheet sheet;
    QString err;
    if (!readXlsxFirstSheet(path, sheet, &err)) {
        qDebug() << "Lecture operators.xlsx :" << err;
        return;
    }
    parseOperatorRows(sheet.rows);
}

bool MainWindow::lookupOperator(const QString &id, OperatorInfo &out) const {
    const QString key = normalizeOperatorId(id);
    auto it = operators.constFind(key);
    if (it == operators.constEnd()) return false;
    out = it.value();
    return true;
}

void MainWindow::loadProductConfig() {
    products.clear();
    const QString path = findDataFile({"product_config.csv"});
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    bool first = true;
    int refCol = -1, cycleCol = -1, targetCol = -1;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        QStringList cols = splitCsvLine(line);
        if (first) {
            first = false;
            for (int i = 0; i < cols.size(); ++i) {
                const QString h = cols[i].toLower().remove('"').remove(' ');
                if (h == "part_number" || h == "partnumber" || h.contains("part_number"))
                    refCol = i;
                else if (h.contains("cycle")) cycleCol = i;
                else if (h.contains("target") || h.contains("cible") || h.contains("hour") || h.contains("heure"))
                    targetCol = i;
            }
            if (refCol < 0) refCol = 0;
            if (cycleCol < 0) cycleCol = qMin(1, cols.size() - 1);
            if (targetCol < 0) targetCol = qMin(2, cols.size() - 1);
            continue;
        }
        if (cols.size() <= refCol) continue;
        ProductConfigEntry p;
        p.partNumber       = cols[refCol].remove('"').trimmed();
        p.cycleTimeSeconds = cycleCol >= 0 && cycleCol < cols.size() ? cols[cycleCol].toInt() : 0;
        p.targetPerHour    = targetCol >= 0 && targetCol < cols.size() ? cols[targetCol].toInt() : 0;
        if (!p.partNumber.isEmpty())
            products.insert(p.partNumber.toUpper(), p);
    }
}

bool MainWindow::lookupProduct(const QString &ref, ProductConfigEntry &out) const {
    auto it = products.constFind(ref.trimmed().toUpper());
    if (it == products.constEnd()) return false;
    out = it.value();
    return true;
}

// ── machine_info.json ──────────────────────────────────────────
QString MainWindow::machineInfoPath() const {
    QString path = findDataFile({"machine_info.json"});
    if (!path.isEmpty()) return path;
    // Pas encore trouvé : on le crée dans le dossier data/
    return dataDirPath() + "/machine_info.json";
}

void MainWindow::loadMachineInfo() {
    machinesInfo.clear();
    QFile f(machineInfoPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue &v : arr) {
        QJsonObject o = v.toObject();
        MachineInfo m;
        m.name           = o["name"].toString();
        m.idMachine      = o["id_machine"].toString();
        m.productionLine = o["production_line"].toString();
        m.project        = o["project"].toString();
        machinesInfo.append(m);
    }
}

void MainWindow::saveMachineInfo() const {
    QJsonArray arr;
    for (const MachineInfo &m : machinesInfo) {
        QJsonObject o;
        o["name"]            = m.name;
        o["id_machine"]      = m.idMachine;
        o["production_line"] = m.productionLine;
        o["project"]         = m.project;
        arr.append(o);
    }
    QFile f(machineInfoPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(arr).toJson());
    }
}

// Complète machinesInfo avec les machines réellement utilisées lors des
// sessions (historique) mais absentes de machine_info.json, afin que le
// Tableau Machine reflète toujours les machines utilisées. Toute machine
// ajoutée ainsi est aussitôt persistée dans machine_info.json.
void MainWindow::syncMachinesFromHistory() {
    auto hist = loadHistory();
    bool changed = false;
    for (const InspectionSession &s : std::as_const(hist)) {
        QString mName = s.machine.trimmed();
        if (mName.isEmpty() || mName == "—") continue;
        bool found = false;
        for (const MachineInfo &m : std::as_const(machinesInfo)) {
            if (m.name.compare(mName, Qt::CaseInsensitive) == 0 ||
                m.idMachine.compare(mName, Qt::CaseInsensitive) == 0) {
                found = true; break;
            }
        }
        if (!found) {
            MachineInfo nm;
            nm.name = mName;
            nm.idMachine = mName;
            nm.productionLine = "";
            nm.project = "";
            machinesInfo.append(nm);
            changed = true;
        }
    }
    if (changed) saveMachineInfo();
}

// ── production_records.json / breakdown.json (dossier de l'exécutable) ──
QString MainWindow::productionRecordsJsonPath() const {
    return dataDirPath() + "/production_records.json";
}
QString MainWindow::breakdownJsonPath() const {
    return dataDirPath() + "/breakdown.json";
}
QString MainWindow::shiftsJsonPath() const {
    return dataDirPath() + "/shifts.json";
}

// Écrit (ou régénère) shifts.json avec la définition des shifts utilisée
// par getShiftForTime(), afin qu'elle soit consultable/éditable en dehors
// de l'application.
void MainWindow::writeShiftsJson() const {
    QJsonArray arr;
    auto addShift = [&](int num, const QString &start, const QString &end, const QString &label) {
        QJsonObject o;
        o["shift"] = num;
        o["debut"] = start;
        o["fin"]   = end;
        o["label"] = label;
        arr.append(o);
    };
    addShift(1, "06:00", "14:00", "Matin");
    addShift(2, "14:00", "22:00", "Après-midi");
    addShift(3, "22:00", "06:00", "Nuit");

    QFile f(shiftsJsonPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson());
}

void MainWindow::regenerateProductionRecordsJson() const {
    auto hist = loadHistory();
    QJsonArray arr;
    for (const InspectionSession &s : std::as_const(hist)) {
        ProductConfigEntry pcfg;
        int targetH = s.cadencePerHour;
        if (lookupProduct(s.reference, pcfg) && pcfg.targetPerHour > 0)
            targetH = pcfg.targetPerHour;
        double targetSh = targetH * 7.5;
        int shift = getShiftForTime(s.startTime);
        int actualProd = s.conformes + s.nonConformes;
        double perfPct = targetSh > 0 ? (actualProd / targetSh) * 100.0 : 0.0;

        QJsonObject o;
        o["jour"]            = s.startTime.isValid() ? s.startTime.toString("dd/MM/yyyy") : "";
        o["shift"]           = shift;
        o["machine"]         = s.machine;
        o["operateur"]       = (s.nomOperateur + " " + s.prenomOperateur).trimmed();
        o["reference"]       = s.reference;
        o["cible_heure"]     = targetH;
        o["cible_shift"]     = qRound(targetSh);
        o["conforme"]        = s.conformes;
        o["non_conforme"]    = s.nonConformes;
        o["performance_pct"] = QString::number(perfPct, 'f', 1).toDouble();
        arr.append(o);
    }
    QFile f(productionRecordsJsonPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson());
}

void MainWindow::regenerateBreakdownJson() const {
    auto hist = loadHistory();

    struct ArretAgg {
        QString machine, dateStr;
        int     shift = 1;
        int     nbArret = 0;
        qint64  totalDurationSecs = 0;
        QStringList starts, ends, causes;
    };
    QVector<ArretAgg> agg;
    QMap<QString,int> keyToIdx;

    for (const InspectionSession &s : std::as_const(hist)) {
        if (s.downtimes.isEmpty()) continue;
        QString dateStr = s.startTime.isValid() ? s.startTime.toString("dd/MM/yyyy") : "—";
        int shift = getShiftForTime(s.startTime);
        QString key = s.machine + "|" + dateStr + "|" + QString::number(shift);
        int idx;
        auto it = keyToIdx.find(key);
        if (it != keyToIdx.end()) idx = it.value();
        else {
            ArretAgg a; a.machine = s.machine; a.dateStr = dateStr; a.shift = shift;
            agg.append(a); idx = agg.size()-1; keyToIdx.insert(key, idx);
        }
        agg[idx].nbArret += s.downtimes.size();
        for (const auto &d : s.downtimes) {
            agg[idx].totalDurationSecs += d.durationSeconds;
            if (d.startDowntime.isValid()) agg[idx].starts << d.startDowntime.toString("HH:mm");
            agg[idx].ends << (d.endDowntime.isValid() ? d.endDowntime.toString("HH:mm") : "En cours");
            if (!d.causeArret.isEmpty() && !agg[idx].causes.contains(d.causeArret))
                agg[idx].causes << d.causeArret;
        }
    }

    QJsonArray arr;
    for (const ArretAgg &a : std::as_const(agg)) {
        QJsonObject o;
        o["date"]              = a.dateStr;
        o["shift"]             = a.shift;
        o["machine"]           = a.machine;
        o["nombre_arret"]      = a.nbArret;
        o["duree_arret_secondes"] = a.totalDurationSecs;
        o["duree_arret"]       = formatDur(a.totalDurationSecs);
        o["debut_arret"]       = a.starts.join(", ");
        o["fin_arret"]         = a.ends.join(", ");
        o["cause_arret"]       = a.causes.join(" / ");
        arr.append(o);
    }
    QFile f(breakdownJsonPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson());
}

QList<InspectionSession> MainWindow::loadHistory() const {
    QList<InspectionSession> list;
    QFile f(dataPath());
    if (!f.open(QIODevice::ReadOnly)) return list;
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
        QJsonObject o = v.toObject();
        InspectionSession s;
        s.reference      = o["reference"].toString();
        s.nomOperateur   = o["nomOperateur"].toString();
        s.prenomOperateur= o["prenomOperateur"].toString();
        s.idOperateur    = o["idOperateur"].toString();
        s.machine        = o["machine"].toString();
        s.cadencePerHour = o["cadencePerHour"].toInt();
        s.conformes      = o["conformes"].toInt();
        s.nonConformes   = o["nonConformes"].toInt();
        s.startTime      = QDateTime::fromString(o["startTime"].toString(), Qt::ISODate);
        s.endTime        = QDateTime::fromString(o["endTime"].toString(),   Qt::ISODate);
        for (const QJsonValue &dv : o["downtimes"].toArray()) {
            QJsonObject d = dv.toObject();
            DowntimeRecord dr;
            dr.startDowntime   = QDateTime::fromString(d["start"].toString(), Qt::ISODate);
            dr.endDowntime     = QDateTime::fromString(d["end"].toString(),   Qt::ISODate);
            dr.durationSeconds = d["dur"].toInt();
            dr.causeArret      = d["cause"].toString();
            s.downtimes.append(dr);
        }
        list.append(s);
    }
    return list;
}

// ═══════════════════════════════════════════════════════════════
//  Export — Rapport HTML (identique à la photo)
// ═══════════════════════════════════════════════════════════════
void MainWindow::exportCSV() {
    QString savePath = QFileDialog::getSaveFileName(this,
        "Exporter le rapport CSV",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
            + "/rapport_inspection_lear.csv",
        "CSV (*.csv)");
    if (savePath.isEmpty()) return;

    auto hist = loadHistory();

    // Calculs globaux
    int    totConf=0, totNonConf=0;
    double sumCad=0, sumShift=0;
    qint64 totDowntime=0;
    for (int i=0; i<hist.size(); i++) {
        const InspectionSession &s = hist[i];
        totConf    += s.conformes;
        totNonConf += s.nonConformes;
        sumCad     += s.actualCadencePerHour();
        sumShift   += s.actualCadencePerHour() * 7.5;
        totDowntime += s.totalDowntimeSecs();
    }
    int n   = hist.size();
    int tot = totConf + totNonConf;
    double avgCad      = n>0 ? sumCad/n   : 0;
    double avgShift    = n>0 ? sumShift/n : 0;
    double avgDowntime = n>0 ? (double)totDowntime/n : 0;
    double totConfPct  = tot>0 ? (totConf*100.0/tot) : 0;

    QString now = QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm:ss");

    QFile f(savePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Erreur", "Impossible de creer le fichier."); return;
    }
    QTextStream o(&f);
    o.setEncoding(QStringConverter::Utf8);
    o << "\xEF\xBB\xBF"; // BOM UTF-8 pour Excel

    // ══════════════════════════════════════════════
    // EN-TETE DU RAPPORT
    // ══════════════════════════════════════════════
    o << "RAPPORT DE PRODUCTION & INSPECTION - LEAR CORPORATION\n";
    o << "Date d'export             : " << now << "\n";
    o << "Nombre de sessions        : " << n << "\n";
    o << "Shift standard            : 7.5 heures\n";
    o << "Taux de conformité global : " << QString::number(totConfPct, 'f', 1).replace(".", ",") << "%\n";
    o << "\n";

    auto pad = [](const QString &s, int width) -> QString {
        return s.leftJustified(width, ' ');
    };

    // ══════════════════════════════════════════════
    // EN-TETE TABLEAU (ASCII STRICT)
    // ══════════════════════════════════════════════
    o << pad("N°", 8) 
      << pad("REFERENCE", 16) 
      << pad("MACHINE", 28) 
      << pad("CONFORMES", 16) 
      << pad("NON CONFORMES", 16) 
      << pad("CIBLE/HEURE", 16) 
      << pad("TEMPS ARRET", 16) 
      << pad("CIBLE/SHIFT", 16) << "\n";
    
    o << "-------------------------------------------------------------------------------------------------------------------------------------\n";

    // ══════════════════════════════════════════════
    // LIGNES DE DONNEES
    // ══════════════════════════════════════════════
    for (int i=0; i<hist.size(); i++) {
        const InspectionSession &s = hist[i];
        double cReal   = s.actualCadencePerHour();
        double projSh  = cReal * 7.5;
        qint64 dt      = s.totalDowntimeSecs();
        qint64 d_h     = dt / 3600;
        qint64 d_m     = (dt % 3600) / 60;
        qint64 d_s     = dt % 60;
        QString dtStr  = QString::asprintf("%02lld:%02lld:%02lld", d_h, d_m, d_s);

        o << pad(QString::number(i+1), 8) 
          << pad(s.reference, 16) 
          << pad(s.machine, 28) 
          << pad(QString::number(s.conformes), 16) 
          << pad(QString::number(s.nonConformes), 16) 
          << pad(QString::number(qRound(cReal)), 16) 
          << pad(dtStr, 16) 
          << pad(QString::number(qRound(projSh)), 16) 
          << "\n";
    }

    // ══════════════════════════════════════════════
    // TOTAUX & MOYENNES
    // ══════════════════════════════════════════════
    o << "-------------------------------------------------------------------------------------------------------------------------------------\n";
    
    qint64 t_h = totDowntime / 3600;
    qint64 t_m = (totDowntime % 3600) / 60;
    qint64 t_s = totDowntime % 60;
    QString totDtStr = QString::asprintf("%02lld:%02lld:%02lld", t_h, t_m, t_s);

    o << pad("TOTAUX", 8) << pad("", 16) << pad("", 28) 
      << pad(QString::number(totConf), 16) 
      << pad(QString::number(totNonConf), 16) 
      << pad(QString::number(qRound(sumCad)), 16) 
      << pad(totDtStr, 16) 
      << pad(QString::number(qRound(sumShift)), 16) 
      << "\n";

    qint64 avgDtSecs = qRound(avgDowntime);
    qint64 a_h = avgDtSecs / 3600;
    qint64 a_m = (avgDtSecs % 3600) / 60;
    qint64 a_s = avgDtSecs % 60;
    QString avgDtStr = QString::asprintf("%02lld:%02lld:%02lld", a_h, a_m, a_s);

    o << pad("MOYENNES", 8) << pad("", 16) << pad("", 28) 
      << pad("", 16) 
      << pad("", 16) 
      << pad(QString::number(qRound(avgCad)), 16) 
      << pad(avgDtStr, 16) 
      << pad(QString::number(qRound(avgShift)), 16) 
      << "\n";

    o << "\n";
    f.close();

    QMessageBox::information(this, "Export réussi",
        QString("Rapport CSV (Tableau) exporté avec succès !\n\n%1\n\n%2 session(s) exportée(s).\n\nOuvrez avec Excel ou LibreOffice Calc.")
        .arg(savePath).arg(n));
}

// ═══════════════════════════════════════════════════════════════
//  Export — Rapport Excel (.xlsx)
// ═══════════════════════════════════════════════════════════════
void MainWindow::exportXLSX() {
    QString savePath = QFileDialog::getSaveFileName(this,
        "Exporter le rapport Excel",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
            + "/rapport_inspection_lear.xlsx",
        "Excel (*.xlsx)");
    if (savePath.isEmpty()) return;

    auto hist = loadHistory();

    // Calculs globaux
    int    totConf=0, totNonConf=0;
    double sumCad=0, sumShift=0, sumEff=0;
    for (int i=0; i<hist.size(); i++) {
        const InspectionSession &s = hist[i];
        totConf    += s.conformes;
        totNonConf += s.nonConformes;
        sumCad     += s.actualCadencePerHour();
        sumShift   += s.actualCadencePerHour() * 7.5;
        sumEff     += s.efficiencyPct();
    }
    int n   = hist.size();
    int tot = totConf + totNonConf;
    double avgCad      = n>0 ? sumCad/n   : 0;
    double avgShift    = n>0 ? sumShift/n : 0;
    double avgEff      = n>0 ? sumEff/n   : 0;
    double totConfPct  = tot>0 ? (totConf*100.0/tot) : 0;

    QString now = QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm:ss");

    QFile f(savePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Erreur", "Impossible de créer le fichier."); return;
    }
    QTextStream o(&f);
    o.setEncoding(QStringConverter::Utf8);

    // ══════════════════════════════════════════════
    // HTML FORMAT - Excel reconnaît le HTML!
    // C'est plus simple et ça fonctionne partout
    // ══════════════════════════════════════════════
    o << "<!DOCTYPE html>\n";
    o << "<html>\n<head>\n";
    o << "<meta charset=\"UTF-8\">\n";
    o << "<style>\n";
    o << "body { font-family: Arial, sans-serif; }\n";
    o << "table { border-collapse: collapse; width: 100%; margin-top: 20px; }\n";
    o << "th { background-color: #1a2540; color: white; padding: 10px; text-align: left; border: 1px solid #333; }\n";
    o << "td { padding: 8px; border: 1px solid #ddd; }\n";
    o << "tr:nth-child(even) { background-color: #f9f9f9; }\n";
    o << ".header { font-size: 18px; font-weight: bold; color: #1a2540; margin-top: 20px; }\n";
    o << ".info { color: #666; margin: 5px 0; }\n";
    o << ".green { color: #16a34a; font-weight: bold; }\n";
    o << ".orange { color: #ea8c55; font-weight: bold; }\n";
    o << ".red { color: #dc2626; font-weight: bold; }\n";
    o << "</style>\n";
    o << "</head>\n<body>\n";

    // Header
    o << "<div class=\"header\">RAPPORT DE PRODUCTION & INSPECTION - LEAR CORPORATION</div>\n";
    o << "<div class=\"info\">Date: " << now << "</div>\n";
    o << "<div class=\"info\">Sessions: " << n << "</div>\n";
    o << "<div class=\"info\">Taux de conformité global: <span class=\"green\">" << QString::number(totConfPct, 'f', 1) << "%</span></div>\n";

    // Tableau
    o << "<table>\n";
    o << "<tr>\n";
    o << "<th>N°</th>\n";
    o << "<th>Référence</th>\n";
    o << "<th>Machine</th>\n";
    o << "<th>Cadence/h (pcs/h)</th>\n";
    o << "<th>Projection Shift (pcs)</th>\n";
    o << "<th>Efficacité (%)</th>\n";
    o << "<th>Conformes (pcs)</th>\n";
    o << "<th>Non Conformes (pcs)</th>\n";
    o << "<th>Total Inspecté</th>\n";
    o << "<th>Taux Conformité (%)</th>\n";
    o << "</tr>\n";

    for (int i=0; i<hist.size(); i++) {
        const InspectionSession &s = hist[i];
        double cReal   = s.actualCadencePerHour();
        double projSh  = cReal * 7.5;
        double eff     = s.efficiencyPct();
        double confPct = s.conformityPct();

        QString effClass = eff >= 95 ? "green" : eff >= 75 ? "orange" : "red";
        QString confClass = confPct >= 95 ? "green" : confPct >= 75 ? "orange" : "red";

        o << "<tr>\n";
        o << "<td>" << (i+1) << "</td>\n";
        o << "<td>" << s.reference << "</td>\n";
        o << "<td>" << s.machine.split("-").first().trimmed() << "</td>\n";
        o << "<td>" << QString::number(cReal,  'f', 1) << "</td>\n";
        o << "<td>" << QString::number(projSh, 'f', 1) << "</td>\n";
        o << "<td class=\"" << effClass << "\">" << QString::number(eff, 'f', 1) << "</td>\n";
        o << "<td>" << s.conformes << "</td>\n";
        o << "<td>" << s.nonConformes << "</td>\n";
        o << "<td>" << s.totalInspected() << "</td>\n";
        o << "<td class=\"" << confClass << "\">" << QString::number(confPct, 'f', 1) << "</td>\n";
        o << "</tr>\n";
    }

    // Totaux
    o << "<tr style=\"background-color: #e8e8e8; font-weight: bold;\">\n";
    o << "<td colspan=\"6\">TOTAUX</td>\n";
    o << "<td>" << totConf << "</td>\n";
    o << "<td>" << totNonConf << "</td>\n";
    o << "<td>" << tot << "</td>\n";
    o << "<td class=\"green\">" << QString::number(totConfPct, 'f', 1) << "</td>\n";
    o << "</tr>\n";

    // Moyennes
    o << "<tr style=\"background-color: #f0f0f0; font-weight: bold;\">\n";
    o << "<td colspan=\"3\">MOYENNES</td>\n";
    o << "<td>" << QString::number(avgCad, 'f', 1) << "</td>\n";
    o << "<td>" << QString::number(avgShift, 'f', 1) << "</td>\n";
    o << "<td>" << QString::number(avgEff, 'f', 1) << "</td>\n";
    o << "<td colspan=\"4\"></td>\n";
    o << "</tr>\n";

    o << "</table>\n";

    // Formules
    o << "<div class=\"header\" style=\"margin-top: 40px;\">Formules Utilisées</div>\n";
    o << "<div class=\"info\">Cadence/h = Total pièces / Temps écoulé (h)</div>\n";
    o << "<div class=\"info\">Projection Shift = Cadence/h × 7.5</div>\n";
    o << "<div class=\"info\">Efficacité % = (Cadence réelle / Cadence cible) × 100</div>\n";
    o << "<div class=\"info\">Taux Conformité % = (Conformes / Total inspecté) × 100</div>\n";

    o << "</body>\n</html>\n";

    f.close();

    QMessageBox::information(this, "Export réussi",
        QString("Rapport Excel exporté avec succès !\n\n%1\n\n%2 session(s) exportée(s).\n\nOuvrez avec Microsoft Excel ou LibreOffice Calc.")
        .arg(savePath).arg(n));
}



void MainWindow::exportHTML() {
    QString savePath = QFileDialog::getSaveFileName(this,
        "Exporter le rapport HTML",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
            + "/rapport_inspection_lear.html",
        "HTML (*.html)");
    if (savePath.isEmpty()) return;

    auto hist = loadHistory();

    // Calculs globaux
    int    totConf=0, totNonConf=0;
    double sumCad=0, sumShift=0, sumEff=0;
    for (int i=0; i<hist.size(); i++) {
        const InspectionSession &s = hist[i];
        totConf    += s.conformes;
        totNonConf += s.nonConformes;
        sumCad     += s.actualCadencePerHour();
        sumShift   += s.actualCadencePerHour() * 7.5;
        sumEff     += s.efficiencyPct();
    }
    int n   = hist.size();
    int tot = totConf + totNonConf;
    double avgCad      = n>0 ? sumCad/n   : 0;
    double avgShift    = n>0 ? sumShift/n : 0;
    double avgEff      = n>0 ? sumEff/n   : 0;
    double totConfPct  = tot>0 ? (totConf*100.0/tot) : 0;

    QString now = QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm:ss");

    QFile f(savePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Erreur", "Impossible de creer le fichier."); return;
    }
    QTextStream o(&f);
    o.setEncoding(QStringConverter::Utf8);

    // ══════════════════════════════════════════════
    // HTML HEAD
    // ══════════════════════════════════════════════
    o << "<!DOCTYPE html>\n";
    o << "<html lang=\"fr\">\n";
    o << "<head>\n";
    o << "  <meta charset=\"UTF-8\">\n";
    o << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    o << "  <title>Rapport de Production & Inspection - LEAR Corporation</title>\n";
    o << "  <style>\n";
    o << "    * { margin: 0; padding: 0; box-sizing: border-box; }\n";
    o << "    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #f5f5f5; padding: 20px; }\n";
    o << "    .container { max-width: 1200px; margin: 0 auto; background: white; padding: 40px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }\n";
    o << "    .header { border-bottom: 3px solid #dc2626; padding-bottom: 20px; margin-bottom: 30px; }\n";
    o << "    h1 { color: #111827; font-size: 28px; margin-bottom: 10px; }\n";
    o << "    .info-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin-bottom: 30px; }\n";
    o << "    .info-box { background: #f9fafb; padding: 15px; border-radius: 8px; border-left: 4px solid #dc2626; }\n";
    o << "    .info-box label { color: #6b7280; font-weight: 600; font-size: 12px; display: block; margin-bottom: 5px; }\n";
    o << "    .info-box value { color: #111827; font-size: 18px; font-weight: 700; }\n";
    o << "    table { width: 100%; border-collapse: collapse; margin: 30px 0; }\n";
    o << "    table thead { background: #1f2937; color: white; }\n";
    o << "    table th { padding: 12px; text-align: left; font-weight: 600; border: 1px solid #e5e7eb; }\n";
    o << "    table td { padding: 12px; border: 1px solid #e5e7eb; }\n";
    o << "    table tbody tr:nth-child(even) { background: #f9fafb; }\n";
    o << "    table tbody tr:hover { background: #f3f4f6; }\n";
    o << "    .conformite-haute { color: #16a34a; font-weight: 700; }\n";
    o << "    .conformite-moyenne { color: #ea8c55; font-weight: 700; }\n";
    o << "    .conformite-basse { color: #dc2626; font-weight: 700; }\n";
    o << "    .efficacite-haute { color: #16a34a; font-weight: 700; }\n";
    o << "    .efficacite-moyenne { color: #ea8c55; font-weight: 700; }\n";
    o << "    .efficacite-basse { color: #dc2626; font-weight: 700; }\n";
    o << "    .summary { background: #f3f4f6; padding: 20px; border-radius: 8px; margin-top: 20px; }\n";
    o << "    .summary h3 { color: #111827; margin-bottom: 15px; }\n";
    o << "    .summary-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }\n";
    o << "    .summary-item { padding: 10px; background: white; border-radius: 4px; border-left: 3px solid #dc2626; }\n";
    o << "    .summary-item label { color: #6b7280; font-size: 12px; font-weight: 600; display: block; margin-bottom: 5px; }\n";
    o << "    .summary-item value { color: #111827; font-size: 16px; font-weight: 700; }\n";
    o << "    .footer { margin-top: 40px; padding-top: 20px; border-top: 1px solid #e5e7eb; color: #6b7280; font-size: 12px; text-align: center; }\n";
    o << "    .logo { color: #dc2626; font-weight: 700; font-size: 14px; margin-bottom: 10px; }\n";
    o << "  </style>\n";
    o << "</head>\n";
    o << "<body>\n";
    o << "  <div class=\"container\">\n";

    // ══════════════════════════════════════════════
    // HEADER
    // ══════════════════════════════════════════════
    o << "    <div class=\"header\">\n";
    o << "      <div class=\"logo\">◐ LEAR CORPORATION</div>\n";
    o << "      <h1>Rapport de Production & Inspection</h1>\n";
    o << "    </div>\n";

    // ══════════════════════════════════════════════
    // INFO GLOBALES
    // ══════════════════════════════════════════════
    o << "    <div class=\"info-grid\">\n";
    o << "      <div class=\"info-box\">\n";
    o << "        <label>Date d'export</label>\n";
    o << "        <value>" << now << "</value>\n";
    o << "      </div>\n";
    o << "      <div class=\"info-box\">\n";
    o << "        <label>Nombre de sessions</label>\n";
    o << "        <value>" << n << "</value>\n";
    o << "      </div>\n";
    o << "      <div class=\"info-box\">\n";
    o << "        <label>Shift standard</label>\n";
    o << "        <value>7.5 heures</value>\n";
    o << "      </div>\n";
    o << "      <div class=\"info-box\">\n";
    o << "        <label>Taux global de conformité</label>\n";
    o << "        <value>" << QString::number(totConfPct, 'f', 1) << "%</value>\n";
    o << "      </div>\n";
    o << "    </div>\n";

    // ══════════════════════════════════════════════
    // TABLEAU SESSIONS
    // ══════════════════════════════════════════════
    o << "    <h2 style=\"color: #111827; margin-top: 30px; margin-bottom: 15px; font-size: 18px;\">Détail des Sessions</h2>\n";
    o << "    <table>\n";
    o << "      <thead>\n";
    o << "        <tr>\n";
    o << "          <th>N°</th>\n";
    o << "          <th>Référence</th>\n";
    o << "          <th>Machine</th>\n";
    o << "          <th>Cadence/h (pcs/h)</th>\n";
    o << "          <th>Projection Shift (pcs)</th>\n";
    o << "          <th>Efficacité (%)</th>\n";
    o << "          <th>Conformes (pcs)</th>\n";
    o << "          <th>Non Conformes (pcs)</th>\n";
    o << "          <th>Total Inspecté (pcs)</th>\n";
    o << "          <th>Taux Conformité (%)</th>\n";
    o << "        </tr>\n";
    o << "      </thead>\n";
    o << "      <tbody>\n";

    for (int i=0; i<hist.size(); i++) {
        const InspectionSession &s = hist[i];
        double cReal   = s.actualCadencePerHour();
        double projSh  = cReal * 7.5;
        double eff     = s.efficiencyPct();
        double confPct = s.conformityPct();
        
        QString effClass = eff >= 95 ? "efficacite-haute" : eff >= 75 ? "efficacite-moyenne" : "efficacite-basse";
        QString confClass = confPct >= 95 ? "conformite-haute" : confPct >= 75 ? "conformite-moyenne" : "conformite-basse";

        o << "        <tr>\n";
        o << "          <td>" << (i+1) << "</td>\n";
        o << "          <td>" << s.reference << "</td>\n";
        o << "          <td>" << s.machine.split("-").first().trimmed() << "</td>\n";
        o << "          <td>" << QString::number(cReal,  'f', 1) << "</td>\n";
        o << "          <td>" << QString::number(projSh, 'f', 1) << "</td>\n";
        o << "          <td class=\"" << effClass << "\">" << QString::number(eff, 'f', 1) << "</td>\n";
        o << "          <td>" << s.conformes << "</td>\n";
        o << "          <td>" << s.nonConformes << "</td>\n";
        o << "          <td>" << s.totalInspected() << "</td>\n";
        o << "          <td class=\"" << confClass << "\">" << QString::number(confPct, 'f', 1) << "</td>\n";
        o << "        </tr>\n";
    }

    o << "      </tbody>\n";
    o << "    </table>\n";

    // ══════════════════════════════════════════════
    // RESUME
    // ══════════════════════════════════════════════
    o << "    <div class=\"summary\">\n";
    o << "      <h3>Résumé & Moyennes</h3>\n";
    o << "      <div class=\"summary-grid\">\n";
    o << "        <div class=\"summary-item\">\n";
    o << "          <label>Total Pièces Conformes</label>\n";
    o << "          <value>" << totConf << "</value>\n";
    o << "        </div>\n";
    o << "        <div class=\"summary-item\">\n";
    o << "          <label>Total Pièces Non Conformes</label>\n";
    o << "          <value>" << totNonConf << "</value>\n";
    o << "        </div>\n";
    o << "        <div class=\"summary-item\">\n";
    o << "          <label>Total Pièces Inspectées</label>\n";
    o << "          <value>" << tot << "</value>\n";
    o << "        </div>\n";
    o << "        <div class=\"summary-item\">\n";
    o << "          <label>Taux de Conformité Global</label>\n";
    o << "          <value>" << QString::number(totConfPct, 'f', 1) << "%</value>\n";
    o << "        </div>\n";
    o << "        <div class=\"summary-item\">\n";
    o << "          <label>Cadence Moyenne/h</label>\n";
    o << "          <value>" << QString::number(avgCad, 'f', 1) << " pcs/h</value>\n";
    o << "        </div>\n";
    o << "        <div class=\"summary-item\">\n";
    o << "          <label>Projection Shift Moyenne</label>\n";
    o << "          <value>" << QString::number(avgShift, 'f', 1) << " pcs</value>\n";
    o << "        </div>\n";
    o << "        <div class=\"summary-item\">\n";
    o << "          <label>Efficacité Moyenne</label>\n";
    o << "          <value>" << QString::number(avgEff, 'f', 1) << "%</value>\n";
    o << "        </div>\n";
    o << "      </div>\n";
    o << "    </div>\n";

    // ══════════════════════════════════════════════
    // FORMULES
    // ══════════════════════════════════════════════
    o << "    <div style=\"margin-top: 40px; padding: 20px; background: #f9fafb; border-radius: 8px;\">\n";
    o << "      <h3 style=\"color: #111827; margin-bottom: 15px;\">Formules Utilisées</h3>\n";
    o << "      <ul style=\"color: #374151; line-height: 1.8; margin-left: 20px;\">\n";
    o << "        <li><strong>Cadence/h</strong> = Total pièces / Temps écoulé (h)</li>\n";
    o << "        <li><strong>Projection Shift</strong> = Cadence/h × 7.5</li>\n";
    o << "        <li><strong>Efficacité %</strong> = (Cadence réelle / Cadence cible) × 100</li>\n";
    o << "        <li><strong>Taux Conformité %</strong> = (Conformes / Total inspecté) × 100</li>\n";
    o << "      </ul>\n";
    o << "    </div>\n";

    o << "    <div class=\"footer\">\n";
    o << "      <p>Rapport généré automatiquement par le Système d'Inspection Industrielle LEAR Corporation</p>\n";
    o << "      <p>" << now << "</p>\n";
    o << "    </div>\n";

    o << "  </div>\n";
    o << "</body>\n";
    o << "</html>\n";

    f.close();

    QMessageBox::information(this, "Export reussi",
        QString("Rapport HTML exporte avec succes !\n\n%1\n\n%2 session(s) exportee(s).\n\nOuvrez dans votre navigateur.")
        .arg(savePath).arg(n));
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), dtActive(false), dtSecs(0)
{
    setWindowTitle("Système d'Inspection Industrielle — Lear Corporation");
    // Fenêtre centrée sur le contenu, sans plein écran / maximisation —
    // la taille exacte est ajustée à chaque page via fitWindowToCurrentPage()
    setWindowFlag(Qt::WindowMaximizeButtonHint, false);

    dtTimer = new QTimer(this); dtTimer->setInterval(1000);
    connect(dtTimer, &QTimer::timeout, this, &MainWindow::tickDowntime);

    liveTimer = new QTimer(this); liveTimer->setInterval(5000); // refresh cadence every 5s
    connect(liveTimer, &QTimer::timeout, this, &MainWindow::updateCadenceDisplay);

    // Sessions sauvegardées localement en JSON uniquement

    setupUI(); applyStyles();

    // Initialisation cooldown conforme/non-conforme
    lastDecisionTime = QDateTime();
    cooldownTimer = new QTimer(this);
    cooldownTimer->setInterval(1000);
    connect(cooldownTimer, &QTimer::timeout, this, &MainWindow::updateButtonStates);
    cooldownTimer->start();

    loadOperatorData();
    loadProductConfig();
    loadMachineInfo();
    writeShiftsJson();
    pruneSeededPlaceholders();
    rebuildCsvFromHistory();
    populateRefCombo();
    fitWindowToCurrentPage();
}
MainWindow::~MainWindow(){}

void MainWindow::fitWindowToCurrentPage() {
    // Ajuste la taille de la fenêtre à la carte de la page affichée
    // (pas de grand fond vide autour, pas de plein écran).
    QWidget *page = stack ? stack->currentWidget() : nullptr;
    if (!page) return;
    page->adjustSize();
    QSize s = page->sizeHint();
    if (!s.isValid() || s.width() < 100 || s.height() < 100) s = QSize(900, 820);
    setFixedSize(s);
    QRect sg = QApplication::primaryScreen()->availableGeometry();
    move((sg.width()-width())/2, (sg.height()-height())/2);
}

// ═══════════════════════════════════════════════════════════════
//  Database — sauvegarde JSON locale uniquement (MySQL supprimé)
// ═══════════════════════════════════════════════════════════════
void MainWindow::initDatabase() {
    // Sauvegarde locale JSON — aucune base de données externe requise
}

// ═══════════════════════════════════════════════════════════════
//  Database — stub vide (sauvegarde JSON locale utilisée à la place)
// ═══════════════════════════════════════════════════════════════
void MainWindow::saveSessionToDb(const InspectionSession &s) {
    Q_UNUSED(s);
    // Sauvegarde assurée par saveSession() via JSON local
}

// ═══════════════════════════════════════════════════════════════
//  UI Setup
// ═══════════════════════════════════════════════════════════════
void MainWindow::populateRefCombo() {
    if (!refCombo) return;
    refCombo->blockSignals(true);
    refCombo->clear();
    QStringList parts;
    for (auto it = products.constBegin(); it != products.constEnd(); ++it)
        parts << it.value().partNumber;
    parts.sort(Qt::CaseInsensitive);
    for (const QString &pn : parts) {
        ProductConfigEntry p;
        if (lookupProduct(pn, p))
            refCombo->addItem(p.partNumber, p.targetPerHour);
    }
    if (refCombo->count() == 0)
        refCombo->addItem("(product_config.csv introuvable)", 0);
    refCombo->blockSignals(false);
    updateTargetPreview();
}

void MainWindow::updateTargetPreview() {
    if (!targetHourLbl || !targetShiftLbl) return;
    ProductConfigEntry p;
    const QString ref = refCombo->currentText().trimmed();
    if (lookupProduct(ref, p)) {
        const int shiftTarget = qRound(p.targetPerHour * 7.5);
        targetHourLbl->setText(
            QString("Cible horaire (target_per_hour) : %1 pcs/h").arg(p.targetPerHour));
        targetShiftLbl->setText(
            QString("Cible shift (7,5 h) : %1 pièces").arg(shiftTarget));
    } else {
        targetHourLbl->setText("Cible horaire (target_per_hour) : —");
        targetShiftLbl->setText("Cible shift (7,5 h) : —");
    }
}

void MainWindow::setupUI(){
    stack = new QStackedWidget(this);
    setCentralWidget(stack);
    setupStartPage();
    setupInspectionPage();
    setupStatsPage();
    stack->addWidget(startPage);
    stack->addWidget(inspPage);
    stack->addWidget(statsPage);
    stack->setCurrentIndex(0);
}

// ─── START PAGE ─────────────────────────────────────────────────
void MainWindow::setupStartPage(){
    startPage = new BgWidget();
    startPage->setObjectName("startPage");
    QVBoxLayout *pl = new QVBoxLayout(startPage);
    pl->setAlignment(Qt::AlignCenter); pl->setContentsMargins(0,0,0,0);

    QFrame *card = new QFrame(); card->setObjectName("startCard"); card->setFixedWidth(460);
    QVBoxLayout *cl = new QVBoxLayout(card);
    cl->setSpacing(0); cl->setContentsMargins(30,20,30,20);

    // Title
    QHBoxLayout *trow = new QHBoxLayout();
    QLabel *ico = new QLabel(); ico->setObjectName("modalIcon");
    QPixmap logoPix(210, 60);
    logoPix.fill(Qt::transparent);
    QPainter painter(&logoPix);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Cercle rouge
    painter.setBrush(QColor(224, 32, 32));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(0, 4, 40, 40);
    
    // Lettre L noire
    painter.setPen(Qt::black);
    painter.setFont(QFont("Arial", 22, QFont::Bold | QFont::StyleItalic));
    painter.drawText(QRect(0, 4, 40, 40), Qt::AlignCenter, "L");
    
    // Texte LEAR
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 18, QFont::Bold));
    painter.drawText(QRect(50, 4, 160, 24), Qt::AlignLeft | Qt::AlignVCenter, "LEAR");
    
    // Texte CORPORATION
    painter.setPen(QColor(180, 180, 180));
    painter.setFont(QFont("Arial", 9, QFont::Bold));
    painter.drawText(QRect(52, 28, 160, 16), Qt::AlignLeft | Qt::AlignVCenter, "CORPORATION");
    
    painter.end();
    ico->setPixmap(logoPix);
    
    QVBoxLayout *tcol = new QVBoxLayout(); tcol->setSpacing(2);
    QLabel *title = new QLabel(""); title->setObjectName("startTitleLabel");
    QLabel *sub   = new QLabel("");
    sub->setObjectName("startSubtitleLabel"); sub->setWordWrap(true);
    tcol->addWidget(title); tcol->addWidget(sub);
    trow->addWidget(ico,0,Qt::AlignTop); trow->addSpacing(14); trow->addLayout(tcol,1);
    cl->addLayout(trow); cl->addSpacing(18);

    // Reference
    auto addField = [&](const QString &lbl, QWidget *w){
        QLabel *l = new QLabel(lbl); l->setObjectName("fieldLabel");
        cl->addWidget(l); cl->addSpacing(6); cl->addWidget(w); cl->addSpacing(16);
    };
    refCombo = new QComboBox(); refCombo->setObjectName("inputField");
    refCombo->setMinimumHeight(44);
    refCombo->setEditable(false);
    refCombo->setMaxVisibleItems(16);
    addField("📦  PART NUMBER (RÉFÉRENCE)", refCombo);
    connect(refCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ updateTargetPreview(); });

    machineCombo = new QComboBox(); machineCombo->setObjectName("inputField");
    machineCombo->setMinimumHeight(44);
    for (const QString &m:MACHINES) machineCombo->addItem(m);
    addField("🏭  MACHINE / POSTE", machineCombo);

    idEdit = new QLineEdit(); idEdit->setObjectName("inputField");
    idEdit->setMinimumHeight(44);
    idEdit->setMaxLength(3);
    idEdit->setPlaceholderText("ex: 234");
    addField("🆔  3 DERNIERS CHIFFRES DE L'ID OPÉRATEUR", idEdit);

    targetHourLbl = new QLabel();
    targetHourLbl->setObjectName("shiftPreview");
    targetShiftLbl = new QLabel();
    targetShiftLbl->setObjectName("shiftPreview");
    cl->addWidget(targetHourLbl);
    cl->addSpacing(6);
    cl->addWidget(targetShiftLbl);
    cl->addSpacing(24);

    startBtn = new QPushButton("COMMENCER L'INSPECTION");
    startBtn->setObjectName("primaryBtn"); startBtn->setMinimumHeight(58);
    startBtn->setCursor(Qt::PointingHandCursor);
    connect(startBtn,&QPushButton::clicked,this,&MainWindow::onStartInspection);
    cl->addWidget(startBtn); cl->addSpacing(12);

    // Bouton : télécharger l'historique complet en CSV
    QPushButton *csvDownloadBtn = new QPushButton("💾  Télécharger l'historique → historique.csv");
    csvDownloadBtn->setObjectName("shiftPreview");
    csvDownloadBtn->setCursor(Qt::PointingHandCursor);
    csvDownloadBtn->setMinimumHeight(38);
    csvDownloadBtn->setStyleSheet(
        "QPushButton#shiftPreview{background:#04241a;color:#4ade80;border:1px solid #14532d;"
        "  border-radius:8px;font-size:12px;font-weight:700;text-align:center;}"
        "QPushButton#shiftPreview:hover{background:#062e21;border-color:#22c55e;}"
        "QPushButton#shiftPreview:pressed{background:#031a13;}");
    connect(csvDownloadBtn, &QPushButton::clicked, this, &MainWindow::onDownloadHistoryCsv);
    cl->addWidget(csvDownloadBtn);

    // History count badge
    int count = loadHistory().size();
    if (count > 0){
        QLabel *badge = new QLabel(QString("📋  %1 session(s) dans l'historique").arg(count));
        badge->setObjectName("badgeLbl"); badge->setAlignment(Qt::AlignCenter);
        cl->addSpacing(10); cl->addWidget(badge);
    }

    pl->addWidget(card,0,Qt::AlignCenter);
    QGraphicsDropShadowEffect *sh = new QGraphicsDropShadowEffect(card);
    sh->setBlurRadius(50); sh->setOffset(0,4); sh->setColor(QColor(0,0,0,90));
    card->setGraphicsEffect(sh);
}

// ─── INSPECTION PAGE ─────────────────────────────────────────────
void MainWindow::setupInspectionPage(){
    inspPage = new BgWidget(); inspPage->setObjectName("inspectionPage");
    QVBoxLayout *pl = new QVBoxLayout(inspPage);
    pl->setAlignment(Qt::AlignCenter); pl->setContentsMargins(0,0,0,0);

    QFrame *modal = new QFrame(); modal->setObjectName("inspectionCard");
    modal->setFixedWidth(720);
    QVBoxLayout *ml = new QVBoxLayout(modal);
    ml->setSpacing(10); ml->setContentsMargins(30,28,30,28);

    // ── Barre Date / Heure exacte ──
    QHBoxLayout *dtRow = new QHBoxLayout(); dtRow->setContentsMargins(0,0,0,0);
    dtRow->setSpacing(10);
    lbDateTimeNow = new QLabel("—");
    lbDateTimeNow->setObjectName("dateTimeLabel");
    dtRow->addWidget(lbDateTimeNow);
    dtRow->addStretch();

    // Side-bar de statut machine (visible en haut de la 2ème fenêtre)
    dtStatusPill = new QFrame();
    dtStatusPill->setStyleSheet(
        "QFrame{background:rgba(8,14,28,180);border-radius:8px;"
        "border-left:4px solid #22c55e;}");
    QHBoxLayout *pillL = new QHBoxLayout(dtStatusPill);
    pillL->setContentsMargins(10,5,12,5); pillL->setSpacing(0);
    dtStatusPillLbl = new QLabel("● MACHINE EN MARCHE");
    dtStatusPillLbl->setStyleSheet("color:#22c55e;font-size:10.5px;font-weight:800;background:transparent;");
    pillL->addWidget(dtStatusPillLbl);
    dtRow->addWidget(dtStatusPill);

    ml->addLayout(dtRow);

    // ── Header ──
    QFrame *hdrPanel = new QFrame();
    hdrPanel->setStyleSheet("QFrame{background:rgba(6,10,18,150);border-radius:14px;}");
    QHBoxLayout *hdr = new QHBoxLayout(hdrPanel); hdr->setContentsMargins(14,10,14,10);
    auto makeInfo = [&](const QString &title, QLabel *&val) {
        QVBoxLayout *v = new QVBoxLayout(); v->setSpacing(1);
        QLabel *t = new QLabel(title); t->setObjectName("infoTitle");
        val = new QLabel("—"); val->setObjectName("infoValue");
        v->addWidget(t); v->addWidget(val); return v;
    };
    hdr->addLayout(makeInfo("PART NUMBER", lbRef));
    hdr->addSpacing(20);
    hdr->addLayout(makeInfo("MACHINE", lbMachine));
    hdr->addSpacing(20);
    hdr->addLayout(makeInfo("NOM", lbNomOp));
    hdr->addSpacing(20);
    hdr->addLayout(makeInfo("PRÉNOM", lbPrenomOp));
    hdr->addSpacing(20);
    hdr->addLayout(makeInfo("ID", lbIdOp));
    hdr->addStretch();
    QLabel *logo = new QLabel("⚙ LEAR INSPECTION"); logo->setObjectName("logoLabel");
    hdr->addWidget(logo);
    ml->addWidget(hdrPanel);

    QFrame *div = new QFrame(); div->setObjectName("cardDivider");
    div->setFrameShape(QFrame::HLine); div->setFixedHeight(1);
    ml->addWidget(div);

    // ── Progress ──
    QFrame *pf = new QFrame(); pf->setObjectName("progressFrame");
    QVBoxLayout *pfl = new QVBoxLayout(pf);
    pfl->setContentsMargins(16,12,16,12); pfl->setSpacing(5);
    QHBoxLayout *ptop = new QHBoxLayout();
    QVBoxLayout *plc = new QVBoxLayout(); plc->setSpacing(0);
    QLabel *ptitle = new QLabel("PROGRESSION"); ptitle->setObjectName("infoTitle");
    lbProgress = new QLabel("0 / 0"); lbProgress->setObjectName("progressBig");
    plc->addWidget(ptitle); plc->addWidget(lbProgress);
    lbPercent = new QLabel("0%"); lbPercent->setObjectName("percentLabel");
    ptop->addLayout(plc); ptop->addStretch(); ptop->addWidget(lbPercent);
    progressBar = new QProgressBar(); progressBar->setObjectName("mainProgress");
    progressBar->setMinimumHeight(8); progressBar->setTextVisible(false);
    pfl->addLayout(ptop); pfl->addWidget(progressBar);
    ml->addWidget(pf);

    // ── Stat cards (Conforme / Non Conforme / Restantes) ──
    QHBoxLayout *stats = new QHBoxLayout(); stats->setSpacing(8);
    auto makeStatCard = [&](const QString &lbl,const QString &id,const QString &icon,
                             QLabel *&cnt)->QFrame*{
        QFrame *c=new QFrame(); c->setObjectName(id);
        QHBoxLayout *hl=new QHBoxLayout(c);
        hl->setContentsMargins(14,12,14,12); hl->setSpacing(0);
        QVBoxLayout *vl=new QVBoxLayout(); vl->setSpacing(3);
        QLabel *l=new QLabel(lbl); l->setObjectName("statTitle");
        cnt=new QLabel("0"); cnt->setObjectName("statCount");
        vl->addWidget(l); vl->addWidget(cnt);
        hl->addLayout(vl,1);
        QLabel *ic=new QLabel(icon); ic->setObjectName(id+"_icon");
        ic->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
        hl->addWidget(ic); return c;
    };
    stats->addWidget(makeStatCard("CONFORME",    "conformeCard",    "✔",lbConf),   1);
    stats->addWidget(makeStatCard("NON CONFORME","nonConformeCard", "✘",lbNonConf),1);
    stats->addWidget(makeStatCard("RESTANTES",   "restantesCard",   "⏳",lbRest),  1);
    ml->addLayout(stats);

    // ── Cadence panel retiré de cette fenêtre (labels gardés en interne,
    //     non ajoutés au layout, pour ne pas casser updateCadenceDisplay()) ──
    lbCadenceHour  = new QLabel("—");
    lbTargetHour   = new QLabel("cible : —  pcs/h");
    lbCadenceShift = new QLabel("—");
    lbTargetShift  = new QLabel("cible : —  pcs");
    lbEfficiency   = new QLabel("—");

    // ── Action buttons ──
    QHBoxLayout *ar = new QHBoxLayout(); ar->setSpacing(10);
    confBtn = new QPushButton("✔   CONFORME");
    confBtn->setObjectName("conformeBtn"); confBtn->setMinimumHeight(54);
    confBtn->setCursor(Qt::PointingHandCursor);
    connect(confBtn,&QPushButton::clicked,this,&MainWindow::onConforme);
    nonConfBtn = new QPushButton("✘   NON CONFORME");
    nonConfBtn->setObjectName("nonConformeBtn"); nonConfBtn->setMinimumHeight(54);
    nonConfBtn->setCursor(Qt::PointingHandCursor);
    connect(nonConfBtn,&QPushButton::clicked,this,&MainWindow::onNonConforme);
    ar->addWidget(confBtn,1); ar->addWidget(nonConfBtn,1);
    ml->addLayout(ar);

    // ── Downtime buttons ──
    QHBoxLayout *dr = new QHBoxLayout(); dr->setSpacing(10);
    dtStartBtn = new QPushButton("▶   DÉMARRER L'ARRÊT");
    dtStartBtn->setObjectName("downtimeStartBtn"); dtStartBtn->setMinimumHeight(48);
    dtStartBtn->setCursor(Qt::PointingHandCursor);
    connect(dtStartBtn,&QPushButton::clicked,this,&MainWindow::onStartDowntime);
    dtStopBtn = new QPushButton("↺   MACHINE REDÉMARRÉE");
    dtStopBtn->setObjectName("downtimeStopBtn"); dtStopBtn->setMinimumHeight(48);
    dtStopBtn->setCursor(Qt::PointingHandCursor); dtStopBtn->setEnabled(false);
    connect(dtStopBtn,&QPushButton::clicked,this,&MainWindow::onStopDowntime);
    dr->addWidget(dtStartBtn,1); dr->addWidget(dtStopBtn,1);
    ml->addLayout(dr);

    // ── Downtime status panel ──
    dtPanel = new QFrame(); dtPanel->setObjectName("downtimePanel");
    QVBoxLayout *dl = new QVBoxLayout(dtPanel);
    dl->setContentsMargins(14,10,14,10); dl->setSpacing(3);
    QHBoxLayout *dsr = new QHBoxLayout();
    dtStatusLbl = new QLabel("● STATUT : MACHINE EN MARCHE"); dtStatusLbl->setObjectName("downtimeStatus");
    dtTimerLbl  = new QLabel("00:00:00");                     dtTimerLbl->setObjectName("downtimeTimer");
    dsr->addWidget(dtStatusLbl); dsr->addStretch(); dsr->addWidget(dtTimerLbl);
    dl->addLayout(dsr);
    dtHistoryLbl = new QLabel("Aucun arrêt enregistré cette session.");
    dtHistoryLbl->setObjectName("downtimeHistory"); dtHistoryLbl->setWordWrap(true);
    dl->addWidget(dtHistoryLbl);
    ml->addWidget(dtPanel);

    // ── Bottom buttons ──
    QHBoxLayout *br = new QHBoxLayout(); br->setSpacing(10);

    resetBtn = nullptr; // Bouton Réinitialiser retiré de cette fenêtre

    newSessBtn = new QPushButton("＋  NOUVELLE SESSION");
    newSessBtn->setObjectName("newSessBtn"); newSessBtn->setMinimumHeight(46);
    newSessBtn->setCursor(Qt::PointingHandCursor);
    connect(newSessBtn,&QPushButton::clicked,this,&MainWindow::onNewSession);

    statsBtn = new QPushButton("◉  STATISTIQUES");
    statsBtn->setObjectName("statsBtn2"); statsBtn->setMinimumHeight(46);
    statsBtn->setCursor(Qt::PointingHandCursor);
    connect(statsBtn, &QPushButton::clicked, this, &MainWindow::showStatsPage);

    sessionTableBtn = new QPushButton("≡  HISTORIQUE");
    sessionTableBtn->setObjectName("sessionTableBtn"); sessionTableBtn->setMinimumHeight(46);
    sessionTableBtn->setCursor(Qt::PointingHandCursor);
    connect(sessionTableBtn, &QPushButton::clicked, this, &MainWindow::onShowSessionTable);

    br->addWidget(newSessBtn,1);
    br->addWidget(statsBtn,1); br->addWidget(sessionTableBtn,1);
    ml->addLayout(br);

    // ── Initialiser le dialog de choix d'historique ──
    choiceDialog = new QDialog(this);
    choiceDialog->setWindowTitle("Choix de l'historique — LEAR Corporation");
    choiceDialog->setFixedSize(360, 330);
    choiceDialog->setStyleSheet(
        "QDialog { background-color: #080e1c; }"
        "QPushButton#opBtn, QPushButton#refBtn, QPushButton#arretBtn { background-color: #0d1526; color: #e2e8f0; "
        "  border: 1px solid #1e2d4a; border-radius: 13px; padding: 14px 16px; "
        "  font-weight: 700; font-size: 14px; text-align: left; }"
        "QPushButton#opBtn:hover { background-color: #1c1330; border-color: #a78bfa; color: #fff; }"
        "QPushButton#refBtn:hover { background-color: #241a08; border-color: #f59e0b; color: #fff; }"
        "QPushButton#arretBtn:hover { background-color: #2a1010; border-color: #f43f5e; color: #fff; }"
        "QPushButton#choiceCloseBtn { background-color: transparent; color: #f87171; "
        "  border: 1px solid #7f1d1d; border-radius: 9px; padding: 9px 20px; font-weight: 700; }"
        "QPushButton#choiceCloseBtn:hover { background-color: #7f1d1d; color: #fff; }"
        "QLabel { color: #f0f6fc; }"
    );
    QVBoxLayout *choiceOuter = new QVBoxLayout(choiceDialog);
    choiceOuter->setContentsMargins(0,0,0,0); choiceOuter->setSpacing(0);
    QFrame *choiceTopBar = new QFrame(); choiceTopBar->setFixedHeight(4);
    choiceTopBar->setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
                                 "stop:0 #7a0d0d, stop:0.5 #e02020, stop:1 #7a0d0d);");
    choiceOuter->addWidget(choiceTopBar);

    QVBoxLayout *choiceL = new QVBoxLayout();
    choiceL->setContentsMargins(20, 16, 20, 16); choiceL->setSpacing(9);
    choiceOuter->addLayout(choiceL);

    QLabel *choiceTitle = new QLabel("📋  Choisir le tableau historique");
    choiceTitle->setStyleSheet("color:#e02020; font-size:14px; font-weight:800;");
    QLabel *choiceSub = new QLabel("Consultez les machines, la production ou les arrêts (breakdowns)");
    choiceSub->setStyleSheet("color:#94a3b8; font-size:10px; font-weight:500;");
    choiceSub->setWordWrap(true);
    choiceL->addWidget(choiceTitle);
    choiceL->addWidget(choiceSub);
    choiceL->addSpacing(2);

    auto mkChoiceBtn = [](const QString &objName, const QString &emoji,
                          const QString &title, const QString &desc,
                          const QString &badgeBg) -> QPushButton* {
        QPushButton *btn = new QPushButton();
        btn->setObjectName(objName);
        btn->setMinimumHeight(48);
        btn->setCursor(Qt::PointingHandCursor);

        QHBoxLayout *row = new QHBoxLayout(btn);
        row->setContentsMargins(9,5,9,5); row->setSpacing(11);

        QLabel *badge = new QLabel(emoji);
        badge->setFixedSize(32,32);
        badge->setAlignment(Qt::AlignCenter);
        badge->setStyleSheet(QString("background:%1;border-radius:16px;font-size:14px;").arg(badgeBg));

        QVBoxLayout *txtCol = new QVBoxLayout(); txtCol->setSpacing(1);
        QLabel *t = new QLabel(title); t->setStyleSheet("font-size:12.5px;font-weight:800;color:#f0f6fc;background:transparent;");
        QLabel *d = new QLabel(desc);  d->setStyleSheet("font-size:9.5px;font-weight:500;color:#8391a8;background:transparent;");
        txtCol->addWidget(t); txtCol->addWidget(d);

        row->addWidget(badge);
        row->addLayout(txtCol);
        row->addStretch();
        return btn;
    };

    QPushButton *opBtn    = mkChoiceBtn("opBtn",    "🏭", "Tableau Machine",
                              "Liste des machines (ligne, projet)", "#2a1d40");
    QPushButton *refBtn   = mkChoiceBtn("refBtn",   "📦", "Tableau Production Records",
                              "Sessions groupées par référence", "#3a2a10");
    QPushButton *arretBtn = mkChoiceBtn("arretBtn", "🛑", "Tableau Breakdowns",
                              "Machine, shift, nombre et cause d'arrêt", "#3a1010");

    choiceL->addWidget(opBtn);
    choiceL->addWidget(refBtn);
    choiceL->addWidget(arretBtn);
    choiceL->addStretch();

    QPushButton *choiceCloseBtn = new QPushButton("✕  Fermer");
    choiceCloseBtn->setObjectName("choiceCloseBtn");
    choiceCloseBtn->setFixedWidth(120);
    choiceCloseBtn->setCursor(Qt::PointingHandCursor);
    connect(choiceCloseBtn, &QPushButton::clicked, choiceDialog, &QDialog::close);
    
    QHBoxLayout *choiceBtnRow = new QHBoxLayout();
    choiceBtnRow->addStretch();
    choiceBtnRow->addWidget(choiceCloseBtn);
    choiceL->addLayout(choiceBtnRow);

    QGraphicsDropShadowEffect *choiceSh = new QGraphicsDropShadowEffect(choiceDialog);
    choiceSh->setBlurRadius(50); choiceSh->setOffset(0,6); choiceSh->setColor(QColor(200,0,0,90));
    choiceDialog->setGraphicsEffect(choiceSh);

    connect(opBtn, &QPushButton::clicked, this, &MainWindow::showMachineTable);
    connect(refBtn, &QPushButton::clicked, this, &MainWindow::showProductionTable);
    connect(arretBtn, &QPushButton::clicked, this, &MainWindow::showBreakdownTable);

    // ── Table Dialog Common Stylesheet ──
    QString tableDlgStyle = 
        "QDialog { background-color: #0f1624; border-radius: 14px; }"
        "QTableWidget { background-color: #080e1c; color: #c9d1d9; "
        "  gridline-color: transparent; border: 1px solid #1e2d4a; border-radius: 10px; font-size: 12px; }"
        "QTableWidget::item { padding: 5px; border-bottom: 1px solid #131f38; }"
        "QTableWidget::item:selected { background-color: #e02020; color: white; }"
        "QHeaderView::section { background-color: #16213c; color: #8a9ab8; "
        "  font-size: 10px; font-weight: 700; letter-spacing: 0.8px; padding: 6px; "
        "  border: none; border-right: 1px solid #1e2d4a; border-bottom: 2px solid #e02020; }"
        "QPushButton { background-color: #e02020; color: white; border: none; "
        "  border-radius: 7px; padding: 6px 16px; font-weight: 700; font-size: 12px; }"
        "QPushButton:hover { background-color: #f03030; }"
        "QLabel { color: #f0f6fc; font-size: 13px; font-weight: 700; }";

    // ── Initialiser le dialog Tableau Machine ──
    machineTableDialog = new QDialog(this);
    machineTableDialog->setWindowTitle("Tableau Machine — LEAR Corporation");
    machineTableDialog->setMinimumSize(520, 260);
    machineTableDialog->resize(560, 290);
    machineTableDialog->setStyleSheet(tableDlgStyle);
    QVBoxLayout *opDlgL = new QVBoxLayout(machineTableDialog);
    opDlgL->setContentsMargins(16,16,16,16); opDlgL->setSpacing(10);
    QLabel *opDlgTitle = new QLabel("🏭 Tableau Machine");
    opDlgTitle->setStyleSheet("color:#e02020; font-size:15px; font-weight:800;");
    opDlgL->addWidget(opDlgTitle);

    machineTable = new QTableWidget(0, 5);
    machineTable->setHorizontalHeaderLabels({
        "🆔 ID", "🏭 NAME", "📍 PRODUCTION LINE", "📦 PROJET", "⚙ ACTIONS"
    });
    machineTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    machineTable->horizontalHeader()->setMinimumSectionSize(90);
    machineTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    machineTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    machineTable->setAlternatingRowColors(true);
    machineTable->verticalHeader()->setDefaultSectionSize(34);
    machineTable->verticalHeader()->setVisible(false);
    machineTable->setStyleSheet(
        "QTableWidget { alternate-background-color: #0d1830; }"
        "QTableWidget::item { padding: 4px 8px; }");
    opDlgL->addWidget(machineTable);
    QGraphicsDropShadowEffect *opTblSh = new QGraphicsDropShadowEffect(machineTable);
    opTblSh->setBlurRadius(30); opTblSh->setOffset(0,4); opTblSh->setColor(QColor(0,0,0,120));
    machineTable->setGraphicsEffect(opTblSh);

    QPushButton *opCloseBtn = new QPushButton("✕  Fermer");
    opCloseBtn->setFixedWidth(110);
    opCloseBtn->setFixedHeight(32);
    connect(opCloseBtn, &QPushButton::clicked, machineTableDialog, &QDialog::close);
    QHBoxLayout *opBtnRow = new QHBoxLayout();
    opBtnRow->addStretch();
    opBtnRow->addWidget(opCloseBtn);
    opDlgL->addLayout(opBtnRow);

    // ── Initialiser le dialog Tableau Production Records ──
    productionTableDialog = new QDialog(this);
    productionTableDialog->setWindowTitle("Tableau Production Records — LEAR Corporation");
    productionTableDialog->setMinimumSize(640, 280);
    productionTableDialog->resize(760, 310);
    productionTableDialog->setStyleSheet(tableDlgStyle);
    QVBoxLayout *refDlgL = new QVBoxLayout(productionTableDialog);
    refDlgL->setContentsMargins(16,16,16,16); refDlgL->setSpacing(10);
    QLabel *refDlgTitle = new QLabel("📦 Tableau Production Records");
    refDlgTitle->setStyleSheet("color:#e02020; font-size:15px; font-weight:800;");
    refDlgL->addWidget(refDlgTitle);

    productionTable = new QTableWidget(0, 11);
    productionTable->setHorizontalHeaderLabels({
        "📅 JOUR", "🕐 SHIFT", "🏭 MACHINE", "👤 OPÉRATEUR", "🏷 RÉFÉRENCE",
        "⚡ CIBLE / HEURE", "📦 CIBLE / SHIFT", "✔ CONFORME", "✘ NON CONFORME",
        "📈 PERFORMANCE", "⚙ ACTION"
    });
    productionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    productionTable->horizontalHeader()->setMinimumSectionSize(75);
    productionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    productionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    productionTable->setAlternatingRowColors(true);
    productionTable->verticalHeader()->setDefaultSectionSize(34);
    productionTable->verticalHeader()->setVisible(false);
    productionTable->setStyleSheet(
        "QTableWidget { alternate-background-color: #0d1830; }"
        "QTableWidget::item { padding: 4px 8px; }");
    refDlgL->addWidget(productionTable);
    QGraphicsDropShadowEffect *refTblSh = new QGraphicsDropShadowEffect(productionTable);
    refTblSh->setBlurRadius(30); refTblSh->setOffset(0,4); refTblSh->setColor(QColor(0,0,0,120));
    productionTable->setGraphicsEffect(refTblSh);

    QPushButton *refCloseBtn = new QPushButton("✕  Fermer");
    refCloseBtn->setFixedWidth(110);
    refCloseBtn->setFixedHeight(32);
    connect(refCloseBtn, &QPushButton::clicked, productionTableDialog, &QDialog::close);
    QHBoxLayout *refBtnRow = new QHBoxLayout();
    refBtnRow->addStretch();
    refBtnRow->addWidget(refCloseBtn);
    refDlgL->addLayout(refBtnRow);

    // ── Dialog Tableau Breakdowns ──
    breakdownTableDialog = new QDialog(this);
    breakdownTableDialog->setWindowTitle("Tableau Breakdowns — LEAR Corporation");
    breakdownTableDialog->setMinimumSize(700, 300);
    breakdownTableDialog->resize(820, 330);
    breakdownTableDialog->setStyleSheet(tableDlgStyle);
    QVBoxLayout *arretDlgL = new QVBoxLayout(breakdownTableDialog);
    arretDlgL->setContentsMargins(16,16,16,16); arretDlgL->setSpacing(10);
    QLabel *arretDlgTitle = new QLabel("🛑 Tableau Breakdowns");
    arretDlgTitle->setStyleSheet("color:#e02020; font-size:15px; font-weight:800;");
    arretDlgL->addWidget(arretDlgTitle);

    breakdownTable = new QTableWidget(0, 9);
    breakdownTable->setHorizontalHeaderLabels({
        "📅 DATE", "🕐 SHIFT", "🏭 MACHINE", "🛑 NOMBRE D'ARRÊT", "⏱ DURÉE D'ARRÊT",
        "▶ DÉBUT ARRÊT", "⏹ FIN ARRÊT", "❗ CAUSE D'ARRÊT", "⚙ ACTIONS"
    });
    breakdownTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    breakdownTable->horizontalHeader()->setMinimumSectionSize(75);
    breakdownTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    breakdownTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    breakdownTable->setAlternatingRowColors(true);
    breakdownTable->verticalHeader()->setDefaultSectionSize(34);
    breakdownTable->verticalHeader()->setVisible(false);
    breakdownTable->setStyleSheet(
        "QTableWidget { alternate-background-color: #0d1830; }"
        "QTableWidget::item { padding: 4px 8px; }");
    arretDlgL->addWidget(breakdownTable);
    QGraphicsDropShadowEffect *arretTblSh = new QGraphicsDropShadowEffect(breakdownTable);
    arretTblSh->setBlurRadius(30); arretTblSh->setOffset(0,4); arretTblSh->setColor(QColor(0,0,0,120));
    breakdownTable->setGraphicsEffect(arretTblSh);

    QPushButton *arretCloseBtn = new QPushButton("✕  Fermer");
    arretCloseBtn->setFixedWidth(110);
    arretCloseBtn->setFixedHeight(32);
    connect(arretCloseBtn, &QPushButton::clicked, breakdownTableDialog, &QDialog::close);
    QHBoxLayout *arretBtnRow = new QHBoxLayout();
    arretBtnRow->addStretch();
    arretBtnRow->addWidget(arretCloseBtn);
    arretDlgL->addLayout(arretBtnRow);

    // Mettre à nullptr les variables inutilisées pour éviter les crashs potentiels
    sessionTableDialog = nullptr;
    sessionTable = nullptr;

    QGraphicsDropShadowEffect *sh = new QGraphicsDropShadowEffect(modal);
    sh->setBlurRadius(50); sh->setOffset(0,4); sh->setColor(QColor(0,0,0,90));
    modal->setGraphicsEffect(sh);
    pl->addWidget(modal,0,Qt::AlignCenter);
}

// ═══════════════════════════════════════════════════════════════
//  Styles
// ═══════════════════════════════════════════════════════════════
void MainWindow::applyStyles(){
    setStyleSheet(R"(
QMainWindow,BgWidget{background:transparent;}

/* START CARD */
QFrame#startCard{
    background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 rgba(16,22,34,95),stop:1 rgba(8,12,20,100));
    border-radius:20px;
    border-top:4px solid #e02020;
    border-left:1px solid rgba(255,255,255,0.10);
    border-right:1px solid rgba(255,255,255,0.06);
    border-bottom:1px solid rgba(255,255,255,0.06);
}
QLabel#modalIcon{color:#e02020;font-size:30px;}
QLabel#startTitleLabel{color:#f0f6fc;font-size:18px;font-weight:700;font-family:"Segoe UI","Ubuntu",sans-serif;}
QLabel#startSubtitleLabel{color:#cbd5e1;font-size:11px;font-family:"Segoe UI","Ubuntu",sans-serif;}
QLabel#titleLabel{color:#f0f6fc;font-size:24px;font-weight:800;font-family:"Segoe UI","Ubuntu",sans-serif;}
QLabel#subtitleLabel{color:#8a9ab8;font-size:12px;font-family:"Segoe UI","Ubuntu",sans-serif;}
QLabel#fieldLabel{color:#8a9ab8;font-size:11px;font-weight:700;letter-spacing:1.4px;}
QLabel#shiftPreview{
    color:#14b8a6;font-size:13px;font-weight:700;
    background-color:rgba(5,20,20,180);border:1px solid rgba(20,184,166,0.3);border-radius:8px;padding:8px 14px;
}
QLabel#badgeLbl{color:#8a9ab8;font-size:12px;}
QLineEdit#inputField,QComboBox#inputField,QSpinBox#inputField{
    background-color:rgba(8,14,28,180);color:#c9d1d9;border:1px solid rgba(30,45,74,180);
    border-radius:10px;padding:0 16px;font-size:14px;
}
QLineEdit#inputField:hover,QComboBox#inputField:hover,QSpinBox#inputField:hover{border:1px solid rgba(224,32,32,0.5);}
QLineEdit#inputField:focus,QComboBox#inputField:focus,QSpinBox#inputField:focus{border:1px solid #e02020;}
QComboBox#inputField::drop-down{border:none;width:28px;}
QComboBox QAbstractItemView{background:#0f1624;color:#c9d1d9;border:1px solid #1e2d4a;selection-background-color:#e02020;selection-color:white;}
QSpinBox#inputField::up-button,QSpinBox#inputField::down-button{background:#162040;border:none;width:22px;}
QPushButton#primaryBtn{
    background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #c0392b,stop:0.5 #e02020,stop:1 #c0392b);
    color:white;border:none;border-radius:10px;font-size:14px;font-weight:800;letter-spacing:2px;
}
QPushButton#primaryBtn:hover{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #d44235,stop:0.5 #f03030,stop:1 #d44235);}
QPushButton#primaryBtn:pressed{background:#a93226;}
QPushButton#exportBtn{
    background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #0e6b6b,stop:0.5 #0d9488,stop:1 #0e6b6b);
    color:white;border:none;border-radius:10px;font-size:13px;font-weight:700;letter-spacing:1.5px;
}
QPushButton#exportBtn:hover{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #0f8080,stop:0.5 #14b8a6,stop:1 #0f8080);}
QPushButton#exportBtn:pressed{background:#0a5555;}

/* INSPECTION CARD */
QFrame#inspectionCard{
    background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 rgba(16,22,34,95),stop:1 rgba(8,12,20,100));
    border-radius:20px;
    border-top:4px solid #e02020;
    border-left:1px solid rgba(255,255,255,0.10);
    border-right:1px solid rgba(255,255,255,0.06);
    border-bottom:1px solid rgba(255,255,255,0.06);
}
QFrame#cardDivider{background-color:rgba(30,45,74,180);border:none;max-height:1px;}
QLabel#infoTitle{color:#8a9ab8;font-size:10px;font-weight:700;letter-spacing:1.2px;}
QLabel#infoValue{color:#f0f6fc;font-size:16px;font-weight:700;}
QLabel#logoLabel{color:#e02020;font-size:13px;font-weight:800;letter-spacing:2px;}
QLabel#dateTimeLabel{color:#8fd6ff;font-size:11.5px;font-weight:700;background:rgba(6,10,18,150);
    border-radius:9px;padding:5px 12px;}

/* Progress */
QFrame#progressFrame{background-color:rgba(8,14,28,180);border-radius:10px;border:1px solid rgba(30,45,74,180);}
QLabel#progressBig{color:#f0f6fc;font-size:30px;font-weight:800;}
QLabel#percentLabel{color:#8a9ab8;font-size:18px;font-weight:700;}
QProgressBar#mainProgress{background-color:rgba(26,37,64,150);border-radius:4px;border:2px solid #0f5d2a;}
QProgressBar#mainProgress::chunk{
    background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #10b981,stop:0.5 #34d399,stop:1 #10b981);
    border-radius:3px;
    border:1px solid #059669;
}

/* Stat cards */
QFrame#conformeCard{background-color:rgba(10,32,16,180);border-radius:10px;border:1px solid rgba(34,197,94,0.5);}
QFrame#nonConformeCard{background-color:rgba(32,10,10,180);border-radius:10px;border:1px solid rgba(239,68,68,0.5);}
QFrame#restantesCard{background-color:rgba(10,14,31,180);border-radius:10px;border:1px solid rgba(96,128,204,0.5);}
QLabel#statTitle{color:#aaaaaa;font-size:10px;font-weight:700;letter-spacing:1.2px;}
QFrame#conformeCard    QLabel#statCount{color:#22c55e;font-size:34px;font-weight:800;}
QFrame#nonConformeCard QLabel#statCount{color:#ef4444;font-size:34px;font-weight:800;}
QFrame#restantesCard   QLabel#statCount{color:#f0f6fc;font-size:34px;font-weight:800;}
QFrame#conformeCard    QLabel#conformeCard_icon{color:#22c55e;font-size:22px;}
QFrame#nonConformeCard QLabel#nonConformeCard_icon{color:#ef4444;font-size:22px;}
QFrame#restantesCard   QLabel#restantesCard_icon{color:#6080cc;font-size:20px;}

/* Cadence panel */
QFrame#cadencePanel{
    background-color:rgba(8,14,28,180);border-radius:10px;
    border:1px solid rgba(30,45,74,180);
}
QLabel#cadTitle{color:#8a9ab8;font-size:10px;font-weight:700;letter-spacing:1.2px;}
QLabel#cadValue{color:#f0f6fc;font-size:22px;font-weight:800;}
QLabel#cadTarget{color:#8a9ab8;font-size:11px;}
QFrame#cadVsep{background-color:rgba(30,45,74,180);border:none;max-width:1px;}
QLabel#effValue{color:#e3b341;font-size:22px;font-weight:800;}

/* Action buttons */
QPushButton#conformeBtn{
    background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #22c55e,stop:1 #16a34a);
    color:white;border:1px solid #14532d;border-radius:12px;font-size:16px;font-weight:800;letter-spacing:2px;
}
QPushButton#conformeBtn:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #4ade80,stop:1 #22c55e); border:1px solid #16a34a;}
QPushButton#conformeBtn:pressed{background:#22c55e; color:#080e1c; font-weight:900; border:2px solid #16a34a;}
QPushButton#conformeBtn:disabled{background:#0a5f24;color:#4a8a4a; border:none;}

QPushButton#nonConformeBtn{
    background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ef4444,stop:1 #dc2626);
    color:white;border:1px solid #7f1d1d;border-radius:12px;font-size:16px;font-weight:800;letter-spacing:2px;
}
QPushButton#nonConformeBtn:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f87171,stop:1 #ef4444); border:1px solid #dc2626;}
QPushButton#nonConformeBtn:pressed{background:#ef4444; color:#fff; font-weight:900; border:2px solid #991b1b;}
QPushButton#nonConformeBtn:disabled{background:#5f1f1f;color:#a04a4a; border:none;}

/* Downtime buttons */
QPushButton#downtimeStartBtn{
    background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #eab308,stop:1 #ca8a04);
    color:#1e1e1e;border:1px solid #713f12;border-radius:12px;font-size:15px;font-weight:800;letter-spacing:2px;
}

QPushButton#downtimeStartBtn:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f0be30,stop:1 #d4a017);}
QPushButton#downtimeStartBtn:disabled{background:#2d2810;color:#5a4e20;}
QPushButton#downtimeStopBtn{
    background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #1d4ed8,stop:1 #1e40af);
    color:white;border:none;border-radius:10px;font-size:13px;font-weight:800;letter-spacing:1.5px;
}
QPushButton#downtimeStopBtn:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #2563eb,stop:1 #1d4ed8);}
QPushButton#downtimeStopBtn:disabled{background:#101830;color:#2a3560;}

/* Downtime panel */
QFrame#downtimePanel{background-color:rgba(8,14,28,180);border-radius:10px;border:1px solid rgba(30,45,74,180);border-left:4px solid #22c55e;}
QLabel#downtimeStatus{color:#22c55e;font-size:11px;font-weight:700;letter-spacing:0.8px;}
QLabel#downtimeTimer{color:#f0f6fc;font-size:17px;font-weight:700;font-family:"Courier New",monospace;}
QLabel#downtimeHistory{color:#8a9ab8;font-size:11px;}

/* Secondary / bottom buttons — chaque bouton a sa couleur */
QPushButton#resetBtn{
    background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #374151,stop:1 #1f2937);
    color:#f0f6fc;border:1px solid #4b5563;border-radius:10px;
    font-size:13px;font-weight:700;letter-spacing:1px;
}
QPushButton#resetBtn:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #4b5563,stop:1 #374151);border-color:#6b7280;}
QPushButton#resetBtn:pressed{background:#111827;}

QPushButton#newSessBtn{
    background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #059669,stop:1 #047857);
    color:white;border:1px solid #065f46;border-radius:10px;
    font-size:13px;font-weight:700;letter-spacing:1px;
}
QPushButton#newSessBtn:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #10b981,stop:1 #059669);border-color:#34d399;}
QPushButton#newSessBtn:pressed{background:#065f46;}

QPushButton#statsBtn2{
    background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #0284c7,stop:1 #0369a1);
    color:white;border:1px solid #075985;border-radius:10px;
    font-size:13px;font-weight:700;letter-spacing:1px;
}
QPushButton#statsBtn2:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #38bdf8,stop:1 #0284c7);border-color:#7dd3fc;}
QPushButton#statsBtn2:pressed{background:#075985;}

QPushButton#sessionTableBtn{
    background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #7c3aed,stop:1 #5b21b6);
    color:white;border:1px solid #4c1d95;border-radius:10px;
    font-size:13px;font-weight:700;letter-spacing:1px;
}
QPushButton#sessionTableBtn:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #8b5cf6,stop:1 #7c3aed);border-color:#a78bfa;}
QPushButton#sessionTableBtn:pressed{background:#4c1d95;}

QScrollBar:vertical{background:#080e1c;width:6px;border-radius:3px;}
QScrollBar::handle:vertical{background:#253050;border-radius:3px;}
)");
}

// ═══════════════════════════════════════════════════════════════
//  Cadence display update
// ═══════════════════════════════════════════════════════════════
void MainWindow::updateCadenceDisplay(){
    double actual    = curSess.actualCadencePerHour();   // production réelle/h
    double target    = curSess.cadencePerHour;            // cible/h
    double prodShift = actual * 7.5;                     // production réelle extrapolée shift

    // Performance = Production réelle / Cible × 100
    double perf = target > 0 ? (actual / target) * 100.0 : 0.0;

    lbCadenceHour->setText(QString::number(qRound(actual)));
    lbTargetHour->setText(QString("cible : %1  pcs/h").arg(qRound(target)));
    lbCadenceShift->setText(QString::number(qRound(prodShift)));
    lbTargetShift->setText(QString("cible : %1  pcs/shift").arg(curSess.targetPerShift()));

    QString perfStr = QString("%1%").arg(perf,0,'f',1);
    lbEfficiency->setText(perfStr);
    QString effColor = perf >= 100 ? "#22c55e" : perf >= 75 ? "#e3b341" : "#ef4444";
    lbEfficiency->setStyleSheet(QString("color:%1;font-size:22px;font-weight:800;").arg(effColor));
}

// ═══════════════════════════════════════════════════════════════
//  Slots
// ═══════════════════════════════════════════════════════════════
void MainWindow::onStartInspection(){
    QString ref = refCombo->currentText().trimmed();
    if (ref.isEmpty() || ref.startsWith("(")){
        QMessageBox::warning(this,"Champ requis",
            "Veuillez sélectionner un part_number (product_config.csv).");
        refCombo->setFocus(); return;
    }
    // Chercher par les 3 derniers chiffres
    QString suffix = idEdit->text().trimmed().remove(QRegularExpression("\\D"));
    if (suffix.length() < 1){
        QMessageBox::warning(this,"Champ requis",
            "Veuillez saisir au moins 1 chiffre de l'ID opérateur.");
        idEdit->setFocus(); return;
    }
    // Trouver l'opérateur dont l'ID se termine par ces chiffres
    OperatorInfo op;
    bool found = false;
    for (auto it = operators.constBegin(); it != operators.constEnd(); ++it) {
        if (it.key().endsWith(suffix)) {
            op = it.value();
            found = true;
            break;
        }
    }
    if (!found){
        QMessageBox::warning(this,"ID inconnu",
            QString("Aucun opérateur avec un ID se terminant par « %1 » dans Operators.csv.\n"
                    "Vérifiez le fichier dans le dossier de l'application.").arg(suffix));
        idEdit->setFocus(); return;
    }
    ProductConfigEntry prod;
    if (!lookupProduct(ref, prod)){
        QMessageBox::warning(this,"Part number inconnu",
            QString("Le part_number « %1 » n'existe pas dans product_config.csv.").arg(ref));
        refCombo->setFocus(); return;
    }
    curSess = InspectionSession();
    curSess.reference      = prod.partNumber;
    curSess.machine        = machineCombo->currentText();
    curSess.cadencePerHour = prod.targetPerHour;
    curSess.startTime      = QDateTime::currentDateTime();
    curSess.nomOperateur    = op.nom;
    curSess.prenomOperateur = op.prenom;
    curSess.idOperateur     = op.id;
    dtRecords.clear(); dtActive=false; dtSecs=0;
    lastDecisionTime = QDateTime();
    updateButtonStates();

    lbRef->setText(curSess.reference);
    lbMachine->setText(curSess.machine.split("—").first().trimmed());
    // Afficher infos opérateur dans fenêtre 2
    lbNomOp->setText(curSess.nomOperateur.isEmpty() ? "—" : curSess.nomOperateur);
    lbPrenomOp->setText(curSess.prenomOperateur.isEmpty() ? "—" : curSess.prenomOperateur);
    lbIdOp->setText(curSess.idOperateur.isEmpty() ? "—" : curSess.idOperateur);
    lbProgress->setText("0 / 0");
    lbPercent->setText("0%");
    progressBar->setMaximum(curSess.targetPerShift());
    progressBar->setValue(0);
    lbConf->setText("0"); lbNonConf->setText("0");
    lbRest->setText(QString::number(curSess.targetPerShift()));
    lbCadenceHour->setText("—"); lbTargetHour->setText(QString("cible : %1  pcs/h").arg(curSess.cadencePerHour));
    lbCadenceShift->setText("—"); lbTargetShift->setText(QString("cible : %1  pcs/shift").arg(curSess.targetPerShift()));
    lbEfficiency->setText("—");
    dtStatusLbl->setText("● STATUT : MACHINE EN MARCHE");
    dtStatusLbl->setStyleSheet("color:#22c55e;font-size:11px;font-weight:700;");
    dtPanel->setStyleSheet("QFrame#downtimePanel{background-color:#080e1c;border-radius:10px;border:1px solid #1e2d4a;border-left:3px solid #22c55e;}");
    if (dtStatusPillLbl) { dtStatusPillLbl->setText("● MACHINE EN MARCHE"); dtStatusPillLbl->setStyleSheet("color:#22c55e;font-size:10.5px;font-weight:800;background:transparent;"); }
    if (dtStatusPill) dtStatusPill->setStyleSheet("QFrame{background:rgba(8,14,28,180);border-radius:8px;border-left:4px solid #22c55e;}");
    dtTimerLbl->setText("00:00:00");
    dtHistoryLbl->setText("Aucun arrêt enregistré cette session.");
    dtStartBtn->setEnabled(true); dtStopBtn->setEnabled(false);
    confBtn->setEnabled(true); nonConfBtn->setEnabled(true);
    liveTimer->start();
    stack->setCurrentIndex(1);
    fitWindowToCurrentPage();
}

void MainWindow::onConforme() {
    lastDecisionTime = QDateTime::currentDateTime();
    curSess.conformes++;
    updateCounters();
    updateButtonStates();
}

void MainWindow::onNonConforme() {
    lastDecisionTime = QDateTime::currentDateTime();
    curSess.nonConformes++;
    updateCounters();
    updateButtonStates();
}

void MainWindow::updateButtonStates() {
    if (lbDateTimeNow) {
        lbDateTimeNow->setText("🕓  " + QDateTime::currentDateTime().toString("dddd dd MMMM yyyy — HH:mm:ss"));
    }
    if (!confBtn || !nonConfBtn) return;

    int tot = curSess.totalInspected();
    int tgt = curSess.targetPerShift();
    if (tgt > 0 && tot >= tgt) {
        confBtn->setEnabled(false);
        nonConfBtn->setEnabled(false);
        confBtn->setText("✔   CONFORME (Session terminée)");
        nonConfBtn->setText("✘   NON CONFORME (Session terminée)");
        return;
    }

    QDateTime now = QDateTime::currentDateTime();

    // Un seul choix (CONFORME ou NON CONFORME) toutes les 2 minutes,
    // partagé entre les deux boutons : après avoir choisi l'un des deux,
    // les deux sont bloqués pendant 2 min, puis on choisit à nouveau l'un des deux.
    qint64 secsSinceDecision = lastDecisionTime.isValid() ? lastDecisionTime.secsTo(now) : 999999;
    qint64 remaining = 120 - secsSinceDecision;

    if (dtActive) {
        confBtn->setEnabled(false);
        nonConfBtn->setEnabled(false);
        confBtn->setText("✔   CONFORME  [Machine arrêtée]");
        nonConfBtn->setText("✘   NON CONFORME  [Machine arrêtée]");
    } else if (remaining > 0) {
        confBtn->setEnabled(false);
        nonConfBtn->setEnabled(false);
        int m = remaining / 60, s = remaining % 60;
        QString countdown = QString("[%1:%2]").arg(m).arg(s, 2, 10, QChar('0'));
        confBtn->setText("✔   CONFORME  " + countdown);
        nonConfBtn->setText("✘   NON CONFORME  " + countdown);
    } else {
        confBtn->setEnabled(true);
        nonConfBtn->setEnabled(true);
        confBtn->setText("✔   CONFORME");
        nonConfBtn->setText("✘   NON CONFORME");
    }
}

int MainWindow::getShiftForTime(const QDateTime &dt) const {
    if (!dt.isValid()) return 1;
    QTime time = dt.time();
    int hour = time.hour();
    if (hour >= 6 && hour < 14) {
        return 1;
    } else if (hour >= 14 && hour < 22) {
        return 2;
    } else {
        return 3;
    }
}

void MainWindow::updateCounters(){
    int tot  = curSess.totalInspected();
    int tgt  = curSess.targetPerShift();
    int rest = qMax(0, tgt - tot);
    int pct  = tgt > 0 ? qMin(tot*100/tgt,100) : 0;

    lbConf->setText(QString::number(curSess.conformes));
    lbNonConf->setText(QString::number(curSess.nonConformes));
    lbRest->setText(QString::number(rest));
    lbProgress->setText(QString("%1 / %2").arg(tot).arg(tgt));
    lbPercent->setText(QString("%1%").arg(pct));
    progressBar->setValue(qMin(tot,tgt));
    updateCadenceDisplay();

    // Session complète : fin automatique
    if (tot >= tgt){
        confBtn->setEnabled(false); nonConfBtn->setEnabled(false);
        liveTimer->stop();
        if (dtActive){ dtTimer->stop(); dtActive=false; }
        curSess.endTime   = QDateTime::currentDateTime();
        curSess.downtimes = dtRecords;
        saveSession(curSess);
        rebuildCsvFromHistory();
        saveSessionToDb(curSess);  // stub vide
        QMessageBox::information(this,"Session terminée",
            QString("Shift complété !\n\nConformes : %1\nNon conformes : %2\n"
                    "Cadence réelle : %3 pcs/h\nEfficacité : %4%\n\n✔ Session sauvegardée.")
            .arg(curSess.conformes).arg(curSess.nonConformes)
            .arg(curSess.actualCadencePerHour(),0,'f',1)
            .arg(curSess.efficiencyPct(),0,'f',1));
    }
}

void MainWindow::onStartDowntime(){
    if (dtActive) return;

    // ── Demander la cause de l'arrêt ──
    QString cause = askDowntimeCause();
    if (cause.isEmpty()) return;  // annulé

    dtActive=true; dtSecs=0;
    curDt.startDowntime = QDateTime::currentDateTime();
    curDt.durationSeconds = 0;
    curDt.causeArret = cause;
    dtElapsed.start(); dtTimer->start();
    dtStartBtn->setEnabled(false); dtStopBtn->setEnabled(true);
    dtStatusLbl->setText(QString("⚠ STATUT : MACHINE ARRÊTÉE — %1").arg(cause));
    dtStatusLbl->setStyleSheet("color:#ef4444;font-size:11px;font-weight:700;");
    dtPanel->setStyleSheet("QFrame#downtimePanel{background-color:#080e1c;border-radius:10px;border:1px solid #1e2d4a;border-left:3px solid #ef4444;}");
    if (dtStatusPillLbl) { dtStatusPillLbl->setText("⚠ MACHINE ARRÊTÉE"); dtStatusPillLbl->setStyleSheet("color:#ef4444;font-size:10.5px;font-weight:800;background:transparent;"); }
    if (dtStatusPill) dtStatusPill->setStyleSheet("QFrame{background:rgba(8,14,28,180);border-radius:8px;border-left:4px solid #ef4444;}");
    updateButtonStates();
}

void MainWindow::onStopDowntime(){
    if (!dtActive) return;
    dtTimer->stop(); dtActive=false;
    curDt.endDowntime=QDateTime::currentDateTime(); curDt.durationSeconds=dtSecs;
    dtRecords.append(curDt);
    dtStartBtn->setEnabled(true); dtStopBtn->setEnabled(false);
    dtStatusLbl->setText("● STATUT : MACHINE EN MARCHE");
    dtStatusLbl->setStyleSheet("color:#22c55e;font-size:11px;font-weight:700;");
    dtPanel->setStyleSheet("QFrame#downtimePanel{background-color:#080e1c;border-radius:10px;border:1px solid #1e2d4a;border-left:3px solid #22c55e;}");
    if (dtStatusPillLbl) { dtStatusPillLbl->setText("● MACHINE EN MARCHE"); dtStatusPillLbl->setStyleSheet("color:#22c55e;font-size:10.5px;font-weight:800;background:transparent;"); }
    if (dtStatusPill) dtStatusPill->setStyleSheet("QFrame{background:rgba(8,14,28,180);border-radius:8px;border-left:4px solid #22c55e;}");
    QString hist;
    for (int i=0;i<dtRecords.size();i++){
        const DowntimeRecord &r=dtRecords[i];
        hist += QString("Arrêt #%1 — Cause : %2 | Début : %3 | Fin : %4 | Durée : %5\n")
            .arg(i+1)
            .arg(r.causeArret.isEmpty() ? "—" : r.causeArret)
            .arg(r.startDowntime.toString("HH:mm:ss"),
                 r.endDowntime.toString("HH:mm:ss"),
                 formatDur(r.durationSeconds));
    }
    qint64 tot=0; for (const auto &r : std::as_const(dtRecords)) tot+=r.durationSeconds;
    hist+=QString("Temps d'arrêt total : %1").arg(formatDur(tot));
    dtHistoryLbl->setText(hist.trimmed());
    updateButtonStates();
}

void MainWindow::tickDowntime(){
    dtSecs = dtElapsed.elapsed()/1000;
    dtTimerLbl->setText(formatDur(dtSecs));
}

void MainWindow::onReset(){
    if (QMessageBox::question(this,"Réinitialiser","Réinitialiser les compteurs ?",
        QMessageBox::Yes|QMessageBox::No)!=QMessageBox::Yes) return;
    curSess.conformes=0; curSess.nonConformes=0;
    curSess.startTime=QDateTime::currentDateTime();
    lastDecisionTime = QDateTime();
    if (dtActive){dtTimer->stop();dtActive=false;dtStartBtn->setEnabled(true);dtStopBtn->setEnabled(false);}
    dtRecords.clear(); dtTimerLbl->setText("00:00:00");
    dtHistoryLbl->setText("Aucun arrêt enregistré cette session.");
    dtStatusLbl->setText("● STATUT : MACHINE EN MARCHE");
    dtStatusLbl->setStyleSheet("color:#22c55e;font-size:11px;font-weight:700;");
    dtPanel->setStyleSheet("QFrame#downtimePanel{background-color:#080e1c;border-radius:10px;border:1px solid #1e2d4a;border-left:3px solid #22c55e;}");
    if (dtStatusPillLbl) { dtStatusPillLbl->setText("● MACHINE EN MARCHE"); dtStatusPillLbl->setStyleSheet("color:#22c55e;font-size:10.5px;font-weight:800;background:transparent;"); }
    if (dtStatusPill) dtStatusPill->setStyleSheet("QFrame{background:rgba(8,14,28,180);border-radius:8px;border-left:4px solid #22c55e;}");
    updateCounters();
    updateButtonStates();
}

void MainWindow::onNewSession() {
    if (curSess.totalInspected() > 0) {
        if (QMessageBox::question(this, "Session en cours",
            "Sauvegarder la session actuelle avant de commencer une nouvelle ?",
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            curSess.endTime = QDateTime::currentDateTime();
            curSess.downtimes = dtRecords;
            saveSession(curSess);
            rebuildCsvFromHistory();
            saveSessionToDb(curSess);  // stub vide
            QMessageBox::information(this, "Session sauvegardée",
                QString("✔ Session sauvegardée dans l'historique\n\n"
                        "Conformes : %1\nNon conformes : %2")
                .arg(curSess.conformes).arg(curSess.nonConformes));
        }
    }
    // Réinitialiser pour nouvelle session
    liveTimer->stop();
    if (dtActive) { dtTimer->stop(); dtActive = false; }
    lastDecisionTime = QDateTime();
    idEdit->clear();
    stack->setCurrentIndex(0);
    fitWindowToCurrentPage();
}

void MainWindow::onExportCSV() { exportCSV(); }

QString MainWindow::formatDur(qint64 s) const {
    return QString("%1:%2:%3")
        .arg(s / 3600, 2, 10, QChar('0'))
        .arg((s % 3600) / 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0'));
}

void MainWindow::onShowSessionTable() {
    choiceDialog->exec();
}

void MainWindow::showMachineTable() {
    choiceDialog->accept();
    refreshMachineTable();
    machineTableDialog->exec();
}

void MainWindow::showProductionTable() {
    choiceDialog->accept();
    refreshProductionTable();
    productionTableDialog->exec();
}

void MainWindow::showBreakdownTable() {
    choiceDialog->accept();
    refreshBreakdownTable();
    breakdownTableDialog->exec();
}

void MainWindow::refreshMachineTable() {
    syncMachinesFromHistory();
    machineTable->setRowCount(0);
    machineTable->setColumnCount(5);
    machineTable->setHorizontalHeaderLabels({
        "🆔 ID", "🏭 NAME", "📍 PRODUCTION LINE", "📦 PROJET", "⚙ ACTIONS"
    });

    // Style du tableau
    machineTable->setShowGrid(false);
    machineTable->setAlternatingRowColors(false);
    machineTable->setSelectionMode(QAbstractItemView::SingleSelection);
    machineTable->setFocusPolicy(Qt::NoFocus);
    machineTable->horizontalHeader()->setStyleSheet(
        "QHeaderView::section{"
        "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #1e3a5f,stop:1 #0f1e35);"
        "  color:#8fd6ff; font-size:10px; font-weight:800; letter-spacing:0.5px;"
        "  padding:6px 7px; border:none; border-right:1px solid #16213c;"
        "  border-bottom:2px solid #0284c7;}"
    );
    machineTable->setStyleSheet(
        "QTableWidget{background:#0a1120;border:1px solid #17233d;border-radius:14px;"
        "  gridline-color:transparent; font-size:11px;}"
        "QTableWidget::item{padding:4px 9px;border-bottom:1px solid #101b30;}"
        "QTableWidget::item:selected{background:#1a3a5c;color:white;}"
    );
    machineTable->verticalHeader()->setVisible(false);
    machineTable->verticalHeader()->setDefaultSectionSize(31);
    machineTable->horizontalHeader()->setMinimumSectionSize(84);
    machineTable->horizontalHeader()->setHighlightSections(false);

    auto mkItem = [](const QString &txt, const QColor &col = QColor("#c9d1d9"),
                     bool bold = false) {
        QTableWidgetItem *item = new QTableWidgetItem(txt);
        item->setForeground(col);
        item->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
        if (bold) {
            QFont f = item->font(); f.setBold(true); item->setFont(f);
        }
        return item;
    };

    const QColor rowEven("#080e1c"), rowOdd("#0b1220");

    for (int row = 0; row < machinesInfo.size(); ++row) {
        const MachineInfo &m = machinesInfo[row];
        machineTable->insertRow(row);

        QColor rowBg = (row % 2 == 0) ? rowEven : rowOdd;
        auto colorRow = [&](int c, QTableWidgetItem *it) {
            it->setBackground(rowBg); machineTable->setItem(row, c, it);
        };

        // Badge ID
        QTableWidgetItem *idItem = mkItem(m.idMachine, QColor("#fbbf24"), true);
        idItem->setBackground(QColor("#1c1200"));
        machineTable->setItem(row, 0, idItem);

        colorRow(1, mkItem(m.name, QColor("#f0f6fc"), true));
        colorRow(2, mkItem(m.productionLine, QColor("#94a3b8")));
        colorRow(3, mkItem(m.project, QColor("#cbd5e1")));

        // Boutons Action — icônes emoji seules, compactes
        QWidget *actionW = new QWidget(); actionW->setStyleSheet("background:transparent;");
        QHBoxLayout *actionL = new QHBoxLayout(actionW);
        actionL->setContentsMargins(2,2,2,2); actionL->setSpacing(6);
        actionL->setAlignment(Qt::AlignCenter);

        const int actionColW = machineTable->columnWidth(4);
        const int rowH       = machineTable->verticalHeader()->defaultSectionSize();
        int btnSize = qBound(16, qMin(actionColW/2 - 10, rowH - 14), 20);

        QPushButton *editBtn = new QPushButton("✎");
        QPushButton *delBtn  = new QPushButton("🗑");
        editBtn->setFixedSize(btnSize, btnSize);
        delBtn->setFixedSize(btnSize, btnSize);
        editBtn->setCursor(Qt::PointingHandCursor);
        delBtn->setCursor(Qt::PointingHandCursor);
        editBtn->setToolTip("Modifier");
        delBtn->setToolTip("Supprimer");
        QString emojiBtnStyle =
            "QPushButton{background:%1;color:#fff;border-radius:%2px;"
            "font-size:%3px;padding:0;border:none;}"
            "QPushButton:hover{background:%4;}"
            "QPushButton:pressed{background:%5;}";
        editBtn->setStyleSheet(emojiBtnStyle
            .arg("#1d4ed8").arg(btnSize/2).arg(qMax(8, btnSize-9))
            .arg("#2563eb").arg("#1e40af"));
        delBtn->setStyleSheet(emojiBtnStyle
            .arg("#991b1b").arg(btnSize/2).arg(qMax(8, btnSize-9))
            .arg("#dc2626").arg("#7f1d1d"));

        actionL->addWidget(editBtn); actionL->addWidget(delBtn);
        connect(editBtn, &QPushButton::clicked, this, [this, row]() { onEditMachine(row); });
        connect(delBtn,  &QPushButton::clicked, this, [this, row]() { onDeleteMachine(row); });
        machineTable->setCellWidget(row, 4, actionW);
    }
    machineTable->resizeColumnsToContents();
    machineTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

void MainWindow::refreshProductionTable() {
    auto hist = loadHistory();
    productionTable->setRowCount(0);
    productionTable->setColumnCount(11);
    productionTable->setHorizontalHeaderLabels({
        "📅 JOUR", "🕐 SHIFT", "🏭 MACHINE", "👤 OPÉRATEUR", "🏷 RÉFÉRENCE",
        "⚡ CIBLE/H", "📦 CIBLE/SHIFT", "✔ CONF.", "✘ NON-C.", "📈 PERFORMANCE", "⚙ ACTIONS"
    });

    productionTable->setShowGrid(false);
    productionTable->setAlternatingRowColors(false);
    productionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    productionTable->setFocusPolicy(Qt::NoFocus);
    productionTable->horizontalHeader()->setStyleSheet(
        "QHeaderView::section{"
        "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #1a1a2e,stop:1 #0d0d1a);"
        "  color:#b79dfc; font-size:10px; font-weight:800; letter-spacing:0.5px;"
        "  padding:6px 5px; border:none; border-right:1px solid #1e1e3a;"
        "  border-bottom:2px solid #7c3aed;}"
    );
    productionTable->setStyleSheet(
        "QTableWidget{background:#0a0a18;border:1px solid #201f3d;border-radius:14px;"
        "  gridline-color:transparent; font-size:11px;}"
        "QTableWidget::item{padding:4px 7px;border-bottom:1px solid #12122a;}"
        "QTableWidget::item:selected{background:#2a1f4a;color:white;}"
    );
    productionTable->verticalHeader()->setVisible(false);
    productionTable->verticalHeader()->setDefaultSectionSize(31);
    productionTable->horizontalHeader()->setHighlightSections(false);

    auto mkItem = [](const QString &txt, const QColor &col = QColor("#c9d1d9"),
                     bool bold = false, const QColor &bg = QColor(0,0,0,0)) {
        QTableWidgetItem *item = new QTableWidgetItem(txt);
        item->setForeground(col);
        item->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
        if (bold) { QFont f = item->font(); f.setBold(true); item->setFont(f); }
        if (bg.alpha() > 0) item->setBackground(bg);
        return item;
    };

    const QString shiftColors[] = {"#f59e0b","#22d3ee","#a78bfa"};
    const QColor rowEven("#080e1c"), rowOdd("#0b1220");

    for (int row = 0; row < hist.size(); ++row) {
        const InspectionSession &s = hist[row];
        productionTable->insertRow(row);
        QColor rowBg = (row % 2 == 0) ? rowEven : rowOdd;

        // Cible (pas la production réelle) : lue depuis product_config.csv via la référence
        ProductConfigEntry pcfg;
        int targetH = s.cadencePerHour;                     // valeur de repli
        if (lookupProduct(s.reference, pcfg) && pcfg.targetPerHour > 0)
            targetH = pcfg.targetPerHour;
        double prodH  = targetH;
        double prodSh = targetH * 7.5;

        auto setIt = [&](int c, QTableWidgetItem *it) {
            it->setBackground(rowBg);
            productionTable->setItem(row, c, it);
        };

        setIt(0, mkItem(s.startTime.isValid() ? s.startTime.toString("dd/MM/yyyy") : "—", QColor("#f0f6fc")));

        int shift = getShiftForTime(s.startTime);
        QTableWidgetItem *shiftItem = mkItem(QString("  Shift %1  ").arg(shift),
            QColor(shiftColors[qBound(0, shift-1, 2)]), true);
        setIt(1, shiftItem);

        setIt(2, mkItem(s.machine, QColor("#94a3b8")));

        QString opName = (s.nomOperateur + " " + s.prenomOperateur).trimmed();
        setIt(3, mkItem(opName.isEmpty() ? "—" : opName, QColor("#cbd5e1")));

        setIt(4, mkItem(s.reference, QColor("#f0f6fc"), true));
        setIt(5, mkItem(QString::number(qRound(prodH)) + " pcs/h", QColor("#2dd4bf"), true));
        setIt(6, mkItem(QString::number(qRound(prodSh)) + " pcs",  QColor("#2dd4bf")));
        productionTable->item(row,5)->setToolTip("Cible / heure (product_config.csv)");
        productionTable->item(row,6)->setToolTip("Cible / shift (7.5h) (product_config.csv)");

        // Badge conforme (vert)
        QTableWidgetItem *confIt = mkItem(QString("  %1  ").arg(s.conformes), QColor("#22c55e"), true, QColor("#0a200f"));
        productionTable->setItem(row, 7, confIt);

        // Badge non-conforme (rouge)
        QTableWidgetItem *ncIt = mkItem(QString("  %1  ").arg(s.nonConformes), QColor("#ef4444"), true, QColor("#200a0a"));
        productionTable->setItem(row, 8, ncIt);

        // Performance (%) = Production réelle / Cible × 100
        int actualProd = s.conformes + s.nonConformes;
        double perfPct = prodSh > 0 ? (actualProd / prodSh) * 100.0 : 0.0;
        QString perfCol = perfPct >= 100 ? "#4ade80" : (perfPct >= 70 ? "#fbbf24" : "#f87171");
        setIt(9, mkItem(QString::number(perfPct, 'f', 1) + " %", QColor(perfCol), true));

        // Boutons — icônes emoji seules, dimensionnées selon la colonne
        QWidget *actionW = new QWidget(); actionW->setStyleSheet("background:transparent;");
        QHBoxLayout *actionL = new QHBoxLayout(actionW);
        actionL->setContentsMargins(2,2,2,2); actionL->setSpacing(6);
        actionL->setAlignment(Qt::AlignCenter);

        const int actionColW = productionTable->columnWidth(10);
        const int rowH       = productionTable->verticalHeader()->defaultSectionSize();
        // Boutons carrés, plus petits, bornés par la largeur/hauteur disponibles de la cellule
        int btnSize = qBound(16, qMin(actionColW/2 - 10, rowH - 14), 20);

        QPushButton *editBtn = new QPushButton("✎");
        QPushButton *delBtn  = new QPushButton("🗑");
        editBtn->setFixedSize(btnSize, btnSize);
        delBtn->setFixedSize(btnSize, btnSize);
        editBtn->setCursor(Qt::PointingHandCursor);
        delBtn->setCursor(Qt::PointingHandCursor);
        editBtn->setToolTip("Modifier");
        delBtn->setToolTip("Supprimer");
        QString emojiBtnStyle =
            "QPushButton{background:%1;color:#fff;border-radius:%2px;"
            "font-size:%3px;padding:0;border:none;}"
            "QPushButton:hover{background:%4;}"
            "QPushButton:pressed{background:%5;}";
        editBtn->setStyleSheet(emojiBtnStyle
            .arg("#1d4ed8").arg(btnSize/2).arg(qMax(8, btnSize-9))
            .arg("#2563eb").arg("#1e40af"));
        delBtn->setStyleSheet(emojiBtnStyle
            .arg("#991b1b").arg(btnSize/2).arg(qMax(8, btnSize-9))
            .arg("#dc2626").arg("#7f1d1d"));

        actionL->addWidget(editBtn); actionL->addWidget(delBtn);
        connect(editBtn, &QPushButton::clicked, this, [this, row]() { onEditSessionReference(row); });
        connect(delBtn,  &QPushButton::clicked, this, [this, row]() { onDeleteSession(row); });
        productionTable->setCellWidget(row, 10, actionW);
    }
    productionTable->resizeColumnsToContents();
    productionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

void MainWindow::refreshBreakdownTable() {
    auto hist = loadHistory();
    breakdownTable->setRowCount(0);
    breakdownTable->setColumnCount(9);
    breakdownTable->setHorizontalHeaderLabels({
        "📅 DATE", "🕐 SHIFT", "🏭 MACHINE", "🛑 NOMBRE D'ARRÊT", "⏱ DURÉE D'ARRÊT",
        "▶ DÉBUT ARRÊT", "⏹ FIN ARRÊT", "❗ CAUSE D'ARRÊT", "⚙ ACTIONS"
    });

    breakdownTable->setShowGrid(false);
    breakdownTable->setAlternatingRowColors(false);
    breakdownTable->setSelectionMode(QAbstractItemView::SingleSelection);
    breakdownTable->setFocusPolicy(Qt::NoFocus);
    breakdownTable->horizontalHeader()->setStyleSheet(
        "QHeaderView::section{"
        "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #3a1414,stop:1 #1a0d0d);"
        "  color:#f87171; font-size:10px; font-weight:800; letter-spacing:0.5px;"
        "  padding:6px 7px; border:none; border-right:1px solid #2a1414;"
        "  border-bottom:2px solid #f43f5e;}"
    );
    breakdownTable->setStyleSheet(
        "QTableWidget{background:#0a1120;border:1px solid #2a1414;border-radius:14px;"
        "  gridline-color:transparent; font-size:11px;}"
        "QTableWidget::item{padding:4px 9px;border-bottom:1px solid #101b30;}"
        "QTableWidget::item:selected{background:#3a1414;color:white;}"
    );
    breakdownTable->verticalHeader()->setVisible(false);
    breakdownTable->verticalHeader()->setDefaultSectionSize(31);
    breakdownTable->horizontalHeader()->setMinimumSectionSize(84);
    breakdownTable->horizontalHeader()->setHighlightSections(false);

    auto mkItem = [](const QString &txt, const QColor &col = QColor("#c9d1d9"),
                     bool bold = false) {
        QTableWidgetItem *item = new QTableWidgetItem(txt);
        item->setForeground(col);
        item->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
        if (bold) { QFont f = item->font(); f.setBold(true); item->setFont(f); }
        return item;
    };

    const QColor rowEven("#080e1c"), rowOdd("#0b1220");
    const QString shiftColors[] = {"#f59e0b","#22d3ee","#a78bfa"};

    // ── Regrouper par MACHINE + JOUR + SHIFT ──
    // Ex : si M01 a fait 2 arrêts dans le même shift le même jour (répartis sur
    // plusieurs sessions), ils doivent se cumuler en une seule ligne avec
    // NOMBRE D'ARRÊT = 2, au lieu d'apparaître sur deux lignes séparées.
    struct ArretAgg {
        QString machine;
        QString dateStr;
        int     shift = 1;
        int     nbArret = 0;
        qint64  totalDurationSecs = 0;
        QStringList starts;
        QStringList ends;
        QStringList causes;
        QVector<int> sessionIndices;
    };
    QVector<ArretAgg> agg;
    QMap<QString,int> keyToIdx;

    for (int hi = 0; hi < hist.size(); ++hi) {
        const InspectionSession &s = hist[hi];
        if (s.downtimes.isEmpty()) continue;
        QString dateStr = s.startTime.isValid() ? s.startTime.toString("dd/MM/yyyy") : "—";
        int shift = getShiftForTime(s.startTime);
        QString key = s.machine + "|" + dateStr + "|" + QString::number(shift);

        int idx;
        auto it = keyToIdx.find(key);
        if (it != keyToIdx.end()) {
            idx = it.value();
        } else {
            ArretAgg a; a.machine = s.machine; a.dateStr = dateStr; a.shift = shift;
            agg.append(a);
            idx = agg.size() - 1;
            keyToIdx.insert(key, idx);
        }

        agg[idx].nbArret += s.downtimes.size();
        agg[idx].sessionIndices.append(hi);
        for (const auto &d : s.downtimes) {
            agg[idx].totalDurationSecs += d.durationSeconds;
            if (d.startDowntime.isValid()) agg[idx].starts << d.startDowntime.toString("HH:mm");
            agg[idx].ends << (d.endDowntime.isValid() ? d.endDowntime.toString("HH:mm") : "En cours");
            if (!d.causeArret.isEmpty() && !agg[idx].causes.contains(d.causeArret))
                agg[idx].causes << d.causeArret;
        }
    }

    breakdownGroupSessions.clear();

    for (int row = 0; row < agg.size(); ++row) {
        const ArretAgg &a = agg[row];
        breakdownTable->insertRow(row);
        QColor rowBg = (row % 2 == 0) ? rowEven : rowOdd;
        auto colorRow = [&](int c, QTableWidgetItem *it) {
            it->setBackground(rowBg); breakdownTable->setItem(row, c, it);
        };

        colorRow(0, mkItem(a.dateStr, QColor("#94a3b8")));

        QString shiftCol = shiftColors[qBound(0, a.shift-1, 2)];
        QTableWidgetItem *shiftItem = mkItem(QString("  Shift %1  ").arg(a.shift), QColor(shiftCol), true);
        shiftItem->setBackground(rowBg);
        breakdownTable->setItem(row, 1, shiftItem);

        colorRow(2, mkItem(a.machine, QColor("#f0f6fc"), true));

        QColor nbCol = a.nbArret > 0 ? QColor("#f43f5e") : QColor("#4b5563");
        colorRow(3, mkItem(QString::number(a.nbArret), nbCol, true));

        colorRow(4, mkItem(a.totalDurationSecs > 0 ? formatDur(a.totalDurationSecs) : "—",
            a.totalDurationSecs > 0 ? QColor("#f43f5e") : QColor("#4b5563"), a.totalDurationSecs > 0));

        colorRow(5, mkItem(a.starts.isEmpty() ? "—" : a.starts.join(", "), QColor("#94a3b8")));
        colorRow(6, mkItem(a.ends.isEmpty()   ? "—" : a.ends.join(", "),   QColor("#94a3b8")));

        QString causeStr = a.causes.isEmpty() ? "—" : a.causes.join(" / ");
        colorRow(7, mkItem(causeStr, a.causes.isEmpty() ? QColor("#4b5563") : QColor("#fbbf24")));

        // Boutons Action — Modifier (cause) / Supprimer (ce groupe d'arrêts)
        QWidget *actionW = new QWidget(); actionW->setStyleSheet("background:transparent;");
        QHBoxLayout *actionL = new QHBoxLayout(actionW);
        actionL->setContentsMargins(2,2,2,2); actionL->setSpacing(6);
        actionL->setAlignment(Qt::AlignCenter);

        const int actionColW = breakdownTable->columnWidth(8);
        const int rowH       = breakdownTable->verticalHeader()->defaultSectionSize();
        int btnSize = qBound(16, qMin(actionColW/2 - 10, rowH - 14), 20);

        QPushButton *editBtn = new QPushButton("✎");
        QPushButton *delBtn  = new QPushButton("🗑");
        editBtn->setFixedSize(btnSize, btnSize);
        delBtn->setFixedSize(btnSize, btnSize);
        editBtn->setCursor(Qt::PointingHandCursor);
        delBtn->setCursor(Qt::PointingHandCursor);
        editBtn->setToolTip("Modifier la cause d'arrêt");
        delBtn->setToolTip("Supprimer ces arrêts");
        QString emojiBtnStyle =
            "QPushButton{background:%1;color:#fff;border-radius:%2px;"
            "font-size:%3px;padding:0;border:none;}"
            "QPushButton:hover{background:%4;}"
            "QPushButton:pressed{background:%5;}";
        editBtn->setStyleSheet(emojiBtnStyle
            .arg("#1d4ed8").arg(btnSize/2).arg(qMax(8, btnSize-9))
            .arg("#2563eb").arg("#1e40af"));
        delBtn->setStyleSheet(emojiBtnStyle
            .arg("#991b1b").arg(btnSize/2).arg(qMax(8, btnSize-9))
            .arg("#dc2626").arg("#7f1d1d"));

        actionL->addWidget(editBtn); actionL->addWidget(delBtn);
        connect(editBtn, &QPushButton::clicked, this, [this, row]() { onEditBreakdownGroup(row); });
        connect(delBtn,  &QPushButton::clicked, this, [this, row]() { onDeleteBreakdownGroup(row); });
        breakdownTable->setCellWidget(row, 8, actionW);

        breakdownGroupSessions.append(a.sessionIndices);
    }
    breakdownTable->resizeColumnsToContents();
    breakdownTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

void MainWindow::exportTableToCsv(QTableWidget *table, const QString &defaultBaseName) {
    if (!table) return;

    QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
                             .filePath(defaultBaseName);
    QString destPath = QFileDialog::getSaveFileName(
        this, "Exporter le tableau (CSV)", suggested, "Fichier CSV (*.csv)");
    if (destPath.isEmpty()) return;

    QFile f(destPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Erreur", "Impossible d'écrire le fichier à cet emplacement.");
        return;
    }

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << "\xEF\xBB\xBF"; // BOM UTF-8

    auto esc = [](QString v) {
        v.replace("\"", "\"\"");
        return "\"" + v + "\"";
    };

    // En-têtes (on retire les emojis / espaces superflus pour un CSV propre)
    QStringList headers;
    for (int c = 0; c < table->columnCount(); ++c) {
        QTableWidgetItem *hIt = table->horizontalHeaderItem(c);
        QString h = hIt ? hIt->text() : QString("Colonne %1").arg(c + 1);
        headers << esc(h.trimmed());
    }
    out << headers.join(";") << "\n";

    for (int r = 0; r < table->rowCount(); ++r) {
        QStringList vals;
        for (int c = 0; c < table->columnCount(); ++c) {
            QTableWidgetItem *it = table->item(r, c);
            vals << esc(it ? it->text().trimmed() : "");
        }
        out << vals.join(";") << "\n";
    }
    f.close();

    QMessageBox::information(this, "✔  Export réussi",
        QString("Le tableau a été exporté avec succès :\n%1").arg(destPath));
}

void MainWindow::onDeleteSession(int row) {
    auto hist = loadHistory();
    if (row < 0 || row >= hist.size()) return;
    if (QMessageBox::question(this, "Supprimer",
            QString("Supprimer la session « %1 » ?").arg(hist[row].reference),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;
    hist.removeAt(row);
    saveAllHistory(hist);
    rebuildCsvFromHistory();
    refreshProductionTable();
    refreshBreakdownTable();
}

void MainWindow::onEditSessionReference(int row) {
    auto hist = loadHistory();
    if (row < 0 || row >= hist.size()) return;
    InspectionSession &sess = hist[row];

    // ── Créer le dialog de modification ──
    QDialog dlg(this);
    dlg.setWindowTitle(QString("Modifier Session #%1 — LEAR Corporation").arg(row + 1));
    dlg.setFixedWidth(370);
    dlg.setStyleSheet(
        "QDialog{background:#0a0f1c;}"
        "QLabel#sectionTitle{color:#f0f6fc;font-size:12.5px;font-weight:800;}"
        "QLabel#fld{color:#8391a8;font-size:9.5px;font-weight:700;letter-spacing:0.7px;}"
        "QLineEdit,QComboBox,QSpinBox{"
        "  background:#0a1120;color:#f0f6fc;border:1px solid #223252;"
        "  border-radius:10px;padding:7px 12px;font-size:12.5px;min-height:24px;}"
        "QLineEdit:focus,QComboBox:focus,QSpinBox:focus{border:1px solid #3b82f6;background:#0d1830;}"
        "QComboBox::drop-down{border:none;width:22px;}"
        "QComboBox QAbstractItemView{background:#0f1624;color:#f0f6fc;border:1px solid #1e2d4a;"
        "  selection-background-color:#1d4ed8;}"
    );

    QVBoxLayout *outerVl = new QVBoxLayout(&dlg);
    outerVl->setContentsMargins(0,0,0,0); outerVl->setSpacing(0);
    QFrame *editTopBar = new QFrame(); editTopBar->setFixedHeight(4);
    editTopBar->setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
                               "stop:0 #7a0d0d, stop:0.5 #e02020, stop:1 #7a0d0d);");
    outerVl->addWidget(editTopBar);

    QVBoxLayout *vl = new QVBoxLayout();
    vl->setContentsMargins(18,14,18,14); vl->setSpacing(10);
    outerVl->addLayout(vl);

    QHBoxLayout *editHdr = new QHBoxLayout(); editHdr->setSpacing(10);
    QLabel *editIcon = new QLabel("✎"); editIcon->setFixedSize(32,32);
    editIcon->setAlignment(Qt::AlignCenter);
    editIcon->setStyleSheet("font-size:14px;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                             "stop:0 #2563eb,stop:1 #1d4ed8);border-radius:16px;color:#fff;");
    QVBoxLayout *editTCol = new QVBoxLayout(); editTCol->setSpacing(0);
    QLabel *editTitle = new QLabel(QString("Modifier Session #%1").arg(row + 1));
    editTitle->setStyleSheet("color:#f0f6fc;font-size:16px;font-weight:800;");
    QLabel *editSub = new QLabel("Corriger les informations de cette session");
    editSub->setStyleSheet("color:#8391a8;font-size:11px;font-weight:500;");
    editTCol->addWidget(editTitle); editTCol->addWidget(editSub);
    editHdr->addWidget(editIcon); editHdr->addLayout(editTCol); editHdr->addStretch();
    vl->addLayout(editHdr);

    // Cartouche par section : panneau arrondi + liseré coloré à gauche
    auto beginSection = [&](const QString &emoji, const QString &title, const QString &accent) -> QVBoxLayout* {
        QFrame *panel = new QFrame();
        panel->setStyleSheet(QString(
            "QFrame{background:#0e1526;border-radius:13px;border-left:3px solid %1;}").arg(accent));
        QVBoxLayout *inner = new QVBoxLayout(panel);
        inner->setContentsMargins(12,10,12,11); inner->setSpacing(7);

        QHBoxLayout *tRow = new QHBoxLayout(); tRow->setSpacing(8);
        QLabel *ic = new QLabel(emoji); ic->setStyleSheet("font-size:13px;background:transparent;");
        QLabel *t = new QLabel(title); t->setObjectName("sectionTitle");
        tRow->addWidget(ic); tRow->addWidget(t); tRow->addStretch();
        inner->addLayout(tRow);

        vl->addWidget(panel);
        return inner;
    };
    auto addFieldCol = [](const QString &lbl, QWidget *w) {
        QVBoxLayout *c = new QVBoxLayout(); c->setSpacing(3);
        QLabel *l = new QLabel(lbl); l->setObjectName("fld");
        c->addWidget(l); c->addWidget(w);
        return c;
    };

    // ── Section Référence / Machine ──
    QVBoxLayout *sec1 = beginSection("📦", "Référence & Machine", "#f59e0b");

    QComboBox *refEdit = new QComboBox();
    QStringList refs;
    for (auto it = products.constBegin(); it != products.constEnd(); ++it)
        refs << it.value().partNumber;
    refs.sort(Qt::CaseInsensitive);
    for (const QString &r : refs) refEdit->addItem(r);
    refEdit->setCurrentText(sess.reference);

    QComboBox *machEdit = new QComboBox();
    for (const QString &m : MACHINES) machEdit->addItem(m);
    machEdit->setCurrentText(sess.machine);
    QHBoxLayout *row1 = new QHBoxLayout(); row1->setSpacing(10);
    row1->addLayout(addFieldCol("RÉFÉRENCE", refEdit), 1);
    row1->addLayout(addFieldCol("MACHINE", machEdit), 1);
    sec1->addLayout(row1);

    // ── Section Opérateur ──
    QVBoxLayout *sec2 = beginSection("👤", "Opérateur", "#a78bfa");
    QLineEdit *nomEdit    = new QLineEdit(sess.nomOperateur);
    QLineEdit *prenomEdit = new QLineEdit(sess.prenomOperateur);
    QLineEdit *idOpEdit   = new QLineEdit(sess.idOperateur);
    QHBoxLayout *row2 = new QHBoxLayout(); row2->setSpacing(10);
    row2->addLayout(addFieldCol("NOM", nomEdit), 1);
    row2->addLayout(addFieldCol("PRÉNOM", prenomEdit), 1);
    sec2->addLayout(row2);
    sec2->addLayout(addFieldCol("ID OPÉRATEUR", idOpEdit));

    // ── Section Production ──
    QVBoxLayout *sec3 = beginSection("📊", "Production", "#4ade80");
    QSpinBox *confEdit = new QSpinBox(); confEdit->setRange(0, 99999);
    confEdit->setValue(sess.conformes);
    QSpinBox *ncEdit   = new QSpinBox(); ncEdit->setRange(0, 99999);
    ncEdit->setValue(sess.nonConformes);
    QHBoxLayout *row3 = new QHBoxLayout(); row3->setSpacing(10);
    row3->addLayout(addFieldCol("CONFORMES", confEdit), 1);
    row3->addLayout(addFieldCol("NON-CONFORMES", ncEdit), 1);
    sec3->addLayout(row3);

    // ── Boutons ──
    vl->addSpacing(8);
    QHBoxLayout *br = new QHBoxLayout(); br->setSpacing(10);

    QPushButton *cancelBtn = new QPushButton("✕  Annuler");
    cancelBtn->setFixedHeight(34);
    cancelBtn->setStyleSheet(
        "QPushButton{background:#1f2937;color:#9ca3af;border:1px solid #374151;"
        "border-radius:8px;font-size:12px;font-weight:700;}"
        "QPushButton:hover{background:#374151;color:#f0f6fc;}");
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    QPushButton *saveBtn = new QPushButton("✔  Enregistrer");
    saveBtn->setFixedHeight(34);
    saveBtn->setStyleSheet(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #059669,stop:1 #047857);"
        "color:white;border:none;border-radius:8px;font-size:12px;font-weight:800;}"
        "QPushButton:hover{background:#10b981;}"
        "QPushButton:pressed{background:#065f46;}");
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    br->addWidget(cancelBtn, 1); br->addWidget(saveBtn, 1);
    vl->addLayout(br);

    QGraphicsDropShadowEffect *editSh = new QGraphicsDropShadowEffect(&dlg);
    editSh->setBlurRadius(45); editSh->setOffset(0,5); editSh->setColor(QColor(0,0,0,110));
    dlg.setGraphicsEffect(editSh);

    if (dlg.exec() != QDialog::Accepted) return;

    // ── Appliquer les modifications ──
    const QString newRef = refEdit->currentText().trimmed();
    ProductConfigEntry prod;
    if (lookupProduct(newRef, prod)) {
        hist[row].reference      = prod.partNumber;
        hist[row].cadencePerHour = prod.targetPerHour;
    }
    hist[row].machine        = machEdit->currentText();
    hist[row].nomOperateur   = nomEdit->text().trimmed();
    hist[row].prenomOperateur= prenomEdit->text().trimmed();
    hist[row].idOperateur    = idOpEdit->text().trimmed();
    hist[row].conformes      = confEdit->value();
    hist[row].nonConformes   = ncEdit->value();

    saveAllHistory(hist);
    rebuildCsvFromHistory();
    refreshProductionTable();
    refreshBreakdownTable();

    QMessageBox::information(this, "✔  Modifié",
        QString("Session #%1 mise à jour avec succès.").arg(row + 1));
}

// ═══════════════════════════════════════════════════════════════
//  Tableau Machine — CRUD sur machine_info.json
// ═══════════════════════════════════════════════════════════════
static void execMachineFormDialog(QWidget *parent, const QString &title,
                                   const QString &subtitle, const QString &icon,
                                   MachineInfo &m, bool &accepted) {
    accepted = false;
    QDialog dlg(parent);
    dlg.setWindowTitle(title + " — LEAR Corporation");
    dlg.setFixedWidth(370);
    dlg.setStyleSheet(
        "QDialog{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #0b1120,stop:1 #080c16);}"
        "QLabel#fld{color:#7c8aa5;font-size:9.5px;font-weight:800;letter-spacing:0.8px;}"
        "QLineEdit{background:#0a1120;color:#f0f6fc;border:1.5px solid #1c2b48;"
        "  border-radius:11px;padding:9px 14px;font-size:13px;min-height:24px;}"
        "QLineEdit:hover{border:1.5px solid #2a3f66;}"
        "QLineEdit:focus{border:1.5px solid #3b82f6;background:#0d1830;}"
    );

    QVBoxLayout *outerVl = new QVBoxLayout(&dlg);
    outerVl->setContentsMargins(0,0,0,0); outerVl->setSpacing(0);
    QFrame *topBar = new QFrame(); topBar->setFixedHeight(4);
    topBar->setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
                           "stop:0 #7a0d0d, stop:0.5 #e02020, stop:1 #7a0d0d);");
    outerVl->addWidget(topBar);

    QVBoxLayout *vl = new QVBoxLayout();
    vl->setContentsMargins(20,16,20,16); vl->setSpacing(10);
    outerVl->addLayout(vl);

    // ── En-tête : badge icône + titre + sous-titre ──
    QHBoxLayout *headRow = new QHBoxLayout(); headRow->setSpacing(10);
    QLabel *badge = new QLabel(icon);
    badge->setFixedSize(38, 38);
    badge->setAlignment(Qt::AlignCenter);
    badge->setStyleSheet(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #1d4ed8,stop:1 #1e3a8a);"
        "border-radius:19px; font-size:16px;");
    headRow->addWidget(badge);

    QVBoxLayout *headTxt = new QVBoxLayout(); headTxt->setSpacing(2);
    QLabel *hTitle = new QLabel(title);
    hTitle->setStyleSheet("color:#f0f6fc;font-size:14.5px;font-weight:800;");
    QLabel *hSub = new QLabel(subtitle);
    hSub->setStyleSheet("color:#64748b;font-size:10px;font-weight:600;");
    hSub->setWordWrap(true);
    headTxt->addWidget(hTitle); headTxt->addWidget(hSub);
    headRow->addLayout(headTxt, 1);
    vl->addLayout(headRow);

    QFrame *sep = new QFrame(); sep->setFixedHeight(1);
    sep->setStyleSheet("background:#1a2540;");
    vl->addWidget(sep);

    // ── Carte des champs ──
    QFrame *card = new QFrame();
    card->setStyleSheet("QFrame{background:#0d1424;border:1px solid #16213c;border-radius:14px;}");
    QVBoxLayout *cardL = new QVBoxLayout(card);
    cardL->setContentsMargins(13,13,13,13); cardL->setSpacing(9);

    auto addFieldCol = [&](const QString &emoji, const QString &lbl, QLineEdit *w) {
        QVBoxLayout *c = new QVBoxLayout(); c->setSpacing(4);
        QLabel *l = new QLabel(emoji + "  " + lbl); l->setObjectName("fld");
        c->addWidget(l); c->addWidget(w);
        cardL->addLayout(c);
    };

    QLineEdit *idEdit   = new QLineEdit(m.idMachine);
    QLineEdit *nameEdit = new QLineEdit(m.name);
    QLineEdit *lineEdit = new QLineEdit(m.productionLine);
    QLineEdit *projEdit = new QLineEdit(m.project);
    idEdit->setPlaceholderText("ex : M01");
    nameEdit->setPlaceholderText("ex : Presse à injecter n°1");
    lineEdit->setPlaceholderText("ex : Ligne 2");
    projEdit->setPlaceholderText("ex : Wear 2002073");
    addFieldCol("🆔", "ID", idEdit);
    addFieldCol("🏭", "NAME", nameEdit);
    addFieldCol("📍", "PRODUCTION LINE", lineEdit);
    addFieldCol("📦", "PROJET", projEdit);
    vl->addWidget(card);

    QHBoxLayout *br = new QHBoxLayout(); br->setSpacing(10);
    QPushButton *cancelBtn = new QPushButton("✕  Annuler");
    cancelBtn->setFixedHeight(38);
    cancelBtn->setStyleSheet(
        "QPushButton{background:#161f33;color:#9ca3af;border:1.5px solid #2a3856;"
        "border-radius:10px;font-size:12.5px;font-weight:700;}"
        "QPushButton:hover{background:#1f2b47;color:#f0f6fc;}");
    cancelBtn->setCursor(Qt::PointingHandCursor);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    QPushButton *saveBtn = new QPushButton("✔  Enregistrer");
    saveBtn->setFixedHeight(38);
    saveBtn->setStyleSheet(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #059669,stop:1 #047857);"
        "color:white;border:none;border-radius:10px;font-size:12.5px;font-weight:800;}"
        "QPushButton:hover{background:#10b981;}"
        "QPushButton:pressed{background:#065f46;}");
    saveBtn->setCursor(Qt::PointingHandCursor);
    QObject::connect(saveBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    br->addWidget(cancelBtn,1); br->addWidget(saveBtn,1);
    vl->addLayout(br);

    QGraphicsDropShadowEffect *sh = new QGraphicsDropShadowEffect(&dlg);
    sh->setBlurRadius(55); sh->setOffset(0,8); sh->setColor(QColor(0,0,0,140));
    dlg.setGraphicsEffect(sh);

    if (dlg.exec() != QDialog::Accepted) return;

    m.idMachine      = idEdit->text().trimmed();
    m.name           = nameEdit->text().trimmed();
    m.productionLine = lineEdit->text().trimmed();
    m.project        = projEdit->text().trimmed();
    accepted = true;
}

void MainWindow::onAddMachine() {
    MachineInfo m;
    bool ok = false;
    execMachineFormDialog(this, "Ajouter une Machine",
        "Renseignez les informations de la nouvelle machine.", "➕", m, ok);
    if (!ok) return;
    if (m.idMachine.isEmpty() || m.name.isEmpty()) {
        QMessageBox::warning(this, "Champs requis", "L'ID et le NAME de la machine sont requis.");
        return;
    }
    machinesInfo.append(m);
    saveMachineInfo();
    refreshMachineTable();
    QMessageBox::information(this, "✔  Ajoutée",
        QString("Machine « %1 » ajoutée avec succès.").arg(m.name));
}

void MainWindow::onEditMachine(int row) {
    if (row < 0 || row >= machinesInfo.size()) return;
    MachineInfo m = machinesInfo[row];
    bool ok = false;
    execMachineFormDialog(this, QString("Modifier « %1 »").arg(m.name),
        "Mettez à jour les informations de cette machine.", "✎", m, ok);
    if (!ok) return;
    machinesInfo[row] = m;
    saveMachineInfo();
    refreshMachineTable();
    QMessageBox::information(this, "✔  Modifiée",
        QString("Machine « %1 » mise à jour avec succès.").arg(m.name));
}

void MainWindow::onDeleteMachine(int row) {
    if (row < 0 || row >= machinesInfo.size()) return;
    const MachineInfo &m = machinesInfo[row];
    if (QMessageBox::question(this, "Supprimer",
            QString("Supprimer la machine « %1 » ?").arg(m.name),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;
    machinesInfo.removeAt(row);
    saveMachineInfo();
    refreshMachineTable();
}

// ═══════════════════════════════════════════════════════════════
//  Tableau Breakdowns — actions sur un groupe (machine+jour+shift)
// ═══════════════════════════════════════════════════════════════
// ── Dialog stylé pour modifier la cause d'arrêt d'un groupe Breakdown ──
static void execEditCauseDialog(QWidget *parent, const QString &machine,
                                 const QString &dateStr, int shift, int nbArret,
                                 const QString &currentCause,
                                 QString &newCause, bool &accepted) {
    accepted = false;
    QDialog dlg(parent);
    dlg.setWindowTitle("Modifier la cause d'arrêt — LEAR Corporation");
    dlg.setFixedWidth(380);
    dlg.setStyleSheet(
        "QDialog{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #0b1120,stop:1 #080c16);}"
        "QLabel#fld{color:#7c8aa5;font-size:9.5px;font-weight:800;letter-spacing:0.8px;}"
        "QLineEdit{background:#0a1120;color:#f0f6fc;border:1.5px solid #3a1414;"
        "  border-radius:11px;padding:9px 14px;font-size:13px;min-height:24px;}"
        "QLineEdit:hover{border:1.5px solid #5c1a1a;}"
        "QLineEdit:focus{border:1.5px solid #f43f5e;background:#170d0d;}"
    );

    QVBoxLayout *outerVl = new QVBoxLayout(&dlg);
    outerVl->setContentsMargins(0,0,0,0); outerVl->setSpacing(0);
    QFrame *topBar = new QFrame(); topBar->setFixedHeight(4);
    topBar->setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
                           "stop:0 #7a0d0d, stop:0.5 #f43f5e, stop:1 #7a0d0d);");
    outerVl->addWidget(topBar);

    QVBoxLayout *vl = new QVBoxLayout();
    vl->setContentsMargins(20,16,20,16); vl->setSpacing(10);
    outerVl->addLayout(vl);

    // ── En-tête : badge + titre + sous-titre ──
    QHBoxLayout *headRow = new QHBoxLayout(); headRow->setSpacing(10);
    QLabel *badge = new QLabel("🛑");
    badge->setFixedSize(38, 38);
    badge->setAlignment(Qt::AlignCenter);
    badge->setStyleSheet(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #991b1b,stop:1 #7f1d1d);"
        "border-radius:19px; font-size:16px;");
    headRow->addWidget(badge);

    QVBoxLayout *headTxt = new QVBoxLayout(); headTxt->setSpacing(2);
    QLabel *hTitle = new QLabel("Modifier la cause d'arrêt");
    hTitle->setStyleSheet("color:#f0f6fc;font-size:14.5px;font-weight:800;");
    QLabel *hSub = new QLabel("Appliquée à tous les arrêts du groupe.");
    hSub->setStyleSheet("color:#64748b;font-size:10px;font-weight:600;");
    hSub->setWordWrap(true);
    headTxt->addWidget(hTitle); headTxt->addWidget(hSub);
    headRow->addLayout(headTxt, 1);
    vl->addLayout(headRow);

    QFrame *sep = new QFrame(); sep->setFixedHeight(1);
    sep->setStyleSheet("background:#1a2540;");
    vl->addWidget(sep);

    // ── Carte de contexte (résumé du groupe) ──
    QFrame *ctxCard = new QFrame();
    ctxCard->setStyleSheet("QFrame{background:#170d0d;border:1px solid #3a1414;border-radius:12px;}");
    QGridLayout *ctxL = new QGridLayout(ctxCard);
    ctxL->setContentsMargins(12,9,12,9); ctxL->setHorizontalSpacing(14); ctxL->setVerticalSpacing(3);
    auto ctxItem = [&](int col, const QString &lbl, const QString &val, const QString &col2) {
        QLabel *l = new QLabel(lbl); l->setStyleSheet("color:#9ca3af;font-size:9px;font-weight:700;letter-spacing:0.5px;");
        QLabel *v = new QLabel(val); v->setStyleSheet(QString("color:%1;font-size:13px;font-weight:800;").arg(col2));
        ctxL->addWidget(l, 0, col); ctxL->addWidget(v, 1, col);
    };
    ctxItem(0, "🏭 MACHINE", machine, "#f0f6fc");
    ctxItem(1, "📅 DATE", dateStr, "#94a3b8");
    ctxItem(2, "🕐 SHIFT", QString("Shift %1").arg(shift), "#22d3ee");
    ctxItem(3, "🛑 NB. ARRÊT", QString::number(nbArret), "#f43f5e");
    vl->addWidget(ctxCard);

    // ── Champ cause d'arrêt ──
    QVBoxLayout *fieldCol = new QVBoxLayout(); fieldCol->setSpacing(4);
    QLabel *fLbl = new QLabel("❗  CAUSE D'ARRÊT"); fLbl->setObjectName("fld");
    QLineEdit *causeEdit = new QLineEdit(currentCause);
    causeEdit->setPlaceholderText("ex : Panne machine");
    fieldCol->addWidget(fLbl); fieldCol->addWidget(causeEdit);
    vl->addLayout(fieldCol);

    QHBoxLayout *br = new QHBoxLayout(); br->setSpacing(10);
    QPushButton *cancelBtn = new QPushButton("✕  Annuler");
    cancelBtn->setFixedHeight(38);
    cancelBtn->setStyleSheet(
        "QPushButton{background:#161f33;color:#9ca3af;border:1.5px solid #2a3856;"
        "border-radius:10px;font-size:12.5px;font-weight:700;}"
        "QPushButton:hover{background:#1f2b47;color:#f0f6fc;}");
    cancelBtn->setCursor(Qt::PointingHandCursor);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    QPushButton *saveBtn = new QPushButton("✔  Enregistrer");
    saveBtn->setFixedHeight(38);
    saveBtn->setStyleSheet(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #dc2626,stop:1 #991b1b);"
        "color:white;border:none;border-radius:10px;font-size:12.5px;font-weight:800;}"
        "QPushButton:hover{background:#ef4444;}"
        "QPushButton:pressed{background:#7f1d1d;}");
    saveBtn->setCursor(Qt::PointingHandCursor);
    QObject::connect(saveBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    br->addWidget(cancelBtn,1); br->addWidget(saveBtn,1);
    vl->addLayout(br);

    QGraphicsDropShadowEffect *sh = new QGraphicsDropShadowEffect(&dlg);
    sh->setBlurRadius(55); sh->setOffset(0,8); sh->setColor(QColor(0,0,0,140));
    dlg.setGraphicsEffect(sh);

    if (dlg.exec() != QDialog::Accepted) return;
    newCause = causeEdit->text().trimmed();
    accepted = true;
}

void MainWindow::onEditBreakdownGroup(int row) {
    if (row < 0 || row >= breakdownGroupSessions.size()) return;
    const QVector<int> &idxs = breakdownGroupSessions[row];
    if (idxs.isEmpty()) return;

    auto hist = loadHistory();
    QString machine, dateStr; int shift = 1, nbArret = 0;
    QStringList existingCauses;
    for (int hi : idxs) {
        if (hi < 0 || hi >= hist.size()) continue;
        const InspectionSession &s = hist[hi];
        if (machine.isEmpty()) {
            machine = s.machine;
            dateStr = s.startTime.isValid() ? s.startTime.toString("dd/MM/yyyy") : "—";
            shift = getShiftForTime(s.startTime);
        }
        nbArret += s.downtimes.size();
        for (const auto &d : s.downtimes)
            if (!d.causeArret.isEmpty() && !existingCauses.contains(d.causeArret))
                existingCauses << d.causeArret;
    }

    QString newCause; bool ok = false;
    execEditCauseDialog(this, machine, dateStr, shift, nbArret,
                         existingCauses.join(" / "), newCause, ok);
    if (!ok || newCause.isEmpty()) return;

    for (int hi : idxs) {
        if (hi < 0 || hi >= hist.size()) continue;
        for (DowntimeRecord &d : hist[hi].downtimes)
            d.causeArret = newCause;
    }
    saveAllHistory(hist);
    rebuildCsvFromHistory();
    refreshBreakdownTable();
    QMessageBox::information(this, "✔  Modifié", "Cause d'arrêt mise à jour avec succès.");
}

void MainWindow::onDeleteBreakdownGroup(int row) {
    if (row < 0 || row >= breakdownGroupSessions.size()) return;
    const QVector<int> &idxs = breakdownGroupSessions[row];
    if (idxs.isEmpty()) return;

    if (QMessageBox::question(this, "Supprimer",
            "Supprimer tous les arrêts de ce groupe (machine / jour / shift) ?",
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    auto hist = loadHistory();
    for (int hi : idxs) {
        if (hi < 0 || hi >= hist.size()) continue;
        hist[hi].downtimes.clear();
    }
    saveAllHistory(hist);
    rebuildCsvFromHistory();
    refreshBreakdownTable();
    refreshProductionTable();
}

// ═══════════════════════════════════════════════════════════════
//  Historique CSV auto-synchro
// ═══════════════════════════════════════════════════════════════

QString MainWindow::histCsvPath() const {
    // Dossier data/ à côté de l'exécutable
    return dataDirPath() + "/historique.csv";
}

static QString csvEscape(const QString &s) {
    QString r = s;
    r.replace("\"", "\"\"");
    if (r.contains(',') || r.contains('"') || r.contains('\n'))
        r = "\"" + r + "\"";
    return r;
}

void MainWindow::appendSessionToCsv(const InspectionSession &s, int sessionNumber) {
    QString path = histCsvPath();
    bool needHeader = !QFile::exists(path);

    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);

    if (needHeader) {
        // BOM + en-tête — fusion des colonnes du tableau Référence et du tableau Opérateur
        out << "\xEF\xBB\xBF";
        out << "N_SESSION,REFERENCE,ID_OPERATEUR,NOM_OPERATEUR,PRENOM_OPERATEUR,"
               "NUMERO_MACHINE,SHIFT,CIBLE_PCS_HEURE,CIBLE_PCS_SHIFT,"
               "CONFORMES,NON_CONFORMES,TEMPS_ARRET,DEBUT_ARRET,FIN_ARRET,CAUSE_ARRET\n";
    }

    // Numéro de session = position réelle de la session dans l'historique (1, 2, 3…)
    int nSess = sessionNumber;

    // Cible (product_config.csv) — identique à ce qui est affiché dans le tableau Référence
    ProductConfigEntry pcfg;
    int targetH = s.cadencePerHour;
    if (lookupProduct(s.reference, pcfg) && pcfg.targetPerHour > 0)
        targetH = pcfg.targetPerHour;
    double piecesShift = targetH * 7.5;

    int    shift       = getShiftForTime(s.startTime);
    qint64 arretSecs   = s.totalDowntimeSecs();
    QString dureeArret = QString("%1:%2:%3")
        .arg(arretSecs/3600,2,10,QChar('0'))
        .arg((arretSecs%3600)/60,2,10,QChar('0'))
        .arg(arretSecs%60,2,10,QChar('0'));

    // Début / fin des arrêts (comme dans le tableau Référence)
    QStringList starts, ends, causes;
    for (const auto &d : s.downtimes) {
        if (d.startDowntime.isValid()) starts << d.startDowntime.toString("HH:mm");
        ends << (d.endDowntime.isValid() ? d.endDowntime.toString("HH:mm") : "En cours");
        if (!d.causeArret.isEmpty()) causes << d.causeArret;
    }
    QString debutStr = starts.isEmpty() ? "—" : starts.join(" / ");
    QString finStr    = ends.isEmpty()   ? "—" : ends.join(" / ");
    QString causeStr  = causes.isEmpty() ? "—" : causes.join(" / ");

    out << nSess << ","
        << csvEscape(s.reference) << ","
        << csvEscape(s.idOperateur) << ","
        << csvEscape(s.nomOperateur) << ","
        << csvEscape(s.prenomOperateur) << ","
        << csvEscape(s.machine) << ","
        << "Shift " << shift << ","
        << targetH << ","
        << QString::number(qRound(piecesShift)) << ","
        << s.conformes << ","
        << s.nonConformes << ","
        << csvEscape(dureeArret) << ","
        << csvEscape(debutStr) << ","
        << csvEscape(finStr) << ","
        << csvEscape(causeStr) << "\n";
    f.close();
}

void MainWindow::onDownloadHistoryCsv() {
    // S'assure que le CSV reflète bien toutes les sessions des 2 tableaux
    // (Opérateur + Référence) avant de le proposer en téléchargement.
    rebuildCsvFromHistory();
    QString srcPath = histCsvPath();

    if (!QFile::exists(srcPath)) {
        QMessageBox::warning(this, "Historique",
            "Aucun fichier historique.csv trouvé pour le moment.");
        return;
    }

    QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
                             .filePath("historique.csv");
    QString destPath = QFileDialog::getSaveFileName(
        this, "Télécharger l'historique (CSV)", suggested, "Fichier CSV (*.csv)");
    if (destPath.isEmpty()) return;

    if (QFile::exists(destPath)) QFile::remove(destPath);
    if (QFile::copy(srcPath, destPath)) {
        QMessageBox::information(this, "✔  Téléchargé",
            QString("Historique exporté avec succès :\n%1").arg(destPath));
    } else {
        QMessageBox::warning(this, "Erreur",
            "Impossible d'enregistrer le fichier à cet emplacement.");
    }
}

void MainWindow::rebuildCsvFromHistory() {
    // Recrée le fichier CSV entièrement depuis l'historique JSON
    QString path = histCsvPath();
    QFile::remove(path);
    auto hist = loadHistory();
    if (hist.isEmpty()) {
        // Écrit quand même l'en-tête pour un fichier valide
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);
            out.setEncoding(QStringConverter::Utf8);
            out << "\xEF\xBB\xBF";
            out << "N_SESSION,REFERENCE,ID_OPERATEUR,NOM_OPERATEUR,PRENOM_OPERATEUR,"
                   "NUMERO_MACHINE,SHIFT,CIBLE_PCS_HEURE,CIBLE_PCS_SHIFT,"
                   "CONFORMES,NON_CONFORMES,TEMPS_ARRET,DEBUT_ARRET,FIN_ARRET,CAUSE_ARRET\n";
        }
        regenerateProductionRecordsJson();
        regenerateBreakdownJson();
        return;
    }
    for (int i = 0; i < hist.size(); ++i)
        appendSessionToCsv(hist[i], i + 1);

    regenerateProductionRecordsJson();
    regenerateBreakdownJson();
}

void MainWindow::pruneSeededPlaceholders() {
    // Supprime les lignes "placeholder" injectées par une ancienne version
    // (référence sans opérateur, sans machine, 0 conforme/non-conforme, aucun arrêt).
    // Ne garde que les vraies sessions réalisées par un opérateur.
    auto hist = loadHistory();
    QList<InspectionSession> cleaned;
    bool changed = false;
    for (const auto &s : std::as_const(hist)) {
        bool looksSeeded = s.idOperateur.trimmed().isEmpty()
                         && s.nomOperateur.trimmed().isEmpty()
                         && s.prenomOperateur.trimmed().isEmpty()
                         && s.machine.trimmed() == "—"
                         && s.conformes == 0
                         && s.nonConformes == 0
                         && s.downtimes.isEmpty();
        if (looksSeeded) { changed = true; continue; }
        cleaned.append(s);
    }
    if (changed) {
        saveAllHistory(cleaned);
        rebuildCsvFromHistory();
    }
}

QString MainWindow::askDowntimeCause() {
    QDialog dlg(this);
    dlg.setWindowTitle("Cause de l'arrêt machine — LEAR Corporation");
    dlg.setFixedWidth(420);
    dlg.setStyleSheet(
        "QDialog{background:#080e1c;}"
        "QLabel{color:#f0f6fc;}"
        "QPushButton#causeBtn{background:#0d1526;color:#e2e8f0;border:1px solid #1e2d4a;"
        "  border-radius:13px;padding:12px 16px;font-size:13.5px;font-weight:700;text-align:left;}"
        "QPushButton#causeBtn:hover{background:#241014;border-color:#ef4444;color:#fff;}"
        "QPushButton#causeBtn:pressed{background:#3f1216;}"
        "QPushButton#cancelCause{background:transparent;color:#9ca3af;border:1px solid #2e3e5d;"
        "  border-radius:9px;padding:9px 20px;font-size:12.5px;font-weight:700;}"
        "QPushButton#cancelCause:hover{background:#1e2d4a;color:#f0f6fc;}"
    );

    QVBoxLayout *outer = new QVBoxLayout(&dlg);
    outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);

    // Liseré rouge en haut, comme les autres cartes de l'app
    QFrame *topBar = new QFrame(); topBar->setFixedHeight(4);
    topBar->setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
                           "stop:0 #7a0d0d, stop:0.5 #e02020, stop:1 #7a0d0d);"
                           "border-top-left-radius:14px;border-top-right-radius:14px;");
    outer->addWidget(topBar);

    QVBoxLayout *vl = new QVBoxLayout();
    vl->setContentsMargins(24,20,24,20); vl->setSpacing(10);
    outer->addLayout(vl);

    QHBoxLayout *hr = new QHBoxLayout(); hr->setSpacing(12);
    QLabel *icon = new QLabel("⚠"); icon->setFixedSize(36,36);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet("font-size:18px;background:#3a1414;border-radius:18px;");
    QVBoxLayout *tCol = new QVBoxLayout(); tCol->setSpacing(1);
    QLabel *title = new QLabel("Cause de l'arrêt");
    title->setStyleSheet("color:#ef4444;font-size:16px;font-weight:800;");
    QLabel *subtitle = new QLabel("Sélectionnez le motif de l'arrêt machine");
    subtitle->setStyleSheet("color:#8391a8;font-size:11.5px;font-weight:500;");
    tCol->addWidget(title); tCol->addWidget(subtitle);
    hr->addWidget(icon); hr->addLayout(tCol); hr->addStretch();
    vl->addLayout(hr);
    vl->addSpacing(4);

    struct CauseDef { QString emoji; QString label; QString badgeBg; };
    const QList<CauseDef> causeDefs = {
        {"🔧", "Panne machine",        "#3a2410"},
        {"🔌", "Problème câble",       "#122f3a"},
        {"📦", "Fin de stock matière", "#2a1a3a"},
    };

    QString chosen;

    // Pile à deux pages : liste des causes / saisie libre "Autre"
    QStackedWidget *causeStack = new QStackedWidget();
    QWidget *listPage = new QWidget();
    QVBoxLayout *listL = new QVBoxLayout(listPage);
    listL->setContentsMargins(0,0,0,0); listL->setSpacing(10);

    for (const auto &cd : causeDefs) {
        QPushButton *btn = new QPushButton();
        btn->setObjectName("causeBtn");
        btn->setMinimumHeight(46);
        btn->setCursor(Qt::PointingHandCursor);

        QHBoxLayout *row = new QHBoxLayout(btn);
        row->setContentsMargins(6,4,6,4); row->setSpacing(12);
        QLabel *badge = new QLabel(cd.emoji);
        badge->setFixedSize(32,32);
        badge->setAlignment(Qt::AlignCenter);
        badge->setStyleSheet(QString("background:%1;border-radius:16px;font-size:15px;").arg(cd.badgeBg));
        QLabel *lbl = new QLabel(cd.label);
        lbl->setStyleSheet("font-size:13.5px;font-weight:700;color:#e2e8f0;background:transparent;");
        row->addWidget(badge); row->addWidget(lbl); row->addStretch();

        listL->addWidget(btn);
        QString causeLabel = cd.label;
        connect(btn, &QPushButton::clicked, &dlg, [&dlg, &chosen, causeLabel]() {
            chosen = causeLabel;
            dlg.accept();
        });
    }

    // Bouton "Autre" — ouvre la saisie libre
    QPushButton *otherBtn = new QPushButton();
    otherBtn->setObjectName("causeBtn");
    otherBtn->setMinimumHeight(46);
    otherBtn->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *otherRow = new QHBoxLayout(otherBtn);
    otherRow->setContentsMargins(6,4,6,4); otherRow->setSpacing(12);
    QLabel *otherBadge = new QLabel("✏");
    otherBadge->setFixedSize(32,32);
    otherBadge->setAlignment(Qt::AlignCenter);
    otherBadge->setStyleSheet("background:#2a2a2a;border-radius:16px;font-size:15px;");
    QLabel *otherLbl = new QLabel("Autre (préciser)");
    otherLbl->setStyleSheet("font-size:13.5px;font-weight:700;color:#e2e8f0;background:transparent;");
    otherRow->addWidget(otherBadge); otherRow->addWidget(otherLbl); otherRow->addStretch();
    listL->addWidget(otherBtn);

    causeStack->addWidget(listPage);

    // Page saisie libre
    QWidget *customPage = new QWidget();
    QVBoxLayout *customL = new QVBoxLayout(customPage);
    customL->setContentsMargins(0,0,0,0); customL->setSpacing(10);
    QLabel *customLbl = new QLabel("Décrivez la cause de l'arrêt :");
    customLbl->setStyleSheet("font-size:12.5px;font-weight:700;color:#c3cbdb;");
    QLineEdit *customEdit = new QLineEdit();
    customEdit->setPlaceholderText("ex : Attente technicien qualité...");
    customEdit->setStyleSheet(
        "QLineEdit{background:#0d1526;color:#f0f6fc;border:1px solid #1e2d4a;"
        "border-radius:9px;padding:9px 12px;font-size:12.5px;min-height:22px;}"
        "QLineEdit:focus{border:1px solid #ef4444;}");
    QHBoxLayout *customBtns = new QHBoxLayout(); customBtns->setSpacing(8);
    QPushButton *backBtn = new QPushButton("←  Retour");
    backBtn->setStyleSheet(
        "QPushButton{background:transparent;color:#9ca3af;border:1px solid #2e3e5d;"
        "border-radius:9px;padding:8px 14px;font-size:12px;font-weight:700;}"
        "QPushButton:hover{background:#1e2d4a;color:#f0f6fc;}");
    backBtn->setCursor(Qt::PointingHandCursor);
    QPushButton *validBtn = new QPushButton("✔  Valider");
    validBtn->setStyleSheet(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #dc2626,stop:1 #991b1b);"
        "color:white;border:none;border-radius:9px;padding:8px 14px;font-size:12px;font-weight:800;}"
        "QPushButton:hover{background:#ef4444;}");
    validBtn->setCursor(Qt::PointingHandCursor);
    customBtns->addWidget(backBtn); customBtns->addWidget(validBtn);
    customL->addWidget(customLbl);
    customL->addWidget(customEdit);
    customL->addLayout(customBtns);
    causeStack->addWidget(customPage);

    connect(otherBtn, &QPushButton::clicked, &dlg, [causeStack, customEdit]() {
        causeStack->setCurrentIndex(1);
        customEdit->setFocus();
    });
    connect(backBtn, &QPushButton::clicked, &dlg, [causeStack]() {
        causeStack->setCurrentIndex(0);
    });
    auto validateCustom = [&dlg, &chosen, customEdit]() {
        QString txt = customEdit->text().trimmed();
        if (txt.isEmpty()) { customEdit->setFocus(); return; }
        chosen = txt;
        dlg.accept();
    };
    connect(validBtn, &QPushButton::clicked, &dlg, validateCustom);
    connect(customEdit, &QLineEdit::returnPressed, &dlg, validateCustom);

    vl->addWidget(causeStack);

    vl->addSpacing(4);
    QHBoxLayout *br = new QHBoxLayout(); br->addStretch();
    QPushButton *cancelBtn = new QPushButton("✕  Annuler");
    cancelBtn->setObjectName("cancelCause");
    cancelBtn->setFixedWidth(120);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    br->addWidget(cancelBtn);
    vl->addLayout(br);

    QGraphicsDropShadowEffect *dsh = new QGraphicsDropShadowEffect(&dlg);
    dsh->setBlurRadius(50); dsh->setOffset(0,6); dsh->setColor(QColor(200,0,0,90));
    dlg.setGraphicsEffect(dsh);

    if (dlg.exec() != QDialog::Accepted) return QString();
    return chosen;
}

// ═══════════════════════════════════════════════════════════════
//  Stats Page
// ═══════════════════════════════════════════════════════════════

void MainWindow::setupStatsPage() {
    statsPage = new BgWidget();
    QVBoxLayout *ml = new QVBoxLayout(statsPage);
    ml->setContentsMargins(28,28,28,28); ml->setSpacing(16);

    // Cards layout
    QHBoxLayout *cardsL = new QHBoxLayout(); cardsL->setSpacing(18);

    // 1. Répartition (Pie Chart)
    QFrame *pieCard = new QFrame(); pieCard->setObjectName("inspectionCard");
    pieCard->setFixedSize(320, 360);
    QVBoxLayout *pieL = new QVBoxLayout(pieCard);
    pieL->setContentsMargins(20,18,20,18);
    QHBoxLayout *pieTRow = new QHBoxLayout(); pieTRow->setSpacing(8);
    QLabel *pieIcon = new QLabel("🥧"); pieIcon->setFixedSize(26,26);
    pieIcon->setAlignment(Qt::AlignCenter);
    pieIcon->setStyleSheet("font-size:13px;background:#241a3d;border-radius:13px;");
    QLabel *pieTitle = new QLabel("Répartition des pièces");
    pieTitle->setStyleSheet("color:#f0f6fc;font-size:14px;font-weight:800;");
    pieTRow->addWidget(pieIcon); pieTRow->addWidget(pieTitle); pieTRow->addStretch();
    pieL->addLayout(pieTRow);
    pieChart = new PieChartWidget();
    pieChart->setFixedSize(170, 170);
    pieL->addStretch();
    pieL->addWidget(pieChart, 0, Qt::AlignCenter);
    
    // Legends for Pie Chart
    QHBoxLayout *legL = new QHBoxLayout(); legL->setSpacing(18); legL->setAlignment(Qt::AlignCenter);
    auto mkLeg = [](const QString &txt, const QString &col){
        QHBoxLayout *l = new QHBoxLayout(); l->setSpacing(6);
        QFrame *c = new QFrame(); c->setFixedSize(10,10); c->setStyleSheet("background:"+col+";border-radius:5px;");
        QLabel *t = new QLabel(txt); t->setStyleSheet("color:#c3cbdb; font-size:11px; font-weight:600;");
        l->addWidget(c); l->addWidget(t);
        return l;
    };
    legL->addLayout(mkLeg("Conforme", "#27ae60"));
    legL->addLayout(mkLeg("Non Conforme", "#e74c3c"));
    pieL->addSpacing(10);
    pieL->addLayout(legL);
    pieL->addStretch();

    QGraphicsDropShadowEffect *pieSh = new QGraphicsDropShadowEffect(pieCard);
    pieSh->setBlurRadius(40); pieSh->setOffset(0,6); pieSh->setColor(QColor(0,0,0,120));
    pieCard->setGraphicsEffect(pieSh);

    // 2. Résumé (Stats list)
    QFrame *sumCard = new QFrame(); sumCard->setObjectName("inspectionCard");
    sumCard->setFixedSize(320, 360);
    QVBoxLayout *sumL = new QVBoxLayout(sumCard);
    sumL->setContentsMargins(20,18,20,18);
    QHBoxLayout *sumTRow = new QHBoxLayout(); sumTRow->setSpacing(8);
    QLabel *sumIcon = new QLabel("📊"); sumIcon->setFixedSize(26,26);
    sumIcon->setAlignment(Qt::AlignCenter);
    sumIcon->setStyleSheet("font-size:13px;background:#1d3a5f;border-radius:13px;");
    QLabel *sumTitle = new QLabel("Résumé");
    sumTitle->setStyleSheet("color:#f0f6fc;font-size:14px;font-weight:800;");
    sumTRow->addWidget(sumIcon); sumTRow->addWidget(sumTitle); sumTRow->addStretch();
    sumL->addLayout(sumTRow);
    sumL->addSpacing(12);

    auto mkStatRow = [](const QString &emoji, const QString &title, QLabel* &lbl, const QString &valCol = "#f0f6fc"){
        QFrame *row = new QFrame();
        row->setStyleSheet("QFrame{background:#0a1120;border-radius:10px;}");
        QHBoxLayout *l = new QHBoxLayout(row); l->setContentsMargins(10,7,10,7); l->setSpacing(8);
        QLabel *ic = new QLabel(emoji); ic->setFixedWidth(17); ic->setStyleSheet("font-size:11.5px;background:transparent;");
        QLabel *t = new QLabel(title); t->setStyleSheet("color:#94a3b8; font-size:11.5px; font-weight:700;background:transparent;");
        lbl = new QLabel("—"); lbl->setStyleSheet("color:"+valCol+"; font-size:13.5px; font-weight:800;background:transparent;");
        l->addWidget(ic); l->addWidget(t); l->addStretch(); l->addWidget(lbl);
        return row;
    };

    sumL->addWidget(mkStatRow("🏷", "Part number", statsRefLabel));
    sumL->addSpacing(6);
    sumL->addWidget(mkStatRow("📦", "Total pièces", statsTotLabel));
    sumL->addSpacing(6);
    sumL->addWidget(mkStatRow("✔", "Conforme", statsConfLabel, "#4ade80"));
    sumL->addSpacing(6);
    sumL->addWidget(mkStatRow("✘", "Non Conforme", statsNonConfLabel, "#f87171"));
    sumL->addSpacing(6);
    sumL->addWidget(mkStatRow("⏳", "Restantes", statsRestLabel));
    sumL->addSpacing(6);
    sumL->addWidget(mkStatRow("🎯", "Taux de conformité", statsPctLabel, "#fbbf24"));
    sumL->addStretch();

    QGraphicsDropShadowEffect *sumSh = new QGraphicsDropShadowEffect(sumCard);
    sumSh->setBlurRadius(40); sumSh->setOffset(0,6); sumSh->setColor(QColor(0,0,0,120));
    sumCard->setGraphicsEffect(sumSh);

    cardsL->addWidget(pieCard);
    cardsL->addWidget(sumCard);

    QHBoxLayout *centerH = new QHBoxLayout();
    centerH->addStretch();
    centerH->addLayout(cardsL);
    centerH->addStretch();

    ml->addStretch();
    ml->addLayout(centerH);
    ml->addStretch();

    // Retour button
    statsReturnBtn = new QPushButton("←  Retour à l'inspection");
    statsReturnBtn->setObjectName("secondaryBtn"); statsReturnBtn->setFixedWidth(200); statsReturnBtn->setMinimumHeight(38);
    statsReturnBtn->setCursor(Qt::PointingHandCursor);
    connect(statsReturnBtn, &QPushButton::clicked, [=](){ stack->setCurrentIndex(1); fitWindowToCurrentPage(); });
    QHBoxLayout *retRow = new QHBoxLayout(); retRow->addStretch(); retRow->addWidget(statsReturnBtn); retRow->addStretch();
    ml->addLayout(retRow);
}

void MainWindow::showStatsPage() {
    pieChart->updateData(curSess.conformes, curSess.nonConformes);
    statsRefLabel->setText(curSess.reference);
    statsTotLabel->setText(QString::number(curSess.totalInspected()));
    statsConfLabel->setText(QString::number(curSess.conformes));
    statsNonConfLabel->setText(QString::number(curSess.nonConformes));
    
    int t = curSess.targetPerShift();
    int r = t - curSess.totalInspected();
    if(r < 0) r = 0;
    statsRestLabel->setText(QString::number(r));
    
    statsPctLabel->setText(QString::number(curSess.conformityPct(), 'f', 2) + " %");
    
    stack->setCurrentIndex(2);
    fitWindowToCurrentPage();
}
