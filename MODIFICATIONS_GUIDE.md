# Guide des Modifications - Application Inspection Industrielle v9

## 📋 Résumé des Changements Demandés

### 1. **Tableau Référence - Nombre de Cible Fixe**
**Avant:** Le nombre de cible était calculé par heure  
**Après:** Le nombre de cible est fixé au DÉBUT de la session et reste constant

**Implémentation:**
- Ajouter un champ `targetNumber` dans la structure `InspectionSession` (mainwindow.h)
- Ce champ sera défini au démarrage de la session: `session.targetNumber = session.targetPerShift()`
- Afficher cette valeur dans les tableaux au lieu de recalculer

```cpp
// Dans mainwindow.h - struct InspectionSession
struct InspectionSession {
    // ... autres champs ...
    int targetNumberFixe = 0;  // ← Nouveau: nombre de cible fixé au début
    
    // Remplacer le calcul dynamique par une valeur fixe
};
```

---

### 2. **Amélioration du Fichier Historique CSV**

#### Format Ancien (5 colonnes d'arrêt):
```
N_SESSION,NUMERO_MACHINE,CONFORMES,NON_CONFORMES,PIECES_HEURE,PIECES_SHIFT,
NOM_OPERATEUR,NB_SHIFT,TEMPS_ARRET,CAUSE_ARRET
```

#### Format Nouveau (avec détails d'arrêt):
```
N_SESSION,NUMERO_MACHINE,CONFORMES,NON_CONFORMES,PIECES_HEURE,PIECES_SHIFT,
NOM_OPERATEUR,NB_SHIFT,DEBUT_ARRET,FIN_ARRET,TEMPS_ARRET,CAUSE_ARRET
```

**Colonnes Ajoutées:**
- `DEBUT_ARRET` - Date/heure du début d'arrêt (format: YYYY-MM-DD HH:MM:SS)
- `FIN_ARRET` - Date/heure de fin d'arrêt (format: YYYY-MM-DD HH:MM:SS)
- `TEMPS_ARRET` - Durée totale (format: HH:MM:SS)

**Nouvelles Colonnes dans la Structure:**
```cpp
struct DowntimeRecord {
    QDateTime startDowntime;   // Existant
    QDateTime endDowntime;     // Existant
    qint64    durationSeconds = 0;
    QString   causeArret;
    // Données déjà disponibles - à utiliser pour le CSV
};
```

---

### 3. **Synchronisation Bidirectionnelle du CSV**

#### Fonctionnalité 1: Auto-sauvegarde dans CSV
```cpp
void MainWindow::appendSessionToCsv(const InspectionSession &s) {
    // À adapter pour utiliser CsvManager::appendSessionToCsvNew()
    // Inclure les dates/heures d'arrêt
}
```

#### Fonctionnalité 2: Charger depuis CSV (Bidirectionnel)
```cpp
// Nouvelle fonction dans mainwindow.cpp
QList<InspectionSession> MainWindow::loadSessionsFromCsv() {
    // Lire le fichier historique.csv
    // Convertir les lignes CSV en sessions
    // Ajouter aux tableaux de l'application
}
```

#### Fonctionnalité 3: Détection de Changements
```cpp
// Ajouter un observateur de fichier
QFileSystemWatcher *csvWatcher;

void setupCsvWatcher() {
    csvWatcher = new QFileSystemWatcher(this);
    csvWatcher->addPath(histCsvPath());
    connect(csvWatcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::onCsvFileChanged);
}

void onCsvFileChanged() {
    // Recharger depuis le CSV et mettre à jour l'UI
    loadSessionsFromCsv();
    updateTableDisplay();
}
```

---

### 4. **Amélioration des Tableaux (UI Esthétique)**

#### Tableaux Référence et Opérateurs - Style Amélioré:

**Couleurs modernes:**
```cpp
// En-tête de tableau
QHeaderView::section {
    background-color: #1e2d4a;
    color: #f0f6fc;
    padding: 8px;
    border: 1px solid #2e3e5d;
    font-weight: bold;
    font-size: 12px;
}

// Lignes alternées
QTableWidget {
    alternate-background-color: #0f1624;
    background-color: #151e2b;
    gridline-color: #2e3e5d;
}

QTableWidget::item:selected {
    background-color: #1e6ba8;
    color: #ffffff;
}

QTableWidget::item {
    padding: 6px;
    color: #cbd5e1;
}
```

#### Largeurs de colonnes ajustées:
```cpp
// Dans onShowSessionTable():
sessionTable->resizeColumnsToContents();
sessionTable->horizontalHeader()->setStretchLastSection(true);

// Hauteur des lignes
sessionTable->verticalHeader()->setDefaultSectionSize(35);
```

#### Tri et filtrage:
```cpp
// Activer le tri
sessionTable->setSortingEnabled(true);
sessionTable->sortItems(0, Qt::DescendingOrder); // Trier par N_SESSION descendant
```

---

### 5. **Suppression de Bande UI (Deuxième Fenêtre)**

**Contexte:** La deuxième fenêtre fait probablement référence à la page d'inspection  
**Élément à supprimer:** Une bande/bannière visuelle inutile

**Localiser dans mainwindow.cpp > setupInspectionPage():**
```cpp
// À identifier et supprimer:
// - QFrame avec gradient inutile
// - QLabel décoratif sans fonction
// - Séparateurs visuels redondants
```

**Exemple de bande à supprimer:**
```cpp
// AVANT (à supprimer):
QFrame *decorativeBand = new QFrame();
decorativeBand->setStyleSheet("background: qlineargradient(...)");
decorativeBand->setFixedHeight(15);
mainLayout->addWidget(decorativeBand);

// APRÈS (supprimé)
```

---

### 6. **Intégration - Étapes Pratiques**

