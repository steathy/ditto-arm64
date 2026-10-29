#pragma once
#include <array>

namespace ditto_update {
// SHA-256 of DER SubjectPublicKeyInfo for approved code-signing keys.
// Intentionally empty until this fork receives its signing identity. Never fill
// this from the feed. For key rotation, ship overlapping old/new pins first.
inline constexpr std::array<const char*, 0> TrustedPublisherKeys{};
}
