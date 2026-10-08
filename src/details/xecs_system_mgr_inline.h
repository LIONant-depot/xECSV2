#include <filesystem>

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

        while (m_BuilderSystems.size())
        {
            auto p = m_BuilderSystems.back().second.release();
            m_BuilderSystems.back().first->m_DestroyFunction(*p);
            delete reinterpret_cast<void*>(p);
            m_BuilderSystems.pop_back();
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
            // The systems that run when something happens (an archetype event, a global event, an event of another system) are the notifiers: only the update systems have an
            // update delegate, and the saved order (Load) and the System Registry index m_UpdaterSystems and that delegate list together.
            if constexpr( real_system::typedef_v.is_notifier_v
                       || real_system::typedef_v.id_v == type::id::GLOBAL_EVENT
                       || real_system::typedef_v.id_v == type::id::SYSTEM_EVENT )
            {
                m_NotifierSystems.push_back({ &type::info_v<T_SYSTEM>, std::make_unique< real_system >(GameMgr) });
                return m_NotifierSystems.back().second.get();
            }
            else if constexpr( real_system::typedef_v.id_v == type::id::BUILDER )
            {
                m_BuilderSystems.push_back({ &type::info_v<T_SYSTEM>, std::make_unique< real_system >(GameMgr) });
                ++m_BuilderSystemsVersion;
                return m_BuilderSystems.back().second.get();
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
        // Builder components never exist on built entities - a (non-builder) system that requires one
        // silently matches nothing once builders are on (doc/xecs_builder_components.md).
        //
        if constexpr (real_system::typedef_v.id_v != type::id::BUILDER)
        {
            for( auto& Access : type::info_v<T_SYSTEM>.m_Access )
            {
                if( Access.m_Match != type::match::MUST && Access.m_Match != type::match::ONE_OF ) continue;
                auto* pInfo = xecs::component::mgr::findComponentTypeInfo( Access.m_ComponentGuid );
                if( pInfo == nullptr || pInfo->m_bBuilder == false ) continue;

                std::printf("[xECS Builder] WARNING: system '%s' requires builder component '%s' - built entities never have it, so this system won't match them\n"
                    , type::info_v<T_SYSTEM>.m_pName, pInfo->m_pName );
                std::fflush(stdout);
            }
        }

        //
        // For the Update systems hook the run function
        //
        if constexpr(real_system::typedef_v.id_v == type::id::UPDATE )
        {
            m_Events.m_OnUpdate.Register<&real_system::Run>(System);

            // The connectors the system declares (see xecs::system::connector).
            if constexpr (requires { real_system::connectors_v; })
                System.m_Connectors = std::span<const xecs::system::connector>(real_system::connectors_v);

            // What the system needs of the place it is placed in. Until a registry says otherwise a system that needs nothing is placed at the top level (what every system did before
            // there were constraints); one that needs something waits, not placed, for someone to put it where it is given.
            System.m_Requires = std::span<const xecs::system::constraint::info>(xecs::system::constraint::of_tuple_v<typename T_SYSTEM::constraints>);
            System.m_bPlaced  = System.m_Requires.empty();
        }

        //
        // If it is a global event delegate then we need to hook it up with the actual event
        // NOTE: that this requires all the relevant events to be register before our system delegate
        if constexpr (real_system::typedef_v.id_v == type::id::GLOBAL_EVENT )
        {
            GameMgr.m_EventMgr.template getEvent< typename typedef_t::event_t >()
                .template Register<&T_SYSTEM::OnEvent>( GameMgr.template getSystem< T_SYSTEM >() );
            GameMgr.m_EventHandlerRecords.push_back({ &type::info_v<T_SYSTEM>, xecs::event::type::info_v<typename typedef_t::event_t>.m_Guid, nullptr });
        }

        //
        // If we are dealing with a system event type
        //
        if constexpr (real_system::typedef_v.id_v == type::id::SYSTEM_EVENT)
        {
            static_assert( xecs::types::tuple_t2i_v<typename typedef_t::event_t, typename typedef_t::system_t::events > + 1 );
            GameMgr.m_EventHandlerRecords.push_back({ &type::info_v<T_SYSTEM>, {}, &type::info_v<typename typedef_t::system_t> });

            if constexpr( xecs::types::is_specialized_v<xecs::system::type::child_update, typedef_t > )
            {
                std::get<typedef_t::event_t>
                ( 
                    reinterpret_cast< details::compleated<typename typedef_t::system_t>* >
                    ( find<typedef_t::system_t>()
                    )->m_Events 
                )
                .Register<&real_system::Run>(System);
            }
            else
            {
                std::get<typedef_t::event_t>
                ( 
                    reinterpret_cast< details::compleated<typename typedef_t::system_t>* >
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
        for( std::size_t i = 0; i < m_UpdaterSystems.size(); ++i )
        {
            if( m_UpdaterSystems[i].second->m_pParent || !m_UpdaterSystems[i].second->m_bPlaced ) continue;     // connected: its parent runs it; not placed: nobody does
            const auto& D = m_Events.m_OnUpdate.m_Delegates[i];
            if( D.m_bEnabled ) D.m_pCallback(D.m_pClass);
        }
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

    //---------------------------------------------------------------------------
    // m_UpdaterSystems and m_Events.m_OnUpdate.m_Delegates are always the exact same size, grown 1:1
    // by RegisterSystem for every Update system (confirmed: only Update systems ever push to
    // m_OnUpdate, in the same call, same order) - every method below relies on that index alignment
    // instead of a separate order-index table.
    //---------------------------------------------------------------------------

    std::vector<update_system_row> mgr::GetUpdateSystemRows( void ) const noexcept
    {
        std::vector<update_system_row> Rows;
        Rows.reserve(m_UpdaterSystems.size());
        for( std::size_t i = 0; i < m_UpdaterSystems.size(); ++i )
        {
            const auto* pParent = m_UpdaterSystems[i].second->m_pParent;
            type::guid  ParentGuid{};
            if( pParent )
                for( auto& Other : m_UpdaterSystems )
                    if( Other.second.get() == pParent ) { ParentGuid = Other.first->m_Guid; break; }

            Rows.push_back(update_system_row
            { .m_Guid            = m_UpdaterSystems[i].first->m_Guid
            , .m_pName           = m_UpdaterSystems[i].first->m_pName
            , .m_bEnabled        = m_Events.m_OnUpdate.m_Delegates[i].m_bEnabled
            , .m_ParentGuid      = ParentGuid
            , .m_ParentConnector = m_UpdaterSystems[i].second->m_ParentConnector
            , .m_bPlaced         = m_UpdaterSystems[i].second->m_bPlaced
            , .m_Requires        = m_UpdaterSystems[i].second->m_Requires
            });
        }
        return Rows;
    }

    //---------------------------------------------------------------------------

    void mgr::MoveUpdateSystem( type::guid Guid, int Delta ) noexcept
    {
        const int Step = (Delta > 0) - (Delta < 0); // -1, 0, or +1 - one up/down click at a time
        if( Step == 0 ) return;

        int i = -1;
        for( int k = 0; k < static_cast<int>(m_UpdaterSystems.size()); ++k )
            if( m_UpdaterSystems[k].first->m_Guid == Guid ) { i = k; break; }
        if( i < 0 ) return;

        // Order only means something among the systems that share a parent and a connector (the top level is the one with no parent):
        // swap with the nearest of those in that direction, clamped at the ends.
        const auto& Me = *m_UpdaterSystems[i].second;
        for( int j = i + Step; j >= 0 && j < static_cast<int>(m_UpdaterSystems.size()); j += Step )
        {
            const auto& Other = *m_UpdaterSystems[j].second;
            if( Other.m_pParent != Me.m_pParent || Other.m_ParentConnector != Me.m_ParentConnector ) continue;
            std::swap( m_UpdaterSystems[i],                m_UpdaterSystems[j] );
            std::swap( m_Events.m_OnUpdate.m_Delegates[i], m_Events.m_OnUpdate.m_Delegates[j] );
            return;
        }
    }

    //---------------------------------------------------------------------------

    void mgr::SetUpdateSystemEnabled( type::guid Guid, bool bEnabled ) noexcept
    {
        for( std::size_t i = 0; i < m_UpdaterSystems.size(); ++i )
        {
            if( m_UpdaterSystems[i].first->m_Guid == Guid )
            {
                m_Events.m_OnUpdate.m_Delegates[i].m_bEnabled = bEnabled;
                return;
            }
        }
    }

    //---------------------------------------------------------------------------

    void mgr::SnapshotForPlay( void ) noexcept
    {
        m_PreRunSnapshot = GetUpdateSystemRows();
    }

    //---------------------------------------------------------------------------
    // Reorders/re-enables m_UpdaterSystems + its delegates back to exactly what they were the moment
    // SnapshotForPlay() was called - discards whatever the user toggled while "playing" (the entire
    // transient-vs-authored distinction lives here: while stopped, m_PreRunSnapshot is empty and every
    // Move/SetEnabled call directly IS the authored state, nothing else to revert).
    void mgr::RestoreFromSnapshot( void ) noexcept
    {
        if( m_PreRunSnapshot.empty() ) return;
        const auto Rows = std::move(m_PreRunSnapshot);
        m_PreRunSnapshot.clear();
        ApplyUpdateSystemRows( Rows );
    }

    //---------------------------------------------------------------------------

    void mgr::ApplyUpdateSystemRows( const std::vector<update_system_row>& Rows ) noexcept
    {
        for( std::size_t i = 0; i < Rows.size() && i < m_UpdaterSystems.size(); ++i )
        {
            const auto Guid = Rows[i].m_Guid;
            if( m_UpdaterSystems[i].first->m_Guid != Guid )
            {
                for( std::size_t k = i + 1; k < m_UpdaterSystems.size(); ++k )
                {
                    if( m_UpdaterSystems[k].first->m_Guid == Guid )
                    {
                        std::swap( m_UpdaterSystems[i],                m_UpdaterSystems[k] );
                        std::swap( m_Events.m_OnUpdate.m_Delegates[i], m_Events.m_OnUpdate.m_Delegates[k] );
                        break;
                    }
                }
            }
            m_Events.m_OnUpdate.m_Delegates[i].m_bEnabled = Rows[i].m_bEnabled;
        }
        // The graph as it was: what was not placed, then the connections (a connection needs its parent placed, so the passes go on while one more can be made).
        for( auto& Row : Rows )
            if( !Row.m_bPlaced ) UnplaceUpdateSystem( Row.m_Guid );
        for( std::size_t Pass = 0; Pass < Rows.size(); ++Pass )
        {
            bool bProgress = false;
            for( auto& Row : Rows )
            {
                if( !Row.m_bPlaced ) continue;
                const int i = [&]{ for( int k = 0; k < static_cast<int>(m_UpdaterSystems.size()); ++k ) if( m_UpdaterSystems[k].first->m_Guid == Row.m_Guid ) return k; return -1; }();
                if( i < 0 ) continue;
                const auto& S = *m_UpdaterSystems[i].second;
                const bool bThere = S.m_bPlaced && ( (S.m_pParent == nullptr) == Row.m_ParentGuid.empty() ) && S.m_ParentConnector == Row.m_ParentConnector;
                if( bThere ) continue;
                if( SetUpdateSystemParent( Row.m_Guid, Row.m_ParentGuid, Row.m_ParentConnector, false ) ) bProgress = true;
            }
            if( !bProgress ) break;
        }
    }

    //---------------------------------------------------------------------------

    bool mgr::DropUpdateSystemOn( type::guid Source, type::guid Target ) noexcept
    {
        if( Source == Target ) return false;

        const auto Rows = GetUpdateSystemRows();
        const update_system_row* pSource = nullptr;
        const update_system_row* pTarget = nullptr;
        for( auto& R : Rows ) { if( R.m_Guid == Source ) pSource = &R; if( R.m_Guid == Target ) pTarget = &R; }
        if( !pSource || !pTarget || !pTarget->m_bPlaced ) return false;
        if( !CanPlaceUpdateSystem( Source, pTarget->m_ParentGuid, pTarget->m_ParentConnector ) ) return false;

        if( !pSource->m_bPlaced || pSource->m_ParentGuid != pTarget->m_ParentGuid || pSource->m_ParentConnector != pTarget->m_ParentConnector )
            if( !SetUpdateSystemParent( Source, pTarget->m_ParentGuid, pTarget->m_ParentConnector ) ) return false;

        // The order among the systems of that connector (or of the top level): the source takes the position the target has now (the target moves one over), one step at a time.
        const auto SiblingsNow = [&]() noexcept
        {
            std::vector<type::guid> Siblings;
            for( auto& R : GetUpdateSystemRows() )
                if( R.m_bPlaced && R.m_ParentGuid == pTarget->m_ParentGuid && R.m_ParentConnector == pTarget->m_ParentConnector ) Siblings.push_back( R.m_Guid );
            return Siblings;
        };
        const auto IndexIn = []( const std::vector<type::guid>& Siblings, type::guid Guid ) noexcept { return static_cast<int>( std::find( Siblings.begin(), Siblings.end(), Guid ) - Siblings.begin() ); };
        const int Goal = IndexIn( SiblingsNow(), Target );
        for( int Guard = 0; Guard < 256; ++Guard )
        {
            const auto Siblings = SiblingsNow();
            const int  is       = IndexIn( Siblings, Source );
            if( is == Goal || is >= static_cast<int>(Siblings.size()) || Goal >= static_cast<int>(Siblings.size()) ) break;
            MoveUpdateSystem( Source, Goal > is ? 1 : -1 );
        }
        return true;
    }

    //---------------------------------------------------------------------------
    // {m_ProjectPath}\Project.config\SystemOrder.config.txt - same fixed, non-asset settings-file
    // convention e10::library_mgr already uses for its own Library.config.txt (plain xtextfile +
    // xproperty::sprop::serializer::Stream against an ordinary XPROPERTY_DEF'd struct).
    xerr mgr::Save( void ) const noexcept
    {
        system_order_config Config;
        Config.m_UpdateOrder.reserve(m_UpdaterSystems.size());
        for( std::size_t i = 0; i < m_UpdaterSystems.size(); ++i )
        {
            Config.m_UpdateOrder.push_back(system_order_entry
            { .m_Guid     = m_UpdaterSystems[i].first->m_Guid.m_Value
            , .m_Name     = m_UpdaterSystems[i].first->m_pName
            , .m_bEnabled = m_Events.m_OnUpdate.m_Delegates[i].m_bEnabled
            , .m_bPlaced  = m_UpdaterSystems[i].second->m_bPlaced
            });

            if( const auto* pParent = m_UpdaterSystems[i].second->m_pParent; pParent )
                for( auto& Other : m_UpdaterSystems )
                    if( Other.second.get() == pParent )
                    {
                        const int c = m_UpdaterSystems[i].second->m_ParentConnector;
                        Config.m_UpdateOrder.back().m_ParentGuid = Other.first->m_Guid.m_Value;
                        if( c >= 0 && c < static_cast<int>(pParent->m_Connectors.size()) ) Config.m_UpdateOrder.back().m_Connector = pParent->m_Connectors[c].m_pName;
                        break;
                    }
        }

        const auto ConfigFolder = std::format(L"{}\\Project.config", m_ProjectPath);
        if( false == std::filesystem::exists(ConfigFolder) )
            std::filesystem::create_directories(ConfigFolder);

        // The systems of a Game that is not the one loaded now are not in m_UpdaterSystems: their entries in the file are not ours to erase (the project's Games share this file, and the
        // editor switches Games). Keep each one right after the entry that came before it in the old file and is still here (at the front when none), so the order around it is as it was.
        {
            xtextfile::stream OldStream;
            system_order_config Old;
            xproperty::settings::context OldContext;
            if( !OldStream.Open(true, std::format(L"{}\\SystemOrder.config.txt", ConfigFolder), { xtextfile::file_type::TEXT })
                && !xproperty::sprop::serializer::Stream( OldStream, Old, OldContext ) )
            {
                auto IndexOf = [&]( std::uint64_t Guid ) noexcept
                {
                    for( std::size_t k = 0; k < Config.m_UpdateOrder.size(); ++k )
                        if( Config.m_UpdateOrder[k].m_Guid == Guid ) return static_cast<int>(k);
                    return -1;
                };
                int InsertAt = 0;
                for( auto& Entry : Old.m_UpdateOrder )
                {
                    if( const int Live = IndexOf(Entry.m_Guid); Live >= 0 ) { InsertAt = Live + 1; continue; }
                    Config.m_UpdateOrder.insert( Config.m_UpdateOrder.begin() + InsertAt, Entry );
                    ++InsertAt;
                }
            }
        }

        xtextfile::stream Stream;
        if( auto Err = Stream.Open(false, std::format(L"{}\\SystemOrder.config.txt", ConfigFolder), { xtextfile::file_type::TEXT }); Err )
            return Err;

        xproperty::settings::context Context;
        return xproperty::sprop::serializer::Stream( Stream, Config, Context );
    }

    //---------------------------------------------------------------------------
    // Meant to be called once at startup, AFTER RegisterSystems<...>() has populated
    // m_UpdaterSystems - a missing file (no prior save yet) is NOT an error, registration order
    // simply stands as-is.
    xerr mgr::Load( void ) noexcept
    {
        xtextfile::stream Stream;
        if( auto Err = Stream.Open(true, std::format(L"{}\\Project.config\\SystemOrder.config.txt", m_ProjectPath), { xtextfile::file_type::TEXT }); Err )
            return {};

        system_order_config Config;
        xproperty::settings::context Context;
        if( auto Err = xproperty::sprop::serializer::Stream( Stream, Config, Context ); Err )
            return Err;

        // Apply the saved order front-to-back, then its enabled state - a saved guid with no
        // currently-registered match is silently skipped (system removed from code since last save);
        // anything currently registered but never mentioned in the file just keeps its current
        // (registration-order tail) position, left enabled.
        int TargetIndex = 0;
        for( auto& Entry : Config.m_UpdateOrder )
        {
            const type::guid Guid{ Entry.m_Guid };

            int CurrentIndex = -1;
            for( int k = TargetIndex; k < static_cast<int>(m_UpdaterSystems.size()); ++k )
                if( m_UpdaterSystems[k].first->m_Guid == Guid ) { CurrentIndex = k; break; }
            if( CurrentIndex < 0 ) continue;

            if( CurrentIndex != TargetIndex )
            {
                std::swap( m_UpdaterSystems[TargetIndex],                m_UpdaterSystems[CurrentIndex] );
                std::swap( m_Events.m_OnUpdate.m_Delegates[TargetIndex], m_Events.m_OnUpdate.m_Delegates[CurrentIndex] );
            }
            m_Events.m_OnUpdate.m_Delegates[TargetIndex].m_bEnabled = Entry.m_bEnabled;
            ++TargetIndex;
        }

        // Where each system is placed. The file lists every system of its game, so one it does not mention is new (a script module added it): it waits as an available system until a person
        // places it. (Without a file there is nothing to say where anything goes: the systems that need nothing of their place run at the top level, as they always did.)
        for( auto& S : m_UpdaterSystems )
        {
            bool bListed = false;
            for( auto& Entry : Config.m_UpdateOrder ) if( Entry.m_Guid == S.first->m_Guid.m_Value ) { bListed = true; break; }
            if( !bListed ) S.second->m_bPlaced = false;
        }

        // A system the file places at the top level is placed if that place gives what it needs; a connected one waits (not placed) until its connection can be made. What cannot be
        // placed where the file says (the parent is gone, or does not have the connector any more, or the place does not give what the system needs now) is an available system again.
        std::vector<const system_order_entry*> Pending;
        for( auto& Entry : Config.m_UpdateOrder )
        {
            const type::guid Guid{ Entry.m_Guid };
            auto It = std::find_if( m_UpdaterSystems.begin(), m_UpdaterSystems.end(), [&]( auto& S ){ return S.first->m_Guid == Guid; } );
            if( It == m_UpdaterSystems.end() ) continue;
            It->second->m_bPlaced = false;
            if( !Entry.m_bPlaced ) continue;
            if( Entry.m_ParentGuid == 0 ) { SetUpdateSystemParent( Guid, type::guid{}, -1, false ); continue; }
            Pending.push_back( &Entry );
        }
        for( std::size_t Pass = 0; Pass <= Pending.size(); ++Pass )                 // a parent may itself be waiting for its own connection: the passes go on while one more is made
        {
            bool bProgress = false;
            for( auto It = Pending.begin(); It != Pending.end(); )
            {
                const auto& Entry = **It;
                int ParentConnector = -1;
                for( auto& Other : m_UpdaterSystems )
                    if( Other.first->m_Guid.m_Value == Entry.m_ParentGuid )
                    {
                        for( int c = 0; c < static_cast<int>(Other.second->m_Connectors.size()); ++c )
                            if( Entry.m_Connector == Other.second->m_Connectors[c].m_pName ) { ParentConnector = c; break; }
                        break;
                    }
                if( ParentConnector >= 0 && SetUpdateSystemParent( type::guid{ Entry.m_Guid }, type::guid{ Entry.m_ParentGuid }, ParentConnector, false ) ) { It = Pending.erase(It); bProgress = true; }
                else ++It;
            }
            if( !bProgress ) break;
        }

        return {};
    }

    //---------------------------------------------------------------------------

    bool mgr::CanPlaceUpdateSystem( type::guid Child, type::guid Parent, int ConnectorIndex, std::string* pWhy ) const noexcept
    {
        auto Why = [&]( std::string Text ) noexcept { if( pWhy ) *pWhy = std::move(Text); return false; };

        const instance* pChild = nullptr;
        const instance* pParent = nullptr;
        for( auto& S : m_UpdaterSystems )
        {
            if( S.first->m_Guid == Child )  pChild  = S.second.get();
            if( !Parent.empty() && S.first->m_Guid == Parent ) pParent = S.second.get();
        }
        if( !pChild ) return Why( "there is no such system" );

        std::span<const constraint::info> Given;                    // the top level of the frame gives nothing
        if( !Parent.empty() )
        {
            if( !pParent ) return Why( "there is no such place" );
            if( ConnectorIndex < 0 || ConnectorIndex >= static_cast<int>(pParent->m_Connectors.size()) ) return Why( "the system has no such connector" );
            if( !pParent->m_bPlaced ) return Why( "the system of that connector is not placed" );
            for( const instance* p = pParent; p; p = p->m_pParent )     // a system cannot be its own ancestor
                if( p == pChild ) return Why( "a system cannot be connected under itself" );
            Given = pParent->m_Connectors[ConnectorIndex].m_Provides;
        }

        if( constraint::Solved( pChild->m_Requires, Given ) ) return true;

        std::string Missing;
        for( auto& N : pChild->m_Requires )
        {
            bool bFound = false;
            for( auto& G : Given ) if( G.m_Guid == N.m_Guid ) { bFound = true; break; }
            if( !bFound ) { if( !Missing.empty() ) Missing += ", "; Missing += N.m_pName; }
        }
        return Why( std::format( "this place does not give what the system needs: {}", Missing ) );
    }

    //---------------------------------------------------------------------------

    bool mgr::IsUpdateSystemPlaced( type::guid Guid ) const noexcept
    {
        for( auto& S : m_UpdaterSystems ) if( S.first->m_Guid == Guid ) return S.second->m_bPlaced;
        return false;
    }

    //---------------------------------------------------------------------------

    std::span<const constraint::info> mgr::GetUpdateSystemConstraints( type::guid Guid ) const noexcept
    {
        for( auto& S : m_UpdaterSystems ) if( S.first->m_Guid == Guid ) return S.second->m_Requires;
        return {};
    }

    //---------------------------------------------------------------------------

    void mgr::UnplaceUpdateSystem( type::guid Guid ) noexcept
    {
        for( auto& S : m_UpdaterSystems )
        {
            if( S.first->m_Guid != Guid ) continue;
            S.second->m_bPlaced = false;
            S.second->m_pParent = nullptr; S.second->m_ParentConnector = -1;
            for( auto& Other : m_UpdaterSystems )                       // what is connected under it has nowhere to run: it is not placed either
                if( Other.second->m_pParent == S.second.get() ) UnplaceUpdateSystem( Other.first->m_Guid );
            return;
        }
    }

    //---------------------------------------------------------------------------

    bool mgr::SetUpdateSystemParent( type::guid Child, type::guid Parent, int ConnectorIndex, bool bGoLast ) noexcept
    {
        auto IndexOf = [&]( type::guid Guid ) noexcept
        {
            for( int k = 0; k < static_cast<int>(m_UpdaterSystems.size()); ++k )
                if( m_UpdaterSystems[k].first->m_Guid == Guid ) return k;
            return -1;
        };

        const int ci = IndexOf(Child);
        if( ci < 0 ) return false;
        auto& ChildSystem = *m_UpdaterSystems[ci].second;
        if( !CanPlaceUpdateSystem( Child, Parent, ConnectorIndex ) ) return false;

        if( Parent.empty() )                                            // the top level
        {
            ChildSystem.m_pParent = nullptr; ChildSystem.m_ParentConnector = -1; ChildSystem.m_bPlaced = true;
            if( bGoLast )
            {
                std::rotate( m_UpdaterSystems.begin() + ci,                m_UpdaterSystems.begin() + ci + 1,                m_UpdaterSystems.end() );
                std::rotate( m_Events.m_OnUpdate.m_Delegates.begin() + ci, m_Events.m_OnUpdate.m_Delegates.begin() + ci + 1, m_Events.m_OnUpdate.m_Delegates.end() );
            }
            return true;
        }

        const int pi = IndexOf(Parent);
        if( pi < 0 ) return false;
        auto& ParentSystem = *m_UpdaterSystems[pi].second;
        if( ConnectorIndex < 0 || ConnectorIndex >= static_cast<int>(ParentSystem.m_Connectors.size()) ) return false;

        for( const xecs::system::instance* p = &ParentSystem; p; p = p->m_pParent )     // a system cannot be its own ancestor
            if( p == &ChildSystem ) return false;

        ChildSystem.m_pParent          = &ParentSystem;
        ChildSystem.m_ParentConnector  = ConnectorIndex;
        ChildSystem.m_bPlaced          = true;

        if( bGoLast )                                                   // last among the children of that connector (the end of the list)
        {
            std::rotate( m_UpdaterSystems.begin() + ci,                m_UpdaterSystems.begin() + ci + 1,                m_UpdaterSystems.end() );
            std::rotate( m_Events.m_OnUpdate.m_Delegates.begin() + ci, m_Events.m_OnUpdate.m_Delegates.begin() + ci + 1, m_Events.m_OnUpdate.m_Delegates.end() );
        }
        return true;
    }

    //---------------------------------------------------------------------------

    void mgr::RunChildren( const xecs::system::instance& Parent, int ConnectorIndex ) noexcept
    {
        for( std::size_t i = 0; i < m_UpdaterSystems.size(); ++i )
        {
            const auto& S = *m_UpdaterSystems[i].second;
            if( S.m_pParent != &Parent || S.m_ParentConnector != ConnectorIndex || !S.m_bPlaced ) continue;
            const auto& D = m_Events.m_OnUpdate.m_Delegates[i];
            if( D.m_bEnabled ) D.m_pCallback(D.m_pClass);
        }
    }

    //---------------------------------------------------------------------------

    std::span<const connector> mgr::GetConnectors( type::guid Guid ) const noexcept
    {
        for( auto& S : m_UpdaterSystems )
            if( S.first->m_Guid == Guid ) return S.second->m_Connectors;
        return {};
    }
}

namespace xecs::system
{
    inline void instance::RunConnector( int ConnectorIndex ) noexcept
    {
        m_GameMgr.m_SystemMgr.RunChildren( *this, ConnectorIndex );
    }
}
