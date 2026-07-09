#ifndef CSVMANAGER_H
#define CSVMANAGER_H

#include <QString>
#include <QList>
#include <QDateTime>

// Structure pour représenter une ligne du fichier historique.csv
struct CsvHistoriqueEntry {
    int    sessionNumber;
    QString machineNumber;
    int    conformes;
    int    nonConformes;
    int    piecesParHeure;
    int    piecesParShift;
    QString nomOperateur;
    QString nbShift;
    QString tempsArret;         // format HH:MM:SS
    QString causeArret;
    QDateTime debutArret;       // Nouveau: heure début
    QDateTime finArret;         // Nouveau: heure fin
    int    dureeArretSecondes;  // Nouveau: durée en secondes
};

class CsvManager {
public:
    // Sauvegarder une session au format CSV avec toutes les colonnes
    static void appendSessionToCsvNew(
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
        const QString &causeArret
    );

    // Charger l'historique depuis le CSV
    static QList<CsvHistoriqueEntry> loadHistoryFromCsv(const QString &csvPath);

    // Convertir un fichier CSV ancien format vers le nouveau format
    static void migrateOldCsvFormat(const QString &csvPath);

    // Créer l'en-tête du fichier CSV
    static QString createCsvHeader();

    // Escapage des valeurs CSV
    static QString csvEscape(const QString &s);
};

#endif // CSVMANAGER_H
