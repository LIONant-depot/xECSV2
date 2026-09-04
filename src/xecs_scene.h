namespace xecs::scene
{
    using guid = xresource::guid<struct scene_tag>;

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
    };

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

        // Residency. A scene is unloadable only when both are zero.
        int                                         m_ExplicitRequests    = 0;
        int                                         m_DependentSceneCount = 0;

        // Live only while resident (never serialized - rebuilt fresh on every load).
        std::unordered_map<permanent_id, xecs::component::entity>   m_LocalToRuntime;
        std::unordered_map<std::uint64_t, permanent_id>              m_RuntimeToLocal;   // keyed by entity.m_Value
        std::vector<xecs::component::entity>                         m_ExternalToRuntime; // indexed by (-key - 1)

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
            return {m_SceneGuid.m_Value};
        }
    
        guid m_SceneGuid;
    };

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

        xecs::game_mgr::instance&                m_GameMgr;
        std::vector<std::unique_ptr<instance>>   m_SceneInstances;

        // Where scene descriptors/entity_db folders live on disk. Not resource-pipeline integrated
        // yet (Milestone 1 uses a plain folder, not the GUID-sharded Descriptors/Scene/<b0>/<b1>/...
        // layout the rest of the resource pipeline uses) - a standalone test sets this directly.
        std::wstring                              m_RootPath;
    };
}