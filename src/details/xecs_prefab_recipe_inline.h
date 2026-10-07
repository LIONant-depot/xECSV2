// A prefab instance is a recipe (documentation/Editors/prefabs_plan.md, phase 3).
//
// A scene stores an instance as ONE entity: the one that carries xecs::editor::prefab_instance (which prefab, and what this instance does
// differently). Its members are not stored: they are spawned from the prefab when the scene loads, and each gets the id DeriveMemberId gives
// for the instance's id and the member's address (xecs::editor::member_address: the member's permanent id inside its prefab, one per instance
// boundary crossed). So a prefab change reaches every instance at the next load, the scene's files do not change when the prefab does, and a
// member keeps its id (references, selection, undo) whatever happens to the prefab's other members.
//
// This file holds what turns a prefab (and its nested prefabs) into the list of entities an instance is made of (the plan), stages those
// entities for a scene load, refreshes a recipe from a live instance before it is saved, carries an instance's overrides into its prefab
// (Apply), and converts the instances saved before phase 3 (every member an entity file of the scene, overrides addressed by child-index
// paths). The scene's own load and save call it (xecs_scene_inline.h); the editor reaches it through xlioncore::xECSEditor.
#include <optional>
#include <unordered_set>

namespace xecs::prefab::recipe
{
    using address = xecs::editor::member_address;

    //-----------------------------------------------------------------------------------------
    // The plan of a prefab: every entity a spawn makes, parents before children, and the prefabs (levels) they come from.
    //-----------------------------------------------------------------------------------------
    struct node
    {
        address                                         m_Address;
        xecs::component::entity                         m_Template;         // the template entity it starts from
        int                                             m_Parent = -1;      // index of its parent in plan::m_Nodes (-1: the root)
        int                                             m_Level  = 0;       // the level whose template m_Template is a member of
        std::vector<int>                                m_Children;
        std::vector<const xecs::component::type::info*> m_Infos;            // what it is made of: its template's, with the nested recipes' component diffs; no entity/parent/children/prefab tags
        bool                                            m_bHasChildren = false;   // its template has a children component (it keeps one even when its instance removed every child)
    };

    // One prefab of the plan: the instance's own (level 0), and one for each nested instance, whose recipe applies to the members under it.
    struct level
    {
        xecs::prefab::guid                  m_Prefab{};
        address                             m_Prefix;                       // the address of this prefab's root in the plan
        const group_bookkeeping*            m_pGroup        = nullptr;      // the template's ids and names
        xecs::component::entity             m_TemplateRoot{};
        int                                 m_Owner         = -1;           // the node that is this nested instance's root (-1: level 0)
        int                                 m_OwnerLevel    = -1;           // the level that node belongs to (what the recipe's references are written against)
        xecs::editor::prefab_instance       m_Recipe;
    };

    struct plan
    {
        std::vector<node>   m_Nodes;
        std::vector<level>  m_Levels;

        int Find( std::span<const std::uint64_t> Address ) const noexcept
        {
            for( int i = 0, n = static_cast<int>(m_Nodes.size()); i < n; ++i )
                if( std::ranges::equal(m_Nodes[i].m_Address, Address) ) return i;
            return -1;
        }
    };
}

namespace xecs::prefab
{
    //-----------------------------------------------------------------------------------------
    // The baked plan of a prefab (documentation/Editors/prefabs_plan.md 3.5, phase 4): what every spawn of it makes, worked out once (mgr::getBaked,
    // at the end of this file). The nested prefabs are expanded and their recipes applied here, never per spawn; each member's data is an inert
    // entity (prefab::tag: no system sees it, no builder runs on it) a spawn copies; where each member's references are, and which member each
    // names, is a table. A spawn (mgr::Spawn) and a scene load (recipe::StageMembers) both start from it.
    //-----------------------------------------------------------------------------------------
    struct baked
    {
        struct reference                                // a reference in a component's own bytes, and the node it names (-1: null)
        {
            const xecs::component::type::info*  m_pInfo     = nullptr;
            std::uint32_t                       m_Offset    = 0;
            int                                 m_Target    = -1;
        };
        struct indirect                                 // the references of a component that are not in its bytes (in a container it holds), in their order
        {
            const xecs::component::type::info*  m_pInfo     = nullptr;
            std::vector<int>                    m_Targets;
        };
        struct node
        {
            xecs::component::entity                                     m_Entity;       // the member's data, nested recipes applied, references null
            std::vector<const xecs::component::type::info*>             m_Infos;        // what a member is made of: the plan's, and parent / children when it has them
            std::vector<reference>                                      m_References;
            std::vector<indirect>                                       m_Indirect;
            std::vector<int>                                            m_Columns;      // Spawn's scratch: where parent, children and each reference are in the pool
            std::unique_ptr<xecs::persist::details::staged_components>  m_pScratch;     // Spawn's scratch: the staging of a member builder systems build
        };

        recipe::plan                            m_Plan;
        std::vector<node>                       m_Nodes;        // one per node of the plan
        std::vector<xecs::component::entity>    m_Spawned;      // what the last Spawn made, node-major
    };
}

namespace xecs::prefab::recipe
{
    //-----------------------------------------------------------------------------------------
    // Small helpers
    //-----------------------------------------------------------------------------------------
    namespace details
    {
        inline bool IsStructural( const xecs::component::type::info* p ) noexcept
        {
            using xecs::component::type::IsComponentType;
            return IsComponentType<xecs::component::entity>(p)
                || IsComponentType<xecs::component::parent>(p)
                || IsComponentType<xecs::component::children>(p)
                || IsComponentType<xecs::prefab::tag>(p)
                || IsComponentType<xecs::prefab::root>(p);
        }

        // An entity's components: data, share and the tags that are saved.
        inline std::vector<const xecs::component::type::info*> GatherInfos( xecs::game_mgr::instance& GameMgr, xecs::component::entity E ) noexcept
        {
            std::vector<const xecs::component::type::info*> Out;
            auto& D = GameMgr.m_ComponentMgr.getEntityDetails(E);
            if( D.m_pPool == nullptr ) return Out;
            auto& A = *D.m_pPool->m_pArchetype;
            for( auto p : A.getDataComponentInfos()  ) Out.push_back(p);
            for( auto p : A.getShareComponentInfos() ) Out.push_back(p);
            xecs::persist::details::AppendPersistentTagInfos( A, Out );
            return Out;
        }

        inline bool Has( const std::vector<const xecs::component::type::info*>& Infos, std::uint64_t Guid ) noexcept
        {
            return std::any_of( Infos.begin(), Infos.end(), [&]( auto p ) noexcept { return p->m_Guid.m_Value == Guid; } );
        }

        inline bool HasComponent( xecs::game_mgr::instance& GameMgr, xecs::component::entity E, const xecs::component::type::info& Info ) noexcept
        {
            if( false == GameMgr.m_ComponentMgr.isEntityValid(E) ) return false;
            auto& D = GameMgr.m_ComponentMgr.getEntityDetails(E);
            return D.m_pPool && D.m_pPool->findIndexComponentFromInfo(Info) >= 0;
        }

        template< typename T >
        inline T* LiveComponent( xecs::game_mgr::instance& GameMgr, xecs::component::entity E ) noexcept
        {
            if( false == E.isValid() || false == GameMgr.m_ComponentMgr.isEntityValid(E) ) return nullptr;
            auto& D = GameMgr.m_ComponentMgr.getEntityDetails(E);
            if( D.m_pPool == nullptr ) return nullptr;
            const auto i = D.m_pPool->findIndexComponentFromInfo( xecs::component::type::info_v<T> );
            if( i < 0 ) return nullptr;
            return reinterpret_cast<T*>( &D.m_pPool->m_pComponent[i][ D.m_PoolIndex.m_Value * sizeof(T) ] );
        }

        inline std::vector<xecs::component::entity> ChildrenOf( xecs::game_mgr::instance& GameMgr, xecs::component::entity E ) noexcept
        {
            if( auto* p = LiveComponent<xecs::component::children>(GameMgr, E) ) return p->m_List;
            return {};
        }

        inline bool StartsWith( std::span<const std::uint64_t> A, std::span<const std::uint64_t> Prefix ) noexcept
        {
            return A.size() >= Prefix.size() && std::equal( Prefix.begin(), Prefix.end(), A.begin() );
        }

        inline address Join( const address& Prefix, std::uint64_t Id ) noexcept
        {
            address A = Prefix;
            A.push_back(Id);
            return A;
        }

        inline bool IsRemovedBy( const xecs::editor::prefab_instance& Recipe, std::span<const std::uint64_t> Relative ) noexcept
        {
            for( auto& D : Recipe.m_HierarchyDiffs )
                if( false == D.m_bAdded && std::ranges::equal(D.m_Member, Relative) ) return true;
            return false;
        }

        // Every entity reference of one component's data (parent and children excluded: a caller sets those itself).
        template< typename T_FN >   // void(xecs::component::entity&)
        inline void ForEachReference( const xecs::component::type::info& Info, std::byte* pData, T_FN&& Fn ) noexcept
        {
            if( Info.m_ReferenceMode == xecs::component::type::reference_mode::NO_REFERENCES ) return;
            if( xecs::component::type::IsComponentType<xecs::component::parent>(&Info) || xecs::component::type::IsComponentType<xecs::component::children>(&Info) ) return;

            if( Info.m_ReferenceMode == xecs::component::type::reference_mode::BY_FUNCTION )
            {
                std::vector<xecs::component::entity*> References;
                Info.m_pReportReferencesFn( References, pData );
                for( auto pRef : References ) Fn( *pRef );
                return;
            }
            if( Info.m_pPropertyTable == nullptr ) return;
            xproperty::settings::context Context{};
            std::string                  SetError;
            xproperty::sprop::collector( pData, *Info.m_pPropertyTable, Context, [&]( const char* pPropertyName, xproperty::any&& Data, const xproperty::type::members&, bool, const void* ) noexcept
            {
                if( Data.getTypeGuid() != xproperty::settings::var_type<xecs::component::entity>::guid_v ) return;
                auto E = Data.get<xecs::component::entity>();
                Fn( E );
                Data.get<xecs::component::entity>() = E;
                xproperty::sprop::setProperty( SetError, pData, *Info.m_pPropertyTable, xproperty::sprop::container::prop{ pPropertyName, Data }, Context );
            });
        }

        // A property's value as the text an override keeps. False for a type that has no text form (a compound leaf...). An entity is not
        // handled here: its text is what the scene writes for it (EncodeEntity).
        inline bool ValueText( const xproperty::any& Value, std::string& Out ) noexcept
        {
            std::array<char, 256> Buffer{};
            int Len = 0;
            if( Value.isEnum() ) Len = xecs::persist::details::EnumAnyToString( Buffer, Value );
            else
            {
                using namespace xproperty::settings;
                const auto G = Value.getTypeGuid();
                const bool bSafe = G == var_type<std::int32_t>::guid_v  || G == var_type<std::uint32_t>::guid_v || G == var_type<std::int16_t>::guid_v
                                || G == var_type<std::uint16_t>::guid_v || G == var_type<std::int8_t>::guid_v   || G == var_type<std::uint8_t>::guid_v
                                || G == var_type<float>::guid_v         || G == var_type<double>::guid_v        || G == var_type<std::string>::guid_v
                                || G == var_type<std::wstring>::guid_v  || G == var_type<std::uint64_t>::guid_v || G == var_type<std::int64_t>::guid_v
                                || G == var_type<bool>::guid_v          || G == var_type<xresource::full_guid>::guid_v;
                if( false == bSafe ) return false;
                Len = AnyToString( Buffer, Value );
            }
            Out.assign( Buffer.data(), Len > 0 ? static_cast<std::size_t>(Len) : 0 );
            return true;
        }

        inline bool IsEntityValue( const xproperty::any& Value ) noexcept
        {
            return Value.getTypeGuid() == xproperty::settings::var_type<xecs::component::entity>::guid_v;
        }

        // An entity override's text: "#<n>", n the reference as the file it belongs to encodes it (a scene: a permanent id, or -(external index)-1;
        // a prefab: a member's id). "invalid" (what the editor shows for a null reference) and "#0" are null. Anything else is not known.
        inline std::optional<std::int64_t> DecodeEntityText( const std::string& Text ) noexcept
        {
            if( Text == "invalid" ) return 0;
            if( Text.size() > 1 && Text[0] == '#' ) return static_cast<std::int64_t>( std::strtoll( Text.c_str() + 1, nullptr, 10 ) );
            return std::nullopt;
        }

        inline std::string EncodeEntityText( std::int64_t Encoded ) noexcept { return std::format( "#{}", Encoded ); }

        // Writes one override onto a component's data. Entity values are given to DecodeEntity (text -> the reference to write), and skipped when it
        // gives nothing. Returns false when the property is not one of the component's.
        template< typename T_DECODE >   // std::optional<xecs::component::entity>(const std::string&)
        inline void ApplyOneComponent( const xecs::editor::prefab_component_override& Entry, const xecs::component::type::info& Info, std::byte* pData, T_DECODE&& DecodeEntity ) noexcept
        {
            for( auto& Prop : Entry.m_PropertyOverrides )
            {
                xproperty::settings::context Context{};
                xproperty::any Current;
                bool bFound = false;
                xproperty::sprop::collector( pData, *Info.m_pPropertyTable, Context, [&]( const char* pName, xproperty::any&& V, const xproperty::type::members&, bool, const void* ) noexcept
                {
                    if( Prop.m_PropertyName == pName ) { Current = std::move(V); bFound = true; }
                });
                if( !bFound || !Current.hasValue() ) continue;

                xproperty::any Parsed;
                if( IsEntityValue(Current) )
                {
                    auto E = DecodeEntity( Prop.m_PropertyValueAsString );
                    if( false == E.has_value() ) continue;
                    Parsed.set<xecs::component::entity>( *E );
                }
                else if( Current.isEnum() )
                {
                    Parsed = std::move(Current);
                    xecs::persist::details::SetEnumAnyFromString( Parsed, Prop.m_PropertyValueAsString );
                }
                else
                {
                    std::string Buffer = Prop.m_PropertyValueAsString;
                    if( xproperty::settings::StringToAny( Parsed, Current.getTypeGuid(), std::span<char>(Buffer.data(), Buffer.size()) ) == false ) continue;
                }
                std::string SetError;
                xproperty::sprop::setProperty( SetError, pData, *Info.m_pPropertyTable, xproperty::sprop::container::prop{ Prop.m_PropertyName, Parsed }, Context );
            }
        }

        // Before phase 3 an override addressed a member by child indices from the entity that carries the prefab_instance (stopping at the nearest
        // nested instance): turned into an address by walking those indices in the template of the recipe's prefab. One that leads nowhere is dropped.
        inline void ConvertPaths( xecs::game_mgr::instance& GameMgr, xecs::editor::prefab_instance& Recipe, xecs::component::entity TemplateRoot, const group_bookkeeping& Group ) noexcept
        {
            const auto Resolve = [&]( const std::vector<std::uint32_t>& Path, address& Out ) noexcept -> bool
            {
                Out.clear();
                if( Path.empty() ) return true;
                auto Cur = TemplateRoot;
                for( auto Index : Path )
                {
                    auto Kids = ChildrenOf( GameMgr, Cur );
                    if( Index >= Kids.size() ) return false;
                    Cur = Kids[Index];
                }
                auto It = Group.m_RuntimeToLocal.find(Cur.m_Value);
                if( It == Group.m_RuntimeToLocal.end() ) return false;
                Out.push_back(It->second);
                return true;
            };

            std::erase_if( Recipe.m_lComponents, [&]( auto& C ) noexcept
            {
                if( C.m_MemberPath.empty() ) return false;
                const bool bOk = Resolve( C.m_MemberPath, C.m_Member );
                C.m_MemberPath.clear();
                return !bOk;
            });
            std::erase_if( Recipe.m_HierarchyDiffs, [&]( auto& D ) noexcept
            {
                if( D.m_bAdded ) return true;                       // recomputed from the live instance; an old "added" path named an entity of the scene
                const bool bOk = Resolve( D.m_MemberPath, D.m_Member );
                D.m_MemberPath.clear();
                return !bOk || D.m_Member.empty();
            });
            Recipe.m_Format = xecs::editor::prefab_instance::recipe_format_v;
        }
    }

