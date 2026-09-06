#ifndef XECS_PLUGIN_TOKEN_H
#define XECS_PLUGIN_TOKEN_H
#pragma once

namespace xecs::plugin
{
    // Explicit, engine-assigned identity for "which loaded plugin generation owns this
    // registration" - deliberately never inferred from a pointer, address range, or HMODULE. The
    // xECSV2 DLL-boundary audit's second probe (Phase 5) proved why that would be unsafe: Windows
    // reused BOTH the identical virtual address range and the identical module handle after a
    // FreeLibrary+LoadLibrary cycle of the very same DLL, so neither can tell "the same load" apart
    // from "a fresh reload" - only an explicit counter can.
    struct token
    {
        std::uint32_t m_Slot       {0}; // Which plugin "slot" this registration belongs to (slot 0 is reserved for the host program itself, which is never unloaded)
        std::uint32_t m_Generation {0}; // Bumped every time this slot's DLL is loaded/reloaded, so a stale token from a prior generation never aliases the current one

        friend auto operator <=> ( const token&, const token& ) = default;
    };

    // The well-known owner for every component registered by the host program itself - today,
    // everything, since there is no plugin DLL yet. RegisterComponents<T...>() defaults to this so
    // every existing call site (E29 included) keeps compiling and behaving exactly as before; a
    // future plugin loader mints a fresh, non-host token per load/reload and passes it explicitly.
    constexpr token host_v { .m_Slot = 0, .m_Generation = 0 };
}

#endif
