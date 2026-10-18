# Code Review Report - Runners Journal / Sticky Arcade

**Date**: 2026-10-17  
**Reviewed By**: Mistral Vibe  
**Build**: firmware.elf (2026-10-07 10:47:18)  
**Target**: reTerminal E1005 (ESP32)  
**Branch**: vibe/october-series  

---

## Executive Summary

This review identified **1 critical security issue**, **4 high-priority refactoring needs**, and **4 medium-priority improvements** in the Runners Journal / Sticky Arcade firmware. The codebase is well-structured with good error handling patterns, but requires immediate attention to security and memory management for production deployment.

**Overall Assessment**: Code quality is GOOD with critical security and memory optimization needs.

---

## Severity Breakdown

| Severity | Count | Description |
|----------|-------|-------------|
| CRITICAL | 1 | Exposed secrets in repository |
| HIGH | 4 | Memory fragmentation, String overuse, network timeout, error propagation |
| MEDIUM | 4 | TODO comment, input validation, resource leaks, hardcoded paths |

---

## Critical Issues (MUST FIX BEFORE PRODUCTION)

### CRIT-001: Exposed Secrets in Repository
- **Location**: `src/secrets.h`
- **Impact**: WiFi credentials and Supabase API key committed to git
- **Risk**: Full device compromise if repository is accessed by unauthorized parties
- **Evidence**:
  ```
  #define WIFI_SSID "<redacted — rotate credentials>"
  #define WIFI_PASSWORD "<redacted — rotate credentials>"
  #define SUPABASE_ANON_KEY "<redacted — rotate anon key>"
  ```

**Recommended Fix**:
1. Remove from git: `git rm --cached src/secrets.h`
2. Add to .gitignore: `echo "secrets.h" >> .gitignore`
3. Create template with placeholder values
4. Use build-time secrets injection for CI/CD

---

## High Priority Issues

### HIGH-001: Memory Fragmentation Risk
- **Location**: `src/main.cpp` (lines 1802-1803, 1874-1875, 1894-1895), `src/epub_archive.cpp`
- **Pattern**: Multiple `ps_malloc` / `malloc` allocations with fallback pattern
- **Impact**: Heap fragmentation on ESP32 leading to allocation failures
- **Evidence**:
  ```cpp
  uint8_t* data = static_cast<uint8_t*>(ps_malloc(length));
  if (data == nullptr) data = static_cast<uint8_t*>(malloc(length));
  ```

**Recommended Fix**:
- Implement memory pooling for image buffers
- Use static allocation for known-size buffers
- Add memory defragmentation strategy

### HIGH-002: Arduino String Overuse
- **Location**: `src/dashboard_data.h` (16 String members across data structures)
- **Impact**: Cascading dynamic allocations, heap fragmentation
- **Evidence**:
  ```cpp
  struct JournalEntry {
    String dato;
    String type;
    String note;
  };
  std::vector<JournalEntry> journal;  // Each entry allocates 3x
  ```

**Recommended Fix**:
- Replace String with fixed char arrays: `char dato[16]`, `char type[32]`, `char note[256]`
- Use `strncpy` with bounds checking
- Add helper functions for safe string copying

### HIGH-003: Inconsistent Network Configuration
- **Location**: `src/runners_journal_config.h`
- **Impact**: 30-second timeout is excessive for battery-powered device
- **Evidence**:
  ```cpp
  constexpr uint32_t HTTP_TIMEOUT_MS = 30ULL * 1000ULL;
  ```

**Recommended Fix**:
- Reduce timeout to 10 seconds
- Add retry logic (2-3 attempts)
- Check Content-Length header before downloading
- Add exponential backoff for retries

### HIGH-004: Missing Error Propagation
- **Location**: `src/dashboard_parse.cpp`
- **Impact**: Silent failures in JSON parsing make debugging difficult
- **Evidence**:
  ```cpp
  if (o.isNull()) continue;  // No logging
  ```

**Recommended Fix**:
- Add error logging to all parse functions
- Propagate parse errors to caller
- Include context (array index, field name) in error messages

---

## Medium Priority Issues

### MEDIUM-001: Unresolved TODO Comment
- **Location**: `src/main.cpp:317`
- **Text**: "TODO: Integrate smoothFont from runners-journal for proper rendering"
- **Impact**: Technical debt, unclear implementation status

