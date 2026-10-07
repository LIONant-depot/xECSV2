#include "xecs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <crtdbg.h>

//------------------------------------------------------------------------------------------------
// Live update (documentation/Editors/prefabs_plan.md, phase 6): the instances in a world's scenes are spawned again when their prefab changes.
//
//      python source/Editors/LevelEditor/smoke/test_prefab_live.py      (compiles this file in Debug - asserts on - and runs it)
//
//   1. A prefab (root, A links to B, B) and a scene with two instances: one overrides A, an entity of the scene references the first one's B,
//      another is under it. Saved.
//   2. Another world changes the prefab on disk (A and B's values, B gains a component, a new member): LiveUpdate in the first world brings both
//      instances up to date - the override stays, the ids stay, the reference and the entity under B follow the new B - and writes nothing, marks
//      nothing (the scene is not dirty).
//   3. A fresh world loading the scene gets what the live update made.
//   4. The prefab loses a member: it leaves both instances, a reference to it is null, the override on it stays (an orphan).
//   5. Nested: a change to the inner prefab reaches an instance of the outer one.
//   6. A change in memory (the undo of an Apply): LiveUpdate without dropping the templates.
//   7. Apply: the other instance in the world gets the change at once.
//   8. A cycle of prefabs (C1 holds C2 holds C1) does not hang: the instance that closes it is left out.
//------------------------------------------------------------------------------------------------

struct position
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "LivePosition" };
    float m_X{};
    XPROPERTY_DEF
    ( "LivePosition", position
    , obj_member<"X", &position::m_X>
    )
};
XPROPERTY_REG(position)

struct link
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "LiveLink" };
    xecs::component::entity m_Target{};
    XPROPERTY_DEF
    ( "LiveLink", link
    , obj_member<"Target", &link::m_Target>
    )
};
XPROPERTY_REG(link)

struct extra
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "LiveExtra" };
    float m_Y{};
    XPROPERTY_DEF
    ( "LiveExtra", extra
    , obj_member<"Y", &extra::m_Y>
    )
};
XPROPERTY_REG(extra)

static int g_Failures = 0;
#define CHECK(EXPR) do { if(!(EXPR)) { std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #EXPR); std::fflush(stdout); ++g_Failures; } } while(false)
#define STEP(MSG)   do { std::printf("STEP: %s\n", MSG); std::fflush(stdout); } while(false)

namespace
{
    using namespace xecs::component;
    namespace fs = std::filesystem;
    using xecs::scene::permanent_id;
    using xecs::scene::DeriveMemberId;
    using address = xecs::editor::member_address;

    const std::wstring k_Project = L"smoke_test_prefab_live_data";

    struct world
    {
        std::unique_ptr<xecs::game_mgr::instance> m_pGM;
        explicit world(bool bReset = true)          // a second world next to the first shares the registry (no reset)
        {
            if (bReset) xecs::component::mgr::resetRegistrations();
            m_pGM = std::make_unique<xecs::game_mgr::instance>();
            m_pGM->RegisterComponents<position, link, extra, xecs::editor::prefab_instance>();
            m_pGM->RegisterSystems<>();
            m_pGM->m_PrefabMgr.m_ProjectPath = k_Project;
            m_pGM->m_SceneMgr.m_ProjectPath  = k_Project;
        }
        xecs::game_mgr::instance* operator->() { return m_pGM.get(); }
        xecs::game_mgr::instance& operator*()  { return *m_pGM; }
    };

    xecs::prefab::guid MakeGuid(std::uint64_t Id) { return xecs::prefab::guid{ .m_Instance = { (Id << 1) | 1 }, .m_Type = xecs::prefab::type_guid_v }; }

