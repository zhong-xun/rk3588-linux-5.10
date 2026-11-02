#include "qrimageprovider.h"

#include <QBitArray>
#include <QImage>
#include <QPainter>
#include <QUrl>
#include <QVector>

namespace {

struct QrMatrix {
    int size = 0;
    QVector<int> modules;
    QVector<bool> reserved;

    explicit QrMatrix(int matrixSize = 0)
        : size(matrixSize)
        , modules(matrixSize * matrixSize, 0)
        , reserved(matrixSize * matrixSize, false)
    {
    }

    bool inBounds(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < size && y < size;
    }

    int index(int x, int y) const
    {
        return y * size + x;
    }

    bool dark(int x, int y) const
    {
        return inBounds(x, y) && modules.at(index(x, y)) != 0;
    }

    void set(int x, int y, bool dark, bool reserve = true)
    {
        if (!inBounds(x, y)) {
            return;
        }
        modules[index(x, y)] = dark ? 1 : 0;
        if (reserve) {
            reserved[index(x, y)] = true;
        }
    }

    bool isReserved(int x, int y) const
    {
        return inBounds(x, y) && reserved.at(index(x, y));
    }
};

void appendBits(QVector<int> *bits, int value, int count)
{
    for (int i = count - 1; i >= 0; --i) {
        bits->append((value >> i) & 1);
    }
}

QVector<int> gfMultiplyPoly(const QVector<int> &left, const QVector<int> &right)
{
    QVector<int> result(left.size() + right.size() - 1, 0);
    for (int i = 0; i < left.size(); ++i) {
        for (int j = 0; j < right.size(); ++j) {
            int product = 0;
            int a = left.at(i);
            int b = right.at(j);
            while (b > 0) {
                if (b & 1) {
                    product ^= a;
                }
                a <<= 1;
                if (a & 0x100) {
                    a ^= 0x11D;
                }
                b >>= 1;
            }
            result[i + j] ^= product;
        }
    }
    return result;
}

int gfMultiply(int a, int b)
{
    int product = 0;
    while (b > 0) {
        if (b & 1) {
            product ^= a;
        }
        a <<= 1;
        if (a & 0x100) {
            a ^= 0x11D;
        }
        b >>= 1;
    }
    return product;
}

int gfPow2(int exponent)
{
    int value = 1;
    for (int i = 0; i < exponent; ++i) {
        value <<= 1;
        if (value & 0x100) {
            value ^= 0x11D;
        }
    }
    return value;
}

QVector<int> reedSolomonGenerator(int degree)
{
    QVector<int> generator{1};
    for (int i = 0; i < degree; ++i) {
        generator = gfMultiplyPoly(generator, QVector<int>{1, gfPow2(i)});
    }
    return generator;
}

QVector<int> reedSolomonRemainder(const QVector<int> &data, int eccCount)
{
    QVector<int> generator = reedSolomonGenerator(eccCount);
    QVector<int> result(eccCount, 0);
    for (int value : data) {
        const int factor = value ^ result.first();
        for (int i = 0; i < eccCount - 1; ++i) {
            result[i] = result.at(i + 1) ^ gfMultiply(generator.at(i + 1), factor);
        }
        result[eccCount - 1] = gfMultiply(generator.last(), factor);
    }
    return result;
}

const int kMaxSafeQrPayloadBytes = 106;

int qrVersionForByteCount(int count)
{
    if (count <= 32) {
        return 3;
    }
    if (count <= 78) {
        return 5;
    }
    if (count <= 106) {
        return 6;
    }
    return 6;
}

int qrDataCodewords(int version)
{
    switch (version) {
    case 3:
        return 55;
    case 5:
        return 108;
    case 6:
        return 136;
    case 8:
        return 194;
    default:
        return 274;
    }
}

int qrEccCodewords(int version)
{
    switch (version) {
    case 3:
        return 15;
    case 5:
        return 26;
    case 6:
        return 36;
    case 8:
        return 48;
    default:
        return 72;
    }
}

QVector<int> byteModeCodewords(const QByteArray &payload, int version)
{
    QVector<int> bits;
    appendBits(&bits, 0x4, 4);
    appendBits(&bits, payload.size(), version <= 9 ? 8 : 16);
    for (char byte : payload) {
        appendBits(&bits, static_cast<unsigned char>(byte), 8);
    }

    const int dataCodewords = qrDataCodewords(version);
    const int capacityBits = dataCodewords * 8;
    appendBits(&bits, 0, qMin(4, capacityBits - bits.size()));
    while (bits.size() % 8 != 0) {
        bits.append(0);
    }

    QVector<int> codewords;
    for (int i = 0; i < bits.size(); i += 8) {
        int value = 0;
        for (int j = 0; j < 8; ++j) {
            value = (value << 1) | bits.at(i + j);
        }
        codewords.append(value);
    }
    for (int pad = 0xEC; codewords.size() < dataCodewords; pad ^= 0xEC ^ 0x11) {
        codewords.append(pad);
    }
    return codewords;
}

void drawFinder(QrMatrix *matrix, int x, int y)
{
    for (int dy = -1; dy <= 7; ++dy) {
        for (int dx = -1; dx <= 7; ++dx) {
            const int xx = x + dx;
            const int yy = y + dy;
            if (!matrix->inBounds(xx, yy)) {
                continue;
            }
            const bool edge = dx == 0 || dx == 6 || dy == 0 || dy == 6;
            const bool center = dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4;
            matrix->set(xx, yy, edge || center);
        }
    }
}

void drawAlignment(QrMatrix *matrix, int cx, int cy)
{
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            matrix->set(cx + dx, cy + dy, qMax(qAbs(dx), qAbs(dy)) != 1);
        }
    }
}

