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
        // No plugin_token parameter here, unlike RegisterComponents below - deliberately. Every
        // system lives in THIS instance's own m_SystemMgr (an ordinary instance member, never a
        // static/shared table - see the xECSV2 type-registration architecture plan's Phase 1 note),
        // so there is no cross-binary "who owns this registration" question to answer: a plugin
        // reload destroys and recreates the whole game_mgr::instance (Phase 8A), which already
        // detaches every system that instance ever registered, for free, with no extra bookkeeping.
        template
        < typename...T_SYSTEMS
        > requires
        ( std::derived_from< T_SYSTEMS, xecs::system::instance> && ...
        )
        void                                RegisterSystems         ( void
                                                                    ) noexcept;
        // Optional Owner defaults to xecs::plugin::host_v, so every existing call site keeps
        // compiling and behaving exactly as before - only a future plugin loader ever passes a
        // non-host token. Components DO need this (unlike systems, just above): the type-info
        // registry they mutate (xecs::component::mgr::s_Registry) is process-wide static state,
        // shared by every game_mgr::instance in the binary - see xecs_component_mgr.h's own
        // ownership-tracking comment.
        template
        < typename...T_COMPONENTS
        >
        void                                RegisterComponents      ( xecs::plugin::token Owner = xecs::plugin::host_v
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
        [[nodiscard]] inline
        std::vector<archetype::instance*>
                                            Search                  ( const xecs::query::instance& Query
                                                                    ) const noexcept;
        [[nodiscard]] inline
        archetype::instance*  findArchetype           ( xecs::archetype::guid Guid 
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
        // XECS_API: Run/Stop are ordinary (non-template) member functions defined out-of-line in
        // xecs_game_mgr.cpp - unlike everything else in this class, which is a template and gets
        // compiled fresh into whichever module calls it, these two need dllexport/dllimport once
        // xECSV2 is a shared library, or a consuming module simply can't find their definitions.
        XECS_API
        void                                Run                     ( void
                                                                    ) noexcept;
        XECS_API
        void                                Stop                    ( void
                                                                    ) noexcept;

        // Type-erased share-component copy-on-write: given new bytes for Info on Entity, find/create
        // the matching pool family and MoveIn. Public wrapper around the protected
        // getOrCreatePoolFamilyFromSameArchetype path used by getEntity's mutable-share edits.
        // No-op (returns true) when the entity is already on a family with that exact key.
        inline bool                         ReinternShareComponent  ( xecs::component::entity Entity
                                                                    , const xecs::component::type::info& Info
                                                                    , std::byte* pNewData
                                                                    ) noexcept;
        // Forwards to xecs::component::mgr::UnregisterPlugin - see that method's own comment for
        // exactly what it does (and does not yet do). The API a future plugin loader actually calls
        // as part of Phase 8A's teardown sequence, kept on instance for symmetry with
        // RegisterComponents above rather than making callers reach into m_ComponentMgr directly.
        inline
        void                                UnregisterPlugin        ( xecs::plugin::token Token
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
        // One type-erased slot for whatever the embedding application (its own "my_game"-equivalent -
        // xLION's xlevel::session, say) wants lower-level code (a system, a cross-module API function)
        // to be able to reach back up to - delta time, a fixed-timestep accumulator, an active-system
        // pointer, whatever doesn't belong in this generic, reusable ECS core itself. Same idiom as
        // GLFW's window userdata / Lua's lua_State userdata: the caller owns type safety, this just
        // holds and hands back a pointer. One slot, not a registry - a second unrelated consumer
        // needing this too is the signal to revisit, not something to design against speculatively.
        void                                 setUserData             ( void* pUserData
                                                                    ) noexcept { m_pUserData = pUserData; }
        template< typename T >
        [[nodiscard]] T*                     getUserData             ( void
                                                                    ) const noexcept { return static_cast<T*>(m_pUserData); }

        // Builder components/systems - see doc/xecs_builder_components.md. Off by default: a world
        // that authors builder components (the editor, while editing) keeps them as ordinary
        // components; a world that runs them (the game, or Play in the editor) turns this on.
        struct build_plan
        {
            archetype::instance*                                                    m_pFinalArchetype = nullptr;    // Input components minus the builder ones
            std::vector<const component::type::info*>                               m_BuilderInfos;                 // Consumed at creation, never placed
            std::vector<std::pair<const system::type::info*, system::instance*>>   m_Builders;                     // Builder systems whose query matches the input
        };
        void                                 EnableBuilders          ( bool bEnable
                                                                    ) noexcept { m_bBuildersEnabled = bEnable; m_BuildPlans.clear(); }
        [[nodiscard]] bool                   areBuildersEnabled      ( void
                                                                    ) const noexcept { return m_bBuildersEnabled; }
        // Cached per input component set. Prefab entities (prefab::tag) are templates and never built.
        inline
        const build_plan&                    getBuildPlan            ( std::span<const component::type::info* const> Infos
                                                                    ) noexcept;
        // The live, actively-tested persistence path for a raw ECS game state (used by
        // dependencies/xECSV2/smoke_test.cpp's own save+load round trip) - distinct from the
        // resource-pipeline-integrated Scene/Level/Prefab persistence (xecs_scene_inline.h/
        // xecs_prefab_mgr_inline.h) that E29 and friends actually use for editor content.
        // XECS_API: same reasoning as Run/Stop above - an ordinary out-of-line member function.
        XECS_API
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
        void*                                                m_pUserData         = nullptr;
        bool                                                 m_bBuildersEnabled  = false;
        std::unordered_map<std::uint64_t, build_plan>        m_BuildPlans        {};
        std::uint32_t                                        m_BuildPlansVersion = 0;       // m_SystemMgr.m_BuilderSystemsVersion the cache was built against
        // For the editor (the System Registry, ListEventHandlers): which global events exist (their name, what they tell and when) and which systems are event handlers. Last members of the
        // world on purpose: a binary built before them (a Game.dll that was not rebuilt) never looks past what it knew.
        struct event_record         { xecs::event::type::guid m_Guid; const char* m_pName; const char* m_pHelp; };
        struct event_handler_record { const xecs::system::type::info* m_pSystem; xecs::event::type::guid m_Event; const xecs::system::type::info* m_pOwner; };   // a global event handler names its event; a system event handler names the system that owns the event
        std::vector<event_record>                            m_EventRecords;
        std::vector<event_handler_record>                    m_EventHandlerRecords;
    };
}