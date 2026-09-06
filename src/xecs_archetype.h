namespace xecs::archetype
{
    using guid = xresource::guid<struct archetype_tag>;

    inline
    guid ComputeGuidFromInfos( std::span<const xecs::component::type::info* const> Infos ) noexcept
    {
        xecs::tools::bits Bits{};
        for( const auto& e : Infos ) Bits.setBit( e->m_BitID );
        return guid{ Bits.GenerateUniqueID() };
    }

    //
    // ARCHETYPE INSTANCE
    //
    struct instance final
    {
        struct events
        {
            xecs::event::instance<xecs::component::entity&>      m_OnEntityCreated;
            xecs::event::instance<xecs::component::entity&>      m_OnEntityDestroyed;
            xecs::event::instance<xecs::component::entity&>      m_OnEntityMovedIn;
            xecs::event::instance<xecs::component::entity&>      m_OnEntityMovedOut;
            xecs::event::instance<instance&, pool::family&>      m_OnPoolFamilyCreated;
            xecs::event::instance<instance&, pool::family&>      m_OnPoolFamilyDestroy;
            std::array<xecs::event::instance<xecs::component::entity&>, xecs::settings::max_components_per_entity_v> m_OnComponentUpdated;
        };

        inline                  instance                ( xecs::archetype::mgr& Mgr 
                                                        ) noexcept;
        template< typename T >
        const T&                getShareComponent       ( const xecs::pool::family& Family 
                                                        ) const noexcept;
        bool                    hasShareComponents      ( void 
                                                        ) const noexcept { return !!m_nShareComponents; }
        constexpr __inline
        const xecs::tools::bits& getExclusiveTagBits    ( void 
                                                        ) const noexcept;
        constexpr __inline
        const xecs::tools::bits& getComponentBits       ( void 
                                                        ) const noexcept;
        constexpr __inline
        std::span<const xecs::component::type::info* const>   
                                getDataComponentInfos   ( void
                                                        ) const noexcept { return { m_InfoArray.data(), static_cast<std::size_t>(m_nDataComponents) }; }
        constexpr __inline
        int                     getShareComponentCount  ( void
                                                        ) const noexcept { return m_nShareComponents; }
        constexpr __inline
        int                     getDataComponentCount   ( void
                                                        ) const noexcept { return m_nDataComponents; }
        __inline
        int                     getTagComponentCount    ( void
                                                        ) const noexcept { return xecs::tools::bits{}.setupAnd(m_ComponentBits, xecs::component::mgr::s_Registry.m_TagsBits).CountComponents(); }
        constexpr __inline
        guid                     getGuid                ( void 
                                                        ) const noexcept;
        __inline
        pool::family*            getFamilyHead          ( void 
                                                        ) noexcept;
        template
        < typename...T_SHARE_COMPONENTS
        > requires
        ( ((xecs::component::type::is_valid_v<T_SHARE_COMPONENTS>) && ...)
            && xecs::tools::all_components_are_share_types_v<T_SHARE_COMPONENTS...>
        ) __inline
        xecs::pool::family&     getOrCreatePoolFamily   ( T_SHARE_COMPONENTS&&... Components
                                                        ) noexcept;
        inline
        xecs::pool::family&     getOrCreatePoolFamily   ( std::span< const xecs::component::type::info* const>  TypeInfos
                                                        , std::span< std::byte* >                               MoveData
                                                        ) noexcept;
        inline
        xecs::pool::family&     getOrCreatePoolFamily   ( xecs::pool::family&                                   OtherArchetypeFamily
                                                        ) noexcept;
        __inline
        xecs::component::entity CreateEntity            ( xecs::pool::family&                                   PoolFamily
                                                        , std::span< const xecs::component::type::info* const>  Infos
                                                        , std::span< std::byte* >                               MoveData
                                                        ) noexcept;
        __inline
        xecs::component::entity CreateEntity            ( std::span< const xecs::component::type::info* const>  Infos
                                                        , std::span< std::byte* >                               MoveData
                                                        ) noexcept;
        template
        < typename T_CALLBACK = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_CALLBACK>
            && xecs::tools::assert_function_return_v<T_CALLBACK, void>
        ) __inline
        xecs::component::entity CreateEntity            ( T_CALLBACK&& Function = xecs::tools::empty_lambda{}
                                                        ) noexcept;
        template
        < typename T_CALLBACK = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_CALLBACK>
            && xecs::tools::assert_function_return_v<T_CALLBACK, void>
        ) __inline
        xecs::component::entity CreateEntity            ( xecs::pool::family&   PoolFamily
                                                        , T_CALLBACK&&          Function = xecs::tools::empty_lambda{}
                                                        ) noexcept;
        template
        < typename T_CALLBACK = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_setter_function_v<T_CALLBACK>
            && xecs::tools::assert_function_return_v<T_CALLBACK, void>
        ) __inline
        void                    CreateEntities          ( int           Count
                                                        , T_CALLBACK&&  Function = xecs::tools::empty_lambda{}
                                                        ) noexcept;
        template
        < typename T_CALLBACK = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_CALLBACK>
            && xecs::tools::assert_function_return_v<T_CALLBACK, void>
        ) __inline
        void                    CreateEntities          ( int                   Count
                                                        , xecs::pool::family    PoolFamily
                                                        , T_CALLBACK&&          Function = xecs::tools::empty_lambda{}
                                                        ) noexcept;
        inline
        void                    DestroyEntity           ( xecs::component::entity& Entity
                                                        ) noexcept;

        template
        < typename T_CALLBACK = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_setter_function_v<T_CALLBACK>
            && xecs::tools::assert_function_return_v<T_CALLBACK, void>
        ) __inline
        xecs::component::entity CreateEntities          ( int                       Count
                                                        , xecs::component::entity   FromEntity
                                                        , T_CALLBACK&&              Function = xecs::tools::empty_lambda{}
                                                        ) noexcept;

        template
        < typename T_CALLBACK = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_CALLBACK>
            && xecs::tools::assert_function_return_v<T_CALLBACK, void>
        ) __inline
        xecs::component::entity CreateEntities          ( xecs::pool::family&       PoolFamily
                                                        , int                       Count
                                                        , xecs::component::entity   FromEntity
                                                        , T_CALLBACK&&              Function = xecs::tools::empty_lambda{}
                                                        ) noexcept;
        
                                                            
