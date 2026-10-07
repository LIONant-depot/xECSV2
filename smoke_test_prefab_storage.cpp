#include "xecs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <crtdbg.h>

//------------------------------------------------------------------------------------------------
// Prefab storage (documentation/Editors/prefabs_plan.md, phase 1): a prefab is stored as a scene.
//
//      python source/Editors/LevelEditor/smoke/test_prefab_storage.py      (compiles this file in Debug - asserts on - and runs it)
//
//   1. Make a prefab of a group whose members reference each other (and one entity outside it): the template's references point at its own
//      members, the one outside is null (phase 0, finding 2: the clone used to keep the source's handles and Save asserted on them).
//   2. The members' names in their scene become the prefab's.
//   3. Save writes the scene format: Descriptor.txt (Root, ActiveEntities, EntityNames), one entity_db file per member, ComponentDeps.txt.
//   4. A fresh world loads it back: same members, values, references, names.
//   5. A member that left the prefab loses its file at the next Save.
//   6. A member referencing a live entity outside the prefab: Save refuses and writes nothing.
//   7. The old format (one Entity.txt) is still read, and the next Save converts it (Entity.txt removed).
//   8. A prefab that does not load leaves nothing behind.
//   9. A Parent record written before Follow was a field (no Follow column) loads, with the default Follow.
//------------------------------------------------------------------------------------------------

struct position
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "StoragePosition" };

    float m_X{};

    XPROPERTY_DEF
    ( "StoragePosition", position
    , obj_member<"X", &position::m_X>
    )
};
XPROPERTY_REG(position)

struct link
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "StorageLink" };

    xecs::component::entity m_Target{};

    XPROPERTY_DEF
    ( "StorageLink", link
    , obj_member<"Target", &link::m_Target>
    )
};
XPROPERTY_REG(link)

static int g_Failures = 0;
#define CHECK(EXPR) do { if(!(EXPR)) { std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #EXPR); std::fflush(stdout); ++g_Failures; } } while(false)
#define STEP(MSG)   do { std::printf("STEP: %s\n", MSG); std::fflush(stdout); } while(false)

namespace
{
    using namespace xecs::component;
    namespace fs = std::filesystem;

    const std::wstring k_Project = L"smoke_test_prefab_storage_data";

    struct world
    {
        std::unique_ptr<xecs::game_mgr::instance> m_pGM;
        world()
        {
            xecs::component::mgr::resetRegistrations();
            m_pGM = std::make_unique<xecs::game_mgr::instance>();
            m_pGM->RegisterComponents<position, link, xecs::editor::prefab_instance>();
            m_pGM->RegisterSystems<>();
            m_pGM->m_PrefabMgr.m_ProjectPath = k_Project;
            m_pGM->m_SceneMgr.m_ProjectPath  = k_Project;
        }
        xecs::game_mgr::instance* operator->() { return m_pGM.get(); }
        xecs::game_mgr::instance& operator*()  { return *m_pGM; }
    };

    xecs::prefab::guid MakeGuid(std::uint64_t Id) { return xecs::prefab::guid{ .m_Instance = { (Id << 1) | 1 }, .m_Type = xecs::prefab::type_guid_v }; }

    fs::path Folder(xecs::prefab::guid G)
    {
        const auto V = G.m_Instance.m_Value;
        return fs::path(k_Project) / "Descriptors" / "Prefab" / std::format("{:02X}", V & 0xFF) / std::format("{:02X}", (V >> 8) & 0xFF) / std::format("{:X}.desc", V);
    }

    int CountEntityFiles(xecs::prefab::guid G)
    {
        int n = 0;
        std::error_code Ec;
        if (!fs::exists(Folder(G) / "entity_db", Ec)) return 0;
        for (auto& E : fs::recursive_directory_iterator(Folder(G) / "entity_db")) if (E.is_regular_file() && E.path().extension() == ".entity") ++n;
        return n;
    }

    std::string ReadAll(const fs::path& P) { std::ifstream F(P, std::ios::binary); std::stringstream S; S << F.rdbuf(); return S.str(); }