    //-----------------------------------------------------------------------------------------
    // MakePlan
    //-----------------------------------------------------------------------------------------
    namespace details
    {
        struct plan_builder
        {
            xecs::game_mgr::instance&   m_GameMgr;
            plan&                       m_Plan;
            xerr                        m_Error{};
            std::vector<std::uint64_t>  m_Enclosing;        // the prefabs being expanded, outermost first: one of them nested again is a cycle (A holds B holds A), left out

            // The nested recipes whose members Address is under (the levels whose prefix is a proper prefix of it), deepest first.
            template< typename T_FN >
            void ForEachEnclosing( std::span<const std::uint64_t> Address, T_FN&& Fn ) noexcept
            {
                for( int k = static_cast<int>(m_Plan.m_Levels.size()) - 1; k >= 1; --k )
                {
                    auto& L = m_Plan.m_Levels[k];
                    if( L.m_Prefix.size() < Address.size() && StartsWith(Address, L.m_Prefix) )
                        Fn( L, Address.subspan(L.m_Prefix.size()) );
                }
            }

            void Visit( int iLevel, xecs::component::entity T, int Parent, address Address ) noexcept
            {
                bool bRemoved = false;
                ForEachEnclosing( Address, [&]( const level& L, std::span<const std::uint64_t> Rel ) noexcept { if( IsRemovedBy(L.m_Recipe, Rel) ) bRemoved = true; } );
                if( bRemoved ) return;

                const int n = static_cast<int>(m_Plan.m_Nodes.size());
                {
                    node N;
                    N.m_Address  = std::move(Address);
                    N.m_Template = T;
                    N.m_Parent   = Parent;
                    N.m_Level    = iLevel;
                    for( auto p : GatherInfos(m_GameMgr, T) )
                    {
                        if( xecs::component::type::IsComponentType<xecs::component::children>(p) ) N.m_bHasChildren = true;
                        if( false == IsStructural(p) ) N.m_Infos.push_back(p);
                    }
                    m_Plan.m_Nodes.push_back( std::move(N) );
                }
                if( Parent >= 0 ) m_Plan.m_Nodes[Parent].m_Children.push_back(n);

                // the component diffs the nested recipes over it make (a nested instance's own root-level ones are already in its template's data)
                ForEachEnclosing( m_Plan.m_Nodes[n].m_Address, [&]( const level& L, std::span<const std::uint64_t> Rel ) noexcept
                {
                    for( auto& D : L.m_Recipe.m_ComponentDiffs )
                    {
                        if( false == std::ranges::equal(D.m_Member, Rel) ) continue;
                        auto& Infos = m_Plan.m_Nodes[n].m_Infos;
                        if( D.m_bAdded )
                        {
                            if( auto* pInfo = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{D.m_ComponentTypeGuid} ); pInfo && !Has(Infos, D.m_ComponentTypeGuid) && !IsStructural(pInfo) )
                                Infos.push_back(pInfo);
                        }
                        else std::erase_if( Infos, [&]( auto p ) noexcept { return p->m_Guid.m_Value == D.m_ComponentTypeGuid; } );
                    }
                });

                if( auto* pPI = LiveComponent<xecs::editor::prefab_instance>(m_GameMgr, T) )
                {
                    ExpandNested( n, *pPI, m_Plan.m_Nodes[n].m_Address );
                    return;
                }

                // a member of the same prefab, however deep: the prefab's prefix and its own id (one id per prefab crossed, not per level of the hierarchy)
                const auto& Group = *m_Plan.m_Levels[iLevel].m_pGroup;
                for( auto Child : ChildrenOf(m_GameMgr, T) )
                {
                    auto It = Group.m_RuntimeToLocal.find(Child.m_Value);
                    if( It == Group.m_RuntimeToLocal.end() ) continue;          // not one of the template's members (cannot happen in a saved prefab)
                    Visit( iLevel, Child, n, Join(m_Plan.m_Levels[iLevel].m_Prefix, It->second) );
                }
            }

            // PI and Address by value: Address is a node's, and m_Plan.m_Nodes grows while the members are visited (a reference into it dangled once it
            // reallocated: the second member of a nested prefab got a garbage address); PI is a live component, and loading the prefab makes entities.
            void ExpandNested( int n, const xecs::editor::prefab_instance PI, const address Address ) noexcept
            {
                // A prefab that holds, however deep, an instance of itself would never end: the instance that closes the cycle is left out (its root stays, without members).
                if( std::ranges::find( m_Enclosing, PI.m_PrefabInstance.m_Instance.m_Value ) != m_Enclosing.end() )
                {
                    std::printf("[Prefab::Plan] WARNING: prefab %llX holds an instance of itself through its nested prefabs (a cycle) - that instance's members are left out\n", static_cast<unsigned long long>(PI.m_PrefabInstance.m_Instance.m_Value));
                    std::fflush(stdout);
                    return;
                }
                m_Enclosing.push_back( PI.m_PrefabInstance.m_Instance.m_Value );
                ExpandNestedOnce( n, PI, Address );
                m_Enclosing.pop_back();
            }

