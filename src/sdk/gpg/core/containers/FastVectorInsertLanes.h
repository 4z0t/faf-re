#pragma once

#include <cstddef>
#include <cstdint>

namespace gpg::core::legacy
{
  /**
   * Legacy fastvector runtime lane used by insert/grow helper families.
   */
  struct FastVectorInsertRuntimeView
  {
    std::byte* start;        // +0x00
    std::byte* finish;       // +0x04
    std::byte* capacity;     // +0x08
    std::byte* inlineOrigin; // +0x0C
  };

  static_assert(sizeof(FastVectorInsertRuntimeView) == 0x10, "FastVectorInsertRuntimeView size must be 0x10");

  /**
   * Scratch storage for one dword-lane fastvector with inline capacity for one
   * 4-byte element.
   */
  struct DwordVectorInlineScratch
  {
    FastVectorInsertRuntimeView view; // +0x00
    std::uint32_t inlineElement;      // +0x10
  };

  static_assert(sizeof(DwordVectorInlineScratch) == 0x14, "DwordVectorInlineScratch size must be 0x14");

  /**
   * Address: 0x0080ABE0 (FUN_0080ABE0)
   * Address: 0x00693430 (FUN_00693430)
   *
   * What it does:
   * Copies 28-byte elements from `[sourceBegin, sourceEnd)` into `destination`
   * and returns the advanced destination lane.
   */
  [[nodiscard]] std::byte* CopyForward28ByteLane(
    std::byte* destination,
    const std::byte* sourceEnd,
    const std::byte* sourceBegin
  ) noexcept;

  /**
   * Address: 0x0080B670 (FUN_0080B670)
   * Address: 0x0080B6E0 (FUN_0080B6E0)
   *
   * What it does:
   * Copies 28-byte elements backward from `[sourceBegin, sourceEnd)` into the
   * destination tail lane.
   */
  [[nodiscard]] std::byte* CopyBackward28ByteLane(
    std::byte* destination,
    const std::byte* sourceEnd,
    const std::byte* sourceBegin
  ) noexcept;

  /**
   * Address: 0x0080AB00 (FUN_0080AB00)
   *
   * What it does:
   * Allocates replacement storage for one 28-byte fastvector lane and
   * materializes prefix/insert/suffix slices into the new storage.
   */
  [[nodiscard]] std::byte* GrowInsert28ByteLane(
    FastVectorInsertRuntimeView& vectorView,
    std::size_t requestedCapacity,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd,
    std::byte* splitPosition
  );

  /**
   * Address: 0x0080A340 (FUN_0080A340)
   *
   * What it does:
   * Inserts one 28-byte range before `insertPosition`, growing storage when
   * capacity is insufficient.
   */
  [[nodiscard]] std::byte* AppendRange28ByteLane(
    FastVectorInsertRuntimeView& vectorView,
    std::byte* insertPosition,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd
  );

  /**
   * Address: 0x0080B150 (FUN_0080B150)
   * Address: 0x0080B2B0 (FUN_0080B2B0)
   * Address: 0x0080B3E0 (FUN_0080B3E0)
   *
   * What it does:
   * Copies 24-byte elements from `[sourceBegin, sourceEnd)` into `destination`
   * and returns the advanced destination lane.
   */
  [[nodiscard]] std::byte* CopyForward24ByteLane(
    std::byte* destination,
    const std::byte* sourceEnd,
    const std::byte* sourceBegin
  ) noexcept;

  /**
   * Address: 0x0080B750 (FUN_0080B750)
   * Address: 0x0080B7B0 (FUN_0080B7B0)
   * Address: 0x0080B810 (FUN_0080B810)
   * Address: 0x0080B870 (FUN_0080B870)
   * Address: 0x0080B8D0 (FUN_0080B8D0)
   * Address: 0x0080B930 (FUN_0080B930)
   *
   * What it does:
   * Copies 24-byte elements backward from `[sourceBegin, sourceEnd)` into the
   * destination tail lane.
   */
  [[nodiscard]] std::byte* CopyBackward24ByteLane(
    std::byte* destination,
    const std::byte* sourceEnd,
    const std::byte* sourceBegin
  ) noexcept;

  /**
   * Address: 0x0080B080 (FUN_0080B080)
   * Address: 0x0080B1E0 (FUN_0080B1E0)
   * Address: 0x0080B310 (FUN_0080B310)
   *
   * What it does:
   * Allocates replacement storage for one 24-byte fastvector lane and
   * materializes prefix/insert/suffix slices into the new storage.
   */
  [[nodiscard]] std::byte* GrowInsert24ByteLane(
    FastVectorInsertRuntimeView& vectorView,
    std::size_t requestedCapacity,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd,
    std::byte* splitPosition
  );

