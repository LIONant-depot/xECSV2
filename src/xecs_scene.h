namespace xecs::scene
{
    // Scene is a full resource: its guid IS a real resource guid (xresource::def_guid), not a
    // separate bridged identity. This is what a Scene reference looks like anywhere in the engine
    // (Level's m_Scenes, another Scene's m_ParentScenes, a picked resource in the editor), and it
    // gets the automatic drag-drop resource-picker widget for free through xproperty's built-in
    // member_ui<xresource::def_guid<T>> specialization - no bridging/conversion code needed anywhere.
    inline constexpr auto type_guid_v = xresource::type_guid(xresource::guid_generator::Instance64FromString("Scene"));
    using guid = xresource::def_guid<type_guid_v>;

    enum class type : std::uint8_t
    { LOCAL
    , GLOBAL
    , OVERLAPPED
    };

    constexpr std::int32_t range_size_v             = 512;
    constexpr std::int32_t sub_range_size_v         = 256;

    struct range
    {
        static constexpr auto sub_range_precision_v    = 4;
        static constexpr auto hash_size_v              = sub_range_size_v / sub_range_precision_v;

        std::size_t                              m_Address;
        int                                      m_nEntities;
        std::array<std::uint8_t, hash_size_v >   m_HashEmpty;
        std::array<std::uint8_t, range_size_v >  m_BlockEntityCount;
        std::array<std::int16_t, range_size_v >  m_HashNextSubRange;
    };

    // A scene's local entity identity, stable across save/load. Dense and scoped to the owning
    // scene only - NOT globally unique by itself (the globally meaningful address is
    // {owning scene guid, permanent_id}, matching external_entity_address below).
    using permanent_id = std::uint32_t;
    constexpr permanent_id invalid_permanent_id_v = 0;

    // What a negative entity-reference key (see xecs_scene_inline.h's Encode/DecodeRef) resolves
    // through: the parent scene that owns the target, and that target's permanent_id within it.
    struct external_entity_address
    {
        guid           m_ParentScene;
        permanent_id   m_ParentEntity;

        XPROPERTY_DEF
        ( "SceneExternalRef", external_entity_address
        , obj_member<"ParentScene",  &external_entity_address::m_ParentScene>
        , obj_member<"ParentEntity", &external_entity_address::m_ParentEntity>
        )
    };
    XPROPERTY_REG(external_entity_address)

    // Purely an editor-organizational grouping of entities within a scene (Unity Hierarchy-style
    // folder) - never touches the live ECS entity graph, never referenced by any component, never
    // seen at runtime. Scene-local like permanent_id, same reasoning (GUID-like ids to avoid merge
    // collisions when two branches each create a folder independently).
    using folder_id = std::uint32_t;
    constexpr folder_id invalid_folder_id_v = 0;

    // Containment-owned, not back-pointer-owned: a folder lists the entities inside it; an entity
    // carries no "which folder am I in" field of its own (matches this codebase's standing "no
    // redundant/duplicated data" convention - the folder is the one and only place that fact lives).
    // Nesting is via m_Parent (invalid = root-level), not a child-folder list - the flat
    // std::vector<folder> plus each one's own m_Parent is what a recursive tree-render walks.
    struct folder
    {
        folder_id                  m_Id     = invalid_folder_id_v;
        folder_id                  m_Parent = invalid_folder_id_v;
        std::string                m_Name;
        std::vector<permanent_id>  m_Entities;

        XPROPERTY_DEF
        ( "SceneFolder", folder
        , obj_member<"Id",       &folder::m_Id>
        , obj_member<"Parent",   &folder::m_Parent>
        , obj_member<"Name",     &folder::m_Name>
        , obj_member<"Entities", &folder::m_Entities>
        )
    };
    XPROPERTY_REG(folder)

    // A user-given display name for one entity (Level Tree inline rename). Editor-organizational like a
    // folder: not a component, never seen at runtime, lives in the scene so it saves/loads with it.
    // An entity with no entry just shows its id. Entries of entities that no longer exist are dropped on save.
    struct entity_name
    {
        permanent_id    m_Id   = invalid_permanent_id_v;
        std::string     m_Name;

        XPROPERTY_DEF
        ( "SceneEntityName", entity_name
        , obj_member<"Id",   &entity_name::m_Id>
        , obj_member<"Name", &entity_name::m_Name>
        )
    };
    XPROPERTY_REG(entity_name)

    enum class state : std::uint8_t
    { Unloaded
    , WaitingForParents
    , LoadingEntities
    , Active
    , Failed
    };

    struct instance
    {
        guid                    m_Guid;
        std::string             m_Name;
        type                    m_Type;
        std::vector<range>      m_lGlobalRanges;

        std::unordered_map<std::uint64_t, std::uint64_t> m_LoadRemappedList;

        // Dependency edges (child -> parent) and the interned external-reference table this
        // scene's entity files use to point at entities living in one of these parents.
        std::vector<guid>                          m_ParentScenes;
        std::vector<external_entity_address>       m_ExternalRefTable;

        // Editor-organizational grouping, live/mutable during editing - copied to/from
        // descriptor::m_Folders wholesale on save/load, same role m_ParentScenes already plays.
        std::vector<folder>                        m_Folders;

        // Entity display names (see entity_name) - live map, copied to/from descriptor::m_EntityNames on save/load.
        std::unordered_map<permanent_id, std::string> m_EntityNames;

        // Entities the descriptor lists that could not be loaded (their file is unreadable, or a component of theirs is not registered: the game module that defines it is not loaded).
        // They are not in the world, but they are still the scene's: a Save keeps them in m_ActiveEntities and keeps the components they need in ComponentDeps.txt, instead of
        // silently forgetting them (the entity file stays on disk, and the next load with the right game module brings them back).
        std::vector<permanent_id>                   m_UnloadedEntities;

        // Residency. A scene is unloadable only when both are zero.
        int                                         m_ExplicitRequests    = 0;
        int                                         m_DependentSceneCount = 0;

        // Live only while resident (never serialized - rebuilt fresh on every load).
        std::unordered_map<permanent_id, xecs::component::entity>   m_LocalToRuntime;
        std::unordered_map<std::uint64_t, permanent_id>              m_RuntimeToLocal;   // keyed by entity.m_Value
        std::vector<xecs::component::entity>                         m_ExternalToRuntime; // indexed by (-key - 1)

        // What a Save needs to actually touch on disk, explicitly recorded by whoever performs the
        // mutation (mgr::MarkEntityNew/MarkEntityDirty/MarkEntityDeleted - called from E29's UI today,
        // meant to be the same choke point any future undo/redo goes through) rather than inferred
        // afterward by scanning entity_db (a directory walk on every Save doesn't scale with entity
        // count) or by rewriting every live entity every Save (correct, but wastes IO proportional to
        // the WHOLE scene instead of to what actually changed - the bigger the scene grows, the more
        // that waste costs).
        //
        // Three independent signed counters per entity, not three vectors/sets and not one merged
        // value - each concern gets its own delta so undo/redo (whenever it lands) can just apply the
        // exact inverse of whatever action it's undoing (a create's undo does m_New -= 1, an edit's
        // undo does m_Dirty -= 1, a delete's undo does m_Deleted -= 1) without needing to know how to
        // surgically pull one id out of a vector, or worry about one undo accidentally cancelling a
        // DIFFERENT pending concern on the same entity. Counting (not just a bool) matters for m_Dirty
        // specifically: three edits then two of them undone must still read as dirty (count 1), where
        // a bool would have gone false after the first undo. m_New and m_Deleted are resolved together
        // at save time rather than auto-cancelling arithmetically against each other (unlike an
        // earlier, since-corrected sketch that merged them into one +1/new -1/deleted value): New>0
        // AND Deleted>0 for the same id means "created and deleted before ever being saved" - nothing
        // on disk to touch either way - while New<=0 AND Deleted>0 means an already-saved entity was
        // deleted and its file must actually go.
        struct pending_change
        {
            std::int32_t   m_New       = 0;
            std::int32_t   m_Dirty     = 0;
            std::int32_t   m_Deleted   = 0;
        };
        std::unordered_map<permanent_id, pending_change>             m_PendingChanges;

        state                                       m_State = state::Unloaded;
    };

    struct component
    {
        constexpr static auto typedef_v = xecs::component::type::share
        { .m_pName          = "Scene" 
        , .m_bBuildFilter   = true
        , .m_ReferenceMode  = xecs::component::type::reference_mode::NO_REFERENCES
        };

        __inline constexpr xecs::component::type::share::key ComputeShareKey( void ) noexcept
        {
            return {m_SceneGuid.m_Instance.m_Value};
        }
    
        guid m_SceneGuid;
    };

    // The module a component belongs to, in a scene's ComponentDeps.txt: the guid of the script module (ScriptModule resource) that defines it, 0 for the engine's and the editor's own
    // components (they belong to no module), and this when nobody could say (the file was written before modules were tracked, or the game module was not loaded when it was saved).
    inline constexpr std::uint64_t unknown_module_v = ~0ull;

    struct component_dependency;

    struct mgr
    {
        mgr( xecs::game_mgr::instance& GameMgr ) noexcept : m_GameMgr{ GameMgr } {}

        // Increments residency and loads the scene (and, recursively, any not-yet-resident
        // parents) if it isn't already resident. A second RequestLoad on an already-resident
        // scene is just a residency bump - it does not reload.
        inline
        xerr        RequestLoad ( guid SceneGuid ) noexcept;

        // Decrements residency; actually unloads (child-first, cascading into parents) only once
        // both m_ExplicitRequests and m_DependentSceneCount reach zero.
        inline
        xerr        ReleaseLoad ( guid SceneGuid ) noexcept;

        inline
        instance*   Find        ( guid SceneGuid ) noexcept;

        // Finds an already-registered scene instance, or registers a brand new (Unloaded, empty)
        // one. Used internally by RequestLoad and by authoring/tooling code (e.g. a level editor,
        // or this milestone's smoke test) that needs to build up an in-memory scene before saving.
        inline
        instance&   FindOrCreate( guid SceneGuid ) noexcept;

        // Authoring-side helpers: write the scene's current in-memory dependency/external-ref state
        // and one entity's current live component data to disk, in the on-disk format RequestLoad's
        // LoadEntity/LoadSceneDescriptor read back. Not part of the runtime load/unload path.
        inline
        xerr        SaveSceneDescriptor ( guid SceneGuid ) noexcept;
        inline
        xerr        SaveEntity          ( guid SceneGuid, permanent_id Id, xecs::component::entity Entity ) noexcept;

        // Writes ComponentDeps.txt - see details::ComponentDepsPath's own comment for what this file
        // is for. Called automatically from SaveScene; exposed separately too in case a future caller
        // needs to force-regenerate it without a full scene save.
        inline
        xerr        SaveSceneComponentDependencies ( guid SceneGuid ) noexcept;

        // The component types the scene's live entities use (guid, name, module), sorted by guid: what SaveSceneComponentDependencies writes, from the live archetypes - for the checks
        // that must not read a file that is older than the scene in memory. The module of each comes from m_pModuleOfComponent.
        inline
        std::vector<component_dependency> CollectSceneComponentDependencies ( guid SceneGuid ) const noexcept;

        // The actual "Save this scene" entry point: resolves instance::m_PendingChanges (see its own
        // comment) into actual disk writes/deletes via SaveEntity, each sanity-checked against whether
        // its file already exists, then calls SaveSceneDescriptor for the descriptor write. Cost is
        // proportional to what actually changed, not to the scene's total entity count - the whole
        // reason this exists instead of a loop over every live entity.
        inline
        xerr        SaveScene           ( guid SceneGuid ) noexcept;

        // Records that Id was just created (MarkEntityNew), that an already-saved Id's live data just
        // changed (MarkEntityDirty), or that Id was removed (MarkEntityDeleted) - see
        // instance::m_PendingChanges' own comment for the counter semantics. Called once per action
        // (New Entity/Instantiate Prefab for New; a property edit, Add/Remove Component, or converting
        // an entity into a prefab instance in place for Dirty; the delete UI action for Deleted) -
        // meant to be the same choke point any future undo/redo goes through, applying the exact
        // inverse delta.
        inline
        void        MarkEntityNew       ( guid SceneGuid, permanent_id Id ) noexcept;
        inline
        void        MarkEntityDirty     ( guid SceneGuid, permanent_id Id ) noexcept;

        inline
        void        MarkEntityDeleted   ( guid SceneGuid, permanent_id Id ) noexcept;

        // Fired once per scene when it's about to actually RUN, not when its data finishes loading -
        // those are different moments in the editor (opening a level to edit it loads scenes without
        // ever firing this; pressing Play fires it for every already-open scene) and the same moment
        // only in a real game (load == about to run there). A component's real data is guaranteed
        // final by the time this fires (unlike NOTIFY_CREATE, which fires with default-constructed
        // values during load - see xecs_archetype_inline.h's own comment on _CreateEntity). Handlers
        // iterate Scene.m_LocalToRuntime directly - it's already exactly the set of entities this
        // scene owns, no separate "just added" bookkeeping needed.
        xecs::event::instance<instance&>         m_OnSceneReady;

        xecs::game_mgr::instance&                m_GameMgr;
        std::vector<std::unique_ptr<instance>>   m_SceneInstances;

        // The project's root path (matching e.g. e10::library_mgr::m_ProjectPath) - Scene descriptor
        // and entity_db folders are computed from this using the real GUID-sharded
        // Descriptors/Scene/<b0>/<b1>/<guid>.desc/ convention every other resource type uses.
        std::wstring                              m_ProjectPath;

        // Says which module defines a component (see unknown_module_v). Set by the host that knows (the editor, from the game DLL's registrations); xECSV2 itself knows nothing of modules.
        // Null: every component is unknown_module_v.
        std::uint64_t                           (*m_pModuleOfComponent)( void* pUser, xecs::component::type::guid ) noexcept = nullptr;
        void*                                   m_pModuleOfComponentUser = nullptr;       // given back to m_pModuleOfComponent: what says the module is the world's own (a Level has its own game module)
    };

    // One entry of a scene's ComponentDeps.txt manifest (see SaveSceneComponentDependencies/
    // LoadSceneComponentDependencies). m_Name is captured at SAVE time so a caller can still show a
    // human-readable "component X is missing" message even after the type is genuinely gone from
    // every currently-loaded DLL - there would be nothing else to get a display name from at that
    // point, since the registry lookup that would normally supply it is exactly what's failing.
    struct component_dependency
    {
        xecs::component::type::guid m_Guid {};
        std::string                 m_Name {};
        std::uint64_t               m_Module = unknown_module_v;        // the script module that defines it (0: none, it is the engine's or the editor's)
    };

    // Standalone read of a scene's dependency manifest - no game_mgr::instance, no entity/archetype
    // construction, safe to call from anywhere that knows a project path and a scene guid (a command-
    // bus Query, an idle-work background thread, a pre-flight check before any world even exists).
    // See SaveSceneComponentDependencies's own comment for what writes this file.
    inline
    std::vector<component_dependency> LoadSceneComponentDependencies( std::wstring_view ProjectPath, guid SceneGuid ) noexcept;
}