#include "MprisAdapter.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QStringList>

#include "audio/Player.h"

namespace auralbit::desktop {

namespace {
constexpr const char* kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char* kServiceName = "org.mpris.MediaPlayer2.auralbit";
constexpr const char* kRootInterface = "org.mpris.MediaPlayer2";
constexpr const char* kPlayerInterface = "org.mpris.MediaPlayer2.Player";
}  // namespace

// ---------------------- root adaptor ----------------------

class MprisRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit MprisRootAdaptor(MprisAdapter* parent)
        : QDBusAbstractAdaptor(parent), m_(parent) {}

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return "auralbit"; }
    QString desktopEntry() const { return "auralbit"; }
    QStringList supportedUriSchemes() const { return {"file"}; }
    QStringList supportedMimeTypes() const {
        return {"audio/mpeg", "audio/flac", "audio/x-flac",
                "audio/ogg",  "audio/x-vorbis+ogg",
                "audio/wav",  "audio/x-wav"};
    }

public slots:
    void Raise()  { emit m_->requestRaise(); }
    void Quit()   { emit m_->requestQuit(); }

private:
    MprisAdapter* m_;
};

// ---------------------- player adaptor ----------------------

class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(double Rate           READ rate          WRITE setRate)
    Q_PROPERTY(QVariantMap Metadata  READ metadata)
    Q_PROPERTY(double Volume         READ volume        WRITE setVolume)
    Q_PROPERTY(qlonglong Position    READ position)
    Q_PROPERTY(double MinimumRate    READ minimumRate)
    Q_PROPERTY(double MaximumRate    READ maximumRate)
    Q_PROPERTY(bool CanGoNext        READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious    READ canGoPrevious)
    Q_PROPERTY(bool CanPlay          READ canPlay)
    Q_PROPERTY(bool CanPause         READ canPause)
    Q_PROPERTY(bool CanSeek          READ canSeek)
    Q_PROPERTY(bool CanControl       READ canControl)

public:
    explicit MprisPlayerAdaptor(MprisAdapter* parent)
        : QDBusAbstractAdaptor(parent), m_(parent) {}

    QString playbackStatus() const {
        if (!m_->player()) return "Stopped";
        switch (m_->player()->state()) {
            case audio::PlayerState::Playing: return "Playing";
            case audio::PlayerState::Paused:  return "Paused";
            case audio::PlayerState::Stopped: return "Stopped";
        }
        return "Stopped";
    }
    double rate() const { return 1.0; }
    void setRate(double) {}
    QVariantMap metadata() const { return m_->metadata(); }
    double volume() const { return 1.0; }
    void setVolume(double) {}  // Volume not exposed through Player yet.
    qlonglong position() const {
        if (!m_->player()) return 0;
        return static_cast<qlonglong>(m_->player()->position_seconds() * 1'000'000);
    }
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    bool canGoNext() const { return m_->canGoNext(); }
    bool canGoPrevious() const { return m_->canGoPrev(); }
    bool canPlay() const { return true; }
    bool canPause() const { return true; }
    bool canSeek() const { return true; }
    bool canControl() const { return true; }

signals:
    void Seeked(qlonglong position_us);

public slots:
    void Next()      { emit m_->requestNext(); }
    void Previous()  { emit m_->requestPrevious(); }
    void Pause()     { emit m_->requestPause(); }
    void PlayPause() { emit m_->requestPlayPause(); }
    void Stop()      { emit m_->requestStop(); }
    void Play()      { emit m_->requestPlay(); }
    void Seek(qlonglong offset_us) { emit m_->requestSeek(offset_us); }
    void SetPosition(const QDBusObjectPath& /*track_id*/, qlonglong position_us) {
        emit m_->requestSetPosition(position_us);
    }
    void OpenUri(const QString& uri) { emit m_->requestOpenUri(uri); }

private:
    MprisAdapter* m_;
};

// ---------------------- adapter ----------------------

MprisAdapter::MprisAdapter(audio::Player* player, QObject* parent)
    : QObject(parent), player_(player) {
    root_ = new MprisRootAdaptor(this);
    player_adaptor_ = new MprisPlayerAdaptor(this);
}

MprisAdapter::~MprisAdapter() {
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterObject(kObjectPath);
    bus.unregisterService(kServiceName);
}

bool MprisAdapter::registerOnBus() {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    if (!bus.registerObject(kObjectPath, this)) return false;
    if (!bus.registerService(kServiceName)) return false;
    return true;
}

void MprisAdapter::notifyTrackChanged(const QVariantMap& metadata) {
    metadata_ = metadata;
    emitPropertiesChanged(kPlayerInterface, {{"Metadata", metadata_}});
}

void MprisAdapter::notifyPlaybackStateChanged() {
    emitPropertiesChanged(kPlayerInterface,
                          {{"PlaybackStatus", player_adaptor_->playbackStatus()}});
}

void MprisAdapter::notifyCanGoNextChanged(bool can_next) {
    if (can_go_next_ == can_next) return;
    can_go_next_ = can_next;
    emitPropertiesChanged(kPlayerInterface, {{"CanGoNext", can_next}});
}

void MprisAdapter::notifyCanGoPreviousChanged(bool can_prev) {
    if (can_go_prev_ == can_prev) return;
    can_go_prev_ = can_prev;
    emitPropertiesChanged(kPlayerInterface, {{"CanGoPrevious", can_prev}});
}

void MprisAdapter::notifySeeked(qint64 position_us) {
    QDBusMessage signal = QDBusMessage::createSignal(
        kObjectPath, kPlayerInterface, "Seeked");
    signal << position_us;
    QDBusConnection::sessionBus().send(signal);
}

void MprisAdapter::emitPropertiesChanged(const QString& iface,
                                         const QVariantMap& props) {
    QDBusMessage signal = QDBusMessage::createSignal(
        kObjectPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    signal << iface;
    signal << props;
    signal << QStringList{};
    QDBusConnection::sessionBus().send(signal);
}

}  // namespace auralbit::desktop

#include "MprisAdapter.moc"
