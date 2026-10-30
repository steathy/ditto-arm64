#pragma once

// Upstream stable 3.25.113.0, followed by this ARM64 port's revision.
// This is a community build version, not an official upstream release number.
#define DITTO_ARM64_VERSION_NUMBERS 3,25,113,2
#define DITTO_ARM64_VERSION_STRING "3.25.113.2"
// Set to 0 only when preparing a validated stable release.
#define DITTO_ARM64_NIGHTLY_BUILD 0
#if DITTO_ARM64_NIGHTLY_BUILD
#define DITTO_ARM64_BUILD_LABEL "Nightly Build"
#else
#define DITTO_ARM64_BUILD_LABEL "Stable Build"
#endif
#define DITTO_ARM64_REPOSITORY_URL "https://github.com/steathy/ditto-arm64"
#define DITTO_UPSTREAM_REPOSITORY_URL "https://github.com/sabrogden/Ditto"
#define DITTO_ARM64_RELEASES_URL "https://github.com/steathy/ditto-arm64/releases"
