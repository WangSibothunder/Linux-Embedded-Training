#ifndef CANCHANNEL_H
#define CANCHANNEL_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

class QSocketNotifier;

class CanChannel : public QObject
{
    Q_OBJECT
public:
    explicit CanChannel(QObject *parent = 0);
    ~CanChannel();

    bool isOpen() const { return m_socket >= 0; }
    QString interfaceName() const { return m_interface; }

public slots:
    bool openChannel(const QString &interfaceName, int bitrate);
    void closeChannel();
    bool sendFrame(quint32 identifier, const QByteArray &data);

signals:
    void frameReceived(const QString &interfaceName, quint32 identifier, const QByteArray &data);
    void channelStateChanged(const QString &interfaceName, bool open);
    void errorMessage(const QString &message);

private slots:
    void readFrames();

private:
    bool runIp(const QStringList &arguments, bool reportError = true);

    int m_socket;
    QString m_interface;
    QSocketNotifier *m_notifier;
};

#endif