#### Étape 1: Ajouter les nouveaux fichiers au projet
```bash
# Copier dans le répertoire du projet:
- csvmanager.h
- csvmanager.cpp

# Mettre à jour inspection_app.pro:
HEADERS += csvmanager.h
SOURCES += csvmanager.cpp
```

#### Étape 2: Modifier mainwindow.h
```cpp
#include "csvmanager.h"
#include <QFileSystemWatcher>

// Dans la classe MainWindow:
private:
    QFileSystemWatcher *csvWatcher;
    
    // Nouvelles méthodes
    void setupCsvWatcher();
    void onCsvFileChanged();
    QList<InspectionSession> loadSessionsFromCsv();
```

#### Étape 3: Modifier mainwindow.cpp

**Fonction appendSessionToCsv (remplacer entièrement):**
```cpp
void MainWindow::appendSessionToCsv(const InspectionSession &s) {
    QString path = histCsvPath();
    
    // Calculer les paramètres
    double piecesHeure = s.actualCadencePerHour();
    double piecesShift = piecesHeure * 7.5;
    int shift = getShiftForTime(s.startTime);
    qint64 arretSecs = s.totalDowntimeSecs();
    
    // Récupérer premier et dernier arrêt
    QDateTime debutArret, finArret;
    if (!s.downtimes.isEmpty()) {
        debutArret = s.downtimes.first().startDowntime;
        finArret = s.downtimes.last().endDowntime;
    }
    
    // Causes d'arrêt
    QStringList causes;
    for (const auto &d : s.downtimes)
        if (!d.causeArret.isEmpty()) 
            causes << d.causeArret;
    QString causeStr = causes.isEmpty() ? "—" : causes.join(" / ");
    QString nomOp = s.nomOperateur + " " + s.prenomOperateur;
    
    // Utiliser le nouveau gestionnaire CSV
    CsvManager::appendSessionToCsvNew(
        path,
        loadHistory().size() + 1,
        s.machine,
        s.conformes,
        s.nonConformes,
        qRound(piecesHeure),
        qRound(piecesShift),
        nomOp.trimmed(),
        "Shift " + QString::number(shift),
        debutArret,
        finArret,
        arretSecs,
        causeStr
    );
}
```

**Nouvelle fonction d'observateur:**
```cpp
void MainWindow::setupCsvWatcher() {
    csvWatcher = new QFileSystemWatcher(this);
    csvWatcher->addPath(histCsvPath());
    connect(csvWatcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::onCsvFileChanged);
}

void MainWindow::onCsvFileChanged() {
    QTimer::singleShot(500, this, [this]() {
        // Recharger depuis CSV avec gestion d'erreur
        try {
            auto sessions = loadSessionsFromCsv();
            // Mettre à jour l'affichage si visible
            if (sessionTable && sessionTable->isVisible()) {
                refreshSessionTable();
            }
        } catch (...) {
            // Erreur de lecture - ignorer
        }
    });
}

QList<InspectionSession> MainWindow::loadSessionsFromCsv() {
    QList<InspectionSession> sessions;
    auto entries = CsvManager::loadHistoryFromCsv(histCsvPath());
    
    for (const auto &entry : entries) {
        InspectionSession sess;
        sess.reference = ""; // À déterminer depuis le CSV
        sess.machine = entry.machineNumber;
        sess.conformes = entry.conformes;
        sess.nonConformes = entry.nonConformes;
        sess.nomOperateur = entry.nomOperateur;
        // etc...
        sessions.append(sess);
    }
    return sessions;
}
```

---

### 7. **Tâches de Nettoyage UI**

Dans `setupInspectionPage()`:

1. **Identifier et supprimer les éléments visuels inutiles**
2. **Améliorer l'espacement** des widgets
3. **Harmoniser les couleurs** (#1e2d4a, #0f1624, #cbd5e1, #27ae60)
4. **Éliminer les dégradés** redondants

Exemple:
```cpp
// À SUPPRIMER:
QFrame *topBand = new QFrame();
topBand->setStyleSheet("background: qlineargradient(...); border-bottom: 2px solid #ef4444;");
topBand->setFixedHeight(20);

// À CONSERVER/AMÉLIORER:
// - Les vrais éléments de contenu
// - Les espaceurs pour la mise en page
```

---

### 8. **Tests & Validation**

✅ **Test 1:** Créer une session → Vérifier que le CSV contient les 12 colonnes  
✅ **Test 2:** Éditer historique.csv → Vérifier que l'app se met à jour  
✅ **Test 3:** Arrêter la machine → Vérifier début/fin/durée dans CSV  
✅ **Test 4:** Afficher les tableaux → Vérifier le style amélioré  
✅ **Test 5:** Nombre de cible → Vérifier qu'il reste fixe toute la session  

---

### 9. **Fichier de Configuration (product_config.csv)**

Format attendu:
```csv
PartNumber,CycleTimeSeconds,TargetPerHour
PN001,30,120
PN002,45,80
```

---

### 10. **Migration de Données**

Si vous avez un historique.csv existant:

```cpp
// Au démarrage:
if (QFile::exists(histCsvPath())) {
    // Vérifier format
    auto entries = CsvManager::loadHistoryFromCsv(histCsvPath());
    
    // Si ancien format détecté
    if (needsMigration(entries)) {
        CsvManager::migrateOldCsvFormat(histCsvPath());
    }
}
```

---

## 📝 Conclusion

Ces modifications améliorent:
- **Fonctionnalité:** Synchronisation bidirectionnelle du CSV
- **Données:** Traçabilité complète des arrêts machines
- **UI:** Tableaux plus lisibles et cohérents
- **Fiabilité:** Nombre de cible fixe et transparent

Bonne chance avec l'intégration! 🚀
