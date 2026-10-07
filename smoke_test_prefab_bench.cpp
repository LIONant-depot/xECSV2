#include "xecs.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <atomic>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <filesystem>
#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

//------------------------------------------------------------------------------------------------
// Prefab baseline benchmark (documentation/Editors/prefabs_plan.md, phase 0). BUILD IT IN RELEASE (the numbers mean nothing in Debug):
//
//      python source/Editors/LevelEditor/smoke/prefab_bench.py          (compiles this file with cl /O2 and runs it)
//      smoke_test_prefab_bench.exe [--quick] [--check]
//
// What it measures, for prefabs of 1, 10 and 50 members, flat and nested two deep (a prefab holding an instance of a prefab holding an instance of a prefab), with entity references between
// the members (every member points at another member of its own prefab):
//   * spawn:  prefab::mgr::CreatePrefabInstance(Count, guid, {}, bRemoveRoot=false) - since phase 4 it is prefab::mgr::Spawn, the baked plan (the editor's InstantiatePrefab spawns with it too) - for
//             Count = 1, 100 and 10,000: time per instance, time per entity, and heap allocations (global operator new) per instance. Every cell runs in a fresh world, after one cold spawn (it bakes
//             the plan) that is timed on its own.
//   * --check: the phase 4 targets as a gate (test_prefab_spawn.py): batched (Count >= 100) under 100 ns per entity, no heap allocation per instance but the children lists (one per member that has
//             children: a std::vector), and nested within 1.5x flat of the same size. It fails when spawning slows down by about 2x from what phase 4 measured.
//   * load:   prefab::mgr::EnsureLoaded from the text the editor writes (what the game pays when a template is first needed), with the stdout noise of Save/EnsureLoaded going to NUL.
// Each cell is also checked: the number of entities spawned, and every reference of every spawned entity (link, parent, children) pointing at an entity of the same instance and never at the template.
//
// Not counted: heap use outside operator new (the pools take their pages from the system), and the work a builder system would add (none is registered here).
//------------------------------------------------------------------------------------------------

namespace
{
    std::atomic<std::uint64_t> g_Allocs{ 0 };
    std::atomic<std::uint64_t> g_Bytes { 0 };
}

void* operator new     (std::size_t n)                         { g_Allocs.fetch_add(1, std::memory_order_relaxed); g_Bytes.fetch_add(n, std::memory_order_relaxed); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc{}; }
void* operator new[]   (std::size_t n)                         { return operator new(n); }
void  operator delete  (void* p) noexcept                      { std::free(p); }
void  operator delete[](void* p) noexcept                      { std::free(p); }
void  operator delete  (void* p, std::size_t) noexcept         { std::free(p); }
void  operator delete[](void* p, std::size_t) noexcept         { std::free(p); }

//------------------------------------------------------------------------------------------------

struct position
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "BenchPosition" };

    float m_X{};
    float m_Y{};
    float m_Z{};

    XPROPERTY_DEF
    ( "BenchPosition", position
    , obj_member<"X", &position::m_X>
    , obj_member<"Y", &position::m_Y>
    , obj_member<"Z", &position::m_Z>
    )
};
XPROPERTY_REG(position)

struct link
{
    constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "BenchLink" };

    xecs::component::entity m_Target{};

    XPROPERTY_DEF
    ( "BenchLink", link
    , obj_member<"Target", &link::m_Target>
    )
};
XPROPERTY_REG(link)

//------------------------------------------------------------------------------------------------

namespace
{
    using clock_t_ = std::chrono::steady_clock;
    const std::wstring k_Project = L"smoke_test_prefab_bench_data";

    double MicrosSince(clock_t_::time_point T0) { return std::chrono::duration<double, std::micro>(clock_t_::now() - T0).count(); }

    struct world
    {
        std::unique_ptr<xecs::game_mgr::instance> m_pGM;

