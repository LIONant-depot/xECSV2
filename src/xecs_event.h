namespace xecs::event
{
    //
    // TYPE
    //
    namespace type
    {
        using guid = xresource::guid<struct event_tag>;

        struct info
        {
            const type::guid  m_Guid;
            const char* const m_pName;
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

        struct global
        {
            const char* m_pName = "Unnamed Notified Move Out System";
            guid        m_Guid{};
        };
    }

    //
    // OVERRIDES
    //
    struct overrides
    {
        constexpr static auto   typedef_v = type::global{};
    };

    //
    // INSTANCE
    //
    template< typename...T_ARGS >
    struct instance : overrides
    {
        struct info
        {
            using callback = void(void* pPtr, T_ARGS...);

            callback*       m_pCallback;
            void*           m_pClass;
            const char*     m_pName;
            // Skips this one delegate in NotifyAll without removing it - RemoveDelegate is declared
            // below but has no implementation anywhere, so a genuine "system order/enable" feature
            // (see xecs::system::mgr::SetUpdateSystemEnabled) needs a way to disable a delegate that
            // doesn't depend on it. Defaults true so every OTHER existing use of this template (there
            // are several across the engine, all assuming "every registered delegate always fires")
            // is completely unaffected.
            bool            m_bEnabled  = true;
        };

                            instance                ( const instance&
                                                    ) = delete;
                            instance                ( void 
                                                    ) noexcept = default;
        template
        < auto      T_FUNCTION_PTR_V
        , typename  T_CLASS
        > __inline
        void                Register                ( T_CLASS& Class
                                                    ) noexcept;
        constexpr __inline
        void                NotifyAll               ( T_ARGS... Args 
                                                    ) const noexcept;
        template
        < typename T_CLASS
        >
        void                RemoveDelegate          ( T_CLASS& Class
                                                    ) noexcept;
        std::vector<info>   m_Delegates{};
    };
}
