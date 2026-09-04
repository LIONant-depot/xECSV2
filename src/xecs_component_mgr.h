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
        using bits_to_info_array = std::array<const xecs::component::type::info*, xecs::settings::max_component_types_v>;
        using component_info_map = std::unordered_map<xecs::component::type::guid, const xecs::component::type::info*>;

        details::global_info_mgr                            m_GlobalEntityInfos {};
        inline static component_info_map                    m_ComponentInfoMap  {};
        inline static xecs::tools::bits                     s_ShareBits         {};
        inline static xecs::tools::bits                     s_DataBits          {};
        inline static xecs::tools::bits                     s_TagsBits          {};
        inline static xecs::tools::bits                     s_ExclusiveTagsBits {};
        inline static int                                   s_UniqueID          = 0;
        inline static bits_to_info_array                    s_BitsToInfo        {};
        inline static int                                   s_nTypes            = 0;
        inline static bool                                  s_isLocked          = false;
    };
}