  /**
   * Address: 0x0080A8C0 (FUN_0080A8C0)
   * Address: 0x0080ACB0 (FUN_0080ACB0)
   * Address: 0x0080AE70 (FUN_0080AE70)
   * Address: 0x004E7460 (FUN_004E7460)
   *
   * What it does:
   * Inserts one 24-byte range before `insertPosition`, growing storage when
   * capacity is insufficient.
   */
  [[nodiscard]] std::byte* AppendRange24ByteLane(
    FastVectorInsertRuntimeView& vectorView,
    std::byte* insertPosition,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd
  );

  /**
   * Address: 0x00547C70 (FUN_00547C70)
   *
   * What it does:
   * Inserts one 20-byte range before `insertPosition`, growing storage when
   * capacity is insufficient.
   */
  [[nodiscard]] std::byte* AppendRange20ByteLane(
    FastVectorInsertRuntimeView& vectorView,
    std::byte* insertPosition,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd
  );

  /**
   * Address: 0x0080F460 (FUN_0080F460)
   * Address: 0x006D28A0 (FUN_006D28A0)
   *
   * What it does:
   * Copies 8-byte elements from `[sourceBegin, sourceEnd)` into `destination`
   * and returns the advanced destination lane.
   */
  [[nodiscard]] std::byte* CopyForward8ByteLane(
    std::byte* destination,
    const std::byte* sourceEnd,
    const std::byte* sourceBegin
  ) noexcept;

  /**
   * Address: 0x0080F390 (FUN_0080F390)
   *
   * What it does:
   * Allocates replacement storage for one 8-byte fastvector lane and
   * materializes prefix/insert/suffix slices into the new storage.
   */
  [[nodiscard]] std::byte* GrowInsert8ByteLane(
    FastVectorInsertRuntimeView& vectorView,
    std::size_t requestedCapacity,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd,
    std::byte* splitPosition
  );

  /**
   * Address: 0x0080EF20 (FUN_0080EF20)
   * Address: 0x007A24B0 (FUN_007A24B0)
   *
   * What it does:
   * Inserts one 8-byte range before `insertPosition`, growing storage when
   * capacity is insufficient.
   */
  [[nodiscard]] std::byte* AppendRange8ByteLane(
    FastVectorInsertRuntimeView& vectorView,
    std::byte* insertPosition,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd
  );

  /**
   * Address: 0x0080F550 (FUN_0080F550)
   *
   * What it does:
   * Copies 32-byte elements from `[sourceBegin, sourceEnd)` into `destination`
   * and returns the advanced destination lane.
   */
  [[nodiscard]] std::byte* CopyForward32ByteLane(
    std::byte* destination,
    const std::byte* sourceEnd,
    const std::byte* sourceBegin
  ) noexcept;

  /**
   * Address: 0x0080F490 (FUN_0080F490)
   *
   * What it does:
   * Allocates replacement storage for one 32-byte fastvector lane and
   * materializes prefix/insert/suffix slices into the new storage.
   */
  [[nodiscard]] std::byte* GrowInsert32ByteLane(
    FastVectorInsertRuntimeView& vectorView,
    std::size_t requestedCapacity,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd,
    std::byte* splitPosition
  );

  /**
   * Address: 0x0080F0A0 (FUN_0080F0A0)
   * Address: 0x0056E4A0 (FUN_0056E4A0)
   * Address: 0x00723200 (FUN_00723200)
   *
   * What it does:
   * Inserts one 32-byte range before `insertPosition`, growing storage when
   * capacity is insufficient.
   */
  [[nodiscard]] std::byte* AppendRange32ByteLane(
    FastVectorInsertRuntimeView& vectorView,
    std::byte* insertPosition,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd
  );

  /**
   * Address: 0x0081BC90 (FUN_0081BC90)
   * Address: 0x006A0F70 (FUN_006A0F70)
   *
   * What it does:
   * Copies 12-byte elements from `[sourceBegin, sourceEnd)` into `destination`
   * and returns the advanced destination lane.
   */
  [[nodiscard]] std::byte* CopyForward12ByteLane(
    std::byte* destination,
    const std::byte* sourceEnd,
    const std::byte* sourceBegin
  ) noexcept;

  /**
   * Address: 0x0081BBC0 (FUN_0081BBC0)
   *
   * What it does:
   * Allocates replacement storage for one 12-byte fastvector lane and
   * materializes prefix/insert/suffix slices into the new storage.
   */
  [[nodiscard]] std::byte* GrowInsert12ByteLane(
    FastVectorInsertRuntimeView& vectorView,
    std::size_t requestedCapacity,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd,
    std::byte* splitPosition
  );

