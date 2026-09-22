#include "analyzer/analyzertrack.h"

#include "util/assert.h"

AnalyzerTrack::AnalyzerTrack(TrackPointer track)
        : AnalyzerTrack(std::move(track), Options()) {
}

AnalyzerTrack::AnalyzerTrack(TrackPointer track, Options options)
        : m_track(std::move(track)),
          m_options(options) {
    DEBUG_ASSERT(m_track);
}

const TrackPointer& AnalyzerTrack::getTrack() const {
    return m_track;
}

const AnalyzerTrack::Options& AnalyzerTrack::getOptions() const {
    return m_options;
}
