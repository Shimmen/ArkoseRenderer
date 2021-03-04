#pragma once

namespace Profiling {
inline void setNameForActiveThread(const char* name);
}


#ifdef TRACY_ENABLE

#include <Tracy.hpp>

#define END_OF_FRAME_PROFILE_MARKER() FrameMark

#define SCOPED_PROFILE_ZONE() ZoneScopedC(0x454535)

#define SCOPED_PROFILE_ZONE_COLOR(color) ZoneScopedC(color)
#define SCOPED_PROFILE_ZONE_NAMED(name) ZoneScopedN(name)

#define SCOPED_PROFILE_ZONE_DYNAMIC(nameStr, color) \
	ZoneScopedC(color)                              \
	ZoneName(nameStr.c_str(), nameStr.length())

#define SCOPED_PROFILE_ZONE_BACKEND() ZoneScopedC(0x00ff00);
#define SCOPED_PROFILE_ZONE_BACKEND_NAMED(name) ZoneScopedNC(name, 0x00ff00);

#define SCOPED_PROFILE_ZONE_GPUCOMMAND() ZoneScopedC(0xff0000);
#define SCOPED_PROFILE_ZONE_GPURESOURCE() ZoneScopedC(0x0000ff);

void Profiling::setNameForActiveThread(const char* name)
{
    tracy::SetThreadName(name);
}

#endif
