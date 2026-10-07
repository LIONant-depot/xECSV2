#include "xecs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <crtdbg.h>

//------------------------------------------------------------------------------------------------
// A prefab instance is a recipe (documentation/Editors/prefabs_plan.md, phase 3).
//
//      python source/Editors/LevelEditor/smoke/test_prefab_recipe.py      (compiles this file in Debug - asserts on - and runs it)
//
//   0. DeriveMemberId: stable, 63 bits, above 32 bits, the root keeps the instance's id.
//   1. An instance placed in a scene: its members have derived ids, the scene knows them (m_InstanceMembers), with their prefab names.
//   2. Saved: one file for the instance (its recipe: no children record, Format 1), none for its members; the descriptor lists only it; a member
//      override, a component added to a member (its data as overrides), an entity of the scene referencing a member, one under a member.
//   3. A fresh world loads it back: same ids, values, references, the entity under the member is its child again; saving again changes no byte.
//   4. The prefab gains a member and has its children reordered: the saved scene gets the new member, every override stays on its member.
//   5. The prefab loses a member: its override is an orphan (kept), the reference to it is null, nothing crashes.
//   6. Nested: a prefab with an instance of another as a member; an override inside the nested instance from the level, and one in the nested
//      recipe of the prefab: both reach the spawned member, after a save and a load too.
//   7. Apply: an override and an entity of the scene under the instance go into the prefab; that entity becomes a member (its derived id).
//------------------------------------------------------------------------------------------------

struct position
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "RecipePosition" };

    float m_X{};

    XPROPERTY_DEF
    ( "RecipePosition", position
    , obj_member<"X", &position::m_X>
    )
};
XPROPERTY_REG(position)

struct link
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "RecipeLink" };

    xecs::component::entity m_Target{};

    XPROPERTY_DEF
    ( "RecipeLink", link
    , obj_member<"Target", &link::m_Target>
    )
};
XPROPERTY_REG(link)

