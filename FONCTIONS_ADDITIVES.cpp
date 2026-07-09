// ════════════════════════════════════════════════════════════════════════════════
// ADDITIONS À MAINWINDOW.CPP - Version 9
// Synchronisation bidirectionnelle CSV et amélioration des tableaux
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────────────
// 1. AMÉLIORATIONS DE L'HISTORIQUE CSV
// ──────────────────────────────────────────────────────────────────────────────

// À ajouter dans le constructeur de MainWindow:
void MainWindow::setupCsvWatcher() {
    csvWatcher = new QFileSystemWatcher(this);
    csvWatcher->addPath(histCsvPath());
    connect(csvWatcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::onCsvFileChanged);
}

// Nouvelle fonction: observer les changements du fichier CSV
void MainWindow::onCsvFileChanged() {
    // Délai pour éviter les mises à jour multiples
    QTimer::singleShot(500, this, [this]() {
        if (sessionTable && sessionTable->isVisible()) {
            // Recharger et afficher le tableau
            auto sessions = loadSessionsFromCsv();
            if (!sessions.isEmpty()) {
                displaySessionsInTable(sessions);
            }
        }
    });
}

// Remplacer la fonction existante appendSessionToCsv par cette version améliorée:
void MainWindow::appendSessionToCsv(const InspectionSession &s) {
    QString path = histCsvPath();
    
    // Calculer les paramètres
    double piecesHeure = s.actualCadencePerHour();
    double piecesShift = piecesHeure * 7.5;
    int shift = getShiftForTime(s.startTime);
    qint64 arretSecs = s.totalDowntimeSecs();
    
    // Récupérer les dates/heures des arrêts
    QDateTime debutArret, finArret;
    if (!s.downtimes.isEmpty()) {
        debutArret = s.downtimes.first().startDowntime;
        finArret = s.downtimes.last().endDowntime;
    }
    
    // Compiler les causes d'arrêt
    QStringList causes;
    for (const auto &d : s.downtimes) {
        if (!d.causeArret.isEmpty()) {
            causes << d.causeArret;
        }
    }
    QString causeStr = causes.isEmpty() ? "—" : causes.join(" / ");
    QString nomOp = (s.nomOperateur + " " + s.prenomOperateur).trimmed();
    
    // Utiliser le gestionnaire CSV pour ajouter avec toutes les colonnes
    CsvManager::appendSessionToCsvNew(
        path,
        loadHistory().size() + 1,  // Numéro de session
        s.machine,
        s.conformes,
        s.nonConformes,
        qRound(piecesHeure),
        qRound(piecesShift),
        nomOp,
        QString("Shift %1").arg(shift),
        debutArret,
        finArret,
        arretSecs,
        causeStr
    );
}

// Nouvelle fonction: charger les sessions depuis le fichier CSV
QList<InspectionSession> MainWindow::loadSessionsFromCsv() {
    QList<InspectionSession> sessions;
    auto entries = CsvManager::loadHistoryFromCsv(histCsvPath());
    
    for (const auto &entry : entries) {
        InspectionSession sess;
        // Mapper les données du CSV vers la structure
        sess.machine = entry.machineNumber;
        sess.conformes = entry.conformes;
        sess.nonConformes = entry.nonConformes;
        sess.nomOperateur = entry.nomOperateur;
        
        // Récupérer les infos d'arrêt
        DowntimeRecord dt;
        dt.startDowntime = entry.debutArret;
        dt.endDowntime = entry.finArret;
        dt.durationSeconds = entry.dureeArretSecondes;
        dt.causeArret = entry.causeArret;
        
        if (entry.debutArret.isValid()) {
            sess.downtimes.append(dt);
        }
        
        sessions.append(sess);
    }
    return sessions;
}

// ──────────────────────────────────────────────────────────────────────────────
// 2. AMÉLIORATIONS DES TABLEAUX - STYLE ET PRÉSENTATION
// ──────────────────────────────────────────────────────────────────────────────

