#include "qmlavplayer.h"

QmlAVPlayer::QmlAVPlayer(QObject *parent)
    : QObject(parent)
    , m_complete(false)
    , m_multiSinkMode(false)
    , m_demuxer(nullptr)
    , m_presentIndex(0)
    , m_audioSink(nullptr)
{
    m_playTimer.setSingleShot(true);
    connect(&m_playTimer, &QTimer::timeout, this, &QmlAVPlayer::play);
}

QmlAVPlayer::~QmlAVPlayer()
{
    stop();
}

void QmlAVPlayer::componentComplete()
{
    if (m_autoPlay) {
        play();
    } else if (m_autoLoad) {
        load();
    }

    m_complete = true;
}

void QmlAVPlayer::play()
{
    logDebug() << "play()";

    if (load()) {
        m_demuxer->start();
    }
}

void QmlAVPlayer::stop()
{
    stopPlayback(true);
}

void QmlAVPlayer::restartPlayback()
{
    logDebug() << "restartPlayback()";
    stopPlayback(false);
    play();
}

void QmlAVPlayer::stopPlayback(bool clearVideoSinks)
{
    logDebug() << "stopPlayback(clearVideoSinks=" << clearVideoSinks << ")";

    if (m_demuxer) {
        disconnect(m_demuxer, nullptr, this, nullptr);
        delete m_demuxer;
        m_demuxer = nullptr;
    }

    for (auto *sink : m_videoSinks) {
        if (sink)
            sink->setVideoFrame(QVideoFrame());
    }
    if (clearVideoSinks)
        m_videoSinks.clear();

    m_lastVideoFormat = QVideoFrameFormat();
    for (int i = 0; i < PresentFrameSlots; ++i)
        m_presentFrames[i] = QVideoFrame();
    m_presentIndex = 0;

    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
    }

    m_audioIODevice.clear();

    setPlaybackState(QMediaPlayer::StoppedState);
    setHasVideo(false);
    setHasAudio(false);
}

void QmlAVPlayer::setVideoSink(QVideoSink *sink)
{
    if (m_multiSinkMode) {
        if (sink)
            addVideoSink(sink);
        return;
    }
    
    // Replace all sinks with just this one
    if (m_videoSinks.count() == 1 && m_videoSinks.first() == sink) {
        return;  // No change
    }
    
    m_videoSinks.clear();
    
    if (sink) {
        m_videoSinks.append(sink);
    }
    
    emit videoSinkChanged();
    emit videoSinkCountChanged();
}

void QmlAVPlayer::setMultiSinkMode(bool mode)
{
    if (m_multiSinkMode == mode) {
        return;
    }
    
    logDebug() << "setMultiSinkMode(" << mode << ")";
    m_multiSinkMode = mode;
    emit multiSinkModeChanged();
}

void QmlAVPlayer::addVideoSink(QVideoSink *sink)
{
    if (!sink || m_videoSinks.contains(sink)) {
        return;
    }
    
    logDebug() << "addVideoSink() - adding sink, total will be:" << (m_videoSinks.count() + 1);
    
    m_videoSinks.append(sink);
    
    // Auto-remove sink when it's destroyed
    connect(sink, &QObject::destroyed, this, [this, sink]() {
        logDebug() << "Sink destroyed, auto-removing from list";
        m_videoSinks.removeAll(sink);
        emit videoSinkCountChanged();
    });
    
    emit videoSinkCountChanged();
}

void QmlAVPlayer::removeVideoSink(QVideoSink *sink)
{
    if (!sink) {
        return;
    }
    
    int index = m_videoSinks.indexOf(sink);
    if (index < 0) {
        return;
    }
    
    logDebug() << "removeVideoSink() - removing sink, total will be:" << (m_videoSinks.count() - 1);

    disconnect(sink, &QObject::destroyed, this, nullptr);
    // Drop the last presented frame so hidden carousel tiles do not keep QByteArray
    // backing (heaptrack: 155 live QVideoFrame allocations from createFrame).
    sink->setVideoFrame(QVideoFrame());
    m_videoSinks.removeAt(index);
    emit videoSinkCountChanged();
}

