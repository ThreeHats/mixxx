// Tests for tableview-related things
// Right now it's just testing the serialize-unserialize of the header state code.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <QStandardItemModel>
#include <QtDebug>

#include "library/browse/browsetablemodel.h"
#include "library/proxytrackmodel.h"
#include "library/trackmodel.h"
#include "proto/headers.pb.h"
#include "test/librarytest.h"
#include "widget/wtracktableviewheader.h"

class HeaderViewStateTest : public testing::Test {
};

TEST_F(HeaderViewStateTest, RoundTrip) {
    mixxx::library::HeaderViewState headerViewState_pb;
    mixxx::library::HeaderViewState::HeaderState* header_state_pb =
                headerViewState_pb.add_header_state();

    header_state_pb->set_hidden(true);
    header_state_pb->set_size(50);
    header_state_pb->set_logical_index(10);
    header_state_pb->set_visual_index(2);
    header_state_pb->set_column_name("MyCol");

    header_state_pb =
                headerViewState_pb.add_header_state();

    header_state_pb->set_hidden(false);
    header_state_pb->set_size(22);
    header_state_pb->set_logical_index(6);
    header_state_pb->set_visual_index(3);
    header_state_pb->set_column_name("MyOtherCol");

    headerViewState_pb.set_sort_indicator_shown(true);
    headerViewState_pb.set_sort_indicator_section(1);
    headerViewState_pb.set_sort_order(Qt::DescendingOrder);

    // Create a HeaderViewState based on the proto.
    HeaderViewState view_state(headerViewState_pb);

    // Get a serialized form of the state.
    QString saved_state = view_state.saveState();

    // Initialize a new state object with the saved state.
    HeaderViewState loaded_state(saved_state);

    // Compare the old saved state with the new one.
    ASSERT_EQ(saved_state, loaded_state.saveState());

    // Ensure that the serialization is not bullshit.
    ASSERT_NE("", saved_state);
}

TEST_F(HeaderViewStateTest, GoodHeaderState) {
    const QString kGoodSerializedProto("ChEIARAAGAEgACoHcHJldmlldwoICAAQSxgEIAE"
            "KEggBEAAYAiACKghjb3ZlcmFydAoICAEQABgDIAMKCAgBEAAYBSAECggIABBaGBYgB"
            "QoICAEQABgGIAYKCQgAEPwBGBcgBwoJCAAQvAEYByAICgkIABDmARgIIAkKCAgBEAA"
            "YCSAKCggIARAAGAogCwoJCAAQkwQYGSAMCggIARAAGAwgDQoICAEQABgNIA4KCAgBE"
            "AAYDiAPCggIARAAGBAgEAoICAAQVxgRIBEKCAgBEAAYEiASCggIABBGGBMgEwoICAA"
            "QMhgPIBQKCAgAEHUYCyAVCggIARAAGBQgFgoICAAQNxgVIBcKCAgBEAAYGCAYCggIA"
            "RAAGBogGQoICAEQABgbIBoKCAgBEAAYHCAbCggIARAAGAAgHAoICAEQABgdIB0KCAg"
            "BEAAYHiAeEAEYFiAB");

    HeaderViewState view_state(kGoodSerializedProto);
    ASSERT_TRUE(view_state.healthy());
    ASSERT_EQ(kGoodSerializedProto, view_state.saveState());
}

TEST_F(HeaderViewStateTest, BadHeaderState) {
    HeaderViewState view_state("BLAHBLAHBLAHBAD");
    ASSERT_FALSE(view_state.healthy());
}

namespace {

/// A model with named columns, enough for HeaderViewState::restoreState().
class NamedColumnModel : public QStandardItemModel {
  public:
    explicit NamedColumnModel(const QStringList& columnNames)
            : QStandardItemModel(0, columnNames.size()) {
        for (int i = 0; i < columnNames.size(); ++i) {
            setHeaderData(i, Qt::Horizontal, columnNames.at(i), TrackModel::kHeaderNameRole);
            setHeaderData(i, Qt::Horizontal, columnNames.at(i), Qt::DisplayRole);
        }
    }
};

void addHeaderState(mixxx::library::HeaderViewState* pState,
        const char* columnName,
        bool hidden,
        int logicalIndex,
        int visualIndex) {
    mixxx::library::HeaderViewState::HeaderState* pHeader = pState->add_header_state();
    pHeader->set_hidden(hidden);
    pHeader->set_size(60);
    pHeader->set_logical_index(logicalIndex);
    pHeader->set_visual_index(visualIndex);
    pHeader->set_column_name(columnName);
}

} // namespace

TEST_F(HeaderViewStateTest, RestoreIgnoresAnUnknownColumn) {
    // A state saved by another build holds a column that this model does not
    // have, at an index that this model does have. The restore must not apply
    // that entry to whatever column now sits at the index.
    // The entry of the column that went away comes last, thus its index wins
    // over the entry that the model does have.
    mixxx::library::HeaderViewState state_pb;
    addHeaderState(&state_pb, "artist", false, 0, 0);
    addHeaderState(&state_pb, "title", false, 2, 1);
    addHeaderState(&state_pb, "gone_column", true, 2, 2);

    NamedColumnModel model({QStringLiteral("artist"),
            QStringLiteral("bpm"),
            QStringLiteral("title")});
    WTrackTableViewHeader header(Qt::Horizontal);
    header.setModel(&model);

    HeaderViewState view_state(state_pb);
    view_state.restoreState(&header, false);

    EXPECT_FALSE(header.isSectionHidden(0)); // artist, from the state
    EXPECT_FALSE(header.isSectionHidden(1)); // bpm, unknown to the state
    EXPECT_FALSE(header.isSectionHidden(2)); // title, not the gone column
}

class ProxyTrackModelHeaderTest : public LibraryTest {
};

TEST_F(ProxyTrackModelHeaderTest, ProxyForwardsTheCommonColumnLayout) {
    // The Computer and the Recording view put a proxy between the header and
    // the track model.
    BrowseTableModel browseModel(
            nullptr, trackCollectionManager(), nullptr, "mixxx.db.model.browse");
    ProxyTrackModel proxyModel(&browseModel, true);

    // A file browser holds no track set, thus it cannot take a layout that a
    // track table saved.
    EXPECT_FALSE(browseModel.canLoadTrackSetColumns());
    EXPECT_FALSE(proxyModel.canLoadTrackSetColumns());

    // The proxy has no database of its own. Without the forward the write goes
    // to an invalid connection and is lost.
    const QString state = QStringLiteral("a-serialized-state");
    EXPECT_TRUE(proxyModel.setCommonHeaderState(state));
    EXPECT_EQ(state, proxyModel.getCommonHeaderState());
    EXPECT_EQ(state, browseModel.getCommonHeaderState());
}
