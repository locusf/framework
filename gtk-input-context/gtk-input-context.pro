include(../config.pri)

TEMPLATE = subdirs
CONFIG += ordered

system(pkg-config gtk+-2.0) {
    SUBDIRS *= src
    SUBDIRS += client-gtk
}

system(pkg-config gtk+-3.0) {
    SUBDIRS *= src
    SUBDIRS += client-gtk3
}
