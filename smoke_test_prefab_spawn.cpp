#include "xecs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <filesystem>
#include <crtdbg.h>

//------------------------------------------------------------------------------------------------
// Spawning from the baked plan (documentation/Editors/prefabs_plan.md 3.5, phase 4).
//
//      python -m pytest source/Editors/LevelEditor/smoke/test_prefab_spawn.py      (compiles this file in Debug - asserts on - and runs it)
//
//   1. A flat prefab spawned three times in one call: the span the spawn returns (node-major), every member's values, parent, children and references
//      inside its own instance (never the template, never another instance), what the systems see.
//   2. CreatePrefabInstance(Count, guid, Callback, bRemoveRoot=false) spawns from the plan, the callback runs on each root.
//   3. References not in a component's own bytes (in a container it holds) are patched too.
//   4. Nested: a prefab holding an instance of another, with a value of its nested recipe: the spawn has it, every reference inside the instance.
//   5. Builders: with the world's builder systems on, a spawned member is built (the builder component consumed, the handle written from the
//      prefab's data); off, it keeps its builder component as an ordinary one. A scene's InstantiateInScene builds too (the editor's Play).
//   6. A prefab saved after a change bakes again: the next spawn has the change.
//------------------------------------------------------------------------------------------------

struct position
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "SpawnPosition" };
    float m_X{};
    XPROPERTY_DEF( "SpawnPosition", position, obj_member<"X", &position::m_X> )
};
XPROPERTY_REG(position)

struct link
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "SpawnLink" };
    xecs::component::entity m_Target{};
    XPROPERTY_DEF( "SpawnLink", link, obj_member<"Target", &link::m_Target> )
};
XPROPERTY_REG(link)

// references in a container: they are not in the component's own bytes
struct group
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "SpawnGroup" };
    std::vector<xecs::component::entity> m_List;
    void ReportReferences( std::vector<xecs::component::entity*>& Out ) noexcept { for (auto& E : m_List) Out.push_back(&E); }
    XPROPERTY_DEF( "SpawnGroup", group, obj_member<"List", &group::m_List> )
};
XPROPERTY_REG(group)

// a builder component and the handle a builder system writes from it
struct shape
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "SpawnShape", .m_bBuilder = true };
    float m_Size{};
    XPROPERTY_DEF( "SpawnShape", shape, obj_member<"Size", &shape::m_Size> )
};
XPROPERTY_REG(shape)

struct body
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "SpawnBody" };
    int   m_Handle{};
    float m_Size{};
    XPROPERTY_DEF( "SpawnBody", body, obj_member<"Handle", &body::m_Handle>, obj_member<"Size", &body::m_Size> )
};
XPROPERTY_REG(body)

static int g_Built = 0;

struct body_builder : xecs::system::instance
{
    constexpr static auto typedef_v = xecs::system::type::builder{ .m_pName = "Spawn Body Builder" };
    using xecs::system::instance::instance;
    void operator()( const shape& S, body& B ) noexcept { B.m_Handle = ++g_Built; B.m_Size = S.m_Size; }
};

static int g_Failures = 0;
#define CHECK(EXPR) do { if(!(EXPR)) { std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #EXPR); std::fflush(stdout); ++g_Failures; } } while(false)
#define STEP(MSG)   do { std::printf("STEP: %s\n", MSG); std::fflush(stdout); } while(false)

namespace
{
    using namespace xecs::component;
    namespace fs = std::filesystem;

    const std::wstring k_Project = L"smoke_test_prefab_spawn_data";

    struct world
    {
        std::unique_ptr<xecs::game_mgr::instance> m_pGM;
        world()
        {
            xecs::component::mgr::resetRegistrations();
            m_pGM = std::make_unique<xecs::game_mgr::instance>();
            m_pGM->RegisterComponents<position, link, group, shape, body, xecs::editor::prefab_instance>();
            m_pGM->RegisterSystems<body_builder>();
            m_pGM->m_PrefabMgr.m_ProjectPath = k_Project;
            m_pGM->m_SceneMgr.m_ProjectPath  = k_Project;
        }
        xecs::game_mgr::instance* operator->() { return m_pGM.get(); }
        xecs::game_mgr::instance& operator*()  { return *m_pGM; }
    };

    xecs::prefab::guid MakeGuid(std::uint64_t Id) { return xecs::prefab::guid{ .m_Instance = { (Id << 1) | 1 }, .m_Type = xecs::prefab::type_guid_v }; }

    template< typename T > T* Get(xecs::game_mgr::instance& G, entity E) { return xecs::prefab::recipe::details::LiveComponent<T>(G, E); }
    float X(xecs::game_mgr::instance& G, entity E) { auto* p = Get<position>(G, E); return p ? p->m_X : -1.0f; }

