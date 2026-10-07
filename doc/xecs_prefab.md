<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / Prefabs

<h3><details><summary><i><b>Related Topics </b>(Click to open)</i></summary>

* [Component Serialization](xecs_component_serialization.md)
* [Component Properties](xecs_component_properties.md)
* [Component Typedef](xecs_component_typedef.md)
* [Scene entity references](ecs_scene_entity_references.md)
* [Scene Ranges](xecs_scene_ranges.md)
* [Scene file format, details about entities](xecs_scene_serialization_entity.md)
</details></h3>

Prefabs also known in the game industry as (blue-prints, or archetypes) is a way to to define an Entity or Scene with a specific set of values in them for the shake of cloning them (know as prefabs instances) at runtime or in the editor. There are two kind of prefabs:

1. [Entity Prefabs](xecs_prefab_entity.md) - Which means we are dealing with a single entity
2. [Scene Prefabs](xecs_prefab_scene.md) - Which means that we are dealing with a group of entities (where there is not root entity)

If an entity has children is it a Entity Prefab or a Scene Prefab?<br>
This are still consider Scene Prefabs because there is more than one. However here the Scene prefab Pivot may be removed in favor of the single parent, since both of them would represent exactly the same case.

There are two Context in which prefabs operate:

1. **Runtime** - The function calls will be made by systems. 
2. **Editor** - The user will be able to create prefabs and prefabs instance using the editor. This context provides extra functionality over the Runtime such property overrides and component overrides. 

Check more details on [Prefab Editor](xecs_editor_prefab.md)

## Prefab GUID vs EntityID

The prefab GUID **(xecs::prefab::guid)** is the official Identifier of a Prefab (not matter if it is an entity prefab or a scene prefab). This global identifier will also be used as its resource GUID. The Prefab EntityID **(xecs::component::entity)** is the temporary ID of a prefab. Please note that every time a prefab is loaded the EntityID will be different, the GUID in the other hand is persistent. This is why it is discourage to use the entity id for prefabs.

~~~cpp
constexpr static auto prefab_rctype_v = xcore::guid::rctype<>{ xcore::guid::plugin<>("xECS/Prefab"), "Prefab" };
using                 guid            = xcore::guid::rcfull_singletype<prefab_rctype_v>;
~~~

The runtime will have a hash-map that will map between the xecs::prefab::guid to the actual EntityID. 

Prefabs are consider assets and when you save them they will have assets names. However all assets must be converted to resources. It is the job of the Game Editor to serialize a prefab asset into a resource. Ones it has become a resource it will follow the resource naming convention: **(resource_instance_GUID.resource_type_GUID)**. This means that the file name will be hard to read.


## Serializing Prefabs

Serializing prefabs is very similar to how a [scene serializes](xecs_scene_serialization.md). Scene groups entities base on where they were created, usually in the editor under the context of a particular scene. Prefabs don't belong to any scene, prefab instances do. Prefabs are thought to be entities that are globally accessible by any scene and always loaded, they are in the truer sense global objects. To know which entity is a prefab it uses a [exclusive_tag](xecs_component_type_tag.md) named ***xecs::prefab::tag***.

### On disk: a prefab is a scene

Since phase 1 of the prefab plan (`documentation/Editors/prefabs_plan.md`) a prefab is stored with the scene's format and the scene's code (`xecs::scene::details::ReadEntityFile`, `WriteEntityFile`, `CollectComponentDependencies`):

| File in `Descriptors/Prefab/<b0>/<b1>/<guid>.desc/` | What it holds |
|---|---|
| `Descriptor.txt` | `xecs::prefab::descriptor`: `Root` (the member every other descends from), `ActiveEntities` (the members, sorted), `EntityNames` (the names the members had in their scene) |
| `entity_db/<b0>/<b1>/<id>.entity` | one member, in the scene's entity format (`PermanentId`, its components; references are member ids). `prefab::tag` and `prefab::root` are never written: load adds them |
| `ComponentDeps.txt` | the component types the members use, with their module, as a scene's |

* `prefab::mgr::Save` writes every member (a prefab is saved whole), then the descriptor (temp file and rename: it is what says the folder holds this format), then `ComponentDeps.txt`, and removes what is not the prefab any more (an old `Entity.txt`, the files of members that are gone).
* Rules checked by `Save` before anything is written: one root, every member descends from it, and what is written references only the prefab's own members. A reference to a live entity outside refuses the save; one to an entity that no longer exists is written as null.
* Making a template keeps those rules: `CloneEntityIntoPrefabGroup` moves the references of the copies to the copies they point at (a reference to anything outside the group becomes null, with a warning), and gives each copy the name its source has in its scene.
* `prefab::mgr::EnsureLoaded` reads a member file the way a scene does (every member staged, none created unless all read), adds `prefab::tag` (so no system and no builder sees a template) and `prefab::root` to the root. A template's bookkeeping (`m_PrefabGroups`) is an `xecs::scene::instance` held by the prefab manager, never in the scene manager's list.
* The old format (every member in one `Entity.txt`, `LocalId` records) is still read (`details::EnsureLoadedOldFormat`); its ids are kept as the permanent ids and the next `Save` converts it. The editor's `UpgradeProject` converts all of them at once.
* The members' ids are `xecs::scene::permanent_id`: 64 bits since phase 2, see [Entity ids](xecs_scene_entity_ids.md) for their text form and what is on disk.

