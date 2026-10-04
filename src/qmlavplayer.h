#ifndef QMLAVPLAYER_H
#define QMLAVPLAYER_H

#include <QQmlParserStatus>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QTimer>
#include <QList>

#include "qmlavframe.h"
#include "qmlavdemuxer.h"
#include "qmlavaudioiodevice.h"
#include "qmlavpropertyhelpers.h"

class QmlAVPlayer : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)

    // Qt6: Use QVideoSink instead of QAbstractVideoSurface
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    // Number of connected video sinks
    Q_PROPERTY(int videoSinkCount READ videoSinkCount NOTIFY videoSinkCountChanged)
    // When true, setVideoSink adds to the list instead of replacing
    Q_PROPERTY(bool multiSinkMode READ multiSinkMode WRITE setMultiSinkMode NOTIFY multiSinkModeChanged)

    QMLAV_PROPERTY_DECL(QVariantMap, avOptions, setAVOptions, avOptionsChanged);
    QMLAV_PROPERTY_DECL(bool, autoLoad, setAutoLoad, autoLoadChanged) = true;
    QMLAV_PROPERTY_DECL(bool, autoPlay, setAutoPlay, autoPlayChanged) = false;
    QMLAV_PROPERTY(int, loops, setLoops, loopsChanged) = 1; // NOTE: Implemented partially (Once playing and infinite loop behavior)
    QMLAV_PROPERTY_DECL(QUrl, source, setSource, sourceChanged);
    // Qt6: QMediaPlayer::State is now QMediaPlayer::PlaybackState
    QMLAV_PROPERTY_READONLY(QMediaPlayer::PlaybackState, playbackState, playbackStateChanged) = QMediaPlayer::StoppedState;
    QMLAV_PROPERTY_READONLY(QMediaPlayer::MediaStatus, status, statusChanged) = QMediaPlayer::NoMedia;
    QMLAV_PROPERTY_READONLY(QVariant, bufferProgress, bufferProgressChanged) = 1.0; // TODO:
    QMLAV_PROPERTY(bool, muted, setMuted, mutedChanged) = false; // TODO:
    QMLAV_PROPERTY_DECL(double, volume, setVolume, volumeChanged) = 0.0;
    QMLAV_PROPERTY_READONLY(bool, hasVideo, hasVideoChanged) = false;
    QMLAV_PROPERTY_READONLY(bool, hasAudio, hasAudioChanged) = false;
    // coded size times sample aspect ratio. 0 until the first video frame.
    QMLAV_PROPERTY_READONLY(double, displayAspect, displayAspectChanged) = 0.0;

public:
    QmlAVPlayer(QObject *parent = nullptr);
    ~QmlAVPlayer() override;

    // Returns first sink or nullptr
    QVideoSink *videoSink() const { return m_videoSinks.isEmpty() ? nullptr : m_videoSinks.first(); }
    int videoSinkCount() const { return m_videoSinks.count(); }
    bool multiSinkMode() const { return m_multiSinkMode; }
    void setMultiSinkMode(bool mode);
    
    virtual void classBegin() override {}
    virtual void componentComplete() override;

    // Multi-sink support - add/remove sinks without stopping playback
    Q_INVOKABLE void addVideoSink(QVideoSink *sink);
    Q_INVOKABLE void removeVideoSink(QVideoSink *sink);
    // Tear down the demuxer and play again without dropping attached sinks.
    Q_INVOKABLE void restartPlayback();

signals:
    void videoFramePresented();
    void videoSinkChanged();
    void videoSinkCountChanged();
    void multiSinkModeChanged();

public slots:
    void play();
    void stop();
    void setVideoSink(QVideoSink *sink);
    void frameHandler(const std::shared_ptr<QmlAVFrame> frame);

protected:
    bool load();
    void stateMachine();
    void reset();
    void stopPlayback(bool clearVideoSinks);

    void setPlaybackState(const QMediaPlayer::PlaybackState state);
    void setStatus(const QMediaPlayer::MediaStatus status);
    void setHasVideo(bool hasVideo);
    void setHasAudio(bool hasAudio);

private:
    bool m_complete;
    bool m_multiSinkMode;
    QmlAVDemuxer *m_demuxer;
    QList<QVideoSink*> m_videoSinks;
    QVideoFrameFormat m_lastVideoFormat;  // Cache format for late-joining sinks
    static constexpr int PresentFrameSlots = 8;
    QVideoFrame m_presentFrames[PresentFrameSlots];
    int m_presentIndex;

    int pickWritablePresentSlot();
    QTimer m_playTimer;

    QmlAVAudioIODevice m_audioIODevice;
    QAudioSink *m_audioSink;
    bool m_audioOutputChecked = false;
    bool m_audioOutputAvailable = false;
};

#endif // QMLAVPLAYER_H
