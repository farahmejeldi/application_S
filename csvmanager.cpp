#include "csvmanager.h"
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QDebug>

QString CsvManager::csvEscape(const QString &s) {
    QString r = s;
    r.replace("\"", "\"\"");
    if (r.contains(',') || r.contains('"') || r.contains('\n'))
        r = "\"" + r + "\"";
    return r;
}

QString CsvManager::createCsvHeader() {
    return "N_SESSION,NUMERO_MACHINE,CONFORMES,NON_CONFORMES,"
           "PIECES_HEURE,PIECES_SHIFT,NOM_OPERATEUR,NB_SHIFT,"
           "DEBUT_ARRET,FIN_ARRET,TEMPS_ARRET,CAUSE_ARRET";
}

void CsvManager::appendSessionToCsvNew(
    const QString &csvPath,
    int sessionNumber,
    const QString &machine,
    int conformes,
    int nonConformes,
    int piecesParHeure,
    int piecesParShift,
    const QString &nomOperateur,
    const QString &nbShift,
    const QDateTime &debutArret,
    const QDateTime &finArret,
    int dureeArretSecondes,
    const QString &causeArret)
{
    bool needHeader = !QFile::exists(csvPath);

    QFile f(csvPath);
    if (!f.open(QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Impossible d'ouvrir le fichier CSV:" << csvPath;
        return;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);

    if (needHeader) {
        out << "\xEF\xBB\xBF"; // BOM UTF-8
        out << createCsvHeader() << "\n";
    }

    // Format de durée HH:MM:SS
    int heures = dureeArretSecondes / 3600;
    int minutes = (dureeArretSecondes % 3600) / 60;
    int secondes = dureeArretSecondes % 60;
    QString dureeStr = QString("%1:%2:%3")
        .arg(heures, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secondes, 2, 10, QChar('0'));

    // Format des datetime: YYYY-MM-DD HH:MM:SS
    QString debutStr = debutArret.isValid() ? debutArret.toString("yyyy-MM-dd HH:mm:ss") : "";
    QString finStr = finArret.isValid() ? finArret.toString("yyyy-MM-dd HH:mm:ss") : "";

    out << sessionNumber << ","
        << csvEscape(machine) << ","
        << conformes << ","
        << nonConformes << ","
        << piecesParHeure << ","
        << piecesParShift << ","
        << csvEscape(nomOperateur) << ","
        << csvEscape(nbShift) << ","
        << csvEscape(debutStr) << ","
        << csvEscape(finStr) << ","
        << csvEscape(dureeStr) << ","
        << csvEscape(causeArret) << "\n";

    f.close();
}

QList<CsvHistoriqueEntry> CsvManager::loadHistoryFromCsv(const QString &csvPath) {
    QList<CsvHistoriqueEntry> entries;

    QFile f(csvPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Fichier CSV non trouvé:" << csvPath;
        return entries;
    }

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    QString header = in.readLine(); // Sauter l'en-tête (et le BOM si présent)
    if (header.startsWith("\xEF\xBB\xBF")) {
        header = header.mid(3); // Enlever le BOM
    }

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;

        // Parser la ligne CSV (simple, pas de gestion des guillemets complexes)
        QStringList parts = line.split(',');
        if (parts.size() < 10) continue; // Au minimum 10 colonnes

        CsvHistoriqueEntry entry;
        entry.sessionNumber = parts[0].toInt();
        entry.machineNumber = parts[1];
        entry.conformes = parts[2].toInt();
        entry.nonConformes = parts[3].toInt();
        entry.piecesParHeure = parts[4].toInt();
        entry.piecesParShift = parts[5].toInt();
        entry.nomOperateur = parts[6];
        entry.nbShift = parts[7];
        
        // Colonnes anciennes (indices 8-9) ou nouvelles (indices 8-11)
        if (parts.size() >= 12) {
            // Nouveau format avec début/fin
            entry.debutArret = QDateTime::fromString(parts[8], "yyyy-MM-dd HH:mm:ss");
            entry.finArret = QDateTime::fromString(parts[9], "yyyy-MM-dd HH:mm:ss");
            entry.tempsArret = parts[10];
            entry.causeArret = parts[11];
            
            // Calculer la durée depuis le format HH:MM:SS
            QStringList timeParts = parts[10].split(':');
            if (timeParts.size() == 3) {
                entry.dureeArretSecondes = 
                    timeParts[0].toInt() * 3600 + 
                    timeParts[1].toInt() * 60 + 
                    timeParts[2].toInt();
            }
        } else {
            // Ancien format sans début/fin
            entry.tempsArret = parts[8];
            entry.causeArret = parts[9];
            entry.dureeArretSecondes = 0;
        }

        entries.append(entry);
    }

    f.close();
    return entries;
}

void CsvManager::migrateOldCsvFormat(const QString &csvPath) {
    // Charger les données existantes
    QList<CsvHistoriqueEntry> entries = loadHistoryFromCsv(csvPath);

    // Supprimer l'ancien fichier
    QFile::remove(csvPath);

    // Réécrire avec le nouveau format
    for (const auto &entry : entries) {
        appendSessionToCsvNew(
            csvPath,
            entry.sessionNumber,
            entry.machineNumber,
            entry.conformes,
            entry.nonConformes,
            entry.piecesParHeure,
            entry.piecesParShift,
            entry.nomOperateur,
            entry.nbShift,
            entry.debutArret,
            entry.finArret,
            entry.dureeArretSecondes,
            entry.causeArret
        );
    }
}