QVector<int> alignmentCenters(int version)
{
    switch (version) {
    case 3:
        return QVector<int>{6, 22};
    case 5:
        return QVector<int>{6, 30};
    case 6:
        return QVector<int>{6, 34};
    case 8:
        return QVector<int>{6, 24, 42};
    default:
        return QVector<int>{6, 28, 50};
    }
}

void drawFunctionPatterns(QrMatrix *matrix, int version)
{
    const int size = matrix->size;
    drawFinder(matrix, 0, 0);
    drawFinder(matrix, size - 7, 0);
    drawFinder(matrix, 0, size - 7);

    for (int i = 8; i < size - 8; ++i) {
        const bool dark = i % 2 == 0;
        matrix->set(6, i, dark);
        matrix->set(i, 6, dark);
    }

    const QVector<int> centers = alignmentCenters(version);
    for (int cy : centers) {
        for (int cx : centers) {
            const bool overlapsFinder = (cx == 6 && cy == 6)
                || (cx == 6 && cy == size - 7)
                || (cx == size - 7 && cy == 6);
            if (!overlapsFinder) {
                drawAlignment(matrix, cx, cy);
            }
        }
    }

    matrix->set(8, size - 8, true);
    for (int i = 0; i < 9; ++i) {
        if (i != 6) {
            matrix->set(8, i, false);
            matrix->set(i, 8, false);
        }
    }
    for (int i = 0; i < 8; ++i) {
        matrix->set(size - 1 - i, 8, false);
        matrix->set(8, size - 1 - i, false);
    }
}

bool maskBit(int mask, int x, int y)
{
    switch (mask) {
    case 0:
        return (x + y) % 2 == 0;
    case 1:
        return y % 2 == 0;
    case 2:
        return x % 3 == 0;
    default:
        return (x + y) % 3 == 0;
    }
}

void drawFormatBits(QrMatrix *matrix, int mask)
{
    int data = (1 << 3) | mask; // error correction level L
    int bits = data << 10;
    const int generator = 0x537;
    for (int i = 14; i >= 10; --i) {
        if ((bits >> i) & 1) {
            bits ^= generator << (i - 10);
        }
    }
    int format = ((data << 10) | bits) ^ 0x5412;
    const int size = matrix->size;
    const int coords1[15][2] = {{8,0},{8,1},{8,2},{8,3},{8,4},{8,5},{8,7},{8,8},{7,8},{5,8},{4,8},{3,8},{2,8},{1,8},{0,8}};
    const int coords2[15][2] = {{size-1,8},{size-2,8},{size-3,8},{size-4,8},{size-5,8},{size-6,8},{size-7,8},{8,size-8},{8,size-7},{8,size-6},{8,size-5},{8,size-4},{8,size-3},{8,size-2},{8,size-1}};
    for (int i = 0; i < 15; ++i) {
        const bool dark = ((format >> i) & 1) != 0;
        matrix->set(coords1[i][0], coords1[i][1], dark);
        matrix->set(coords2[i][0], coords2[i][1], dark);
    }
}

