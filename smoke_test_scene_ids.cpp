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
#include <regex>
#include <filesystem>
#include <crtdbg.h>

//------------------------------------------------------------------------------------------------
// 64-bit permanent ids (documentation/Editors/prefabs_plan.md, phase 2):
//
//      python source/Editors/LevelEditor/smoke/test_entity_ids.py      (compiles this file in Debug - asserts on - and runs it)
//
//   0. The text form of an id: 8 hex digits when it fits in 32 bits, 16 otherwise; either is read back (and a short or lower case one).
//   1. A scene of entities whose ids are small, exactly 32 bits, just past 32 bits, and the largest id: saved. The files: an id that fits is
//      written as it always was (PermanentId column, no EntityId64 record, an 8 digit file name); one that does not has the record EntityId64 and a 16 digit file name.
//   2. A fresh world loads it back: every entity under the id it was saved with, every reference (same scene and to the parent scene, whose external table holds
//      a 64-bit id) resolved, names and folder members intact. Three ids that share their low 32 bits stay three entities. Saving again changes no byte.
//   3. DiscoverEntityIds finds the files of both widths.
//   4. A scene written before the widening (the descriptor's ids as u32) loads: the same data with the id rows turned back into u32 is read as it was.
//   5. A prefab whose members have big ids: saved, loaded by a fresh world; and a prefab descriptor written with u32 ids (phase 1's) loads.
//------------------------------------------------------------------------------------------------

struct position
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "IdsPosition" };

    float m_X{};

    XPROPERTY_DEF
    ( "IdsPosition", position
    , obj_member<"X", &position::m_X>
    )
};
XPROPERTY_REG(position)