**Recommended Fix**: Resolve (implement or remove comment)

### MEDIUM-002: Missing Input Validation
- **Location**: `src/dashboard_fetch.cpp`
- **Impact**: Response size validated AFTER download (wastes bandwidth)
- **Evidence**: No Content-Length header check

**Recommended Fix**:
- Check Content-Length header before downloading
- Validate response size before parsing
- Add response signature verification

### MEDIUM-003: Resource Leak Potential
- **Location**: `src/dashboard_render.h` (SmoothFont class)
- **Impact**: Caller must remember to call unload()
- **Evidence**: Manual resource management without RAII

**Recommended Fix**:
- Create RAII wrapper (SmoothFontGuard)
- Use destructor-based cleanup
- Add resource tracking for debugging

### MEDIUM-004: Hardcoded Paths
- **Location**: Multiple files
- **Impact**: Difficult to test with different SD card structures
- **Evidence**: `/fonts/sans_bold_*.vlw` hardcoded in multiple places

**Recommended Fix**:
- Centralize path constants in `src/paths.h`
- Use configuration for SD card mount point
- Add path validation

---

## Strengths

1. **Error Handling**: Most code paths properly check for null/failure
2. **Memory Cleanup**: Consistent use of `free()` after `malloc`
3. **Fallback Strategies**: PSRAM -> regular RAM allocation fallbacks
4. **Type Safety**: Proper use of `static_cast` and type checking
5. **Resource Bounds**: Size limits defined (MAX_DASHBOARD_BODY_BYTES = 64KB)
6. **Validation**: `allocationBytes()` checks for overflow
7. **Testing**: Evidence of unit tests for core functionality

---

## Size Analysis

| Component | Size | Status |
|-----------|------|--------|
| firmware.bin | 2.9MB | Acceptable |
| firmware.elf | 24MB | Large (debug symbols) |
| Font headers | ~10MB | Major bloat source |
| Game assets | ~5MB | Could be external |

**Note**: `epub_latin_fonts.h` (6.2MB) and `game_ui_fonts.h` (3.7MB) are compiled into every build regardless of usage.

---

## Recommended Actions

### Immediate (Within 24 hours)
- [ ] Fix CRIT-001: Remove secrets from repository
- [ ] Verify secrets.h is in .gitignore
- [ ] Rotate exposed API keys and WiFi credentials

### Short-term (Within 1 week)
- [ ] Fix HIGH-001: Implement memory pooling
- [ ] Fix HIGH-002: Replace String with fixed buffers
- [ ] Fix HIGH-003: Improve network configuration
- [ ] Fix HIGH-004: Add error logging

### Medium-term (Within 1 month)
- [ ] Fix MEDIUM-001: Resolve TODO comment
- [ ] Fix MEDIUM-002: Add input validation
- [ ] Fix MEDIUM-003: Implement RAII for resources
- [ ] Fix MEDIUM-004: Centralize path constants
- [ ] Externalize font data to SD card

---

## Verification Commands

```bash
# Verify secrets not in git
git ls-files | grep -i secret
# Should return: NOTHING

# Verify .gitignore updated
cat .gitignore | grep secrets
# Should return: secrets.h

# Check firmware size
pio run -t size

# Verify String reduction
grep -r "String " src/dashboard_data.h
# Should return: 0 results after fix
```

---

## Files Analyzed

- `src/main.cpp` (112KB, main application logic)
- `src/dashboard_fetch.cpp` (network operations)
- `src/dashboard_parse.cpp` (JSON parsing)
- `src/dashboard_data.h` (data structures)
- `src/dashboard_render.h` (rendering logic)
- `src/epub_archive.cpp` (EPUB handling)
- `src/game_language_store.cpp` (configuration)
- `src/sticky_wifi_credentials.cpp` (WiFi management)
- `src/secrets.h` (security issue)
- `src/runners_journal_config.h` (configuration)

---

## Build Information

- **Build System**: PlatformIO
- **Target**: reTerminal E1005 (ESP32)
- **Framework**: Arduino
- **Latest Build**: 2026-10-07 10:47:18
- **Firmware Size**: 2.9MB (firmware.bin)

---

*Report generated by Mistral Vibe on 2026-10-17*