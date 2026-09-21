# getBit vs findIndexComponentFromInfo (cross-DLL component lookup)

> xECSV2 gotcha: archetype.getComponentBits().getBit(info_v<T>.m_BitID) can read false/invalid/wrong-binary's-value at some call sites even though the component IS present; findIndexComponentFromInfo (same-binary) or a GUID-resolved lookup through the shared registry (cross-DLL) is the reliable check. Recurred 7 times same-binary (2 real crashes) plus one fully-root-caused cross-DLL case in SerializeGameState. Standing user directive: anything file/serialization-related must resolve identity via GUID, never a runtime-mutated bit/handle
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-09-06).

Found while verifying `xecs_multientity_prefab_architecture` (note pending migration)'s nested-prefab-instance recursion: a
new early-return branch checking `archetype.getComponentBits().getBit(xecs::component::type::info_v<T>.m_BitID)`
consistently read the bit as `invalid_bit_id_v` (0xffff) or simply false at THAT call site, even
though the SAME entity's SAME pool, checked via `pool->findIndexComponentFromInfo(info_v<T>) >= 0`
in the SAME function, correctly found the component present. This wasn't a one-off - the same
mismatch reproduced at 5 independent call sites across two rounds (two + one in
`xecs_prefab_mgr_inline.h`/`xecs_reference_remap_inline.h`'s `CreatePrefabInstance`/
`ComputePrefabInstanceSaveOverlay`, two in E29's `AttachPrefabInstanceComponent`/`FindPrefabInstance`
- the second round found via a second AI's review, not my own testing, since `FindPrefabInstance`'s
bug never happened to surface in either of my own test scenarios), always resolved by switching to
`findIndexComponentFromInfo`. Worth grepping for `getComponentBits().getBit(` against
`info_v<xecs::editor::prefab_instance>` (or any other runtime-registered, non-force-registered
component) again if this class of bug recurs - it's easy to miss since it doesn't crash, it just
silently returns the wrong answer at one call site while an equivalent check elsewhere is fine.

**Not fully root-caused.** `xecs::component::type::info_v<T>` is a proper C++17 inline variable
template (single global storage guaranteed by the standard), so this shouldn't be possible in theory.
Suspected contributing factor: `xecs.h` includes ALL of xECSV2's `_inline.h` detail headers directly
(confirmed - `xecs.cpp` and every example `.cpp` like `E29_LevelScene_Editor.cpp` each
`#include "xecs.h"` in full), and many `mgr::`-qualified member function definitions in those headers
(`CreatePrefabInstance`, `CreateNestedPrefabInstance`, `CloneEntityIntoPrefabGroup`,
`CreatePrefabFromEntity`, `Save`, `EnsureLoaded`, and likely others) are NOT marked `inline` despite
being defined outside their class body in a header - technically an ODR violation if truly compiled
into multiple translation units without complaint, though the linker never reports LNK2005 for this
in practice. Whether this non-`inline` pattern is actually causing the bit-read mismatch, or whether
it's an unrelated `consteval`+`mutable` runtime-write quirk (see
`xproperty_null_fnptr_designated_init_crash` (note pending migration) for a related class of danger in this codebase's own
constexpr-with-mutable-field convention), was not conclusively determined - the investigation was
time-boxed once a reliable workaround was found.

**The reliable pattern going forward**: for "does this entity/archetype have component T" checks,
prefer `pool->findIndexComponentFromInfo(xecs::component::type::info_v<T>) >= 0` over
`archetype.getComponentBits().getBit(info_v<T>.m_BitID)`, matching how `SaveGroupMember`/
`LoadGroupMember` already do per-component lookups. `getComponentBits()`/`.getBit()` is still fine
for its ORIGINAL purpose (building up a `bits` object to construct/compare whole archetypes via
`AddFromComponents`/`ClearFromComponents`/`Superset`/`Subset`) - the fragility only showed up for a
POINT CHECK ("is bit X set") comparing a bit index captured at one call site against bits computed
elsewhere.

**Update 2026-09-06: recurred a 6th time, this one a real, user-facing, 100%-reproducible CRASH**, not
just a silent misbehavior - `CloneEntityIntoPrefabGroup`'s own "opaque leaf" check (is Source itself a
nested prefab instance? - `xecs_prefab_mgr_inline.h`, the function that captures live entities into a
NEW prefab group) used this exact fragile `.getBit()` idiom too, and I missed it during the earlier
sweep because I only fixed sites AS THEY WERE REPORTED (once by my own testing, once by a second AI's
code review) rather than doing one exhaustive grep across the whole codebase the FIRST time this
pattern was identified as unreliable. Symptom: selecting a plain entity + an EXISTING multi-entity
prefab instance together and making a new prefab from the selection crashed with
`Assertion failed: Entity.isValid()` in `xecs_component_mgr_inline.h:43` - the opaque-leaf check
silently failed to fire, so the code fell through into recursing INTO the multi-entity instance's own
internal children as if they were plain members of the new outer group, corrupting it.

