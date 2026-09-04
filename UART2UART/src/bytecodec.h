#ifndef BYTECODEC_H
#define BYTECODEC_H
#include <QByteArray>
#include <QString>
#include <QTextCodec>
#include <memory>

namespace ByteCodec {
bool parseHex(const QString &text, QByteArray *result, QString *error = nullptr);
QString hex(const QByteArray &bytes);
QByteArray terminator(int index); // 0:none, 1:LF, 2:CR, 3:CRLF
}

// Decoder state survives readyRead boundaries (a UTF-8 character may span reads).
class Utf8Stream {
public:
    Utf8Stream();
    void reset();
    QString decode(const QByteArray &bytes);
private:
    std::unique_ptr<QTextDecoder> m_decoder;
};
#endif
