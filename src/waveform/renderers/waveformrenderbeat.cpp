#include "waveform/renderers/waveformrenderbeat.h"

#include <QPainter>

#include "track/track.h"
#include "util/painterscope.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

class QPaintEvent;

namespace {
// A track with a bar phase draws the beat lines weaker than the bar lines.
constexpr float kBeatAlphaFactor = 0.6f;
} // namespace

WaveformRenderBeat::WaveformRenderBeat(WaveformWidgetRenderer* waveformWidgetRenderer)
        : WaveformRendererAbstract(waveformWidgetRenderer) {
    m_beats.resize(128);
    m_downbeats.resize(32);
}

WaveformRenderBeat::~WaveformRenderBeat() {
}

void WaveformRenderBeat::setup(const QDomNode& node, const SkinContext& context) {
    m_beatColor = QColor(context.selectString(node, "BeatColor"));
    m_beatColor = WSkinColor::getCorrectColor(m_beatColor).toRgb();
}

void WaveformRenderBeat::draw(QPainter* painter, QPaintEvent* /*event*/) {
    TrackPointer pTrackInfo = m_waveformRenderer->getTrackInfo();

    if (!pTrackInfo) {
        return;
    }

    mixxx::BeatsPointer trackBeats = pTrackInfo->getBeats();
    if (!trackBeats) {
        return;
    }

    int alpha = m_waveformRenderer->getBeatGridAlpha();
    if (alpha == 0) {
        return;
    }
#ifdef MIXXX_USE_QOPENGL
    // Using alpha transparency with drawLines causes a graphical issue when
    // drawing with QPainter on the QOpenGLWindow: instead of individual lines
    // a large rectangle encompassing all beatlines is drawn.
    m_beatColor.setAlphaF(1.f);
#else
    m_beatColor.setAlphaF(alpha/100.0);
#endif

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0) {
        return;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition();
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition();

    // qDebug() << "trackSamples" << trackSamples
    //          << "firstDisplayedPosition" << firstDisplayedPosition
    //          << "lastDisplayedPosition" << lastDisplayedPosition;

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);
    auto it = trackBeats->iteratorFrom(startPosition);

    // if no beat do not waste time saving/restoring painter
    if (it == trackBeats->cend() || *it > endPosition) {
        return;
    }

    PainterScope PainterScope(painter);

    painter->setRenderHint(QPainter::Antialiasing);

    const Qt::Orientation orientation = m_waveformRenderer->getOrientation();
    const float rendererWidth = m_waveformRenderer->getWidth();
    const float rendererHeight = m_waveformRenderer->getHeight();

    // The grid index of the first drawn beat. Stepping it with the iterator
    // costs less than a lookup for each beat of a grid with tempo markers.
    const std::optional<mixxx::BarPhase>& barPhase = trackBeats->barPhase();
    const bool drawBars = barPhase && it != trackBeats->cbegin();
    int beatIndex = drawBars ? trackBeats->beatIndex(it) : 0;

    int beatCount = 0;
    int downbeatCount = 0;

    for (; it != trackBeats->cend() && *it <= endPosition; ++it, ++beatIndex) {
        double beatPosition = it->toEngineSamplePos();
        double xBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(beatPosition);

        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;

        const bool isDownbeat = drawBars && barPhase->isDownbeat(beatIndex);
        QVector<QLineF>& lines = isDownbeat ? m_downbeats : m_beats;
        int& count = isDownbeat ? downbeatCount : beatCount;

        // If we don't have enough space, double the size.
        if (count >= lines.size()) {
            lines.resize(lines.size() * 2);
        }

        if (orientation == Qt::Horizontal) {
            lines[count++].setLine(xBeatPoint, 0.0f, xBeatPoint, rendererHeight);
        } else {
            lines[count++].setLine(0.0f, xBeatPoint, rendererWidth, xBeatPoint);
        }
    }

    QColor beatColor = m_beatColor;
    if (barPhase) {
        // A track with a bar phase draws the beat lines weaker than the bar
        // lines. Without a bar phase the lines keep the alpha of the skin.
        beatColor.setAlphaF(m_beatColor.alphaF() * kBeatAlphaFactor);
    }

    QPen beatPen(beatColor);
    beatPen.setWidthF(std::max(1.0, scaleFactor()));
    painter->setPen(beatPen);
    // Make sure to use constData to prevent detaches!
    painter->drawLines(m_beats.constData(), beatCount);

    if (downbeatCount > 0) {
        QPen downbeatPen(m_beatColor);
        downbeatPen.setWidthF(std::max(1.0, scaleFactor()));
        painter->setPen(downbeatPen);
        painter->drawLines(m_downbeats.constData(), downbeatCount);
    }
}