    // An entity with a position, a link, a group and children, under Parent when given (not registered anywhere: the source of a prefab).
    entity Plain(world& W, float V, entity Parent = {})
    {
        entity E;
        if (Parent.isValid())
        {
            E = W->getOrCreateArchetype<position, link, group, parent, children>().CreateEntity([&](position& P, parent& Pa) noexcept { P.m_X = V; Pa.m_Value = Parent; });
            (void)W->getEntity(Parent, [&](children& C) noexcept { C.m_List.push_back(E); });
        }
        else E = W->getOrCreateArchetype<position, link, group, children>().CreateEntity([&](position& P) noexcept { P.m_X = V; });
        return E;
    }

    // R(1) with A(2) and B(3): R links to A, A to R, B to A; R's group holds B and A.
    void MakeFlat(world& W, xecs::prefab::guid P)
    {
        auto R = Plain(W, 1), A = Plain(W, 2, R), B = Plain(W, 3, R);
        Get<link>(*W, R)->m_Target = A;
        Get<link>(*W, A)->m_Target = R;
        Get<link>(*W, B)->m_Target = A;
        Get<group>(*W, R)->m_List = { B, A };
        W->m_PrefabMgr.CreatePrefabFromEntity(R, P);
        for (auto E : { B, A, R }) W->DeleteEntity(E);
        W->m_ArchetypeMgr.UpdateStructuralChanges();
    }

    // Instance k of a flat spawn (node-major span): R, A, B.
    struct flat { entity R, A, B; };
    flat Instance(std::span<const entity> S, std::size_t Count, std::size_t k) { return { S[k], S[Count + k], S[2 * Count + k] }; }

    void CheckFlat(xecs::game_mgr::instance& G, const flat& I, const std::set<std::uint64_t>& Others)
    {
        CHECK(X(G, I.R) == 1.0f && X(G, I.A) == 2.0f && X(G, I.B) == 3.0f);
        CHECK(Get<link>(G, I.R)->m_Target == I.A && Get<link>(G, I.A)->m_Target == I.R && Get<link>(G, I.B)->m_Target == I.A);
        CHECK(Get<parent>(G, I.A)->m_Value == I.R && Get<parent>(G, I.B)->m_Value == I.R && Get<parent>(G, I.R) == nullptr);
        CHECK((Get<children>(G, I.R)->m_List == std::vector<entity>{ I.A, I.B }));
        CHECK((Get<group>(G, I.R)->m_List == std::vector<entity>{ I.B, I.A }));
        for (auto E : { I.R, I.A, I.B }) CHECK(!Others.contains(E.m_Value));
    }

    std::size_t VisibleWith(xecs::game_mgr::instance& G)
    {
        std::size_t N = 0;
        xecs::query::instance Q;
        Q.m_Must.AddFromComponents<position>();
        G.Foreach(G.Search(Q), [&](const position&) noexcept { ++N; });
        return N;
    }
}

