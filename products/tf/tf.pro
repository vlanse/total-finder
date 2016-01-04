TARGET = tf
DESCRIPTION = Total Finder File Manager
TEMPLATE = app
CONFIG += release

QT += widgets

CONFIG += c++11

PATH_STEP = ../..

DESTDIR = $${PATH_STEP}/.shadow/bin
UI_DIR = $${PATH_STEP}/.shadow/ui
OBJECTS_DIR = $${PATH_STEP}/.shadow/obj
MOC_DIR = $${PATH_STEP}/.shadow/moc

# Application section

FORMS += \
  ui/dir_view_panel.ui \
  ui/main_window.ui \
  ui/settings_dialog.ui \

HEADERS += \
  dir_model.h \
  dir_view_panel.h \
  event_filters.h \
  main_window.h \
  settings.h \
  settings_dialog.h \
  tab_manager.h \

SOURCES += \
  dir_model.cpp \
  dir_view_panel.cpp \
  event_filters.cpp \
  main.cpp \
  main_window.cpp \
  settings.cpp \
  settings_dialog.cpp \
  tab_manager.cpp \

# Platform section
