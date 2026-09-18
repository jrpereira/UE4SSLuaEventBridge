#pragma once
// Shared product version. API compatibility is versioned separately.
#define UE4SSLEB_VERSION "0.3.4-rc.2"
#define UE4SSLEB_WIDEN_IMPL(value) L##value
#define UE4SSLEB_WIDEN(value) UE4SSLEB_WIDEN_IMPL(value)
