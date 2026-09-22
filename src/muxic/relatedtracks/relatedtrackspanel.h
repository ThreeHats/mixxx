#pragma once

#include <QWidget>

#include "muxic/relatedtracks/decktrack.h"
#include "muxic/relatedtracks/relatedtrackstablemodel.h"
#include "preferences/usersettings.h"
#include "util/parented_ptr.h"

class KeyboardEventFilter;
class Library;
class QLabel;
class QRadioButton;
class QSplitter;
class WLibrary;
class WTrackTableView;

namespace muxic {

class PanelPlacement;
class RelatedDeckWatcher;

/// A second track table under the library table, with the tracks that go
/// with the tracks on the decks.
///
/// The panel lives in the library area of the skin, thus it takes the
/// style of the skin. `[Skin],show_related_tracks_panel` shows and hides it.
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

  private slots:
    void slotUpdateNeeded();
    void slotModeChanged();

  private:
    /// Gives the splitter of the skin to the panel, which keeps its place.
    void setSplitter(QSplitter* pSplitter);
    void updateStatusLabel(const DeckTrackList& deckTracks);

    const UserSettingsPointer m_pConfig;
    Library* const m_pLibrary;
    parented_ptr<WTrackTableView> m_pTrackTable;
    /// The table view owns the model, thus the view saves its column
    /// layout before the model goes.
    parented_ptr<RelatedTracksTableModel> m_pModel;
    parented_ptr<QLabel> m_pStatusLabel;
    parented_ptr<QRadioButton> m_pRelatedButton;
    parented_ptr<QRadioButton> m_pSuggestionsButton;
    parented_ptr<RelatedDeckWatcher> m_pDeckWatcher;
    parented_ptr<PanelPlacement> m_pPlacement;
    /// True while the table view holds the model of the panel.
    bool m_modelIsLoaded;
};

} // namespace muxic
