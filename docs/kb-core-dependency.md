# How the port depends on kb-core

## Today: a hash-locked copy of kb-core `main`

kb-core (`kokeb-labs/kb-core`) is private and has no release tag yet, so this repository's CI cannot fetch it and
cannot pin a release. Until both change, the port carries an unchanged copy of the kb-core files it builds against
in `third_party/kb-core` (see [SOUP and kb-core](soup.md)):

- the copied commit is on kb-core's `main`; `tools/kb_core_snapshot.py --update` refuses any other commit, and the
  lock records the branch and the commit;
- the copy is the whole `cmake/` folder plus the three parts the port uses (`utils`, `rtos`, `interfaces`), their
  backends, the host test port and the test harness; documentation, drivers and the other parts are not copied;
- every configure and `tools/kb_core_snapshot.py --check` re-hash every file against `third_party/kb-core.lock`.

A re-pin is one commit: `--update` from a kb-core checkout at the new `main` commit, then the full pre-flight and the
firmware builds of every family.

## Plan: a kb-core release, fetched, never copied

The copy is an interim measure. It is replaced in these steps, in order:

1. **A kb-core release tag.** The port pins a tag (for example `v0.3.0` once the M3 to M5 work is tagged), never a
   branch commit. kb-core's own MISRA and test evidence belongs to its releases, so the port's SOUP record points
   at that release's evidence.
2. **CI access without a copy.** One of: kb-core becomes public before this port does (a public port cannot build
   against a private dependency anyway), or a read-only deploy key or fine-grained token for `kokeb-labs/kb-core`
   held as an organisation secret and used only by the checkout step of the shared workflows.
3. **Consumption.** `find_package(kb CONFIG REQUIRED COMPONENTS interfaces rtos utils)` stays the primary path
   (already supported by `cmake/kb_port_stm32_kb_core.cmake`). The fallback becomes
   `FetchContent_Declare(kb GIT_REPOSITORY https://github.com/kokeb-labs/kb-core GIT_TAG <tag commit>)` with the
   same post-fetch `git rev-parse HEAD` check the HAL fetch already does (`cmake/kb_port_stm32_hal.cmake`).
4. **Records.** kb-core in `docs/soup.md` as a pinned internal dependency with its release, and a compatibility row
   (port version x kb-core version) as kb-core's design foresees. A scheduled job builds the port against kb-core
   `main` to catch contract drift before a release.
5. **Removal.** `third_party/kb-core`, `third_party/kb-core.lock` and `tools/kb_core_snapshot.py` are deleted in the
   same change that adds the fetch.

No port code changes in any of these steps: the port uses kb-core only through its public `kb/<part>/` headers.