        world()
        {
            xecs::component::mgr::resetRegistrations();
            m_pGM = std::make_unique<xecs::game_mgr::instance>();
            m_pGM->RegisterComponents<position, link, xecs::editor::prefab_instance>();     // the host registers prefab_instance, as xLION's core does (nested prefabs need it)
            m_pGM->RegisterSystems<>();
            m_pGM->m_PrefabMgr.m_ProjectPath = k_Project;
        }
        ~world() { m_pGM.reset(); }
        xecs::game_mgr::instance& operator*() { return *m_pGM; }
        xecs::game_mgr::instance* operator->() { return m_pGM.get(); }
    };

    // stdout of Save/EnsureLoaded is a debug trace with a flush per line: it goes to NUL while they are timed.
    struct silence
    {
        int m_Saved = -1;
        silence()
        {
            std::fflush(stdout);
            m_Saved = _dup(1);
            const int Nul = _open("NUL", _O_WRONLY);
            _dup2(Nul, 1);
            _close(Nul);
        }
        ~silence()
        {
            std::fflush(stdout);
            _dup2(m_Saved, 1);
            _close(m_Saved);
        }
    };

    xecs::prefab::guid MakeGuid(std::uint64_t Id)
    {
        return xecs::prefab::guid{ .m_Instance = { (Id << 1) | 1 }, .m_Type = xecs::prefab::type_guid_v };     // an odd value is a guid, an even one is taken for a pointer to a resource
    }

    // A prefab of Members entities (Members >= 1): a root with Members-1 children directly under it, every child pointing at the root and the root at its first child. If pNested is given, the last
    // child is not a plain member but an instance of that other prefab (Members counts it as one member). The authored entities are destroyed once the prefab is made: only the template stays.
    xecs::prefab::guid Author(xecs::game_mgr::instance& G, std::uint64_t Id, int Members, const xecs::prefab::guid* pNested)
    {
        using namespace xecs::component;
        std::vector<entity> Authored;

        const bool bHasChildren = Members > 1;
        auto Root = bHasChildren ? G.getOrCreateArchetype<position, link, children>().CreateEntity([]( position& P, link&, children& ) noexcept { P.m_X = 1.0f; })
                                 : G.getOrCreateArchetype<position, link>().CreateEntity([]( position& P, link& ) noexcept { P.m_X = 1.0f; });
        Authored.push_back(Root);

        std::vector<entity> Kids;
        for (int i = 1; i < Members; ++i)
        {
            const bool bSlot = pNested && i == Members - 1;
            entity Kid;
            if (bSlot)
            {
                Kid = G.getOrCreateArchetype<position, link, parent, xecs::editor::prefab_instance>().CreateEntity([&]( position& P, link&, parent& Par, xecs::editor::prefab_instance& PI ) noexcept
                {
                    P.m_X = 2.0f; Par.m_Value = Root; PI.m_PrefabInstance = *pNested;
                });
            }
            else
            {
                Kid = G.getOrCreateArchetype<position, link, parent>().CreateEntity([&]( position& P, link& L, parent& Par ) noexcept
                {
                    P.m_X = 2.0f + static_cast<float>(i); Par.m_Value = Root; L.m_Target = Root;
                });
            }
            Kids.push_back(Kid);
            Authored.push_back(Kid);
        }

        if (bHasChildren)
        {
            (void)G.getEntity(Root, [&]( children& C ) noexcept { C.m_List = Kids; });
            (void)G.getEntity(Root, [&]( link& L ) noexcept { L.m_Target = Kids.front(); });
        }

        // The template is a copy whose references point at its own members (the clone moves them: prefabs_plan.md, phase 1). Before phase 1 it kept the handles of the entities it was copied
        // from, and this function patched them by hand.
        const auto Guid = MakeGuid(Id);
        G.m_PrefabMgr.CreatePrefabFromEntity(Root, Guid);

        for (auto It = Authored.rbegin(); It != Authored.rend(); ++It)
            G.DeleteEntity(*It);
        G.m_ArchetypeMgr.UpdateStructuralChanges();
        return Guid;
    }

    // How many entities one spawn of a prefab of Members members makes, flat or nested two deep (see Nested below).
    struct shape
    {
        const char* m_pName;
        int         m_Members;          // as the person counts it: the entities of one spawn
        bool        m_bNested;
    };