            void ExpandNestedOnce( int n, const xecs::editor::prefab_instance& PI, const address& Address ) noexcept
            {
                if( auto Err = m_GameMgr.m_PrefabMgr.EnsureLoaded(PI.m_PrefabInstance); Err )
                {
                    std::printf("[Prefab::Plan] WARNING: nested prefab %llX did not load (%s) - its members are left out\n", static_cast<unsigned long long>(PI.m_PrefabInstance.m_Instance.m_Value), std::string(Err.getMessage()).c_str());
                    std::fflush(stdout);
                    return;
                }
                auto RootIt  = m_GameMgr.m_PrefabMgr.m_PrefabList.find(PI.m_PrefabInstance.m_Instance.m_Value);
                auto GroupIt = m_GameMgr.m_PrefabMgr.m_PrefabGroups.find(PI.m_PrefabInstance.m_Instance.m_Value);
                if( RootIt == m_GameMgr.m_PrefabMgr.m_PrefabList.end() || GroupIt == m_GameMgr.m_PrefabMgr.m_PrefabGroups.end() ) return;

                level L;
                L.m_Prefab       = PI.m_PrefabInstance;
                L.m_Prefix       = Address;
                L.m_pGroup       = &GroupIt->second;
                L.m_TemplateRoot = RootIt->second;
                L.m_Owner        = n;
                L.m_OwnerLevel   = m_Plan.m_Nodes[n].m_Level;
                L.m_Recipe       = PI;
                if( L.m_Recipe.m_Format == 0 ) ConvertPaths( m_GameMgr, L.m_Recipe, L.m_TemplateRoot, *L.m_pGroup );
                m_Plan.m_Levels.push_back( std::move(L) );
                const int iLevel = static_cast<int>(m_Plan.m_Levels.size()) - 1;

                const auto Root = RootIt->second;
                if( auto* pInner = LiveComponent<xecs::editor::prefab_instance>(m_GameMgr, Root) )
                {
                    // the nested prefab is itself an instance at its root (a variant): its members are its base's, addressed the same way
                    const auto Copy = *pInner;
                    ExpandNested( n, Copy, Address );
                    return;
                }

                const auto& Group = GroupIt->second;
                for( auto Child : ChildrenOf(m_GameMgr, Root) )
                {
                    auto It = Group.m_RuntimeToLocal.find(Child.m_Value);
                    if( It == Group.m_RuntimeToLocal.end() ) continue;
                    Visit( iLevel, Child, n, Join(Address, It->second) );
                }
            }
        };
    }

    // The plan of a prefab (loaded if it is not resident). The nested instances are expanded with their recipes' removals and component diffs; the
    // instance's own recipe is not applied here (the stage and the refresh do that).
    inline xerr MakePlan( xecs::game_mgr::instance& GameMgr, xecs::prefab::guid PrefabGuid, plan& Out ) noexcept
    {
        Out = {};
        if( auto Err = GameMgr.m_PrefabMgr.EnsureLoaded(PrefabGuid); Err ) return Err;
        auto RootIt  = GameMgr.m_PrefabMgr.m_PrefabList.find(PrefabGuid.m_Instance.m_Value);
        auto GroupIt = GameMgr.m_PrefabMgr.m_PrefabGroups.find(PrefabGuid.m_Instance.m_Value);
        if( RootIt == GameMgr.m_PrefabMgr.m_PrefabList.end() || GroupIt == GameMgr.m_PrefabMgr.m_PrefabGroups.end() )
            return xerr::create<xecs::game_mgr::state::FAILURE, "MakePlan: the prefab is not resident">();

        level L0;
        L0.m_Prefab       = PrefabGuid;
        L0.m_pGroup       = &GroupIt->second;
        L0.m_TemplateRoot = RootIt->second;
        Out.m_Levels.push_back( std::move(L0) );

        details::plan_builder B{ GameMgr, Out };
        B.m_Enclosing.push_back( PrefabGuid.m_Instance.m_Value );
        B.Visit( 0, RootIt->second, -1, {} );
        return B.m_Error;
    }

    // True when an instance of Prefab is made of Used: it is Used, or it nests Used however deep (what a change of Used reaches; what a prefab cannot hold: itself).
    inline bool Uses( xecs::game_mgr::instance& GameMgr, xecs::prefab::guid Prefab, xecs::prefab::guid Used ) noexcept
    {
        if( Prefab.m_Instance.m_Value == Used.m_Instance.m_Value ) return true;
        plan P;
        if( MakePlan(GameMgr, Prefab, P) ) return false;
        return std::ranges::any_of( P.m_Levels, [&]( const level& L ) noexcept { return L.m_Prefab.m_Instance.m_Value == Used.m_Instance.m_Value; } );
    }

    // The template entity at an address of a prefab (what a member of an instance starts from), or an invalid entity.
    inline xecs::component::entity FindTemplate( xecs::game_mgr::instance& GameMgr, xecs::prefab::guid PrefabGuid, std::span<const std::uint64_t> Address ) noexcept
    {
        plan P;
        if( MakePlan(GameMgr, PrefabGuid, P) ) return {};
        const int i = P.Find(Address);
        return i < 0 ? xecs::component::entity{} : P.m_Nodes[i].m_Template;
    }

    // The name a member is spawned with: its template's name in its prefab.
    inline std::string TemplateName( const plan& Plan, int iNode ) noexcept
    {
        auto& N = Plan.m_Nodes[iNode];
        auto& G = *Plan.m_Levels[N.m_Level].m_pGroup;
        auto  It = G.m_RuntimeToLocal.find(N.m_Template.m_Value);
        if( It == G.m_RuntimeToLocal.end() ) return {};
        auto Name = G.m_EntityNames.find(It->second);
        return Name == G.m_EntityNames.end() ? std::string{} : Name->second;
    }

    //-----------------------------------------------------------------------------------------
    // Staging (scene load, and the members an instance is missing)
    //-----------------------------------------------------------------------------------------
    namespace details
    {
        // A reference of a template entity of level L, as a scene of the instance writes it: the id of the member it names.
        inline xecs::component::entity EncodeTemplateReference( const plan& Plan, int iLevel, xecs::scene::permanent_id InstanceId, xecs::component::entity R ) noexcept
        {
            if( false == R.isValid() ) return {};
            auto& L = Plan.m_Levels[iLevel];
            if( R.m_Value == L.m_TemplateRoot.m_Value ) return xecs::persist::details::EncodeRef( static_cast<std::int64_t>(xecs::scene::DeriveMemberId(InstanceId, L.m_Prefix)) );
            auto It = L.m_pGroup->m_RuntimeToLocal.find(R.m_Value);
            if( It == L.m_pGroup->m_RuntimeToLocal.end() ) return {};
            return xecs::persist::details::EncodeRef( static_cast<std::int64_t>(xecs::scene::DeriveMemberId(InstanceId, Join(L.m_Prefix, It->second))) );
        }

        // A nested recipe's entity override names a member of the prefab that holds the nested instance by its id there.
        inline std::optional<xecs::component::entity> DecodeLevelEntity( const plan& Plan, const level& L, xecs::scene::permanent_id InstanceId, const std::string& Text ) noexcept
        {
            auto V = DecodeEntityText(Text);
            if( false == V.has_value() ) return std::nullopt;
            if( *V <= 0 || L.m_OwnerLevel < 0 ) return xecs::component::entity{};
            auto& Owner = Plan.m_Levels[L.m_OwnerLevel];
            auto  It    = Owner.m_pGroup->m_LocalToRuntime.find( static_cast<local_id>(*V) );
            if( It == Owner.m_pGroup->m_LocalToRuntime.end() ) return xecs::component::entity{};
            return EncodeTemplateReference( Plan, L.m_OwnerLevel, InstanceId, It->second );
        }
    }

    // Which nodes the instance's recipe removed (a removed member takes its subtree with it).
    inline std::vector<bool> RemovedNodes( const plan& Plan, const xecs::editor::prefab_instance& Recipe ) noexcept
    {
        std::vector<bool> Removed( Plan.m_Nodes.size(), false );
        for( std::size_t i = 1; i < Plan.m_Nodes.size(); ++i )
            Removed[i] = Removed[ Plan.m_Nodes[i].m_Parent ] || details::IsRemovedBy( Recipe, Plan.m_Nodes[i].m_Address );
        return Removed;
    }

    // The components a member is made of: its node's, with the instance's own component diffs for it.
    inline std::vector<const xecs::component::type::info*> MemberInfos( const plan& Plan, int iNode, const xecs::editor::prefab_instance& Recipe ) noexcept
    {
        auto Infos = Plan.m_Nodes[iNode].m_Infos;
        for( auto& D : Recipe.m_ComponentDiffs )
        {
            if( false == std::ranges::equal(D.m_Member, Plan.m_Nodes[iNode].m_Address) ) continue;
            if( D.m_bAdded )
            {
                if( auto* pInfo = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{D.m_ComponentTypeGuid} ); pInfo && !details::Has(Infos, D.m_ComponentTypeGuid) && !details::IsStructural(pInfo) )
                    Infos.push_back(pInfo);
            }
            else std::erase_if( Infos, [&]( auto p ) noexcept { return p->m_Guid.m_Value == D.m_ComponentTypeGuid; } );
        }
        return Infos;
    }

    // Stages the members of one instance (every node but the root) that Filter accepts and its recipe did not remove: each read from the baked
    // plan (the nested recipes already applied), its parent, children and references written as the ids of the members they name (encoded, as a
    // scene file has them), and its id derived. The instance's own recipe is ApplyStagedOverrides'.
    template< typename T_FILTER >   // bool(int iNode)
    inline void StageMembers
    ( xecs::game_mgr::instance&                         GameMgr
    , const xecs::prefab::baked&                        Baked
    , const xecs::editor::prefab_instance&              Recipe
    , xecs::scene::permanent_id                         InstanceId
    , std::vector<xecs::scene::details::staged_entity>& Out
    , std::vector<int>&                                 OutNodes
    , T_FILTER&&                                        Filter
    ) noexcept
    {
        const auto& Plan    = Baked.m_Plan;
        const auto  Removed = RemovedNodes( Plan, Recipe );
        const auto  Encode  = [&]( int Target ) noexcept
        {
            return Target < 0 ? xecs::component::entity{} : xecs::persist::details::EncodeRef( static_cast<std::int64_t>(xecs::scene::DeriveMemberId(InstanceId, Plan.m_Nodes[Target].m_Address)) );
        };
        for( int i = 1, n = static_cast<int>(Plan.m_Nodes.size()); i < n; ++i )
        {
            if( Removed[i] || false == Filter(i) ) continue;
            auto& N = Plan.m_Nodes[i];
            auto& B = Baked.m_Nodes[i];

            auto Infos = MemberInfos( Plan, i, Recipe );
            Infos.push_back( &xecs::component::type::info_v<xecs::component::parent> );
            std::vector<xecs::component::entity> Kids;
            for( auto c : N.m_Children ) if( false == Removed[c] ) Kids.push_back( xecs::persist::details::EncodeRef( static_cast<std::int64_t>(xecs::scene::DeriveMemberId(InstanceId, Plan.m_Nodes[c].m_Address)) ) );
            if( N.m_bHasChildren || false == Kids.empty() ) Infos.push_back( &xecs::component::type::info_v<xecs::component::children> );

            xecs::scene::details::staged_entity S;
            S.m_Id      = xecs::scene::DeriveMemberId( InstanceId, N.m_Address );
            S.m_pPlan   = &GameMgr.getBuildPlan( { Infos.data(), Infos.size() } );
            S.m_pStaged = std::make_unique<xecs::persist::details::staged_components>( Infos );
            auto& St    = *S.m_pStaged;
            St.CopyFrom( GameMgr, B.m_Entity, { &xecs::component::type::info_v<xecs::component::children> } );

            St.find<xecs::component::parent>()->m_Value = Encode( N.m_Parent );
            if( auto* pKids = St.find<xecs::component::children>() ) pKids->m_List = std::move(Kids);

            for( auto& R : B.m_References )
                if( auto* pData = St.find(*R.m_pInfo) ) *reinterpret_cast<xecs::component::entity*>( pData + R.m_Offset ) = Encode( R.m_Target );
            for( auto& I : B.m_Indirect )
            {
                auto* pData = St.find(*I.m_pInfo);
                if( pData == nullptr ) continue;
                std::size_t j = 0;
                details::ForEachReference( *I.m_pInfo, pData, [&]( xecs::component::entity& R ) noexcept { R = Encode( j < I.m_Targets.size() ? I.m_Targets[j] : -1 ); ++j; } );
            }

            Out.push_back( std::move(S) );
            OutNodes.push_back(i);
        }
    }

    namespace details
    {
        template< typename T_FIND, typename T_DECODE >
        inline void ApplyStagedEntry( const xecs::editor::prefab_component_override& Entry, const address& Target, T_FIND&& FindStaged, T_DECODE&& Decode ) noexcept
        {
            auto* pInfo = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{Entry.m_ComponentTypeGuid} );
            if( pInfo == nullptr || pInfo->m_pPropertyTable == nullptr ) return;
            auto* pStaged = FindStaged( Target );
            if( pStaged == nullptr ) return;
            if( auto* pData = pStaged->find(*pInfo) ) ApplyOneComponent( Entry, *pInfo, pData, Decode );
        }
    }

    // The nested recipes' property overrides on staged entities (their references encoded as the ids an instance of InstanceId gives the members),
    // deepest first: what the bake does once for every spawn. An entity value of a nested recipe names a member of the prefab that holds it.
    // FindStaged(address) gives the staged entity of a member, or nullptr (not staged: nothing to do).
    template< typename T_FIND >   // xecs::persist::details::staged_components*(const address&)
    inline void ApplyNestedOverrides( const plan& Plan, xecs::scene::permanent_id InstanceId, T_FIND&& FindStaged ) noexcept
    {
        for( int k = static_cast<int>(Plan.m_Levels.size()) - 1; k >= 1; --k )
        {
            auto& L = Plan.m_Levels[k];
            for( auto& Entry : L.m_Recipe.m_lComponents )
            {
                address Target = L.m_Prefix;
                Target.insert( Target.end(), Entry.m_Member.begin(), Entry.m_Member.end() );
                details::ApplyStagedEntry( Entry, Target, FindStaged, [&]( const std::string& Text ) noexcept { return details::DecodeLevelEntity(Plan, L, InstanceId, Text); } );
            }
        }
    }

    // An instance's own recipe on its staged members (StageMembers: the nested recipes are in the baked plan already). An entity value is the
    // reference as the scene encodes it.
    template< typename T_FIND >   // xecs::persist::details::staged_components*(const address&)
    inline void ApplyStagedOverrides( const xecs::editor::prefab_instance& Recipe, T_FIND&& FindStaged ) noexcept
    {
        for( auto& Entry : Recipe.m_lComponents )
        {
            details::ApplyStagedEntry( Entry, Entry.m_Member, FindStaged, []( const std::string& Text ) noexcept -> std::optional<xecs::component::entity>
            {
                auto V = details::DecodeEntityText(Text);
                if( false == V.has_value() ) return std::nullopt;
                return xecs::persist::details::EncodeRef(*V);
            });
        }
    }

    //-----------------------------------------------------------------------------------------
    // Scene load (xecs_scene_inline.h, EnsureLoaded): the recipes' members are staged with the rest of the scene's entities, with every
    // recipe's overrides (so the builders, which run when they are created, see them). OutMembers says what the scene must know of each
    // member once it is created (RegisterMembers).
    //-----------------------------------------------------------------------------------------
    inline void StageSceneInstances
    ( xecs::game_mgr::instance&                                                     GameMgr
    , std::vector<xecs::scene::details::staged_entity>&                             Staged
    , std::unordered_map<xecs::scene::permanent_id, std::size_t>&                   StagedIndex
    , std::vector<std::pair<xecs::scene::permanent_id, xecs::scene::instance_member>>& OutMembers
    ) noexcept
    {
        struct instance_stage { std::size_t m_Root; const xecs::prefab::baked* m_pBaked; };
        std::vector<instance_stage> Instances;

        const std::size_t nFiles = Staged.size();
        for( std::size_t r = 0; r < nFiles; ++r )
        {
            auto* pPI = Staged[r].m_pStaged->find<xecs::editor::prefab_instance>();
            if( pPI == nullptr || pPI->m_Format == 0 ) continue;              // not an instance, or one saved before recipes (converted once it is live)

            const auto* pBaked = GameMgr.m_PrefabMgr.getBaked( pPI->m_PrefabInstance );      // one per prefab, however many instances
            if( pBaked == nullptr )
            {
                std::printf("[Scene::Load] WARNING: the prefab of instance %s did not load - it is loaded without its members\n", xecs::scene::FormatPermanentId(Staged[r].m_Id).c_str());
                std::fflush(stdout);
                continue;
            }
            const auto* pThePlan = &pBaked->m_Plan;
            const auto  RootId   = Staged[r].m_Id;
            const auto  Recipe   = *pPI;

            std::vector<xecs::scene::details::staged_entity> Members;
            std::vector<int>                                 Nodes;
            StageMembers( GameMgr, *pBaked, Recipe, RootId, Members, Nodes, []( int ) noexcept { return true; } );

            // the root's children: the members under it (the scene's entities whose parent it is join once created, LinkChildren). Its children
            // component was given by DetectAndUnionPrefabInstance when its prefab can have members.
            const auto Removed = RemovedNodes( *pThePlan, Recipe );
            if( auto* pKids = Staged[r].m_pStaged->find<xecs::component::children>() )
            {
                pKids->m_List.clear();
                for( auto c : pThePlan->m_Nodes[0].m_Children ) if( false == Removed[c] ) pKids->m_List.push_back( xecs::persist::details::EncodeRef( static_cast<std::int64_t>(xecs::scene::DeriveMemberId(RootId, pThePlan->m_Nodes[c].m_Address)) ) );
            }

            for( std::size_t m = 0; m < Members.size(); ++m )
            {
                if( StagedIndex.contains(Members[m].m_Id) )
                {
                    std::printf("[Scene::Load] WARNING: the id %s derived for a member of instance %s is already taken - that member is left out\n", xecs::scene::FormatPermanentId(Members[m].m_Id).c_str(), xecs::scene::FormatPermanentId(RootId).c_str());
                    std::fflush(stdout);
                    continue;
                }
                OutMembers.push_back({ Members[m].m_Id, xecs::scene::instance_member{ .m_Root = RootId, .m_Address = pThePlan->m_Nodes[Nodes[m]].m_Address, .m_Name = TemplateName(*pThePlan, Nodes[m]) } });
                StagedIndex[Members[m].m_Id] = Staged.size();
                Staged.push_back( std::move(Members[m]) );
            }
            Instances.push_back({ r, pBaked });
        }

        for( auto& I : Instances )
        {
            const auto RootId = Staged[I.m_Root].m_Id;
            const auto Recipe = *Staged[I.m_Root].m_pStaged->find<xecs::editor::prefab_instance>();
            ApplyStagedOverrides( Recipe, [&]( const address& A ) noexcept -> xecs::persist::details::staged_components*
            {
                auto S = StagedIndex.find( xecs::scene::DeriveMemberId(RootId, A) );
                return S == StagedIndex.end() || !Staged[S->second].m_pStaged ? nullptr : Staged[S->second].m_pStaged.get();
            });
        }
    }

    // What the scene knows of each member it created (its instance, its address, the name it was spawned with).
    inline void RegisterMembers( xecs::scene::instance& Scene, std::vector<std::pair<xecs::scene::permanent_id, xecs::scene::instance_member>>& Members ) noexcept
    {
        for( auto& [Id, M] : Members )
        {
            if( false == Scene.m_LocalToRuntime.contains(Id) ) continue;
            if( false == M.m_Name.empty() && false == Scene.m_EntityNames.contains(Id) ) Scene.m_EntityNames[Id] = M.m_Name;     // a name the scene holds for it is a rename
            Scene.m_InstanceMembers[Id] = std::move(M);
        }
        Members.clear();
    }

    // An entity whose parent's children list does not have it (an entity of the scene added under a member or under an instance root: their
    // children lists are not stored) is appended to it.
    inline void LinkChildren( xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene ) noexcept
    {
        std::vector<std::pair<xecs::component::entity, xecs::component::entity>> Missing;
        for( auto& [Id, E] : Scene.m_LocalToRuntime )
        {
            auto* pParent = details::LiveComponent<xecs::component::parent>( GameMgr, E );
            if( pParent == nullptr || false == pParent->m_Value.isValid() ) continue;
            if( false == GameMgr.m_ComponentMgr.isEntityValid(pParent->m_Value) ) continue;
            auto* pKids = details::LiveComponent<xecs::component::children>( GameMgr, pParent->m_Value );
            if( pKids == nullptr || std::none_of( pKids->m_List.begin(), pKids->m_List.end(), [&]( auto& K ) noexcept { return K.m_Value == E.m_Value; } ) )
                Missing.push_back({ pParent->m_Value, E });
        }
        std::sort( Missing.begin(), Missing.end(), [&]( auto& A, auto& B ) noexcept { return Scene.m_RuntimeToLocal[A.second.m_Value] < Scene.m_RuntimeToLocal[B.second.m_Value]; } );
        for( auto& [P, E] : Missing )
        {
            // an instance root whose prefab has one member (so no children of its own) gets the component its scene's entities need
            if( details::LiveComponent<xecs::component::children>( GameMgr, P ) == nullptr )
            {
                const xecs::component::type::info* const Add[] = { &xecs::component::type::info_v<xecs::component::children> };
                (void)GameMgr.AddOrRemoveComponents( P, { Add, 1 }, {} );            // the handle stays (a move of archetype)
            }
            if( auto* pKids = details::LiveComponent<xecs::component::children>( GameMgr, P ) ) pKids->m_List.push_back(E);
        }
    }

    //-----------------------------------------------------------------------------------------
    // Live instances: pairing what CreatePrefabInstance (or a scene saved before recipes) made with the plan.
    //-----------------------------------------------------------------------------------------

    // The live entity of each node (invalid: none), walking the plan's nodes in order and pairing each node's children with the live entity's
    // children by position, skipping the nodes Removed says are gone. The live children that pair with no node are listed in pUnpaired (the
    // entities of the scene added under the instance).
    inline std::vector<xecs::component::entity> PairLive( xecs::game_mgr::instance& GameMgr, const plan& Plan, xecs::component::entity LiveRoot, const std::vector<bool>* pRemoved, std::vector<xecs::component::entity>* pUnpaired ) noexcept
    {
        std::vector<xecs::component::entity> ByNode( Plan.m_Nodes.size() );
        ByNode[0] = LiveRoot;
        for( std::size_t i = 0; i < Plan.m_Nodes.size(); ++i )
        {
            if( false == ByNode[i].isValid() ) continue;
            const auto Live = details::ChildrenOf( GameMgr, ByNode[i] );
            std::size_t k = 0;
            for( auto c : Plan.m_Nodes[i].m_Children )
            {
                if( pRemoved && (*pRemoved)[c] ) continue;
                if( k < Live.size() ) ByNode[c] = Live[k++];
            }
            if( pUnpaired ) for( ; k < Live.size(); ++k ) pUnpaired->push_back( Live[k] );
        }
        return ByNode;
    }

    // An instance's overrides on its live members (entity values are left as they are: a live entity is already right, and a text form needs the
    // scene). FindLive(address) gives the member, or an invalid entity.
    template< typename T_FIND >
    inline void ApplyLiveOverrides( xecs::game_mgr::instance& GameMgr, const xecs::editor::prefab_instance& Recipe, T_FIND&& FindLive ) noexcept
    {
        for( auto& Entry : Recipe.m_lComponents )
        {
            auto* pInfo = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{Entry.m_ComponentTypeGuid} );
            if( pInfo == nullptr || pInfo->m_pPropertyTable == nullptr ) continue;
            const auto E = FindLive( Entry.m_Member );
            if( false == E.isValid() || false == GameMgr.m_ComponentMgr.isEntityValid(E) ) continue;
            auto& D = GameMgr.m_ComponentMgr.getEntityDetails(E);
            const auto iType = D.m_pPool ? D.m_pPool->findIndexComponentFromInfo(*pInfo) : -1;
            if( iType < 0 ) continue;      // DATA only: a SHARE value belongs to the whole family
            details::ApplyOneComponent( Entry, *pInfo, &D.m_pPool->m_pComponent[iType][ D.m_PoolIndex.m_Value * pInfo->m_Size ], []( const std::string& ) noexcept { return std::optional<xecs::component::entity>{}; } );
        }
    }

    // A nested instance in a prefab written before phase 3 (its recipe addressed members by child-index paths): converted in place once the
    // prefab is loaded, against its own prefab's template. The prefab's next save writes it as it is now.
    inline void ConvertNestedRecipe( xecs::game_mgr::instance& GameMgr, xecs::component::entity Member ) noexcept
    {
        auto* pPI = details::LiveComponent<xecs::editor::prefab_instance>( GameMgr, Member );
        if( pPI == nullptr || pPI->m_Format != 0 ) return;
        const auto Guid = pPI->m_PrefabInstance;
        if( GameMgr.m_PrefabMgr.EnsureLoaded(Guid) ) return;
        auto RootIt  = GameMgr.m_PrefabMgr.m_PrefabList.find(Guid.m_Instance.m_Value);
        auto GroupIt = GameMgr.m_PrefabMgr.m_PrefabGroups.find(Guid.m_Instance.m_Value);
        if( RootIt == GameMgr.m_PrefabMgr.m_PrefabList.end() || GroupIt == GameMgr.m_PrefabMgr.m_PrefabGroups.end() ) return;
        if( (pPI = details::LiveComponent<xecs::editor::prefab_instance>( GameMgr, Member )) == nullptr ) return;
        details::ConvertPaths( GameMgr, *pPI, RootIt->second, GroupIt->second );
    }

    // What CreatePrefabInstance does for a member of a template that is a nested instance, once the nested prefab's entities are made under
    // NestedRoot: the nested recipe's component diffs on its members, its removals and its overrides.
    inline void ApplyNestedRecipeLive( xecs::game_mgr::instance& GameMgr, xecs::component::entity NestedRoot, const xecs::editor::prefab_instance& Recipe ) noexcept
    {
        // Most nested recipes touch only their root (or nothing): no plan is needed for that, and a spawn makes this call for every nested instance.
        const bool bMembers = std::any_of( Recipe.m_lComponents.begin(),    Recipe.m_lComponents.end(),    []( auto& C ) noexcept { return !C.m_Member.empty() || !C.m_MemberPath.empty(); } )
                           || std::any_of( Recipe.m_ComponentDiffs.begin(), Recipe.m_ComponentDiffs.end(), []( auto& D ) noexcept { return !D.m_Member.empty(); } )
                           || std::any_of( Recipe.m_HierarchyDiffs.begin(), Recipe.m_HierarchyDiffs.end(), []( auto& H ) noexcept { return !H.m_bAdded; } );
        if( false == bMembers )
        {
            if( false == Recipe.m_lComponents.empty() )
                ApplyLiveOverrides( GameMgr, Recipe, [&]( const address& A ) noexcept { return A.empty() ? NestedRoot : xecs::component::entity{}; } );
            return;
        }

        auto PI = Recipe;
        plan P;
        if( MakePlan(GameMgr, PI.m_PrefabInstance, P) ) return;
        if( PI.m_Format == 0 ) details::ConvertPaths( GameMgr, PI, P.m_Levels[0].m_TemplateRoot, *P.m_Levels[0].m_pGroup );

        auto ByNode = PairLive( GameMgr, P, NestedRoot, nullptr, nullptr );

        // members' component diffs (the root's were applied by the caller)
        for( auto& D : PI.m_ComponentDiffs )
        {
            if( D.m_Member.empty() ) continue;
            const int i = P.Find(D.m_Member);
            if( i <= 0 || false == ByNode[i].isValid() ) continue;
            auto* pInfo = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{D.m_ComponentTypeGuid} );
            if( pInfo == nullptr || details::IsStructural(pInfo) ) continue;
            const bool bHas = details::Has( details::GatherInfos(GameMgr, ByNode[i]), D.m_ComponentTypeGuid );
            if( D.m_bAdded == bHas ) continue;
            const xecs::component::type::info* const One[] = { pInfo };
            ByNode[i] = D.m_bAdded ? GameMgr.AddOrRemoveComponents( ByNode[i], { One, 1 }, {} ) : GameMgr.AddOrRemoveComponents( ByNode[i], {}, { One, 1 } );
        }

        ApplyLiveOverrides( GameMgr, PI, [&]( const address& A ) noexcept -> xecs::component::entity
        {
            const int i = P.Find(A);
            return i < 0 ? xecs::component::entity{} : ByNode[i];
        });

        // removals last, deepest first (the subtree goes with its root)
        for( int i = static_cast<int>(P.m_Nodes.size()) - 1; i >= 1; --i )
            if( details::IsRemovedBy(PI, P.m_Nodes[i].m_Address) && ByNode[i].isValid() && GameMgr.m_ComponentMgr.isEntityValid(ByNode[i]) )
                xecs::persist::details::DeleteEntitySubtreeUnregistered( GameMgr, ByNode[i] );
    }

    //-----------------------------------------------------------------------------------------
    // Live instances in a scene (the editor's): refresh, spawn what is missing, place, convert, apply.
    //-----------------------------------------------------------------------------------------
    namespace details
    {
        inline xecs::component::entity FindMember( const xecs::scene::instance& Scene, xecs::scene::permanent_id RootId, std::span<const std::uint64_t> A ) noexcept
        {
            auto It = Scene.m_LocalToRuntime.find( xecs::scene::DeriveMemberId(RootId, A) );
            return It == Scene.m_LocalToRuntime.end() ? xecs::component::entity{} : It->second;
        }

        // What a reference read from a file of the scene names, now (positive: an id of the scene; negative: its external table).
        inline xecs::component::entity ResolveInScene( const xecs::scene::instance& Scene, std::int64_t Encoded ) noexcept
        {
            if( Encoded == 0 ) return {};
            if( Encoded > 0 )
            {
                auto It = Scene.m_LocalToRuntime.find( static_cast<xecs::scene::permanent_id>(Encoded) );
                return It == Scene.m_LocalToRuntime.end() ? xecs::component::entity{} : It->second;
            }
            const auto i = static_cast<std::size_t>(-Encoded - 1);
            return i < Scene.m_ExternalToRuntime.size() ? Scene.m_ExternalToRuntime[i] : xecs::component::entity{};
        }

        // The children of E that are entities of the scene and not members (the scene's entities added under the instance).
        inline bool HasAddedChildren( xecs::game_mgr::instance& GameMgr, const xecs::scene::instance& Scene, xecs::component::entity E ) noexcept
        {
            for( auto K : ChildrenOf(GameMgr, E) )
            {
                auto It = Scene.m_RuntimeToLocal.find(K.m_Value);
                if( It != Scene.m_RuntimeToLocal.end() && false == Scene.m_InstanceMembers.contains(It->second) ) return true;
            }
            return false;
        }

        inline xecs::editor::prefab_component_override& EntryFor( xecs::editor::prefab_instance& PI, std::uint64_t Guid, const address& A ) noexcept
        {
            for( auto& C : PI.m_lComponents ) if( C.m_ComponentTypeGuid == Guid && std::ranges::equal(C.m_Member, A) ) return C;
            PI.m_lComponents.push_back({ .m_ComponentTypeGuid = Guid, .m_Member = A });
            return PI.m_lComponents.back();
        }

        inline void SetText( xecs::editor::prefab_component_override& Entry, const char* pName, std::string Text ) noexcept
        {
            for( auto& P : Entry.m_PropertyOverrides ) if( P.m_PropertyName == pName ) { P.m_PropertyValueAsString = std::move(Text); return; }
            Entry.m_PropertyOverrides.push_back({ .m_PropertyName = pName, .m_PropertyValueAsString = std::move(Text) });
        }

        // A property that is saved (not const, not DONT_SAVE), as the serializer decides it.
        inline bool IsSaved( const xproperty::type::members& Member, bool bConst, const void* pInstance ) noexcept
        {
            if( bConst ) return false;
            xproperty::settings::context Context{};
            if( auto* pDynamic = Member.getUserData<xproperty::settings::member_dynamic_flags_t>(); pDynamic ) return false == pDynamic->m_pCallback(pInstance, Context).m_bDontSave;
            if( auto* pStatic  = Member.getUserData<xproperty::settings::member_flags_t>();         pStatic  ) return false == pStatic->m_Flags.m_bDontSave;
            return true;
        }

        // The text of a live property for an override: an entity as the file it goes to encodes it (Encode), anything else as ValueText.
        template< typename T_ENCODE >   // bool(xecs::component::entity, std::int64_t&)
        inline bool LiveText( const xproperty::any& V, std::string& Out, T_ENCODE&& Encode ) noexcept
        {
            if( IsEntityValue(V) )
            {
                std::int64_t N = 0;
                const auto   E = V.get<xecs::component::entity>();
                if( E.isValid() && false == Encode(E, N) ) N = 0;
                Out = EncodeEntityText(N);
                return true;
            }
            return ValueText( V, Out );
        }

        // Every saved property of one component of a live entity, as overrides: the data of a component added to a member (which has no file).
        template< typename T_ENCODE >
        inline void DumpComponent( xecs::game_mgr::instance& GameMgr, xecs::editor::prefab_instance& PI, const xecs::component::type::info& Info, xecs::component::entity E, const address& A, T_ENCODE&& Encode ) noexcept
        {
            auto& Entry = EntryFor( PI, Info.m_Guid.m_Value, A );
            Entry.m_PropertyOverrides.clear();
            if( Info.m_pPropertyTable == nullptr ) return;
            auto* pData = xecs::persist::details::ResolveLiveComponentPointer( GameMgr, E, Info );
            if( pData == nullptr ) return;
            xproperty::settings::context Context{};
            xproperty::sprop::collector( pData, *Info.m_pPropertyTable, Context, [&]( const char* pName, xproperty::any&& V, const xproperty::type::members& Member, bool bConst, const void* pInstance ) noexcept
            {
                if( false == IsSaved(Member, bConst, pInstance) ) return;
                std::string Text;
                if( LiveText(V, Text, Encode) ) Entry.m_PropertyOverrides.push_back({ .m_PropertyName = pName, .m_PropertyValueAsString = std::move(Text) });
            });
        }

        // An entry's texts, from the live entity's values.
        template< typename T_ENCODE >
        inline void RefreshTexts( xecs::game_mgr::instance& GameMgr, xecs::editor::prefab_component_override& Entry, xecs::component::entity E, T_ENCODE&& Encode ) noexcept
        {
            auto* pInfo = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{Entry.m_ComponentTypeGuid} );
            if( pInfo == nullptr || pInfo->m_pPropertyTable == nullptr ) return;
            auto* pData = xecs::persist::details::ResolveLiveComponentPointer( GameMgr, E, *pInfo );
            if( pData == nullptr ) return;
            xproperty::settings::context Context{};
            xproperty::sprop::collector( pData, *pInfo->m_pPropertyTable, Context, [&]( const char* pName, xproperty::any&& V, const xproperty::type::members&, bool, const void* ) noexcept
            {
                for( auto& Prop : Entry.m_PropertyOverrides )
                    if( Prop.m_PropertyName == pName ) { std::string Text; if( LiveText(V, Text, Encode) ) Prop.m_PropertyValueAsString = std::move(Text); }
            });
        }

        // Moves an entity of the scene to another id (the members a conversion or an Apply turns into members of an instance): its maps, its
        // name, and the file of the old id goes at the next save.
        inline void ReId( xecs::scene::mgr& Mgr, xecs::scene::instance& Scene, xecs::scene::permanent_id OldId, xecs::scene::permanent_id NewId, xecs::scene::instance_member Member ) noexcept
        {
            auto It = Scene.m_LocalToRuntime.find(OldId);
            if( It == Scene.m_LocalToRuntime.end() ) return;
            const auto E = It->second;
            Scene.m_LocalToRuntime.erase(It);
            Scene.m_LocalToRuntime[NewId]      = E;
            Scene.m_RuntimeToLocal[E.m_Value]  = NewId;

            if( auto Name = Scene.m_EntityNames.find(OldId); Name != Scene.m_EntityNames.end() )
            {
                Scene.m_EntityNames[NewId] = std::move(Name->second);
                Scene.m_EntityNames.erase(OldId);
            }
            else if( false == Member.m_Name.empty() ) Scene.m_EntityNames[NewId] = Member.m_Name;
            for( auto& F : Scene.m_Folders ) std::erase( F.m_Entities, OldId );
            Scene.m_InstanceMembers.erase(OldId);
            Scene.m_InstanceMembers[NewId] = std::move(Member);
            Mgr.MarkEntityDeleted( Scene.m_Guid, OldId );

            // other scenes that reference it through their external table
            for( auto& pOther : Mgr.m_SceneInstances )
                for( auto& Ext : pOther->m_ExternalRefTable )
                    if( Ext.m_ParentScene == Scene.m_Guid && Ext.m_ParentEntity == OldId ) Ext.m_ParentEntity = NewId;
        }

        // The entities of the scene that reference one of Moved are written again (their files name the old ids).
        inline void MarkReferencesDirty( xecs::scene::mgr& Mgr, xecs::scene::instance& Scene, const std::unordered_set<std::uint64_t>& Moved ) noexcept
        {
            if( Moved.empty() ) return;
            for( auto& [Id, E] : Scene.m_LocalToRuntime )
            {
                if( Scene.m_InstanceMembers.contains(Id) || false == Mgr.m_GameMgr.m_ComponentMgr.isEntityValid(E) ) continue;
                bool bRefers = false;
                xecs::persist::details::RemapEntityReferences( Mgr.m_GameMgr, E, [&]( xecs::component::entity R ) noexcept { if( Moved.contains(R.m_Value) ) bRefers = true; return R; } );
                if( bRefers ) Mgr.MarkEntityDirty( Scene.m_Guid, Id );
            }
        }
    }

    //-----------------------------------------------------------------------------------------
    // The recipe of a live instance, refreshed from what its members are now (before it is saved, cloned into a prefab, applied or respawned): the
    // component diffs of its members (a component added to a member keeps its data as overrides of every property: a member has no file), the
    // members it removed, which members have entities of the scene under them, and the text of each override that is an entity (as Encode writes
    // it). The root's own component diffs are refreshed when its file is written (RefreshPrefabInstanceOverlayRecord). What addresses a member the
    // prefab no longer has (an orphan) is kept as it is.
    //-----------------------------------------------------------------------------------------
    template< typename T_ENCODE >   // bool(xecs::component::entity Target, std::int64_t& OutEncoded)
    inline void RefreshRecipe( xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::permanent_id RootId, T_ENCODE&& Encode ) noexcept
    {
        auto RootIt = Scene.m_LocalToRuntime.find(RootId);
        if( RootIt == Scene.m_LocalToRuntime.end() || Scene.m_InstanceMembers.contains(RootId) ) return;
        const auto Root = RootIt->second;
        auto* pPI = details::LiveComponent<xecs::editor::prefab_instance>( GameMgr, Root );
        if( pPI == nullptr || pPI->m_Format == 0 ) return;
        plan P;
        if( MakePlan(GameMgr, pPI->m_PrefabInstance, P) ) return;
        pPI = details::LiveComponent<xecs::editor::prefab_instance>( GameMgr, Root );           // loading a prefab makes entities: fetched again
        if( pPI == nullptr ) return;
        auto& PI = *pPI;

        const bool bSkipBuilders = GameMgr.areBuildersEnabled();        // a running world has consumed them
        const auto Comparable    = [&]( const xecs::component::type::info* p ) noexcept { return false == details::IsStructural(p) && false == (bSkipBuilders && p->m_bBuilder); };

        std::vector<xecs::editor::prefab_component_diff> Diffs;
        for( auto& D : PI.m_ComponentDiffs ) if( D.m_Member.empty() || P.Find(D.m_Member) < 0 ) Diffs.push_back(D);
        std::vector<xecs::editor::prefab_hierarchy_diff> Hier;

        std::vector<bool> Live( P.m_Nodes.size(), false );
        Live[0] = true;
        for( std::size_t i = 1; i < P.m_Nodes.size(); ++i )
        {
            auto&      N = P.m_Nodes[i];
            const auto E = details::FindMember( Scene, RootId, N.m_Address );
            Live[i] = E.isValid() && GameMgr.m_ComponentMgr.isEntityValid(E);
            if( false == Live[i] )
            {
                if( Live[N.m_Parent] ) Hier.push_back({ .m_Member = N.m_Address, .m_bAdded = false });
                continue;
            }

            const auto AllLive = details::GatherInfos( GameMgr, E );
            for( auto p : AllLive )
                if( Comparable(p) && false == details::Has(N.m_Infos, p->m_Guid.m_Value) )
                {
                    Diffs.push_back({ .m_ComponentTypeGuid = p->m_Guid.m_Value, .m_bAdded = true, .m_Member = N.m_Address });
                    details::DumpComponent( GameMgr, PI, *p, E, N.m_Address, Encode );
                }
            for( auto p : N.m_Infos )
                if( Comparable(p) && false == details::Has(AllLive, p->m_Guid.m_Value) )
                    Diffs.push_back({ .m_ComponentTypeGuid = p->m_Guid.m_Value, .m_bAdded = false, .m_Member = N.m_Address });

            std::erase_if( PI.m_lComponents, [&]( auto& C ) noexcept { return std::ranges::equal(C.m_Member, N.m_Address) && false == details::Has(AllLive, C.m_ComponentTypeGuid); } );
            for( auto& C : PI.m_lComponents )
                if( std::ranges::equal(C.m_Member, N.m_Address) ) details::RefreshTexts( GameMgr, C, E, Encode );

            if( details::HasAddedChildren(GameMgr, Scene, E) ) Hier.push_back({ .m_Member = N.m_Address, .m_bAdded = true });
        }
        if( details::HasAddedChildren(GameMgr, Scene, Root) ) Hier.push_back({ .m_Member = {}, .m_bAdded = true });
        for( auto& C : PI.m_lComponents )
            if( C.m_Member.empty() ) details::RefreshTexts( GameMgr, C, Root, Encode );

        PI.m_ComponentDiffs = std::move(Diffs);
        PI.m_HierarchyDiffs = std::move(Hier);
    }

    //-----------------------------------------------------------------------------------------
    // The members an instance should have and does not (its recipe's removals aside): what Revert Hierarchy brings back, and what a prefab gained
    // since an old scene was saved. Each is staged and made as a scene load makes it, registered in the scene, and linked under its parent.
    // Returns how many were made.
    //-----------------------------------------------------------------------------------------
    inline int SpawnMissingMembers( xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::permanent_id RootId ) noexcept
    {
        auto RootIt = Scene.m_LocalToRuntime.find(RootId);
        if( RootIt == Scene.m_LocalToRuntime.end() ) return 0;
        auto* pPI = details::LiveComponent<xecs::editor::prefab_instance>( GameMgr, RootIt->second );
        if( pPI == nullptr || pPI->m_Format == 0 ) return 0;
        const auto Recipe = *pPI;

        const auto* pBaked = GameMgr.m_PrefabMgr.getBaked( Recipe.m_PrefabInstance );
        if( pBaked == nullptr ) return 0;
        const auto& P       = pBaked->m_Plan;
        const auto  Removed = RemovedNodes( P, Recipe );
        std::vector<bool> Missing( P.m_Nodes.size(), false );
        for( std::size_t i = 1; i < P.m_Nodes.size(); ++i )
            Missing[i] = !Removed[i] && !details::FindMember( Scene, RootId, P.m_Nodes[i].m_Address ).isValid();

        std::vector<xecs::scene::details::staged_entity> Staged;
        std::vector<int>                                 Nodes;
        StageMembers( GameMgr, *pBaked, Recipe, RootId, Staged, Nodes, [&]( int i ) noexcept { return Missing[i]; } );
        if( Staged.empty() ) return 0;

        std::unordered_map<xecs::scene::permanent_id, std::size_t> Index;
        for( std::size_t k = 0; k < Staged.size(); ++k ) Index[Staged[k].m_Id] = k;
        ApplyStagedOverrides( Recipe, [&]( const address& A ) noexcept -> xecs::persist::details::staged_components*
        {
            auto It = Index.find( xecs::scene::DeriveMemberId(RootId, A) );
            return It == Index.end() ? nullptr : Staged[It->second].m_pStaged.get();
        });

        std::vector<xecs::component::entity> Made;
        for( std::size_t k = 0; k < Staged.size(); ++k )
        {
            const auto Id = Staged[k].m_Id;
            if( Scene.m_LocalToRuntime.contains(Id) )
            {
                std::printf("[Prefab] WARNING: the id %s derived for a member of instance %s is already taken - that member is left out\n", xecs::scene::FormatPermanentId(Id).c_str(), xecs::scene::FormatPermanentId(RootId).c_str());
                std::fflush(stdout);
                continue;
            }
            xecs::scene::details::CreateStagedEntity( GameMgr, Scene, Staged[k] );
            Made.push_back( Scene.m_LocalToRuntime.at(Id) );
            auto Name = TemplateName( P, Nodes[k] );
            if( false == Name.empty() && false == Scene.m_EntityNames.contains(Id) ) Scene.m_EntityNames[Id] = Name;
            Scene.m_InstanceMembers[Id] = xecs::scene::instance_member{ .m_Root = RootId, .m_Address = P.m_Nodes[Nodes[k]].m_Address, .m_Name = std::move(Name) };
        }
        for( auto E : Made )
            xecs::persist::details::RemapLoadedEntityReferences( GameMgr, E, [&]( std::int64_t Encoded ) noexcept { return details::ResolveInScene(Scene, Encoded); } );
        LinkChildren( GameMgr, Scene );
        return static_cast<int>(Made.size());
    }

    //-----------------------------------------------------------------------------------------
    // Places an instance of a prefab in a scene under Id (the editor's InstantiatePrefab): made with the call a game spawns with
    // (prefab::mgr::Spawn: built when the world runs builder systems, as in Play), given its recipe (empty), registered with its members (their
    // derived ids and names), and put under Parent when one is given. Returns the root, or an invalid entity.
    //-----------------------------------------------------------------------------------------
    inline xecs::component::entity InstantiateInScene( xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::prefab::guid PrefabGuid, xecs::scene::permanent_id Id, xecs::component::entity Parent ) noexcept
    {
        if( Id == xecs::scene::invalid_permanent_id_v || Id > xecs::scene::max_permanent_id_v || Scene.m_LocalToRuntime.contains(Id) ) return {};
        const auto* pBaked = GameMgr.m_PrefabMgr.getBaked( PrefabGuid );
        if( pBaked == nullptr ) return {};
        const auto& P = pBaked->m_Plan;

        const auto Spawned = GameMgr.m_PrefabMgr.Spawn( PrefabGuid, 1 );
        if( Spawned.empty() ) return {};
        const std::vector<xecs::component::entity> ByNode( Spawned.begin(), Spawned.end() );       // one instance: the entity of each node
        auto Root = ByNode[0];

        // its recipe (a variant's root came with its base's: this is an instance of the variant)
        if( details::LiveComponent<xecs::editor::prefab_instance>(GameMgr, Root) == nullptr )
        {
            const xecs::component::type::info* const Add[] = { &xecs::component::type::info_v<xecs::editor::prefab_instance> };
            Root = GameMgr.AddOrRemoveComponents( Root, { Add, 1 }, {} );
        }
        if( auto* pPI = details::LiveComponent<xecs::editor::prefab_instance>(GameMgr, Root) )
            *pPI = xecs::editor::prefab_instance{ .m_PrefabInstance = PrefabGuid };

        if( Parent.isValid() && GameMgr.m_ComponentMgr.isEntityValid(Parent) )
        {
            const xecs::component::type::info* const AddParent[] = { &xecs::component::type::info_v<xecs::component::parent> };
            if( details::LiveComponent<xecs::component::parent>(GameMgr, Root) == nullptr ) Root = GameMgr.AddOrRemoveComponents( Root, { AddParent, 1 }, {} );
            details::LiveComponent<xecs::component::parent>(GameMgr, Root)->m_Value = Parent;
            if( details::LiveComponent<xecs::component::children>(GameMgr, Parent) == nullptr )
            {
                const xecs::component::type::info* const AddKids[] = { &xecs::component::type::info_v<xecs::component::children> };
                const auto Moved = GameMgr.AddOrRemoveComponents( Parent, { AddKids, 1 }, {} );
                if( auto It = Scene.m_RuntimeToLocal.find(Parent.m_Value); It != Scene.m_RuntimeToLocal.end() && Moved.m_Value != Parent.m_Value )
                {
                    const auto ParentId = It->second;
                    Scene.m_RuntimeToLocal.erase(It);
                    Scene.m_LocalToRuntime[ParentId]      = Moved;
                    Scene.m_RuntimeToLocal[Moved.m_Value] = ParentId;
                }
                Parent = Moved;
            }
            details::LiveComponent<xecs::component::children>(GameMgr, Parent)->m_List.push_back(Root);
            if( auto It = Scene.m_RuntimeToLocal.find(Parent.m_Value); It != Scene.m_RuntimeToLocal.end() ) GameMgr.m_SceneMgr.MarkEntityDirty( Scene.m_Guid, It->second );
        }

        Scene.m_LocalToRuntime[Id]           = Root;
        Scene.m_RuntimeToLocal[Root.m_Value] = Id;
        GameMgr.m_SceneMgr.MarkEntityNew( Scene.m_Guid, Id );

        for( std::size_t i = 1; i < P.m_Nodes.size(); ++i )
        {
            if( false == ByNode[i].isValid() ) continue;
            const auto MemberId = xecs::scene::DeriveMemberId( Id, P.m_Nodes[i].m_Address );
            if( Scene.m_LocalToRuntime.contains(MemberId) )
            {
                std::printf("[Prefab] WARNING: the id %s derived for a member of instance %s is already taken - that member is not registered\n", xecs::scene::FormatPermanentId(MemberId).c_str(), xecs::scene::FormatPermanentId(Id).c_str());
                std::fflush(stdout);
                continue;
            }
            Scene.m_LocalToRuntime[MemberId]          = ByNode[i];
            Scene.m_RuntimeToLocal[ByNode[i].m_Value] = MemberId;
            auto Name = TemplateName( P, static_cast<int>(i) );
            if( false == Name.empty() ) Scene.m_EntityNames[MemberId] = Name;
            Scene.m_InstanceMembers[MemberId] = xecs::scene::instance_member{ .m_Root = Id, .m_Address = P.m_Nodes[i].m_Address, .m_Name = std::move(Name) };
        }
        return Root;
    }

    //-----------------------------------------------------------------------------------------
    // Converts the instances of a scene saved before recipes (prefab_instance::m_Format 0: every member an entity file of the scene, overrides
    // addressed by child-index paths), once the scene is live: each member is paired with the prefab's (by position, as the old spawn made them),
    // takes its derived id, and its values that differ from a fresh spawn become overrides; the overrides' paths become addresses; the entities
    // of the scene under the instance that are no member stay what they are (entities of the scene, under their member); members the prefab
    // gained since are spawned. Nothing is written: the old member files go, and the recipe is written, at the next save of the scene. An
    // instance whose prefab does not load is left as it is. Returns how many were converted.
    //-----------------------------------------------------------------------------------------
    inline bool ConvertOldInstance( xecs::scene::mgr& Mgr, xecs::scene::instance& Scene, xecs::scene::permanent_id RootId ) noexcept
    {
        auto& GameMgr = Mgr.m_GameMgr;
        auto  RootIt  = Scene.m_LocalToRuntime.find(RootId);
        if( RootIt == Scene.m_LocalToRuntime.end() ) return false;
        const auto Root = RootIt->second;
        auto* pPI = details::LiveComponent<xecs::editor::prefab_instance>( GameMgr, Root );
        if( pPI == nullptr || pPI->m_Format != 0 ) return false;
        const auto Old = *pPI;

        const auto* pBaked = GameMgr.m_PrefabMgr.getBaked( Old.m_PrefabInstance );
        if( pBaked == nullptr )
        {
            std::printf("[Scene::Convert] WARNING: instance %s is left as it was saved: its prefab did not load\n", xecs::scene::FormatPermanentId(RootId).c_str());
            std::fflush(stdout);
            return false;
        }
        const auto& P = pBaked->m_Plan;

        // the old removals: child indices of a fresh spawn of the prefab
        std::vector<bool> Removed( P.m_Nodes.size(), false );
        for( auto& D : Old.m_HierarchyDiffs )
        {
            if( D.m_bAdded || D.m_MemberPath.empty() ) continue;
            int i = 0;
            bool bOk = true;
            for( auto Index : D.m_MemberPath )
            {
                if( Index >= P.m_Nodes[i].m_Children.size() ) { bOk = false; break; }
                i = P.m_Nodes[i].m_Children[Index];
            }
            if( bOk && i > 0 ) Removed[i] = true;
        }
        for( std::size_t i = 1; i < P.m_Nodes.size(); ++i ) if( Removed[ P.m_Nodes[i].m_Parent ] ) Removed[i] = true;

        const auto ByNode = PairLive( GameMgr, P, Root, &Removed, nullptr );

        xecs::editor::prefab_instance Recipe;
        Recipe.m_PrefabInstance = Old.m_PrefabInstance;
        Recipe.m_ComponentDiffs = Old.m_ComponentDiffs;         // the old ones were the root's
        for( auto& D : Recipe.m_ComponentDiffs ) D.m_Member.clear();
        for( auto C : Old.m_lComponents )
        {
            if( false == C.m_MemberPath.empty() )
            {
                // the old path walked the live children of the instance
                auto Cur = Root;
                for( auto Index : C.m_MemberPath )
                {
                    auto Kids = details::ChildrenOf( GameMgr, Cur );
                    Cur = Index < Kids.size() ? Kids[Index] : xecs::component::entity{};
                    if( false == Cur.isValid() ) break;
                }
                int Found = -1;
                for( std::size_t i = 1; i < ByNode.size(); ++i ) if( Cur.isValid() && ByNode[i].m_Value == Cur.m_Value ) { Found = static_cast<int>(i); break; }
                if( Found < 0 ) continue;           // an entity of the scene under the instance (not a member): its file holds its values
                C.m_Member = P.m_Nodes[Found].m_Address;
                C.m_MemberPath.clear();
            }
            Recipe.m_lComponents.push_back( std::move(C) );
        }
        for( std::size_t i = 1; i < P.m_Nodes.size(); ++i )
            if( Removed[i] && false == Removed[ P.m_Nodes[i].m_Parent ] ) Recipe.m_HierarchyDiffs.push_back({ .m_Member = P.m_Nodes[i].m_Address, .m_bAdded = false });

        // a member's file held all its values: those that differ from a fresh spawn of the prefab are overrides now
        {
            std::vector<xecs::scene::details::staged_entity> Fresh;
            std::vector<int>                                 Nodes;
            const xecs::editor::prefab_instance              Nothing{ .m_PrefabInstance = Old.m_PrefabInstance };
            StageMembers( GameMgr, *pBaked, Nothing, RootId, Fresh, Nodes, [&]( int i ) noexcept { return ByNode[i].isValid(); } );     // the nested recipes are in the bake

            for( std::size_t k = 0; k < Fresh.size(); ++k )
            {
                const auto& Address = P.m_Nodes[Nodes[k]].m_Address;
                const auto  Live    = ByNode[Nodes[k]];
                for( auto pInfo : details::GatherInfos(GameMgr, Live) )
                {
                    if( pInfo->m_pPropertyTable == nullptr || pInfo->m_TypeID == xecs::component::type::id::TAG || details::IsStructural(pInfo) ) continue;
                    auto* pFresh = Fresh[k].m_pStaged->find(*pInfo);
                    auto* pLive  = xecs::persist::details::ResolveLiveComponentPointer( GameMgr, Live, *pInfo );
                    if( pFresh == nullptr || pLive == nullptr ) continue;

                    std::unordered_map<std::string, std::string> FreshText;
                    xproperty::settings::context Context{};
                    xproperty::sprop::collector( pFresh, *pInfo->m_pPropertyTable, Context, [&]( const char* pName, xproperty::any&& V, const xproperty::type::members&, bool, const void* ) noexcept
                    {
                        std::string T;
                        if( false == details::IsEntityValue(V) && details::ValueText(V, T) ) FreshText[pName] = std::move(T);
                    });
                    xproperty::sprop::collector( pLive, *pInfo->m_pPropertyTable, Context, [&]( const char* pName, xproperty::any&& V, const xproperty::type::members& Member, bool bConst, const void* pInstance ) noexcept
                    {
                        if( details::IsEntityValue(V) || false == details::IsSaved(Member, bConst, pInstance) ) return;
                        std::string T;
                        if( false == details::ValueText(V, T) ) return;
                        auto It = FreshText.find(pName);
                        if( It == FreshText.end() || It->second == T ) return;
                        auto& Entry = details::EntryFor( Recipe, pInfo->m_Guid.m_Value, Address );
                        if( std::none_of( Entry.m_PropertyOverrides.begin(), Entry.m_PropertyOverrides.end(), [&]( auto& O ) noexcept { return O.m_PropertyName == pName; } ) )
                            Entry.m_PropertyOverrides.push_back({ .m_PropertyName = pName, .m_PropertyValueAsString = std::move(T) });
                    });
                }
            }
        }

        // every member takes its derived id
        std::unordered_set<std::uint64_t> Moved;
        for( std::size_t i = 1; i < P.m_Nodes.size(); ++i )
        {
            if( false == ByNode[i].isValid() ) continue;
            auto It = Scene.m_RuntimeToLocal.find( ByNode[i].m_Value );
            if( It == Scene.m_RuntimeToLocal.end() ) continue;
            const auto OldId = It->second;
            const auto NewId = xecs::scene::DeriveMemberId( RootId, P.m_Nodes[i].m_Address );
            if( Scene.m_LocalToRuntime.contains(NewId) )
            {
                std::printf("[Scene::Convert] WARNING: the id %s derived for member %s of instance %s is already taken - it keeps its id\n", xecs::scene::FormatPermanentId(NewId).c_str(), xecs::scene::FormatPermanentId(OldId).c_str(), xecs::scene::FormatPermanentId(RootId).c_str());
                std::fflush(stdout);
                continue;
            }
            details::ReId( Mgr, Scene, OldId, NewId, xecs::scene::instance_member{ .m_Root = RootId, .m_Address = P.m_Nodes[i].m_Address, .m_Name = TemplateName(P, static_cast<int>(i)) } );
            Moved.insert( ByNode[i].m_Value );
        }
        details::MarkReferencesDirty( Mgr, Scene, Moved );

        if( auto* pLivePI = details::LiveComponent<xecs::editor::prefab_instance>( GameMgr, Root ) ) *pLivePI = std::move(Recipe);
        SpawnMissingMembers( GameMgr, Scene, RootId );
        Mgr.MarkEntityDirty( Scene.m_Guid, RootId );

        std::printf("[Scene::Convert] instance %s of prefab %llX: %zu member(s) now addressed by their prefab ids\n", xecs::scene::FormatPermanentId(RootId).c_str(), static_cast<unsigned long long>(Old.m_PrefabInstance.m_Instance.m_Value), Moved.size());
        std::fflush(stdout);
        return true;
    }

    inline int ConvertOldInstances( xecs::scene::mgr& Mgr, xecs::scene::instance& Scene ) noexcept
    {
        auto& GameMgr = Mgr.m_GameMgr;
        std::vector<std::pair<int, xecs::scene::permanent_id>> Olds;          // (depth, id): the outer instances first, the ones they contain become members
        for( auto& [Id, E] : Scene.m_LocalToRuntime )
        {
            auto* pPI = details::LiveComponent<xecs::editor::prefab_instance>( GameMgr, E );
            if( pPI == nullptr || pPI->m_Format != 0 ) continue;
            int  Depth = 0;
            for( auto Cur = E; ; ++Depth )
            {
                auto* pParent = details::LiveComponent<xecs::component::parent>( GameMgr, Cur );
                if( pParent == nullptr || false == pParent->m_Value.isValid() || Depth > 1000 ) break;
                Cur = pParent->m_Value;
            }
            Olds.push_back({ Depth, Id });
        }
        std::sort( Olds.begin(), Olds.end() );
        int n = 0;
        for( auto& [Depth, Id] : Olds )
        {
            if( Scene.m_InstanceMembers.contains(Id) ) continue;       // it is a member of an instance converted before it
            if( ConvertOldInstance(Mgr, Scene, Id) ) ++n;
        }
        return n;
    }

    //-----------------------------------------------------------------------------------------
    // Live update (documentation/Editors/prefabs_plan.md 3.6, phase 6): the instances in a world's scenes that are made of a prefab that changed (it
    // is theirs, or one they nest) are spawned again from it with their recipes - what a save and a load of their scene would make of them, without
    // either: nothing is written to the scene's files and nothing is marked to be (the scene does not turn dirty). Members keep their ids (derived),
    // so the references to them, the selection and undo still name them; the entities of the scene under a member stay, under the member as it is
    // now; a member the prefab lost is gone, and a reference to it is null.
    // In two halves, because a recipe is refreshed against the template it was made from (a component or a member the prefab gains must not look
    // like one the instance removed): PrepareLiveUpdate, before the template changes, refreshes each such recipe and writes each such instance's
    // root as its scene's file would hold it, to a folder of its own; FinishLiveUpdate, after the change, reads them back against the prefab as it is
    // now and replaces each instance. bDropTemplates: the change is on disk (another editor saved it), the templates of the changed prefabs are read
    // again; otherwise the templates in memory are already the new ones (an Apply, its undo). A Play world is not for this: a running game keeps
    // what it started with (the editor does not call it there).
    //-----------------------------------------------------------------------------------------
    struct live_update
    {
        struct item
        {
            xecs::scene::guid           m_Scene{};
            xecs::scene::permanent_id   m_Root = xecs::scene::invalid_permanent_id_v;
            std::wstring                m_File;
        };
        struct renamed                                          // an entity of a scene that took another id in between (Apply: an entity that joined the prefab)
        {
            xecs::scene::guid           m_Scene{};
            xecs::scene::permanent_id   m_Old = xecs::scene::invalid_permanent_id_v;
            xecs::scene::permanent_id   m_New = xecs::scene::invalid_permanent_id_v;
        };

        std::vector<xecs::prefab::guid> m_Changed;
        std::vector<item>               m_Items;
        std::vector<renamed>            m_Renamed;
        std::wstring                    m_Folder;               // removed with this

        live_update() = default;
        live_update( const live_update& ) = delete;
        live_update& operator=( const live_update& ) = delete;
        live_update( live_update&& O ) noexcept : m_Changed(std::move(O.m_Changed)), m_Items(std::move(O.m_Items)), m_Renamed(std::move(O.m_Renamed)), m_Folder(std::exchange(O.m_Folder, {})) {}
       ~live_update() { if( false == m_Folder.empty() ) { std::error_code Ec; std::filesystem::remove_all( std::filesystem::path(m_Folder), Ec ); } }
    };

    // ExceptScene/ExceptRoot: an instance left out (the one an Apply comes from: it is the change).
    inline live_update PrepareLiveUpdate( xecs::scene::mgr& Mgr, std::span<const xecs::prefab::guid> Changed, xecs::scene::guid ExceptScene = {}, xecs::scene::permanent_id ExceptRoot = xecs::scene::invalid_permanent_id_v ) noexcept
    {
        live_update U;
        U.m_Changed.assign( Changed.begin(), Changed.end() );
        auto&      GameMgr   = Mgr.m_GameMgr;
        const auto IsChanged = [&]( xecs::prefab::guid G ) noexcept { return std::ranges::any_of( Changed, [&]( const xecs::prefab::guid& C ) noexcept { return C.m_Instance.m_Value == G.m_Instance.m_Value; } ); };

        for( auto& pScene : Mgr.m_SceneInstances )
        {
            if( pScene == nullptr || pScene->m_State != xecs::scene::state::Active ) continue;
            auto& Scene = *pScene;

            std::vector<std::pair<xecs::scene::permanent_id, xecs::prefab::guid>> Roots;
            for( auto& [Id, E] : Scene.m_LocalToRuntime )
            {
                if( Scene.m_InstanceMembers.contains(Id) || false == GameMgr.m_ComponentMgr.isEntityValid(E) ) continue;
                if( Id == ExceptRoot && Scene.m_Guid == ExceptScene ) continue;
                auto* pPI = details::LiveComponent<xecs::editor::prefab_instance>( GameMgr, E );
                if( pPI && pPI->m_Format != 0 ) Roots.push_back({ Id, pPI->m_PrefabInstance });
            }
            std::ranges::sort( Roots, {}, &std::pair<xecs::scene::permanent_id, xecs::prefab::guid>::first );

            // As the scene writes a reference; what it cannot write (a dead entity) is null, as a save and a load would leave it.
            const auto Resolve = [&]( xecs::component::entity T, std::int64_t& Out ) noexcept
            {
                if( xecs::scene::details::ResolveReferenceInScene( Mgr, Scene, T, Out ) ) return true;
                Out = 0;
                return true;
            };

            for( auto& [Id, Guid] : Roots )
            {
                const auto* pBaked = GameMgr.m_PrefabMgr.getBaked( Guid );
                if( pBaked == nullptr ) continue;
                if( std::ranges::none_of( pBaked->m_Plan.m_Levels, [&]( const level& L ) noexcept { return IsChanged(L.m_Prefab); } ) ) continue;

                RefreshRecipe( GameMgr, Scene, Id, Resolve );
                if( U.m_Folder.empty() )
                {
                    static std::atomic<std::uint32_t> s_Count{ 0 };
                    std::error_code Ec;
                    U.m_Folder = ( std::filesystem::temp_directory_path(Ec) / std::format( L"xlion_live_update_{:X}_{}", reinterpret_cast<std::uintptr_t>(&Mgr), ++s_Count ) ).wstring();
                    std::filesystem::remove_all( std::filesystem::path(U.m_Folder), Ec );
                    std::filesystem::create_directories( std::filesystem::path(U.m_Folder), Ec );
                }
                live_update::item Item{ .m_Scene = Scene.m_Guid, .m_Root = Id, .m_File = std::format( L"{}/{:X}_{:X}.entity", U.m_Folder, Scene.m_Guid.m_Instance.m_Value, Id ) };
                if( auto Err = xecs::scene::details::WriteEntityFile( GameMgr, Item.m_File, Id, Scene.m_LocalToRuntime.at(Id), Resolve ); Err )
                {
                    std::printf("[Prefab::LiveUpdate] WARNING: instance %s could not be written (%s) - it is left as it is\n", xecs::scene::FormatPermanentId(Id).c_str(), std::string(Err.getMessage()).c_str());
                    std::fflush(stdout);
                    continue;
                }
                U.m_Items.push_back( std::move(Item) );
            }
        }
        return U;
    }

    // Returns how many instances were spawned again.
    inline int FinishLiveUpdate( xecs::scene::mgr& Mgr, live_update& U, bool bDropTemplates ) noexcept
    {
        auto& GameMgr = Mgr.m_GameMgr;
        auto& Prefabs = GameMgr.m_PrefabMgr;
        if( bDropTemplates ) for( auto& G : U.m_Changed ) Prefabs.DropTemplate( G );
        Prefabs.InvalidateBaked();
        if( U.m_Items.empty() ) return 0;

        std::unordered_map<std::uint64_t, xecs::component::entity> Moved;     // an entity of an instance that was replaced -> the one that replaces it (invalid: gone)
        std::vector<xecs::scene::instance*>                         Touched;
        int nDone = 0;
        for( auto& Item : U.m_Items )
        {
            auto* pScene = Mgr.Find( Item.m_Scene );
            if( pScene == nullptr ) continue;
            auto& Scene  = *pScene;
            auto  RootIt = Scene.m_LocalToRuntime.find( Item.m_Root );
            if( RootIt == Scene.m_LocalToRuntime.end() ) continue;

            // The new instance is staged first: when it cannot be made (its prefab no longer loads) the old one stays as it is.
            std::vector<xecs::scene::details::staged_entity> Staged(1);
            if( auto Err = xecs::scene::details::ReadEntityFile( GameMgr, Item.m_File, Staged[0] ); Err || Staged[0].m_Id != Item.m_Root )
            {
                std::printf("[Prefab::LiveUpdate] WARNING: instance %s could not be read back - it is left as it is\n", xecs::scene::FormatPermanentId(Item.m_Root).c_str());
                std::fflush(stdout);
                continue;
            }
            const auto* pPI = Staged[0].m_pStaged->find<xecs::editor::prefab_instance>();
            if( pPI == nullptr || Prefabs.getBaked( pPI->m_PrefabInstance ) == nullptr )
            {
                std::printf("[Prefab::LiveUpdate] WARNING: the prefab of instance %s does not load - it is left as it is\n", xecs::scene::FormatPermanentId(Item.m_Root).c_str());
                std::fflush(stdout);
                continue;
            }
            std::unordered_map<xecs::scene::permanent_id, std::size_t>                      Index{ { Item.m_Root, 0 } };
            std::vector<std::pair<xecs::scene::permanent_id, xecs::scene::instance_member>> Members;
            StageSceneInstances( GameMgr, Staged, Index, Members );

            // The old one leaves the world: its root and its members (the entities of the scene under them stay). A member's name that is the one it
            // was spawned with goes too (the prefab may have renamed it); a rename stays.
            std::vector<std::pair<xecs::scene::permanent_id, xecs::component::entity>> Old{ { Item.m_Root, RootIt->second } };
            for( auto& [Id, M] : Scene.m_InstanceMembers )
            {
                if( M.m_Root != Item.m_Root ) continue;
                auto It = Scene.m_LocalToRuntime.find(Id);
                Old.push_back({ Id, It == Scene.m_LocalToRuntime.end() ? xecs::component::entity{} : It->second });
                if( auto Name = Scene.m_EntityNames.find(Id); Name != Scene.m_EntityNames.end() && Name->second == M.m_Name ) Scene.m_EntityNames.erase(Name);
            }
            for( auto& [Id, E] : Old )
            {
                Scene.m_LocalToRuntime.erase(Id);
                Scene.m_InstanceMembers.erase(Id);
                if( false == E.isValid() ) continue;
                Scene.m_RuntimeToLocal.erase(E.m_Value);
                if( GameMgr.m_ComponentMgr.isEntityValid(E) ) { auto Dead = E; GameMgr.DeleteEntity(Dead); }
            }

            // The new one: the root under its id, the members under their derived ids.
            std::vector<xecs::component::entity> Made;
            for( auto& S : Staged )
            {
                if( S.m_Id != Item.m_Root && Scene.m_LocalToRuntime.contains(S.m_Id) )
                {
                    std::printf("[Prefab::LiveUpdate] WARNING: the id %s derived for a member of instance %s is already taken - that member is left out\n", xecs::scene::FormatPermanentId(S.m_Id).c_str(), xecs::scene::FormatPermanentId(Item.m_Root).c_str());
                    std::fflush(stdout);
                    std::erase_if( Members, [&]( auto& M ) noexcept { return M.first == S.m_Id; } );
                    continue;
                }
                const auto Id = S.m_Id;
                xecs::scene::details::CreateStagedEntity( GameMgr, Scene, S );
                Made.push_back( Scene.m_LocalToRuntime.at(Id) );
            }
            RegisterMembers( Scene, Members );
            for( auto E : Made )
                xecs::persist::details::RemapLoadedEntityReferences( GameMgr, E, [&]( std::int64_t Encoded ) noexcept
                {
                    for( auto& R : U.m_Renamed ) if( Encoded > 0 && R.m_Scene == Scene.m_Guid && static_cast<xecs::scene::permanent_id>(Encoded) == R.m_Old ) Encoded = static_cast<std::int64_t>(R.m_New);
                    return details::ResolveInScene( Scene, Encoded );
                });

            for( auto& [Id, E] : Old )
            {
                if( false == E.isValid() ) continue;
                auto It = Scene.m_LocalToRuntime.find(Id);
                Moved[E.m_Value] = It == Scene.m_LocalToRuntime.end() ? xecs::component::entity{} : It->second;
            }
            if( std::ranges::find( Touched, pScene ) == Touched.end() ) Touched.push_back( pScene );
            ++nDone;
        }

        // What referenced an entity that was replaced references the one that replaces it, in every scene of the world: an instance's parent (its
        // children list), an entity of the scene under a member (its parent), any other reference, the scenes' tables of external entities.
        if( false == Moved.empty() )
        {
            const auto Remap = [&]( xecs::component::entity R ) noexcept { auto It = R.isValid() ? Moved.find(R.m_Value) : Moved.end(); return It == Moved.end() ? R : It->second; };
            for( auto& pScene : Mgr.m_SceneInstances )
            {
                if( pScene == nullptr ) continue;
                for( auto& [Id, E] : pScene->m_LocalToRuntime )
                    if( GameMgr.m_ComponentMgr.isEntityValid(E) ) xecs::persist::details::RemapEntityReferences( GameMgr, E, Remap );
                for( auto& X : pScene->m_ExternalToRuntime ) X = Remap(X);
            }
        }
        for( auto* pScene : Touched ) LinkChildren( GameMgr, *pScene );
        GameMgr.m_ArchetypeMgr.UpdateStructuralChanges();
        return nDone;
    }

    inline int LiveUpdate( xecs::scene::mgr& Mgr, std::span<const xecs::prefab::guid> Changed, bool bDropTemplates ) noexcept
    {
        auto U = PrepareLiveUpdate( Mgr, Changed );
        return FinishLiveUpdate( Mgr, U, bDropTemplates );
    }

    //-----------------------------------------------------------------------------------------
    // Unity's "Apply to Prefab": what this one instance does differently becomes its prefab's, and the prefab is saved. Property overrides are
    // written into the template members (a reference to a member of the instance becomes a reference to the prefab's member it stands for; one to
    // anything else cannot be kept by a prefab and is null); component diffs add and remove the components (with their data); the members the
    // instance removed leave the prefab; the entities of the scene under the instance join it, and become members of the instance (they take
    // their derived ids). What concerns a member of a nested instance is written into that nested instance's recipe in the prefab. What addresses
    // a member the prefab no longer has stays on the instance. The other instances in this world get the change where they did not override it
    // (Unity), at once: they are spawned again (live update, above); the editor tells the other worlds when the file is written.
    //-----------------------------------------------------------------------------------------
    namespace details
    {
        // A component the template entity should gain (with the live entity's data) or lose.
        inline bool CarryComponent( xecs::game_mgr::instance& GameMgr, xecs::component::entity Live, xecs::component::entity Template, const xecs::component::type::info& Info, bool bAdd ) noexcept
        {
            const xecs::component::type::info* const One[] = { &Info };
            const auto Moved = bAdd ? GameMgr.AddOrRemoveComponents( Template, { One, 1 }, {} ) : GameMgr.AddOrRemoveComponents( Template, {}, { One, 1 } );
            if( Moved.isZombie() || Moved.m_Value != Template.m_Value ) return false;
            if( false == bAdd || Info.m_TypeID == xecs::component::type::id::TAG ) return true;
            auto* pSrc = xecs::persist::details::ResolveLiveComponentPointer( GameMgr, Live, Info );
            if( pSrc == nullptr ) return true;
            if( Info.m_TypeID == xecs::component::type::id::SHARE ) { GameMgr.ReinternShareComponent( Template, Info, pSrc ); return true; }
            auto* pDst = xecs::persist::details::ResolveLiveComponentPointer( GameMgr, Template, Info );
            if( pDst == nullptr ) return true;
            if( Info.m_pCopyFn ) Info.m_pCopyFn( pDst, pSrc );
            else                 std::memcpy( pDst, pSrc, Info.m_Size );
            return true;
        }
    }

    inline xerr ApplyToPrefab( xecs::scene::mgr& Mgr, xecs::scene::instance& Scene, xecs::scene::permanent_id RootId ) noexcept
    {
        auto& GameMgr = Mgr.m_GameMgr;
        auto& Prefabs = GameMgr.m_PrefabMgr;
        auto  RootIt  = Scene.m_LocalToRuntime.find(RootId);
        if( RootIt == Scene.m_LocalToRuntime.end() ) return xerr::create<xecs::game_mgr::state::FAILURE, "ApplyToPrefab: the instance is not in the scene">();
        const auto Root = RootIt->second;
        auto* pLivePI = details::LiveComponent<xecs::editor::prefab_instance>( GameMgr, Root );
        if( pLivePI == nullptr ) return xerr::create<xecs::game_mgr::state::FAILURE, "ApplyToPrefab: the entity is not a prefab instance">();
        if( pLivePI->m_Format == 0 ) return xerr::create<xecs::game_mgr::state::FAILURE, "ApplyToPrefab: the instance was saved before recipes and is not converted">();
        if( Scene.m_InstanceMembers.contains(RootId) ) return xerr::create<xecs::game_mgr::state::FAILURE, "ApplyToPrefab: the entity is a member of another instance (apply that instance)">();

        RefreshRecipe( GameMgr, Scene, RootId, [&]( xecs::component::entity T, std::int64_t& Out ) noexcept { return xecs::scene::details::ResolveReferenceInScene( Mgr, Scene, T, Out ); } );
        const auto PI = *details::LiveComponent<xecs::editor::prefab_instance>( GameMgr, Root );
        const auto Guid = PI.m_PrefabInstance;

        // The other instances of the prefab in this world (and those of prefabs that nest it) get the change once it is made (live update): their
        // recipes are refreshed now, against the prefab as it is before.
        auto Update = PrepareLiveUpdate( Mgr, std::span<const xecs::prefab::guid>( &Guid, 1 ), Scene.m_Guid, RootId );

        plan P;
        if( auto Err = MakePlan(GameMgr, Guid, P); Err ) return Err;
        auto& Group = Prefabs.m_PrefabGroups[Guid.m_Instance.m_Value];
        const auto PrefabRoot = P.m_Levels[0].m_TemplateRoot;

        // Where an address goes in the prefab: a template entity of the prefab to write (and, when it is itself a nested instance, whose recipe
        // also records it at its root), or a nested instance of the prefab whose recipe records it at Rel.
        struct target { xecs::component::entity m_Template; address m_Rel; bool m_bWrite; };
        const auto Target = [&]( std::span<const std::uint64_t> A ) noexcept -> std::optional<target>
        {
            const int i = P.Find(A);
            if( i < 0 ) return std::nullopt;
            if( P.m_Nodes[i].m_Level == 0 ) return target{ P.m_Nodes[i].m_Template, {}, true };
            int k = P.m_Nodes[i].m_Level;
            while( P.m_Levels[k].m_OwnerLevel != 0 ) k = P.m_Levels[k].m_OwnerLevel;
            const auto& Owner = P.m_Nodes[ P.m_Levels[k].m_Owner ];
            return target{ Owner.m_Template, address( A.begin() + Owner.m_Address.size(), A.end() ), false };
        };
        const auto RecipeOf = [&]( xecs::component::entity T ) noexcept { return details::LiveComponent<xecs::editor::prefab_instance>( GameMgr, T ); };

        // the template entity a live entity of the scene stands for (the instance's root, or a member of the prefab itself)
        std::unordered_map<std::uint64_t, xecs::component::entity> Known;
        Known[Root.m_Value] = PrefabRoot;
        for( auto& N : P.m_Nodes )
            if( N.m_Level == 0 && false == N.m_Address.empty() )
                if( auto E = details::FindMember(Scene, RootId, N.m_Address); E.isValid() ) Known[E.m_Value] = N.m_Template;
        const auto TemplateOf = [&]( xecs::component::entity R ) noexcept -> xecs::component::entity
        {
            if( false == R.isValid() ) return {};
            auto It = Known.find(R.m_Value);
            if( It != Known.end() ) return It->second;
            std::printf("[Prefab::Apply] WARNING: an override references an entity that is not a member of the prefab - the prefab keeps a null reference\n");
            std::fflush(stdout);
            return {};
        };
        const auto GroupText = [&]( xecs::component::entity T ) noexcept
        {
            auto It = T.isValid() ? Group.m_RuntimeToLocal.find(T.m_Value) : Group.m_RuntimeToLocal.end();
            return details::EncodeEntityText( It == Group.m_RuntimeToLocal.end() ? 0 : static_cast<std::int64_t>(It->second) );
        };

        // 1) the components of the root and of the members
        {
            const auto Comparable = []( const xecs::component::type::info* p ) noexcept { return false == details::IsStructural(p) && false == p->m_bBuilder && false == xecs::component::type::IsComponentType<xecs::editor::prefab_instance>(p); };
            const auto RootLive = details::GatherInfos( GameMgr, Root );
            const auto RootTpl  = details::GatherInfos( GameMgr, PrefabRoot );
            for( auto p : RootLive ) if( Comparable(p) && !details::Has(RootTpl, p->m_Guid.m_Value) ) if( !details::CarryComponent(GameMgr, Root, PrefabRoot, *p, true) )  return xerr::create<xecs::game_mgr::state::FAILURE, "ApplyToPrefab: could not add a component to the prefab root">();
            for( auto p : RootTpl  ) if( Comparable(p) && !details::Has(RootLive, p->m_Guid.m_Value) ) if( !details::CarryComponent(GameMgr, Root, PrefabRoot, *p, false) ) return xerr::create<xecs::game_mgr::state::FAILURE, "ApplyToPrefab: could not remove a component from the prefab root">();
        }
        for( auto& D : PI.m_ComponentDiffs )
        {
            if( D.m_Member.empty() ) continue;
            auto T    = Target(D.m_Member);
            auto* pInfo = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{D.m_ComponentTypeGuid} );
            if( !T || pInfo == nullptr ) continue;
            if( T->m_bWrite )
            {
                details::CarryComponent( GameMgr, details::FindMember(Scene, RootId, D.m_Member), T->m_Template, *pInfo, D.m_bAdded );
                continue;
            }
            if( auto* pNested = RecipeOf(T->m_Template) )
            {
                std::erase_if( pNested->m_ComponentDiffs, [&]( auto& X ) noexcept { return X.m_ComponentTypeGuid == D.m_ComponentTypeGuid && std::ranges::equal(X.m_Member, T->m_Rel); } );
                pNested->m_ComponentDiffs.push_back({ .m_ComponentTypeGuid = D.m_ComponentTypeGuid, .m_bAdded = D.m_bAdded, .m_Member = T->m_Rel });
            }
        }

        // 2) the property overrides
        for( auto& C : PI.m_lComponents )
        {
            auto T = Target(C.m_Member);
            auto* pInfo = xecs::component::mgr::findComponentTypeInfo( xecs::component::type::guid{C.m_ComponentTypeGuid} );
            if( !T || pInfo == nullptr || pInfo->m_pPropertyTable == nullptr ) continue;
            const auto Live = C.m_Member.empty() ? Root : details::FindMember( Scene, RootId, C.m_Member );
            auto* pLive = Live.isValid() ? xecs::persist::details::ResolveLiveComponentPointer( GameMgr, Live, *pInfo ) : nullptr;
            if( pLive == nullptr ) continue;            // a member the instance removed: its overrides stay with it

            std::byte* pTpl = nullptr;
            if( T->m_bWrite )
            {
                auto& D = GameMgr.m_ComponentMgr.getEntityDetails(T->m_Template);
                const auto iType = D.m_pPool ? D.m_pPool->findIndexComponentFromInfo(*pInfo) : -1;
                if( iType >= 0 ) pTpl = &D.m_pPool->m_pComponent[iType][ D.m_PoolIndex.m_Value * pInfo->m_Size ];
            }
            const bool bRecord = false == T->m_bWrite || RecipeOf(T->m_Template) != nullptr;

            for( auto& Prop : C.m_PropertyOverrides )
            {
                xproperty::settings::context Context{};
                xproperty::any Value;
                bool bFound = false;
                xproperty::sprop::collector( pLive, *pInfo->m_pPropertyTable, Context, [&]( const char* pName, xproperty::any&& V, const xproperty::type::members&, bool, const void* ) noexcept
                {
                    if( Prop.m_PropertyName == pName ) { Value = std::move(V); bFound = true; }
                });
                if( false == bFound ) continue;

                std::string Text;
                if( details::IsEntityValue(Value) )
                {
                    const auto Mapped = TemplateOf( Value.get<xecs::component::entity>() );
                    Value.get<xecs::component::entity>() = Mapped;
                    Text = GroupText(Mapped);
                }
                else if( false == details::ValueText(Value, Text) ) Text.clear();

                if( pTpl )
                {
                    std::string SetError;
                    xproperty::sprop::setProperty( SetError, pTpl, *pInfo->m_pPropertyTable, xproperty::sprop::container::prop{ Prop.m_PropertyName, Value }, Context );
                }
                if( bRecord && false == Text.empty() )
                    if( auto* pNested = RecipeOf(T->m_Template) )
                        details::SetText( details::EntryFor(*pNested, C.m_ComponentTypeGuid, T->m_Rel), Prop.m_PropertyName.c_str(), Text );
            }
        }

        // 3) the members the instance removed
        for( auto& D : PI.m_HierarchyDiffs )
        {
            if( D.m_bAdded ) continue;
            auto T = Target(D.m_Member);
            if( !T ) continue;
            if( T->m_bWrite )
            {
                if( GameMgr.m_ComponentMgr.isEntityValid(T->m_Template) ) xecs::persist::details::DeleteEntitySubtreeUnregistered( GameMgr, T->m_Template );
            }
            else if( auto* pNested = RecipeOf(T->m_Template) )
            {
                if( false == details::IsRemovedBy(*pNested, T->m_Rel) ) pNested->m_HierarchyDiffs.push_back({ .m_Member = T->m_Rel, .m_bAdded = false });
            }
        }

        // 4) the entities of the scene under the instance join the prefab, under the template of their parent
        struct joined { xecs::component::entity m_Source; std::unordered_map<std::uint64_t, xecs::component::entity> m_Clones; };
        std::vector<joined> Joined;
        {
            std::vector<std::pair<xecs::component::entity, xecs::component::entity>> Adds;      // (source, template parent)
            const auto Collect = [&]( xecs::component::entity Live, std::span<const std::uint64_t> A ) noexcept
            {
                for( auto K : details::ChildrenOf(GameMgr, Live) )
                {
                    auto It = Scene.m_RuntimeToLocal.find(K.m_Value);
                    if( It == Scene.m_RuntimeToLocal.end() || Scene.m_InstanceMembers.contains(It->second) ) continue;
                    auto T = Target(A);
                    if( T && T->m_bWrite && RecipeOf(T->m_Template) == nullptr ) Adds.push_back({ K, T->m_Template });
                    else
                    {
                        std::printf("[Prefab::Apply] WARNING: entity %s is under a member of a nested instance - it stays an entity of the scene\n", xecs::scene::FormatPermanentId(It->second).c_str());
                        std::fflush(stdout);
                    }
                }
            };
            Collect( Root, {} );
            for( auto& N : P.m_Nodes )
                if( false == N.m_Address.empty() )
                    if( auto E = details::FindMember(Scene, RootId, N.m_Address); E.isValid() ) Collect( E, N.m_Address );

            for( auto& [Source, TemplateParent] : Adds )
            {
                joined J{ Source, {} };
                const auto Clone = Prefabs.CloneSubtreeIntoPrefab( Source, Group, /*bIsRoot=*/false, &Known, &J.m_Clones, nullptr );
                if( false == Clone.isValid() ) continue;
                if( details::LiveComponent<xecs::component::children>(GameMgr, TemplateParent) == nullptr )
                {
                    const xecs::component::type::info* const AddKids[] = { &xecs::component::type::info_v<xecs::component::children> };
                    (void)GameMgr.AddOrRemoveComponents( TemplateParent, { AddKids, 1 }, {} );
                }
                details::LiveComponent<xecs::component::children>(GameMgr, TemplateParent)->m_List.push_back(Clone);
                if( auto* pParent = details::LiveComponent<xecs::component::parent>(GameMgr, Clone) ) pParent->m_Value = TemplateParent;
                Joined.push_back( std::move(J) );
            }
        }

        // A component carried with its data (step 1) can hold a reference to an entity of the scene: one to the instance's root or one of its
        // members becomes the prefab's member it stands for; anything else cannot be kept by a prefab and is null.
        {
            std::vector<xecs::component::entity> Members;
            for( auto& [Id, Member] : Group.m_LocalToRuntime ) if( GameMgr.m_ComponentMgr.isEntityValid(Member) ) Members.push_back(Member);
            for( auto Member : Members )
                xecs::persist::details::RemapWrittenReferences( GameMgr, Member, [&]( xecs::component::entity R ) noexcept { return xecs::persist::details::KeepReferenceInsidePrefab( GameMgr, Group, nullptr, R, &Known ); } );
        }

        // 5) the instance does not differ from its prefab any more, but in what addresses a member the prefab does not have
        if( auto* pPI = details::LiveComponent<xecs::editor::prefab_instance>( GameMgr, Root ) )
        {
            std::erase_if( pPI->m_lComponents,    [&]( auto& C ) noexcept { return P.Find(C.m_Member) >= 0; } );
            std::erase_if( pPI->m_ComponentDiffs, [&]( auto& D ) noexcept { return P.Find(D.m_Member) >= 0; } );
            pPI->m_HierarchyDiffs.clear();
        }

        if( auto Err = Prefabs.Save(Guid); Err ) return Err;

        // 6) the entities that joined the prefab are members of the instance now: their derived ids (an instance among them brings its members along)
        std::unordered_set<std::uint64_t> Moved;
        for( auto& J : Joined )
        {
            for( auto& [SourceValue, Clone] : J.m_Clones )
            {
                auto Src = Scene.m_RuntimeToLocal.find(SourceValue);
                auto Loc = Group.m_RuntimeToLocal.find(Clone.m_Value);
                if( Src == Scene.m_RuntimeToLocal.end() || Loc == Group.m_RuntimeToLocal.end() ) continue;
                const auto OldId   = Src->second;
                const address A    = { Loc->second };
                const auto NewId   = xecs::scene::DeriveMemberId( RootId, A );
                std::string Name;
                if( auto N = Group.m_EntityNames.find(Loc->second); N != Group.m_EntityNames.end() ) Name = N->second;

                // the members of an instance that joined are addressed through it now
                std::vector<std::pair<xecs::scene::permanent_id, xecs::scene::instance_member>> Inner;
                for( auto& [Id, M] : Scene.m_InstanceMembers ) if( M.m_Root == OldId ) Inner.push_back({ Id, M });

                details::ReId( Mgr, Scene, OldId, NewId, xecs::scene::instance_member{ .m_Root = RootId, .m_Address = A, .m_Name = Name } );
                Update.m_Renamed.push_back({ Scene.m_Guid, OldId, NewId });
                Moved.insert( SourceValue );
                for( auto& [Id, M] : Inner )
                {
                    address InnerA = A;
                    InnerA.insert( InnerA.end(), M.m_Address.begin(), M.m_Address.end() );
                    if( auto E = Scene.m_LocalToRuntime.find(Id); E != Scene.m_LocalToRuntime.end() ) Moved.insert( E->second.m_Value );
                    const auto InnerId = xecs::scene::DeriveMemberId(RootId, InnerA);
                    details::ReId( Mgr, Scene, Id, InnerId, xecs::scene::instance_member{ .m_Root = RootId, .m_Address = InnerA, .m_Name = M.m_Name } );
                    Update.m_Renamed.push_back({ Scene.m_Guid, Id, InnerId });
                }
            }
        }
        details::MarkReferencesDirty( Mgr, Scene, Moved );
        Mgr.MarkEntityDirty( Scene.m_Guid, RootId );

        // the other instances, from the prefab as it is now (in memory: what was saved, or what the editor that holds the prefab was handed)
        FinishLiveUpdate( Mgr, Update, /*bDropTemplates*/ false );
        return {};
    }
}

