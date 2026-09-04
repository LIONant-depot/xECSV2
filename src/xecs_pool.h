namespace xecs::pool
{
    struct family;

    constexpr static std::uint32_t invalid_delete_global_index_v = 0xffffffffu >> 1;

    namespace type
    {
        using guid = xresource::guid<struct pool_family_guid_tag>;
    }

    //------------------------------------------------------------------------------
    // POOL INSTANCE
    //------------------------------------------------------------------------------
    struct instance final
    {
                        instance                            ( void 
                                                            ) noexcept = default;
                        instance                            ( const instance& 
                                                            ) noexcept = delete;
        inline
                       ~instance                            ( void 
                                                            ) noexcept;
        inline
        void            Initialize                          ( std::span<const component::type::info* const >    DataComponentInfosSpan
                                                            , xecs::pool::family&                               Family
                                                            ) noexcept;
        inline
        void            Clear                               ( void 
                                                            ) noexcept;
        inline
        index           Append                              ( int Count
                                                            ) noexcept;
        inline
        void            Delete                              ( index Index
                                                            ) noexcept;
        inline
        index           MoveInFromPool                      ( index             ToNewIndex
                                                            , index             FromIndexToMove
                                                            , pool::instance&   FromPool
                                                            ) noexcept;
        inline 
        void            CopyEntity                          ( index             ToNewIndex
                                                            , index             FromIndex
                                                            , pool::instance&   FromPool            // Pull must be 100% compatible
                                                            ) noexcept;
        constexpr
        int             Size                                ( void 
                                                            ) const noexcept;
        constexpr
        int             findIndexComponentFromInfo          ( const xecs::component::type::info&
                                                            ) const noexcept;
        constexpr
        int             findIndexComponentFromInfoInSequence( const xecs::component::type::info&
                                                            , int&                        Sequence 
                                                            ) const noexcept;

        inline
        void            UpdateStructuralChanges             ( xecs::component::mgr& ComponentMgr
                                                            ) noexcept;
        constexpr
        int             getFreeSpace                        ( void
                                                            ) const noexcept;
        constexpr
        bool            isLastEntry                         ( index Index
                                                            ) const noexcept;
        template
        < typename T_COMPONENT
        > requires
        ( std::is_same_v<T_COMPONENT, std::decay_t<T_COMPONENT>>
        )
        T_COMPONENT&    getComponent                        ( index EntityIndex 
                                                            ) const noexcept;
        template
        < typename T_COMPONENT
        > requires
        ( std::is_same_v<T_COMPONENT, std::decay_t<T_COMPONENT>>
        )
        T_COMPONENT&    getComponentInSequence              ( index         EntityIndex
                                                            , int&          Sequence
                                                            ) const noexcept;
        inline
        std::byte*      getComponentInSequenceByInfo        ( const xecs::component::type::info&  Info
                                                            , index                               EntityIndex
                                                            , int&                                Sequence
                                                            ) noexcept;    
    private:
        inline
        bool            Free                                ( index Index
                                                            , bool  bCallDestructors
                                                            ) noexcept;
    public:

        using pointer_array = std::array<std::byte*, xecs::settings::max_data_components_per_entity_v>;

        std::span<const component::type::info* const >      m_ComponentInfos            {};
        int                                                 m_CurrentCount              {};
        int                                                 m_Size                      {};
        std::uint32_t                                       m_DeleteGlobalIndex         { invalid_delete_global_index_v };
        std::uint32_t                                       m_DeleteMoveIndex           { invalid_delete_global_index_v };
        pointer_array                                       m_pComponent                {};
        xecs::archetype::instance*                          m_pArchetype                {};
        instance*                                           m_pPendingStructuralChanges { nullptr };
        xecs::pool::family*                                 m_pMyFamily                 { nullptr };
        std::unique_ptr<instance>                           m_Next                      {};
    };

    //------------------------------------------------------------------------------
    // POOL FAMILY INSTANCE
    //------------------------------------------------------------------------------
    struct family
    {
        using guid = xresource::guid<struct family_pool_guid_tag>;

        constexpr
        static guid ComputeGuid(  xecs::archetype::guid                                 Guid
                                , std::span<const xecs::component::type::info* const>   Types
                                , std::span<const std::byte* const>                     Data
                                ) noexcept;
        inline
        void Initialize
        ( guid                                              Guid
        , archetype::instance&                              ArchetypeInstance
        , std::span<xecs::component::entity>                ShareEntityList
        , std::span<xecs::component::type::share::key>      ShareKeyList
        , std::span<const xecs::component::type::info*>     ShareInfoList
        , std::span<const xecs::component::type::info*>     DataInfoList
        ) noexcept;

        template
        < typename T_FUNCTION
        >
        void AppendEntities( int Count, xecs::archetype::mgr& ArchetypeMgr, T_FUNCTION&& Function ) noexcept;

        inline
        void MoveIn( 
              xecs::game_mgr::instance& GameMgr
            , xecs::pool::family&       FromFamily
            , xecs::pool::instance&     FromPool
            , xecs::pool::index         FromIndex ) noexcept;

        struct share_details
        {
            xecs::component::entity             m_Entity;
            xecs::component::type::share::key   m_Key;
        };

        using share_details_array = std::array<share_details, xecs::settings::max_share_components_per_entity_v>;

        guid                                            m_Guid              {};
        instance                                        m_DefaultPool       {};
        std::span<const xecs::component::type::info*>   m_ShareInfos        {};
        std::unique_ptr<xecs::pool::family>             m_Next              {};
        family*                                         m_pPrev             {};
        share_details_array                             m_ShareDetails      {};
        archetype::instance*                            m_pArchetypeInstance{};
    };
}
