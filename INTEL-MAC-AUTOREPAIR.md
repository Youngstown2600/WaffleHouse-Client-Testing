# Intel macOS Homebrew auto-repair

The macOS builder now repairs partial Intel Homebrew bootstraps before installing dependencies.
It creates and validates the Homebrew state/lock directories under `/usr/local/var/homebrew`
and repairs ownership only for Homebrew-managed `/usr/local` paths. It intentionally does not
recursively change ownership of all `/usr/local`.

This addresses failures such as:
- `mkdir: /usr/local/var: Permission denied`
- `Can't create brew vendor-install ruby lock in /usr/local/var/homebrew/locks`

After repair the normal dependency installation resumes, including the existing Intel source-build
fallback when a binary package is unavailable.
