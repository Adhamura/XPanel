#include "QrMatrix.h"

#include "qrcodegen.hpp"

#include <stdexcept>

QrMatrix qrEncode(const QString &text)
{
    QrMatrix m;
    const QByteArray utf8 = text.toUtf8();
    if (utf8.isEmpty())
        return m;
    const auto ecc = utf8.size() > 700 ? qrcodegen::QrCode::Ecc::LOW : qrcodegen::QrCode::Ecc::MEDIUM;
    try {
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(utf8.constData(), ecc);
        m.size = qr.getSize();
        m.cells.resize(m.size * m.size);
        for (int y = 0; y < m.size; ++y)
            for (int x = 0; x < m.size; ++x)
                m.cells[y * m.size + x] = qr.getModule(x, y) ? 1 : 0;
    } catch (const std::length_error &) { // data_too_long
        return QrMatrix();
    }
    return m;
}