    // Nested two deep: A holds an instance of B holds an instance of C. The members are split over the three so that one spawn makes the same number of entities as the flat prefab of the same size
    // (a nested slot is one member of its own prefab and the whole inner prefab once spawned).
    struct built
    {
        xecs::prefab::guid                  m_Outer;
        int                                 m_Entities;
        std::vector<xecs::prefab::guid>     m_All;          // every prefab made (the outer one and what it nests)
    };

    built Build(xecs::game_mgr::instance& G, const shape& S, std::uint64_t Seed)
    {
        if (!S.m_bNested) { const auto Only = Author(G, Seed, S.m_Members, nullptr); return { Only, S.m_Members, { Only } }; }

        // entities = (a-1) + (b-1) + c with a, b the authored members of the outer and middle prefab (each has one slot)
        const int N = S.m_Members;
        const int a = (N + 2) / 3 + 1;
        const int b = (N + 2) / 3 + 1;
        const int c = N - (a - 1) - (b - 1);
        const auto Inner  = Author(G, Seed + 3, c, nullptr);
        const auto Middle = Author(G, Seed + 2, b, &Inner);
        const auto Outer  = Author(G, Seed + 1, a, &Middle);
        return { Outer, (a - 1) + (b - 1) + c, { Outer, Middle, Inner } };
    }

    // The instances in a world, as the systems see them (the templates carry an exclusive tag, so they are not here): every entity, who its parent is, and which instance it belongs to (the root it
    // reaches by following the parents: the nested instances hang from the slot of the prefab that holds them, so a nested spawn is one instance with all its entities).
    struct topology
    {
        std::unordered_set<std::uint64_t>                       m_All;
        std::unordered_map<std::uint64_t, std::uint64_t>        m_Parent;
        std::unordered_map<std::uint64_t, std::uint64_t>        m_Root;
        std::unordered_map<std::uint64_t, std::size_t>          m_Members;      // root -> entities of that instance
    };

    topology Scan(xecs::game_mgr::instance& G)
    {
        topology T;
        xecs::query::instance QAll;
        QAll.m_Must.AddFromComponents<position>();
        G.Foreach(G.Search(QAll), [&]( const xecs::component::entity& E, const position& ) noexcept { T.m_All.insert(E.m_Value); });

        xecs::query::instance QPar;
        QPar.m_Must.AddFromComponents<xecs::component::parent>();
        G.Foreach(G.Search(QPar), [&]( const xecs::component::entity& E, const xecs::component::parent& P ) noexcept { T.m_Parent[E.m_Value] = P.m_Value.m_Value; });

        for (auto E : T.m_All)
        {
            auto Top = E;
            for (auto It = T.m_Parent.find(Top); It != T.m_Parent.end() && T.m_All.count(It->second); It = T.m_Parent.find(Top)) Top = It->second;
            T.m_Root[E] = Top;
            ++T.m_Members[Top];
        }
        return T;
    }

