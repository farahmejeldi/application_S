#ifndef XLSXREADER_H
#define XLSXREADER_H

#include <QString>
#include <QStringList>
#include <QVector>

struct XlsxSheet {
    QVector<QStringList> rows;
};

bool readXlsxFirstSheet(const QString &filePath, XlsxSheet &out, QString *errorMsg = nullptr);

#endif
