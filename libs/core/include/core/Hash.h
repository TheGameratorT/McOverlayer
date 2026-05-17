#pragma once

#include <QString>
#include <QVector>
#include <QPair>
#include <cstdint>

namespace Core {

// Accumulate the polynomial rolling hash without applying the finalizer.
// Pass a non-zero init to continue hashing from a previously accumulated state,
// e.g. to hash (prefix + suffix) as ringHashFinal(ringHashAccum(suffix, ringHashAccum(prefix))).
uint32_t ringHashAccum(const QString &s, uint32_t init = 0);

// Apply the MurmurHash3-style bit-scatter finalizer.
inline uint32_t ringHashFinal(uint32_t h) noexcept {
    h ^= (h >> 16);
    h  = (h * 0x45d9f3bu) & 0xFFFFFFFFu;
    h ^= (h >> 16);
    return h;
}

// Full hash: equivalent to ringHashFinal(ringHashAccum(s)).
uint32_t ringHash(const QString &s);

// Return the overlay path at the nearest clockwise position >= pos on the ring.
// Ring must be sorted by hash value. Wraps around to ring[0] if pos is past the end.
QString ringSelect(const QVector<QPair<uint32_t, QString>> &ring, uint32_t pos);

// Convert a string seed to a signed 32-bit integer using Java's String.hashCode().
// Matches Minecraft's seed derivation from a string.
int stringToSeed(const QString &s);

} // namespace Core
