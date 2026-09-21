#pragma once

#include <QPointer>
#include <QWidget>
#include <memory>

#include "muxic/relatedtracks/decktrack.h"
#include "muxic/relatedtracks/relatedtrackstablemodel.h"
#include "preferences/usersettings.h"
#include "util/parented_ptr.h"

class ControlProxy;
class KeyboardEventFilter;
class Library;
class QLabel;
class QRadioButton;
class QSplitter;
class WLibrary;
class WTrackTableView;

namespace muxic {

class RelatedDeckWatcher;

/// A second track table under the library table, with the tracks that go
/// with the tracks on the decks.
///
/// The panel lives in the library area of the skin, thus it follows the
/// library into a window of its own and it takes the style of the skin.
/// The control `[Skin],show_related_tracks_panel` shows and hides it.
class RelatedTracksPanel : public QWidget {
    Q_OBJECT

  public:
    RelatedTracksPanel(QWidget* pParent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            KeyboardEventFilter* pKeyboard,
            double backgroundColorOpacity);
    ~RelatedTracksPanel() override;

    /// Puts the panel under the library widget of a skin, in a splitter.
    /// Returns the widget that takes the place of the library widget.
    static QWidget* wrapLibraryWidget(WLibrary* pLibraryWidget,
            Library* pLibrary,
            UserSettingsPointer pConfig,
            KeyboardEventFilter* pKeyboard);

  protected:
    void showEvent(QShowEvent* pEvent) override;
    void hideEvent(QHideEvent* pEvent) override;

  private slots:
    void slotShowControlChanged(double value);
    void slotUpdateNeeded();
    void slotModeChanged();
    void slotSplitterMoved();

  private:
    /// Gives the splitter of the skin to the panel, which keeps its height.
    void setSplitter(QSplitter* pSplitter);
    void restoreSplitHeight();
    void updateStatusLabel(const DeckTrackList& deckTracks);

    const UserSettingsPointer m_pConfig;
    Library* const m_pLibrary;
    RelatedTracksTableModel m_model;
    parented_ptr<WTrackTableView> m_pTrackTable;
    parented_ptr<QLabel> m_pStatusLabel;
    parented_ptr<QRadioButton> m_pRelatedButton;
    parented_ptr<QRadioButton> m_pSuggestionsButton;
    parented_ptr<RelatedDeckWatcher> m_pDeckWatcher;
    std::unique_ptr<ControlProxy> m_pShowControl;
    QPointer<QSplitter> m_pSplitter;
    /// True while the table view holds the model of the panel.
    bool m_modelIsLoaded;
};

} // namespace muxic