struct extra
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "RecipeExtra" };

    float       m_Y{};
    std::string m_Label;

    XPROPERTY_DEF
    ( "RecipeExtra", extra
    , obj_member<"Y",     &extra::m_Y>
    , obj_member<"Label", &extra::m_Label>
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

    const std::wstring k_Project = L"smoke_test_prefab_recipe_data";

    struct world
    {
        std::unique_ptr<xecs::game_mgr::instance> m_pGM;
        world()
        {
            xecs::component::mgr::resetRegistrations();
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

    fs::path FolderOf(const char* pKind, std::uint64_t V)
    {
        return fs::path(k_Project) / "Descriptors" / pKind / std::format("{:02X}", V & 0xFF) / std::format("{:02X}", (V >> 8) & 0xFF) / std::format("{:X}.desc", V);
    }
    fs::path EntityFile(const fs::path& Folder, permanent_id Id)
    {
        return Folder / "entity_db" / std::format("{:02X}", Id & 0xFF) / std::format("{:02X}", (Id >> 8) & 0xFF) / (xecs::scene::FormatPermanentId(Id) + ".entity");
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
    float  Y(xecs::game_mgr::instance& G, entity E)      { float V = -1; (void)G.getEntity(E, [&](extra& P) noexcept { V = P.m_Y; }); return V; }
    std::vector<entity> Kids(xecs::game_mgr::instance& G, entity E) { std::vector<entity> K; (void)G.getEntity(E, [&](children& C) noexcept { K = C.m_List; }); return K; }
    entity ParentOf(xecs::game_mgr::instance& G, entity E) { entity P; (void)G.getEntity(E, [&](parent& C) noexcept { P = C.m_Value; }); return P; }
    void   SetX(xecs::game_mgr::instance& G, entity E, float V) { (void)G.getEntity(E, [&](position& P) noexcept { P.m_X = V; }); }
    void   SetTarget(xecs::game_mgr::instance& G, entity E, entity T) { (void)G.getEntity(E, [&](link& L) noexcept { L.m_Target = T; }); }

    xecs::editor::prefab_instance& PIOf(xecs::game_mgr::instance& G, entity E)
    {
        auto* p = xecs::prefab::recipe::details::LiveComponent<xecs::editor::prefab_instance>(G, E);
        return *p;
    }

    // An override as the editor records it (SetProperty): the live value and its entry in the recipe.
    void Override(xecs::game_mgr::instance& G, entity Root, entity Member, const xecs::editor::member_address& A, float V)
    {
        SetX(G, Member, V);
        auto& Entry = xecs::prefab::recipe::details::EntryFor(PIOf(G, Root), xecs::component::type::info_v<position>.m_Guid.m_Value, A);
        xecs::prefab::recipe::details::SetText(Entry, "RecipePosition/X", std::format("{:f}", V));
    }

    // An entity with a position (and children: it is a parent), not registered anywhere: the source of a prefab.
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

    // The members of P's template by their X: R = 1 (root), A = 2, B = 3.
    xecs::prefab::local_id LocalIdOf(xecs::game_mgr::instance& G, xecs::prefab::guid P, float V)
    {
        auto& Group = G.m_PrefabMgr.m_PrefabGroups.at(P.m_Instance.m_Value);
        for (auto& [Id, E] : Group.m_LocalToRuntime) if (X(G, E) == V) return Id;
        return 0;
    }

    void MarkAll(world& W, xecs::scene::guid G)
    {
        auto& Scene = *W->m_SceneMgr.Find(G);
        for (auto& [Id, E] : Scene.m_LocalToRuntime) if (!Scene.m_InstanceMembers.contains(Id)) W->m_SceneMgr.MarkEntityNew(G, Id);
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

    STEP("0. derived ids");
    {
        const xecs::editor::member_address A{ 0x1234 }, B{ 0x1234, 0x77 };
        CHECK(DeriveMemberId(0x100, {}) == 0x100);
        CHECK(DeriveMemberId(0x100, A) == DeriveMemberId(0x100, A));
        CHECK(DeriveMemberId(0x100, A) != DeriveMemberId(0x101, A));
        CHECK(DeriveMemberId(0x100, A) != DeriveMemberId(0x100, B));
        std::set<permanent_id> Seen;
        for (std::uint64_t i = 1; i <= 20000; ++i)
        {
            const auto Id = DeriveMemberId(0xABC, xecs::editor::member_address{ i });
            CHECK(Id > 0xFFFFFFFFull && Id <= xecs::scene::max_permanent_id_v);
            Seen.insert(Id);
        }
        CHECK(Seen.size() == 20000);
        CHECK(xecs::scene::FormatPermanentId(DeriveMemberId(0x100, A)).size() == 16);
    }

    const auto P  = MakeGuid(0x7001);       // root R(1) with A(2) and B(3); A links to B
    const auto Q  = MakeGuid(0x7002);       // root with one child that is an instance of P (nested)
    const xecs::scene::guid S{ "SmokeTestRecipeScene" };
    const permanent_id I = 0x100, E = 0x200, C = 0x300;
    xecs::prefab::local_id LA = 0, LB = 0;
    permanent_id MA = 0, MB = 0;
    std::string FilesAfterSave;

    {
        world W;
        STEP("1. the prefab, and an instance of it in a scene");
        {
            auto R  = Plain(W, 1);
            auto PA = Plain(W, 2, R);
            auto PB = Plain(W, 3, R);
            PA = W->AddOrRemoveComponents<std::tuple<link>>(PA);
            SetTarget(*W, PA, PB);
            W->m_PrefabMgr.CreatePrefabFromEntity(R, P);
            auto& Group = W->m_PrefabMgr.m_PrefabGroups.at(P.m_Instance.m_Value);
            LA = LocalIdOf(*W, P, 2); LB = LocalIdOf(*W, P, 3);
            Group.m_EntityNames[LA] = "Arm";
            CHECK(!W->m_PrefabMgr.Save(P));
        }
        auto& Scene = W->m_SceneMgr.FindOrCreate(S);
        auto Root = xecs::prefab::recipe::InstantiateInScene(*W, Scene, P, I, {});
        CHECK(Root.isValid());
        MA = DeriveMemberId(I, xecs::editor::member_address{ LA });
        MB = DeriveMemberId(I, xecs::editor::member_address{ LB });
        CHECK(Scene.m_LocalToRuntime.contains(I) && Scene.m_LocalToRuntime.contains(MA) && Scene.m_LocalToRuntime.contains(MB));
        CHECK(Scene.m_InstanceMembers.size() == 2 && Scene.m_InstanceMembers[MA].m_Root == I && Scene.m_InstanceMembers[MA].m_Address == xecs::editor::member_address{ LA });
        CHECK(Scene.m_EntityNames[MA] == "Arm");
        const auto A = Scene.m_LocalToRuntime.at(MA), B = Scene.m_LocalToRuntime.at(MB);
        CHECK(X(*W, A) == 2.0f && X(*W, B) == 3.0f && Target(*W, A).m_Value == B.m_Value);
        CHECK(PIOf(*W, Root).m_Format == 1 && PIOf(*W, Root).m_PrefabInstance == P);

        STEP("2. overrides, a component added to a member, references, an entity under a member; saved");
        Override(*W, Root, A, xecs::editor::member_address{ LA }, 20.0f);
        auto Bx = W->AddOrRemoveComponents<std::tuple<extra>>(B);
        CHECK(Bx.m_Value == B.m_Value);
        (void)W->getEntity(B, [&](extra& X2) noexcept { X2.m_Y = 4.5f; X2.m_Label = "added"; });
        auto Holder = W->getOrCreateArchetype<position, link>().CreateEntity([&](position& Po, link& L) noexcept { Po.m_X = 9; L.m_Target = A; });
        Scene.m_LocalToRuntime[E] = Holder; Scene.m_RuntimeToLocal[Holder.m_Value] = E;
        auto Under = W->getOrCreateArchetype<position, parent>().CreateEntity([&](position& Po, parent& Pa) noexcept { Po.m_X = 8; Pa.m_Value = B; });
        (void)W->getEntity(B, [&](children& K) noexcept { K.m_List.push_back(Under); });
        Scene.m_LocalToRuntime[C] = Under; Scene.m_RuntimeToLocal[Under.m_Value] = C;
        MarkAll(W, S);
        CHECK(!W->m_SceneMgr.SaveScene(S));

        const auto Folder = FolderOf("Scene", S.m_Instance.m_Value);
        CHECK(fs::exists(EntityFile(Folder, I)) && fs::exists(EntityFile(Folder, E)) && fs::exists(EntityFile(Folder, C)));
        CHECK(!fs::exists(EntityFile(Folder, MA)) && !fs::exists(EntityFile(Folder, MB)));
        const auto RootText = ReadAll(EntityFile(Folder, I));
        CHECK(RootText.find("AllChildren") == std::string::npos);                                     // a recipe does not hold its root's children
        CHECK(RootText.find("\"EditorPrafabInstance/Format\"") != std::string::npos);
        CHECK(RootText.find("MemberPath") == std::string::npos);
        CHECK(RootText.find("RecipeExtra/Y") != std::string::npos && RootText.find("4.5") != std::string::npos);   // the added component's data, as overrides
        CHECK(RootText.find(std::format("#{:X}", LA)) != std::string::npos);                         // a member address
        const auto Descriptor = ReadAll(Folder / "Descriptor.txt");
        CHECK(Descriptor.find("\"Scene/ActiveEntities[]\"         ;s64 3") != std::string::npos || Descriptor.find(";s64 3") != std::string::npos);
        CHECK(Descriptor.find(std::format("#{:X}", MA)) == std::string::npos);                       // no member in the descriptor (its name is the prefab's)
        FilesAfterSave = AllFilesOf(Folder);
    }

    {
        STEP("3. a fresh world loads it: same ids, values, references; a second save changes nothing");
        world W;
        CHECK(!W->m_SceneMgr.RequestLoad(S));
        auto& Scene = *W->m_SceneMgr.Find(S);
        CHECK(Scene.m_LocalToRuntime.size() == 5);
        CHECK(Scene.m_InstanceMembers.size() == 2);
        CHECK(Scene.m_LocalToRuntime.contains(MA) && Scene.m_LocalToRuntime.contains(MB));
        if (Scene.m_LocalToRuntime.contains(MA) && Scene.m_LocalToRuntime.contains(MB))
        {
            const auto Root = Scene.m_LocalToRuntime.at(I), A = Scene.m_LocalToRuntime.at(MA), B = Scene.m_LocalToRuntime.at(MB);
            CHECK(X(*W, A) == 20.0f);                                                                  // the override
            CHECK(X(*W, B) == 3.0f);
            CHECK(Target(*W, A).m_Value == B.m_Value);                                                 // a reference inside the prefab
            CHECK(HasExtra(*W, B) && Y(*W, B) == 4.5f);                                               // the component added to the member
            CHECK(Target(*W, Scene.m_LocalToRuntime.at(E)).m_Value == A.m_Value);                      // the scene's reference to a member
            const auto Under = Scene.m_LocalToRuntime.at(C);
            CHECK(ParentOf(*W, Under).m_Value == B.m_Value);
            const auto BK = Kids(*W, B);
            CHECK(std::any_of(BK.begin(), BK.end(), [&](entity K) { return K.m_Value == Under.m_Value; }));
            const auto RK = Kids(*W, Root);
            CHECK(RK.size() == 2);
            CHECK(Scene.m_EntityNames[MA] == "Arm");
            CHECK(ParentOf(*W, A).m_Value == Root.m_Value);
        }
        for (auto& [Id, X2] : Scene.m_LocalToRuntime) { if (!Scene.m_InstanceMembers.contains(Id)) W->m_SceneMgr.MarkEntityDirty(S, Id); }
        CHECK(!W->m_SceneMgr.SaveScene(S));
        CHECK(AllFilesOf(FolderOf("Scene", S.m_Instance.m_Value)) == FilesAfterSave);
    }

    {
        STEP("4. the prefab gains a member and its children are reordered: the saved scene gets it, every override stays on its member");
        world W;
        CHECK(!W->m_PrefabMgr.EnsureLoaded(P));
        auto  TRoot = W->m_PrefabMgr.m_PrefabList.at(P.m_Instance.m_Value);
        auto& Group = W->m_PrefabMgr.m_PrefabGroups.at(P.m_Instance.m_Value);
        auto  Src   = W->getOrCreateArchetype<position, parent>().CreateEntity([&](position& Po) noexcept { Po.m_X = 5; });
        auto  New   = W->m_PrefabMgr.CloneSubtreeIntoPrefab(Src, Group, false, nullptr, nullptr, nullptr);
        (void)W->getEntity(New, [&](parent& Pa) noexcept { Pa.m_Value = TRoot; });
        (void)W->getEntity(TRoot, [&](children& K) noexcept { K.m_List.insert(K.m_List.begin(), New); std::reverse(K.m_List.begin(), K.m_List.end()); });
        CHECK(!W->m_PrefabMgr.Save(P));
    }
    {
        world W;
        CHECK(!W->m_SceneMgr.RequestLoad(S));
        auto& Scene = *W->m_SceneMgr.Find(S);
        CHECK(Scene.m_InstanceMembers.size() == 3);
        CHECK(X(*W, Scene.m_LocalToRuntime.at(MA)) == 20.0f && X(*W, Scene.m_LocalToRuntime.at(MB)) == 3.0f);
        int nFive = 0;
        for (auto& [Id, M] : Scene.m_InstanceMembers) if (X(*W, Scene.m_LocalToRuntime.at(Id)) == 5.0f) ++nFive;
        CHECK(nFive == 1);
        CHECK(Kids(*W, Scene.m_LocalToRuntime.at(I)).size() == 3);
    }

    {
        STEP("5. the prefab loses a member: its override is an orphan (kept), the reference to it is null, nothing crashes");
        {
            world W;
            CHECK(!W->m_PrefabMgr.EnsureLoaded(P));
            auto& Group = W->m_PrefabMgr.m_PrefabGroups.at(P.m_Instance.m_Value);
            xecs::persist::details::DeleteEntitySubtreeUnregistered(*W, Group.m_LocalToRuntime.at(LA));
            CHECK(!W->m_PrefabMgr.Save(P));
        }
        world W;
        CHECK(!W->m_SceneMgr.RequestLoad(S));
        auto& Scene = *W->m_SceneMgr.Find(S);
        CHECK(!Scene.m_LocalToRuntime.contains(MA));
        CHECK(Scene.m_LocalToRuntime.contains(MB));
        CHECK(!Target(*W, Scene.m_LocalToRuntime.at(E)).isValid());
        auto& PI = PIOf(*W, Scene.m_LocalToRuntime.at(I));
        CHECK(std::any_of(PI.m_lComponents.begin(), PI.m_lComponents.end(), [&](auto& O) { return O.m_Member == xecs::editor::member_address{ LA }; }));
        W->m_SceneMgr.MarkEntityDirty(S, I);
        CHECK(!W->m_SceneMgr.SaveScene(S));
        CHECK(ReadAll(EntityFile(FolderOf("Scene", S.m_Instance.m_Value), I)).find(std::format("#{:X}", LA)) != std::string::npos);   // still in the file
    }

    const xecs::scene::guid S2{ "SmokeTestRecipeNested" };
    const permanent_id J = 0x400;
    xecs::prefab::local_id LN = 0, LB2 = 0;
    {
        STEP("6. nested: a prefab with an instance of another as a member; overrides from the prefab's nested recipe and from the level");
        world W;
        auto& Tmp  = W->m_SceneMgr.FindOrCreate(xecs::scene::guid{ "SmokeTestRecipeTmp" });
        auto  R    = Plain(W, 100);
        auto  Inst = xecs::prefab::recipe::InstantiateInScene(*W, Tmp, P, 0x999, R);       // P under R
        CHECK(Inst.isValid());
        LB2 = LocalIdOf(*W, P, 3);
        // the nested recipe (what Q's member does to P): B's X is 55
        Override(*W, Inst, Tmp.m_LocalToRuntime.at(DeriveMemberId(0x999, xecs::editor::member_address{ LB2 })), xecs::editor::member_address{ LB2 }, 55.0f);
        W->m_PrefabMgr.CreatePrefabFromEntity(R, Q);
        CHECK(!W->m_PrefabMgr.Save(Q));
        auto& QGroup = W->m_PrefabMgr.m_PrefabGroups.at(Q.m_Instance.m_Value);
        for (auto& [Id, M] : QGroup.m_LocalToRuntime) if (xecs::prefab::recipe::details::LiveComponent<xecs::editor::prefab_instance>(*W, M)) LN = Id;
        CHECK(LN != 0);

        auto& Scene = W->m_SceneMgr.FindOrCreate(S2);
        auto  Root  = xecs::prefab::recipe::InstantiateInScene(*W, Scene, Q, J, {});
        CHECK(Root.isValid());
        const auto NB = DeriveMemberId(J, xecs::editor::member_address{ LN, LB2 });
        CHECK(Scene.m_LocalToRuntime.contains(NB));
        if (Scene.m_LocalToRuntime.contains(NB))
        {
            CHECK(X(*W, Scene.m_LocalToRuntime.at(NB)) == 55.0f);                                   // the nested recipe of the prefab
            Override(*W, Root, Scene.m_LocalToRuntime.at(NB), xecs::editor::member_address{ LN, LB2 }, 77.0f);
        }
        CHECK(Scene.m_InstanceMembers.size() == 3);                                                    // N, and P's two members under it (A was removed from P in 5)
        MarkAll(W, S2);
        CHECK(!W->m_SceneMgr.SaveScene(S2));
    }
    {
        world W;
        CHECK(!W->m_SceneMgr.RequestLoad(S2));
        auto& Scene = *W->m_SceneMgr.Find(S2);
        const auto NB = DeriveMemberId(J, xecs::editor::member_address{ LN, LB2 });
        CHECK(Scene.m_LocalToRuntime.contains(NB));
        if (Scene.m_LocalToRuntime.contains(NB)) CHECK(X(*W, Scene.m_LocalToRuntime.at(NB)) == 77.0f);       // the level's override wins
        CHECK(Scene.m_InstanceMembers.size() == 3);

        STEP("6b. a fresh instance of the nested prefab has the nested recipe's value");
        auto R2 = xecs::prefab::recipe::InstantiateInScene(*W, Scene, Q, 0x401, {});
        CHECK(R2.isValid());
        const auto NB2 = DeriveMemberId(0x401, xecs::editor::member_address{ LN, LB2 });
        CHECK(Scene.m_LocalToRuntime.contains(NB2) && X(*W, Scene.m_LocalToRuntime.at(NB2)) == 55.0f);
    }

    {
        STEP("7. Apply: an override and an entity of the scene under the instance go into the prefab; that entity becomes a member");
        world W;
        CHECK(!W->m_SceneMgr.RequestLoad(S));
        auto& Scene = *W->m_SceneMgr.Find(S);
        const auto Root = Scene.m_LocalToRuntime.at(I);
        Override(*W, Root, Scene.m_LocalToRuntime.at(MB), xecs::editor::member_address{ LB }, 33.0f);
        auto Added = W->getOrCreateArchetype<position, parent>().CreateEntity([&](position& Po, parent& Pa) noexcept { Po.m_X = 44; Pa.m_Value = Root; });
        (void)W->getEntity(Root, [&](children& K) noexcept { K.m_List.push_back(Added); });
        const permanent_id AddedId = 0x500;
        Scene.m_LocalToRuntime[AddedId] = Added; Scene.m_RuntimeToLocal[Added.m_Value] = AddedId;
        W->m_SceneMgr.MarkEntityNew(S, AddedId);

        CHECK(!xecs::prefab::recipe::ApplyToPrefab(W->m_SceneMgr, Scene, I));
        CHECK(!Scene.m_LocalToRuntime.contains(AddedId));                                              // it took its derived id
        CHECK(Scene.m_InstanceMembers.size() == 4);                                                    // B, the member of step 4, the entity under B (step 2) and the one under the root: Apply takes every entity added under the instance
        if (Scene.m_InstanceMembers.size() != 4) for (auto& [Id, M] : Scene.m_InstanceMembers) std::printf("  member %s X=%f\n", xecs::scene::FormatPermanentId(Id).c_str(), X(*W, Scene.m_LocalToRuntime.at(Id)));
        bool bFound = false;
        for (auto& [Id, M] : Scene.m_InstanceMembers) if (Scene.m_LocalToRuntime.at(Id).m_Value == Added.m_Value) { bFound = true; CHECK(Id == DeriveMemberId(I, M.m_Address)); }
        CHECK(bFound);
        auto& PI = PIOf(*W, Root);
        CHECK(std::none_of(PI.m_lComponents.begin(), PI.m_lComponents.end(), [&](auto& O) { return O.m_Member == xecs::editor::member_address{ LB }; }));
        CHECK(!W->m_SceneMgr.SaveScene(S));
        CHECK(!fs::exists(EntityFile(FolderOf("Scene", S.m_Instance.m_Value), C)));                   // members now: their files are gone
        CHECK(!fs::exists(EntityFile(FolderOf("Scene", S.m_Instance.m_Value), AddedId)));
    }
    {
        world W;
        CHECK(!W->m_SceneMgr.RequestLoad(S));
        auto& Scene = *W->m_SceneMgr.Find(S);
        CHECK(X(*W, Scene.m_LocalToRuntime.at(MB)) == 33.0f);                                          // from the prefab now
        int n44 = 0;
        for (auto& [Id, M] : Scene.m_InstanceMembers) if (X(*W, Scene.m_LocalToRuntime.at(Id)) == 44.0f) ++n44;
        CHECK(n44 == 1);
        int n8 = 0;
        for (auto& [Id, M] : Scene.m_InstanceMembers) if (X(*W, Scene.m_LocalToRuntime.at(Id)) == 8.0f) ++n8;
        CHECK(n8 == 1 && !Scene.m_LocalToRuntime.contains(C));                                       // the entity under B (step 2) joined the prefab too
        if (n8 != 1 || Scene.m_LocalToRuntime.contains(C)) for (auto& [Id, E2] : Scene.m_LocalToRuntime) std::printf("  entity %s X=%f member=%d\n", xecs::scene::FormatPermanentId(Id).c_str(), X(*W, E2), (int)Scene.m_InstanceMembers.contains(Id));
        CHECK(!Scene.m_LocalToRuntime.contains(0x500));
        auto R3 = xecs::prefab::recipe::InstantiateInScene(*W, Scene, P, 0x600, {});
        int n = 0;
        for (auto& [Id, M] : Scene.m_InstanceMembers) if (M.m_Root == 0x600) { ++n; CHECK(M.m_Address.size() == 1); }       // a member of the prefab itself, however deep: one id
        CHECK(R3.isValid() && n == 4);
        if (n != 4) for (auto& [Id, M] : Scene.m_InstanceMembers) if (M.m_Root == 0x600) std::printf("  member %s X=%f\n", xecs::scene::FormatPermanentId(Id).c_str(), X(*W, Scene.m_LocalToRuntime.at(Id)));
    }

    const auto P1 = MakeGuid(0x7003);       // one entity: an instance of it has no children of its own
    const xecs::scene::guid S3{ "SmokeTestRecipeSingle" };
    {
        STEP("8. an entity of the scene under an instance of a one-entity prefab: its root gets a children component again at load");
        world W;
        auto Src = W->getOrCreateArchetype<position>().CreateEntity([&](position& Po) noexcept { Po.m_X = 7; });
        W->m_PrefabMgr.CreatePrefabFromEntity(Src, P1);
        CHECK(!W->m_PrefabMgr.Save(P1));
        auto& Scene = W->m_SceneMgr.FindOrCreate(S3);
        auto Root = xecs::prefab::recipe::InstantiateInScene(*W, Scene, P1, 0x700, {});
        CHECK(Root.isValid());
        Root = W->AddOrRemoveComponents<std::tuple<children>>(Root);
        auto Under = W->getOrCreateArchetype<position, parent>().CreateEntity([&](position& Po, parent& Pa) noexcept { Po.m_X = 70; Pa.m_Value = Root; });
        (void)W->getEntity(Root, [&](children& K) noexcept { K.m_List.push_back(Under); });
        Scene.m_LocalToRuntime[0x701] = Under; Scene.m_RuntimeToLocal[Under.m_Value] = 0x701;
        MarkAll(W, S3);
        CHECK(!W->m_SceneMgr.SaveScene(S3));
    }
    {
        world W;
        CHECK(!W->m_SceneMgr.RequestLoad(S3));
        auto& Scene = *W->m_SceneMgr.Find(S3);
        const auto K = Kids(*W, Scene.m_LocalToRuntime.at(0x700));
        CHECK(K.size() == 1 && K[0].m_Value == Scene.m_LocalToRuntime.at(0x701).m_Value);
    }

    if (g_Failures == 0) fs::remove_all(fs::path(k_Project), Ec);          // kept when something failed: what the files say
    if (g_Failures == 0) std::printf("ALL CHECKS PASSED\n");
    else                 std::printf("%d CHECK(S) FAILED\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