    // Every entity visible to the systems is an entity of some instance, every instance has the entities of one spawn, and every reference of every entity (link, parent, children) lands in ITS OWN instance:
    // not in the template, not in nothing, and not in another instance (a remap that patched instance i with the members of instance j would be caught here).
    bool Verify(xecs::game_mgr::instance& G, std::size_t ExpectedInstances, std::size_t EntitiesPerInstance, bool bHasLinks, std::string& Why)
    {
        const auto T = Scan(G);
        if (T.m_All.size() != ExpectedInstances * EntitiesPerInstance) { Why = "spawned " + std::to_string(T.m_All.size()) + " entities, expected " + std::to_string(ExpectedInstances * EntitiesPerInstance); return false; }
        if (T.m_Members.size() != ExpectedInstances) { Why = std::to_string(T.m_Members.size()) + " instances, expected " + std::to_string(ExpectedInstances); return false; }
        for (auto& [Root, N] : T.m_Members) if (N != EntitiesPerInstance) { Why = "an instance has " + std::to_string(N) + " entities, expected " + std::to_string(EntitiesPerInstance); return false; }

        std::size_t Outside = 0, Foreign = 0, Links = 0;
        auto Check = [&]( std::uint64_t From, std::uint64_t To ) noexcept
        {
            auto It = T.m_Root.find(To);
            if (It == T.m_Root.end()) ++Outside;
            else if (It->second != T.m_Root.at(From)) ++Foreign;
        };

        xecs::query::instance QLink;
        QLink.m_Must.AddFromComponents<link>();
        G.Foreach(G.Search(QLink), [&]( const xecs::component::entity& E, const link& L ) noexcept { if (L.m_Target.isValid()) { ++Links; Check(E.m_Value, L.m_Target.m_Value); } });
        if (bHasLinks && Links == 0) { Why = "no link was set"; return false; }

        xecs::query::instance QPar;
        QPar.m_Must.AddFromComponents<xecs::component::parent>();
        G.Foreach(G.Search(QPar), [&]( const xecs::component::entity& E, const xecs::component::parent& P ) noexcept { Check(E.m_Value, P.m_Value.m_Value); });

        xecs::query::instance QKids;
        QKids.m_Must.AddFromComponents<xecs::component::children>();
        G.Foreach(G.Search(QKids), [&]( const xecs::component::entity& E, const xecs::component::children& C ) noexcept { for (auto& K : C.m_List) Check(E.m_Value, K.m_Value); });

        if (Outside || Foreign) { Why = std::to_string(Outside) + " reference(s) outside every instance (the template, or nothing) and " + std::to_string(Foreign) + " into ANOTHER instance"; return false; }
        return true;
    }

    // The check has to be able to fail: spawn three instances, make one link of one instance point into another, and Verify must say so. Run at the start of every benchmark.
    bool VerifyCanFail(std::string& Why)
    {
        world W;
        const auto B = Build(*W, { "check", 10, false }, 0xC0DE0000ull);
        W->m_PrefabMgr.CreatePrefabInstance(3, B.m_Outer, xecs::tools::empty_lambda{}, false);
        W->m_ArchetypeMgr.UpdateStructuralChanges();
        std::string Ignored;
        if (!Verify(*W, 3, B.m_Entities, true, Ignored)) { Why = "three good instances were refused: " + Ignored; return false; }

        const auto T = Scan(*W);
        std::vector<std::uint64_t> Holders;
        xecs::query::instance QLink;
        QLink.m_Must.AddFromComponents<link>();
        W->Foreach(W->Search(QLink), [&]( const xecs::component::entity& E, const link& ) noexcept { Holders.push_back(E.m_Value); });
        std::uint64_t Other = 0;
        for (auto H : Holders) if (T.m_Root.at(H) != T.m_Root.at(Holders.front())) { Other = H; break; }
        if (Other == 0) { Why = "found no second instance to point at"; return false; }
        W->Foreach(W->Search(QLink), [&]( const xecs::component::entity& E, link& L ) noexcept { if (E.m_Value == Holders.front()) L.m_Target.m_Value = Other; });
        if (Verify(*W, 3, B.m_Entities, true, Ignored)) { Why = "a link into another instance was not noticed"; return false; }
        return true;
    }

    struct cell
    {
        double       m_ColdUs        = 0;
        double       m_InstanceUs    = 0;
        double       m_EntityNs      = 0;
        double       m_AllocsPerInst = 0;
        double       m_BytesPerInst  = 0;
        std::size_t  m_Instances     = 0;
        bool         m_bOk           = true;
        std::string  m_Why;
    };