int main()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    for (int t : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT }) { _CrtSetReportMode(t, _CRTDBG_MODE_FILE); _CrtSetReportFile(t, _CRTDBG_FILE_STDERR); }
#endif
    std::error_code Ec;
    fs::remove_all(fs::path(k_Project), Ec);

    const auto P = MakeGuid(0x5001);    // flat
    const auto Q = MakeGuid(0x5002);    // a root holding an instance of P
    const auto Bp = MakeGuid(0x5003);   // a root with a builder component, and a child linking to it

    {
        world W;
        MakeFlat(W, P);
        std::set<std::uint64_t> Templates;
        for (auto& [Id, E] : W->m_PrefabMgr.m_PrefabGroups.at(P.m_Instance.m_Value).m_LocalToRuntime) Templates.insert(E.m_Value);

        STEP("1. a flat prefab spawned three times in one call");
        const auto Span = W->m_PrefabMgr.Spawn(P, 3);
        CHECK(Span.size() == 9);
        const std::vector<entity> S(Span.begin(), Span.end());
        std::set<std::uint64_t> Seen(Templates);
        for (auto& [G, pB] : W->m_PrefabMgr.m_Baked) for (auto& N : pB->m_Nodes) Seen.insert(N.m_Entity.m_Value);
        for (std::size_t k = 0; k < 3; ++k)
        {
            const auto I = Instance(S, 3, k);
            std::set<std::uint64_t> Others(Seen);
            for (std::size_t j = 0; j < 3; ++j) if (j != k) for (auto E : { S[j], S[3 + j], S[6 + j] }) Others.insert(E.m_Value);
            CheckFlat(*W, I, Others);
        }
        W->m_ArchetypeMgr.UpdateStructuralChanges();
        CHECK(VisibleWith(*W) == 9);                             // the templates and the baked entities are inert
        CHECK(X(*W, W->m_PrefabMgr.m_PrefabList.at(P.m_Instance.m_Value)) == 1.0f);

        STEP("2. CreatePrefabInstance(Count, guid) spawns from the plan; the callback runs on each root");
        CHECK(W->m_PrefabMgr.CreatePrefabInstance(2, P, [](position& Pos) noexcept { Pos.m_X = 50.0f; }, /*bRemoveRoot=*/false));
        const std::vector<entity> S2(W->m_PrefabMgr.m_Baked.at(P.m_Instance.m_Value)->m_Spawned);
        CHECK(S2.size() == 6);
        if (S2.size() == 6)
            for (std::size_t k = 0; k < 2; ++k)
            {
                CHECK(X(*W, S2[k]) == 50.0f && X(*W, S2[2 + k]) == 2.0f);
                CHECK(Get<link>(*W, S2[2 + k])->m_Target == S2[k] && Get<parent>(*W, S2[4 + k])->m_Value == S2[k]);
            }

        STEP("3. references in a container (the root's group) are patched");
        const auto& Baked = *W->m_PrefabMgr.m_Baked.at(P.m_Instance.m_Value);
        CHECK(Baked.m_Nodes[0].m_Indirect.size() == 1 && Baked.m_Nodes[0].m_References.size() == 1);    // the group, and the link in the bytes
        CHECK((Get<group>(*W, Baked.m_Nodes[0].m_Entity)->m_List == std::vector<entity>{ entity{}, entity{} }));   // the bake keeps every reference null

        STEP("4. nested: the nested recipe's value is in every spawn, every reference inside its instance");
        {
            auto& Tmp  = W->m_SceneMgr.FindOrCreate(xecs::scene::guid{ "SmokeTestSpawnTmp" });
            auto  R    = Plain(W, 100);
            auto  Inst = xecs::prefab::recipe::InstantiateInScene(*W, Tmp, P, 0x999, R);
            CHECK(Inst.isValid());
            // the nested recipe: B's X is 55
            xecs::prefab::local_id LB = 0;
            for (auto& [Id, E] : W->m_PrefabMgr.m_PrefabGroups.at(P.m_Instance.m_Value).m_LocalToRuntime) if (X(*W, E) == 3.0f) LB = Id;
            const xecs::editor::member_address AB{ LB };
            Get<position>(*W, Tmp.m_LocalToRuntime.at(xecs::scene::DeriveMemberId(0x999, AB)))->m_X = 55.0f;
            auto& Entry = xecs::prefab::recipe::details::EntryFor(*Get<xecs::editor::prefab_instance>(*W, Inst), xecs::component::type::info_v<position>.m_Guid.m_Value, AB);
            xecs::prefab::recipe::details::SetText(Entry, "SpawnPosition/X", "55.000000");
            Get<link>(*W, R)->m_Target = Inst;
            W->m_PrefabMgr.CreatePrefabFromEntity(R, Q);
        }
        const auto NSpan = W->m_PrefabMgr.Spawn(Q, 2);
        const std::vector<entity> N(NSpan.begin(), NSpan.end());
        const auto& QB = *W->m_PrefabMgr.m_Baked.at(Q.m_Instance.m_Value);
        CHECK(QB.m_Plan.m_Nodes.size() == 4 && N.size() == 8);          // Q's root, the nested instance (P's root) and P's two members
        // both members of the nested prefab are addressed through it (the plan's nested addresses once dangled when its node list grew)
        if (QB.m_Plan.m_Nodes.size() == 4) CHECK(QB.m_Plan.m_Nodes[2].m_Address.size() == 2 && QB.m_Plan.m_Nodes[3].m_Address.size() == 2 && QB.m_Plan.m_Nodes[2].m_Address[0] == QB.m_Plan.m_Nodes[1].m_Address[0]);
        if (N.size() == 8)
            for (std::size_t k = 0; k < 2; ++k)
            {
                entity Root = N[k], Nested, B;
                for (std::size_t i = 1; i < 4; ++i)
                {
                    if (QB.m_Plan.m_Nodes[i].m_Address.size() == 1) Nested = N[i * 2 + k];
                    if (X(*W, N[i * 2 + k]) == 55.0f) B = N[i * 2 + k];
                }
                CHECK(Nested.isValid() && B.isValid());                 // B has the nested recipe's value
                CHECK(Get<xecs::editor::prefab_instance>(*W, Nested) != nullptr);
                CHECK(Get<link>(*W, Root)->m_Target == Nested);         // the outer root's link to the nested instance
                CHECK(Get<parent>(*W, Nested)->m_Value == Root);
                if (B.isValid()) CHECK(Get<parent>(*W, B)->m_Value == Nested && Get<link>(*W, Get<link>(*W, B)->m_Target)->m_Target == Nested);
            }
        // what the inspector's "revert to the prefab's value" goes back to for a member inside the nested instance: the baked member (the nested recipe's 55), not the inner prefab's own template member (3)
        if (QB.m_Plan.m_Nodes.size() == 4)
            for (std::size_t i = 2; i < 4; ++i)
            {
                const auto& Address = QB.m_Plan.m_Nodes[i].m_Address;
                const auto Baked2   = W->m_PrefabMgr.FindBakedMember(Q, Address);
                const auto Inner    = xecs::prefab::recipe::FindTemplate(*W, Q, Address);
                CHECK(Baked2.isValid() && Inner.isValid());
                if (Baked2.isValid() && Inner.isValid() && QB.m_Plan.m_Nodes[i].m_Template.m_Value == Inner.m_Value && X(*W, Inner) == 3.0f) CHECK(X(*W, Baked2) == 55.0f);
            }
    }

    STEP("5. builders: a spawned member is built when the world runs them");
    {
        world W;
        {
            auto R = W->getOrCreateArchetype<position, shape, body, children>().CreateEntity([](position& P, shape& S) noexcept { P.m_X = 7; S.m_Size = 2.5f; });
            auto C = W->getOrCreateArchetype<position, link, parent>().CreateEntity([&](position& P, link& L, parent& Pa) noexcept { P.m_X = 8; L.m_Target = R; Pa.m_Value = R; });
            (void)W->getEntity(R, [&](children& K) noexcept { K.m_List.push_back(C); });
            W->m_PrefabMgr.CreatePrefabFromEntity(R, Bp);
            W->DeleteEntity(C); W->DeleteEntity(R);
            W->m_ArchetypeMgr.UpdateStructuralChanges();
        }
        const auto OffSpan = W->m_PrefabMgr.Spawn(Bp, 1);
        const std::vector<entity> Off(OffSpan.begin(), OffSpan.end());
        CHECK(Off.size() == 2 && Get<shape>(*W, Off[0]) != nullptr && Get<body>(*W, Off[0])->m_Handle == 0);     // off: an ordinary component

        W->EnableBuilders(true);
        const int Before = g_Built;
        const auto OnSpan = W->m_PrefabMgr.Spawn(Bp, 3);
        const std::vector<entity> On(OnSpan.begin(), OnSpan.end());
        CHECK(On.size() == 6 && g_Built == Before + 3);
        if (On.size() == 6)
            for (std::size_t k = 0; k < 3; ++k)
            {
                CHECK(Get<shape>(*W, On[k]) == nullptr);                                         // consumed
                CHECK(Get<body>(*W, On[k]) && Get<body>(*W, On[k])->m_Handle > Before && Get<body>(*W, On[k])->m_Size == 2.5f);
                CHECK(X(*W, On[k]) == 7.0f && Get<link>(*W, On[3 + k])->m_Target == On[k] && Get<parent>(*W, On[3 + k])->m_Value == On[k]);
                CHECK((Get<children>(*W, On[k])->m_List == std::vector<entity>{ On[3 + k] }));
            }
        CHECK(Get<shape>(*W, W->m_PrefabMgr.m_PrefabList.at(Bp.m_Instance.m_Value)) != nullptr);     // the template is never built

        auto& Scene = W->m_SceneMgr.FindOrCreate(xecs::scene::guid{ "SmokeTestSpawnPlay" });
        const auto Root = xecs::prefab::recipe::InstantiateInScene(*W, Scene, Bp, 0x700, {});
        CHECK(Root.isValid() && Get<shape>(*W, Root) == nullptr && Get<body>(*W, Root) && Get<body>(*W, Root)->m_Handle > 0);
        CHECK(Scene.m_InstanceMembers.size() == 1);
    }

    STEP("6. a prefab saved after a change bakes again");
    {
        world W;
        MakeFlat(W, P);
        (void)W->m_PrefabMgr.Spawn(P, 1);
        const auto OldBaked = W->m_PrefabMgr.m_Baked.at(P.m_Instance.m_Value)->m_Nodes[0].m_Entity;
        Get<position>(*W, W->m_PrefabMgr.m_PrefabList.at(P.m_Instance.m_Value))->m_X = 9.0f;      // what Apply does to the template
        CHECK(!W->m_PrefabMgr.Save(P));
        CHECK(W->m_PrefabMgr.m_Baked.empty());
        W->m_ArchetypeMgr.UpdateStructuralChanges();
        CHECK(!W->m_ComponentMgr.isEntityValid(OldBaked));                                         // the old plan's entities are gone
        const auto S = W->m_PrefabMgr.Spawn(P, 1);
        CHECK(S.size() == 3 && X(*W, S[0]) == 9.0f);
    }

    fs::remove_all(fs::path(k_Project), Ec);
    if (g_Failures == 0) { std::printf("ALL CHECKS PASSED\n"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", g_Failures);
    return 1;
}
