# FAF-RE

Reconstruction/disassembly project for the old **Supreme Commander: Forged Alliance** engine and game binaries. Inspired by [Forged Alliance Forever](https://faforever.com) team-work.

## Recovery Coverage (`17/04/2026`, `fa_full_2026_03_26`)

Progress snapshot:

- Total FAF functions: `67,167`
  - *IDA index, exported*
- Progress coverage:  **`70.06%`**
  - *Consists of `recovered` + `skip` + `external_dependency` ÷ exported*
  - *Total amount of completed tokens: `47,056`*

Progress DB status breakdown:

- `recovered`: `37,564` (79.81%)
- `skip`: `5,707` (12.13%) — CRT-internal / compiler-generated / orphan template instantiations / static-init glue
- `external_dependency`: `3,785` (8.04%) — third-party libs
  - *libpng, zlib, wxWidgets, LuaPlus/Lua, boost, MSVC STL, CRI Sofdec/ADX, undname, bugsplat, CRT helpers*
- `needs_evidence`: `2,561` (3.81%)
- **`blocked`: `2,038` (3.03%)**
  - *strict circular/dep-blocked (in-DB literal `status == "blocked"`)*  
  - *combined with `needs_evidence`, the "not-yet-recovered non-engine-external" bucket is `4,599`*
  - *the `stats` tool's `blocked_count` aggregates the same two buckets and reports `4,599`*
    — functions previously attempted that depend on an unrecovered subsystem, a not-yet-typed owner class, or a non-trivial call-tree not yet walked bottom-up.

## Build Quickstart + Patches

Use a Visual Studio developer shell before `msbuild`:

```bat
%comspec% /k "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
```

Patch/bootstrap required external dependencies:

```bat
powershell -ExecutionPolicy Bypass -File scripts\bootstrap_boost_1_34_1_required.ps1 -BoostRoot "<your external boost path>"
powershell -ExecutionPolicy Bypass -File scripts\bootstrap_wxwindows_2_4_2_required.ps1 -WxRoot "<your external wxWindows-2.4.2 path>"
```

Build:

```bat
msbuild src\sdk\sdk.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32
```

This command is validated in a VS developer shell; expected result is `0 Error(s), 0 Warning(s)`.

Latest build coverage snapshot (`17/04/2026`):

- Command: `build_sdk.bat`
- Result: `0 Error(s), 0 Warning(s)`
- Verification log: `tmp/build_sdk_stdout_2026-04-17.log`

Recommended hang-safe build wrapper:

```bat
powershell -ExecutionPolicy Bypass -File scripts\build_sdk_with_timeout.ps1 -TimeoutMinutes 120 -NoOutputTimeoutMinutes 10
```

Patch/build details:

- [boost_1_34_1_faf_required.md](dependencies/patches/boost_1_34_1_faf_required.md)
- [luaplus_build1081_faf_required.md](dependencies/patches/luaplus_build1081_faf_required.md)
- [wildmagic3p8_faf_required.md](dependencies/patches/wildmagic3p8_faf_required.md)
- [wxwindows_2_4_2_faf_required.md](dependencies/patches/wxwindows_2_4_2_faf_required.md)

## Known Dependencies in FAF

- Boost `1.34.1`
- LuaPlus `5.0` build `1081`
- wxWidgets `2.4.2` (MSW)
- Wild Magic `3.8` (now Geometric Tools)
- [zlib `1.2.3`](https://github.com/OSDVF/zlib-win-x64)
- BugSplat
- CRI Middleware (Sofdec + ADX)
- DirectX 9/10 (with XACT audio; DX10 path is partial/non-functional in FA)

## Projects

- `inspect-injector`: injector/launcher for runtime inspection.
- `inspect`: injected DLL.
- `main`: FAF wrapper app.
- `sdk`: reconstructed SDK/game code.

## Notes

- `USE_X87_COMPATIBILITY`: keeps x87-compatible floating-point behavior where needed.

## Support / Donations

This reconstruction effort is heavily AI-assisted for large-scale binary/code analysis and recovery workflows.  
AI tools help speed up a big part of the work, but they also require multiple paid subscriptions and personal tooling costs.

If you want to support the project and help cover those AI-tools subscriptions, donations are welcome:

- BTC: `1PisoseCX7ALNvZnZAD28qax19gyi8VUE5`
- ETH: `0xb4CCC68BEfcDEA01eABfB8B2b8aBB87185e0D87e`

## Credits

Big thanks to all active maintainers and contributors of the FAF project. This work builds on many years of engine inspection and disassembly effort by the FAF community, with special recognition to:

- [Hdt80bro](https://github.com/Hdt80bro)
- [4z0t](https://github.com/4z0t)
