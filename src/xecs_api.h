#ifndef XECS_API_H
#define XECS_API_H
#pragma once

// Export/import decoration for the handful of symbols that must have exactly ONE definition
// shared across every module that links xECSV2, once it is actually built as a shared library
// (Phase 7 of the xECSV2 type-registration architecture plan) - the explicit instantiations of
// xECSV2's own built-in component types' info_var<T> (see xecs_builtin_instantiations.h) and the
// few genuinely out-of-line symbols defined in xecs_game_mgr.cpp/xecs_prefab_mgr.cpp.
//
// Expands to nothing at all unless XECS_BUILD_SHARED is defined - xECSV2's default, everyday mode
// is still today's compiled-directly-into-the-consumer static/included-source build (a single
// xecs.cpp #include'd straight into whichever executable uses it), where there is exactly one
// physical copy of everything already and no import/export boundary to cross. Only the CMake
// target that actually builds xECSV2.dll defines XECS_BUILD_SHARED+XECS_EXPORTS (dllexport); any
// OTHER target that links against that DLL instead of compiling xecs.cpp itself defines
// XECS_BUILD_SHARED alone (dllimport).
#if defined(XECS_BUILD_SHARED)
    #if defined(XECS_EXPORTS)
        #define XECS_API __declspec(dllexport)
    #else
        #define XECS_API __declspec(dllimport)
    #endif
#else
    #define XECS_API
#endif

#endif
