#pragma once
#if __has_include("GravityDesktopBuildIdentity.generated.h")
#include "GravityDesktopBuildIdentity.generated.h"
#else
#define GRAVITY_BUILD_ID "UNIDENTIFIED"
#define GRAVITY_BUILD_COMMIT "UNKNOWN"
#define GRAVITY_BUILD_DIFF "UNKNOWN"
#define GRAVITY_BUILD_SOURCE "UNVERIFIED"
#define GRAVITY_BUILD_UTC "UNKNOWN"
#define GRAVITY_BUILD_CHANNEL "UNKNOWN"
#endif
#ifdef GRAVITYDESKTOP_PACKAGED
#define GRAVITY_BUILD_TYPE "MSIX"
#else
#define GRAVITY_BUILD_TYPE "ordinary"
#endif
