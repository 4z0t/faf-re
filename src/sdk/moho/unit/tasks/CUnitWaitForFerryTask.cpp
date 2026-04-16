#include "moho/unit/tasks/CUnitWaitForFerryTask.h"

#include <new>

#include "moho/ai/IAiCommandDispatchImpl.h"
#include "moho/unit/core/Unit.h"

namespace
{
  constexpr std::uint64_t kUnitStateMaskWaitForFerry = (1ull << 21);
}

namespace moho
{
  /**
   * Address: 0x0060FAA0 (FUN_0060FAA0, Moho::CUnitWaitForFerryTask::CUnitWaitForFerryTask)
   * Mangled: ??0CUnitWaitForFerryTask@Moho@@QAE@@Z
   *
   * What it does:
   * Initializes wait-for-ferry task state from dispatch context, stores ferry
   * unit weak-link ownership, snapshots move goal payload, and sets owner unit
   * focus/state for ferry assignment.
   */
  CUnitWaitForFerryTask::CUnitWaitForFerryTask(
    Unit* const ferryUnit,
    IAiCommandDispatchImpl* const dispatch,
    const SNavGoal& moveGoal
  )
    : CCommandTask(static_cast<CCommandTask*>(dispatch))
    , mDispatch(dispatch)
    , mFerryUnit()
    , mMoveGoal(moveGoal)
  {
    mFerryUnit.Set(ferryUnit);

    Unit* const ownerUnit = mUnit;
    if (ownerUnit != nullptr) {
      ownerUnit->UnitStateMask |= kUnitStateMaskWaitForFerry;
      ownerUnit->SetFocusEntity(ferryUnit);
    }
  }

  /**
   * Address: 0x0060FB90 (FUN_0060FB90, Moho::CUnitWaitForFerryTask::~CUnitWaitForFerryTask)
   * Mangled: ??1CUnitWaitForFerryTask@Moho@@QAE@@Z
   *
   * What it does:
   * Clears owner assigned-transport/focus weak-link lanes, frees pending
   * occupancy-grid reservation, and drops wait-for-ferry state ownership.
   */
  CUnitWaitForFerryTask::~CUnitWaitForFerryTask()
  {
    Unit* const ownerUnit = mUnit;
    if (ownerUnit != nullptr) {
      ownerUnit->AssignedTransportRef.AsWeakPtr<Unit>().UnlinkFromOwnerChain();
      ownerUnit->SetFocusEntity(nullptr);
      ownerUnit->FreeOgridRect();
      ownerUnit->UnitStateMask &= ~kUnitStateMaskWaitForFerry;
    }

    mFerryUnit.UnlinkFromOwnerChain();
  }

  /**
   * Address: 0x0060FF50 (FUN_0060FF50, Moho::CUnitWaitForFerryTask::operator new)
   * Mangled: ??2CUnitWaitForFerryTask@Moho@@QAE@@Z
   *
   * What it does:
   * Allocates one wait-for-ferry task object and forwards dispatch, move-goal,
   * and ferry-unit context into in-place construction.
   */
  CUnitWaitForFerryTask* CUnitWaitForFerryTask::Create(
    IAiCommandDispatchImpl* const dispatch,
    const SNavGoal& moveGoal,
    Unit* const ferryUnit
  )
  {
    void* const storage = ::operator new(sizeof(CUnitWaitForFerryTask));
    if (!storage) {
      return nullptr;
    }

    try {
      return ::new (storage) CUnitWaitForFerryTask(ferryUnit, dispatch, moveGoal);
    } catch (...) {
      ::operator delete(storage);
      throw;
    }
  }
} // namespace moho