void drawCodewords(QrMatrix *matrix, const QVector<int> &codewords, int mask)
{
    QVector<int> bits;
    for (int codeword : codewords) {
        appendBits(&bits, codeword, 8);
    }

    int bitIndex = 0;
    int direction = -1;
    for (int right = matrix->size - 1; right >= 1; right -= 2) {
        if (right == 6) {
            --right;
        }
        for (int rowStep = 0; rowStep < matrix->size; ++rowStep) {
            const int y = direction < 0 ? matrix->size - 1 - rowStep : rowStep;
            for (int col = 0; col < 2; ++col) {
                const int x = right - col;
                if (matrix->isReserved(x, y)) {
                    continue;
                }
                const bool raw = bitIndex < bits.size() ? bits.at(bitIndex++) != 0 : false;
                matrix->set(x, y, raw ^ maskBit(mask, x, y), false);
            }
        }
        direction = -direction;
    }
}

int penaltyScore(const QrMatrix &matrix)
{
    int score = 0;
    for (int y = 0; y < matrix.size; ++y) {
        int runColor = matrix.dark(0, y);
        int runLength = 1;
        for (int x = 1; x < matrix.size; ++x) {
            const bool color = matrix.dark(x, y);
            if (color == runColor) {
                ++runLength;
            } else {
                if (runLength >= 5) {
                    score += 3 + runLength - 5;
                }
                runColor = color;
                runLength = 1;
            }
        }
        if (runLength >= 5) {
            score += 3 + runLength - 5;
        }
    }
    for (int x = 0; x < matrix.size; ++x) {
        int runColor = matrix.dark(x, 0);
        int runLength = 1;
        for (int y = 1; y < matrix.size; ++y) {
            const bool color = matrix.dark(x, y);
            if (color == runColor) {
                ++runLength;
            } else {
                if (runLength >= 5) {
                    score += 3 + runLength - 5;
                }
                runColor = color;
                runLength = 1;
            }
        }
        if (runLength >= 5) {
            score += 3 + runLength - 5;
        }
    }
    return score;
}

QrMatrix createQrMatrix(const QString &text)
{
    const QByteArray payload = text.toUtf8().left(kMaxSafeQrPayloadBytes);
    const int version = qrVersionForByteCount(payload.size());
    const QVector<int> data = byteModeCodewords(payload, version);
    QVector<int> codewords = data;
    const QVector<int> ecc = reedSolomonRemainder(data, qrEccCodewords(version));
    for (int value : ecc) {
        codewords.append(value);
    }

    const int size = version * 4 + 17;
    QrMatrix best(size);
    int bestScore = INT_MAX;
    for (int mask = 0; mask < 4; ++mask) {
        QrMatrix candidate(size);
        drawFunctionPatterns(&candidate, version);
        drawCodewords(&candidate, codewords, mask);
        drawFormatBits(&candidate, mask);
        const int score = penaltyScore(candidate);
        if (score < bestScore) {
            bestScore = score;
            best = candidate;
        }
    }
    return best;
}

QImage renderQrImage(const QString &text, const QSize &requestedSize)
{
    const QrMatrix matrix = createQrMatrix(text);
    const int target = qBound(96, qMax(requestedSize.width(), requestedSize.height()), 720);
    const int quiet = 4;
    const int cells = matrix.size + quiet * 2;
    const int scale = qMax(2, target / cells);
    const int imageSize = cells * scale;

    QImage image(imageSize, imageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(20, 27, 36));
    for (int y = 0; y < matrix.size; ++y) {
        for (int x = 0; x < matrix.size; ++x) {
            if (matrix.dark(x, y)) {
                painter.drawRect((x + quiet) * scale, (y + quiet) * scale, scale, scale);
            }
        }
    }
    return image;
}

} // namespace

QrImageProvider::QrImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage QrImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QString text = QUrl::fromPercentEncoding(id.toUtf8());
    if (text.isEmpty() || text.toUtf8().size() > kMaxSafeQrPayloadBytes) {
        text = QStringLiteral("http://<LAN-IP>:17999");
    }
    QImage image = renderQrImage(text, requestedSize);
    if (size) {
        *size = image.size();
    }
    return image;
}
