#pragma once

#include <QObject>
#include <QVariantMap>

namespace auralbit::audio { class Player; }

namespace auralbit::desktop {

class MprisRootAdaptor;
class MprisPlayerAdaptor;

// Owns the org.mpris.MediaPlayer2 + .Player adaptor objects, registers them
// on the session bus, and acts as the bridge between MainWindow and the
// outside world. MainWindow calls notify*() to publish state changes; the
// adapter emits request*() signals when D-Bus clients invoke methods.
class MprisAdapter : public QObject {
    Q_OBJECT
public:
    explicit MprisAdapter(audio::Player* player, QObject* parent = nullptr);
    ~MprisAdapter() override;

    // Registers org.mpris.MediaPlayer2.auralbit on the session bus and
    // exposes /org/mpris/MediaPlayer2. Returns false if the bus is missing
    // or the name is taken.
    bool registerOnBus();

    // ---- State publishing (called from MainWindow) ----
    // metadata follows the MPRIS metadata format (mpris:trackid, mpris:length,
    // xesam:title, xesam:artist, xesam:album, xesam:trackNumber, …).
    void notifyTrackChanged(const QVariantMap& metadata);
    void notifyPlaybackStateChanged();    // Emits PropertiesChanged for PlaybackStatus.
    void notifyCanGoNextChanged(bool can_next);
    void notifyCanGoPreviousChanged(bool can_prev);
    void notifySeeked(qint64 position_us);

    // ---- Read-only accessors used by the adaptor classes ----
    audio::Player* player() const { return player_; }
    const QVariantMap& metadata() const { return metadata_; }
    bool canGoNext() const { return can_go_next_; }
    bool canGoPrev() const { return can_go_prev_; }

signals:
    // Emitted by D-Bus method invocations. MainWindow connects to these.
    void requestRaise();
    void requestQuit();
    void requestPlayPause();
    void requestPlay();
    void requestPause();
    void requestStop();
    void requestNext();
    void requestPrevious();
    void requestSeek(qint64 offset_us);
    void requestSetPosition(qint64 position_us);
    void requestOpenUri(const QString& uri);

private:
    void emitPropertiesChanged(const QString& iface, const QVariantMap& props);

    audio::Player* player_;
    MprisRootAdaptor* root_ = nullptr;
    MprisPlayerAdaptor* player_adaptor_ = nullptr;

    QVariantMap metadata_;
    bool can_go_next_ = false;
    bool can_go_prev_ = false;
};

}  // namespace auralbit::desktop
