#pragma once

#include "base/Qv2rayFeatures.hpp"

// The legacy NTP implementation was retired from the maintained Windows
// product. Preferences keeps the historical slot for UI compatibility, but the
// entry point is hidden and util_has_ntp must remain disabled. Fail loudly if a
// future change tries to re-enable the backend without restoring an implementation.
#if QV2RAY_FEATURE(util_has_ntp)
#error "The legacy Qv2ray NTP backend has been retired; do not re-enable util_has_ntp without a maintained implementation."
#endif
