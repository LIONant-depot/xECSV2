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
        // m_BitID/m_DefaultShareKey are runtime-assigned (by component::mgr::RegisterComponent,
        // written back here) onto what's otherwise a compile-time singleton (info_v<T>, one per
        // linked binary/module - see its own declaration comment below). Safe as-is, PROVIDED only
        // one physical binary ever names/registers this T: true for every game/consumer-defined
        // component type (the editor only ever touches an unknown type generically, through this
        // same `info*`, never by independently instantiating info_v<ThatType> itself), and true for
        // today's single-binary (static/included-source) build regardless of type. The one case
        // that needs care is xECSV2's OWN built-in types that multiple modules must reference by
        // name (entity, parent, children, share_as_data_exclusive_tag, ref_count, share_filter,
        // prefab::tag, prefab::root, editor::prefab_instance, component::entity_reference) once
        // xECSV2 is ALSO built as a shared library AND a second module (Game.dll, Phase 8) also
        // independently names one of these types.
        //
        // Phase 7 attempted the obvious fix - explicit instantiation of info_var<T> for these 10,
        // decorated dllexport/dllimport - and hit a real, confirmed MSVC limitation: `value` is a
        // C++17 `inline static constexpr` member, and MSVC's "inline variable" vague-linkage model
        // for such members does not participate in a class template's explicit-instantiation
        // dllexport/dllimport mechanism the way an ordinary (non-inline) static data member, or an
        // ordinary member FUNCTION on the very same template (confirmed via its own operator=),
        // does - confirmed with an isolated minimal repro outside xECS's own headers entirely, and
        // reproduced again after replacing `value` with a plain (non-inline, non-constexpr) `static
        // const info` via full template specialization: `info_var<T>::value` itself then correctly
        // exports/imports (verified via `dumpbin /exports` and object-file symbol inspection - the
        // reference DOES route through the `__imp_` thunk when accessed directly), but info_v<T>
        // itself (`constexpr auto& info_v = details::info_var<T>::value;`, declared just below) is
        // now ALSO its own separate inline variable, and reading THROUGH it - even a plain
        // `info_v<T>.m_Guid`, no address-of needed - was independently confirmed (same repro
        // methodology) to still silently produce a plain, non-imported reference regardless of
        // `value`'s own correct export/import decoration. Attempting to fix info_v<T> itself the
        // same way (a full specialization, non-constexpr, `extern`-declared) hit a genuine C++
        // language-rule wall: reference variables cannot be `extern`-declared-then-defined through
        // explicit template specialization the way ordinary objects can (MSVC C2530/C2766).
        //
        // Deferred to Phase 8, deliberately, rather than solved here: there is no second binary yet
        // (Game.dll) that actually needs to independently name one of these 10 types, so nothing in
        // today's single-consumer (xGPU_unit_test only) shared-library configuration is actually
        // broken by leaving these 10 on the ordinary, single-binary-safe primary template just like
        // every game-defined type - each binary that touches them gets its own self-consistent
        // local copy, exactly like today's default build. Phase 8, once it has an actual second
        // module to link and test against, should solve this with a real accessor-FUNCTION-based
        // indirection (confirmed working for ordinary member functions above) rather than trying to
        // export the raw data member/reference directly - i.e. route info_v<T> for these 10 through
        // a properly dllexport/dllimport'd function call instead of a directly-bound reference
        // variable, and validate the fix against Game.dll rather than a synthetic repro. Deliberately
        // NOT routed through a runtime registry lookup for every access either way - that would cost
        // real hot-path performance for a safety property this direct, mutable field already has
        // under the stated invariant once Phase 8 actually closes this gap.
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
        mutable type::share::key    m_DefaultShareKey;      // Default value for this share component - same cross-module invariant as m_BitID above
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

    // One compile-time singleton per (T_COMPONENT, linked binary/module) - `info_var<T>::value` is a
    // genuine C++17 `inline static` CLASS member, so it's already correctly ODR-merged across every
    // translation unit WITHIN one program (confirmed: xecs.cpp and E29's own .cpp are separate TUs
    // in the same xGPU_unit_test.exe and have shared this state correctly all session - this is NOT
    // the same bug class as xecs_scene_descriptor.h's own g_Factory, which was a namespace-scope
    // static). It only becomes "one per module" - i.e. a second, independent copy - the moment a
    // SECOND, separately linked binary (a DLL) also instantiates info_v<T_COMPONENT> for the same T.
    // See info::m_BitID's own comment for exactly when that matters and what to do about it.
    template< typename T_COMPONENT >
    requires( type::is_valid_v<xecs::types::decay_full_t<T_COMPONENT>> )
    constexpr auto& info_v = details::info_var<xecs::types::decay_full_t<T_COMPONENT>>::value;

    // GUID-by-value identity check for a runtime-discovered `info*` against a specific, compile-
    // time-known component type T - the safe replacement for `pInfo == &info_v<T>` (or `!=`), which
    // silently assumes there is exactly one `info_v<T>` address per type. That's true only within a
    // single binary - an empirical, built-and-run test (see the xECSV2 DLL-boundary audit) proved
    // two binaries that both reference the same T get two different `info_v<T>` addresses, so any
    // comparison keyed on the address (rather than the type's stable m_Guid) silently breaks the
    // moment a second binary is involved. `pInfo == nullptr` is deliberately never a match (matches
    // every prior call site's own implicit assumption that pInfo is always a valid component of
    // SOME type - this only ever answers "is it type T", not "is it null").
    template< typename T_COMPONENT >
    requires( type::is_valid_v<xecs::types::decay_full_t<T_COMPONENT>> )
    constexpr bool IsComponentType( const info* pInfo ) noexcept
    {
        return pInfo != nullptr && pInfo->m_Guid == info_v<T_COMPONENT>.m_Guid;
    }
}

template<>
struct std::hash< xecs::component::type::share::key >
{
    auto operator()(const typename xecs::component::type::share::key obj) const { return hash<std::uint64_t>()(obj.m_Value); }
};

#endif