# Scene save hardening (orphan check + atomic descriptor write)

> Both previously-deferred scene-persistence hardening items are now IMPLEMENTED (2026-09-12) - load-time orphan/dangling consistency check in EnsureLoaded, and SaveSceneDescriptor's atomic temp-file+rename write. Verified live; the orphan check immediately surfaced 36 real orphaned entity files in this actual project.
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-09-12).

Surfaced 2026-09-07 while reviewing an external AI's design document on entity/scene persistence
against this project's actual implementation (mostly accurate for our real architecture - one file
per entity, delete-at-save via real file removal, GUIDs never reused, scene descriptor kept
line-diffable for Git - all confirmed correct by direct code reading). Two things from that review
are genuinely worth building, but the user explicitly asked to document them for later, not implement
now. Documented as code comments (the natural place a future implementer will actually see them) in
`xecs_scene_inline.h`, not just here:

1. **Load-time consistency check** (comment on `DiscoverEntityIds`,
   `example.lionprj/Cache/dependencies/xECSV2/src/details/xecs_scene_inline.h` ~line 60) - a cheap
   O(n) set comparison between `DiscoverEntityIds`' own disk scan and `descriptor::m_ActiveEntities`,
   split into two severities: file-exists-but-not-listed = orphaned (low severity, offer cleanup);
   listed-but-file-missing = dangling reference (high severity, warn/block on load). `DiscoverEntityIds`
   already exists as exactly the building block this needs (currently unused by the normal load path,
   kept only as a manual repair tool) - this would be its first real caller.

2. **`SaveScene` is not atomic as a whole** (comment on `SaveScene`, same file, right before its
   `SaveSceneDescriptor` call) - it deletes/writes entity files one at a time in a loop, and only
   rewrites the descriptor (the GUID existence list) AFTER the whole loop finishes. A crash between an
   entity file being deleted and the descriptor rewrite leaves the descriptor claiming a GUID is still
   active with no file behind it - a real, reachable dangling-reference case, not just theoretical.
   Documented two independent, either-sufficient fixes: (a) item 1 above would catch it after the
   fact; (b) write the descriptor to a temp file + atomic rename, which at least guarantees the
   descriptor itself is never caught mid-write (doesn't make the whole multi-file save transactional,
   but closes the specific "descriptor claims something no longer true" failure mode).

An external review's claim that a crash mid-save "leaves exactly one of two consistent states, no
partially-applied state" was checked directly against the real `SaveScene` loop and found to be
FALSE for this codebase's current implementation - worth remembering if that same claim resurfaces
(e.g. from a future review, or written into docs) before item 2 above is actually built.

**2026-09-12: both items implemented and verified live**, direct user request to get unblocked on
deferred hardening debt rather than new feature work.

- **Item 1 (load-time check)**: `EnsureLoaded` (`xecs_scene_inline.h`) now calls `DiscoverEntityIds`
  and diffs it against `m_ActiveEntities` right after `LoadSceneDescriptor` succeeds - orphan files
  (on disk, not listed) get a low-severity warn-only printf; dangling references (listed, file
  missing) get a distinct, louder warning, layered ALONGSIDE (not replacing) the existing per-entity
  "failed to load, skipping" message the load loop already had. Deliberately does NOT block the load
  even for a dangling reference - matches this same function's own established "one bad entity never
  takes the whole scene down" philosophy, rather than introducing a new, more disruptive failure mode.
- **Item 2 (atomic descriptor write)**: `SaveSceneDescriptor` now serializes to `<path>.tmp` and
  `std::filesystem::rename`s it over the real path (atomic on the same volume) - a failed write never
  touches the real descriptor at all; a failed rename returns an error rather than silently succeeding.
  Does NOT make the whole multi-file `SaveScene` transactional (individual entity writes still aren't
  atomic with each other or with the descriptor) - closes only "the descriptor file itself is never
  caught half-written," exactly as originally scoped.

**Real finding, not a hypothetical**: the very first `EnsureLoaded` after this landed surfaced **36
genuine orphaned entity files** already sitting in this actual project's Scene 1 (`08C298C9F6668005`)
- accumulated cruft from earlier sessions' testing across many prior sessions, never cleaned up
before now because nothing was ever looking. Left in place (not deleted) - flagged to the user rather
than acted on unilaterally, per this project's own "don't delete without asking" rule.

**Verification method** (both halves): launched the app with stdout redirected to a file (`Start-Process
-RedirectStandardOutput`) since these are `printf`-based warnings with no visible console window
otherwise. Dangling-reference path deliberately constructed: created a throwaway entity via CLI, Saved
(writes both the file and the descriptor's active-list entry), manually `rm`'d just the `.entity` file
on disk (bypassing the normal Delete command), closed+reopened the scene - confirmed the dangling
warning fired with the exact right Id. Then Saved again (recomputes `m_ActiveEntities` purely from
live `m_LocalToRuntime`, which never contained the never-successfully-loaded entity) and reloaded once
more - confirmed the warning was gone and the descriptor had self-corrected, no manual file surgery
needed to clean up the test. Also confirmed no leftover `.tmp` file after several saves (atomic rename
completing cleanly every time).

**Immediate follow-up, same session - direct user correction**: "We should be careful about making
things slow... Load/Save etc should stay pretty fast... the sanity check is something that could have
run in the background." Right call - the check does a `recursive_directory_iterator` walk of the
WHOLE entity_db folder, which was sitting inline on `EnsureLoaded` (the Load/Play/Stop critical path).
First fix: moved it onto a detached background thread - `DiscoverEntityIds`/`SceneFolder`/
`EntityDbFolder` gained `std::wstring_view ProjectPath`-only overloads (the `mgr&`-taking ones now just
forward to them) specifically so a background thread never captures `mgr&` across the thread boundary -
a real lifetime hazard given Stop/hot-reload can destroy the whole GameMgr while a scan might still be
in flight.

**Second follow-up, same session - direct user scale concern**: "scenes could be about 1,000,000
entities... what is ok now with 10 may not scale so gracefully later." A background thread alone still
doesn't solve it at that scale - firing on EVERY Play/Stop/Load risks thread pileup (a 1M-entity scan
could easily outlive the gap between two reloads) and I/O contention with real work happening at the
same time. **Final design, per direct user follow-up ("it should go into a Sanity/Background process
step... when the editor becomes idle... call the section Idle Work")**: the check was pulled OUT of
`EnsureLoaded` entirely and now lives in a new, general-purpose idle-triggered maintenance system -
`e29_idle_work_system` (note pending migration) memory has the full design/implementation. The scene sanity scan is that
system's first task, not a special case baked into the scene-loading code itself.

Verified (both the background-thread version and, after the final move, the idle-triggered version):
`OpenLevel` round-trip ~76-87ms, `Play`+`Stop` round-trip ~109ms (dominated by E29CLI's own process-
spawn/pipe overhead, not any scan - because by the final design, no scan runs on this path AT ALL
anymore), warnings still correct (85 orphans across both scenes - the earlier "36" was a `tail -50`
undercount of one scene, not a regression; zero dangling references after the earlier test's
self-correction).