//---------------------------------------------------------------------------------------------
// The baked plan and the spawn (documentation/Editors/prefabs_plan.md 3.5, phase 4)
//---------------------------------------------------------------------------------------------
namespace xecs::prefab
{
    namespace recipe::details
    {
        // The bake stages its members as the members of an instance of this id: their references are those ids while it finds out which member
        // each names. Then every reference of a component is given a sentinel (its order in the low bits) to find where it is in the bytes.
        inline constexpr xecs::scene::permanent_id  bake_instance_id_v   = 1;
        inline constexpr std::uint64_t              bake_sentinel_v      = 0xB4CEB4CE00000000ull;
        inline constexpr std::uint64_t              bake_sentinel_mask_v = 0xFFFFFFFF00000000ull;
    }

    //-----------------------------------------------------------------------------------------

    void mgr::InvalidateBaked( void ) noexcept
    {
        for( auto& [Guid, pBaked] : m_Baked )
            for( auto& N : pBaked->m_Nodes )
                if( N.m_Entity.isValid() && m_GameMgr.m_ComponentMgr.isEntityValid(N.m_Entity) ) m_GameMgr.DeleteEntity(N.m_Entity);
        m_Baked.clear();
    }

    //-----------------------------------------------------------------------------------------
    // Bakes a prefab: its plan (nested prefabs expanded), each member staged from its template with the nested recipes applied (as a scene load
    // stages an instance's members), then the table of its references, and its data placed in an inert entity with every reference null.
    //-----------------------------------------------------------------------------------------
    xecs::component::entity mgr::FindBakedMember( guid PrefabGuid, std::span<const std::uint64_t> Address ) noexcept
    {
        auto* pBaked = getBaked( PrefabGuid );
        if( pBaked == nullptr ) return {};
        const int i = pBaked->m_Plan.Find( Address );
        return i < 0 ? xecs::component::entity{} : pBaked->m_Nodes[i].m_Entity;
    }

