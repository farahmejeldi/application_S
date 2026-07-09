# Application Inspection Industrielle v9 - Guide de Déploiement

## 📦 Fichiers Fournies

### Nouveaux Fichiers
```
inspection_modifiee_v9/
├── csvmanager.h                 # Gestionnaire de CSV (nouveau)
├── csvmanager.cpp               # Implémentation du gestionnaire (nouveau)
├── MODIFICATIONS_GUIDE.md        # Guide complet des modifications
├── FONCTIONS_ADDITIVES.cpp      # Fonctions à intégrer dans mainwindow.cpp
├── README_DEPLOYMENT.md         # Ce fichier
└── [Fichiers originaux inchangés]
    ├── main.cpp
    ├── mainwindow.h
    ├── mainwindow.cpp
    ├── xlsxreader.h
    ├── xlsxreader.cpp
    └── ...
```

---

## 🚀 Guide d'Intégration Rapide

### Étape 1: Préparer l'Environnement
```bash
# Copier les nouveaux fichiers au répertoire du projet
cp csvmanager.h /chemin/vers/inspection_industrielle_modifie/
cp csvmanager.cpp /chemin/vers/inspection_industrielle_modifie/
```

### Étape 2: Modifier le fichier .pro

**Fichier: `inspection_app.pro`**

Ajouter ces lignes:
```qmake
# Ajouter après les HEADERS existants:
HEADERS += csvmanager.h

# Ajouter après les SOURCES existants:
SOURCES += csvmanager.cpp
```

Exemple complet:
```qmake
QT += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

TARGET = inspection_app
TEMPLATE = app

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    xlsxreader.cpp \
    csvmanager.cpp        # ← AJOUT

HEADERS += \
    mainwindow.h \
    xlsxreader.h \
    csvmanager.h          # ← AJOUT

# ...reste du fichier...
```

### Étape 3: Modifier mainwindow.h

**Chercher:** `class MainWindow : public QMainWindow`

**Ajouter après les includes existants:**
```cpp
#include "csvmanager.h"
#include <QFileSystemWatcher>
```

**Ajouter dans la section `private` des variables membres:**
```cpp
private:
    // ... variables existantes ...
    
    // ── NOUVEAUX pour synchronisation CSV ──
    QFileSystemWatcher *csvWatcher;
    
    // ── NOUVELLES méthodes ──
    void setupCsvWatcher();
    void onCsvFileChanged();
    QList<InspectionSession> loadSessionsFromCsv();
    void displaySessionsInTable(const QList<InspectionSession> &sessions);
    void styleTableWidget(QTableWidget *table);
    void setupSessionTableDialog();
```

**Modifier la struct `InspectionSession` (ligne ~71):**
```cpp
struct InspectionSession {
    QString   reference;
    QString   machine;
    int       cadencePerHour  = 0;
    int       targetNumberFixe = 0;  // ← NOUVEAU: Nombre de cible fixe
    // ... reste inchangé ...
};
```

### Étape 4: Modifier mainwindow.cpp

#### Modification 4.1: Remplacer la fonction `appendSessionToCsv`

**Localiser:** environ ligne 2044

**Remplacer le contenu entier par la version du fichier `FONCTIONS_ADDITIVES.cpp`** (section "1. AMÉLIORATIONS DE L'HISTORIQUE CSV", fonction `appendSessionToCsv`)

#### Modification 4.2: Ajouter les nouvelles fonctions

**Après la fonction `appendSessionToCsv`, ajouter:**
- `setupCsvWatcher()`
- `onCsvFileChanged()`
- `loadSessionsFromCsv()`
- `displaySessionsInTable()`
- `styleTableWidget()`

(Copier depuis `FONCTIONS_ADDITIVES.cpp`)

#### Modification 4.3: Initialiser l'observateur CSV

**Dans le constructeur `MainWindow::MainWindow()`, après `setupUI();`**
```cpp
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUI();
    setupCsvWatcher();        // ← AJOUTER CETTE LIGNE
    setupSessionTableDialog(); // ← AJOUTER CETTE LIGNE
    
    // ... reste du constructeur ...
}
```

#### Modification 4.4: Fixer le nombre de cible au démarrage

**Localiser:** la fonction `onStartInspection()`

**Ajouter cette ligne après `curSess.startTime = QDateTime::currentDateTime();`:**
```cpp
curSess.targetNumberFixe = curSess.targetPerShift();
```

#### Modification 4.5: Utiliser le nombre fixe dans les affichages

**Localiser:** la fonction `updateCounters()`

**Remplacer les calculs de `lbRest`:**
```cpp
// AVANT:
// lbRest->setText(QString::number(curSess.targetPerShift() - curSess.totalInspected()));

// APRÈS:
int targetFixed = curSess.targetNumberFixe > 0 
    ? curSess.targetNumberFixe 
    : curSess.targetPerShift();
lbRest->setText(QString::number(targetFixed - curSess.totalInspected()));
```

### Étape 5: Compiler et Tester

