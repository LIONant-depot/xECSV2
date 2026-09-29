<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>

# [xECS](xecs.md) / Builder Components

| STATUS:<br>:page_with_curl: | **Phase 1 implemented** (flag, builder systems, build plans, scene load, editor Play). Not staged yet: runtime prefab spawning and typed `CreateEntity` - see *Implementation status* at the end. First consumer: xLION physics. |
|:---:|---|

## The problem

Some components are only useful to **create** an entity. Their data is meant for another system that
will own it from then on. For example, the collider sizes, friction and damping of a physics entity are
given to Box3D, which creates the body and its shapes and becomes the owner of that data.

After that, keeping those components on the entity is useless at best and misleading at worst: the first
time something calls a Box3D setter, the copy on the entity is stale. This isn't about transforming data
inside the ECS (Unity's baking problem). It's a **handoff**: data *and ownership* move to a system
outside the ECS, and the entity keeps only a handle to it.

## Terms

| Term | Meaning |
|---|---|
| **Builder component** | A normal component type marked with a flag. It exists only while the entity is being created, and never reaches the final archetype. |
| **Builder system** | Consumes builder components during creation and hands their data to the owning system (Box3D, audio, navmesh...), writing the handle it gets back into a normal component. |
| **Owner** | The system that holds the real data after the handoff. Every later read or write goes through its API, using the handle. |
| **Handle component** | A normal (non-builder) component holding the owner's handle, e.g. `physics_body::m_BodyId`. |

Not to be confused with **recipe components**, a separate, editor-only concept. Adding a recipe to an
entity converts it to a spec (adds/removes components, e.g. "static physics entity"), after which the user
can change anything. Recipes are an authoring action and have nothing to do with creation-time building.

## Declaring a builder component

A flag on the existing typedefs (`data`, `share`, `tag`), not a new `component::type::id` kind, so sort
order and pool layout are untouched:

```c++
struct physics_body_properties
{
    constexpr static auto typedef_v = xecs::component::type::share
    { .m_Guid     = xecs::component::type::guid{ "xlioncore::physics::physics_body_properties" }
    , .m_pName    = "PhysicsBodyProperties"
    , .m_bBuilder = true
    };
    ...
};
```

`component::type::info` gains `m_bBuilder:1` next to `m_bExclusiveTag`, and `component::mgr` keeps a
mask of all builder bits.

A builder component that holds a container (`vector_small<box,1>`) must be DATA, not SHARE: a share's
key is a hash of its raw bytes, and a spilled vector's heap pointer breaks that hash.

## Builder systems

```c++
struct body_builder : xecs::system::instance
{
    constexpr static auto typedef_v = xecs::system::type::builder{ .m_pName = "Physics Body Builder" };

    using xecs::system::instance::instance;

    void operator()( const xecs::component::entity&   Entity      // the real, final entity - for Box3D user data
                   , xlioncore::transform&             T
                   , const physics_body_properties&    BodyProps   // builder component: read-only
                   , const physics_shape_properties&   ShapeProps  // builder component: read-only
                   , physics_body&                     Body        // handle component: gets m_BodyId
                   , const physics_dynamics*           pDyn ) noexcept;   // pointer = optional
};
```

* `system::type::id::BUILDER`. Runs only during entity creation, never in the frame loop.
* The query comes from the arguments like any `operator()` system: a reference is required, a pointer is
  optional (nullptr when absent). Pointers mean optional for every xECS system, not only builders.
* Builder components must be taken `const` (several builders may read the same one) - a `static_assert`
  in the generated build function.
* Side effects are the point: a builder calls the owner. What it writes on the entity is only handles.
* Order: registration order. Add an explicit order field only when a real dependency appears.

## Creation flow: the entity is placed once

No temporary archetype and no archetype moves. Builder components never live in a pool, so nothing about
them is ever registered.

1. **Stage** every component in `xecs::persist::details::staged_components`, placement-constructed first
   (see [scratch buffers](scratch_buffer_construct_before_copy.md)), and fill it (file data, prefab
   defaults, prefab overrides, a spawn callback).
2. **Build plan** (`game_mgr::getBuildPlan`), cached per input component set: which builder systems
   match, and the final archetype (*input − builder components*). Handle components like `physics_body`
   must already be part of the input. Plans are rebuilt when builder systems are registered or builders
   are switched on/off.
3. **Place** the entity once in the final archetype: non-builder components are moved in, non-builder
   shares interned once (`archetype::instance::CreateEntity(Infos, MoveData, OnCreated)`).
4. **Run builder systems** inside that call's `OnCreated` callback - the entity and its pool slot exist,
   `NOTIFY_CREATE` hasn't fired yet. Builder components are read from staging, everything else from the
   entity (so the builder gets the real entity handle, no reservation needed).
5. `NOTIFY_CREATE` fires, now always with final data and handles. Staging (with the builder components)
   is destroyed.

A signature without builder components skips staging entirely: today's creation path, zero overhead.

### Handoff rules

* **No cancellation after a handoff.** If builder B fails after builder A created a body, cancelling
  would leak the body. A failed handoff is logged as an error; the entity is still placed, without that
  handle.
* **Release is `NOTIFY_DESTROY`**, as physics already does: whatever was handed off is released when the
  entity dies.
* **The owner may be locked.** Box3D refuses body creation inside step callbacks. If spawns can happen
  there, the owner queues the handoff internally, the same way physics already queues destroys.

### Builder components nobody consumes

A builder component with no builder system matching it would be thrown away with its data going nowhere.
That is almost always an authoring mistake:

* **At creation**, log an error naming the component type and entity, then discard the component.
* **In the editor**, the entity's **Systems** button (which already lists the systems the entity would run
  on) turns red. It also lists the builder systems that will consume each builder component, and what
  each one hands off to (e.g. "Physics Builder → Box3D body").