    cell RunSpawn(const shape& S, int Count, int Reps)
    {
        cell Result;
        world W;
        const auto B = Build(*W, S, 0x5EED0000ull + static_cast<std::uint64_t>(S.m_Members) * 16 + (S.m_bNested ? 8 : 0));

        const auto Cold0 = clock_t_::now();
        if (!W->m_PrefabMgr.CreatePrefabInstance(1, B.m_Outer, xecs::tools::empty_lambda{}, false)) { Result.m_bOk = false; Result.m_Why = "the prefab is not resident"; return Result; }
        Result.m_ColdUs = MicrosSince(Cold0);
        W->m_PrefabMgr.CreatePrefabInstance(1, B.m_Outer, xecs::tools::empty_lambda{}, false);   // warm: the second one is already steady state

        const auto A0 = g_Allocs.load(), By0 = g_Bytes.load();
        const auto T0 = clock_t_::now();
        for (int r = 0; r < Reps; ++r)
            W->m_PrefabMgr.CreatePrefabInstance(Count, B.m_Outer, xecs::tools::empty_lambda{}, false);
        const double Us = MicrosSince(T0);
        const auto Allocs = g_Allocs.load() - A0, Bytes = g_Bytes.load() - By0;

        // New entities stay hidden from the systems until the structural changes are resolved (the end of a system, in a frame). It is not timed: the creation counters it moves cost nothing next to the spawn.
        W->m_ArchetypeMgr.UpdateStructuralChanges();

        Result.m_Instances     = static_cast<std::size_t>(Count) * Reps;
        Result.m_InstanceUs    = Us / static_cast<double>(Result.m_Instances);
        Result.m_EntityNs      = Result.m_InstanceUs * 1000.0 / B.m_Entities;
        Result.m_AllocsPerInst = static_cast<double>(Allocs) / static_cast<double>(Result.m_Instances);
        Result.m_BytesPerInst  = static_cast<double>(Bytes)  / static_cast<double>(Result.m_Instances);
        Result.m_bOk           = Verify(*W, Result.m_Instances + 2, static_cast<std::size_t>(B.m_Entities), B.m_Entities > 1, Result.m_Why);
        return Result;
    }

    // Template load: author in one world, save with the text writer, then load in a fresh world (timed). Reps loads, mean.
    struct load_result { double m_Us = 0; bool m_bOk = true; std::string m_Why; };

    load_result RunLoad(const shape& S, int Reps)
    {
        load_result Result;
        std::error_code Ec;
        std::filesystem::remove_all(std::filesystem::path(k_Project), Ec);

        xecs::prefab::guid Outer;
        {
            world W;
            const auto Seed = 0x10AD0000ull + static_cast<std::uint64_t>(S.m_Members) * 16 + (S.m_bNested ? 8 : 0);
            const auto B = Build(*W, S, Seed);
            Outer = B.m_Outer;
            silence Quiet;
            for (auto& Guid : B.m_All)
                if (auto Err = W->m_PrefabMgr.Save(Guid); Err) { Result.m_bOk = false; Result.m_Why = "Save failed: " + std::string(Err.getMessage()); }
        }

        double Total = 0;
        for (int r = 0; r < Reps && Result.m_bOk; ++r)
        {
            world W;
            double Us;
            xerr   Err;
            {
                silence Quiet;
                const auto T0 = clock_t_::now();
                Err = W->m_PrefabMgr.EnsureLoaded(Outer);
                Us  = MicrosSince(T0);
            }
            if (Err) { Result.m_bOk = false; Result.m_Why = std::string("EnsureLoaded failed: ") + std::string(Err.getMessage()); break; }
            Total += Us;
        }
        Result.m_Us = Total / Reps;
        std::filesystem::remove_all(std::filesystem::path(k_Project), Ec);
        return Result;
    }
}