// Nouvelle fonction: styliser les tableaux
void MainWindow::styleTableWidget(QTableWidget *table) {
    // Style général du tableau
    table->setStyleSheet(
        "QTableWidget {"
        "  background-color: #151e2b;"
        "  alternate-background-color: #0f1624;"
        "  gridline-color: #2e3e5d;"
        "  border: 1px solid #2e3e5d;"
        "  border-radius: 6px;"
        "}"
        
        "QTableWidget::item {"
        "  padding: 6px 8px;"
        "  color: #cbd5e1;"
        "  border-bottom: 1px solid #1e2a38;"
        "}"
        
        "QTableWidget::item:selected {"
        "  background-color: #1e6ba8;"
        "  color: #ffffff;"
        "}"
        
        "QHeaderView::section {"
        "  background-color: #1e2d4a;"
        "  color: #f0f6fc;"
        "  padding: 8px;"
        "  border: 1px solid #2e3e5d;"
        "  font-weight: bold;"
        "  font-size: 12px;"
        "}"
        
        "QHeaderView::section:hover {"
        "  background-color: #273852;"
        "}"
        
        "QScrollBar:vertical {"
        "  background-color: #0f1624;"
        "  width: 12px;"
        "  border: 1px solid #2e3e5d;"
        "}"
        
        "QScrollBar::handle:vertical {"
        "  background-color: #2e3e5d;"
        "  border-radius: 6px;"
        "  min-height: 20px;"
        "}"
        
        "QScrollBar::handle:vertical:hover {"
        "  background-color: #4a5a7a;"
        "}"
    );
    
    // Configuration du tableau
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSortingEnabled(true);
    table->setShowGrid(true);
    table->setGridStyle(Qt::SolidLine);
    
    // Hauteur des lignes et en-têtes
    table->verticalHeader()->setDefaultSectionSize(32);
    table->horizontalHeader()->setDefaultSectionSize(100);
    table->horizontalHeader()->setStretchLastSection(true);
    
    // Désactiver la colonne de numéro de ligne
    table->verticalHeader()->setVisible(false);
}

// Nouvelle fonction: popul les données du tableau de session avec style amélioré
void MainWindow::displaySessionsInTable(const QList<InspectionSession> &sessions) {
    if (!sessionTable) return;
    
    sessionTable->setRowCount(sessions.size());
    
    for (int row = 0; row < sessions.size(); ++row) {
        const auto &sess = sessions[row];
        
        // Colonne 0: N° Session
        auto *item0 = new QTableWidgetItem(QString::number(row + 1));
        item0->setTextAlignment(Qt::AlignCenter);
        sessionTable->setItem(row, 0, item0);
        
        // Colonne 1: Machine
        sessionTable->setItem(row, 1, new QTableWidgetItem(sess.machine));
        
        // Colonne 2: Conformes
        auto *item2 = new QTableWidgetItem(QString::number(sess.conformes));
        item2->setTextAlignment(Qt::AlignCenter);
        sessionTable->setItem(row, 2, item2);
        
        // Colonne 3: Non Conformes
        auto *item3 = new QTableWidgetItem(QString::number(sess.nonConformes));
        item3->setTextAlignment(Qt::AlignCenter);
        sessionTable->setItem(row, 3, item3);
        
        // Colonne 4: Pièces/heure
        auto *item4 = new QTableWidgetItem(
            QString::number(static_cast<int>(sess.actualCadencePerHour()))
        );
        item4->setTextAlignment(Qt::AlignCenter);
        sessionTable->setItem(row, 4, item4);
        
        // Colonne 5: Opérateur
        QString nomOp = (sess.nomOperateur + " " + sess.prenomOperateur).trimmed();
        sessionTable->setItem(row, 5, new QTableWidgetItem(nomOp));
        
        // Colonne 6: Temps d'arrêt
        qint64 arretSecs = sess.totalDowntimeSecs();
        QString dureeStr = QString("%1:%2:%3")
            .arg(arretSecs/3600, 2, 10, QChar('0'))
            .arg((arretSecs%3600)/60, 2, 10, QChar('0'))
            .arg(arretSecs%60, 2, 10, QChar('0'));
        auto *itemArret = new QTableWidgetItem(dureeStr);
        itemArret->setTextAlignment(Qt::AlignCenter);
        sessionTable->setItem(row, 6, itemArret);
        
        // Colonne 7: Cause d'arrêt
        QStringList causes;
        for (const auto &dt : sess.downtimes) {
            if (!dt.causeArret.isEmpty()) {
                causes << dt.causeArret;
            }
        }
        QString causeStr = causes.isEmpty() ? "—" : causes.join(", ");
        sessionTable->setItem(row, 7, new QTableWidgetItem(causeStr));
    }
    
    // Redimensionner les colonnes
    sessionTable->resizeColumnsToContents();
    
    // Styles des lignes alternées
    for (int row = 0; row < sessionTable->rowCount(); ++row) {
        QColor bgColor;
        if (row % 2 == 0) {
            bgColor = QColor(15, 22, 36);  // #0f1624
        } else {
            bgColor = QColor(21, 30, 43);  // #151e2b
        }
        
        for (int col = 0; col < sessionTable->columnCount(); ++col) {
            auto item = sessionTable->item(row, col);
            if (item) {
                item->setBackground(bgColor);
            }
        }
    }
    
    // Trier par numéro de session (descendant)
    sessionTable->sortItems(0, Qt::DescendingOrder);
}

