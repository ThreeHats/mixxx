#include "waveform/renderers/allshader/waveformrendererstem.h"

#include <QFont>
#include <QImage>
#include <QOpenGLTexture>

#include "control/controlproxy.h"
#include "engine/channels/enginedeck.h"
#include "engine/engine.h"
#include "rendergraph/material/rgbamaterial.h"
#include "rendergraph/vertexupdaters/rgbavertexupdater.h"
#include "track/track.h"
#include "util/assert.h"
#include "util/math.h"
#include "waveform/renderers/stemwaveformscale.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "waveform/waveform.h"
#include "waveform/waveformwidgetfactory.h"

namespace {
#ifdef __SCENEGRAPH__
// FIXME this is a workaround an issue with waveform only drawing partially in
// SG. The workaround is to reduce the the number of vertices, by reducing the
// precision of waveform strips.
const float kPixelPerStrip = 2;
#else
const float kPixelPerStrip = 1;
#endif
} // namespace

using namespace rendergraph;

namespace allshader {

WaveformRendererStem::WaveformRendererStem(
        WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type,
        ::WaveformRendererSignalBase::Options options)
        : WaveformRendererSignalBase(waveformWidget, options),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip),
          m_splitStemTracks(false),
          m_outlineOpacity(0.15f),
          m_opacity(0.75f) {
    initForRectangles<RGBAMaterial>(0);
    setUsePreprocess(true);
}

void WaveformRendererStem::onSetup(const QDomNode&) {
}

bool WaveformRendererStem::init() {
    m_pStemGain.clear();
    m_pStemMute.clear();

    // Keep a valid drawing order even for displays that provide a static track
    // without a channel group. The stem data itself is independent of the
    // per-stem volume/mute controls, so those controls are optional.
    m_stackOrder.resize(mixxx::kMaxSupportedStems);
    std::iota(m_stackOrder.begin(), m_stackOrder.end(), 0);

    if (m_waveformRenderer->getGroup().isEmpty()) {
        return true;
    }
    for (int stemIdx = 0; stemIdx < mixxx::kMaxSupportedStems; stemIdx++) {
        QString stemGroup = EngineDeck::getGroupForStem(m_waveformRenderer->getGroup(), stemIdx);
        m_pStemGain.emplace_back(
                std::make_unique<ControlProxy>(stemGroup,
                        QStringLiteral("volume")));
        m_pStemMute.emplace_back(
                std::make_unique<ControlProxy>(stemGroup,
                        QStringLiteral("mute")));
        auto bringToForeground = [this, stemIdx](double) {
            if (!m_reorderOnChange) {
                return;
            }
            m_stackOrder.removeAll(stemIdx);
            m_stackOrder.append(stemIdx);
        };
        m_pStemGain.back()->connectValueChanged(this, bringToForeground);
        m_pStemMute.back()->connectValueChanged(this, bringToForeground);
    }

#ifndef __SCENEGRAPH__
    auto* pWaveformWidgetFactory = WaveformWidgetFactory::instance();
    setSplitStemTracks(pWaveformWidgetFactory->isStemSplitTracks());
    connect(pWaveformWidgetFactory,
            &WaveformWidgetFactory::stemSplitTracksChanged,
            this,
            &WaveformRendererStem::setSplitStemTracks);
    setReorderOnChange(pWaveformWidgetFactory->isStemReorderOnChange());
    connect(pWaveformWidgetFactory,
            &WaveformWidgetFactory::stemReorderOnChangeChanged,
            this,
            &WaveformRendererStem::setReorderOnChange);
    setOutlineOpacity(pWaveformWidgetFactory->getStemOutlineOpacity());
    connect(pWaveformWidgetFactory,
            &WaveformWidgetFactory::stemOutlineOpacityChanged,
            this,
            &WaveformRendererStem::setOutlineOpacity);
    setOpacity(pWaveformWidgetFactory->getStemOpacity());
    connect(pWaveformWidgetFactory,
            &WaveformWidgetFactory::stemOpacityChanged,
            this,
            &WaveformRendererStem::setOpacity);
#endif
    return true;
}

void WaveformRendererStem::preprocess() {
    if (!preprocessInner()) {
        if (geometry().vertexCount() != 0) {
            geometry().allocate(0);
            markDirtyGeometry();
        }
    }
}

