#include "xlsxreader.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtGlobal>

static bool extractXlsxZip(const QString &xlsxPath, const QString &destDir, QString *errorMsg)
{
#ifdef Q_OS_WIN
    const QString script = QString(
        "Add-Type -AssemblyName System.IO.Compression.FileSystem; "
        "[System.IO.Compression.ZipFile]::ExtractToDirectory('%1', '%2')"
    ).arg(QString(xlsxPath).replace("'", "''"),
          QString(destDir).replace("'", "''"));

    QProcess proc;
    proc.start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script});
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
        if (errorMsg)
            *errorMsg = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return false;
    }
    return true;
#else
    Q_UNUSED(xlsxPath)
    Q_UNUSED(destDir)
    if (errorMsg)
        *errorMsg = "Lecture Excel disponible sous Windows uniquement.";
    return false;
#endif
}

static QVector<QString> loadSharedStrings(const QString &xmlPath)
{
    QVector<QString> strings;
    QFile f(xmlPath);
    if (!f.open(QIODevice::ReadOnly))
        return strings;

    QXmlStreamReader xml(&f);
    QString current;
    bool inSi = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == QStringLiteral("si"))
                inSi = true;
            else if (xml.name() == QStringLiteral("t") && inSi)
                current += xml.readElementText();
        } else if (xml.isEndElement() && xml.name() == QStringLiteral("si")) {
            strings.append(current);
            current.clear();
            inSi = false;
        }
    }
    return strings;
}

static QString cellValue(QXmlStreamReader &xml, const QVector<QString> &sharedStrings)
{
    const auto attrs = xml.attributes();
    const QString type = attrs.value(QStringLiteral("t")).toString();
    QString value;
    while (xml.readNext()) {
        if (xml.isEndElement() && xml.name() == QStringLiteral("c"))
            break;
        if (xml.isStartElement() && (xml.name() == QStringLiteral("v") || xml.name() == QStringLiteral("t") || xml.name() == QStringLiteral("is"))) {
            if (xml.name() == QStringLiteral("is")) {
                // Handle inline strings
                while (xml.readNext()) {
                    if (xml.isEndElement() && xml.name() == QStringLiteral("is")) break;
                    if (xml.isStartElement() && xml.name() == QStringLiteral("t"))
                        value += xml.readElementText();
                }
            } else {
                value = xml.readElementText();
            }
        }
    }
    if (type == QLatin1String("s")) {
        bool ok = false;
        const int idx = value.toInt(&ok);
        if (ok && idx >= 0 && idx < sharedStrings.size())
            return sharedStrings.at(idx);
    }
    return value.trimmed();
}

static int columnIndexFromCellRef(const QString &cellRef)
{
    int col = 0;
    for (const QChar &ch : cellRef) {
        if (ch.isLetter())
            col = col * 26 + (ch.toUpper().unicode() - QChar('A').unicode() + 1);
        else
            break;
    }
    return qMax(0, col - 1);
}

bool readXlsxFirstSheet(const QString &filePath, XlsxSheet &out, QString *errorMsg)
{
    out.rows.clear();
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        if (errorMsg) *errorMsg = "Impossible de créer un dossier temporaire.";
        return false;
    }
    if (!extractXlsxZip(filePath, tmp.path(), errorMsg))
        return false;

    const QString sharedPath = tmp.filePath("xl/sharedStrings.xml");
    const QString sheetPath  = tmp.filePath("xl/worksheets/sheet1.xml");
    if (!QFile::exists(sheetPath)) {
        if (errorMsg) *errorMsg = "Feuille sheet1 introuvable dans le fichier Excel.";
        return false;
    }

    const QVector<QString> sharedStrings = loadSharedStrings(sharedPath);

    QFile sheetFile(sheetPath);
    if (!sheetFile.open(QIODevice::ReadOnly)) {
        if (errorMsg) *errorMsg = "Impossible d'ouvrir la feuille Excel.";
        return false;
    }

    QXmlStreamReader xml(&sheetFile);
    int currentRow = -1;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("row")) {
            currentRow = xml.attributes().value("r").toInt() - 1;
            if (currentRow < 0)
                currentRow = out.rows.size();
            if (currentRow >= out.rows.size())
                out.rows.resize(currentRow + 1);
        } else if (xml.isStartElement() && xml.name() == QStringLiteral("c")) {
            const QString ref = xml.attributes().value("r").toString();
            const int col = columnIndexFromCellRef(ref);
            const QString val = cellValue(xml, sharedStrings);
            if (currentRow < 0)
                currentRow = 0;
            if (currentRow >= out.rows.size())
                out.rows.resize(currentRow + 1);
            QStringList &row = out.rows[currentRow];
            if (col >= row.size())
                row.resize(col + 1);
            row[col] = val;
        }
    }

    if (out.rows.isEmpty()) {
        if (errorMsg) *errorMsg = "Le fichier Excel ne contient aucune donnée.";
        return false;
    }
    return true;
}
