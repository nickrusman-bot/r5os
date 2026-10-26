# R5 OS

This tree is an independent fork. AI-assisted work is allowed here.

Do not open GitHub pull requests against `haiku/haiku`.
Do not push to `git.haiku-os.org` or `review.haiku-os.org`.
Haiku does not accept AI-generated or AI-modified contributions.

`upstream` is fetch-only, for optionally merging official Haiku commits into the `r5` branch.
`origin` is the R5 OS repository and the source of truth.

GitHub pull requests on `origin` target **`main`** directly. Do not stack PRs on feature branches unless the user asks for that.

Always keep `tracker/STATUS.md` current. After any ticket-status change (`tracker-set.py`, overlay fix, wontfix, not-applicable), run `python3 scripts/tracker-report.py` and include `tracker/r5-status.tsv` plus the regenerated `tracker/STATUS.md` in the same commit/PR. Never leave STATUS.md stale.

## Architectures

R5 OS targets **64-bit only**:

1. **AMD64** (`x86_64`) — primary; first images and the Ryzen 5 5625U reference machine.
2. **ARM64** (`aarch64`) — second; QEMU first, then real hardware.
3. **RISC-V 64** (`riscv64`, RV64GC) — optional third, after AMD64/ARM64 are usable.

Do not spend effort on 32-bit x86, ARM32, PowerPC, SPARC, or m68k unless needed to keep the tree building. Do not drop Haiku’s existing 32-bit ports in drive-by commits.

## Versioning

Public releases: **R5 OS YYYY.N** (`2026.1`, `2026.2`, …). New year resets N to 1.
Unreleased trees use `YYYY.N-dev` in `distro/VERSION`. Jam `HAIKU_REVISION` is `r5-` plus that string (see `scripts/r5-version.sh`). Do not use Haiku `hrev` tags as the distro version.

## Code style

Follow Haiku coding guidelines for anything under `haiku/` (tabs for indent, 100-column limit, InterCaps types/functions, `f`/`k`/`g`/`s` prefixes, two blank lines between functions, C++ casts, `int32`/`status_t`, no style-only noise in a functional commit).

https://www.haiku-os.org/development/coding-guidelines/

Prefer matching the file you are editing over inventing a new style. Do not mix a bugfix with a drive-by reformat; style-only changes say "no functional change" in the commit message. Use `haiku-format` / `src/tools/checkstyle/` when the tree is available.