bool WaveformRendererStem::preprocessInner() {
    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();

    if (!pTrack || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return false;
    }

    auto stemInfo = pTrack->getStemInfo();
    // If this track isn't a stem track, skip the rendering
    if (stemInfo.isEmpty()) {
        return false;
    }
    auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                         : ::WaveformRendererAbstract::Play;

    ConstWaveformPointer waveform = pTrack->getWaveform();
    if (waveform.isNull()) {
        return false;
    }

    const int dataSize = waveform->getDataSize();
    if (dataSize <= 1) {
        return false;
    }

    const WaveformData* data = waveform->data();
    if (data == nullptr) {
        return false;
    }
    // If this waveform doesn't contain stem data, skip the rendering
    if (!waveform->hasStem()) {
        return false;
    }

    uint selectedStems = m_waveformRenderer->getSelectedStems();

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
    const int length = static_cast<int>(m_waveformRenderer->getLength());
    const int pixelLength = static_cast<int>(m_waveformRenderer->getLength() * devicePixelRatio);
    const int stripLength = static_cast<int>(static_cast<float>(pixelLength) / kPixelPerStrip);
    const float invDevicePixelRatio = kPixelPerStrip / devicePixelRatio;
    const float halfStripSize = kPixelPerStrip / 2.0f / devicePixelRatio;

    // See waveformrenderersimple.cpp for a detailed explanation of the frame and index calculation
    const int visualFramesSize = dataSize / 2;
    const double firstVisualFrame =
            m_waveformRenderer->getFirstDisplayedPosition(positionType) * visualFramesSize;
    const double lastVisualFrame =
            m_waveformRenderer->getLastDisplayedPosition(positionType) * visualFramesSize;

    // Represents the # of visual frames per horizontal pixel.
    const double visualIncrementPerPixel =
            (lastVisualFrame - firstVisualFrame) / static_cast<double>(stripLength);

    // Per-band gain from the EQ knobs.
    float allGain(1.0);
    getGains(&allGain, nullptr, nullptr, nullptr);

    const float breadth = static_cast<float>(m_waveformRenderer->getBreadth());
    const float stemBreadth = m_splitStemTracks ? breadth / 4.0f : 0;
    const float halfBreadth = (m_splitStemTracks ? stemBreadth : breadth) / 2.0f;

    const int stemCount = static_cast<int>(std::min<qsizetype>(
            stemInfo.size(), mixxx::kMaxSupportedStems));

    // Effective visual frame for x
    double xVisualFrame = qRound(firstVisualFrame / visualIncrementPerPixel) *
            visualIncrementPerPixel;

    const int numVerticesPerLine = 6; // 2 triangles

    const int reserved = numVerticesPerLine *
            (mixxx::audio::ChannelCount::stem() * stripLength + 1);

    geometry().setDrawingMode(Geometry::DrawingMode::Triangles);
    geometry().allocate(reserved);
    markDirtyGeometry();

    RGBAVertexUpdater vertexUpdater{geometry().vertexDataAs<Geometry::RGBAColoredPoint2D>()};
    vertexUpdater.addRectangle({0.f,
                                       halfBreadth - 0.5f},
            {static_cast<float>(length),
                    m_isSlipRenderer ? halfBreadth : halfBreadth + 0.5f},
            {0.f, 0.f, 0.f, 0.f});

    const double maxSamplingRange = visualIncrementPerPixel / 2.0;

    for (int visualIdx = 0; visualIdx < stripLength; visualIdx++) {
        const int visualFrameStart = std::lround(xVisualFrame - maxSamplingRange);
        const int visualFrameStop = std::lround(xVisualFrame + maxSamplingRange);

        const int visualIndexStart = std::max(visualFrameStart * 2, 0);
        const int visualIndexStop =
                std::min(std::max(visualFrameStop, visualFrameStart + 1) * 2, dataSize - 1);

        const float fVisualIdx = static_cast<float>(visualIdx) * invDevicePixelRatio;

        const mixxx::StemStripPeaks peaks = mixxx::stemStripPeaks(
                data, visualIndexStart, visualIndexStop, stemCount);
        // The stems sum to the mix, thus the loudest one alone is smaller
        // than the mix. The strip lifts it to the mix. A lane gets no lift.
        const float stemScale = m_splitStemTracks
                ? 1.0f
                : mixxx::stemOverlayScale(peaks.all, peaks.loudestStem(stemCount));

        int stemLayer = 0;
        for (int stemIdx : std::as_const(m_stackOrder)) {
            if (stemIdx >= stemInfo.size()) {
                continue;
            }
            // Stem is drawn twice with different opacity level, this allow to
            // see the maximum signal by transparency
            for (int layerIdx = 0; layerIdx < 2; layerIdx++) {
                QColor stemColor = stemInfo[stemIdx].getColor();
                float color_r = stemColor.redF(),
                      color_g = stemColor.greenF(),
                      color_b = stemColor.blueF(),
                      color_a = stemColor.alphaF() * (layerIdx ? m_opacity : m_outlineOpacity);

                // The outline layer shows the whole signal, the main layer
                // follows the fader and the mute of the stem.
                float volume = 1.f;
                if (layerIdx) {
                    if (selectedStems) {
                        volume = (selectedStems & 1 << stemIdx) ? 1.f : 0.f;
                    } else if (!m_pStemMute.empty() && m_pStemMute[stemIdx]->toBool()) {
                        volume = 0.f;
                    } else if (!m_pStemGain.empty()) {
                        volume = static_cast<float>(m_pStemGain[stemIdx]->get());
                    }
                }

                // Lines are thin rectangles
                // shadow
                float height = mixxx::stemStripHalfHeight(peaks.stems[stemIdx],
                        stemScale,
                        volume,
                        allGain,
                        halfBreadth);
                if (m_splitStemTracks) {
                    height = std::min(height, halfBreadth);
                }
                const int yIndex = m_splitStemTracks ? stemIdx : stemLayer;
                vertexUpdater.addRectangle(
                        {fVisualIdx - halfStripSize,
                                yIndex * stemBreadth + halfBreadth - height},
                        {fVisualIdx + halfStripSize,
                                m_isSlipRenderer
                                        ? yIndex * stemBreadth + halfBreadth
                                        : yIndex * stemBreadth + halfBreadth + height},
                        {color_r, color_g, color_b, color_a});
            }
            stemLayer++;
        }

        xVisualFrame += visualIncrementPerPixel;
    }

    DEBUG_ASSERT(reserved == vertexUpdater.index());

    markDirtyMaterial();

    return true;
}

} // namespace allshader
