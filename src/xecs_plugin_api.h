#ifndef XECS_PLUGIN_API_H
#define XECS_PLUGIN_API_H
#pragma once

// Phase 8 of the xECSV2 type-registration architecture plan: the ABI a hot-reloadable "Game.dll"
// implements, so its own component/system types register into the SAME live, shared world an
// editor (E29) drives - see the plan's Phase 8 for the full reload-sequence design.
//
// Deliberately much smaller than a general-purpose plugin ABI, e.g. E27_NodeOS's own
// xnode_os_plugin_api.h (pure-virtual node/factory interfaces + an ixnode_os_host callback
// interface) - NodeOS needs that machinery from scratch because a "node" has no other safe,
// generic, cross-DLL identity. xECS's OWN component/system registration
// (RegisterComponents<T...>()/RegisterSystems<T...>()) already IS that safe, GUID-based, generic
// mechanism, once xECSV2 itself is built as a shared library (Phase 7) - there is nothing left for
// a Game.dll's own ABI to invent. A Game.dll's whole job is to call those SAME template member
// functions on the host's own xecs::game_mgr::instance, compiled fresh into Game.dll's own
// translation unit (safe under this project's closed-toolchain assumption - same compiler, same
// /MDd flags, same xecs.h header, so identical class layout - see xnode_os_plugin_api.h's own top
// comment for why that assumption is what makes ANY C++ type cross a DLL boundary safely here).
//
// Requires xECSV2 built as a shared library (XECS_BUILD_SHARED_LIBRARY=ON) - a Game.dll linked
// against a statically-linked/included-source xECSV2 would get its own disconnected component
// registry (exactly the bug Phases 1-6 fixed), making a hot-reloadable Game.dll pointless.
//
// Two entry points, not one combined "Register" call - ORDER MATTERS and this enforces it
// structurally: xecs::component::mgr::LockComponentTypes() (called internally by the FIRST
// RegisterSystems<...>() call, host's or plugin's) permanently locks the component registry, so
// EVERY RegisterComponents<...>() call - the host's own types AND the plugin's - must happen
// before ANY RegisterSystems<...>() call. The host's reload sequence is exactly:
//   host RegisterComponents -> XecsPlugin_RegisterComponents -> host RegisterSystems ->
//   XecsPlugin_RegisterSystems
// A single combined entry point would force the plugin to choose one relative position and make
// the other order impossible to express.
extern "C"
{
    using xecs_plugin_pfn_register_components = void( xecs::game_mgr::instance& GameMgr, xecs::plugin::token Token ) noexcept;
    using xecs_plugin_pfn_register_systems     = void( xecs::game_mgr::instance& GameMgr ) noexcept;

    // Called once, right before the host unregisters this plugin's token
    // (xecs::component::mgr::UnregisterPlugin) and FreeLibrary's the module - a chance for the
    // plugin to release anything it privately allocated outside the ECS world (rare: most game
    // state lives IN the world - entities/components - which the host already owns and tears down
    // itself as part of the reload sequence, not through this callback).
    using xecs_plugin_pfn_unregister            = void( xecs::plugin::token Token ) noexcept;
}

// Fixed, undecorated names resolved via GetProcAddress - same convention as
// XNODE_OS_CREATE_FACTORY_NAME - regardless of what the DLL file itself is named.
#define XECS_PLUGIN_REGISTER_COMPONENTS_NAME  "XecsPlugin_RegisterComponents"
#define XECS_PLUGIN_REGISTER_SYSTEMS_NAME     "XecsPlugin_RegisterSystems"
#define XECS_PLUGIN_UNREGISTER_NAME           "XecsPlugin_Unregister"

#endif
