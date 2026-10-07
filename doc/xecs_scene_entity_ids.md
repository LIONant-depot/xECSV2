<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / [Scene](xecs_scene.md) / Entity ids

An entity of a scene (and a member of a [prefab](xecs_prefab.md), which is stored as a scene) is known by its **permanent id**: `xecs::scene::permanent_id`, scoped to the scene that owns it, the same every time the scene is saved and loaded. Entities that point at each other, undo, selection and the commands all name an entity by it.

Since phase 2 of the prefab plan (`documentation/Editors/prefabs_plan.md`) the id is **64 bits** (`std::uint64_t`). The ids the editor mints are still the GUID-like 32-bit values they always were (`xscene::NextFreeEntityId`); the rest of the range is for ids that are derived: since phase 3 a member of a prefab instance has the id `xecs::scene::DeriveMemberId(instance id, member address)` (a 63-bit hash, always above 32 bits so it never takes a minted id, printed with 16 digits; never stored: the same each time the instance is spawned). See [Prefabs](xecs_prefab.md), "An instance in a scene is a recipe".

| Rule | Why |
|---|---|
| `0` is `invalid_permanent_id_v` | as before |
| the top bit is clear: an id is at most `max_permanent_id_v` (`0x7FFF...F`) | an entity reference is an `int64` in the file: positive is an id of the same scene, negative is `-(index into the external table) - 1` (`xecs_reference_remap_inline.h`). `CreateEntity` and `InstantiatePrefab` refuse an id with the top bit set |

## Text form

`xecs::scene::FormatPermanentId` (and `FormatPermanentIdW`): **8 hex digits when the id fits in 32 bits, 16 otherwise** - every id of before the widening reads exactly as it did. `ParsePermanentId` takes either, in any case, and shorter text (`7e5712` is `007E5712`). The commands, the listings (`ListEntities`, `ListFolders`), the logs and the names of entity files use it (`xscene::commands::ParseEntityId` / `FormatEntityId` are the same functions for the command line).

## On disk

| Where | Before | Now |
|---|---|---|
| `entity_db/<low byte>/<next byte>/<id>.entity` | `<id>` with 8 hex digits | the same name for an id that fits in 32 bits; 16 hex digits for one that does not |
| `EntityInfo` record of an entity file | `{ PermanentId:g  nComponents:d }` | **unchanged**: an id that fits is written there, whole. An id that does not fit has the record `[ EntityId64 ] { Id:G }` right after `EntityInfo` (`PermanentId` then holds the low 32 bits and is ignored by the reader) |
| `Descriptor.txt` (scene and prefab): `ActiveEntities`, `EntityNames`, the members of a folder, `ExternalRefs`' `ParentEntity`, a prefab's `Root` | `;u32` rows | `;u64` rows |
| the old prefab format (`Entity.txt`, `LocalId`, `RootLocalId`) | 32 bits | read as 32 bits; the next `Save` writes the scene format |

An entity file of an id that fits in 32 bits is therefore **byte for byte what it was**, and an old editor reads it. A descriptor changes when its scene is next saved (its `;u32` rows become `;u64`).

Reading the old data: the entity file reader asks for the 32-bit column and then for `EntityId64` if the next record is that one. The descriptor is read by the xproperty serializer, which widens an unsigned integer read from a file into a wider member (`xproperty::sprop::WidenToMember`, `property_sprop_getset.h`): a `;u32` row fills a `u64` member. Widening is the only conversion it makes.

## Tests

`smoke_test_scene_ids.cpp` (run by `smoke/test_entity_ids.py`): the text form; ids of every width in two scenes (small, `0xFFFFFFFF`, `0x100000000`, a 63-bit one, the largest) saved, the files checked, a fresh world loading them (references, the external table, names, folders, three ids with the same low 32 bits), saving again changing no byte, `DiscoverEntityIds`, a scene whose descriptors are rewritten with `;u32` rows, and a prefab with big member ids and one whose descriptor has `;u32` rows.