    fs::path SceneFolder(xecs::scene::guid S)
    {
        const auto V = S.m_Instance.m_Value;
        return fs::path(k_Project) / "Descriptors" / "Scene" / std::format("{:02X}", V & 0xFF) / std::format("{:02X}", (V >> 8) & 0xFF) / std::format("{:X}.desc", V);
    }
    std::string ReadAll(const fs::path& P) { std::ifstream F(P, std::ios::binary); std::stringstream S; S << F.rdbuf(); return S.str(); }
    std::string AllFilesOf(const fs::path& Folder)
    {
        std::vector<std::pair<std::string, std::string>> Files;
        for (auto& E : fs::recursive_directory_iterator(Folder)) if (E.is_regular_file()) Files.push_back({ E.path().generic_string(), ReadAll(E.path()) });
        std::sort(Files.begin(), Files.end());
        std::string All;
        for (auto& [Name, Text] : Files) All += "==" + Name + "\n" + Text;
        return All;
    }

    float  X(xecs::game_mgr::instance& G, entity E)      { float V = -1; (void)G.getEntity(E, [&](position& P) noexcept { V = P.m_X; }); return V; }
    entity Target(xecs::game_mgr::instance& G, entity E) { entity T; (void)G.getEntity(E, [&](link& L) noexcept { T = L.m_Target; }); return T; }
    bool   HasExtra(xecs::game_mgr::instance& G, entity E) { bool b = false; (void)G.getEntity(E, [&](extra&) noexcept { b = true; }); return b; }
    std::vector<entity> Kids(xecs::game_mgr::instance& G, entity E) { std::vector<entity> K; (void)G.getEntity(E, [&](children& C) noexcept { K = C.m_List; }); return K; }
    entity ParentOf(xecs::game_mgr::instance& G, entity E) { entity P; (void)G.getEntity(E, [&](parent& C) noexcept { P = C.m_Value; }); return P; }
    void   SetX(xecs::game_mgr::instance& G, entity E, float V) { (void)G.getEntity(E, [&](position& P) noexcept { P.m_X = V; }); }
    void   SetTarget(xecs::game_mgr::instance& G, entity E, entity T) { (void)G.getEntity(E, [&](link& L) noexcept { L.m_Target = T; }); }
    bool   Contains(const std::vector<entity>& L, entity E) { return std::any_of(L.begin(), L.end(), [&](entity K) { return K.m_Value == E.m_Value; }); }

    xecs::editor::prefab_instance& PIOf(xecs::game_mgr::instance& G, entity E) { return *xecs::prefab::recipe::details::LiveComponent<xecs::editor::prefab_instance>(G, E); }

    // An override as the editor records it (SetProperty): the live value and its entry in the recipe.
    void Override(xecs::game_mgr::instance& G, entity Root, entity Member, const address& A, float V)
    {
        SetX(G, Member, V);
        auto& Entry = xecs::prefab::recipe::details::EntryFor(PIOf(G, Root), xecs::component::type::info_v<position>.m_Guid.m_Value, A);
        xecs::prefab::recipe::details::SetText(Entry, "LivePosition/X", std::format("{:f}", V));
    }

    entity Plain(world& W, float V, entity Parent = {})
    {
        entity E;
        if (Parent.isValid())
        {
            E = W->getOrCreateArchetype<position, parent, children>().CreateEntity([&](position& P, parent& Pa) noexcept { P.m_X = V; Pa.m_Value = Parent; });
            (void)W->getEntity(Parent, [&](children& C) noexcept { C.m_List.push_back(E); });
        }
        else E = W->getOrCreateArchetype<position, children>().CreateEntity([&](position& P) noexcept { P.m_X = V; });
        return E;
    }

    xecs::prefab::local_id LocalIdOf(xecs::game_mgr::instance& G, xecs::prefab::guid P, float V)
    {
        auto& Group = G.m_PrefabMgr.m_PrefabGroups.at(P.m_Instance.m_Value);
        for (auto& [Id, E] : Group.m_LocalToRuntime) if (X(G, E) == V) return Id;
        return 0;
    }
    entity TemplateOf(xecs::game_mgr::instance& G, xecs::prefab::guid P, xecs::prefab::local_id Id) { return G.m_PrefabMgr.m_PrefabGroups.at(P.m_Instance.m_Value).m_LocalToRuntime.at(Id); }

