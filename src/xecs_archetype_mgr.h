namespace xecs::archetype
{
    //
    // ARCHETYPE MGR
    //
    struct mgr
    {
        struct events
        {
            xecs::event::instance<xecs::archetype::instance&>         m_OnNewArchetype;
        };

        inline                                  mgr                         ( xecs::game_mgr::instance& GameMgr 
                                                                            ) noexcept;
        inline
        archetype::instance&                    getOrCreateArchetype        ( const xecs::tools::bits& ComponentBits
                                                                            ) noexcept;
        inline
        void                                    UpdateStructuralChanges     ( void 
                                                                            ) noexcept;
        inline
        void                                    AddToStructuralPendingList  ( instance& Archetype
                                                                            ) noexcept;
        inline
        void                                    AddToStructuralPendingList  ( pool::instance& Pool
                                                                            ) noexcept;
        __inline
        archetype::instance*                    findArchetype               ( xecs::archetype::guid Guid 
                                                                            ) noexcept;
        template
        < typename T_FUNCTION = xecs::tools::empty_lambda
        > requires
        ( xecs::function::is_callable_v<T_FUNCTION>
        ) [[nodiscard]] xecs::component::entity
                                                AddOrRemoveComponents       ( xecs::component::entity       Entity
                                                                            , const xecs::tools::bits&      Add
                                                                            , const xecs::tools::bits&      Sub
                                                                            , T_FUNCTION&&                  Function = xecs::tools::empty_lambda{}
                                                                            ) noexcept;
        inline
        archetype::instance&                    CreateArchetype             ( archetype::guid      Guid
                                                                            , const tools::bits&   Bits
                                                                            ) noexcept;

        // Pool family is all the share components of a certain type with a certain value plus the archetype guid
        using pool_family_map               = std::unordered_map<xecs::pool::family::guid,          xecs::pool::family*         >;
        using archetype_map                 = std::unordered_map<xecs::archetype::guid,             xecs::archetype::instance*  >;
        using share_component_entity_map    = std::unordered_map<xecs::component::type::share::key, xecs::component::entity     >;

        static constexpr auto end_structural_changes_v = ~static_cast<std::size_t>(0);

        xecs::game_mgr::instance&                           m_GameMgr;
        events                                              m_Events                    {};
        share_component_entity_map                          m_ShareComponentEntityMap   {};
        archetype_map                                       m_ArchetypeMap              {};
        std::vector<std::unique_ptr<archetype::instance>>   m_lArchetype                {};
        std::vector<std::pair<tools::bits, tools::bits>>    m_lArchetypeBits            {};
        pool_family_map                                     m_PoolFamily                {};
        instance*                                           m_pArchetypeStrututalPending{ reinterpret_cast<instance*>(end_structural_changes_v) };
        pool::instance*                                     m_pPoolStructuralPending    { reinterpret_cast<pool::instance*>(end_structural_changes_v) };
    };
}