  /**
   * Address: 0x0081B830 (FUN_0081B830)
   *
   * What it does:
   * Inserts one 12-byte range before `insertPosition`, growing storage when
   * capacity is insufficient.
   */
  [[nodiscard]] std::byte* AppendRange12ByteLane(
    FastVectorInsertRuntimeView& vectorView,
    std::byte* insertPosition,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd
  );

  /**
   * Address: 0x006C0DE0 (FUN_006C0DE0)
   *
   * What it does:
   * Inserts one 16-byte range before `insertPosition`, growing storage when
   * capacity is insufficient.
   */
  [[nodiscard]] std::byte* AppendRange16ByteLane(
    FastVectorInsertRuntimeView& vectorView,
    std::byte* insertPosition,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd
  );

  /**
   * Address: 0x007F0C50 (FUN_007F0C50)
   *
   * What it does:
   * Copies 16-byte elements from `[sourceBegin, sourceEnd)` into
   * `destination`, stores the copied begin in `outBegin`, and advances
   * `rangeEnd` to the copied tail.
   */
  [[nodiscard]] std::byte* CopyForward16ByteLaneWithBeginOut(
    std::byte*& outBegin,
    std::byte* destination,
    std::byte*& rangeEnd,
    const std::byte* sourceBegin
  ) noexcept;

  /**
   * Address: 0x0092BD70 (FUN_0092BD70)
   *
   * What it does:
   * Copies 2-byte elements from `[sourceBegin, sourceEnd)` into `destination`
   * and returns the advanced destination lane.
   */
  [[nodiscard]] std::byte* CopyForwardWordLane(
    std::byte* destination,
    const std::byte* sourceEnd,
    const std::byte* sourceBegin
  ) noexcept;

  /**
   * Address: 0x0092CBF0 (FUN_0092CBF0)
   *
   * What it does:
   * Allocates replacement storage for one 2-byte fastvector lane and
   * materializes prefix/insert/suffix slices into the new storage.
   */
  [[nodiscard]] std::byte* GrowInsertWordLane(
    FastVectorInsertRuntimeView& vectorView,
    std::size_t requestedCapacity,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd,
    std::byte* splitPosition
  );

  /**
   * Address: 0x0092D9B0 (FUN_0092D9B0)
   *
   * What it does:
   * Inserts one 2-byte range before `insertPosition`, growing storage when
   * capacity is insufficient.
   */
  [[nodiscard]] std::byte* AppendRangeWordLane(
    FastVectorInsertRuntimeView& vectorView,
    std::byte* insertPosition,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd
  );

  /**
   * Address: 0x0080F660 (FUN_0080F660)
   * Address: 0x0082E7A0 (FUN_0082E7A0)
   * Address: 0x0084ED40 (FUN_0084ED40)
   * Address: 0x00868500 (FUN_00868500)
   * Address: 0x005407F0 (FUN_005407F0)
   * Address: 0x0059D370 (FUN_0059D370)
   * Address: 0x005C6CF0 (FUN_005C6CF0)
   * Address: 0x005D44A0 (FUN_005D44A0)
   * Address: 0x006FBE40 (FUN_006FBE40)
   * Address: 0x00702E40 (FUN_00702E40)
   * Address: 0x0072AB30 (FUN_0072AB30)
   * Address: 0x00774260 (FUN_00774260)
   * Address: 0x006E3ED0 (FUN_006E3ED0)
   * Address: 0x00706080 (FUN_00706080)
   *
   * What it does:
   * Copies 4-byte elements from `[sourceBegin, sourceEnd)` into `destination`
   * and returns the advanced destination lane.
   */
  [[nodiscard]] std::byte* CopyForwardDwordLane(
    std::byte* destination,
    const std::byte* sourceEnd,
    const std::byte* sourceBegin
  ) noexcept;

  /**
   * Address: 0x0080F590 (FUN_0080F590)
   * Address: 0x0082E6D0 (FUN_0082E6D0)
   * Address: 0x0084EC70 (FUN_0084EC70)
   * Address: 0x00868430 (FUN_00868430)
   * Address: 0x00540720 (FUN_00540720)
   * Address: 0x0059D2A0 (FUN_0059D2A0)
   * Address: 0x005C6C20 (FUN_005C6C20)
   * Address: 0x005D43D0 (FUN_005D43D0)
   * Address: 0x006FBD70 (FUN_006FBD70)
   * Address: 0x00702D70 (FUN_00702D70)
   * Address: 0x0072AA60 (FUN_0072AA60)
   * Address: 0x00774190 (FUN_00774190)
   *
   * What it does:
   * Allocates replacement storage for one 4-byte fastvector lane and
   * materializes prefix/insert/suffix slices into the new storage.
   */
  [[nodiscard]] std::byte* GrowInsertDwordLane(
    FastVectorInsertRuntimeView& vectorView,
    std::size_t requestedCapacity,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd,
    std::byte* splitPosition
  );