// Mettre à jour setupUI pour styliser les tableaux:
void MainWindow::setupSessionTableDialog() {
    sessionTableDialog = new QDialog(this);
    sessionTableDialog->setWindowTitle("📋 Historique des Sessions");
    sessionTableDialog->setMinimumSize(1000, 600);
    sessionTableDialog->setStyleSheet(
        "QDialog{"
        "  background-color: #0f1624;"
        "  border: 1px solid #2e3e5d;"
        "}"
        "QLabel{"
        "  color: #f0f6fc;"
        "  font-size: 14px;"
        "}"
    );
    
    QVBoxLayout *layout = new QVBoxLayout(sessionTableDialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    
    // Titre
    QLabel *title = new QLabel("📋 Tableau des Sessions");
    title->setStyleSheet("color: #27ae60; font-size: 16px; font-weight: bold;");
    layout->addWidget(title);
    
    // Tableau
    sessionTable = new QTableWidget();
    sessionTable->setColumnCount(8);
    sessionTable->setHorizontalHeaderLabels({
        "N° Session", "Machine", "Conformes", "Non Conformes",
        "Pièces/h", "Opérateur", "Temps Arrêt", "Cause Arrêt"
    });
    
    styleTableWidget(sessionTable);
    layout->addWidget(sessionTable);
    
    // Boutons d'action
    QHBoxLayout *btnLayout = new QHBoxLayout();
    
    QPushButton *refreshBtn = new QPushButton("🔄 Rafraîchir");
    refreshBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #2e5f8a;"
        "  color: #f0f6fc;"
        "  border: 1px solid #1e3e5a;"
        "  border-radius: 6px;"
        "  padding: 8px 16px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #3a7aaa;"
        "}"
    );
    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
        auto sessions = loadSessionsFromCsv();
        displaySessionsInTable(sessions);
    });
    btnLayout->addWidget(refreshBtn);
    
    btnLayout->addStretch();
    
    QPushButton *closeBtn = new QPushButton("Fermer");
    closeBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #374151;"
        "  color: #9ca3af;"
        "  border: 1px solid #4b5563;"
        "  border-radius: 6px;"
        "  padding: 8px 16px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #4b5563;"
        "  color: #f0f6fc;"
        "}"
    );
    connect(closeBtn, &QPushButton::clicked, sessionTableDialog, &QDialog::close);
    btnLayout->addWidget(closeBtn);
    
    layout->addLayout(btnLayout);
    
    sessionTableDialog->setLayout(layout);
}

// ──────────────────────────────────────────────────────────────────────────────
// 3. AMÉLIORATION DE LA STRUCTURE INSPECTIONSESSION
// ──────────────────────────────────────────────────────────────────────────────

// À ajouter dans mainwindow.h - struct InspectionSession:
struct InspectionSession {
    QString reference;
    QString machine;
    int cadencePerHour = 0;
    int targetNumberFixe = 0;  // ← NOUVEAU: Nombre de cible fixé au début
    int conformes = 0;
    int nonConformes = 0;
    QDateTime startTime;
    QDateTime endTime;
    QList<DowntimeRecord> downtimes;
    QString nomOperateur;
    QString prenomOperateur;
    QString idOperateur;
    
    // ... autres méthodes existantes ...
};

// Quand on démarre une session, définir le nombre de cible fixe:
void MainWindow::onStartInspection() {
    // ... code existant ...
    
    curSess.startTime = QDateTime::currentDateTime();
    curSess.targetNumberFixe = curSess.targetPerShift();  // ← FIXER LE NOMBRE
    
    // ... reste du code ...
}

// Utiliser targetNumberFixe dans les affichages:
void MainWindow::updateCounters() {
    // ...
    int targetFixed = curSess.targetNumberFixe > 0 
        ? curSess.targetNumberFixe 
        : curSess.targetPerShift();
    lbRest->setText(QString::number(targetFixed - curSess.totalInspected()));
    // ...
}

// ──────────────────────────────────────────────────────────────────────────────
// 4. INIT DANS LE CONSTRUCTEUR
// ──────────────────────────────────────────────────────────────────────────────

// À ajouter dans le constructeur MainWindow::MainWindow():
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    // ... code existant ...
    
    setupUI();
    setupCsvWatcher();  // ← NOUVEAU
    setupSessionTableDialog();  // ← NOUVEAU
    
    // ... reste du code ...
}

