#pragma once

#include <QPalette>
#include <QString>
#include <QWidget>

#include "preferences/usersettings.h"

class QVBoxLayout;

/// A window of its own for the library area of the skin. The window holds the
/// library widget of the skin while the library is detached.
class WLibraryWindow : public QWidget {
    Q_OBJECT
  public:
    explicit WLibraryWindow(UserSettingsPointer pConfig);
    ~WLibraryWindow() override;

    /// Put the library area of the skin in the window.
    void adoptLibraryWidget(QWidget* pLibraryWidget);

    /// Give the window the style and the colors of the main window, because a
    /// window does not get the style sheet of the skin from a parent.
    void applyStyle(const QString& baseStyleSheet,
            const QString& skinStyleSheet,
            const QPalette& palette);

    /// Put the window where it was, on the screen that it was on. If that
    /// screen is absent, put the window on the primary screen.
    void restorePlacement();
    void savePlacement();

  signals:
    void closeRequested();

  protected:
    void closeEvent(QCloseEvent* pEvent) override;

  private:
    const UserSettingsPointer m_pConfig;
    QVBoxLayout* m_pLayout;
};