## Editor vs game

Builders are switched per `game_mgr`:

* **Editor, edit mode: off.** The editor is the authoring world. Builder components are ordinary
  components there: shown, edited, prefab-overridden, saved.
* **Editor, Play: on.** Play rebuilds the world from the saved copy (the same `CreateWorld` +
  `RestoreFromV1` that Stop already uses), so every entity goes through the same creation flow as in the
  game. This replaces per-subsystem Play hooks such as physics's `OnSceneReady` statics loop.
* **Game: always on.**

While Playing, the Inspector shows handle components, whose properties read and write the owner live (as
`physics_body`'s `BodyIndex` already does). Editing friction during Play really changes the simulation, and
"Keep property tweaks" has real values to keep.

Adding a builder component to an already-placed entity while builders are on is an error: the handoff
moment has passed. Runtime changes go through the owner's API (e.g. `TeleportBody`, and `b3Body_SetType`
for static demotion).

## Scene load and prefabs

**Scene load** (`scene::details::EnsureLoaded`):

1. Read every entity into staging (`ReadEntity`) - a prefab instance starts from the prefab's current values.
2. Apply prefab instance overrides in staging. Member paths walk the staged `children` lists, which still
   hold encoded permanent ids. Overrides on share and builder components work too (they're private copies).
3. Create every entity (`CreateStagedEntity`) - builders run here and see the overridden values.
4. Resolve external references and remap every entity reference to the live handles.

Overrides must come before building: an instance's overridden transform (e.g. its scale) is what the
physics builder needs - applying them after creation built bodies from the prefab's values.

**Prefab roots are never built**: they are templates, and building one would create a real Box3D body for
the template. An instance is staged from the root's components plus its overrides or spawn callback, then
built and placed like anything else.

## xLION physics

| Component | Kind | Builder? |
|---|---|---|
| `physics_body_properties` | SHARE | Builder (damping, sleep, CCD) |
| `physics_collider_box / sphere / capsule` (`vector_small<shape,1>` each) | DATA | Builder. Per shape: local offset/rotation, size, material guid, filter, sensor. Several on one entity = compound body. |
| PhysicsMaterial | Resource, referenced by guid from collider shapes | Not a component |
| `physics_dynamics` (display name `PhysicsDynamics`) | DATA | Stays: works with the physics system every frame (velocity readback, force/torque, mass) |
| `physics_body` | DATA | Stays: handle component |
| `static_tag`, `transform` | TAG, DATA | Stay |

What goes away:

* `OnSceneReady` and its statics loop.
* The per-frame `NeedsRecreate` / `B3_IS_NULL` poll.
* `physics_body_properties` / `physics_shape_properties` in the `OnUpdate` query.
* Recreating a body from component data (use Box3D setters instead).

## Open questions

* **Game.dll hot reload during Play.** The world is recreated (and builders re-enabled) and entities come
  back from the raw bridge snapshot, already built. The physics system - and with it the Box3D world - is
  recreated with the world, so the restored `physics_body` handles likely point at bodies that no longer
  exist. Needs a real test and probably an owner-side answer (keep the Box3D world across the reload, or
  snapshot/rebuild bodies).
* **Handoff queueing** inside owners (Box3D world lock): needed only if spawns can happen from inside
  step callbacks. Check where physics events are dispatched.

## Implementation status

Done (phase 1):
* `m_bBuilder` on the component typedefs and `component::type::info`.
* `system::type::builder` / `id::BUILDER`, `system::type::info::m_BuildFunction`, `system::mgr::m_BuilderSystems`.
* `game_mgr::EnableBuilders` / `getBuildPlan`; unconsumed builder components logged when a plan is made.
* `staged_components::Create(GameMgr, Plan)` and `archetype::instance::CreateEntity(..., OnCreated)`.
* Scene load: read -> overrides in staging -> create/build -> references.
* A guard in `archetype::mgr::CreateArchetype`: while builders are on, an archetype holding builder
  components (outside prefab templates and share-entities) logs an error - the path that made it isn't staged.
* xLION: Play rebuilds the world from V1 with builders on; physics `body_builder` creates bodies
  (`OnSceneReady` and the per-frame recreate are gone; type changes use `b3Body_SetType`).

Still to do:
* Stage the runtime paths: prefab instancing (`prefab::mgr::CreatePrefabInstance`) and typed
  `CreateEntity` / archetype `CreateEntity(callback)`.
* Editor: the Systems button lists builder systems and turns red for unconsumed builder components.
* `smoke_test_builder.cpp`.
