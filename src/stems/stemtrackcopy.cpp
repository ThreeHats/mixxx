#include "stems/stemtrackcopy.h"

#include "track/cue.h"
#include "track/track.h"
#include "util/assert.h"

namespace mixxx {

BeatsPointer rescaleBeats(const BeatsPointer& pBeats, audio::SampleRate sampleRate) {
    if (!pBeats || !sampleRate.isValid()) {
        return pBeats;
    }
    const audio::SampleRate sourceSampleRate = pBeats->getSampleRate();
    if (!sourceSampleRate.isValid() || sourceSampleRate == sampleRate) {
        return pBeats;
    }
    const double ratio = sampleRate / sourceSampleRate;

    std::vector<BeatMarker> markers;
    markers.reserve(pBeats->getMarkers().size());
    for (const BeatMarker& marker : pBeats->getMarkers()) {
        markers.emplace_back(
                audio::FramePos(marker.position().value() * ratio).toLowerFrameBoundary(),
                marker.beatsTillNextMarker());
    }
    return Beats::fromBeatMarkers(sampleRate,
            markers,
            audio::FramePos(pBeats->getLastMarkerPosition().value() * ratio)
                    .toLowerFrameBoundary(),
            pBeats->getLastMarkerBpm(),
            pBeats->getSubVersion());
}

void copyTrackToStem(const Track& sourceTrack, Track* pStemTrack) {
    VERIFY_OR_DEBUG_ASSERT(pStemTrack) {
        return;
    }
    const audio::SampleRate sourceSampleRate = sourceTrack.getSampleRate();
    audio::SampleRate stemSampleRate = pStemTrack->getSampleRate();
    if (!stemSampleRate.isValid()) {
        stemSampleRate = sourceSampleRate;
    }

    // The grid and the BPM need an unlocked track.
    pStemTrack->setBpmLocked(false);

    pStemTrack->setTitle(sourceTrack.getTitle());
    pStemTrack->setArtist(sourceTrack.getArtist());
    pStemTrack->setAlbum(sourceTrack.getAlbum());
    pStemTrack->setAlbumArtist(sourceTrack.getAlbumArtist());
    pStemTrack->setComposer(sourceTrack.getComposer());
    pStemTrack->setGrouping(sourceTrack.getGrouping());
    pStemTrack->setYear(sourceTrack.getYear());
    pStemTrack->setTrackNumber(sourceTrack.getTrackNumber());
    pStemTrack->setTrackTotal(sourceTrack.getTrackTotal());
    pStemTrack->updateGenre(sourceTrack.getGenre());
    pStemTrack->setComment(sourceTrack.getComment());
    pStemTrack->setColor(sourceTrack.getColor());
    pStemTrack->setRating(sourceTrack.getRating());
    pStemTrack->setReplayGain(sourceTrack.getReplayGain());
    pStemTrack->setKeys(sourceTrack.getKeys());

    const BeatsPointer pBeats = sourceTrack.getBeats();
    if (pBeats) {
        pStemTrack->trySetBeats(rescaleBeats(pBeats, stemSampleRate));
    } else if (sourceTrack.getBpm() > 0) {
        pStemTrack->trySetBpm(sourceTrack.getBpm());
    }

    if (sourceSampleRate.isValid() && stemSampleRate.isValid()) {
        QList<CuePointer> stemCues;
        const QList<CuePointer> sourceCues = sourceTrack.getCuePoints();
        stemCues.reserve(sourceCues.size());
        for (const CuePointer& pSourceCue : sourceCues) {
            stemCues.append(CuePointer(new Cue(
                    pSourceCue->getCueInfo(sourceSampleRate), stemSampleRate, true)));
        }
        pStemTrack->setCuePoints(stemCues);
    }

    pStemTrack->setBpmLocked(sourceTrack.isBpmLocked());
}

} // namespace mixxx
