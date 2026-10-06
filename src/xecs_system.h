namespace xecs::system
{
    struct mgr;

    // A connector is a place on a system where other systems can be connected: its children. The parent system decides when its children run
    // (system::instance::RunConnector) and how many times, they run in the order they have in the system manager. A system names its connectors
    // and says what each one is for, so the person building the hierarchy in the editor knows what they are:
    //
    //      static constexpr std::array<xecs::system::connector, 1> connectors_v { { { "Before Step", "Runs once for every step, right before it" } } };
    //
    // Which system is connected to which connector is DATA (xecs::system::mgr::Save/Load, edited in the System Registry): a system nobody connected
    // runs at the top level, in the order it was registered.
    //
    // A constraint is a guarantee a place of the system graph gives to the systems placed in it (the top level of the frame gives none), and a need of a system: a system can only
    // be placed where everything it needs is given. A constraint is an empty type that names itself:
    //
    //      struct fixed_step { static constexpr auto typedef_v = xecs::system::constraint::def{ .m_pName = "Fixed Step", .m_pDescription = "Runs once for every fixed step of the game" }; };
    //
    // A system says what it needs with `using constraints = std::tuple<fixed_step>;`, a connector what it gives in `m_Provides` (see provides_v). The guid is made from the type, as
    // the guid of a system is: the editor shows the names, the data (SystemOrder.config.txt) never mentions a constraint, only where each system is placed.
    //
    namespace constraint
    {
        using guid = xresource::guid<struct constraint_tag>;

        struct def
        {
            const char* m_pName         = "Unnamed Constraint";
            const char* m_pDescription  = "";
        };

        struct info
        {
            guid        m_Guid;
            const char* m_pName;
            const char* m_pDescription;
        };

        namespace details
        {
            template< typename T >
            consteval guid GuidOf( void ) noexcept { return guid{ __FUNCSIG__ }; }

            template< typename T >
            inline constexpr info info_v = { GuidOf<T>(), T::typedef_v.m_pName, T::typedef_v.m_pDescription };

            template< typename T_TUPLE >
            struct of_tuple;

            template< typename... T >
            struct of_tuple< std::tuple<T...> >
            {
                static constexpr std::array<info, sizeof...(T)> value_v = { info_v<T>... };
            };
        }

        // What a connector gives: provides_v<fixed_step, before_step>
        template< typename... T >
        inline constexpr std::array<info, sizeof...(T)> provides_v = { details::info_v<T>... };

        // What a system needs, from its `constraints` tuple.
        template< typename T_TUPLE >
        inline constexpr auto& of_tuple_v = details::of_tuple<T_TUPLE>::value_v;

        // Whether everything in Needed is in Given.
        inline constexpr bool Solved( std::span<const info> Needed, std::span<const info> Given ) noexcept
        {
            for( auto& N : Needed )
            {
                bool bFound = false;
                for( auto& G : Given ) if( G.m_Guid == N.m_Guid ) { bFound = true; break; }
                if( !bFound ) return false;
            }
            return true;
        }
    }

    struct connector
    {
        const char*                         m_pName;
        const char*                         m_pDescription;
        std::span<const constraint::info>   m_Provides {};      // what the systems connected here are given (a system needs all of its constraints given to be connected)
    };

    namespace type
    {
        using guid = xresource::guid<struct system_tag>;

        enum class id : std::uint8_t
        {
            UPDATE
        ,   NOTIFY_CREATE
        ,   NOTIFY_DESTROY
        ,   NOTIFY_MODIFIED
        ,   NOTIFY_MOVE_IN
        ,   NOTIFY_MOVE_OUT
        ,   NOTIFY_COMPONENT_CHANGE
        ,   NOTIFY_COMPONENT_ADDED
        ,   NOTIFY_COMPONENT_REMOVE
        ,   POOL_FAMILY_CREATE
        ,   POOL_FAMILY_DESTROY
        ,   GLOBAL_EVENT
        ,   SYSTEM_EVENT
        ,   BUILDER
        };

        struct update
        {
            static constexpr auto       id_v                = id::UPDATE;
            static constexpr auto       is_notifier_v       = false;
            const char*                 m_pName             = "Unnamed Update System";
            guid                        m_Guid              {};
        };

        struct notify_create
        {
            static constexpr auto       id_v                = id::NOTIFY_CREATE;
            static constexpr auto       is_notifier_v       = true;
            const char*                 m_pName             = "Unnamed Notified Create Entity System";
            guid                        m_Guid              {};
        };

        struct notify_destroy
        {
            static constexpr auto       id_v                = id::NOTIFY_DESTROY;
            static constexpr auto       is_notifier_v       = true;
            const char*                 m_pName             = "Unnamed Notified Destroy Entity System";
            guid                        m_Guid              {};
        };

        struct notify_moved_in
        {
            static constexpr auto       id_v                = id::NOTIFY_MOVE_IN;
            static constexpr auto       is_notifier_v       = true;
            const char*                 m_pName             = "Unnamed Notified Move In Entity System";
            guid                        m_Guid              {};
        };

        struct notify_moved_out
        {
            static constexpr auto       id_v                = id::NOTIFY_MOVE_OUT;
            static constexpr auto       is_notifier_v       = true;
            const char*                 m_pName             = "Unnamed Notified Move Out System";
            guid                        m_Guid              {};
        };

        struct notify_component_change
        {
            static constexpr auto        id_v               = id::NOTIFY_COMPONENT_CHANGE;
            static constexpr auto        is_notifier_v      = true;
            const char*                  m_pName            = "Unnamed Component Change System";
            guid                         m_Guid             {};
      const xecs::component::type::info* m_pComponentInfo   {};
        };

        // Runs once per entity while it is being created, never in the frame loop. Reads the entity's
        // builder components (const) and hands their data to whatever system owns it from then on,
        // writing back only handles - see doc/xecs_builder_components.md.
        struct builder
        {
            static constexpr auto       id_v                = id::BUILDER;
            static constexpr auto       is_notifier_v       = false;
            const char*                 m_pName             = "Unnamed Builder System";
            guid                        m_Guid              {};
        };

        struct pool_family_create
        {
            static constexpr auto       id_v                = id::POOL_FAMILY_CREATE;
            static constexpr auto       is_notifier_v       = true;
            const char*                 m_pName             = "Unnamed Pool Family Create System";
            guid                        m_Guid              {};
        };

        struct pool_family_destroy
        {
            static constexpr auto       id_v                = id::POOL_FAMILY_DESTROY;
            static constexpr auto       is_notifier_v       = true;
            const char*                 m_pName             = "Unnamed Pool Family Create System";
            guid                        m_Guid              {};
        };

        template< typename T_EVENT >
        requires( std::derived_from< T_EVENT, xecs::event::overrides> )
        struct global_event
        {
            using                        event_t            = T_EVENT;
            static constexpr auto        id_v               = id::GLOBAL_EVENT;
            static constexpr auto        is_notifier_v      = false;
            const char*                  m_pName            = "Unnamed Global Event System Delegate";
            guid                         m_Guid             {};
        };

        template< typename T_SYSTEM, typename T_EVENT >
        requires( std::derived_from< T_SYSTEM, xecs::system::instance>
                  && ( xecs::types::is_specialized_v<xecs::event::instance, T_EVENT>
                       || std::derived_from< T_EVENT, xecs::event::overrides>) )
        struct system_event
        {
            using                        system_t           = T_SYSTEM;
            using                        event_t            = T_EVENT;
            static constexpr auto        id_v               = id::SYSTEM_EVENT;
            static constexpr auto        is_notifier_v      = false;
            const char*                  m_pName            = "Unnamed System Event Delegate";
            guid                         m_Guid             {};
        };

        template< typename T_SYSTEM, typename T_EVENT >
        requires( std::derived_from< T_SYSTEM, xecs::system::instance>
                  && ( xecs::types::is_specialized_v<xecs::event::instance, T_EVENT>
                       || std::derived_from< T_EVENT, xecs::event::overrides>) )
        struct child_update
        {
            using                        system_t           = T_SYSTEM;
            using                        event_t            = T_EVENT;
            static constexpr auto        id_v               = id::SYSTEM_EVENT;
            static constexpr auto        is_notifier_v      = false;
            const char*                  m_pName            = "Unnamed Child Update Delegate";
            guid                         m_Guid             {};
        };

        // What a system declares about one component, derived at compile time from its `query` tuple
        // and its operator() parameters (see details::access_v). For tools/debugging only - a system
        // may still touch other components inside its own OnUpdate code.
        enum class match : std::uint8_t
        {   MUST        // entity must have it to be processed
        ,   ONE_OF      // entity must have at least one of the ONE_OF set
        ,   NONE_OF     // entity must NOT have it
        ,   IF_PRESENT  // touched if present, never affects matching (query::optional)
        };

        enum class access : std::uint8_t
        {   NONE        // filter only (none_of)
        ,   READ        // const in the query tuple / const operator() parameter
        ,   WRITE       // non-const: the system may modify it
        };

        struct component_access
        {
            xecs::component::type::guid     m_ComponentGuid;
            const char*                     m_pComponentName;
            match                           m_Match;
            access                          m_Access;
        };

        struct info
        {
            using notifier_registration = void( xecs::archetype::instance&, xecs::system::instance&) noexcept;
            using destroy_fn            = void( xecs::system::instance& ) noexcept;
            using resolve_fn            = std::byte*( const void* pContext, const xecs::component::type::info& Info ) noexcept;
            using build_fn              = void( xecs::system::instance&, resolve_fn* pResolve, const void* pContext ) noexcept;

            const type::guid                        m_Guid;
            // Runtime-computed (by system::mgr::RegisterSystem), written back onto what's otherwise
            // a compile-time singleton - same invariant as component::type::info::m_BitID (see its
            // own comment): safe as long as only one binary ever names/registers this T_SYSTEM, which
            // is every system in practice - xECSV2 ships NO built-in system types of its own
            // (RegisterSystems<T...>() is always instantiated with types the CONSUMER supplies), so
            // unlike the handful of built-in component types, there is no case here that will ever
            // need the explicit-instantiation/export treatment a shared-library build requires.
            mutable xecs::query::instance           m_Query;
            notifier_registration* const            m_NotifierRegistration;
            destroy_fn* const                       m_DestroyFunction;
            build_fn* const                         m_BuildFunction;        // Builder systems only: calls operator() with each argument's pointer from pResolve
            const char* const                       m_pName;
            const id                                m_ID;
            const std::span<const component_access> m_Access;
        };

        namespace details
        {
            template< typename T >
            consteval type::info CreateInfo(void) noexcept;

            template< typename T >
            static constexpr auto info_v = CreateInfo<T>();
        }

        template< typename T_SYSTEM >
        constexpr static auto& info_v = details::info_v<T_SYSTEM>;
    }

    //-----------------------------------------------------------------
    // SYSTEM OVERRIDES
    //-----------------------------------------------------------------
    struct overrides
    {
        using                   entity      = xecs::component::entity;      // Shortcut for entity
        using                   query       = std::tuple<>;                 // Override this to specify the query for the system
        using                   events      = std::tuple<>;                 // Override this to create events which other systems can use
        using                   constraints = std::tuple<>;                 // Override this to say what the system needs from the place it is placed in (see xecs::system::constraint)
        constexpr static auto   typedef_v   = type::update{};               // Override this to specify which type of system this is

        void    OnCreate                ( void )                noexcept {} // All Systems:         When the system is created
        void    OnGameStart             ( void )                noexcept {} // All Systems:         When the game starts or when it becomes unpaused
        void    OnFrameStart            ( void )                noexcept {} // All Systems:         At the begging of a frame
        void    OnPreUpdate             ( void )                noexcept {} // Update Systems:      Is call just before the OnUpdate gets call
        void    OnUpdate                ( void )                noexcept {} // Update Systems:      If you want full control of the query
        void    OnPostUpdate            ( void )                noexcept {} // Update Systems:      Is call just after the OnUpdate gets call and before structural changes happen
        void    OnPostStructuralChanges ( void )                noexcept {} // Update Systems:      After the Structural changes has taken place (applies only to )
        void    OnFrameEnd              ( void )                noexcept {} // All Systems:         Beginning of every frame
        void    OnGameEnd               ( void )                noexcept {} // All Systems:         When the game is done
        void    OnDestroy               ( void )                noexcept {} // All Systems:         Before destroying the system
        void    OnGamePause             ( void )                noexcept {} // All Systems:         When the game is paused 
        void    OnEvent                 ( ...  )                noexcept {} // Event System:        User overrides to receive the event message
        void    OnNotify                ( entity& Entity )      noexcept {} // Notify Systems:      Advance control
        void    OnPoolFamily            ( archetype::instance&              // Pool Family System:  Gets notified when a pool is created or destroy
                                        , pool::family&       ) noexcept {}
    };

    //-----------------------------------------------------------------
    // SYSTEM INSTANCE
    //-----------------------------------------------------------------
    struct instance : overrides
    {
                                            instance                ( const instance&
                                                                    ) noexcept = delete;

        constexpr                           instance                ( xecs::game_mgr::instance& GameMgr
                                                                    ) noexcept;
        __forceinline
        bool                                isEntityDead            ( xecs::component::entity Entity
                                                                    ) const noexcept;
        template
        < typename   T_EVENT
        , typename   T_CLASS
        , typename...T_ARGS
        > requires
        ( std::derived_from<T_CLASS, xecs::system::instance>
            && (false == std::is_same_v<typename T_CLASS::events, xecs::system::overrides::events>)
            && !!(xecs::types::tuple_t2i_v<T_EVENT, typename T_CLASS::events > +1)
        ) __inline constexpr
        static void                         SendEventFrom           ( T_CLASS* pThis
                                                                    , T_ARGS&&... Args
                                                                    ) noexcept;
        template
        < typename      T_GLOBAL_EVENT
        > requires
        ( std::derived_from< T_GLOBAL_EVENT, xecs::event::overrides>
        ) __inline constexpr
        T_GLOBAL_EVENT&                     getGlobalEvent          ( void
                                                                    ) const noexcept;
        template
        < typename      T_GLOBAL_EVENT
        , typename...   T_ARGS
        > requires 
        ( std::derived_from< T_GLOBAL_EVENT, xecs::event::overrides>
        ) __inline constexpr
        void                                SendGlobalEvent         ( T_ARGS&&... Args 
                                                                    ) const noexcept;
        template
        < typename... T_TUPLES_OF_COMPONENTS_OR_COMPONENTS
        > requires
        ( 
            (   (  xecs::tools::valid_tuple_components_v<T_TUPLES_OF_COMPONENTS_OR_COMPONENTS>
                || xecs::component::type::is_valid_v<T_TUPLES_OF_COMPONENTS_OR_COMPONENTS> 
                ) &&... )
        ) __inline constexpr
        archetype::instance&                getOrCreateArchetype    ( void 
                                                                    ) const noexcept;
        template
        < typename... T_COMPONENTS
        > __inline constexpr
        [[nodiscard]] std::vector<archetype::instance*>
                                            Search                  ( const xecs::query::instance& Query
                                                                    ) const noexcept;
        template
        <   typename T_TUPLE_ADD
        ,   typename T_TUPLE_SUBTRACT   = std::tuple<>
        ,   typename T_FUNCTION         = xecs::tools::empty_lambda
        > requires
        ( xecs::function::is_callable_v<T_FUNCTION>
        && xecs::types::is_specialized_v<std::tuple, T_TUPLE_ADD>
        && xecs::types::is_specialized_v<std::tuple, T_TUPLE_SUBTRACT>
        ) __inline constexpr
        [[nodiscard]] xecs::component::entity
                                            AddOrRemoveComponents   ( xecs::component::entity   Entity
                                                                    , T_FUNCTION&&              Function = xecs::tools::empty_lambda{}
                                                                    ) const noexcept;
        __inline
        void                                DeleteEntity            ( xecs::component::entity& Entity 
                                                                    ) const noexcept;
        template
        <   typename T_FUNCTION
        ,   auto     T_SHARE_AS_DATA = false
        > requires
        ( xecs::tools::assert_is_callable_v<T_FUNCTION>
            && (   xecs::tools::function_return_v<T_FUNCTION, bool >
                || xecs::tools::function_return_v<T_FUNCTION, void > )
        ) __inline constexpr
            bool                            Foreach                 ( std::span<xecs::archetype::instance* const>   List
                                                                    , T_FUNCTION&&                                  Function 
                                                                    ) const noexcept;
        template
        <   typename T_FUNCTION
        ,   auto     T_SHARE_AS_DATA = false
        > requires
        ( xecs::tools::assert_is_callable_v<T_FUNCTION>
            && (   xecs::tools::function_return_v<T_FUNCTION, bool >
                || xecs::tools::function_return_v<T_FUNCTION, void > )
        ) __inline constexpr
            bool                            QForeach                ( T_FUNCTION&&                                  Function
                                                                    ) const noexcept;

        template
        <   typename T_FUNCTION
        > requires
        ( xecs::tools::assert_is_callable_v<T_FUNCTION>
            && (   xecs::tools::function_return_v<T_FUNCTION, bool >
                || xecs::tools::function_return_v<T_FUNCTION, void > )
        ) __inline
        bool                                Foreach                 ( const xecs::component::share_filter&  ShareFilter
                                                                    , const xecs::query::instance&          Query
                                                                    , T_FUNCTION&&                          Function 
                                                                    ) noexcept;
        template
        < typename T_SYSTEM
        > __inline constexpr
        T_SYSTEM*                           findSystem              ( void
                                                                    ) const noexcept;
        template
        < typename T_SYSTEM
        > __inline constexpr
        T_SYSTEM&                           getSystem               ( void
                                                                    ) const noexcept;
        template
        < typename T_FUNCTION = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_FUNCTION>
          && (false == xecs::tools::function_has_share_component_args_v<T_FUNCTION>)
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
        < typename...T_COMPONENTS
        , typename T_FUNCTION   = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_FUNCTION>
        ) xforceinline 
        [[nodiscard]]  xecs::prefab::guid   CreatePrefab            ( T_FUNCTION&&              Function        = xecs::tools::empty_lambda{}
                                                                    ) noexcept;
        template
        < typename T_FUNCTION   = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_FUNCTION>
        ) xforceinline 
        [[nodiscard]] xecs::prefab::guid    CreatePrefab            ( const xecs::tools::bits&  ComponentBits
                                                                    , T_FUNCTION&&              Function        = xecs::tools::empty_lambda{}
                                                                    ) noexcept;
        template
        < typename T_ADD_TUPLE = std::tuple<>
        , typename T_SUB_TUPLE = std::tuple<>
        , typename T_FUNCTION  = xecs::tools::empty_lambda
        > requires
        ( xecs::tools::assert_standard_function_v<T_FUNCTION>
        ) xforceinline 
        void                                CreatePrefabInstance    ( int                       Count
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
        ) xforceinline 
        [[nodiscard]] xecs::prefab::guid    CreatePrefabVariant     ( xecs::prefab::guid        PrefabGuid
                                                                    , T_FUNCTION&&              Function = xecs::tools::empty_lambda{}
                                                                    ) noexcept;
        template
        < typename... T_COMPONENTS
        > requires
        ( xecs::tools::assert_valid_tuple_components_v< std::tuple<T_COMPONENTS...> >
        ) __inline
        bool                                hasComponents           ( xecs::component::entity Entity
                                                                    ) const noexcept;
        template
        < typename T_SHARE_COMPONENT
        > __inline
        const xecs::component::share_filter*
                                            findShareFilter         ( T_SHARE_COMPONENT&&       ShareComponent
                                                                    , xecs::archetype::guid     ArchetypeGuid = xecs::archetype::guid{}
                                                                    ) noexcept;
        __inline
        const xecs::component::share_filter*
                                            findShareFilter         ( xecs::component::type::share::key Key
                                                                    ) noexcept;
        // The connectors of this system (empty when it has none), and what is connected to them: a system that is connected is not run by the
        // frame, its parent runs it. isConnected() tells a system which of the two it is.
        [[nodiscard]] std::span<const connector>    getConnectors           ( void ) const noexcept { return m_Connectors; }
        [[nodiscard]] bool                          isConnected             ( void ) const noexcept { return m_pParent != nullptr; }
        // What the system needs from the place it is placed in, and whether it is placed at all: a system that is not placed does not run (it is one of the available systems of the registry).
        [[nodiscard]] std::span<const constraint::info> getConstraints      ( void ) const noexcept { return m_Requires; }
        [[nodiscard]] bool                          isPlaced                ( void ) const noexcept { return m_bPlaced; }
        // The game manager this system belongs to: for what the system's own functions do not forward (the user data of the game, a function that takes the manager).
        [[nodiscard]] xecs::game_mgr::instance&     getGameMgr              ( void ) const noexcept { return m_GameMgr; }
        // Runs the children of one of the connectors of THIS system, in order (a child that is disabled is skipped).
        inline void                                 RunConnector            ( int ConnectorIndex ) noexcept;

    private:

        xecs::game_mgr::instance&   m_GameMgr;
        std::span<const connector>  m_Connectors    {};
        std::span<const constraint::info> m_Requires {};            // what the system needs of the place it is placed in
        bool                        m_bPlaced       = true;         // in the graph (the top level, or a connector): an update system that is not placed does not run
        xecs::system::instance*     m_pParent       = nullptr;      // the system this one is connected to, and the connector of it
        int                         m_ParentConnector = -1;
        friend struct xecs::system::mgr;

        template< typename T_USER_SYSTEM >
        requires( std::derived_from< T_USER_SYSTEM, xecs::system::instance > )
        friend struct details::compleated;
    };
}