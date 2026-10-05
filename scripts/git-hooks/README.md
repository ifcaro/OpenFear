# Git hooks

Not installed automatically — git doesn't version `.git/hooks/`, so this
directory holds the scripts and each contributor opts in once:

```
git config core.hooksPath scripts/git-hooks
```

`pre-commit` blocks a commit if any staged `.cpp`/`.h` file doesn't match
the root `.clang-format`. Skips silently (doesn't block) if `clang-format`
isn't on PATH. The real enforcement point is CI
(`.github/workflows/format-check.yml`), which runs on every push/PR
regardless of whether a contributor set this up locally — this hook is a
faster local heads-up, not the source of truth.
