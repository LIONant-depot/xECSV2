# Pool reallocation hazard for live component references

> Recurring xECSV2 bug class: a live T& into an entity's own pool slot goes stale if a recursive call creates another entity in the same archetype/pool; fix pattern is snapshot-then-deferred-writeback
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-09-05).

Holding a live reference (e.g. `children&` obtained via `getComponent<children>(...)`, or a pointer
captured from a `CreateEntities` callback) into an entity's own pool-component slot is unsafe across
any RECURSIVE call that creates/moves OTHER entities, if a sibling happens to land in the exact same
archetype/pool - pool storage can reallocate underneath the held reference, silently corrupting or
writing through a dangling pointer. This isn't a hypothetical: it caused a real, hard-to-find bug
while building `xecs_multientity_prefab_architecture` (note pending migration) (found via a live assert and confirmed via
extensive value-level diagnostic logging).

**Fix pattern, applied consistently everywhere this was found**: snapshot the list into a local
`std::vector` copy BEFORE recursing (read-only iteration afterward is safe), accumulate results into
a SEPARATE new list during the recursion, then re-fetch the entity's pool details FRESH (never trust
a pool/details reference captured before the recursion started) and write the accumulated list back
via `std::move`.

**Every site this was found and fixed** (`dependencies/xECSV2/src/details/xecs_prefab_mgr_inline.h`
unless noted):
- `CloneEntityIntoPrefabGroup`'s children-population loop.
- `Save`'s `CollectGroupMembers` walk.
- The 4 branches (share/non-share x parent/no-parent) of the low-level `CreatePrefabInstance`'s
  children-remap loops - pre-existing engine code, not introduced this session, fixed via a new
  shared `RemapChildrenSafely` lambda used by all 4.
- 2 occurrences in the "Scene-Prefab" branch of the templated
  `CreatePrefabInstance<T_ADD,T_SUB,T_CALLBACK>` overload.

**When to suspect this again**: any future xECSV2 code that (a) gets a live component reference via
`getComponent<T>(...)` or a pool pointer, then (b) makes a recursive/nested call that itself creates
entities, is a candidate - check whether the held reference is used again AFTER the recursive call
without being re-fetched.
