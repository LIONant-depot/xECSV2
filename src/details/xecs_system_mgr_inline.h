namespace xecs::system
{
    //-------------------------------------------------------------------------------------------
    mgr::~mgr( void ) noexcept
    {
        while( m_UpdaterSystems.size() )
        {
            auto p = m_UpdaterSystems.back().second.release();
            m_UpdaterSystems.back().first->m_DestroyFunction(*p);
            delete reinterpret_cast<void*>(p);
            m_UpdaterSystems.pop_back();
        }

        while (m_NotifierSystems.size())
        {
            auto p = m_NotifierSystems.back().second.release();
            m_NotifierSystems.back().first->m_DestroyFunction(*p);
            delete reinterpret_cast<void*>(p);
            m_NotifierSystems.pop_back();
        }

    }

    //-------------------------------------------------------------------------------------------

    template
    < typename T_SYSTEM
    > requires( std::derived_from< T_SYSTEM, xecs::system::instance> )
    T_SYSTEM& mgr::RegisterSystem( xecs::game_mgr::instance& GameMgr ) noexcept
    {
        using real_system = details::compleated<T_SYSTEM>;
        using typedef_t   = std::decay_t<decltype(T_SYSTEM::typedef_v)>;

        //
        // TODO: Validate the crap out of each system type
        //


        //
        // Register System
        //
        auto& System = *static_cast<real_system*>([&]
        {
            if constexpr( real_system::typedef_v.is_notifier_v )
            {
                m_NotifierSystems.push_back({ &type::info_v<T_SYSTEM>, std::make_unique< real_system >(GameMgr) });
                return m_NotifierSystems.back().second.get();
            }
            else
            {
                m_UpdaterSystems.push_back({ &type::info_v<T_SYSTEM>, std::make_unique< real_system >(GameMgr) });
                return m_UpdaterSystems.back().second.get();
            }
        }());

        m_SystemMaps.emplace( std::pair<type::guid, xecs::system::instance*>
            { type::info_v<T_SYSTEM>.m_Guid, static_cast<instance*>(&System) });

        //
        // Call the OnCreate if the user overwrote that
        //
        if constexpr ( &T_SYSTEM::OnCreate != &xecs::system::overrides::OnCreate )
        {
            System.OnCreate();
        }

        //
        // For all systems that are not type event we create the query and update the info
        // this is like caching it...
        // 
        if constexpr (real_system::typedef_v.id_v != type::id::GLOBAL_EVENT )
        {
            xecs::query::instance Q;
            Q.AddQueryFromTuple(xecs::types::null_tuple_v< typename T_SYSTEM::query >);
            if constexpr ( xecs::function::is_callable_v<T_SYSTEM>)
            {
                Q.AddQueryFromFunction<T_SYSTEM>();
            }
            type::info_v<T_SYSTEM>.m_Query = Q;
        }

        //
        // For the Update systems hook the run function
        //
        if constexpr(real_system::typedef_v.id_v == type::id::UPDATE )
        {
            m_Events.m_OnUpdate.Register<&real_system::Run>(System);
        }

        //
        // If it is a global event delegate then we need to hook it up with the actual event
        // NOTE: that this requires all the relevant events to be register before our system delegate
        if constexpr (real_system::typedef_v.id_v == type::id::GLOBAL_EVENT )
        {
            GameMgr.m_EventMgr.getEvent< typedef_t::event_t >()
                .Register<&T_SYSTEM::OnEvent>( GameMgr.getSystem< T_SYSTEM >() );
        }

        //
        // If we are dealing with a system event type
        //
        if constexpr (real_system::typedef_v.id_v == type::id::SYSTEM_EVENT)
        {
            static_assert( xecs::types::tuple_t2i_v<typedef_t::event_t, typedef_t::system_t::events > + 1 );

            if constexpr( xecs::types::is_specialized_v<xecs::system::type::child_update, typedef_t > )
            {
                std::get<typedef_t::event_t>
                ( 
                    reinterpret_cast< details::compleated<typedef_t::system_t>* >
                    ( find<typedef_t::system_t>()
                    )->m_Events 
                )
                .Register<&real_system::Run>(System);
            }
            else
            {
                std::get<typedef_t::event_t>
                ( 
                    reinterpret_cast< details::compleated<typedef_t::system_t>* >
                    ( find<typedef_t::system_t>()
                    )->m_Events 
                )
                .Register<&T_SYSTEM::OnEvent>(System);
            }
        }

        //
        // General messages
        //
        if constexpr ( &real_system::OnGameStart != &xecs::system::overrides::OnGameStart )
        {
            m_Events.m_OnGameStart.Register<&real_system::OnGameStart>(System);
        }
        if constexpr (&real_system::OnGameEnd != &xecs::system::overrides::OnGameEnd)
        {
            m_Events.m_OnGameEnd.Register<&real_system::OnGameEnd>(System);
        }
        if constexpr (&real_system::OnFrameStart != &xecs::system::overrides::OnFrameStart)
        {
            m_Events.m_OnFrameStart.Register<&real_system::OnFrameStart>(System);
        }
        if constexpr (&real_system::OnFrameEnd != &xecs::system::overrides::OnFrameEnd)
        {
            m_Events.m_OnFrameEnd.Register<&real_system::OnFrameEnd>(System);
        }

        return System;
    }

    //---------------------------------------------------------------------------

    void mgr::Run( void ) noexcept
    {
        m_Events.m_OnFrameStart.NotifyAll();
        m_Events.m_OnUpdate.NotifyAll();
        m_Events.m_OnFrameEnd.NotifyAll();
    }

    //---------------------------------------------------------------------------
    template< typename T_SYSTEM >
    T_SYSTEM* mgr::find( void ) noexcept
    {
        auto I = m_SystemMaps.find( xecs::system::type::info_v<T_SYSTEM>.m_Guid );
        if(I == m_SystemMaps.end() ) return nullptr;
        return static_cast<T_SYSTEM*>(I->second);
    }

    //---------------------------------------------------------------------------

    void mgr::OnNewArchetype( xecs::archetype::instance& Archetype ) noexcept
    {
        for( auto& E : m_NotifierSystems )
        {
            auto& Entry = *E.second;
            if(E.first->m_Query.Compare(Archetype.m_ComponentBits) )
            {
                E.first->m_NotifierRegistration( Archetype, Entry );
            }
        }
    }
}