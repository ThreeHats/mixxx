#include "muxic/relatedtracks/relatedtrackspanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_relatedtrackspanel.cpp"
#include "muxic/relatedtracks/panelplacement.h"
#include "muxic/relatedtracks/relateddeckwatcher.h"
#include "muxic/relatedtracks/trackrelationstorage.h"
#include "util/assert.h"
#include "widget/wlibrary.h"
#include "widget/wtracktableview.h"

namespace muxic {

namespace {

const ConfigKey kShowConfigKey(
        QStringLiteral("[Skin]"), QStringLiteral("show_related_tracks_panel"));

const ConfigKey kPanelHeightConfigKey(
        QStringLiteral("[Library]"), QStringLiteral("related_panel_height"));

const ConfigKey kSuggestionsConfigKey(
        QStringLiteral("[Library]"), QStringLiteral("related_panel_suggestions"));

// The height of the panel before the user moves the splitter, in pixels.
constexpr int kDefaultPanelHeight = 160;

// The skins hold a rule for this name. The panel takes the style of the
// controls row of the Auto DJ view and of the Recording view.
const QString kFeatureControlsObjectName = QStringLiteral("LibraryFeatureControls");

// The skins hold a rule for the handle of this name.
const QString kSplitterObjectName = QStringLiteral("LibrarySplitter");

} // anonymous namespace

RelatedTracksPanel::RelatedTracksPanel(QWidget* pParent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        KeyboardEventFilter* pKeyboard,
        double backgroundColorOpacity)
        : QWidget(pParent),
          m_pConfig(pConfig),
          m_pLibrary(pLibrary),
          m_modelIsLoaded(false) {
    setObjectName(QStringLiteral("RelatedTracksPanel"));

    auto* pPanelLayout = new QVBoxLayout(this);
    pPanelLayout->setContentsMargins(0, 0, 0, 0);
    pPanelLayout->setSpacing(0);

    auto* pHeader = new QWidget(this);
    pHeader->setObjectName(kFeatureControlsObjectName);
    auto* pHeaderLayout = new QHBoxLayout(pHeader);
    pHeaderLayout->setContentsMargins(4, 0, 4, 0);

    m_pStatusLabel = make_parented<QLabel>(QString(), pHeader);
    m_pRelatedButton = make_parented<QRadioButton>(tr("Related"), pHeader);
    m_pSuggestionsButton = make_parented<QRadioButton>(tr("Suggestions"), pHeader);
    m_pSuggestionsButton->setChecked(
            m_pConfig->getValue(kSuggestionsConfigKey, false));
    m_pRelatedButton->setChecked(!m_pSuggestionsButton->isChecked());
    connect(m_pSuggestionsButton,
            &QRadioButton::toggled,
            this,
            &RelatedTracksPanel::slotModeChanged);

    pHeaderLayout->addWidget(m_pStatusLabel);
    pHeaderLayout->addStretch(1);
    pHeaderLayout->addWidget(m_pRelatedButton);
    pHeaderLayout->addWidget(m_pSuggestionsButton);
    pPanelLayout->addWidget(pHeader);

    m_pTrackTable = make_parented<WTrackTableView>(
            this, pConfig, pLibrary, backgroundColorOpacity);
    // The panel is short, thus the table must be able to shrink.
    m_pTrackTable->setMinimumHeight(0);
    if (pKeyboard != nullptr) {
        m_pTrackTable->installEventFilter(pKeyboard);
    }
    pPanelLayout->addWidget(m_pTrackTable);

    m_pModel = make_parented<RelatedTracksTableModel>(m_pTrackTable,
            pLibrary->trackCollectionManager(),
            RelatedTracksTableModel::kPanelSettingsNamespace);

    // A row of the panel behaves like a row of the library table. The panel
    // never takes the model of the library table: it has a view of its own.
    connect(m_pTrackTable,
            &WTrackTableView::loadTrack,
            m_pLibrary,
            &Library::slotLoadTrack);
    connect(m_pTrackTable,
            &WTrackTableView::loadTrackToPlayer,
            m_pLibrary,
            &Library::slotLoadTrackToPlayer);
    connect(m_pLibrary,
            &Library::setTrackTableFont,
            m_pTrackTable,
            &WTrackTableView::setTrackTableFont);
    connect(m_pLibrary,
            &Library::setTrackTableRowHeight,
            m_pTrackTable,
            &WTrackTableView::setTrackTableRowHeight);
    connect(m_pLibrary,
            &Library::setSelectedClick,
            m_pTrackTable,
            &WTrackTableView::setSelectedClick);
    // The library sent its font before this panel existed.
    m_pTrackTable->setTrackTableFont(m_pLibrary->getTrackTableFont());
    m_pTrackTable->setTrackTableRowHeight(m_pLibrary->getTrackTableRowHeight());
    m_pTrackTable->setSelectedClick(m_pLibrary->selectedClickEnabled());

    TrackRelationStorage* pStorage = nullptr;
    TrackCollectionManager* pTrackCollectionManager = m_pLibrary->trackCollectionManager();
    if (pTrackCollectionManager != nullptr &&
            pTrackCollectionManager->internalCollection() != nullptr) {
        pStorage = &pTrackCollectionManager->internalCollection()->trackRelations();
    }
    m_pDeckWatcher = make_parented<RelatedDeckWatcher>(this, pStorage);
    connect(m_pDeckWatcher,
            &RelatedDeckWatcher::updateNeeded,
            this,
            &RelatedTracksPanel::slotUpdateNeeded);

    updateStatusLabel(DeckTrackList());
}

RelatedTracksPanel::~RelatedTracksPanel() = default;

// static
QWidget* RelatedTracksPanel::wrapLibraryWidget(WLibrary* pLibraryWidget,
        Library* pLibrary,
        UserSettingsPointer pConfig,
        KeyboardEventFilter* pKeyboard) {
    VERIFY_OR_DEBUG_ASSERT(pLibraryWidget != nullptr && pLibrary != nullptr) {
        return pLibraryWidget;
    }
    auto* pSplitter = new QSplitter(Qt::Vertical, pLibraryWidget->parentWidget());
    pSplitter->setObjectName(kSplitterObjectName);
    pSplitter->setChildrenCollapsible(false);
    // The skin gave its size policy to the library widget. The splitter
    // stands in its place and takes it over. <MinimumSize>, <MaximumSize>
    // and <Style> of the <Library> node stay on the library widget. No
    // skin of Mixxx sets them.
    pSplitter->setSizePolicy(pLibraryWidget->sizePolicy());

    auto* pPanel = new RelatedTracksPanel(pSplitter,
            pConfig,
            pLibrary,
            pKeyboard,
            pLibraryWidget->getTrackTableBackgroundColorOpacity());

    pSplitter->addWidget(pLibraryWidget);
    pSplitter->addWidget(pPanel);
    // The library table takes the space that a resize of the window gives.
    pSplitter->setStretchFactor(0, 1);
    pSplitter->setStretchFactor(1, 0);
    pPanel->setSplitter(pSplitter);
    return pSplitter;
}

void RelatedTracksPanel::setSplitter(QSplitter* pSplitter) {
    m_pPlacement = make_parented<PanelPlacement>(this,
            pSplitter,
            m_pConfig,
            kShowConfigKey,
            kPanelHeightConfigKey,
            kDefaultPanelHeight);
    connect(m_pPlacement,
            &PanelPlacement::shownChanged,
            m_pDeckWatcher,
            &RelatedDeckWatcher::setActive);
}

void RelatedTracksPanel::slotModeChanged() {
    m_pConfig->setValue(kSuggestionsConfigKey, m_pSuggestionsButton->isChecked());
    m_pDeckWatcher->requestUpdate();
}

void RelatedTracksPanel::slotUpdateNeeded() {
    const DeckTrackList deckTracks = m_pDeckWatcher->deckTracks();
    if (m_pSuggestionsButton->isChecked()) {
        m_pModel->selectSuggestedForDecks(deckTracks);
    } else {
        m_pModel->selectRelatedToDecks(deckTracks);
    }
    if (!m_modelIsLoaded) {
        // The columns of the two modes are the same, thus the view takes
        // the model one time only.
        m_pTrackTable->loadTrackModel(m_pModel);
        m_modelIsLoaded = true;
    }
    updateStatusLabel(deckTracks);
}

void RelatedTracksPanel::updateStatusLabel(const DeckTrackList& deckTracks) {
    if (deckTracks.isEmpty()) {
        m_pStatusLabel->setText(tr("No track on a deck"));
        return;
    }
    QStringList deckNumbers;
    deckNumbers.reserve(deckTracks.size());
    for (const DeckTrack& deckTrack : deckTracks) {
        deckNumbers.append(QString::number(deckTrack.deckNumber));
    }
    const QString decks = deckNumbers.join(QStringLiteral(", "));
    if (m_pSuggestionsButton->isChecked()) {
        m_pStatusLabel->setText(deckNumbers.size() == 1
                        ? tr("Suggestions for deck %1").arg(decks)
                        : tr("Suggestions for decks %1").arg(decks));
        return;
    }
    m_pStatusLabel->setText(deckNumbers.size() == 1
                    ? tr("Tracks that go with deck %1").arg(decks)
                    : tr("Tracks that go with decks %1").arg(decks));
}

} // namespace muxic
