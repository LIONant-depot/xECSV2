#if !(defined(_MSC_VER) && !defined(__clang__))
#include "xecs_msvc_funcsig.h"
#endif
namespace xecs::event
{
    //-------------------------------------------------------------------------------------------

    namespace type::details
    {
        template< typename T_SYSTEM >
        consteval type::info CreateInfo( void ) noexcept
        {
            return type::info
            {
                .m_Guid                 = T_SYSTEM::typedef_v.m_Guid.m_Value
                                             ? T_SYSTEM::typedef_v.m_Guid
#if defined(_MSC_VER) && !defined(__clang__)
                                             : type::guid{ __FUNCSIG__ }
#else
                                             : type::guid{ xecs::details::msvc_funcsig::funcsig<T_SYSTEM, "struct xecs::event::type::info __cdecl xecs::event::type::details::CreateInfo<", ">(void) noexcept">::value.m }   // = MSVC __FUNCSIG__ (saved guids match)
#endif
            ,   .m_pName                = T_SYSTEM::typedef_v.m_pName
//            ,   .m_ID                   = T_SYSTEM::typedef_v.id_v
            };
        }
    }



    //---------------------------------------------------------------------------
    template
    < typename...   T_ARGS
    > template
    < auto          T_FUNCTION_PTR_V
    , typename      T_CLASS
    > 
    void instance<T_ARGS...>::Register( T_CLASS& Class ) noexcept
    {
        m_Delegates.push_back
        (
            info
            {
                .m_pCallback = []( void* pPtr, T_ARGS... Args ) constexpr noexcept
                {
                    std::invoke( T_FUNCTION_PTR_V, reinterpret_cast<T_CLASS*>(pPtr), std::forward<T_ARGS>(Args)... );
                }
            ,  .m_pClass = &Class
            }
        );
    }

    //---------------------------------------------------------------------------
    template
    < typename...T_ARGS
    > constexpr
    void instance<T_ARGS...>::NotifyAll( T_ARGS... Args ) const noexcept
    {
        for (auto& E : m_Delegates)
        {
            if (E.m_bEnabled) E.m_pCallback(E.m_pClass, std::forward<T_ARGS>(Args)...);
        }
    }
}
