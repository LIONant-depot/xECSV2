namespace xecs::component
{
    namespace ranges
    {
        constexpr auto sub_ranges_count_per_range_v         = 512;
        constexpr auto runtime_range_count_v                = 1024;
        constexpr auto editor_scene_range_count_overflow_v  = 10024;     // You can grow by 10 Million entities
        constexpr auto sub_range_entity_count_v             = 256;
        constexpr auto sub_range_byte_count_v               = xecs::settings::virtual_page_size_v; // == sub_range_entity_count_v * sizeof(entity::global_info)
    }

    namespace details
    {
        struct global_info_mgr
        {
            entity::global_info*    m_pGlobalInfo          =  nullptr;
            int                     m_EmptyHead            = -1;
            int                     m_LastRuntimeSubrange  = -1;
            int                     m_MaxSceneRange;

            inline
                                           ~global_info_mgr     ( void ) noexcept;
            inline
            void                            Initialize          ( int LastKnownSceneRanged ) noexcept;
            inline
            entity::global_info&            getEntityDetails    ( xecs::component::entity Entity ) noexcept;
            inline
            const entity::global_info&      getEntityDetails    ( xecs::component::entity Entity ) const noexcept;
            inline
            entity                          AllocInfo           ( pool::index PoolIndex, xecs::pool::instance& Pool ) noexcept;
            inline
            void                            FreeInfo            ( std::uint32_t GlobalIndex, xecs::component::entity& SwappedEntity ) noexcept;
            inline
            void                            FreeInfo            ( std::uint32_t GlobalIndex ) noexcept;
            inline 
            void                            AppendNewSubrange   ( void ) noexcept;
        };
    }

    struct range
    {
        std::size_t         m_StardingAddress;
        xecs::scene::guid   m_SceneGuid;
    };

    //
    // The single registry of every component TYPE ever registered - what used to be scattered
    // across nine separate `inline static` data members directly on `mgr` (m_ComponentInfoMap,
    // s_ShareBits, s_DataBits, s_TagsBits, s_ExclusiveTagsBits, s_UniqueID, s_BitsToInfo, s_nTypes,
    // s_isLocked). Bundled into one struct, given exactly one out-of-line definition
    // (mgr::s_Registry, defined once in details/xecs_component_mgr_inline.h) so there is one,
    // discoverable, physical owner - "inline static" at namespace OR class scope still only
    // guarantees "one instance per linked binary/module," not "one instance," which is exactly the
    // wrong mental model for what eventually needs to be the one shared authority once xECSV2 is
    // split across a game-code DLL and a host (see the xECSV2 DLL-boundary audit for the empirical
    // proof of why this matters). This is a pure storage-consolidation pass - the fields' MEANING
    // and the code that mutates them (RegisterComponent/LockComponentTypes/etc., still declared on
    // mgr below, same public signatures) are unchanged; only where they physically live changed.
    //
    namespace type
    {
        struct registry
        {
            using bits_to_info_array = std::array<const xecs::component::type::info*, xecs::settings::max_component_types_v>;
            using component_info_map = std::unordered_map<xecs::component::type::guid, const xecs::component::type::info*>;

            component_info_map    m_ComponentInfoMap  {};
            xecs::tools::bits     m_ShareBits         {};
            xecs::tools::bits     m_DataBits          {};
            xecs::tools::bits     m_TagsBits          {};
            xecs::tools::bits     m_ExclusiveTagsBits {};
            int                   m_UniqueID          = 0;
            bits_to_info_array    m_BitsToInfo        {};
            int                   m_nTypes            = 0;
            bool                  m_isLocked          = false;
        };
    }

    //
    // MGR
    //
    struct mgr final
    {
        inline
                                            mgr                     ( void 
                                                                    ) noexcept;
        template
        < typename T_COMPONENT
        > requires
        ( xecs::component::type::is_valid_v<T_COMPONENT>
        )
        void                                RegisterComponent       ( void
                                                                    ) noexcept;
        inline
        const entity::global_info&          getEntityDetails        ( xecs::component::entity Entity 
                                                                    ) const noexcept;
        inline
        void                                DeleteGlobalEntity      ( std::uint32_t              GlobalIndex
                                                                    , xecs::component::entity&   SwappedEntity 
                                                                    ) noexcept;
        inline
        void                                DeleteGlobalEntity      ( std::uint32_t GlobalIndex
                                                                    ) noexcept;
        inline
        void                                MovedGlobalEntity       ( xecs::pool::index         PoolIndex
                                                                    , xecs::component::entity&  SwappedEntity
                                                                    ) noexcept;
        inline 
        entity                              AllocNewEntity          ( pool::index                   PoolIndex
                                                                    , xecs::archetype::instance&    Archetype
                                                                    , xecs::pool::instance&         Pool 
                                                                    ) noexcept;
        inline
        void                                LockComponentTypes      ( void 
                                                                    ) noexcept;
        inline static
        const xecs::component::type::info*  findComponentTypeInfo   ( xecs::component::type::guid Guid
                                                                    ) noexcept;
        inline static
        void                                resetRegistrations      ( void 
                                                                    ) noexcept;
        details::global_info_mgr                            m_GlobalEntityInfos {};

        // The single component-type registry this binary owns - see its own comment (above,
        // namespace type { struct registry {...} }) for why this is an out-of-line static (one
        // definition, in details/xecs_component_mgr_inline.h) rather than another `inline static`.
        static type::registry                               s_Registry;
    };
}