**Lesson: the FIRST time this pattern is found broken anywhere, grep the entire codebase for
`getComponentBits().getBit(` immediately and audit every hit against `xecs::editor::prefab_instance`
(or any other non-force-registered component) in one pass - don't fix occurrences one at a time as
they're reported, since each unfixed occurrence is a LATENT, exactly-as-severe bug waiting for the
right code path to hit it.**

**That "6 sites, 0 remaining" sweep was ITSELF wrong - a 7th occurrence survived it**, found only
because the user hit ANOTHER crash (same assert, same location) immediately after the "fixed" build:
`CollectGroupMembers`'s own opaque-leaf check (the SAVE-side counterpart of
`CloneEntityIntoPrefabGroup`'s). The earlier sweep grepped for the exact literal expression
`getComponentBits().getBit(xecs::component::type::info_v<xecs::editor::prefab_instance>` - but this
call site (following the SAME structured-binding style used elsewhere in this file:
`if (const auto Bit = info_v<T>.m_BitID; Bit != invalid && Archetype.getBit(Bit))`) extracts the bit
into a local `Bit` variable FIRST, so the literal string the grep searched for never appears in the
source at all - only `...getBit(Bit)` does. **The correct sweep is: grep bare `\.getBit\(` (no
`info_v<...>` in the pattern) across the whole codebase, then manually read every hit's surrounding
few lines to see what component it's actually checking** - a literal-string grep against one specific
call-site's exact phrasing will always miss any site written in a different (but equally common)
style. Redid this properly on 2026-09-06 (`grep -n '\.getBit\(' ...` across all 3 touched files,
manually reviewed each): only the already-fixed prefab_instance sites and unrelated
children/parent/tag/generic-bit-loop checks remained. If this bug surfaces an 8th time, that's the
sweep to redo - not a re-run of the old literal-string grep.

Still worth eventually pursuing the real root cause (marking the relevant `xecs_*_mgr_inline.h`
member function definitions `inline` - see below) rather than continuing to route around it
reactively.

**Update 2026-09-07: a DISTINCT, fully-root-caused variant of this same symptom shape, specific to
the shared-library (xECSV2.dll) build - do not conflate with the same-binary mystery above.**
`xecs::game_mgr::instance::SerializeGameState` (`details/xecs_game_mgr.cpp`) directly read
`xecs::component::type::info_v<xecs::prefab::tag>.m_BitID` in two places (filtering prefab
archetypes on write). It is the ONLY xECSV2 function that is both `XECS_API`-exported (its body
compiled exactly ONCE, into `xECSV2.dll`) AND touches a built-in type's `info_v<T>` directly by
name - every other built-in-touching function (Scene/Level/Prefab persistence) is still `inline`,
recompiled per-TU into whichever binary calls it, which in this project is always the host EXE - the
same binary that also does every `RegisterComponents<T...>()`/`RegisterSystems<T...>()` call. Since
`info_v<T>` for a built-in type is a per-BINARY inline static (confirmed empirically during Phase 7 -
see `xecs_component_type.h`'s own comment on `info::m_BitID`), the EXE's own copy gets correctly
registered/locked, but `xECSV2.dll`'s own, separate copy - the one `SerializeGameState`'s
compiled-once body actually reads - never does, sitting forever at its sentinel (a 5-digit "invalid"
value). Crashed live: `Assertion failed: Bit >= 0 && Bit < max_component_types_v` the first time
E29's Phase 8 async-reload work called `SerializeGameState` for a fast play-session snapshot, 100%
reproducible on ANY call once a Game.dll reload happened while playing. Confirmed via a standalone
smoke test with THE SAME prefab/entity_reference/binary-mode content in a single-binary build (no
crash - proves binary mode and the components themselves are fine) versus reading the live crash
log's exact bit value in E29's actual shared build (valid, 11 - proves the EXE's own copy was fine,
narrowing it to a binary-local storage mismatch).

**Fix, per the user's own explicit standing principle** ("anything that has to do with files should
always use GUIDs of the type, never the bits - those are just runtime things"): resolve the type via
its `m_Guid` (a compile-time constant, identical across every binary that instantiates `info_v<T>`,
unlike `m_BitID` which is runtime-mutated only wherever registration happens) through
`m_ComponentMgr.findComponentTypeInfo(Guid)` - itself safe because `s_Registry`
(`m_ComponentInfoMap`) IS correctly `XECS_API`-shared. Two call sites fixed by computing
`PrefabTagBit` once via this lookup at the top of the function instead of touching
`info_v<xecs::prefab::tag>.m_BitID` directly. Generalizes the existing `findIndexComponentFromInfo`
lesson above to the cross-binary case: same failure shape (a bit index read as invalid/wrong despite
the component being present), same underlying principle (don't trust a runtime-mutated per-binary
field; resolve through a GUID-keyed, properly-shared lookup instead), different actual mechanism (DLL
boundary, not same-binary weirdness).