    baked* mgr::getBaked( guid PrefabGuid ) noexcept
    {
        if( auto It = m_Baked.find(PrefabGuid.m_Instance.m_Value); It != m_Baked.end() ) return It->second.get();
        if( EnsureLoaded(PrefabGuid) || false == m_PrefabGroups.contains(PrefabGuid.m_Instance.m_Value) ) return nullptr;

        using staged = xecs::persist::details::staged_components;
        constexpr auto Id = recipe::details::bake_instance_id_v;

        auto  pBaked = std::make_unique<baked>();
        auto& B      = *pBaked;
        if( recipe::MakePlan( m_GameMgr, PrefabGuid, B.m_Plan ) ) return nullptr;
        const auto& Plan = B.m_Plan;
        const int   n    = static_cast<int>(Plan.m_Nodes.size());

        std::unordered_map<xecs::scene::permanent_id, int> NodeOf;
        for( int i = 0; i < n; ++i ) NodeOf[ xecs::scene::DeriveMemberId(Id, Plan.m_Nodes[i].m_Address) ] = i;

        // 1) every member staged from its template, its references as member ids; then the nested recipes
        std::vector<std::unique_ptr<staged>> Staged( n );
        B.m_Nodes.resize( n );
        for( int i = 0; i < n; ++i )
        {
            auto& N  = Plan.m_Nodes[i];
            auto& BN = B.m_Nodes[i];
            BN.m_Infos = N.m_Infos;
            if( N.m_Parent >= 0 )                                   BN.m_Infos.push_back( &xecs::component::type::info_v<xecs::component::parent> );
            if( N.m_bHasChildren || false == N.m_Children.empty() ) BN.m_Infos.push_back( &xecs::component::type::info_v<xecs::component::children> );

            Staged[i] = std::make_unique<staged>( std::span{ BN.m_Infos.data(), BN.m_Infos.size() } );
            Staged[i]->CopyFrom( m_GameMgr, N.m_Template, { &xecs::component::type::info_v<xecs::component::children>, &xecs::component::type::info_v<xecs::component::parent> } );
            for( auto pInfo : BN.m_Infos )
            {
                if( pInfo->m_TypeID == xecs::component::type::id::TAG ) continue;
                if( auto* pData = Staged[i]->find(*pInfo) )
                    recipe::details::ForEachReference( *pInfo, pData, [&]( xecs::component::entity& R ) noexcept { R = recipe::details::EncodeTemplateReference(Plan, N.m_Level, Id, R); } );
            }
        }
        recipe::ApplyNestedOverrides( Plan, Id, [&]( const recipe::address& A ) noexcept -> staged*
        {
            auto It = NodeOf.find( xecs::scene::DeriveMemberId(Id, A) );
            return It == NodeOf.end() ? nullptr : Staged[It->second].get();
        });

        // 2) the references: which member each names, and where it is
        for( int i = 0; i < n; ++i )
        {
            auto& BN = B.m_Nodes[i];
            for( auto pInfo : BN.m_Infos )
            {
                if( pInfo->m_TypeID == xecs::component::type::id::TAG || pInfo->m_ReferenceMode == xecs::component::type::reference_mode::NO_REFERENCES ) continue;
                auto* pData = Staged[i]->find(*pInfo);
                if( pData == nullptr ) continue;

                std::vector<int> Targets;
                recipe::details::ForEachReference( *pInfo, pData, [&]( xecs::component::entity& R ) noexcept
                {
                    auto It = R.isValid() ? NodeOf.find( static_cast<xecs::scene::permanent_id>(xecs::persist::details::DecodeRef(R)) ) : NodeOf.end();
                    R.m_Value = recipe::details::bake_sentinel_v | Targets.size();
                    Targets.push_back( It == NodeOf.end() ? -1 : It->second );
                });
                if( Targets.empty() ) continue;

                std::vector<int> Offsets( Targets.size(), -1 );
                for( std::size_t o = 0; o + sizeof(xecs::component::entity) <= pInfo->m_Size; o += alignof(xecs::component::entity) )
                {
                    std::uint64_t V;
                    std::memcpy( &V, pData + o, sizeof(V) );
                    const auto j = V & ~recipe::details::bake_sentinel_mask_v;
                    if( (V & recipe::details::bake_sentinel_mask_v) == recipe::details::bake_sentinel_v && j < Targets.size() ) Offsets[j] = static_cast<int>(o);
                }
                if( std::none_of( Offsets.begin(), Offsets.end(), []( int o ) noexcept { return o < 0; } ) )
                {
                    for( std::size_t j = 0; j < Targets.size(); ++j ) BN.m_References.push_back({ pInfo, static_cast<std::uint32_t>(Offsets[j]), Targets[j] });
                }
                else BN.m_Indirect.push_back({ pInfo, std::move(Targets) });

                recipe::details::ForEachReference( *pInfo, pData, []( xecs::component::entity& R ) noexcept { R = xecs::component::entity{}; } );
            }
            BN.m_Columns.resize( 2 + BN.m_References.size() + BN.m_Indirect.size() );

            // 3) the inert entity a spawn copies
            auto Infos = BN.m_Infos;
            Infos.push_back( &xecs::component::type::info_v<xecs::prefab::tag> );
            BN.m_Entity = Staged[i]->Create( m_GameMgr.getOrCreateArchetype( std::span<const xecs::component::type::info* const>{ Infos.data(), Infos.size() } ) );
        }

        return ( m_Baked[PrefabGuid.m_Instance.m_Value] = std::move(pBaked) ).get();
    }

