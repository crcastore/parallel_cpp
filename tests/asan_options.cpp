// Catch2 triggers a spurious "container-overflow" in its internal
// TablePrinter / ConsoleReporter teardown when built with Apple libc++ + ASan.
// This is a known false-positive (not in our code). Disable that specific check
// so the sanitiser still catches real bugs in the code under test.
extern "C" const char *__asan_default_options()
{
    return "detect_container_overflow=0";
}
