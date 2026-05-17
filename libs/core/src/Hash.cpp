#include "core/Hash.h"

#include <QChar>
#include <algorithm>

namespace Core {

uint32_t ringHashAccum(const QString &s, uint32_t init)
{
    uint32_t h = init;
    for (QChar c : s)
        h = (31u * h + c.unicode()) & 0xFFFFFFFFu;
    return h;
}

uint32_t ringHash(const QString &s)
{
    return ringHashFinal(ringHashAccum(s));
}

QString ringSelect(const QVector<QPair<uint32_t, QString>> &ring, uint32_t pos)
{
    if (ring.isEmpty())
        return {};

    // Binary search for first entry with hash >= pos
    auto it = std::lower_bound(
        ring.cbegin(), ring.cend(),
        QPair<uint32_t, QString>{pos, {}},
        [](const QPair<uint32_t, QString> &a, const QPair<uint32_t, QString> &b) {
            return a.first < b.first;
        }
    );

    if (it == ring.cend())
        it = ring.cbegin();  // wrap around

    return it->second;
}

int stringToSeed(const QString &s)
{
    // Replicate Java's String.hashCode() with 32-bit signed overflow
    int32_t h = 0;
    for (QChar c : s)
        h = 31 * h + static_cast<int32_t>(c.unicode());
    return static_cast<int>(h);
}

} // namespace Core
