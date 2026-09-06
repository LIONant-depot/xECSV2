namespace xecs::game_mgr
{
    //---------------------------------------------------------------------------

    enum class state : std::uint8_t
    { OK      = 0
    , FAILURE = 1
    };

    //---------------------------------------------------------------------------

    struct instance final
    {
                                            instance                ( const instance& 
                                                                    ) = delete;
        inline                              instance                ( void
                                                                    ) noexcept;
        template
        < typename...T_SYSTEMS
        > requires
        ( std::derived_from< T_SYSTEMS, xecs::system::instance> && ...
        )
        void                                RegisterSystems         ( void
                                                                    ) noexcept;
        template
        < typename...T_COMPONENTS
        > 
        void                                RegisterComponents      ( void
                                                                    ) noexcept;
        template
        < typename...T_GLOBAL_EVENTS
        > requires
        ( std::derived_from< T_GLOBAL_EVENTS, xecs::event::overrides> 
          && ...
        )
        void                                RegisterGlobalEvents    ( void 
                                                                    ) noexcept;
        inline
        void                                DeleteEntity            ( xecs::component::entity& Entity 
                                                                    ) noexcept;
        template
        < typename T_FUNCTION = xecs::tools::empty_lambda
        > requires
        ( xecs::function::is_callable_v<T_FUNCTION>
        ) [[nodiscard]] xecs::component::entity
                                            AddOrRemoveComponents   ( xecs::component::entity                             Entity
                                                                    , std::span<const xecs::component::type::info* const> Add
                                                                    , std::span<const xecs::component::type::info* const> Sub
                                                                    , T_FUNCTION&&                                        Function = xecs::tools::empty_lambda{}
                                                                    ) noexcept;
        template
        <   typename T_TUPLE_ADD
        ,   typename T_TUPLE_SUBTRACT   = std::tuple<>
        ,   typename T_FUNCTION         = xecs::tools::empty_lambda
        > requires
        ( xecs::function::is_callable_v<T_FUNCTION>
        && xecs::types::is_specialized_v<std::tuple, T_TUPLE_ADD>
        && xecs::types::is_specialized_v<std::tuple, T_TUPLE_SUBTRACT>
        ) [[nodiscard]] xecs::component::entity
                                            AddOrRemoveComponents   ( xecs::component::entity   Entity
                                                                    , T_FUNCTION&&              Function = xecs::tools::empty_lambda{}
                                                                    ) noexcept;
        template
        < typename T_FUNCTION = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_FUNCTION>
        ) __inline
        [[nodiscard]] xecs::component::entity
                                            findEntity              ( xecs::component::entity Entity
                                                                    , T_FUNCTION&&            Function = xecs::tools::empty_lambda{}
                                                                    ) noexcept;
        __inline
        [[nodiscard]] xecs::archetype::instance&
                                            getArchetype            ( xecs::component::entity Entity 
                                                                    ) const noexcept;
        template
        < typename T_FUNCTION = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_FUNCTION>
        ) __inline
        [[nodiscard]] xecs::component::entity
                                            getEntity               ( xecs::component::entity Entity
                                                                    , T_FUNCTION&&            Function = xecs::tools::empty_lambda{}
                                                                    ) noexcept;
        inline
        [[nodiscard]] std::vector<archetype::instance*>
                                            Search                  ( const xecs::query::instance& Query
                                                                    ) const noexcept;
        inline
        [[nodiscard]] archetype::instance*  findArchetype           ( xecs::archetype::guid Guid 
                                                                    ) const noexcept;
        inline
        archetype::instance&                getOrCreateArchetype    ( std::span<const component::type::info* const> Types 
                                                                    ) noexcept;
        template
        < typename      T_GLOBAL_EVENT
        , typename...   T_ARGS
        > requires
        ( std::derived_from< T_GLOBAL_EVENT, xecs::event::overrides>
        )
        void                                SendGlobalEvent         ( T_ARGS&&... Args 
                                                                    ) const noexcept;

        template
        < typename      T_GLOBAL_EVENT
        > requires
        ( std::derived_from< T_GLOBAL_EVENT, xecs::event::overrides>
        )
        T_GLOBAL_EVENT&                     getGlobalEvent          ( void 
                                                                    ) const noexcept;
        template
        < typename... T_TUPLES_OF_COMPONENTS_OR_COMPONENTS
        > requires
        ( 
            (   (  xecs::tools::valid_tuple_components_v<T_TUPLES_OF_COMPONENTS_OR_COMPONENTS>
                || xecs::component::type::is_valid_v<T_TUPLES_OF_COMPONENTS_OR_COMPONENTS> 
                ) &&... )
        )
        archetype::instance&                getOrCreateArchetype    ( void 
                                                                    ) noexcept;
        template
        < typename...T_COMPONENTS
        , typename T_FUNCTION   = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_FUNCTION>
        ) xecs::prefab::guid                CreatePrefab            ( T_FUNCTION&&              Function        = xecs::tools::empty_lambda{}
                                                                    ) noexcept;
        template
        < typename T_FUNCTION   = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_FUNCTION>
        ) xecs::prefab::guid                CreatePrefab            ( xecs::tools::bits         ComponentBits
                                                                    , T_FUNCTION&&              Function        = xecs::tools::empty_lambda{}
                                                                    ) noexcept;
        template
        < typename T_ADD_TUPLE = std::tuple<>
        , typename T_SUB_TUPLE = std::tuple<>
        , typename T_FUNCTION  = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_FUNCTION>
        ) void                              CreatePrefabInstance    ( int                       Count
                                                                    , xecs::prefab::guid        PrefabGuid
                                                                    , T_FUNCTION&&              Function        = xecs::tools::empty_lambda{}
                                                                    , bool                      bRemoveRoot     = true
                                                                    ) noexcept;
        template
        < typename T_ADD_TUPLE = std::tuple<>
        , typename T_SUB_TUPLE = std::tuple<>
        , typename T_FUNCTION  = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_FUNCTION>
        ) xforceinline [[nodiscard]] xecs::prefab::guid
                                            CreatePrefabVariant     ( xecs::prefab::guid        PrefabGuid
                                                                    , T_FUNCTION&&              Function = xecs::tools::empty_lambda{}
                                                                    ) noexcept;
        template
        <   typename T_FUNCTION
        ,   auto     T_SHARE_AS_DATA_V = false
        > requires
        ( xecs::tools::assert_is_callable_v<T_FUNCTION>
            && (   xecs::tools::function_return_v<T_FUNCTION, bool >
                || xecs::tools::function_return_v<T_FUNCTION, void > )
        ) __inline
        bool                                Foreach                 ( std::span<xecs::archetype::instance* const>   List
                                                                    , T_FUNCTION&&                                  Function 
                                                                    ) noexcept;
        template
        <   typename T_FUNCTION
        ,   auto     T_SHARE_AS_DATA_V = false
        > requires
        ( xecs::tools::assert_is_callable_v<T_FUNCTION>
          && (   xecs::tools::function_return_v<T_FUNCTION, bool >
              || xecs::tools::function_return_v<T_FUNCTION, void > )
        ) __inline
        bool                                Foreach                 ( const std::vector<const xecs::archetype::instance*>&   List
                                                                    , T_FUNCTION&&                                           Function 
                                                                    ) noexcept;
        void                                Run                     ( void 
                                                                    ) noexcept;
        void                                Stop                    ( void 
                                                                    ) noexcept;
        template
        < typename T_SYSTEM
        > __inline
        T_SYSTEM*                           findSystem              ( void
                                                                    ) noexcept;
        template
        < typename T_SYSTEM
        > __inline
        T_SYSTEM&                           getSystem               ( void
                                                                    ) noexcept;
        // The live, actively-tested persistence path for a raw ECS game state (used by
        // dependencies/xECSV2/smoke_test.cpp's own save+load round trip) - distinct from the
        // resource-pipeline-integrated Scene/Level/Prefab persistence (xecs_scene_inline.h/
        // xecs_prefab_mgr_inline.h) that E29 and friends actually use for editor content.
        xerr                                SerializeGameState      ( const char* pFileName
                                                                    , bool        isRead
                                                                    , bool        isBinary = false
                                                                    ) noexcept;

        xecs::system::mgr                                   m_SystemMgr         {};
        xecs::event::mgr                                    m_EventMgr          {};
        xecs::component::mgr                                m_ComponentMgr      {};
        xecs::archetype::mgr                                m_ArchetypeMgr      {*this};
        xecs::prefab::mgr                                   m_PrefabMgr         {*this};
        xecs::scene::mgr                                    m_SceneMgr          {*this};
        xecs::level::mgr                                    m_LevelMgr          {*this};
        bool                                                m_isRunning         = false;
        xecs::log::channel                                  m_LogChannel        { "xecs" };
    };
}