#ifndef XECS_BUILTIN_INSTANTIATIONS_H
#define XECS_BUILTIN_INSTANTIATIONS_H
#pragma once

// Phase 4/7 of the xECSV2 type-registration architecture plan: xECSV2's OWN built-in component
// types are the one case where more than one physical module needs to independently name the same
// T at its own compile time once xECSV2 is split into a shared library - every OTHER component type
// is either consumer/game-defined (only the one binary that declares it ever names it) or touched
// by the editor only generically, through an already-existing type-erased `info*` (see
// xecs_component_type.h's own comment on info_v<T> for the full reasoning). These ten `extern
// template` declarations mean every module that includes this header (instead of implicitly
// instantiating its own, disconnected copy of info_var<T>) imports the ONE instantiation xECSV2
// itself owns - see xecs_game_mgr.cpp for the matching explicit-instantiation DEFINITIONS.
//
// XECS_API expands to nothing in today's default (static/included-source) build, so this is a
// harmless, purely compile-time declaration in that mode - the real import/export behavior only
// activates once XECS_BUILD_SHARED is defined by the build (Phase 7's new CMake target).
namespace xecs::component::type::details
{
    extern template struct XECS_API info_var<xecs::component::entity>;
    extern template struct XECS_API info_var<xecs::component::parent>;
    extern template struct XECS_API info_var<xecs::component::children>;
    extern template struct XECS_API info_var<xecs::component::share_as_data_exclusive_tag>;
    extern template struct XECS_API info_var<xecs::component::ref_count>;
    extern template struct XECS_API info_var<xecs::component::share_filter>;
    extern template struct XECS_API info_var<xecs::component::entity_reference>;
    extern template struct XECS_API info_var<xecs::prefab::tag>;
    extern template struct XECS_API info_var<xecs::prefab::root>;
    extern template struct XECS_API info_var<xecs::editor::prefab_instance>;
}

#endif
