namespace xecs::event
{
    //
    // EVENT MGR
    //
    struct mgr
    {
        template
        < typename T_EVENT
        , typename T_CLASS
        >
        void            RegisterClass           ( T_CLASS&& Class ) noexcept;

        template
        < typename T_EVENT
        >
        T_EVENT&        getEvent                ( void ) const noexcept;

        template
        < typename T_EVENT
        >
        void            Register                ( void ) noexcept;

        std::unordered_map<type::guid, overrides*>  m_GlobalEventsMap;
        std::vector<std::unique_ptr<overrides>>     m_GlobalEvents;
    };
}