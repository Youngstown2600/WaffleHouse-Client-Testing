# WaffleHouse-Client 5.6 macOS builder auto-repair

The macOS builder now verifies the actual Qt Multimedia CMake package after Homebrew dependency installation. If `qtmultimedia` is registered as installed but `Qt6MultimediaConfig.cmake` is missing, the builder automatically repairs/reinstalls the formula (unless `--no-auto-deps` was requested).

CMake is now given `Qt6_DIR` and `Qt6Multimedia_DIR` explicitly. This handles Homebrew's split Qt formula layout, where `qtbase` and `qtmultimedia` live in different prefixes. If the first CMake configure still fails, the builder repairs `qtmultimedia`, clears the CMake configure cache, and retries once automatically.