//-------------------------------------------------------------------------------------------------------------
protected:
//-------------------------------------------------------------------------------------------------------------
                                instance                ( const instance& 
                                                        ) = delete;
        inline
        void                    Initialize              ( archetype::guid       Guid
                                                        , const tools::bits&    AllComponentsBits
                                                        ) noexcept;
        inline
        xecs::pool::family&     getOrCreatePoolFamilyFromSameArchetype
                                                        ( xecs::pool::family&                                   FromFamily
                                                        , xecs::tools::bits                                     UpdatedComponentBits
                                                        , std::span< std::byte* >                               MoveData
                                                        , std::span< const xecs::component::type::share::key >  Keys
                                                        ) noexcept;
        inline
        xecs::pool::family&     CreateNewPoolFamily     ( xecs::pool::family::guid                          Guid
                                                        , std::span<xecs::component::entity>                ShareEntityList
                                                        , std::span<xecs::component::type::share::key>      ShareKeyList
                                                        ) noexcept;
        inline
        xecs::pool::family&     getOrCreatePoolFamilyFromDifferentArchetype
                                                        ( xecs::component::entity        Entity
                                                        ) noexcept;
        template
        < typename T_FUNCTION = xecs::tools::empty_lambda
        > requires
        ( xecs::function::is_callable_v<T_FUNCTION>
            && xecs::tools::assert_function_return_v<T_FUNCTION, void>
            && xecs::tools::assert_function_args_have_no_share_or_tag_components_v<T_FUNCTION>
            && xecs::tools::assert_function_args_have_only_non_const_references_v<T_FUNCTION>
        ) [[nodiscard]] xecs::component::entity
                                _MoveInEntity           ( xecs::component::entity&  Entity
                                                        , xecs::pool::family&       ToPoolFamily
                                                        , T_FUNCTION&&              Function = xecs::tools::empty_lambda{}
                                                        ) noexcept;
        template
        < typename T_FUNCTION = xecs::tools::empty_lambda
        > requires
        ( xecs::function::is_callable_v<T_FUNCTION>
            && xecs::tools::assert_function_return_v<T_FUNCTION, void>
            && xecs::tools::assert_function_args_have_no_share_or_tag_components_v<T_FUNCTION>
            && xecs::tools::assert_function_args_have_only_non_const_references_v<T_FUNCTION>
        ) [[nodiscard]] xecs::component::entity
                                _MoveInEntity           ( xecs::component::entity&  Entity
                                                        , T_FUNCTION&&              Function = xecs::tools::empty_lambda{}
                                                        ) noexcept;
        __inline
        void                    _UpdateStructuralChanges( void
                                                        ) noexcept;
        inline
        void                    _AddFamilyToPendingList ( std::unique_ptr<pool::family>&& PoolFamily
                                                        ) noexcept;
        template
        < typename T_CALLBACK = xecs::tools::empty_lambda
        > requires
        ( xecs::function::is_callable_v<T_CALLBACK>
            && false == xecs::tools::function_has_share_component_args_v<T_CALLBACK>
        ) __inline
        xecs::component::entity _CreateEntities         ( int           Count
                                                        , T_CALLBACK&&  Function = xecs::tools::empty_lambda{}
                                                        ) noexcept;
        template
        < typename T_CALLBACK = xecs::tools::empty_lambda
        > requires
        ( xecs::function::is_callable_v<T_CALLBACK>
            && false == xecs::tools::function_has_share_component_args_v<T_CALLBACK>
        )
        xecs::component::entity _CreateEntities         ( int                   Count
                                                        , xecs::pool::family&   PoolFamily
                                                        , T_CALLBACK&&          Function = xecs::tools::empty_lambda{}
                                                        ) noexcept;
        template
        < typename T_CALLBACK = xecs::tools::empty_lambda
        > requires
        ( xecs::function::is_callable_v<T_CALLBACK>
            && true == xecs::tools::function_has_share_component_args_v<T_CALLBACK>
        )
        xecs::component::entity _CreateEntities         ( int           Count
                                                        , T_CALLBACK&&  Function = xecs::tools::empty_lambda{}
                                                        ) noexcept;

        template
        < typename T_CALLBACK = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_function_return_v<T_CALLBACK, void>
        ) 
        void                    _CreateEntity           ( xecs::pool::family&   PoolFamily
                                                        , int                   Count
                                                        , T_CALLBACK&&          Function = xecs::tools::empty_lambda{}
                                                        ) noexcept;

        using share_archetypes_array = std::array<xecs::archetype::instance*,    xecs::settings::max_components_per_entity_v >;
        using scene_array            = std::array<std::unique_ptr<pool::family>, xecs::settings::max_scenes_v >;

        xecs::archetype::mgr&               m_Mgr;
        guid                                m_Guid                      {};
        xecs::tools::bits                   m_ComponentBits             {};
        xecs::tools::bits                   m_ExclusiveTagsBits         {};
        std::uint8_t                        m_nDataComponents           {};
        std::uint8_t                        m_nShareComponents          {};
        pool::family                        m_DefaultPoolFamily         {};
        xecs::component::entity::info_array m_InfoArray                 {};
        instance*                           m_pPendingStructuralChanges {};
        share_archetypes_array              m_ShareArchetypesArray      {};
        std::unique_ptr<pool::family>       m_PoolFamilyPending         {};
        scene_array                         m_SceneArray                {};

    public:
        events                              m_Events                    {};

    friend struct mgr;
    friend struct pool::family;
    friend struct system::mgr;
    friend struct game_mgr::instance;
    template<typename T, auto T_MODE_V > friend struct query::iterator;
    };
}