int QmlAVPlayer::pickWritablePresentSlot()
{
    for (int n = 0; n < PresentFrameSlots; ++n) {
        const int i = (m_presentIndex + n) % PresentFrameSlots;
        QVideoFrame &frame = m_presentFrames[i];
        if (!frame.isValid())
            return i;
        if (frame.map(QVideoFrame::WriteOnly)) {
            frame.unmap();
            return i;
        }
    }
    return m_presentIndex;
}

void QmlAVPlayer::frameHandler(const std::shared_ptr<QmlAVFrame> frame)
{
    if (m_playbackState == QMediaPlayer::PlayingState) {
        if (frame->type() == QmlAVFrame::TypeVideo) {
            auto vf = std::static_pointer_cast<QmlAVVideoFrame>(frame);
            const AVRational sar = vf->sampleAspectRatio();
            if (vf->width() > 0 && vf->height() > 0 && sar.num > 0 && sar.den > 0) {
                const double aspect = (double(vf->width()) * sar.num) / (double(vf->height()) * sar.den);
                if (qAbs(m_displayAspect - aspect) > 0.0001) {
                    m_displayAspect = aspect;
                    emit displayAspectChanged(aspect);
                }
            }
            const int slot = pickWritablePresentSlot();
            QVideoFrame qvf = vf->toQVideoFrame(&m_presentFrames[slot]);
            m_presentIndex = (slot + 1) % PresentFrameSlots;

            if (!m_videoSinks.isEmpty() && qvf.isValid()) {
                // Cache video format for debugging
                if (!m_lastVideoFormat.isValid()) {
                    m_lastVideoFormat = qvf.surfaceFormat();
                    
                    logDebug() << "Video format: "
                               << "QVideoFrameFormat(" << m_lastVideoFormat.pixelFormat() 
                               << ", " << m_lastVideoFormat.frameSize()
                               << ", colorSpace=" << m_lastVideoFormat.colorSpace()
                               << ')';
                    
                    setHasVideo(true);
                }

                // Qt6: Simply set the frame on all connected sinks
                for (auto *sink : m_videoSinks) {
                    if (sink) {
                        sink->setVideoFrame(qvf);
                    }
                }
                
                emit videoFramePresented();
            }
        } else if (frame->type() == QmlAVFrame::TypeAudio) {
            auto af = std::static_pointer_cast<QmlAVAudioFrame>(frame);

            if (!m_audioOutputChecked) {
                m_audioOutputChecked = true;
                m_audioOutputAvailable = !QMediaDevices::defaultAudioOutput().isNull();
                if (!m_audioOutputAvailable)
                    logWarning() << "No audio output device; audio playback disabled";
            }
            if (!m_audioOutputAvailable || !af->audioFormat().isValid())
                return;

            m_audioIODevice.enqueue(af);

            if (!m_audioSink) {
                auto f = af->audioFormat();
                logDebug() << "Starting audio with: " << f;

                auto outputDevice = QMediaDevices::defaultAudioOutput();
                if (outputDevice.isNull()) {
                    m_audioOutputAvailable = false;
                    m_audioIODevice.clear();
                    logWarning() << "No audio output device; audio playback disabled";
                    return;
                }
                m_audioSink = new QAudioSink(outputDevice, f);
                m_audioSink->setVolume(QAudio::convertVolume(m_volume,
                                                             QAudio::LogarithmicVolumeScale,
                                                             QAudio::LinearVolumeScale));
                m_audioSink->start(&m_audioIODevice);
                setHasAudio(true);
            }
        }
    }
}

void QmlAVPlayer::setAVOptions(QmlAVPropertyType<QVariantMap> avOptions)
{
    if (m_avOptions == avOptions) {
        return;
    }

    m_avOptions = avOptions;

    reset();

    emit avOptionsChanged(avOptions);
}

void QmlAVPlayer::setAutoLoad(QmlAVPropertyType<bool> autoLoad)
{
    if (m_autoLoad == autoLoad) {
        return;
    }

    m_autoLoad = autoLoad;

    if (m_complete && autoLoad) {
        load();
    }

    emit autoLoadChanged(autoLoad);
}