    std::uint8_t Follow(xecs::game_mgr::instance& G, entity E) { std::uint8_t F = 0; (void)G.getEntity(E, [&](parent& P) noexcept { F = P.m_Follow; }); return F; }
    float    X(xecs::game_mgr::instance& G, entity E)      { float V = -1; (void)G.getEntity(E, [&](position& P) noexcept { V = P.m_X; }); return V; }
    entity   Target(xecs::game_mgr::instance& G, entity E) { entity T; (void)G.getEntity(E, [&](link& L) noexcept { T = L.m_Target; }); return T; }
    std::vector<entity> Kids(xecs::game_mgr::instance& G, entity E) { std::vector<entity> K; (void)G.getEntity(E, [&](children& C) noexcept { K = C.m_List; }); return K; }

    // The template's members by their X (each member of the test prefab has its own): 1 = root, 2 = A, 3 = B, 4 = C.
    struct members { entity m_Root, m_A, m_B, m_C; };
    members TemplateMembers(xecs::game_mgr::instance& G, xecs::prefab::guid Guid)
    {
        members M;
        M.m_Root = G.m_PrefabMgr.m_PrefabList.at(Guid.m_Instance.m_Value);
        for (auto K : Kids(G, M.m_Root))
        {
            const auto V = X(G, K);
            if (V == 2.0f) M.m_A = K; else if (V == 3.0f) M.m_B = K; else if (V == 4.0f) M.m_C = K;
        }
        return M;
    }

    // Root (X=1) -> B; children A (X=2) -> Root, B (X=3) -> A, C (X=4) -> Outside. Every member references another; C references an entity that is not in the prefab.
    void CheckTemplate(xecs::game_mgr::instance& G, xecs::prefab::guid Guid, bool bWithC)
    {
        const auto M = TemplateMembers(G, Guid);
        CHECK(X(G, M.m_Root) == 1.0f);
        CHECK(M.m_A.isValid() && M.m_B.isValid());
        CHECK(M.m_C.isValid() == bWithC);
        CHECK(Target(G, M.m_Root) == M.m_B);
        CHECK(Target(G, M.m_A) == M.m_Root);
        CHECK(Target(G, M.m_B) == M.m_A);
        if (bWithC) CHECK(Target(G, M.m_C).isValid() == false);
        for (auto K : Kids(G, M.m_Root))
        {
            entity P; (void)G.getEntity(K, [&](parent& Par) noexcept { P = Par.m_Value; });
            CHECK(P == M.m_Root);
        }
    }

