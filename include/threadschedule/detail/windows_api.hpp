#pragma once

#ifdef _WIN32

#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif

#  ifndef NOMINMAX
#    define NOMINMAX
#  endif

#  ifndef STRICT
#    define STRICT
#  endif

#  ifndef _WIN32_WINNT
#    ifdef THREADSCHEDULE_WINDOWS_VISTA_COMPAT
#      define _WIN32_WINNT 0x0600
#    else
#      define _WIN32_WINNT 0x0601
#    endif
#  endif

#  ifndef WINVER
#    define WINVER _WIN32_WINNT
#  endif

#  include <windows.h>

#endif