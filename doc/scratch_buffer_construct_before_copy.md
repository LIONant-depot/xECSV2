# Scratch buffers: construct before copy

> A std::vector<std::byte> scratch buffer used as a stand-in for a component must be placement-constructed (m_pConstructFn) before m_pCopyFn/operator= runs on it, or non-trivial members (esp. std::vector) corrupt under MSVC debug STL
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-09-05).

`xecs::scene::mgr::SaveEntity` (dependencies/xECSV2/src/details/xecs_scene_inline.h) copies a live
component into a temporary `std::vector<std::byte> Scratch(pInfo->m_Size)` before serializing it.
That vector constructor only zero-fills raw bytes - it never runs the component type's real
constructor. For POD components (`m_pCopyFn == nullptr`, falls back to `memcpy`) that's harmless.
But `m_pCopyFn`'s body is a real `operator=` call, and assigning into memory that was never actually
constructed is undefined behavior for any type containing non-trivial members like `std::vector`:
MSVC's debug STL keeps a per-container "proxy" object that only a genuine constructor sets up, and
zeroed-but-never-constructed memory doesn't have one.

**Symptom**: "can't dereference invalidated vector iterator" assert, heap-layout-dependent (a
heisenbug) - reproduced reliably in the real long-running app but NOT in short-lived headless test
processes, since the corrupted-but-still-technically-zeroed proxy pointer only actually gets walked
once something else's heap allocations happen to collide with it.

**Fix**: call `pInfo->m_pConstructFn(Scratch.data())` (if non-null) before `m_pCopyFn`/`memcpy` -
exactly what `pool::instance::Append()` already does for every real pool slot. Checked the rest of
the codebase for the same pattern (`m_pCopyFn(` call sites): the only other occurrence
(`xecs_game_mgr.cpp`'s `EditorSetEntityComponentProperty`, using `xcore::memory::AlignedMalloc`
instead of a vector - even less safe, no zero-fill at all) is inside an `#if 0` block, so it's dead
code and wasn't touched.

**How to apply**: any future "copy a component into a scratch/temp buffer before doing X" code in
this codebase must construct that buffer first if the component type isn't POD. Grep for
`m_pCopyFn(` call sites when adding new scratch-buffer patterns and check the destination was
constructed, not just zero-allocated.

See also the project's standing working rule (how this was actually found - via property-visit
logging pinpointing exactly which array walk the crash landed on) and
`xecs_prefab_override_design` (note pending migration) (the feature this bug was found while building).
