namespace xecs::system
{
    //-----------------------------------------------------------------
    // ECS SYSTEM DEFINITIONS
    //-----------------------------------------------------------------
    struct mgr final
    {
        struct events
        {
            xecs::event::instance<>     m_OnGameStart;
            xecs::event::instance<>     m_OnGameEnd;
            xecs::event::instance<>     m_OnUpdate;
            xecs::event::instance<>     m_OnFrameStart;
            xecs::event::instance<>     m_OnFrameEnd;
        };

                                mgr                     ( const mgr&
                                                        ) noexcept = delete;
                                mgr                     ( void
                                                        ) noexcept = default;
        inline                 ~mgr                     ( void
                                                        ) noexcept;
        template
        < typename T_SYSTEM
        > requires
        ( std::derived_from< T_SYSTEM, xecs::system::instance>
        )
        T_SYSTEM&               RegisterSystem          ( xecs::game_mgr::instance& GameMgr
                                                        ) noexcept;
        inline
        void                    Run                     ( void
                                                        ) noexcept;
        template< typename T_SYSTEM >
        T_SYSTEM*               find                    ( void
                                                        ) noexcept;
        inline
        void                    OnNewArchetype          ( xecs::archetype::instance& Archetype
                                                        ) noexcept;

        using system_list = std::vector< std::pair<const xecs::system::type::info*, std::unique_ptr<xecs::system::instance>> >;

        std::unordered_map<type::guid, xecs::system::instance*> m_SystemMaps;
        system_list                                             m_UpdaterSystems;
        system_list                                             m_NotifierSystems;
        events                                                  m_Events;
    };
}