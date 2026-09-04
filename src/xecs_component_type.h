#ifndef XECS_COMPONENT_TYPE_H
#define XECS_COMPONENT_TYPE_H
#pragma once

namespace xecs::component::type
{
    using guid                  = xresource::guid<struct component_type_tag>;
    using full_serialize_fn     = xerr( xecs::serializer::stream& TextFile, bool isRead, std::byte* pComponentArray, int& Count ) noexcept;
    using report_references_fn  = void( std::vector<xecs::component::entity*>&, std::byte* pComponent ) noexcept;

    // Tells the component type how we should serialize the component
    enum class serialize_mode : std::uint8_t
    { AUTO                                      // (Default) Automatically chooses one option from below. Serializer always has higher priority.
    , BY_SERIALIZER                             // Produces the same output as the Auto by this is explicit 
    , BY_PROPERTIES                             // When a component type have both a serializer function and properties choose properties.
    , DONT_SERIALIZE                            // It may not a serializer but may have properties but yet we do not want to serialize at all.
    };

    // Tells the component type how we should serialize the component
    enum class reference_mode : std::uint8_t
    { AUTO                                      // (Default) Assumes that it has references and it will check the serialize_fn first if null then will try to use properties and check if it has
    , BY_FUNCTION                               // Uses the function to resolve its dependencies
    , BY_PROPERTIES                             // Uses the properties to resolve its dependencies
    , NO_REFERENCES                             // Wont check for anything because it is assumed not to have references
    };

    // The order of this enum is very important as the system relies in this order
    // This is the general shorting order of components Data, then Share components, then Tags
    enum class id : std::uint8_t
    { DATA
    , SHARE
    , TAG
    };

    struct data
    {
        constexpr static auto   max_size_v          = xecs::settings::virtual_page_size_v;
        constexpr static auto   id_v                = id::DATA;

        guid                    m_Guid               {};
        const char*             m_pName              {"Unnamed data component"};
        serialize_mode          m_SerializeMode      { serialize_mode::AUTO };
        reference_mode          m_ReferenceMode      { reference_mode::AUTO };
    };

    struct tag
    {
        constexpr static auto   max_size_v          = 1;
        constexpr static auto   id_v                = id::TAG;
        constexpr static auto   exclusive_v         = false;

        guid                    m_Guid              {};
        const char*             m_pName             { "Unnamed tag component" };
    };

    struct exclusive_tag
    {
        constexpr static auto   max_size_v          = 1;
        constexpr static auto   id_v                = id::TAG;
        constexpr static auto   exclusive_v         = true;

        guid                    m_Guid              {};
        const char*             m_pName             { "Unnamed exclusive tag component" };
    };

    struct share
    {
        constexpr static auto   max_size_v          = xecs::settings::virtual_page_size_v;
        constexpr static auto   id_v                = id::SHARE;

        struct key
        {
            std::uint64_t       m_Value;
            friend auto operator <=> ( const key&, const key& ) = default;
        };
        using compute_key_fn = key(const std::byte*) noexcept;

        guid                    m_Guid               {};
        const char*             m_pName              { "Unnamed share component" };
        bool                    m_bGlobalScoped      { true };                           // TODO: To be deleted! Global Scoped vs Archetype Scoped. If you want a per-family (Such every family has a bbox)? This is a TODO for the future.
        //bool                  m_bDeleteOnZeroRef   { true };                           // TODO: Potentially add this feature.
        bool                    m_bBuildFilter       { false };                          // Tells xECS to automatically create a reference to all its references "a filter". So if we want to find all entities that have a share of a particular value we can do it quickly.
        serialize_mode          m_SerializeMode      { serialize_mode::AUTO };
        reference_mode          m_ReferenceMode      { reference_mode::AUTO };
    };

    namespace details
    {
        template< typename T_COMPONENT >
        struct is_valid
        {
            template<auto U>     struct Check;
            template<typename>   static std::false_type Test(...);
            template<typename C> static auto Test(Check<&C::typedef_v>*) -> std::conditional_t
            <
                   ( std::is_same_v< const data,           decltype(C::typedef_v) > && sizeof(T_COMPONENT) <= data::max_size_v   )
                || ( std::is_same_v< const tag,            decltype(C::typedef_v) > && sizeof(T_COMPONENT) <= tag::max_size_v    )
                || ( std::is_same_v< const exclusive_tag,  decltype(C::typedef_v) > && sizeof(T_COMPONENT) <= exclusive_tag::max_size_v )
                || ( std::is_same_v< const share,          decltype(C::typedef_v) > && sizeof(T_COMPONENT) <= share::max_size_v  )
            ,   std::true_type
            ,   std::false_type
            >;
            constexpr static auto value = decltype(Test<T_COMPONENT>(nullptr))::value;
        };
    }
    template< typename T_COMPONENT >
    constexpr bool is_valid_v = details::is_valid<xecs::types::decay_full_t<T_COMPONENT>>::value;

    //
    // TYPE INFO
    //
    struct info final
    {
        constexpr static auto invalid_bit_id_v = 0xffff;

        using construct_fn      = void(std::byte*) noexcept;
        using destruct_fn       = void(std::byte*) noexcept;
        using move_fn           = void(std::byte* Dst, std::byte* Src ) noexcept;
        using copy_fn           = void(std::byte* Dst, const std::byte* Src ) noexcept;
        using compute_key_fn    = share::compute_key_fn;

        const type::guid            m_Guid;                 // Unique Identifier for the component type
        mutable std::uint16_t       m_BitID;                // Which bit was allocated for this type at run time
        const std::uint16_t         m_Size;                 // Size of the component in bytes
        const type::id              m_TypeID;               // Simple enumeration that tells what type of component is this
        const bool                  m_bGlobalScoped:1       // If the component is a share, it indicates if it should be factor to a globally scope or to an archetype scope
        ,                           m_bBuildShareFilter:1   // If the component is a share, it indicates if the query can filter by its key
        ,                           m_bExclusiveTag:1;      // If the component is a tag, is it a exclusive tag
        construct_fn* const         m_pConstructFn;         // Constructor function pointer if required
        destruct_fn* const          m_pDestructFn;          // Destructor function pointer if required
        move_fn* const              m_pMoveFn;              // Move function pointer if required
        copy_fn* const              m_pCopyFn;              // Copy function for the component
        compute_key_fn* const       m_pComputeKeyFn;        // Computes the key from a share component
        full_serialize_fn* const    m_pSerilizeFn;          // This is the serialize function
        report_references_fn* const m_pReportReferencesFn;  // This is a callback to report references for components that have references to other entities
        const xproperty::type::object* m_pPropertyTable;    // Properties for the component
        mutable type::share::key    m_DefaultShareKey;      // Default value for this share component
        serialize_mode              m_SerializeMode;        // Tells the component how it should serialize itself
        reference_mode              m_ReferenceMode;        // Tells if the component has references and if so how to resolve them
        const char* const           m_pName;                // Friendly Human readable string name for the component type
    };

    namespace details
    {
        template< typename T >
        consteval info CreateInfo(void) noexcept;

        template< typename T >
        struct info_var
        {
            inline static constexpr auto value = CreateInfo<T>();
        };
    }

    template< typename T_COMPONENT >
    requires( type::is_valid_v<xecs::types::decay_full_t<T_COMPONENT>> )
    constexpr auto& info_v = details::info_var<xecs::types::decay_full_t<T_COMPONENT>>::value;
}

template<>
struct std::hash< xecs::component::type::share::key >
{
    auto operator()(const typename xecs::component::type::share::key obj) const { return hash<std::uint64_t>()(obj.m_Value); }
};

#endif