### An instance in a scene is a recipe

Since phase 3 of the prefab plan a scene stores a prefab instance as **one entity file**: the entity that carries `xecs::editor::prefab_instance` (its *recipe*: which prefab, and what this instance does differently). Its members are not stored; they are spawned from the prefab when the scene loads (`xecs_prefab_recipe_inline.h`).

* **Member addresses** (`xecs::editor::member_address`): a member is named by its permanent id inside its prefab; a member of a nested instance by the chain of ids that crosses each instance boundary (`[outer member id, inner member id]`). The root of the instance is the empty address. The root of a variant (a prefab whose root is an instance of another) adds no element, so its members are addressed as its base's. Reordering, inserting or removing children in a prefab changes no address.
* **Member ids**: a member's id in the scene is `xecs::scene::DeriveMemberId(instance id, address)` (a 63-bit hash above 32 bits; the root keeps the instance's id). The same every time the instance is spawned, never stored. The scene knows its members in `instance::m_InstanceMembers` (instance, address, the name it was spawned with): they are in `m_LocalToRuntime` like any entity, but have no file and are not in the descriptor's `ActiveEntities` (a name is written only when it was renamed in the scene).
* **The recipe** (`prefab_instance`, `Format` 1): property overrides (`m_lComponents`, each with its member's address), component diffs (`m_ComponentDiffs`: added or removed, per member; a component added to a member keeps its data as overrides of every one of its properties), and hierarchy diffs (`m_HierarchyDiffs`: the members the instance removed; and, informational, the members with entities of the scene under them). An entity value of an override is written `#<n>`, `n` the reference as the file encodes it. The root's own added components are written in its file as before; its `children` are not (its members are spawned, and an entity of the scene under the instance says so in its own `parent`).
* **Load** (`EnsureLoaded`): every file read into staging; each recipe's prefab turned into a *plan* (`recipe::MakePlan`: every member in order, the nested instances expanded with their own recipes); the members staged from the templates with their derived ids, parents, children and references written as those ids; every recipe's overrides applied in staging (nested ones first), so the builders, which run when the entities are created, see them; created; references remapped; the entities of the scene under a member joined to its children list (`recipe::LinkChildren`).
* **Save** (`SaveScene`, `SaveEntity`): a change to a member is a change to its instance's recipe (the instance is written instead); the recipe is refreshed from what the members are now (`recipe::RefreshRecipe`) before it is written.
* **Orphans**: what addresses a member the prefab no longer has is kept (never moved to another member) and the editor lists it (`ListPrefabOverrides`, `RemoveOrphanOverrides`).
* **Apply** (`recipe::ApplyToPrefab`): property overrides are written into the template members (a reference to a member of the instance becomes a reference to the prefab's member; anything else is null); component diffs add and remove the components; removed members leave the prefab; the entities of the scene under the instance join the prefab and become members of the instance (they take their derived ids). What concerns a member of a nested instance is written into that nested instance's recipe in the prefab.
* **Converting** (decision D4): an instance written before recipes (`Format` 0: every member an entity file of the scene, overrides addressed by child-index paths) is converted once the scene is live (`recipe::ConvertOldInstances`): its members are paired with the prefab's by position, take their derived ids (what referenced them is written again), their values that differ from a fresh spawn become overrides, and the old member files go at the next save. A nested recipe in a prefab written before phase 3 is converted when the prefab loads. `UpgradeProject` (editor) loads and saves every scene that still has one.
* The editor places an instance with `prefab::mgr::Spawn` (the call a game spawns with) and registers what it made with the plan (`recipe::InstantiateInScene`).

### Spawning: the baked plan

`prefab::mgr::Spawn(Guid, Count, Callback)` is the game's call (and `CreatePrefabInstance(Count, Guid, Callback, bRemoveRoot=false)` goes through it for a prefab of the scene format). The first spawn of a prefab bakes it (`mgr::getBaked`, `xecs_prefab_recipe_inline.h`):

* the plan: every entity an instance is made of, parents first, nested prefabs expanded (a nested prefab costs what a flat one of the same size costs);
* each member's data with the nested recipes applied, kept in an inert entity (`prefab::tag`: no system sees it, no builder runs on it), its references null;
* where each member's references are (component, byte offset) and which member each names; a reference inside a container the component holds is kept by its order and patched through the property walk.

A spawn makes each member `Count` times in one call (a copy of its baked entity), then writes every parent, children list and reference from the table, then runs `Callback` on the roots. A member whose components builder systems take (physics colliders) is staged and built one at a time, like a scene load, when the world runs builders (the game, Play). A scene load stages an instance's members from the same baked data and applies the instance's own recipe on top (`recipe::StageMembers`). Saving or making a prefab drops every baked plan (`InvalidateBaked`); the next spawn bakes again. The only heap allocations of a spawn are the children lists (`children::m_List` is a `std::vector`). A reference held by a SHARE component of a member is not patched (a share belongs to its whole family).

### A prefab open in an editor

Since phase 5 of the prefab plan an editor opens a prefab as **a scene of its own guid**: `scene::instance::m_bPrefabDocument` is set before the scene is loaded (`FindOrCreate` + `RequestLoad`), and the scene manager then reads and writes the prefab's folder instead of a scene's (`details::SceneFolder(mgr, guid)`), reads its descriptor as a prefab's (`xecs::prefab::document::LoadDescriptor`: `Root`, `ActiveEntities`, `EntityNames`; the old `Entity.txt` format is refused: `UpgradeProject` first) and saves it as a prefab (`document::Save`, called by `scene::mgr::SaveScene`). The entities are ordinary live ones (no `prefab::tag`); a nested instance is a recipe, as in any scene.

* **Save** writes the whole prefab (there are no pending-change counters to resolve): it first checks the rules of a prefab - exactly one entity without a parent (the root), and what is written references only the prefab's own entities - and writes nothing when one fails. Then every entity that is not a member of a nested instance (`SaveEntity`, which refreshes a recipe first), the descriptor (temp file and rename; the `Game` it names is kept, it is the editor's), `ComponentDeps.txt`, and removes the files of the entities that left the prefab. Saved into its own folder, it drops the template this world holds of the prefab (`mgr::DropTemplate`) and the plans baked from it.
* `instance::m_FolderOverride` points the scene at another folder for the time of a call: `SaveScene` then writes a **snapshot** of the document, and a load reads it back (an editor's undo of a change that came from elsewhere).
* **One writer**: `mgr::m_pRedirect` (`save_redirect`, set through `xECSEditor::SetPrefabSaveRedirect`) is asked by `mgr::Save` whether another editor holds the prefab as its document; if so the saved state goes to a new folder (`SaveTo`) and the editor is handed it (`m_pDeliver`) instead of the prefab's file being written.
* `prefab::descriptor::m_Game` is the Game the prefab plays with in its editor (a Level's `Game`): `mgr::Save` and `document::Save` keep what the file says.
* `mgr::FindBakedMember(Guid, Address)` is the member as an instance starts from it, the recipes of nested instances applied (`recipe::FindTemplate` is the template member of the prefab that owns it: the inner prefab's own value).

### Live update

Since phase 6 of the prefab plan the instances in a world are brought up to date when their prefab changes (`recipe::LiveUpdate(Mgr, Changed, bDropTemplates)`, `xECSEditor::LiveUpdatePrefab`): every instance root of an active scene whose plan holds a changed prefab (it is the instance's, or one it nests however deep) is spawned again from the prefab with its recipe - what a save and a load of its scene would make of it, without either: nothing is written, nothing is marked pending, so the scene does not turn dirty. Members keep their ids (derived), so references to them, the editor's selection and undo still name them; whatever referenced an old entity (a parent's children list, an entity of the scene under a member, any reference, the tables of external entities of every scene) is remapped to the one that replaces it; a member the prefab lost is gone and a reference to it is null (an override on it stays, an orphan).

* In two halves because a recipe is refreshed against the template it was made from (a component or member the prefab gains must not read as one the instance removed): `PrepareLiveUpdate`, before the template changes, refreshes each recipe and writes each root as its file would hold it to a temp folder; `FinishLiveUpdate`, after it, drops the changed templates when `bDropTemplates` (the change is on disk), drops every baked plan, reads the roots back and replaces each instance (an instance whose prefab no longer loads is left as it is).
* Who calls it: `document::Save` (the Prefab Editor's own world: its context scenes), `recipe::ApplyToPrefab` (the other instances in the applying world: their recipes are prepared before the template changes, the applied instance is left out), and the editor for every other editor's world when a prefab's file changes (`save_redirect::m_pSaved`, called by `mgr::Save` after it wrote the prefab's own folder). A world that is playing is never updated: a running game keeps what it started with.
* `MakePlan` leaves out a nested instance that closes a cycle (A holds B holds A) with a warning instead of never ending; `recipe::Uses(GameMgr, Prefab, Used)` (`xECSEditor::PrefabUses`) says whether an instance of `Prefab` is made of `Used`, which is how the editor refuses to make such a cycle.

## Creating a Prefab Instance

When creating a prefab instance you must have the **xecs::prefab::guid** for the prefab where the instance will be based on. If the prefab has children all the children will also be instantiated. Note that in the function when creating the instance you can add/remove and initialize any component you like. Please note that any reference to any other entity won't be updated right away. 

For more info check out [Entity Prefabs](xecs_prefab_entity.md) and [Scene Prefabs](xecs_prefab_scene.md) as they talked about the specifics for each case.

---