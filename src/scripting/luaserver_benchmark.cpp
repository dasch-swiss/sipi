/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * Per-request Lua VM cost, C++ runtime: `LuaServer` construction (full stdlib
 * via luaL_openlibs + createGlobals) + init-script source re-parse + execution,
 * with `require`d modules loaded from disk each time — the per-preflight /
 * per-route cost of the C++ path. Paired with the Rust-side
 * `//src/scripting/rust:runtime_benchmark`; run both on the same machine and
 * compare medians (`docs/src/development/benchmarking.md` discipline).
 *
 * The init script + script dir default to the repo's own
 * `config/sipi.init.lua` / `scripts/`; point SIPI_BENCH_INITSCRIPT /
 * SIPI_BENCH_SCRIPTDIR at a dsp-api checkout to measure the production
 * closure (9 files, 8 arriving via require).
 */

#include <benchmark/benchmark.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include "scripting/LuaServer.h"
#include "scripting/request_context.h"

namespace {

std::string env_or(const char *name, const char *fallback)
{
  const char *v = std::getenv(name);
  return (v != nullptr && v[0] != '\0') ? v : fallback;
}

std::string read_file(const std::string &path)
{
  std::ifstream in(path);
  if (in.fail()) { throw std::runtime_error("cannot read " + path); }
  return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

// VM build (luaL_openlibs + createGlobals) + init-script parse + execution —
// what make_lua_server pays before every preflight / Lua route today.
void BM_LuaServerVmBuildPlusInit(benchmark::State &state)
{
  const std::string initscript = env_or("SIPI_BENCH_INITSCRIPT", "config/sipi.init.lua");
  const std::string scriptdir = env_or("SIPI_BENCH_SCRIPTDIR", "scripts");
  const std::string src = read_file(initscript);
  const std::string lua_scriptdir = scriptdir + "/?.lua";
  for (auto _ : state) {
    shttps::RequestContext ctx;
    shttps::LuaServer vm(ctx, src, /*iscode=*/true, lua_scriptdir);
    benchmark::DoNotOptimize(vm.lua());
  }
}
BENCHMARK(BM_LuaServerVmBuildPlusInit)->Unit(benchmark::kMicrosecond);

// VM build alone (no init script) — isolates the luaL_openlibs +
// createGlobals cost from the script parse/exec cost.
void BM_LuaServerVmBuildOnly(benchmark::State &state)
{
  const std::string scriptdir = env_or("SIPI_BENCH_SCRIPTDIR", "scripts");
  const std::string lua_scriptdir = scriptdir + "/?.lua";
  for (auto _ : state) {
    shttps::RequestContext ctx;
    shttps::LuaServer vm(ctx, "", /*iscode=*/true, lua_scriptdir);
    benchmark::DoNotOptimize(vm.lua());
  }
}
BENCHMARK(BM_LuaServerVmBuildOnly)->Unit(benchmark::kMicrosecond);

// A fixed compute chunk on an already-built VM — the no-hook baseline the
// Rust runtime's hook-overhead numbers compare against.
void BM_LuaServerComputeChunk(benchmark::State &state)
{
  shttps::RequestContext ctx;
  shttps::LuaServer vm(ctx, "", /*iscode=*/true, "scripts/?.lua");
  const std::string chunk = "local s = 0 for i = 1, 100000 do s = s + i % 7 end return s";
  for (auto _ : state) { benchmark::DoNotOptimize(vm.executeChunk(chunk, "bench")); }
}
BENCHMARK(BM_LuaServerComputeChunk)->Unit(benchmark::kMicrosecond);

}// namespace
