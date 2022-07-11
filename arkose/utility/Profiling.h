#pragma once

namespace Profiling {
inline void setNameForActiveThread(const char* name);
}


#if defined(TRACY_ENABLE)

#include <Tracy.hpp>

#define END_OF_FRAME_PROFILE_MARKER() FrameMark

#define SCOPED_PROFILE_ZONE() ZoneScopedC(0x454535)

#define SCOPED_PROFILE_ZONE_COLOR(color) ZoneScopedC(color)
#define SCOPED_PROFILE_ZONE_NAMED(name) ZoneScopedN(name)
#define SCOPED_PROFILE_ZONE_NAME_AND_COLOR(name, color) ZoneScopedNC(name, color)

#define SCOPED_PROFILE_ZONE_DYNAMIC(nameStr, color) \
	ZoneScopedC(color)                              \
	ZoneName(nameStr.c_str(), nameStr.length())

#define SCOPED_PROFILE_ZONE_BACKEND() ZoneScopedC(0x00ff00);
#define SCOPED_PROFILE_ZONE_BACKEND_NAMED(name) ZoneScopedNC(name, 0x00ff00);

#define SCOPED_PROFILE_ZONE_PHYSICS() ZoneScopedC(0xdddddd);
#define SCOPED_PROFILE_ZONE_PHYSICS_NAMED(name) ZoneScopedNC(name, 0xdddddd);

#define SCOPED_PROFILE_ZONE_GPUCOMMAND() ZoneScopedC(0xff0000);
#define SCOPED_PROFILE_ZONE_GPURESOURCE() ZoneScopedC(0x0000ff);

void Profiling::setNameForActiveThread(const char* name)
{
    tracy::SetThreadName(name);
}

#else

#define END_OF_FRAME_PROFILE_MARKER()

#define SCOPED_PROFILE_ZONE()

#define SCOPED_PROFILE_ZONE_COLOR(color)
#define SCOPED_PROFILE_ZONE_NAMED(name)
#define SCOPED_PROFILE_ZONE_NAME_AND_COLOR(name, color)

#define SCOPED_PROFILE_ZONE_DYNAMIC(nameStr, color)

#define SCOPED_PROFILE_ZONE_BACKEND()
#define SCOPED_PROFILE_ZONE_BACKEND_NAMED(name)

#define SCOPED_PROFILE_ZONE_PHYSICS()
#define SCOPED_PROFILE_ZONE_PHYSICS_NAMED(name)

#define SCOPED_PROFILE_ZONE_GPUCOMMAND()
#define SCOPED_PROFILE_ZONE_GPURESOURCE()

void Profiling::setNameForActiveThread(const char* name) {}

#endif