    xecs::prefab::descriptor ReadDescriptor(xecs::prefab::guid G)
    {
        xecs::prefab::descriptor D;
        xproperty::settings::context C;
        CHECK(!D.Serialize(true, (Folder(G) / "Descriptor.txt").wstring(), C));
        return D;
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

    const auto Guid = MakeGuid(0x5704A6E1);
    const xecs::scene::guid SceneGuid{ "SmokeTestPrefabStorageScene" };

    {
        world W;
        STEP("1. make a prefab of a group with references between its members");
        auto Outside = W->getOrCreateArchetype<position>().CreateEntity([](position& P) noexcept { P.m_X = 99.0f; });
        auto Root    = W->getOrCreateArchetype<position, link, children>().CreateEntity([](position& P, link&, children&) noexcept { P.m_X = 1.0f; });
        auto A       = W->getOrCreateArchetype<position, link, parent>().CreateEntity([&](position& P, link& L, parent& Par) noexcept { P.m_X = 2.0f; L.m_Target = Root; Par.m_Value = Root; Par.m_Follow = parent::FOLLOW_X; });   // not the default: what is written is what comes back
        auto B       = W->getOrCreateArchetype<position, link, parent>().CreateEntity([&](position& P, link& L, parent& Par) noexcept { P.m_X = 3.0f; L.m_Target = A; Par.m_Value = Root; });
        auto C       = W->getOrCreateArchetype<position, link, parent>().CreateEntity([&](position& P, link& L, parent& Par) noexcept { P.m_X = 4.0f; L.m_Target = Outside; Par.m_Value = Root; });
        (void)W->getEntity(Root, [&](children& Ch, link& L) noexcept { Ch.m_List = { A, B, C }; L.m_Target = B; });

        // The group lives in a scene that names two of its entities.
        auto& Scene = W->m_SceneMgr.FindOrCreate(SceneGuid);
        const xecs::scene::permanent_id Ids[] = { 0x100, 0x101, 0x102, 0x103, 0x104 };
        const entity                    Es[]  = { Root, A, B, C, Outside };
        for (int i = 0; i < 5; ++i) { Scene.m_LocalToRuntime[Ids[i]] = Es[i]; Scene.m_RuntimeToLocal[Es[i].m_Value] = Ids[i]; }
        Scene.m_EntityNames[0x100] = "The Root";
        Scene.m_EntityNames[0x102] = "Bee";

        W->m_PrefabMgr.CreatePrefabFromEntity(Root, Guid);
        CheckTemplate(*W, Guid, true);

        STEP("2. the names of the members come with them");
        {
            const auto  M     = TemplateMembers(*W, Guid);
            const auto& Group = W->m_PrefabMgr.m_PrefabGroups.at(Guid.m_Instance.m_Value);
            CHECK(Group.m_EntityNames.size() == 2);
            CHECK(Group.m_EntityNames.contains(Group.m_RuntimeToLocal.at(M.m_Root.m_Value)) && Group.m_EntityNames.at(Group.m_RuntimeToLocal.at(M.m_Root.m_Value)) == "The Root");
            CHECK(Group.m_EntityNames.contains(Group.m_RuntimeToLocal.at(M.m_B.m_Value))    && Group.m_EntityNames.at(Group.m_RuntimeToLocal.at(M.m_B.m_Value))    == "Bee");
        }

        STEP("3. Save writes the scene format");
        CHECK(!W->m_PrefabMgr.Save(Guid));
        CHECK(fs::exists(Folder(Guid) / "Descriptor.txt"));
        CHECK(fs::exists(Folder(Guid) / "ComponentDeps.txt"));
        CHECK(!fs::exists(Folder(Guid) / "Entity.txt"));
        CHECK(CountEntityFiles(Guid) == 4);
        {
            const auto D     = ReadDescriptor(Guid);
            const auto& Group = W->m_PrefabMgr.m_PrefabGroups.at(Guid.m_Instance.m_Value);
            CHECK(D.m_Root == Group.m_RuntimeToLocal.at(W->m_PrefabMgr.m_PrefabList.at(Guid.m_Instance.m_Value).m_Value));
            CHECK(D.m_ActiveEntities.size() == 4);
            CHECK(std::is_sorted(D.m_ActiveEntities.begin(), D.m_ActiveEntities.end()));
            CHECK(D.m_EntityNames.size() == 2);
            const auto Deps = ReadAll(Folder(Guid) / "ComponentDeps.txt");
            CHECK(Deps.find("StoragePosition") != std::string::npos && Deps.find("StorageLink") != std::string::npos);
        }
    }

    STEP("4. a fresh world loads it back");
    {
        world W;
        CHECK(!W->m_PrefabMgr.EnsureLoaded(Guid));
        CheckTemplate(*W, Guid, true);
        CHECK(Follow(*W, TemplateMembers(*W, Guid).m_A) == parent::FOLLOW_X);
        const auto& Group = W->m_PrefabMgr.m_PrefabGroups.at(Guid.m_Instance.m_Value);
        CHECK(Group.m_LocalToRuntime.size() == 4);
        CHECK(Group.m_EntityNames.size() == 2);
        bool bRootNamed = false;
        for (auto& [Id, Name] : Group.m_EntityNames) bRootNamed |= (Name == "The Root" && Group.m_LocalToRuntime.at(Id) == W->m_PrefabMgr.m_PrefabList.at(Guid.m_Instance.m_Value));
        CHECK(bRootNamed);

        STEP("5. a member that left the prefab loses its file");
        const auto M = TemplateMembers(*W, Guid);
        (void)W->getEntity(M.m_Root, [&](children& Ch) noexcept { std::erase(Ch.m_List, M.m_C); });
        auto CE = M.m_C;
        W->DeleteEntity(CE);
        W->m_ArchetypeMgr.UpdateStructuralChanges();
        CHECK(!W->m_PrefabMgr.Save(Guid));
        CHECK(CountEntityFiles(Guid) == 3);
        CHECK(ReadDescriptor(Guid).m_ActiveEntities.size() == 3);

        STEP("6. a reference to a live entity outside the prefab: Save refuses, nothing is written");
        const auto Before  = ReadAll(Folder(Guid) / "Descriptor.txt");
        auto       Outside = W->getOrCreateArchetype<position>().CreateEntity([](position& P) noexcept { P.m_X = 77.0f; });
        (void)W->getEntity(M.m_A, [&](link& L) noexcept { L.m_Target = Outside; });
        std::printf("(a refusal message is expected next)\n");
        CHECK(W->m_PrefabMgr.Save(Guid));
        CHECK(ReadAll(Folder(Guid) / "Descriptor.txt") == Before);
        CHECK(CountEntityFiles(Guid) == 3);
        (void)W->getEntity(M.m_A, [&](link& L) noexcept { L.m_Target = M.m_Root; });
        CHECK(!W->m_PrefabMgr.Save(Guid));
    }

    STEP("7. the old format is read, and the next Save converts it");
    {
        // The old file, made from the new one: a PrefabGroupInfo record, then every member's record as it is in its entity file ("LocalId" for "PermanentId").
        const auto Old = MakeGuid(0x01DF0A7);
        const auto D   = ReadDescriptor(Guid);
        // bNoFollow: every Parent record as it was written before Follow was a field - "{ Parent:G  }", its separator, and the parent's id alone.
        const auto WriteOld = [&](xecs::prefab::guid To, bool bNoFollow)
        {
            fs::create_directories(Folder(To));
            std::string Text = std::format("\r\n[ PrefabGroupInfo ]\r\n{{ nMembers:d  RootLocalId:g }}\r\n//----------  -------------\r\n      {}         #{:X}  \r\n", D.m_ActiveEntities.size(), D.m_Root);
            for (auto& E : fs::recursive_directory_iterator(Folder(Guid) / "entity_db"))
            {
                if (!E.is_regular_file()) continue;
                auto Member = ReadAll(E.path());
                const auto At = Member.find("PermanentId:g");
                CHECK(At != std::string::npos);
                if (At != std::string::npos) Member.replace(At, std::strlen("PermanentId:g"), "LocalId:g");
                if (bNoFollow)      // the columns are as wide as their values: matched, not searched as text
                    Member = std::regex_replace(Member, std::regex(R"(\{ Parent:G +Follow:h \}\r\n//-+ +-+ *\r\n +(#[0-9A-F]+) +#[0-9A-F]+ *)"), "{ Parent:G  }\r\n//---------\r\n  $1");
                Text += Member;
            }
            if (bNoFollow) CHECK(Text.find("Follow:h") == std::string::npos && Text.find("{ Parent:G  }") != std::string::npos);
            std::ofstream(Folder(To) / "Entity.txt", std::ios::binary) << Text;
        };
        WriteOld(Old, false);

        world W;
        CHECK(!W->m_PrefabMgr.EnsureLoaded(Old));
        CheckTemplate(*W, Old, false);
        CHECK(W->m_PrefabMgr.m_PrefabGroups.at(Old.m_Instance.m_Value).m_LocalToRuntime.contains(D.m_Root));       // the old LocalIds are kept as the permanent ids
        CHECK(!W->m_PrefabMgr.Save(Old));
        CHECK(!fs::exists(Folder(Old) / "Entity.txt"));
        CHECK(CountEntityFiles(Old) == 3);
        CHECK(ReadDescriptor(Old).m_Root == D.m_Root);

        world W2;
        CHECK(!W2->m_PrefabMgr.EnsureLoaded(Old));
        CheckTemplate(*W2, Old, false);
        CHECK(Follow(*W2, TemplateMembers(*W2, Old).m_A) == parent::FOLLOW_X);

        STEP("8. a prefab that does not load leaves nothing behind");
        const auto Broken = MakeGuid(0xB40C3);
        fs::create_directories(Folder(Broken));
        std::ofstream(Folder(Broken) / "Entity.txt", std::ios::binary) << "this is not a prefab\r\n";
        CHECK(W2->m_PrefabMgr.EnsureLoaded(Broken));
        CHECK(!W2->m_PrefabMgr.m_PrefabList.contains(Broken.m_Instance.m_Value));
        CHECK(!W2->m_PrefabMgr.m_PrefabGroups.contains(Broken.m_Instance.m_Value));

        STEP("9. a Parent record without Follow (written before it was a field) loads, with the default");
        const auto NoFollow = MakeGuid(0xF0110);
        WriteOld(NoFollow, true);
        world W3;
        CHECK(!W3->m_PrefabMgr.EnsureLoaded(NoFollow));
        CheckTemplate(*W3, NoFollow, false);
        CHECK(Follow(*W3, TemplateMembers(*W3, NoFollow).m_A) == parent::FOLLOW_DEFAULT);
        CHECK(Follow(*W3, TemplateMembers(*W3, NoFollow).m_B) == parent::FOLLOW_DEFAULT);
    }

    fs::remove_all(fs::path(k_Project), Ec);
    if (g_Failures == 0) { std::printf("ALL CHECKS PASSED\n"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", g_Failures);
    return 1;
}
