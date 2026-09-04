#ifndef XECS_H
#define XECS_H
#pragma once

//--------------------------------------------------------------
// HEADER FILES
//--------------------------------------------------------------

//
// SYSTEM
//
#define NOMINMAX
#include "Windows.h"

#include <iostream>
#include <vector>
#include <array>
#include <functional>
#include <bit>
#include <bitset>

//
// EXTERNAL DEPENDENCIES
//
#include "dependencies/xerr/source/xerr.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"
#include "dependencies/xtextfile/source/xtextfile.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xproperty/source/sprop/property_sprop.h"

// xresource_guid.h only specializes std::hash for its own named aliases (instance_guid, type_guid,
// full_guid, def_guid<...>, ...) - xECS mints its own custom-tagged xresource::guid<T> aliases
// (component::type::guid, archetype::guid, pool::family::guid, ...) that get used as
// std::unordered_map keys, which need a std::hash of their own. xresource::ComputeHash(guid<T>) is
// already generic over any tag, just not wired up as a std::hash specialization - this does that once,
// covering every xECS guid alias, without touching xresource_guid.h itself.
template< typename T >
struct std::hash<xresource::guid<T>>
{
    std::size_t operator()( const xresource::guid<T>& k ) const noexcept { return xresource::ComputeHash(k); }
};

//--------------------------------------------------------------
// PROFILER
//--------------------------------------------------------------
// xECS keeps its Tracy profiler hook call sites (in game_mgr::instance::Run()/system::compleated::Run(),
// see details/xecs_game_mgr.cpp and details/xecs_system_inline.h) for now, even though the old
// xCore-provided profiler backend they used to compile against is gone - they may be wired to a real
// backend again later. Call sites use these WITHOUT a trailing semicolon (matching Tracy's own macro
// convention), so these must expand to nothing at all rather than a no-op expression statement.
#ifndef XCORE_PERF_FRAME_MARK
    #define XCORE_PERF_FRAME_MARK()
#endif
#ifndef XCORE_PERF_FRAME_MARK_START
    #define XCORE_PERF_FRAME_MARK_START(NAME)
#endif
#ifndef XCORE_PERF_FRAME_MARK_END
    #define XCORE_PERF_FRAME_MARK_END(NAME)
#endif
#ifndef XCORE_PERF_ZONE_SCOPED_N
    #define XCORE_PERF_ZONE_SCOPED_N(NAME)
#endif

//--------------------------------------------------------------
// COMPATIBILITY MACROS
//--------------------------------------------------------------
// A handful of small xCore macros xECS's code still uses directly. These are simple, self-contained
// stand-ins (not a port of xCore's actual assert subsystem, which has debug levels/breakpoints/etc
// that nothing here relies on) - just enough for the call sites to keep their original meaning.
#include <cassert>
#ifndef xforceinline
    #define xforceinline __forceinline
#endif
#ifndef xassert
    #define xassert(EXP) assert(EXP)
#endif
#ifndef xassume
    #define xassume(EXP) assert(EXP)
#endif

//--------------------------------------------------------------
// PREDEFINITIONS
//--------------------------------------------------------------
#include "xecs_predefinitions.h"

//--------------------------------------------------------------
// FILES
//--------------------------------------------------------------
#include "xecs_tools_meta.h"
#include "xecs_settings.h"
#include "xecs_event.h"
#include "xecs_event_mgr.h"
#include "xecs_component_type.h"
#include "xecs_component_entity.h"
#include "xecs_entity_xproperty_bridge.h"
#include "xecs_component_others.h"
#include "xecs_serializer.h"
#include "xecs_tools.h"
#include "xecs_tools_bits.h"
#include "xecs_scene.h"
#include "xecs_component_mgr.h"
#include "xecs_pool.h"
#include "xecs_archetype.h"
#include "xecs_archetype_mgr.h"
#include "xecs_query.h"
#include "xecs_query_iterator.h"
#include "xecs_prefab.h"
#include "xecs_prefab_mgr.h"
#include "xecs_system.h"
#include "xecs_system_mgr.h"
#include "xecs_game_mgr.h"
#include "xecs_prefab.h"
#include "xecs_editor.h"

//--------------------------------------------------------------
// INLINE FILES
//--------------------------------------------------------------
#include "details/xecs_component_type_inline.h"
#include "details/xecs_component_entity_inline.h"
#include "details/xecs_component_others_inline.h"
#include "details/xecs_tools_inline.h"
#include "details/xecs_tools_bits_inline.h"
#include "details/xecs_component_mgr_inline.h"
#include "details/xecs_pool_inline.h"
#include "details/xecs_archetype_inline.h"
#include "details/xecs_archetype_mgr_inline.h"
#include "details/xecs_system_inline.h"
#include "details/xecs_system_mgr_inline.h"
#include "details/xecs_query_iterator_inline.h"
#include "details/xecs_game_mgr_inline.h"
#include "details/xecs_query_inline.h"
#include "details/xecs_event_inline.h"
#include "details/xecs_event_mgr_inline.h"
#include "details/xecs_prefab_inline.h"
#include "details/xecs_prefab_mgr_inline.h"
#include "details/xecs_serializer_inline.h"
#include "details/xecs_scene_inline.h"

#endif