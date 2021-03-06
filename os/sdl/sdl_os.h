
// sdl_os.h - SDL Outlaws.h implementation

#ifndef SDL_OS
#define SDL_OS

#include <string>

//////////////// implemented in sdl.cpp

// run the main loop
int sdl_os_main(int argc, const char **argv);

// store a string for one frame, then autorelease
const char* sdl_os_autorelease(std::string &val);

// call from crash handler. flush log, etc.
void sdl_os_report_crash(const std::string &message);

void sdl_set_scaling_factor(float factor);

// utf8 <-> utf16
std::wstring s2ws(const std::string& s);


/////////////// implemented in per-os file

bool os_symlink_f(const char* source, const char* dest);

string os_get_platform_info();

int os_get_system_ram();

void os_stacktrace();

int os_init();
void os_cleanup();

#endif