```bash
# Depuis le répertoire du projet
qmake
make clean
make

# Ou avec QtCreator:
# - Ouvrir le fichier .pro
# - Build → Rebuild Project
```

---

## ✅ Tests de Validation

### Test 1: Création de Session
- Démarrer une nouvelle session
- Vérifier que `targetNumberFixe` est défini
- Vérifier que le nombre de cible s'affiche correctement

### Test 2: Synchronisation CSV
- Éditer manuellement `historique.csv`
- Ajouter une nouvelle ligne
- L'application doit détecter le changement et se mettre à jour

### Test 3: Données d'Arrêt
- Enregistrer un arrêt machine
- Vérifier que `DEBUT_ARRET`, `FIN_ARRET`, `TEMPS_ARRET` sont remplis dans le CSV

### Test 4: Style des Tableaux
- Ouvrir le tableau de session
- Vérifier les couleurs et l'espacement
- Tester le tri par colonne

### Test 5: Chargement CSV
- Fermer l'application
- Modifier le fichier `historique.csv`
- Redémarrer l'application
- Vérifier que les modifications sont chargées

---

## 📊 Format du Nouveau CSV

### Avant (10 colonnes):
```
N_SESSION,NUMERO_MACHINE,CONFORMES,NON_CONFORMES,PIECES_HEURE,PIECES_SHIFT,
NOM_OPERATEUR,NB_SHIFT,TEMPS_ARRET,CAUSE_ARRET
```

### Après (12 colonnes):
```
N_SESSION,NUMERO_MACHINE,CONFORMES,NON_CONFORMES,PIECES_HEURE,PIECES_SHIFT,
NOM_OPERATEUR,NB_SHIFT,DEBUT_ARRET,FIN_ARRET,TEMPS_ARRET,CAUSE_ARRET
```

**Exemple:**
```csv
1,Machine 01,96,84,127,954,Bouali Oumayma,Shift 1,
2023-12-15 09:30:45,2023-12-15 09:35:12,00:04:27,Panne machine
```

---

## 🔧 Dépannage

### Problème 1: "csvmanager.h: No such file or directory"
**Solution:** Vérifier que `csvmanager.h` et `csvmanager.cpp` sont au bon endroit et que `inspection_app.pro` est mis à jour

### Problème 2: CSV ne se met pas à jour automatiquement
**Solution:** Vérifier que `setupCsvWatcher()` est appelé dans le constructeur

### Problème 3: Erreur de compilation avec QFileSystemWatcher
**Solution:** Ajouter `#include <QFileSystemWatcher>` dans mainwindow.h

### Problème 4: Tableaux affichent les anciennes données
**Solution:** Appeler `loadSessionsFromCsv()` puis `displaySessionsInTable()`

### Problème 5: Format CSV incompatible
**Solution:** Exécuter la migration: `CsvManager::migrateOldCsvFormat(histCsvPath());`

---

## 📝 Notes Importantes

1. **Backward Compatibility:** Le nouveau code peut lire l'ancien format CSV, mais génère le nouveau format. Migrer les données anciennes avec `migrateOldCsvFormat()`.

2. **Synchronisation:** L'observateur de fichier utilise `QFileSystemWatcher`. Sur certains systèmes, il peut y avoir un délai de détection.

3. **Performance:** Pour les très gros fichiers CSV (>10MB), charger via `loadSessionsFromCsv()` peut être lent. Envisager une pagination.

4. **Nombre Fixe:** Le `targetNumberFixe` est défini une seule fois au démarrage. Ne change pas même si la vitesse change pendant la session.

5. **Migration d'Anciens Données:** Si vous avez beaucoup de données, utiliser `migrateOldCsvFormat()` au premier démarrage.

---

## 🎨 Couleurs et Styles

Les couleurs utilisées dans la v9:
- **Fond:** `#0f1624` (très sombre)
- **Fond Secondaire:** `#151e2b` (slightly lighter)
- **Accent Bleu:** `#1e6ba8` (selection)
- **Accent Vert:** `#27ae60` (success)
- **Accent Rouge:** `#e02020` (error)
- **Texte:** `#cbd5e1` (gris clair)
- **Texte Primaire:** `#f0f6fc` (blanc cassé)

---

## 📈 Améliorations Futures

- [ ] Export en Excel avec les colonnes améliorées
- [ ] Graphiques de durée d'arrêt
- [ ] Filtrage par date/machine/opérateur
- [ ] Sauvegarde automatique versée de bases de données
- [ ] Import/export bidirectionnel SQL

---

## 💬 Support

Pour les problèmes de compilation ou d'intégration, vérifier:
1. La version de Qt (minimum Qt 5.15)
2. La version du compilateur (C++17 requis)
3. Les includes et chemins de fichiers
4. La cohérence entre .h et .cpp

---

## 📄 Fichiers de Référence

- **MODIFICATIONS_GUIDE.md** - Documentation détaillée
- **FONCTIONS_ADDITIVES.cpp** - Code source à intégrer
- **csvmanager.h/cpp** - Gestionnaire CSV complet

Bonne chance! 🚀