struct link
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "IdsLink" };

    xecs::component::entity m_Target{};

    XPROPERTY_DEF
    ( "IdsLink", link
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

    const std::wstring k_Project = L"smoke_test_scene_ids_data";

    constexpr xecs::scene::permanent_id k_Small   = 0x1001ull;
    constexpr xecs::scene::permanent_id k_Edge32  = 0xFFFFFFFFull;
    constexpr xecs::scene::permanent_id k_First64 = 0x100000000ull;
    constexpr xecs::scene::permanent_id k_Big     = 0x7123456789ABCDEFull;
    constexpr xecs::scene::permanent_id k_Max     = 0x7FFFFFFFFFFFFFFFull;
    constexpr xecs::scene::permanent_id k_Same5   = 0x5ull;                // these three share their low 32 bits
    constexpr xecs::scene::permanent_id k_Same5b  = 0x100000005ull;
    constexpr xecs::scene::permanent_id k_Same5c  = 0x200000005ull;

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

    std::string ReadAll(const fs::path& P) { std::ifstream F(P, std::ios::binary); std::stringstream S; S << F.rdbuf(); return S.str(); }
    void        WriteAll(const fs::path& P, const std::string& T) { std::ofstream(P, std::ios::binary) << T; }

    // The on-disk contract, written out here instead of asked of the engine: <resource folder>/entity_db/<low byte>/<next byte>/<id>.entity, the id with 8 hex digits when it fits in 32 bits, 16 otherwise.
    fs::path EntityFile(const fs::path& ResourceFolder, std::uint64_t Id)
    {
        const auto Name = Id <= 0xFFFFFFFFull ? std::format("{:08X}", Id) : std::format("{:016X}", Id);
        return ResourceFolder / "entity_db" / std::format("{:02X}", Id & 0xFF) / std::format("{:02X}", (Id >> 8) & 0xFF) / (Name + ".entity");
    }

    fs::path SceneFolder(xecs::scene::guid G) { return fs::path(xecs::scene::details::SceneFolder(std::wstring_view(k_Project), G)); }

    fs::path PrefabFolder(xecs::prefab::guid G)
    {
        const auto V = G.m_Instance.m_Value;
        return fs::path(k_Project) / "Descriptors" / "Prefab" / std::format("{:02X}", V & 0xFF) / std::format("{:02X}", (V >> 8) & 0xFF) / std::format("{:X}.desc", V);
    }

    xecs::prefab::guid MakeGuid(std::uint64_t Id) { return xecs::prefab::guid{ .m_Instance = { (Id << 1) | 1 }, .m_Type = xecs::prefab::type_guid_v }; }

    entity Target(xecs::game_mgr::instance& G, entity E) { entity T; (void)G.getEntity(E, [&](link& L) noexcept { T = L.m_Target; }); return T; }
    float  X(xecs::game_mgr::instance& G, entity E)      { float V = -1; (void)G.getEntity(E, [&](position& P) noexcept { V = P.m_X; }); return V; }

    // Turns the id rows of a descriptor (the ones that match Paths) back into u32, what the descriptors held before the ids were widened. "u64" and "u32" are as wide: the columns stay where they are.
    int AsU32(const fs::path& Descriptor, const std::string& Paths)
    {
        auto Text = ReadAll(Descriptor);
        const std::regex Row("(\"(?:" + Paths + ")\"\\s+);u64");
        int n = 0;
        for (std::sregex_iterator I(Text.begin(), Text.end(), Row), End; I != End; ++I) ++n;
        Text = std::regex_replace(Text, Row, "$1;u32");
        WriteAll(Descriptor, Text);
        return n;
    }

    // A scene entity: a position and, when given, a link; registered in the scene under Id.
    entity Place(world& W, xecs::scene::instance& Scene, xecs::scene::permanent_id Id, float X, entity LinkTo = {})
    {
        auto E = W->getOrCreateArchetype<position, link>().CreateEntity([&](position& P, link& L) noexcept { P.m_X = X; L.m_Target = LinkTo; });
        Scene.m_LocalToRuntime[Id] = E;
        Scene.m_RuntimeToLocal[E.m_Value] = Id;
        return E;
    }

    void SaveWhole(world& W, xecs::scene::guid G, bool bNew)
    {
        auto& Scene = *W->m_SceneMgr.Find(G);
        for (auto& [Id, E] : Scene.m_LocalToRuntime) { if (bNew) W->m_SceneMgr.MarkEntityNew(G, Id); else W->m_SceneMgr.MarkEntityDirty(G, Id); }
        const auto Err = W->m_SceneMgr.SaveScene(G);
        CHECK(!Err);
    }

    std::string AllFilesOf(const fs::path& Folder)
    {
        std::vector<std::pair<std::string, std::string>> Files;
        for (auto& E : fs::recursive_directory_iterator(Folder)) if (E.is_regular_file()) Files.push_back({ E.path().generic_string(), ReadAll(E.path()) });
        std::sort(Files.begin(), Files.end());
        std::string All;
        for (auto& [Name, Text] : Files) All += "==" + Name + "\n" + Text;
        return All;
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

    STEP("0. the text form of an id");
    {
        CHECK(xecs::scene::FormatPermanentId(1) == "00000001");
        CHECK(xecs::scene::FormatPermanentId(k_Small) == "00001001");
        CHECK(xecs::scene::FormatPermanentId(k_Edge32) == "FFFFFFFF");
        CHECK(xecs::scene::FormatPermanentId(k_First64) == "0000000100000000");
        CHECK(xecs::scene::FormatPermanentId(k_Big) == "7123456789ABCDEF");
        CHECK(xecs::scene::FormatPermanentId(k_Max) == "7FFFFFFFFFFFFFFF");
        CHECK(xecs::scene::FormatPermanentIdW(k_Small) == L"00001001" && xecs::scene::FormatPermanentIdW(k_Big) == L"7123456789ABCDEF");
        for (auto Id : { k_Small, k_Edge32, k_First64, k_Big, k_Max, k_Same5, k_Same5b })
        {
            CHECK(xecs::scene::ParsePermanentId(xecs::scene::FormatPermanentId(Id).c_str()) == Id);
            CHECK(xecs::scene::ParsePermanentId(xecs::scene::FormatPermanentIdW(Id).c_str()) == Id);
        }
        CHECK(xecs::scene::ParsePermanentId("7123456789abcdef") == k_Big);          // lower case
        CHECK(xecs::scene::ParsePermanentId("1001") == k_Small);                    // shorter than 8 digits
        CHECK(xecs::scene::ParsePermanentId("") == xecs::scene::invalid_permanent_id_v);
        CHECK(xecs::scene::max_permanent_id_v == k_Max);
        CHECK(sizeof(xecs::scene::permanent_id) == 8);
    }

    const xecs::scene::guid ParentGuid{ "SmokeTestIdsParent" };
    const xecs::scene::guid ChildGuid { "SmokeTestIdsChild"  };
    std::string ChildFilesAfterFirstSave;

    {
        world W;
        STEP("1. a parent scene and a child scene whose entities have ids of every width; saved");
        auto& Parent = W->m_SceneMgr.FindOrCreate(ParentGuid);
        auto& Child  = W->m_SceneMgr.FindOrCreate(ChildGuid);
        Child.m_ParentScenes.push_back(ParentGuid);

        const auto PA = Place(W, Parent, k_Big,    10.0f);
        const auto PB = Place(W, Parent, k_Edge32, 11.0f);

        const auto C1 = Place(W, Child, k_First64, 1.0f, PA);               // to the parent scene: the external table
        const auto C2 = Place(W, Child, k_Small,   2.0f, C1);
        const auto C3 = Place(W, Child, k_Max,     3.0f, PB);               // to the parent scene again, the other id
        const auto C4 = Place(W, Child, k_Edge32,  4.0f, C3);
        const auto S1 = Place(W, Child, k_Same5,   5.0f, C2);               // three ids with the same low 32 bits
        const auto S2 = Place(W, Child, k_Same5b,  6.0f, S1);
        const auto S3 = Place(W, Child, k_Same5c,  7.0f, S2);
        (void)C4; (void)S3;

        Child.m_EntityNames[k_First64] = "Big One";
        Child.m_EntityNames[k_Small]   = "Small One";
        Child.m_EntityNames[k_Same5c]  = "Same Low Bits";
        Child.m_Folders.push_back( xecs::scene::folder{ .m_Id = 7, .m_Parent = 0, .m_Name = "Mixed", .m_Entities = { k_Max, k_Small, k_Same5b } } );

        SaveWhole(W, ParentGuid, true);
        SaveWhole(W, ChildGuid,  true);

        STEP("1b. the files");
        for (auto [Guid, Ids] : { std::pair{ ParentGuid, std::vector{ k_Big, k_Edge32 } }
                                , std::pair{ ChildGuid,  std::vector{ k_First64, k_Small, k_Max, k_Edge32, k_Same5, k_Same5b, k_Same5c } } })
        {
            for (auto Id : Ids)
            {
                const auto File = EntityFile(SceneFolder(Guid), Id);
                CHECK(fs::exists(File));
                if (!fs::exists(File)) continue;
                const auto Text = ReadAll(File);
                CHECK(Text.find("PermanentId:g") != std::string::npos);                                       // the column is the one it always was
                const bool bBig = Id > 0xFFFFFFFFull;
                CHECK((Text.find("EntityId64") != std::string::npos) == bBig);                               // and the extra record only exists for an id that does not fit
                if (bBig) CHECK(Text.find(std::format("#{:X}", Id)) != std::string::npos);
                CHECK(Text.find("EntityInfo") < Text.find("ComponentTypes"));
                if (bBig) CHECK(Text.find("EntityId64") > Text.find("EntityInfo") && Text.find("EntityId64") < Text.find("ComponentTypes"));
            }
        }
        {
            const auto Descriptor = ReadAll(SceneFolder(ChildGuid) / "Descriptor.txt");
            CHECK(Descriptor.find("\"Scene/ActiveEntities[]\"") != std::string::npos);
            CHECK(Descriptor.find(";u64") != std::string::npos);                                              // ids are u64 rows now
            CHECK(Descriptor.find("#7FFFFFFFFFFFFFFF") != std::string::npos);
            CHECK(Descriptor.find("\"Scene/ActiveEntities[G:0]\"") != std::string::npos);
            const auto Parents = ReadAll(SceneFolder(ChildGuid) / "Descriptor.txt");
            CHECK(Parents.find("ParentEntity") != std::string::npos);
            CHECK(Parents.find("#7123456789ABCDEF") != std::string::npos);                                    // the external table holds a 64-bit id
        }
        ChildFilesAfterFirstSave = AllFilesOf(SceneFolder(ChildGuid));

        STEP("3. DiscoverEntityIds finds the files of both widths");
        {
            const auto Found = xecs::scene::details::DiscoverEntityIds(std::wstring_view(k_Project), ChildGuid);
            const std::set<xecs::scene::permanent_id> F(Found.begin(), Found.end());
            const std::set<xecs::scene::permanent_id> Want{ k_First64, k_Small, k_Max, k_Edge32, k_Same5, k_Same5b, k_Same5c };
            CHECK(F == Want);
        }
    }

    {
        STEP("2. a fresh world loads them back");
        world W;
        CHECK(!W->m_SceneMgr.RequestLoad(ChildGuid));
        auto* pParent = W->m_SceneMgr.Find(ParentGuid);
        auto* pChild  = W->m_SceneMgr.Find(ChildGuid);
        CHECK(pParent && pChild);
        if (pParent && pChild)
        {
            CHECK(pParent->m_LocalToRuntime.size() == 2 && pChild->m_LocalToRuntime.size() == 7);
            const auto Of = [](xecs::scene::instance* pS, xecs::scene::permanent_id Id) { auto It = pS->m_LocalToRuntime.find(Id); return It == pS->m_LocalToRuntime.end() ? entity{} : It->second; };
            const auto PA = Of(pParent, k_Big),    PB = Of(pParent, k_Edge32);
            const auto C1 = Of(pChild, k_First64), C2 = Of(pChild, k_Small), C3 = Of(pChild, k_Max), C4 = Of(pChild, k_Edge32);
            const auto S1 = Of(pChild, k_Same5),   S2 = Of(pChild, k_Same5b), S3 = Of(pChild, k_Same5c);
            for (auto E : { PA, PB, C1, C2, C3, C4, S1, S2, S3 }) CHECK(E.isValid());

            CHECK(X(*W, PA) == 10.0f && X(*W, PB) == 11.0f);
            CHECK(X(*W, C1) == 1.0f && X(*W, C2) == 2.0f && X(*W, C3) == 3.0f && X(*W, C4) == 4.0f);
            CHECK(X(*W, S1) == 5.0f && X(*W, S2) == 6.0f && X(*W, S3) == 7.0f);                                  // the three that share their low 32 bits are three entities

            CHECK(Target(*W, C1) == PA);          // to the parent scene, the id past 32 bits, through the external table
            CHECK(Target(*W, C2) == C1);
            CHECK(Target(*W, C3) == PB);
            CHECK(Target(*W, C4) == C3);
            CHECK(Target(*W, S1) == C2 && Target(*W, S2) == S1 && Target(*W, S3) == S2);

            CHECK(pChild->m_ExternalRefTable.size() == 2);
            std::set<xecs::scene::permanent_id> External;
            for (auto& A : pChild->m_ExternalRefTable) External.insert(A.m_ParentEntity);
            CHECK(External == (std::set<xecs::scene::permanent_id>{ k_Big, k_Edge32 }));

            CHECK(pChild->m_EntityNames.size() == 3);
            CHECK(pChild->m_EntityNames.at(k_First64) == "Big One" && pChild->m_EntityNames.at(k_Small) == "Small One" && pChild->m_EntityNames.at(k_Same5c) == "Same Low Bits");
            CHECK(pChild->m_Folders.size() == 1 && pChild->m_Folders[0].m_Entities == (std::vector<xecs::scene::permanent_id>{ k_Max, k_Small, k_Same5b }));

            // The runtime-to-id map the editor looks entities up with agrees.
            CHECK(pChild->m_RuntimeToLocal.at(C3.m_Value) == k_Max && pChild->m_RuntimeToLocal.at(S3.m_Value) == k_Same5c);

            STEP("2b. saving what was loaded changes no byte");
            for (auto G : { ParentGuid, ChildGuid }) SaveWhole(W, G, false);
            CHECK(AllFilesOf(SceneFolder(ChildGuid)) == ChildFilesAfterFirstSave);
        }
    }

    {
        STEP("4. a scene written before the widening (u32 id rows in its descriptor) loads");
        const xecs::scene::guid OldParent{ "SmokeTestIdsOldParent" };
        const xecs::scene::guid OldChild { "SmokeTestIdsOldChild"  };
        {
            world W;
            auto& P = W->m_SceneMgr.FindOrCreate(OldParent);
            auto& C = W->m_SceneMgr.FindOrCreate(OldChild);
            C.m_ParentScenes.push_back(OldParent);
            const auto PA = Place(W, P, 0x7E570001, 1.0f);
            const auto C1 = Place(W, C, 0x10, 2.0f, PA);
            const auto C2 = Place(W, C, 0x7E570002, 3.0f, C1);
            (void)C2;
            C.m_EntityNames[0x10] = "Ten";
            C.m_Folders.push_back( xecs::scene::folder{ .m_Id = 3, .m_Parent = 0, .m_Name = "Old", .m_Entities = { 0x10, 0x7E570002 } } );
            SaveWhole(W, OldParent, true);
            SaveWhole(W, OldChild,  true);
        }
        // The entity files of ids that fit are what the old code wrote; the descriptors get their id rows back as u32.
        CHECK(ReadAll(EntityFile(SceneFolder(OldChild), 0x10)).find("EntityId64") == std::string::npos);
        const auto Rows = "Scene/ActiveEntities\\[G:\\d+\\]|Scene/EntityNames\\[G:\\d+\\]/Id|Scene/Folders\\[G:\\d+\\]/Entities\\[G:\\d+\\]|Scene/ExternalRefs\\[G:\\d+\\]/ParentEntity";
        CHECK(AsU32(SceneFolder(OldChild)  / "Descriptor.txt", Rows) == 2 + 1 + 2 + 1);
        CHECK(AsU32(SceneFolder(OldParent) / "Descriptor.txt", Rows) == 1);
        CHECK(ReadAll(SceneFolder(OldChild) / "Descriptor.txt").find(";u32    #10") != std::string::npos || ReadAll(SceneFolder(OldChild) / "Descriptor.txt").find(";u32") != std::string::npos);

        world W;
        CHECK(!W->m_SceneMgr.RequestLoad(OldChild));
        auto* pP = W->m_SceneMgr.Find(OldParent);
        auto* pC = W->m_SceneMgr.Find(OldChild);
        CHECK(pP && pC);
        if (pP && pC)
        {
            CHECK(pP->m_LocalToRuntime.size() == 1 && pP->m_LocalToRuntime.contains(0x7E570001));
            CHECK(pC->m_LocalToRuntime.size() == 2 && pC->m_LocalToRuntime.contains(0x10) && pC->m_LocalToRuntime.contains(0x7E570002));
            CHECK(Target(*W, pC->m_LocalToRuntime.at(0x10)) == pP->m_LocalToRuntime.at(0x7E570001));
            CHECK(Target(*W, pC->m_LocalToRuntime.at(0x7E570002)) == pC->m_LocalToRuntime.at(0x10));
            CHECK(pC->m_EntityNames.size() == 1 && pC->m_EntityNames.at(0x10) == "Ten");
            CHECK(pC->m_Folders.size() == 1 && pC->m_Folders[0].m_Entities == (std::vector<xecs::scene::permanent_id>{ 0x10, 0x7E570002 }));
            CHECK(pC->m_ExternalRefTable.size() == 1 && pC->m_ExternalRefTable[0].m_ParentEntity == 0x7E570001);
        }
    }

    {
        STEP("5. a prefab whose members have big ids");
        const auto Guid = MakeGuid(0x1D5A11);
        const xecs::scene::guid SceneGuid{ "SmokeTestIdsPrefabScene" };
        std::set<xecs::scene::permanent_id> Ids;
        xecs::scene::permanent_id RootId = 0;
        {
            world W;
            auto Root = W->getOrCreateArchetype<position, link, children>().CreateEntity([](position& P, link&, children&) noexcept { P.m_X = 1.0f; });
            auto A    = W->getOrCreateArchetype<position, link, parent>().CreateEntity([&](position& P, link& L, parent& Par) noexcept { P.m_X = 2.0f; L.m_Target = Root; Par.m_Value = Root; });
            auto B    = W->getOrCreateArchetype<position, link, parent>().CreateEntity([&](position& P, link& L, parent& Par) noexcept { P.m_X = 3.0f; L.m_Target = A; Par.m_Value = Root; });
            (void)W->getEntity(Root, [&](children& Ch, link& L) noexcept { Ch.m_List = { A, B }; L.m_Target = B; });
            auto& Scene = W->m_SceneMgr.FindOrCreate(SceneGuid);
            Scene.m_LocalToRuntime[0x100] = Root;  Scene.m_RuntimeToLocal[Root.m_Value] = 0x100;
            Scene.m_LocalToRuntime[0x101] = A;     Scene.m_RuntimeToLocal[A.m_Value]    = 0x101;
            Scene.m_LocalToRuntime[0x102] = B;     Scene.m_RuntimeToLocal[B.m_Value]    = 0x102;
            Scene.m_EntityNames[0x101] = "Ay";
            W->m_PrefabMgr.CreatePrefabFromEntity(Root, Guid);

            // The members take big ids (a prefab's ids are minted; this is what phase 3's derived ids will make of them).
            auto& Group = W->m_PrefabMgr.m_PrefabGroups.at(Guid.m_Instance.m_Value);
            decltype(Group.m_LocalToRuntime) L; decltype(Group.m_RuntimeToLocal) R; decltype(Group.m_EntityNames) N;
            std::uint64_t k = 0;
            for (auto& [Id, E] : Group.m_LocalToRuntime)
            {
                const xecs::scene::permanent_id To = (0x5A00000000ull + (++k << 36)) | (Id & 0xFFFFFFFFull);
                L[To] = E; R[E.m_Value] = To; Ids.insert(To);
                if (auto It = Group.m_EntityNames.find(Id); It != Group.m_EntityNames.end()) N[To] = It->second;
                if (E == W->m_PrefabMgr.m_PrefabList.at(Guid.m_Instance.m_Value)) RootId = To;
            }
            Group.m_LocalToRuntime = std::move(L); Group.m_RuntimeToLocal = std::move(R); Group.m_EntityNames = std::move(N);
            CHECK(Ids.size() == 3 && RootId > 0xFFFFFFFFull);

            CHECK(!W->m_PrefabMgr.Save(Guid));
            for (auto Id : Ids) CHECK(fs::exists(EntityFile(PrefabFolder(Guid), Id)));
            const auto Descriptor = ReadAll(PrefabFolder(Guid) / "Descriptor.txt");
            CHECK(Descriptor.find(std::format("#{:X}", RootId)) != std::string::npos);
            CHECK(Descriptor.find(";u64") != std::string::npos && Descriptor.find(";u32") == std::string::npos);
        }
        {
            world W;
            CHECK(!W->m_PrefabMgr.EnsureLoaded(Guid));
            const auto& Group = W->m_PrefabMgr.m_PrefabGroups.at(Guid.m_Instance.m_Value);
            std::set<xecs::scene::permanent_id> Loaded;
            for (auto& [Id, E] : Group.m_LocalToRuntime) Loaded.insert(Id);
            CHECK(Loaded == Ids);
            const auto Root = W->m_PrefabMgr.m_PrefabList.at(Guid.m_Instance.m_Value);
            CHECK(Group.m_RuntimeToLocal.at(Root.m_Value) == RootId);
            CHECK(X(*W, Root) == 1.0f);
            CHECK(Group.m_EntityNames.size() == 1);
            entity A, B;
            (void)W->getEntity(Root, [&](children& Ch) noexcept { for (auto K : Ch.m_List) { if (X(*W, K) == 2.0f) A = K; else B = K; } });
            CHECK(Target(*W, Root) == B && Target(*W, B) == A && Target(*W, A) == Root);

            STEP("5b. a template instance spawned from it keeps working (the spawn does not care about the ids)");
            CHECK(W->m_PrefabMgr.CreatePrefabInstance(2, Guid, xecs::tools::empty_lambda{}, false));
            W->m_ArchetypeMgr.UpdateStructuralChanges();
        }

        STEP("5c. a prefab descriptor written with u32 ids (phase 1's) loads");
        {
            const auto Old = MakeGuid(0x01D5A11);
            {
                world W;
                auto Root = W->getOrCreateArchetype<position, link, children>().CreateEntity([](position& P, link&, children&) noexcept { P.m_X = 1.0f; });
                auto A    = W->getOrCreateArchetype<position, link, parent>().CreateEntity([&](position& P, link& L, parent& Par) noexcept { P.m_X = 2.0f; L.m_Target = Root; Par.m_Value = Root; });
                (void)W->getEntity(Root, [&](children& Ch) noexcept { Ch.m_List = { A }; });
                auto& Scene = W->m_SceneMgr.FindOrCreate(SceneGuid);
                Scene.m_LocalToRuntime[0x200] = Root;  Scene.m_RuntimeToLocal[Root.m_Value] = 0x200;
                Scene.m_LocalToRuntime[0x201] = A;     Scene.m_RuntimeToLocal[A.m_Value]    = 0x201;
                Scene.m_EntityNames[0x201] = "Aye";
                W->m_PrefabMgr.CreatePrefabFromEntity(Root, Old);
                CHECK(!W->m_PrefabMgr.Save(Old));
            }
            CHECK(AsU32(PrefabFolder(Old) / "Descriptor.txt", "Prefab/Root|Prefab/ActiveEntities\\[G:\\d+\\]|Prefab/EntityNames\\[G:\\d+\\]/Id") == 1 + 2 + 1);
            world W;
            CHECK(!W->m_PrefabMgr.EnsureLoaded(Old));
            const auto& Group = W->m_PrefabMgr.m_PrefabGroups.at(Old.m_Instance.m_Value);
            CHECK(Group.m_LocalToRuntime.size() == 2);
            CHECK(Group.m_EntityNames.size() == 1);
            const auto Root = W->m_PrefabMgr.m_PrefabList.at(Old.m_Instance.m_Value);
            CHECK(X(*W, Root) == 1.0f);
            const auto RootIt = Group.m_RuntimeToLocal.find(Root.m_Value);
            CHECK(RootIt != Group.m_RuntimeToLocal.end());
        }
    }

    fs::remove_all(fs::path(k_Project), Ec);
    if (g_Failures == 0) { std::printf("ALL CHECKS PASSED\n"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", g_Failures);
    return 1;
}
