namespace xecs::system
{
    // One row of the persisted, on-disk Update-system execution order/enabled config - mirrors
    // e10::library_mgr's own "Library.config.txt" convention exactly (plain xtextfile +
    // xproperty::sprop::serializer::Stream against an ordinary XPROPERTY_DEF'd struct, no guid/
    // descriptor/factory or resource-pipeline/asset-browser visibility at all). Vector order IS the
    // execution order - no separate index field needed. m_Guid is the identity used to match a saved
    // entry back to a currently-registered system (type::info_v<T_SYSTEM>.m_Guid); m_Name is a
    // comment/debug-only echo for a human reading the file, never read back as identity (same
    // "read-only echo" role xecs::editor::prefab_property_override::m_PropertyValueAsString plays).
    struct system_order_entry
    {
        std::uint64_t   m_Guid      {};
        std::string     m_Name;
        bool            m_bEnabled  = true;

        XPROPERTY_DEF
        ( "SystemOrderEntry", system_order_entry
        , obj_member<"Guid",    &system_order_entry::m_Guid>
        , obj_member<"Name",    &system_order_entry::m_Name>
        , obj_member<"Enabled", &system_order_entry::m_bEnabled>
        )
    };
    XPROPERTY_REG(system_order_entry)

    struct system_order_config
    {
        std::vector<system_order_entry> m_UpdateOrder;

        XPROPERTY_DEF
        ( "SystemOrderConfig", system_order_config
        , obj_member<"UpdateOrder", &system_order_config::m_UpdateOrder>
        )
    };
    XPROPERTY_REG(system_order_config)

    // One row of the LIVE (in-memory) Update-system list - what the "System Registry" UI actually
    // enumerates/edits every frame (xecs::system::type::info is an all-const, consteval-computed
    // singleton SHARED by every instance of a given system type, so per-registration state like
    // "enabled" and "current position" can never live there - it lives in mgr's own bookkeeping,
    // reflected out through this plain, non-owning view struct instead).
    struct update_system_row
    {
        type::guid  m_Guid;
        const char* m_pName;
        bool        m_bEnabled;
    };

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

        //-------------------------------------------------------------
        // Data-driven Update-system order/enable - see xecs_prefab_mgr.h-style project-relative
        // persistence convention. All five below only ever touch m_UpdaterSystems and its
        // index-aligned counterpart m_Events.m_OnUpdate.m_Delegates (both always the exact same size,
        // grown 1:1 by RegisterSystem for every Update system) - Notifier systems are untouched
        // (order/enable only ever applied to the "important pipeline order" case the user actually
        // asked for, not to structural-change reactions).
        //-------------------------------------------------------------
        inline
        std::vector<update_system_row>
                                GetUpdateSystemRows     ( void
                                                        ) const noexcept;
        inline
        void                    MoveUpdateSystem        ( type::guid Guid
                                                        , int         Delta
                                                        ) noexcept;
        inline
        void                    SetUpdateSystemEnabled  ( type::guid Guid
                                                        , bool        bEnabled
                                                        ) noexcept;
        // Called by game_mgr::instance::Run() on the Stopped->Running transition / Stop() on the
        // reverse - see their own comments. While stopped there is no snapshot, so every
        // Move/SetEnabled call directly IS the authored state (nothing else to keep in sync).
        inline
        void                    SnapshotForPlay         ( void
                                                        ) noexcept;
        inline
        void                    RestoreFromSnapshot     ( void
                                                        ) noexcept;

        // {m_ProjectPath}\Project.config\SystemOrder.config.txt - same fixed, non-asset settings-file
        // convention e10::library_mgr already uses for Library.config.txt. Load() is meant to be
        // called once at startup, AFTER RegisterSystems<...>() has populated m_UpdaterSystems (a saved
        // guid with no currently-registered match is silently skipped; a currently-registered system
        // absent from the file keeps its registration-order tail position, enabled).
        inline
        xerr                    Save                    ( void
                                                        ) const noexcept;
        inline
        xerr                    Load                    ( void
                                                        ) noexcept;

        using system_list = std::vector< std::pair<const xecs::system::type::info*, std::unique_ptr<xecs::system::instance>> >;

        std::unordered_map<type::guid, xecs::system::instance*> m_SystemMaps;
        system_list                                             m_UpdaterSystems;
        system_list                                             m_NotifierSystems;
        events                                                  m_Events;
        std::wstring                                            m_ProjectPath;
        std::vector<update_system_row>                          m_PreRunSnapshot;
    };
}