int main(int argc, char** argv)
{
    bool bQuick = false, bCheck = false;
    for (int i = 1; i < argc; ++i) { bQuick |= std::strcmp(argv[i], "--quick") == 0; bCheck |= std::strcmp(argv[i], "--check") == 0; }
    // Unattended: nothing in this program may wait for somebody to click a dialog (assert, abort, a crash report). It says what happened on stderr and ends.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    for (int t : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT }) { _CrtSetReportMode(t, _CRTDBG_MODE_FILE); _CrtSetReportFile(t, _CRTDBG_FILE_STDERR); }
#endif

    struct counts { int m_Count; int m_Reps; };
    const counts Counts[] = { { 1, bQuick ? 50 : 2000 }, { 100, bQuick ? 2 : 50 }, { 10000, 1 } };
    const shape  Shapes[] =
    { { "flat 1",    1, false }
    , { "flat 10",  10, false }
    , { "flat 50",  50, false }
    , { "nested 10",10, true  }
    , { "nested 50",50, true  }
    };

    std::printf("prefab spawn baseline (%s)\n", bQuick ? "quick" : "full");
#ifdef NDEBUG
    std::printf("build: Release\n");
#else
    std::printf("build: DEBUG - these numbers mean nothing, build with /O2 /DNDEBUG\n");
#endif
    int Failures = 0;
    {
        std::string Why;
        if (VerifyCanFail(Why)) std::printf("self-check: a link into another instance is noticed\n");
        else { std::printf("self-check FAILED: %s\n", Why.c_str()); ++Failures; }
    }

    std::printf("\n%-10s %7s %9s | %12s %12s %12s %12s %10s\n", "prefab", "count", "instances", "us/instance", "ns/entity", "allocs/inst", "bytes/inst", "cold us");

    std::unordered_map<std::string, double> BestNs;     // "flat 10" -> the best batched ns/entity (the nested/flat gate)
    for (auto& S : Shapes)
    {
        // what a spawn allocates: the children list of each member that has children (flat: the root; nested two deep: the three roots)
        const double MaxAllocs = S.m_Members == 1 ? 0.0 : (S.m_bNested ? 3.0 : 1.0);
        for (auto& C : Counts)
        {
            if (bQuick && C.m_Count == 10000) continue;
            auto R = RunSpawn(S, C.m_Count, C.m_Reps);
            if (bCheck && R.m_bOk && C.m_Count >= 100)
            {
#ifdef NDEBUG
                if (R.m_EntityNs > 100.0) { R.m_bOk = false; R.m_Why = "slower than the target of 100 ns per entity"; }
#endif
                if (R.m_AllocsPerInst > MaxAllocs + 0.01) { R.m_bOk = false; R.m_Why = "allocates more than the children lists"; }   // + the spawn's own list growing once to the count
                auto& Best = BestNs[S.m_pName];
                Best = Best == 0 ? R.m_EntityNs : std::min(Best, R.m_EntityNs);
            }
            std::printf("%-10s %7d %9zu | %12.3f %12.1f %12.1f %12.0f %10.1f%s%s\n", S.m_pName, C.m_Count, R.m_Instances, R.m_InstanceUs, R.m_EntityNs, R.m_AllocsPerInst, R.m_BytesPerInst, R.m_ColdUs
                       , R.m_bOk ? "" : "   FAIL: ", R.m_bOk ? "" : R.m_Why.c_str());
            std::fflush(stdout);
            if (!R.m_bOk) ++Failures;
        }
    }
#ifdef NDEBUG
    if (bCheck)
        for (int Size : { 10, 50 })
        {
            const double Flat = BestNs["flat " + std::to_string(Size)], Nested = BestNs["nested " + std::to_string(Size)];
            const bool   bOk  = Flat > 0 && Nested > 0 && Nested <= 1.5 * Flat;
            std::printf("nested %d / flat %d: %.2f%s\n", Size, Size, Flat > 0 ? Nested / Flat : 0.0, bOk ? "" : "   FAIL: nested costs more than 1.5x flat");
            if (!bOk) ++Failures;
        }
#endif

    std::printf("\ntemplate load from text (EnsureLoaded, stdout to NUL, mean of %d)\n%-10s %12s\n", bQuick ? 5 : 50, "prefab", "us");
    for (auto& S : Shapes)
    {
        const auto R = RunLoad(S, bQuick ? 5 : 50);
        std::printf("%-10s %12.1f%s%s\n", S.m_pName, R.m_Us, R.m_bOk ? "" : "   FAIL: ", R.m_bOk ? "" : R.m_Why.c_str());
        if (!R.m_bOk) ++Failures;
    }

    if (Failures == 0) { std::printf("\nALL CHECKS PASSED\n"); return 0; }
    std::printf("\n%d CHECK(S) FAILED\n", Failures);
    return 1;
}
