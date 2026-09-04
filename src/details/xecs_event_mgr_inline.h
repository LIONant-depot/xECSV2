namespace xecs::event
{
    //----------------------------------------------------------------------------------------

    template< typename T_EVENT, typename T_CLASS >
    void mgr::RegisterClass( T_CLASS&& Class ) noexcept
    {
        auto It = m_GlobalEventsMap.find( type::info_v<T_EVENT>.m_Guid );
        assert( It != m_GlobalEventsMap.end() );
        reinterpret_cast<T_EVENT*>(It->second)->Register<&T_CLASS::OnEvent>(Class);
    }

    //----------------------------------------------------------------------------------------

    template< typename T_EVENT >
    T_EVENT& mgr::getEvent( void ) const noexcept
    {
        auto It = m_GlobalEventsMap.find(type::info_v<T_EVENT>.m_Guid);
        assert(It != m_GlobalEventsMap.end());
        return *reinterpret_cast<T_EVENT*>(It->second);
    }

    //----------------------------------------------------------------------------------------

    template< typename T_EVENT >
    void mgr::Register( void ) noexcept
    {
        m_GlobalEvents.push_back( std::make_unique<T_EVENT>() );
        m_GlobalEventsMap.emplace( type::info_v<T_EVENT>.m_Guid, m_GlobalEvents.back().get() );
    }
}