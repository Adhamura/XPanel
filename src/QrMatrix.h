#pragma once

#include <QByteArray>
#include <QString>

// QR code of a text (Project Nayuki's qrcodegen in third_party/qrcodegen). Qt Core only.
struct QrMatrix {
    int size = 0;       // modules per side; 0 = the text does not fit in a QR code
    QByteArray cells;   // size * size, 1 = dark module, row by row
    bool isNull() const { return size == 0; }
    bool dark(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < size && y < size && cells.at(y * size + x) != 0;
    }
};

// UTF-8 text; error correction MEDIUM, LOW for long texts (fewer modules, easier to scan from a screen).
// The generator raises the level again when the version has room.
QrMatrix qrEncode(const QString &text);
