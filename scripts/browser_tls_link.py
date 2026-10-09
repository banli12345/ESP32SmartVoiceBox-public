"""Keep text-browser TLS allocations in PSRAM; preserve other tasks' allocators."""
Import("env")

# ESP-IDF 5.5.5 exposes this allocator in the prebuilt mbedTLS libraries.
# Keep its real implementation for all tasks except TextBrowser.
env.Append(LINKFLAGS=["-Wl,--wrap=esp_mbedtls_mem_calloc"])