    void MarkAll(world& W, xecs::scene::guid G)
    {
        auto& Scene = *W->m_SceneMgr.Find(G);
        for (auto& [Id, E] : Scene.m_LocalToRuntime) if (!Scene.m_InstanceMembers.contains(Id)) W->m_SceneMgr.MarkEntityNew(G, Id);
    }

    entity At(xecs::scene::instance& S, permanent_id Id) { auto It = S.m_LocalToRuntime.find(Id); return It == S.m_LocalToRuntime.end() ? entity{} : It->second; }

    int LiveUpdate(world& W, xecs::prefab::guid P, bool bDrop)
    {
        return xecs::prefab::recipe::LiveUpdate(W->m_SceneMgr, std::span<const xecs::prefab::guid>(&P, 1), bDrop);
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

    const auto P = MakeGuid(0x7101);         // root R(1) with A(2) and B(3); A links to B
    const auto Q = MakeGuid(0x7102);         // a root with an instance of P under it
    const xecs::scene::guid S{ "SmokeTestLiveScene" }, S2{ "SmokeTestLiveNested" };
    const permanent_id I = 0x100, I2 = 0x110, E = 0x200, U = 0x300, E2 = 0x210, J = 0x400;
    xecs::prefab::local_id LA = 0, LB = 0, LN = 0;
    xecs::scene::instance* pNested = nullptr;

    world W;
    STEP("1. a prefab and a scene with two instances (an override, a reference to a member, an entity under a member); saved");
    {
        auto R  = Plain(W, 1);
        auto PA = Plain(W, 2, R);
        auto PB = Plain(W, 3, R);
        PA = W->AddOrRemoveComponents<std::tuple<link>>(PA);
        SetTarget(*W, PA, PB);
        W->m_PrefabMgr.CreatePrefabFromEntity(R, P);
        W->m_PrefabMgr.m_PrefabGroups.at(P.m_Instance.m_Value).m_EntityNames[LocalIdOf(*W, P, 3)] = "Bee";
        CHECK(!W->m_PrefabMgr.Save(P));
        LA = LocalIdOf(*W, P, 2); LB = LocalIdOf(*W, P, 3);
    }
    auto& Scene = W->m_SceneMgr.FindOrCreate(S);
    Scene.m_State = xecs::scene::state::Active;
    const auto MA = DeriveMemberId(I, address{ LA }), MB = DeriveMemberId(I, address{ LB });
    const auto MA2 = DeriveMemberId(I2, address{ LA }), MB2 = DeriveMemberId(I2, address{ LB });
    {
        auto Root  = xecs::prefab::recipe::InstantiateInScene(*W, Scene, P, I, {});
        auto Root2 = xecs::prefab::recipe::InstantiateInScene(*W, Scene, P, I2, {});
        CHECK(Root.isValid() && Root2.isValid());
        Override(*W, Root, At(Scene, MA), address{ LA }, 20.0f);
        auto Holder = W->getOrCreateArchetype<position, link>().CreateEntity([&](position& Po, link& L) noexcept { Po.m_X = 9; L.m_Target = At(Scene, MB); });
        Scene.m_LocalToRuntime[E] = Holder; Scene.m_RuntimeToLocal[Holder.m_Value] = E;
        auto Holder2 = W->getOrCreateArchetype<position, link>().CreateEntity([&](position& Po, link& L) noexcept { Po.m_X = 19; L.m_Target = At(Scene, MA2); });
        Scene.m_LocalToRuntime[E2] = Holder2; Scene.m_RuntimeToLocal[Holder2.m_Value] = E2;
        auto Under = W->getOrCreateArchetype<position, parent>().CreateEntity([&](position& Po, parent& Pa) noexcept { Po.m_X = 8; Pa.m_Value = At(Scene, MB); });
        (void)W->getEntity(At(Scene, MB), [&](children& K) noexcept { K.m_List.push_back(Under); });
        Scene.m_LocalToRuntime[U] = Under; Scene.m_RuntimeToLocal[Under.m_Value] = U;
        Scene.m_EntityNames[MA2] = "Renamed";                                               // a rename of a member in the scene
        MarkAll(W, S);
        CHECK(!W->m_SceneMgr.SaveScene(S));
        CHECK(X(*W, At(Scene, MA)) == 20.0f && X(*W, At(Scene, MA2)) == 2.0f && X(*W, At(Scene, MB2)) == 3.0f);
    }
    const auto FilesBefore   = AllFilesOf(SceneFolder(S));
    const auto PendingBefore = Scene.m_PendingChanges.size();

    STEP("2. another world changes the prefab on disk; LiveUpdate brings both instances up to date, writes nothing, marks nothing");
    xecs::prefab::local_id LNew = 0;
    {
        world W2(false);
        CHECK(!W2->m_PrefabMgr.EnsureLoaded(P));
        SetX(*W2, TemplateOf(*W2, P, LA), 25.0f);
        SetX(*W2, TemplateOf(*W2, P, LB), 30.0f);
        auto B2 = W2->AddOrRemoveComponents<std::tuple<extra>>(TemplateOf(*W2, P, LB));
        (void)W2->getEntity(B2, [&](extra& X2) noexcept { X2.m_Y = 7; });
        auto  TRoot = W2->m_PrefabMgr.m_PrefabList.at(P.m_Instance.m_Value);
        auto& Group = W2->m_PrefabMgr.m_PrefabGroups.at(P.m_Instance.m_Value);
        auto  Src   = W2->getOrCreateArchetype<position, parent>().CreateEntity([&](position& Po) noexcept { Po.m_X = 5; });
        auto  New   = W2->m_PrefabMgr.CloneSubtreeIntoPrefab(Src, Group, false, nullptr, nullptr, nullptr);
        (void)W2->getEntity(New, [&](parent& Pa) noexcept { Pa.m_Value = TRoot; });
        (void)W2->getEntity(TRoot, [&](children& K) noexcept { K.m_List.push_back(New); });
        Group.m_EntityNames[LB] = "Bee2";                                                    // the prefab renames B
        CHECK(!W2->m_PrefabMgr.Save(P));
        LNew = LocalIdOf(*W2, P, 5);
        CHECK(LNew != 0);
    }
    const auto MN = DeriveMemberId(I, address{ LNew }), MN2 = DeriveMemberId(I2, address{ LNew });
    {
        const auto OldB = At(Scene, MB);
        CHECK(LiveUpdate(W, P, true) == 2);
        CHECK(X(*W, At(Scene, MA)) == 20.0f);                                                   // the override stays
        CHECK(X(*W, At(Scene, MB)) == 30.0f && X(*W, At(Scene, MA2)) == 25.0f && X(*W, At(Scene, MB2)) == 30.0f);
        CHECK(HasExtra(*W, At(Scene, MB)) && HasExtra(*W, At(Scene, MB2)));                      // the component the prefab gained
        CHECK(At(Scene, MN).isValid() && X(*W, At(Scene, MN)) == 5.0f && At(Scene, MN2).isValid());   // the member it gained, with its derived id
        CHECK(Scene.m_InstanceMembers.size() == 6);
        CHECK(At(Scene, MB).m_Value != OldB.m_Value);                                           // it is a new entity...
        CHECK(Target(*W, At(Scene, E)).m_Value == At(Scene, MB).m_Value);                       // ...and what referenced the old one references it
        CHECK(Target(*W, At(Scene, MA)).m_Value == At(Scene, MB).m_Value);                      // a reference inside the instance
        CHECK(ParentOf(*W, At(Scene, U)).m_Value == At(Scene, MB).m_Value && Contains(Kids(*W, At(Scene, MB)), At(Scene, U)));   // the entity under B
        CHECK(Kids(*W, At(Scene, I)).size() == 3 && Contains(Kids(*W, At(Scene, I)), At(Scene, MN)));
        CHECK(ParentOf(*W, At(Scene, MA)).m_Value == At(Scene, I).m_Value);
        CHECK(Scene.m_EntityNames[MB] == "Bee2");                                                // the prefab's new name
        CHECK(Scene.m_EntityNames[MA2] == "Renamed");                                            // a rename in the scene stays
        CHECK(Scene.m_PendingChanges.size() == PendingBefore);                                   // not dirty
        CHECK(AllFilesOf(SceneFolder(S)) == FilesBefore);                                         // nothing written
        CHECK(LiveUpdate(W, MakeGuid(0x7999), true) == 0);                                        // a prefab nobody uses: nothing happens
    }

    STEP("3. a fresh world loading the scene gets what the live update made");
    {
        std::vector<std::pair<permanent_id, float>> Live;
        for (auto& [Id, E1] : Scene.m_LocalToRuntime) Live.push_back({ Id, X(*W, E1) });
        world W3(false);
        CHECK(!W3->m_SceneMgr.RequestLoad(S));
        auto& Fresh = *W3->m_SceneMgr.Find(S);
        CHECK(Fresh.m_LocalToRuntime.size() == Scene.m_LocalToRuntime.size());
        for (auto& [Id, V] : Live) { CHECK(Fresh.m_LocalToRuntime.contains(Id)); if (Fresh.m_LocalToRuntime.contains(Id)) CHECK(X(*W3, Fresh.m_LocalToRuntime.at(Id)) == V); }
        CHECK(Target(*W3, At(Fresh, E)).m_Value == At(Fresh, MB).m_Value);
    }

    STEP("4. the prefab loses a member: it leaves both instances, a reference to it is null, the override on it stays (an orphan)");
    {
        {
            world W2(false);
            CHECK(!W2->m_PrefabMgr.EnsureLoaded(P));
            xecs::persist::details::DeleteEntitySubtreeUnregistered(*W2, TemplateOf(*W2, P, LA));
            CHECK(!W2->m_PrefabMgr.Save(P));
        }
        CHECK(LiveUpdate(W, P, true) == 2);
        CHECK(!At(Scene, MA).isValid() && !At(Scene, MA2).isValid() && At(Scene, MB).isValid());
        CHECK(!Target(*W, At(Scene, E2)).isValid());
        auto& PI = PIOf(*W, At(Scene, I));
        CHECK(std::any_of(PI.m_lComponents.begin(), PI.m_lComponents.end(), [&](auto& O) { return O.m_Member == address{ LA }; }));
        CHECK(Scene.m_InstanceMembers.size() == 4);
        CHECK(Scene.m_PendingChanges.size() == PendingBefore);
    }

    STEP("5. nested: a change to the inner prefab reaches an instance of the outer one");
    {
        auto& Tmp  = W->m_SceneMgr.FindOrCreate(xecs::scene::guid{ "SmokeTestLiveTmp" });
        auto  R    = Plain(W, 100);
        CHECK(xecs::prefab::recipe::InstantiateInScene(*W, Tmp, P, 0x999, R).isValid());
        W->m_PrefabMgr.CreatePrefabFromEntity(R, Q);
        CHECK(!W->m_PrefabMgr.Save(Q));
        for (auto& [Id, M] : W->m_PrefabMgr.m_PrefabGroups.at(Q.m_Instance.m_Value).m_LocalToRuntime) if (xecs::prefab::recipe::details::LiveComponent<xecs::editor::prefab_instance>(*W, M)) LN = Id;
        CHECK(LN != 0);
        CHECK(xecs::prefab::recipe::Uses(*W, Q, P) && !xecs::prefab::recipe::Uses(*W, P, Q));

        auto& Nested = W->m_SceneMgr.FindOrCreate(S2);
        pNested = &Nested;
        Nested.m_State = xecs::scene::state::Active;
        CHECK(xecs::prefab::recipe::InstantiateInScene(*W, Nested, Q, J, {}).isValid());
        const auto NN = DeriveMemberId(J, address{ LN, LNew });
        CHECK(X(*W, At(Nested, NN)) == 5.0f);
        {
            world W2(false);
            CHECK(!W2->m_PrefabMgr.EnsureLoaded(P));
            SetX(*W2, TemplateOf(*W2, P, LNew), 6.0f);
            CHECK(!W2->m_PrefabMgr.Save(P));
        }
        CHECK(LiveUpdate(W, P, true) == 3);                                                       // the two of P, and the one of Q
        CHECK(At(Nested, NN).isValid() && X(*W, At(Nested, NN)) == 6.0f);
        CHECK(X(*W, At(Scene, MN)) == 6.0f && X(*W, At(Scene, MN2)) == 6.0f);
    }

    STEP("6. a change in memory (the undo of an Apply): LiveUpdate without dropping the template");
    {
        CHECK(!W->m_PrefabMgr.EnsureLoaded(P));
        SetX(*W, TemplateOf(*W, P, LB), 50.0f);
        CHECK(LiveUpdate(W, P, false) == 3);
        CHECK(X(*W, At(Scene, MB)) == 50.0f && X(*W, At(Scene, MB2)) == 50.0f);
        CHECK(X(*W, TemplateOf(*W, P, LB)) == 50.0f);                                             // the template was kept
    }

    STEP("7. Apply: the other instance in the world gets the change at once");
    {
        const auto Root2Before = At(Scene, I2);
        Override(*W, At(Scene, I), At(Scene, MB), address{ LB }, 60.0f);
        CHECK(!xecs::prefab::recipe::ApplyToPrefab(W->m_SceneMgr, Scene, I));
        CHECK(X(*W, At(Scene, MB)) == 60.0f && X(*W, At(Scene, MB2)) == 60.0f);
        CHECK(At(Scene, I2).m_Value != Root2Before.m_Value);                                       // spawned again
        CHECK(X(*W, At(*pNested, DeriveMemberId(J, address{ LN, LB }))) == 60.0f);           // and the nested one
        CHECK(Target(*W, At(Scene, E)).m_Value == At(Scene, MB).m_Value);
    }

    STEP("8. a cycle of prefabs (C1 holds C2 holds C1) does not hang: the instance that closes it is left out");
    {
        const auto C1 = MakeGuid(0x7201), C2 = MakeGuid(0x7202);
        auto& Tmp = W->m_SceneMgr.FindOrCreate(xecs::scene::guid{ "SmokeTestLiveTmp" });
        W->m_PrefabMgr.CreatePrefabFromEntity(Plain(W, 11), C1);
        CHECK(!W->m_PrefabMgr.Save(C1));
        auto R2 = Plain(W, 12);
        CHECK(xecs::prefab::recipe::InstantiateInScene(*W, Tmp, C1, 0xA01, R2).isValid());
        W->m_PrefabMgr.CreatePrefabFromEntity(R2, C2);
        CHECK(!W->m_PrefabMgr.Save(C2));
        auto R1 = Plain(W, 13);
        CHECK(xecs::prefab::recipe::InstantiateInScene(*W, Tmp, C2, 0xA02, R1).isValid());
        W->m_PrefabMgr.DropTemplate(C1);
        W->m_PrefabMgr.CreatePrefabFromEntity(R1, C1);                                             // C1 now holds C2, which holds C1
        CHECK(!W->m_PrefabMgr.Save(C1));
        xecs::prefab::recipe::plan Plan;
        CHECK(!xecs::prefab::recipe::MakePlan(*W, C1, Plan));
        CHECK(Plan.m_Levels.size() == 2);                                                          // C1, C2 - not C1 again
        CHECK(xecs::prefab::recipe::Uses(*W, C1, C2) && xecs::prefab::recipe::Uses(*W, C2, C1));
        CHECK(xecs::prefab::recipe::InstantiateInScene(*W, Scene, C1, 0xA10, {}).isValid());
    }

    if (g_Failures == 0) fs::remove_all(fs::path(k_Project), Ec);
    if (g_Failures == 0) std::printf("ALL CHECKS PASSED\n");
    else                 std::printf("%d CHECK(S) FAILED\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
