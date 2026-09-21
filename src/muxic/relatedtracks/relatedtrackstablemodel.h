#pragma once

#include <QHash>
#include <QStringList>

#include "library/trackset/tracksettablemodel.h"
#include "muxic/relatedtracks/decktrack.h"
#include "muxic/relatedtracks/trackrelation.h"
#include "track/trackid.h"

namespace muxic {

class TrackRelationStorage;

/// Shows the tracks that go with a reference track.
class RelatedTracksTableModel final : public TrackSetTableModel {
    Q_OBJECT

  public:
    enum class Mode {
        /// Each track that has a relation.
        AllRelated,
        /// The tracks that a relation leads to from the reference track.
        RelatedTo,
        /// The tracks that fit the tempo and the key of the reference track.
        SuggestedFor,
        /// The tracks that a relation leads to from the track of a deck,
        /// with the deck number in a column of its own.
        RelatedToDecks,
        /// The tracks that fit the tempo and the key of the track of a
        /// deck, with the deck number in a column of its own.
        SuggestedForDecks,
    };

    RelatedTracksTableModel(QObject* pParent,
            TrackCollectionManager* pTrackCollectionManager);
    ~RelatedTracksTableModel() final = default;

    void selectAllRelated();
    void selectRelatedTo(TrackId trackId);
    void selectSuggestedFor(TrackId trackId);
    /// Shows the tracks that go with the track of each deck. A track that
    /// goes with two decks has one row per deck.
    void selectRelatedToDecks(const DeckTrackList& deckTracks);
    /// Shows the tracks that fit the tempo and the key of the track of each
    /// deck, one row per deck.
    void selectSuggestedForDecks(const DeckTrackList& deckTracks);
    /// Reads the table again with the mode and the reference track of the
    /// last select.
    void refresh();

    Mode mode() const {
        return m_mode;
    }
    TrackId referenceTrackId() const {
        return m_referenceTrackId;
    }
    /// True if the rows carry a deck number.
    bool showsDecks() const {
        return m_mode == Mode::RelatedToDecks || m_mode == Mode::SuggestedForDecks;
    }

    void removeTracks(const QModelIndexList& indices) final;

    Capabilities getCapabilities() const final;
    QString modelKey(bool noSearch) const override;
    bool isColumnHiddenByDefault(int column) override;

    /// A deck view keeps its own order, thus it takes no sort column from
    /// `[Library],sort_column` and gives none back.
    TrackModel::SortColumnId sortColumnIdFromColumnIndex(int column) const override;
    int columnIndexFromSortColumnId(TrackModel::SortColumnId sortColumn) const override;

    ///////////////////////////////////////////////////////////////////////////
    // Editing of the relation columns
    ///////////////////////////////////////////////////////////////////////////

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index,
            const QVariant& value,
            int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QAbstractItemDelegate* delegateForColumn(const int column, QObject* pParent) override;

    /// True if the rows of this view each carry one relation.
    bool showsOneRelationPerRow() const {
        return m_mode == Mode::RelatedTo && m_referenceTrackId.isValid();
    }

    /// Reads the relation of the row. Returns false without a relation.
    bool relationForIndex(const QModelIndex& index, TrackRelation* pRelation) const;

    /// Sets the direction of the relations of the rows. Returns the count
    /// of the relations that changed.
    int setRelationsBidirectional(const QModelIndexList& indices, bool bidirectional);

    /// The relation types of the table and the types that the menu offers.
    QStringList knownRelationTypes() const;

  protected:
    QString tableColumnSortExpression(int column) const override;

  private:
    void setRelationTable(const QString& tableName, const QString& viewQuery);
    void storeSearchText();
    TrackRelationStorage& storage() const;
    bool writeRelationColumn(const QModelIndex& index, const QVariant& value);
    /// The WHERE of the tracks that fit the tempo and the key of the track.
    QString formatSuggestionConditions(TrackId trackId) const;
    /// The SELECT of one deck of a view that shows the relations.
    QString formatRelatedToDeckBranch(const DeckTrack& deckTrack) const;
    /// The SELECT of one deck of a view that shows the suggestions.
    QString formatSuggestedForDeckBranch(const DeckTrack& deckTrack) const;

    Mode m_mode = Mode::AllRelated;
    TrackId m_referenceTrackId;
    /// The decks of the last select in a deck mode.
    DeckTrackList m_deckTracks;
    /// The text of the last CREATE VIEW of each view name. A view only has
    /// to go and come back when its text changes.
    QHash<QString, QString> m_viewQueries;
    /// The search text of the user, one per mode.
    QHash<int, QString> m_searchTexts;
};

} // namespace muxic
