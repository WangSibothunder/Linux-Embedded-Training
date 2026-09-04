#include "bytecodec.h"

bool ByteCodec::parseHex(const QString &text, QByteArray *result, QString *error)
{
    result->clear();
    if (error) error->clear();
    QByteArray digits;
    digits.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isSpace()) continue;
        const ushort u = c.unicode();
        if (!((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F'))) {
            if (error) *error = QString::fromUtf8("第 %1 个字符不是十六进制数字；请使用如 01 A2 FF 的格式，不加 0x。").arg(i + 1);
            return false;
        }
        digits.append(char(u));
    }
    if (digits.size() % 2) {
        if (error) *error = QString::fromUtf8("HEX 数据必须包含偶数个数字（每两个数字组成一个字节）。");
        return false;
    }
    *result = QByteArray::fromHex(digits);
    return true;
}

QString ByteCodec::hex(const QByteArray &bytes)
{
    static const char digits[] = "0123456789ABCDEF";
    QString result;
    result.reserve(bytes.size() * 3);
    for (int i = 0; i < bytes.size(); ++i) {
        if (i) result += QLatin1Char(' ');
        const unsigned char b = static_cast<unsigned char>(bytes.at(i));
        result += QLatin1Char(digits[b >> 4]);
        result += QLatin1Char(digits[b & 15]);
    }
    return result;
}

QByteArray ByteCodec::terminator(int index)
{
    switch (index) {
    case 1: return QByteArray("\n");
    case 2: return QByteArray("\r");
    case 3: return QByteArray("\r\n");
    default: return QByteArray();
    }
}

Utf8Stream::Utf8Stream() { reset(); }
void Utf8Stream::reset() { m_decoder.reset(QTextCodec::codecForName("UTF-8")->makeDecoder()); }
QString Utf8Stream::decode(const QByteArray &bytes) { return m_decoder->toUnicode(bytes); }
