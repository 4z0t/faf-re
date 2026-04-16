#pragma once

#include <cstddef>
#include <cstdint>

#include "moho/misc/WeakPtr.h"
#include "moho/path/SNavGoal.h"
#include "moho/task/CCommandTask.h"

namespace moho
{
  class IAiCommandDispatchImpl;
  class Unit;

  /**
   * Runtime owner for wait-for-ferry task command lanes.
   */
  class CUnitWaitForFerryTask : public CCommandTask
  {
  public:
    /**
     * Address: 0x0060FAA0 (FUN_0060FAA0, Moho::CUnitWaitForFerryTask::CUnitWaitForFerryTask)
     * Mangled: ??0CUnitWaitForFerryTask@Moho@@QAE@@Z
     *
     * What it does:
     * Initializes one wait-for-ferry task lane from dispatch context, stores
     * ferry weak-link ownership, copies navigation goal payload, and primes
     * unit focus/state for ferry assignment.
     */
    CUnitWaitForFerryTask(Unit* ferryUnit, IAiCommandDispatchImpl* dispatch, const SNavGoal& moveGoal);

    /**
     * Address: 0x0060FB90 (FUN_0060FB90, Moho::CUnitWaitForFerryTask::~CUnitWaitForFerryTask)
     * Mangled: ??1CUnitWaitForFerryTask@Moho@@QAE@@Z
     *
     * What it does:
     * Clears owner weak-link assignments for ferry/focus lanes, frees pending
     * occupancy-grid reservation, and drops the wait-for-ferry state bit.
     */
    ~CUnitWaitForFerryTask() override;

    /**
     * Address: 0x0060FF50 (FUN_0060FF50, Moho::CUnitWaitForFerryTask::operator new)
     * Mangled: ??2CUnitWaitForFerryTask@Moho@@QAE@@Z
     *
     * What it does:
     * Allocates one wait-for-ferry task object and forwards constructor
     * arguments into in-place construction.
     */
    [[nodiscard]] static CUnitWaitForFerryTask* Create(
      IAiCommandDispatchImpl* dispatch,
      const SNavGoal& moveGoal,
      Unit* ferryUnit
    );

  public:
    IAiCommandDispatchImpl* mDispatch; // 0x30
    WeakPtr<Unit> mFerryUnit;          // 0x34
    SNavGoal mMoveGoal;                // 0x3C
  };

  static_assert(sizeof(CUnitWaitForFerryTask) == 0x60, "CUnitWaitForFerryTask size must be 0x60");
  static_assert(
    offsetof(CUnitWaitForFerryTask, mDispatch) == 0x30, "CUnitWaitForFerryTask::mDispatch offset must be 0x30"
  );
  static_assert(
    offsetof(CUnitWaitForFerryTask, mFerryUnit) == 0x34, "CUnitWaitForFerryTask::mFerryUnit offset must be 0x34"
  );
  static_assert(
    offsetof(CUnitWaitForFerryTask, mMoveGoal) == 0x3C, "CUnitWaitForFerryTask::mMoveGoal offset must be 0x3C"
  );
} // namespace moho
