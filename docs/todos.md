# Development TODOs

## DEV-5160: Large Video File Streaming Bug

### Issue Context
- **Linear Issue**: DEV-5160 - DSP-APP unable to display 2.7 GB video from EKWS project
- **Status**: Todo, High Priority
- **Assignee**: Ivan Subotic
- **Git Branch**: feature/dev-5160-dsp-app-unable-to-display-27-gb-video-from-ekws-project

### Problem Description
- Video viewer shows "file does not exist" error for large video files (2.7 GB)
- Direct SIPI file download works fine
- Video streaming fails with specific error from Grafana logs:
  ```
  Internal server error: Error at [/tmp/sipi/shttps/Connection.cpp: 1412]: Trying to read beyond end of file!
  ```
- Issue is in SIPI's HTTP connection handling during range requests, not in DSP-APP frontend

### Root Cause Analysis
The error occurs at `Connection.cpp:1412` when SIPI tries to handle HTTP range requests for large video files. This suggests:
1. Buffer overflow or incorrect range calculation
2. File size miscalculation during streaming
3. HTTP range request header parsing issues
4. Memory management problems with large files

### Development Plan

#### Phase 1: Investigation & Understanding
1. **Examine Connection.cpp:1412** - Analyze the specific line causing "reading beyond end of file" error
2. **Research HTTP range request handling** - Understand current SIPI/SHTTPS range request implementation
   - Look for existing range request tests
   - Check HTTP header parsing logic
   - Examine file reading/streaming mechanisms

#### Phase 2: Test Development
3. **Create simulated large file test** - Unit test that simulates large video streaming without actual large files
   - Use mocks/stubs to simulate large file scenarios
   - Test various range request patterns
   - Focus on edge cases that trigger the error
4. **Reproduce the error** - Write specific unit test that triggers the "reading beyond end of file" scenario
   - Test boundary conditions
   - Test with different file sizes
   - Test malformed range requests

#### Phase 3: Fix & Verify
5. **Fix the root cause** - Address the range request bug in HTTP connection handling
   - Fix buffer/range calculations
   - Add proper bounds checking
   - Improve error handling
6. **Verify the fix** - Ensure tests pass and existing functionality isn't broken
   - Run all existing tests
   - Test with actual video files if needed
   - Performance testing for large files

### Technical Implementation Notes

#### Test Strategy
- **Location**: `test/unit/` using GoogleTest framework
- **Approach**: Mock large files programmatically, avoid adding large files to git
- **Focus**: SHTTPS connection logic and HTTP range request handling
- **Mock Strategy**: Create fake file descriptors or memory-mapped files

#### Key Files to Examine
- `/tmp/sipi/shttps/Connection.cpp` - Main error location
- `shttps/` directory - HTTP server implementation
- `include/SipiHttpServer.hpp` - SIPI HTTP server interface
- Existing tests in `test/unit/` for similar functionality

#### Error Patterns to Test
- Range requests beyond file size
- Malformed range headers
- Large file edge cases
- Memory allocation issues
- Buffer overflow scenarios

### Resources
- **Test video file**: [2.7 GB video](https://drive.google.com/file/d/1Raq3Qd7PY83S82MKZ2CcXdtKULLDIQJ8/view?usp=drive_link)
- **Reproduction steps**: Available in Linear issue
- **Error logs**: Available in Grafana

### Next Steps
1. Start with examining `Connection.cpp:1412`
2. Set up development environment with `just clang` or `nix develop`
3. Run existing tests to understand current test structure
4. Create initial unit test framework for range request testing

### Dependencies
- C++23 compiler (Clang ≥ 15.0)
- GoogleTest framework
- CMake build system
- Nix development environment (recommended)

---

*Last updated: 2025-07-17*
*Issue created: 2025-07-15*