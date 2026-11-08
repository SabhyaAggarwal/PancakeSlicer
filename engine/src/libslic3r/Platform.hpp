#ifndef SLIC3R_Platform_HPP
#define SLIC3R_Platform_HPP

#include <string>

namespace Slic3r {

enum class Platform
{
	Uninitialized,
	Unknown,
	Windows,
	OSX,
	Linux,
	BSDUnix,
};

enum class PlatformFlavor
{
	Uninitialized,
    Unknown,
    Generic,
    GenericLinux,
    LinuxOnChromium,
    WSL,
    WSL2,
    OpenBSD,
    GenericOSX,
    OSXOnX86,
    OSXOnArm,
};

void 			detect_platform();

Platform 		platform();
PlatformFlavor 	platform_flavor();

std::string     platform_os_type();
std::string     platform_architecture();
std::string platform_to_string(Platform platform);
std::string platform_flavor_to_string(PlatformFlavor pf);

}

#endif
