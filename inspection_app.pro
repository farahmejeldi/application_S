QT += core gui widgets

CONFIG += c++17

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    xlsxreader.cpp

HEADERS += \
    mainwindow.h \
    xlsxreader.h

OTHER_FILES += \
    product_config.csv \
    Operators.csv \
    historique.csv \
    machine_info.json \
    lear_background.png

win32 {
    PRODUCT_CSV   = $$shell_path($$PWD/product_config.csv)
    OPERATORS_CSV = $$shell_path($$PWD/Operators.csv)
    HIST_CSV      = $$shell_path($$PWD/historique.csv)
    MACHINE_JSON  = $$shell_path($$PWD/machine_info.json)
    BG_IMAGE      = $$shell_path($$PWD/lear_background.png)

    QMAKE_POST_LINK += $$quote(copy /Y \"$$PRODUCT_CSV\"   \"$$shell_path($$OUT_PWD)\" $$escape_expand(\\n\\t))
    QMAKE_POST_LINK += $$quote(copy /Y \"$$OPERATORS_CSV\" \"$$shell_path($$OUT_PWD)\" $$escape_expand(\\n\\t))
    # Copier historique.csv seulement s'il n'existe pas encore dans OUT_PWD
    QMAKE_POST_LINK += $$quote(if not exist \"$$shell_path($$OUT_PWD/historique.csv)\" copy /Y \"$$HIST_CSV\" \"$$shell_path($$OUT_PWD)\" $$escape_expand(\\n\\t))
    # Copier machine_info.json seulement s'il n'existe pas encore dans OUT_PWD (pour ne pas écraser les machines ajoutées)
    QMAKE_POST_LINK += $$quote(if not exist \"$$shell_path($$OUT_PWD/machine_info.json)\" copy /Y \"$$MACHINE_JSON\" \"$$shell_path($$OUT_PWD)\" $$escape_expand(\\n\\t))
    # Image de fond — toujours copiée/mise à jour à côté de l'exécutable
    QMAKE_POST_LINK += $$quote(copy /Y \"$$BG_IMAGE\" \"$$shell_path($$OUT_PWD)\" $$escape_expand(\\n\\t))
}

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin

!isEmpty(target.path): INSTALLS += target