  /**
   * Address: 0x0080F210 (FUN_0080F210)
   * Address: 0x0082CF20 (FUN_0082CF20)
   * Address: 0x0084E740 (FUN_0084E740)
   * Address: 0x00867D40 (FUN_00867D40)
   * Address: 0x00540130 (FUN_00540130)
   * Address: 0x0059CD10 (FUN_0059CD10)
   * Address: 0x005C5270 (FUN_005C5270)
   * Address: 0x005D4020 (FUN_005D4020)
   * Address: 0x006FBC40 (FUN_006FBC40)
   * Address: 0x00702330 (FUN_00702330)
   * Address: 0x0072A850 (FUN_0072A850)
   * Address: 0x00774000 (FUN_00774000)
   *
   * What it does:
   * Inserts one 4-byte range before `insertPosition`, growing storage when
   * capacity is insufficient.
   */
  [[nodiscard]] std::byte* AppendRangeDwordLane(
    FastVectorInsertRuntimeView& vectorView,
    std::byte* insertPosition,
    const std::byte* sourceBegin,
    const std::byte* sourceEnd
  );

  /**
   * Address: 0x008D8190 (FUN_008D8190)
   * Address: 0x008D81C0 (FUN_008D81C0)
   *
   * What it does:
   * Copies 4-byte elements from `[sourceBegin, sourceEnd)` into `destination`
   * when the source range is provided first.
   */
  [[nodiscard]] std::byte* CopyForwardDwordLaneSourceFirst(
    const std::byte* sourceBegin,
    const std::byte* sourceEnd,
    std::byte* destination
  ) noexcept;

  /**
   * Address: 0x008D8150 (FUN_008D8150)
   *
   * What it does:
   * Copies 12-byte elements from `[sourceBegin, sourceEnd)` into `destination`
   * when the source range is provided first.
   */
  [[nodiscard]] std::byte* CopyForward12ByteLaneSourceFirst(
    const std::byte* sourceBegin,
    const std::byte* sourceEnd,
    std::byte* destination
  ) noexcept;

  /**
   * Address: 0x008F64D0 (FUN_008F64D0)
   *
   * What it does:
   * Copies 28-byte elements from `[sourceBegin, sourceEnd)` into `destination`
   * when the source range is provided first.
   */
  [[nodiscard]] std::byte* CopyForward28ByteLaneSourceFirst(
    const std::byte* sourceBegin,
    const std::byte* sourceEnd,
    std::byte* destination
  ) noexcept;

  /**
   * Address: 0x0071FC60 (FUN_0071FC60)
   *
   * What it does:
   * Copies 56-byte elements from `[sourceBegin, sourceEnd)` into `destination`
   * when the source range is provided first.
   */
  [[nodiscard]] std::byte* CopyForward56ByteLaneSourceFirst(
    const std::byte* sourceBegin,
    const std::byte* sourceEnd,
    std::byte* destination
  ) noexcept;

  /**
   * Address: 0x00756990 (FUN_00756990)
   *
   * What it does:
   * Copies 40-byte elements from `[sourceBegin, sourceEnd)` into `destination`
   * when the source range is provided first.
   */
  [[nodiscard]] std::byte* CopyForward40ByteLaneSourceFirst(
    const std::byte* sourceBegin,
    const std::byte* sourceEnd,
    std::byte* destination
  ) noexcept;

  /**
   * Address: 0x0082D030 (FUN_0082D030)
   *
   * What it does:
   * Replaces destination 4-byte fastvector content with source content,
   * reusing capacity when possible and growing when required.
   */
  FastVectorInsertRuntimeView&
  AssignDwordVectorRange(FastVectorInsertRuntimeView& destination, const FastVectorInsertRuntimeView& source);

  /**
   * Address: 0x0082E5E0 (FUN_0082E5E0)
   *
   * What it does:
   * Initializes one stack-style inline dword-vector scratch lane and assigns
   * source content into that lane via `AssignDwordVectorRange`.
   */
  [[nodiscard]] DwordVectorInlineScratch*
  InitializeDwordInlineScratchFromView(
    DwordVectorInlineScratch* destination,
    const FastVectorInsertRuntimeView& source
  );
} // namespace gpg::core::legacy