    //-----------------------------------------------------------------------------------------
    // Each member made Count times (one call when nothing builds it; staged and built one by one when builder systems take some of its
    // components), then every entity's parent, children and references written, then Callback on the roots.
    //-----------------------------------------------------------------------------------------
    template< typename T_CALLBACK >
    std::span<const xecs::component::entity> mgr::Spawn( guid PrefabGuid, int Count, T_CALLBACK&& Callback ) noexcept
    {
        if( Count <= 0 ) return {};
        auto* pBaked = getBaked( PrefabGuid );
        if( pBaked == nullptr ) return {};
        auto&       B = *pBaked;
        auto&       H = B.m_Spawned;
        const auto  n = B.m_Nodes.size();
        const auto  C = static_cast<std::size_t>(Count);
        H.resize( n * C );

        for( std::size_t i = 0; i < n; ++i )
        {
            auto& N    = B.m_Nodes[i];
            auto& Plan = m_GameMgr.getBuildPlan( std::span<const xecs::component::type::info* const>{ N.m_Infos.data(), N.m_Infos.size() } );
            auto* pOut = &H[i * C];
            if( Plan.m_BuilderInfos.empty() )
            {
                auto& A      = *Plan.m_pFinalArchetype;
                auto& From   = m_GameMgr.m_ComponentMgr.getEntityDetails(N.m_Entity);
                auto& Family = A.hasShareComponents() ? A.getOrCreatePoolFamily(*From.m_pPool->m_pMyFamily) : A.getOrCreatePoolFamily({}, {});
                std::size_t k = 0;
                A.CreateEntities( Family, Count, N.m_Entity, [&]( const xecs::component::entity& E ) noexcept { pOut[k++] = E; } );
            }
            else
            {
                // ponytail: a member builder systems build is staged one at a time (a few allocations each); the builders' own work dwarfs it
                if( !N.m_pScratch ) N.m_pScratch = std::make_unique<xecs::persist::details::staged_components>( std::span{ N.m_Infos.data(), N.m_Infos.size() } );
                for( std::size_t k = 0; k < C; ++k )
                {
                    N.m_pScratch->CopyFrom( m_GameMgr, N.m_Entity );
                    pOut[k] = N.m_pScratch->Create( m_GameMgr, Plan );
                }
            }
        }

        // the links: every entity of a node is in the same archetype, so the columns are found once per node
        for( std::size_t i = 0; i < n; ++i )
        {
            auto&       N   = B.m_Nodes[i];
            const auto& PN  = B.m_Plan.m_Nodes[i];
            const auto* pIn = &H[i * C];
            auto&       Col = N.m_Columns;
            {
                auto& Pool = *m_GameMgr.m_ComponentMgr.getEntityDetails(pIn[0]).m_pPool;
                Col[0] = PN.m_Parent >= 0 ? Pool.findIndexComponentFromInfo( xecs::component::type::info_v<xecs::component::parent> ) : -1;
                Col[1] = Pool.findIndexComponentFromInfo( xecs::component::type::info_v<xecs::component::children> );
                std::size_t c = 2;
                for( auto& R : N.m_References ) Col[c++] = Pool.findIndexComponentFromInfo( *R.m_pInfo );         // -1: a builder took it, or a share
                for( auto& I : N.m_Indirect )   Col[c++] = Pool.findIndexComponentFromInfo( *I.m_pInfo );
            }
            for( std::size_t k = 0; k < C; ++k )
            {
                auto&      D    = m_GameMgr.m_ComponentMgr.getEntityDetails(pIn[k]);
                auto&      Pool = *D.m_pPool;
                const auto Row  = static_cast<std::size_t>(D.m_PoolIndex.m_Value);
                const auto At   = [&]( int Target ) noexcept { return Target < 0 ? xecs::component::entity{} : H[ static_cast<std::size_t>(Target) * C + k ]; };

                if( Col[0] >= 0 ) reinterpret_cast<xecs::component::parent*>( &Pool.m_pComponent[Col[0]][ Row * sizeof(xecs::component::parent) ] )->m_Value = At( PN.m_Parent );
                if( Col[1] >= 0 )
                {
                    auto& L = reinterpret_cast<xecs::component::children*>( &Pool.m_pComponent[Col[1]][ Row * sizeof(xecs::component::children) ] )->m_List;
                    L.clear();
                    L.reserve( PN.m_Children.size() );
                    for( auto c : PN.m_Children ) L.push_back( At(c) );
                }
                std::size_t c = 2;
                for( auto& R : N.m_References )
                {
                    const int iCol = Col[c++];
                    if( iCol >= 0 ) *reinterpret_cast<xecs::component::entity*>( &Pool.m_pComponent[iCol][ Row * R.m_pInfo->m_Size + R.m_Offset ] ) = At( R.m_Target );
                }
                for( auto& I : N.m_Indirect )
                {
                    const int iCol = Col[c++];
                    if( iCol < 0 ) continue;
                    std::size_t j = 0;
                    recipe::details::ForEachReference( *I.m_pInfo, &Pool.m_pComponent[iCol][ Row * I.m_pInfo->m_Size ], [&]( xecs::component::entity& E ) noexcept { E = At( j < I.m_Targets.size() ? I.m_Targets[j] : -1 ); ++j; } );
                }
            }
        }

        if constexpr( false == std::is_same_v< std::decay_t<T_CALLBACK>, xecs::tools::empty_lambda > )
            for( std::size_t k = 0; k < C; ++k ) (void)m_GameMgr.getEntity( H[k], Callback );

        return { H.data(), H.size() };
    }
}