void QmlAVPlayer::setAutoPlay(QmlAVPropertyType<bool> autoPlay)
{
    if (m_autoPlay == autoPlay) {
        return;
    }

    m_autoPlay = autoPlay;

    if (m_complete && autoPlay) {
        play();
    }

    emit autoPlayChanged(autoPlay);
}

void QmlAVPlayer::setSource(QmlAVPropertyType<QUrl> source)
{
    if (m_source == source) {
        return;
    }

    logDebug() << QString("setSource(source=%1)").arg(source.toDisplayString());

    m_source = source;
    if (!qFuzzyIsNull(m_displayAspect)) {
        m_displayAspect = 0.0;
        emit displayAspectChanged(0.0);
    }

    reset();

    emit sourceChanged(source);
}

void QmlAVPlayer::setVolume(QmlAVPropertyType<double> volume)
{
    if (qFuzzyCompare(m_volume, volume)) {
        return;
    }

    m_volume = volume;

    if (m_audioSink) {
        m_audioSink->setVolume(QAudio::convertVolume(volume,
                                                     QAudio::LogarithmicVolumeScale,
                                                     QAudio::LinearVolumeScale));
    }

    emit volumeChanged(volume);
}

bool QmlAVPlayer::load()
{
    if (!m_demuxer && m_source.isValid()) {
        m_demuxer = new QmlAVDemuxer();

        connect(m_demuxer, &QmlAVDemuxer::frameFinished, this, &QmlAVPlayer::frameHandler);
        connect(m_demuxer, &QmlAVDemuxer::playbackStateChanged, this, &QmlAVPlayer::setPlaybackState);
        connect(m_demuxer, &QmlAVDemuxer::mediaStatusChanged, this, &QmlAVPlayer::setStatus);

        m_demuxer->load(m_source, m_avOptions);

        return true;
    }

    return false;
}

void QmlAVPlayer::stateMachine()
{
    logDebug() << QString("stateMachine[m_status=%1; m_playbackState=%2]()").arg(m_status).arg(m_playbackState);

    if (m_playbackState == QMediaPlayer::PausedState) {
        // TODO: Implement it
        logInfo() << QString("%1:%2 Not implemented!").arg(__FILE__).arg(__LINE__);
    } else if (m_playbackState == QMediaPlayer::StoppedState) {
        switch (m_status) {
        case QMediaPlayer::NoMedia:
        case QMediaPlayer::EndOfMedia:
        case QMediaPlayer::InvalidMedia: {
            // Keep sinks so viewports stay attached across reconnect / recycle.
            if (m_demuxer) {
                stopPlayback(false);

                if (m_loops == -1 /*MediaPlayer.Infinite*/) {
                    m_playTimer.start(1000);
                }
            }

            break;
        }
        default:
            break;
        }
    }
}

void QmlAVPlayer::reset()
{
    if (m_complete) {
        stop();

        if (m_autoPlay) {
            play();
        } else if (m_autoLoad) {
            load();
        }
    }
}

void QmlAVPlayer::setPlaybackState(const QMediaPlayer::PlaybackState state)
{
    if (m_playbackState == state) {
        return;
    }

    logDebug() << QString("setPlaybackState(state=%1)").arg(state);

    m_playbackState = state;

    if (sender()) {
        stateMachine();
    }

    emit playbackStateChanged(state);
}

void QmlAVPlayer::setStatus(const QMediaPlayer::MediaStatus status)
{
    if (m_status == status) {
        return;
    }

    logDebug() << QString("setStatus(status=%1)").arg(status);

    m_status = status;

    stateMachine();

    emit statusChanged(status);
}

void QmlAVPlayer::setHasVideo(bool hasVideo)
{
    if (m_hasVideo == hasVideo) {
        return;
    }

    logDebug() << QString("setHasVideo(hasVideo=%1)").arg(hasVideo);

    m_hasVideo = hasVideo;

    emit hasVideoChanged(hasVideo);
}

void QmlAVPlayer::setHasAudio(bool hasAudio)
{
    if (m_hasAudio == hasAudio) {
        return;
    }

    logDebug() << QString("setHasAudio(hasAudio=%1)").arg(hasAudio);

    m_hasAudio = hasAudio;

    emit hasAudioChanged(hasAudio);
}
