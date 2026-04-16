#include "ArchiveSerialization.h"

#include <cstddef>
#include <cstdint>
#include <typeinfo>

#include "gpg/core/reflection/Reflection.h"
#include "gpg/core/reflection/SerializationError.h"
#include "gpg/core/utils/BoostWrappers.h"
#include "gpg/core/utils/Global.h"
#include "moho/ai/EFormationdStatusTypeInfo.h"
#include "moho/ai/CAiFormationInstance.h"
#include "moho/ai/CAiPathSpline.h"
#include "moho/ai/IFormationInstanceCountedPtrReflection.h"
#include "moho/animation/CAniSkel.h"
#include "moho/animation/CAniPose.h"
#include "moho/entity/CTextureScroller.h"
#include "moho/entity/ECollisionBeamEvent.h"
#include "moho/entity/Entity.h"
#include "moho/entity/REntityBlueprintTypeInfo.h"
#include "moho/misc/CEconomyEvent.h"
#include "moho/misc/Listener.h"
#include "moho/misc/LaunchInfoBase.h"
#include "moho/misc/Stats.h"
#include "moho/path/PathTables.h"
#include "moho/resource/ISimResources.h"
#include "moho/resource/CParticleTextureReflection.h"
#include "moho/resource/RScaResource.h"
#include "moho/resource/RScmResource.h"
#include "moho/sim/CArmyStats.h"
#include "moho/sim/CEconStorage.h"
#include "moho/sim/CEconomy.h"
#include "moho/sim/CSquad.h"
#include "moho/sim/CIntelGrid.h"
#include "moho/sim/CWldSession.h"
#include "moho/sim/ReconBlip.h"
#include "moho/sim/Sim.h"
#include "moho/sim/SConditionTriggerTypes.h"
#include "moho/task/CCommandTask.h"
#include "moho/task/CTaskThread.h"
#include "moho/unit/core/Unit.h"
#include "moho/unit/tasks/CAcquireTargetTask.h"
#include "legacy/containers/Tree.h"
#include "ReadArchive.h"
#include "String.h"
#include "WriteArchive.h"

using namespace gpg;

namespace
{
  template <class T>
  [[nodiscard]] gpg::RType* CachedCompatRType()
  {
    static gpg::RType* sType = nullptr;
    if (sType == nullptr) {
      sType = gpg::LookupRType(typeid(T));
    }
    return sType;
  }

  template <class TObject>
  [[nodiscard]] gpg::RRef* BuildCompatTypedRef(gpg::RRef* const out, TObject* const object, gpg::RType* const staticType)
  {
    if (out == nullptr) {
      return nullptr;
    }

    out->mObj = nullptr;
    out->mType = staticType;
    if (object == nullptr) {
      return out;
    }

    gpg::RType* runtimeType = staticType;
    try {
      runtimeType = gpg::LookupRType(typeid(*object));
    } catch (...) {
      runtimeType = staticType;
    }

    int baseOffset = 0;
    if (runtimeType != nullptr && staticType != nullptr && runtimeType->IsDerivedFrom(staticType, &baseOffset)) {
      out->mObj = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(object) - static_cast<std::uintptr_t>(baseOffset)
      );
      out->mType = runtimeType;
      return out;
    }

    out->mObj = object;
    out->mType = runtimeType != nullptr ? runtimeType : staticType;
    return out;
  }

  template <class TView>
  void SaveContiguousArchiveVectorPayload(
    gpg::WriteArchive* const archive,
    const TView& view,
    gpg::RType* const elementType,
    const gpg::RRef& ownerRef
  )
  {
    const unsigned int count = view.begin != nullptr ? static_cast<unsigned int>(view.end - view.begin) : 0u;
    archive->WriteUInt(count);

    for (unsigned int i = 0; i < count; ++i) {
      archive->Write(elementType, view.begin + i, ownerRef);
    }
  }

  [[nodiscard]] gpg::RType* CachedPathQueueImplType()
  {
    static gpg::RType* sType = nullptr;
    if (sType == nullptr) {
      sType = gpg::REF_FindTypeNamed("Moho::PathQueue::Impl");
      if (sType == nullptr) {
        sType = gpg::REF_FindTypeNamed("PathQueue::Impl");
      }
      if (sType == nullptr) {
        sType = gpg::REF_FindTypeNamed("PathQueue_Impl");
      }
    }
    return sType;
  }

  [[nodiscard]] gpg::RType* CachedSOffsetInfoType()
  {
    static gpg::RType* sType = nullptr;
    if (sType == nullptr) {
      sType = gpg::REF_FindTypeNamed("Moho::SOffsetInfo");
    }
    return sType;
  }

  [[nodiscard]] gpg::RType* CachedSAssignedLocInfoType()
  {
    static gpg::RType* sType = nullptr;
    if (sType == nullptr) {
      sType = gpg::REF_FindTypeNamed("Moho::SAssignedLocInfo");
    }
    return sType;
  }
} // namespace

namespace gpg
{
  gpg::RRef* RRef_CAniSkel(gpg::RRef* const outRef, moho::CAniSkel* const value)
  {
    return BuildCompatTypedRef(outRef, value, CachedCompatRType<moho::CAniSkel>());
  }

  gpg::RRef* RRef_Stats_StatItem(gpg::RRef* const outRef, moho::Stats_StatItem* const value)
  {
    return BuildCompatTypedRef(outRef, value, CachedCompatRType<moho::Stats_StatItem>());
  }

  gpg::RRef* RRef_Listener_EFormationdStatus(
    gpg::RRef* const outRef, moho::Listener_EFormationdStatus* const value
  )
  {
    return BuildCompatTypedRef(outRef, value, CachedCompatRType<moho::Listener_EFormationdStatus>());
  }

  gpg::RRef* RRef_Sim(gpg::RRef* const outRef, moho::Sim* const value)
  {
    return BuildCompatTypedRef(outRef, value, CachedCompatRType<moho::Sim>());
  }

  gpg::RRef* RRef_CTaskStage(gpg::RRef* const outRef, moho::CTaskStage* const value)
  {
    return BuildCompatTypedRef(outRef, value, CachedCompatRType<moho::CTaskStage>());
  }

  gpg::RRef* RRef_RScaResource(gpg::RRef* const outRef, moho::RScaResource* const value)
  {
    return BuildCompatTypedRef(outRef, value, CachedCompatRType<moho::RScaResource>());
  }

  gpg::RRef* RRef_CEconStorage(gpg::RRef* const outRef, moho::CEconStorage* const value)
  {
    return BuildCompatTypedRef(outRef, value, CachedCompatRType<moho::CEconStorage>());
  }

  gpg::RRef* RRef_CEconomy(gpg::RRef* const outRef, moho::CEconomy* const value)
  {
    return BuildCompatTypedRef(outRef, value, CachedCompatRType<moho::CEconomy>());
  }

  gpg::RRef* RRef_CTextureScroller(gpg::RRef* const outRef, moho::CTextureScroller* const value)
  {
    return BuildCompatTypedRef(outRef, value, CachedCompatRType<moho::CTextureScroller>());
  }

  gpg::RRef* RRef_CSquad(gpg::RRef* const outRef, moho::CSquad* const value)
  {
    return BuildCompatTypedRef(outRef, value, CachedCompatRType<moho::CSquad>());
  }

  gpg::RRef* RRef_ManyToOneListener_ECollisionBeamEvent(
    gpg::RRef* const outRef,
    moho::ManyToOneListener_ECollisionBeamEvent* const value
  )
  {
    return BuildCompatTypedRef(outRef, value, CachedCompatRType<moho::ManyToOneListener_ECollisionBeamEvent>());
  }

  gpg::RRef* RRef_PathQueue_Impl(gpg::RRef* const outRef, moho::PathQueue::Impl* const value)
  {
    if (outRef == nullptr) {
      return nullptr;
    }

    outRef->mObj = value;
    outRef->mType = CachedPathQueueImplType();
    return outRef;
  }

  /**
   * Address: 0x0055D590 (FUN_0055D590)
   *
   * What it does:
   * Writes one contiguous `UnitWeaponInfo` payload by saving the element count
   * and each reflected lane in order.
   */
  void SaveFastVectorUnitWeaponInfo(
    gpg::WriteArchive* const archive,
    int objectPtr,
    int /*version*/,
    gpg::RRef* const ownerRef
  )
  {
    if (archive == nullptr || objectPtr == 0) {
      return;
    }

    const auto& view =
      gpg::AsFastVectorRuntimeView<moho::UnitWeaponInfo>(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(objectPtr)));
    SaveContiguousArchiveVectorPayload(archive, view, CachedCompatRType<moho::UnitWeaponInfo>(), ownerRef ? *ownerRef : gpg::RRef{});
  }

  /**
   * Address: 0x0056DF80 (FUN_0056DF80)
   *
   * What it does:
   * Writes one contiguous `SOffsetInfo` payload by saving the element count
   * and each reflected lane in order.
   */
  void SaveFastVectorSOffsetInfo(
    gpg::WriteArchive* const archive,
    int objectPtr,
    int /*version*/,
    gpg::RRef* const ownerRef
  )
  {
    if (archive == nullptr || objectPtr == 0) {
      return;
    }

    const auto& view = gpg::AsFastVectorRuntimeView<moho::SUnitOffsetInfo>(
      reinterpret_cast<const void*>(static_cast<std::uintptr_t>(objectPtr))
    );
    SaveContiguousArchiveVectorPayload(archive, view, CachedSOffsetInfoType(), ownerRef ? *ownerRef : gpg::RRef{});
  }

  /**
   * Address: 0x0056E0A0 (FUN_0056E0A0)
   *
   * What it does:
   * Writes one contiguous `SAssignedLocInfo` payload by saving the element
   * count and each reflected lane in order.
   */
  void SaveFastVectorSAssignedLocInfo(
    gpg::WriteArchive* const archive,
    int objectPtr,
    int /*version*/,
    gpg::RRef* const ownerRef
  )
  {
    if (archive == nullptr || objectPtr == 0) {
      return;
    }

    const auto& view = gpg::AsFastVectorRuntimeView<moho::SFormationOccupiedSlot>(
      reinterpret_cast<const void*>(static_cast<std::uintptr_t>(objectPtr))
    );
    SaveContiguousArchiveVectorPayload(
      archive, view, CachedSAssignedLocInfoType(), ownerRef ? *ownerRef : gpg::RRef{}
    );
  }

  /**
   * Address: 0x005B4FF0 (FUN_005B4FF0)
   *
   * What it does:
   * Writes one contiguous `CPathPoint` payload by saving the element count and
   * each reflected lane in order.
   */
  void SaveFastVectorCPathPoint(
    gpg::WriteArchive* const archive,
    int objectPtr,
    int /*version*/,
    gpg::RRef* const ownerRef
  )
  {
    if (archive == nullptr || objectPtr == 0) {
      return;
    }

    const auto& view =
      gpg::AsFastVectorRuntimeView<moho::CPathPoint>(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(objectPtr)));
    SaveContiguousArchiveVectorPayload(archive, view, CachedCompatRType<moho::CPathPoint>(), ownerRef ? *ownerRef : gpg::RRef{});
  }

  /**
   * Address: 0x005C5860 (FUN_005C5860)
   *
   * What it does:
   * Writes one contiguous `vector<SPerArmyReconInfo>` payload by saving the
   * element count and each reflected lane in order.
   */
  void SaveVectorSPerArmyReconInfo(
    gpg::WriteArchive* const archive,
    int objectPtr,
    int /*version*/,
    gpg::RRef* const ownerRef
  )
  {
    if (archive == nullptr || objectPtr == 0) {
      return;
    }

    const auto* const storage =
      reinterpret_cast<const msvc8::vector<moho::SPerArmyReconInfo>*>(static_cast<std::uintptr_t>(objectPtr));
    const auto& view = msvc8::AsVectorRuntimeView(*storage);
    SaveContiguousArchiveVectorPayload(
      archive, view, CachedCompatRType<moho::SPerArmyReconInfo>(), ownerRef ? *ownerRef : gpg::RRef{}
    );
  }

  /**
   * Address: 0x00702250 (FUN_00702250)
   *
   * What it does:
   * Writes one contiguous `vector<EntitySetTemplate<Unit>>` payload by saving
   * the element count and each reflected lane in order.
   */
  void SaveVectorEntitySetTemplateUnit(
    gpg::WriteArchive* const archive,
    int objectPtr,
    int /*version*/,
    gpg::RRef* const ownerRef
  )
  {
    if (archive == nullptr || objectPtr == 0) {
      return;
    }

    const auto* const storage = reinterpret_cast<const msvc8::vector<moho::EntitySetTemplate<moho::Unit>>*>(
      static_cast<std::uintptr_t>(objectPtr)
    );
    const auto& view = msvc8::AsVectorRuntimeView(*storage);
    SaveContiguousArchiveVectorPayload(
      archive, view, CachedCompatRType<moho::EntitySetTemplate<moho::Unit>>(), ownerRef ? *ownerRef : gpg::RRef{}
    );
  }

  class SerConstructResult
  {
  public:
    /**
     * Address: 0x0094F5E0 (FUN_0094F5E0, gpg::SerConstructResult::SetOwned)
     *
     * What it does:
     * Marks load-construct result ownership as `OWNED` and stores the
     * constructed reflected reference.
     */
    void SetOwned(const RRef& ref, unsigned int flags);

    /**
     * Address: 0x0094F630 (FUN_0094F630, gpg::SerConstructResult::SetUnowned)
     *
     * What it does:
     * Marks load-construct result ownership as `UNOWNED` and stores the
     * constructed reflected reference.
     */
    void SetUnowned(const RRef& ref, unsigned int flags);

    /**
     * Address: 0x0094F680 (FUN_0094F680, gpg::SerConstructResult::SetShared)
     * Mangled: ?SetShared@SerConstructResult@gpg@@QAEXABVRRef@2@I@Z_0
     *
     * What it does:
     * Marks load-construct result ownership as `SHARED` and stores one
     * reflected reference lane directly.
     */
    void SetShared(const RRef& ref, unsigned int flags);

    /**
     * Address: 0x0094F6D0 (FUN_0094F6D0, gpg::SerConstructResult::SetShared)
     *
     * What it does:
     * Marks load-construct result ownership as `SHARED`, retains one
     * `boost::shared_ptr<void>` lane, and stores the reflected reference.
     */
    void SetShared(const boost::shared_ptr<void>& object, RType* type, unsigned int flags);
  };

  class SerSaveConstructArgsResult
  {
  public:
    /**
     * Address: 0x0094F750 (FUN_0094F750, gpg::SerSaveConstructArgsResult::SetOwned)
     *
     * What it does:
     * Marks save-construct ownership lane as `OWNED` from the reserved state.
     */
    void SetOwned(unsigned int flags);

    /**
     * Address: 0x0094F7D0 (FUN_0094F7D0, gpg::SerSaveConstructArgsResult::SetShared)
     *
     * What it does:
     * Marks save-construct ownership lane as `SHARED` from the reserved state.
     */
    void SetShared(unsigned int flags);

    /**
     * Address: 0x0094F790 (FUN_0094F790, gpg::SerSaveConstructArgsResult::SetUnowned)
     *
     * What it does:
     * Marks save-construct ownership lane as `UNOWNED` from the reserved state.
     */
    void SetUnowned(unsigned int flags);
  };
} // namespace gpg

namespace
{
  struct SerConstructResultView
  {
    gpg::RRef mRef;                   // +0x00
    boost::SharedPtrRaw<void> mSharedPtr; // +0x08
    TrackedPointerState mState;       // +0x10
    std::uint8_t mSharedFlag;         // +0x14
  };
  static_assert(offsetof(SerConstructResultView, mRef) == 0x0, "SerConstructResultView::mRef offset must be 0x0");
  static_assert(
    offsetof(SerConstructResultView, mSharedPtr) == 0x8, "SerConstructResultView::mSharedPtr offset must be 0x8"
  );
  static_assert(offsetof(SerConstructResultView, mState) == 0x10, "SerConstructResultView::mState offset must be 0x10");
  static_assert(
    offsetof(SerConstructResultView, mSharedFlag) == 0x14, "SerConstructResultView::mSharedFlag offset must be 0x14"
  );
  static_assert(sizeof(SerConstructResultView) == 0x18, "SerConstructResultView size must be 0x18");

  struct SerSaveConstructArgsResultView
  {
    TrackedPointerState mOwnership;
    std::uint8_t mFlagByte4;
  };
  static_assert(
    offsetof(SerSaveConstructArgsResultView, mOwnership) == 0x0,
    "SerSaveConstructArgsResultView::mOwnership offset must be 0x0"
  );
  static_assert(
    offsetof(SerSaveConstructArgsResultView, mFlagByte4) == 0x4,
    "SerSaveConstructArgsResultView::mFlagByte4 offset must be 0x4"
  );

  struct TrackedPointerTreeNodeView : msvc8::Tree<TrackedPointerTreeNodeView>
  {
    gpg::RRef ref;                         // +0x0C
    std::uint8_t reserved14_24[0x11]{};   // +0x14
    std::uint8_t isNil = 0;               // +0x25
  };
  static_assert(offsetof(TrackedPointerTreeNodeView, ref) == 0x0C, "TrackedPointerTreeNodeView::ref offset must be 0x0C");
  static_assert(
    offsetof(TrackedPointerTreeNodeView, isNil) == 0x25,
    "TrackedPointerTreeNodeView::isNil offset must be 0x25"
  );

  struct TrackedPointerTreeView
  {
    void* unknown00 = nullptr;             // +0x00
    TrackedPointerTreeNodeView* head = nullptr; // +0x04
  };
  static_assert(offsetof(TrackedPointerTreeView, head) == 0x04, "TrackedPointerTreeView::head offset must be 0x04");

  /**
   * Address: 0x0094FA20 (FUN_0094FA20, _Tree_RRef_TrackedPointer::_Lbound)
   *
   * What it does:
   * Performs one lower-bound walk over the tracked-pointer RB-tree using
   * `RRef` key ordering (`mType`, then `mObj`) and returns the first node not
   * less than the probe key.
   */
  [[maybe_unused]] TrackedPointerTreeNodeView* FindLowerBoundTrackedPointerNode(
    TrackedPointerTreeView* const tree,
    const gpg::RRef& objectRef
  ) noexcept
  {
    TrackedPointerTreeNodeView* result = tree->head;
    TrackedPointerTreeNodeView* parent = result->parent;

    if (parent->isNil == 0) {
      gpg::RType* const probeType = objectRef.mType;
      do {
        gpg::RType* const nodeType = parent->ref.mType;
        bool nodeLessThanProbe = nodeType < probeType;

        if (nodeType == probeType) {
          nodeLessThanProbe = parent->ref.mObj < objectRef.mObj;
        }

        if (nodeLessThanProbe) {
          parent = parent->right;
        } else {
          result = parent;
          parent = parent->left;
        }
      } while (parent->isNil == 0);
    }

    return result;
  }

  constexpr const char* kSerializationCppPath = "c:\\work\\rts\\main\\code\\src\\libs\\gpgcore\\reflection\\serialization.cpp";

  [[noreturn]] void ThrowSerializationError(const char* const message)
  {
    throw SerializationError(message ? message : "");
  }

  [[noreturn]] void ThrowSerializationError(const msvc8::string& message)
  {
    throw SerializationError(message.c_str());
  }

  const char* SafeTypeName(const RType* const type)
  {
    return type ? type->GetName() : "null";
  }

  struct ReflectedObjectDeleter
  {
    gpg::RType::delete_func_t deleteFunc = nullptr;

    void operator()(void* const object) const noexcept
    {
      if (deleteFunc) {
        deleteFunc(object);
      }
    }
  };

  [[nodiscard]] gpg::RType* CachedLaunchInfoBaseType()
  {
    gpg::RType* type = moho::LaunchInfoBase::sType;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::LaunchInfoBase));
      moho::LaunchInfoBase::sType = type;
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedSessionSaveDataType()
  {
    static gpg::RType* sType = nullptr;
    if (!sType) {
      sType = gpg::LookupRType(typeid(moho::SSessionSaveData));
    }
    return sType;
  }

  /**
   * Address: 0x008849B0 (FUN_008849B0, func_CastSSessionSaveData)
   *
   * What it does:
   * Upcasts one reflected reference to `SSessionSaveData` and returns the
   * typed object pointer when the source is compatible.
   */
  [[nodiscard]] moho::SSessionSaveData* func_CastSSessionSaveData(const gpg::RRef& source)
  {
    const gpg::RRef upcast = gpg::REF_UpcastPtr(source, CachedSessionSaveDataType());
    return static_cast<moho::SSessionSaveData*>(upcast.mObj);
  }

  [[nodiscard]] gpg::RType* CachedCAniPoseType()
  {
    static gpg::RType* sType = nullptr;
    if (!sType) {
      sType = gpg::LookupRType(typeid(moho::CAniPose));
    }
    return sType;
  }

  [[nodiscard]] gpg::RType* CachedCAniSkelType()
  {
    static gpg::RType* sType = nullptr;
    if (!sType) {
      sType = gpg::LookupRType(typeid(moho::CAniSkel));
    }
    return sType;
  }

  [[nodiscard]] gpg::RType* CachedRScmResourceType()
  {
    static gpg::RType* sType = nullptr;
    if (!sType) {
      sType = gpg::LookupRType(typeid(moho::RScmResource));
    }
    return sType;
  }

  [[nodiscard]] gpg::RType* CachedSTriggerType()
  {
    gpg::RType* type = moho::STrigger::sType;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::STrigger));
      moho::STrigger::sType = type;
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedStatsStatItemType()
  {
    static gpg::RType* sType = nullptr;
    if (!sType) {
      sType = gpg::LookupRType(typeid(moho::Stats<moho::StatItem>));
    }
    return sType;
  }

  [[nodiscard]] gpg::RType* CachedISimResourcesType()
  {
    gpg::RType* type = moho::ISimResources::sType;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::ISimResources));
      moho::ISimResources::sType = type;
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedCIntelGridType()
  {
    static gpg::RType* type = nullptr;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::CIntelGrid));
    }
    return type;
  }

  template <class T>
  [[nodiscard]] boost::SharedPtrRaw<T>* AssignSharedPtrRawRetained(
    const boost::SharedPtrRaw<T>* const source,
    boost::SharedPtrRaw<T>* const destination
  ) noexcept
  {
    destination->assign_retain(*source);
    return destination;
  }

  template <class T>
  [[nodiscard]] boost::SharedPtrRaw<T>* ResetSharedPtrRaw(boost::SharedPtrRaw<T>* const value) noexcept
  {
    value->release();
    return value;
  }

  template <class T>
  [[nodiscard]] boost::SharedPtrRaw<T>* CopySharedPtrRawRetained(
    boost::SharedPtrRaw<T>* const destination,
    const boost::SharedPtrRaw<T>* const source
  ) noexcept
  {
    destination->px = source->px;
    destination->pi = source->pi;
    if (destination->pi != nullptr) {
      destination->pi->add_ref_copy();
    }
    return destination;
  }

  template <class T>
  [[nodiscard]] boost::SharedPtrRaw<T>* ReleaseSharedControlOnly(boost::SharedPtrRaw<T>* const value) noexcept
  {
    if (value->pi != nullptr) {
      value->pi->release();
    }
    return value;
  }

  /**
   * Address: 0x0054B200 (FUN_0054B200)
   *
   * What it does:
   * Rebinds one retained raw `shared_ptr<CAniSkel>` lane and releases the
   * previously bound owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::CAniSkel>* AssignSharedCAniSkelRetained(
    const boost::SharedPtrRaw<moho::CAniSkel>* const source,
    boost::SharedPtrRaw<moho::CAniSkel>* const destination
  ) noexcept
  {
    return AssignSharedPtrRawRetained(source, destination);
  }

  /**
   * Address: 0x00550130 (FUN_00550130)
   *
   * What it does:
   * Clears one `shared_ptr<CAniSkel>` lane and releases one retained owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::CAniSkel>* ResetSharedCAniSkel(
    boost::SharedPtrRaw<moho::CAniSkel>* const value
  ) noexcept
  {
    return ResetSharedPtrRaw(value);
  }

  /**
   * Address: 0x005503F0 (FUN_005503F0)
   *
   * What it does:
   * Copies one raw `shared_ptr<CAniSkel>` pair and retains the source owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::CAniSkel>* CopySharedCAniSkelRetained(
    boost::SharedPtrRaw<moho::CAniSkel>* const destination,
    const boost::SharedPtrRaw<moho::CAniSkel>* const source
  ) noexcept
  {
    return CopySharedPtrRawRetained(destination, source);
  }

  /**
   * Address: 0x00551ED0 (FUN_00551ED0)
   *
   * What it does:
   * Rebinds one retained raw `shared_ptr<CIntelGrid>` lane and releases the
   * previously bound owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::CIntelGrid>* AssignSharedCIntelGridRetained(
    const boost::SharedPtrRaw<moho::CIntelGrid>* const source,
    boost::SharedPtrRaw<moho::CIntelGrid>* const destination
  ) noexcept
  {
    return AssignSharedPtrRawRetained(source, destination);
  }

  /**
   * Address: 0x00551F00 (FUN_00551F00)
   *
   * What it does:
   * Clears one `shared_ptr<CIntelGrid>` lane and releases one retained owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::CIntelGrid>* ResetSharedCIntelGrid(
    boost::SharedPtrRaw<moho::CIntelGrid>* const value
  ) noexcept
  {
    return ResetSharedPtrRaw(value);
  }

  /**
   * Address: 0x00551FE0 (FUN_00551FE0)
   *
   * What it does:
   * Copies one raw `shared_ptr<CIntelGrid>` pair and retains the source owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::CIntelGrid>* CopySharedCIntelGridRetained(
    boost::SharedPtrRaw<moho::CIntelGrid>* const destination,
    const boost::SharedPtrRaw<moho::CIntelGrid>* const source
  ) noexcept
  {
    return CopySharedPtrRawRetained(destination, source);
  }

  /**
   * Address: 0x0055B760 (FUN_0055B760)
   *
   * What it does:
   * Releases one retained control-block owner without changing raw pointer
   * lanes in the `shared_ptr<Stats<StatItem>>` scratch pair.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::Stats<moho::StatItem>>* ReleaseSharedStatsStatItemControlOnly(
    boost::SharedPtrRaw<moho::Stats<moho::StatItem>>* const value
  ) noexcept
  {
    return ReleaseSharedControlOnly(value);
  }

  /**
   * Address: 0x0055FBA0 (FUN_0055FBA0)
   *
   * What it does:
   * Rebinds one retained raw `shared_ptr<Stats<StatItem>>` lane and releases
   * the previously bound owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::Stats<moho::StatItem>>* AssignSharedStatsStatItemRetained(
    const boost::SharedPtrRaw<moho::Stats<moho::StatItem>>* const source,
    boost::SharedPtrRaw<moho::Stats<moho::StatItem>>* const destination
  ) noexcept
  {
    return AssignSharedPtrRawRetained(source, destination);
  }

  /**
   * Address: 0x0055FC00 (FUN_0055FC00)
   *
   * What it does:
   * Clears one `shared_ptr<Stats<StatItem>>` lane and releases one retained
   * owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::Stats<moho::StatItem>>* ResetSharedStatsStatItem(
    boost::SharedPtrRaw<moho::Stats<moho::StatItem>>* const value
  ) noexcept
  {
    return ResetSharedPtrRaw(value);
  }

  /**
   * Address: 0x0055FDB0 (FUN_0055FDB0)
   *
   * What it does:
   * Copies one raw `shared_ptr<Stats<StatItem>>` pair and retains the source
   * owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::Stats<moho::StatItem>>* CopySharedStatsStatItemRetained(
    boost::SharedPtrRaw<moho::Stats<moho::StatItem>>* const destination,
    const boost::SharedPtrRaw<moho::Stats<moho::StatItem>>* const source
  ) noexcept
  {
    return CopySharedPtrRawRetained(destination, source);
  }

  /**
   * Address: 0x0055FDD0 (FUN_0055FDD0)
   *
   * What it does:
   * Copies one raw `shared_ptr<CAniPose>` pair and retains the source owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::CAniPose>* CopySharedCAniPoseRetained(
    boost::SharedPtrRaw<moho::CAniPose>* const destination,
    const boost::SharedPtrRaw<moho::CAniPose>* const source
  ) noexcept
  {
    return CopySharedPtrRawRetained(destination, source);
  }

  /**
   * Address: 0x005CE430 (FUN_005CE430)
   *
   * What it does:
   * Rebinds one retained raw `shared_ptr<CIntelGrid>` lane for the legacy
   * CIntelPosHandle serializer path and releases the previous owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::CIntelGrid>* AssignSharedCIntelGrid2Retained(
    const boost::SharedPtrRaw<moho::CIntelGrid>* const source,
    boost::SharedPtrRaw<moho::CIntelGrid>* const destination
  ) noexcept
  {
    return AssignSharedPtrRawRetained(source, destination);
  }

  /**
   * Address: 0x005CE720 (FUN_005CE720)
   *
   * What it does:
   * Copies one raw `shared_ptr<CIntelGrid>` pair for the legacy
   * CIntelPosHandle serializer path and retains the source owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::CIntelGrid>* CopySharedCIntelGrid2Retained(
    boost::SharedPtrRaw<moho::CIntelGrid>* const destination,
    const boost::SharedPtrRaw<moho::CIntelGrid>* const source
  ) noexcept
  {
    return CopySharedPtrRawRetained(destination, source);
  }

  /**
   * Address: 0x00714530 (FUN_00714530)
   *
   * What it does:
   * Rebinds one retained raw `shared_ptr<STrigger>` lane and releases the
   * previously bound owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::STrigger>* AssignSharedSTriggerRetained(
    const boost::SharedPtrRaw<moho::STrigger>* const source,
    boost::SharedPtrRaw<moho::STrigger>* const destination
  ) noexcept
  {
    return AssignSharedPtrRawRetained(source, destination);
  }

  /**
   * Address: 0x00714560 (FUN_00714560)
   *
   * What it does:
   * Clears one `shared_ptr<STrigger>` lane and releases one retained owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::STrigger>* ResetSharedSTrigger(
    boost::SharedPtrRaw<moho::STrigger>* const value
  ) noexcept
  {
    return ResetSharedPtrRaw(value);
  }

  /**
   * Address: 0x00714A50 (FUN_00714A50)
   *
   * What it does:
   * Copies one raw `shared_ptr<STrigger>` pair and retains the source owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::STrigger>* CopySharedSTriggerRetained(
    boost::SharedPtrRaw<moho::STrigger>* const destination,
    const boost::SharedPtrRaw<moho::STrigger>* const source
  ) noexcept
  {
    return CopySharedPtrRawRetained(destination, source);
  }

  /**
   * Address: 0x0073F5B0 (FUN_0073F5B0)
   *
   * What it does:
   * Clears one `shared_ptr<LaunchInfoBase>` lane and releases one retained
   * owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::LaunchInfoBase>* ResetSharedLaunchInfoBase(
    boost::SharedPtrRaw<moho::LaunchInfoBase>* const value
  ) noexcept
  {
    return ResetSharedPtrRaw(value);
  }

  /**
   * Address: 0x00758150 (FUN_00758150)
   *
   * What it does:
   * Rebinds one retained raw `shared_ptr<ISimResources>` lane and releases the
   * previously bound owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::ISimResources>* AssignSharedISimResourcesRetained(
    const boost::SharedPtrRaw<moho::ISimResources>* const source,
    boost::SharedPtrRaw<moho::ISimResources>* const destination
  ) noexcept
  {
    return AssignSharedPtrRawRetained(source, destination);
  }

  /**
   * Address: 0x00758180 (FUN_00758180)
   *
   * What it does:
   * Clears one `shared_ptr<ISimResources>` lane and releases one retained
   * owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::ISimResources>* ResetSharedISimResources(
    boost::SharedPtrRaw<moho::ISimResources>* const value
  ) noexcept
  {
    return ResetSharedPtrRaw(value);
  }

  /**
   * Address: 0x007584A0 (FUN_007584A0, func_CastISimResources)
   *
   * What it does:
   * Upcasts one reflected reference to `ISimResources` and returns the typed
   * object pointer when the source is compatible.
   */
  [[nodiscard]] moho::ISimResources* func_CastISimResources(const gpg::RRef& source)
  {
    const gpg::RRef upcast = gpg::REF_UpcastPtr(source, CachedISimResourcesType());
    return static_cast<moho::ISimResources*>(upcast.mObj);
  }

  /**
   * Address: 0x007584E0 (FUN_007584E0)
   *
   * What it does:
   * Copies one raw `shared_ptr<ISimResources>` pair and retains the source
   * owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::ISimResources>* CopySharedISimResourcesRetained(
    boost::SharedPtrRaw<moho::ISimResources>* const destination,
    const boost::SharedPtrRaw<moho::ISimResources>* const source
  ) noexcept
  {
    return CopySharedPtrRawRetained(destination, source);
  }

  /**
   * Address: 0x0087FCB0 (FUN_0087FCB0)
   *
   * What it does:
   * Rebinds one retained raw `shared_ptr<LaunchInfoBase>` lane and releases
   * the previously bound owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::LaunchInfoBase>* AssignSharedLaunchInfoBaseRetained(
    const boost::SharedPtrRaw<moho::LaunchInfoBase>* const source,
    boost::SharedPtrRaw<moho::LaunchInfoBase>* const destination
  ) noexcept
  {
    return AssignSharedPtrRawRetained(source, destination);
  }

  /**
   * Address: 0x00884670 (FUN_00884670)
   *
   * What it does:
   * Rebinds one retained raw `shared_ptr<SSessionSaveData>` lane and releases
   * the previously bound owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::SSessionSaveData>* AssignSharedSSessionSaveDataRetained(
    const boost::SharedPtrRaw<moho::SSessionSaveData>* const source,
    boost::SharedPtrRaw<moho::SSessionSaveData>* const destination
  ) noexcept
  {
    return AssignSharedPtrRawRetained(source, destination);
  }

  /**
   * Address: 0x008846E0 (FUN_008846E0)
   *
   * What it does:
   * Clears one `shared_ptr<SSessionSaveData>` lane and releases one retained
   * owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::SSessionSaveData>* ResetSharedSSessionSaveData(
    boost::SharedPtrRaw<moho::SSessionSaveData>* const value
  ) noexcept
  {
    return ResetSharedPtrRaw(value);
  }

  /**
   * Address: 0x008849F0 (FUN_008849F0)
   *
   * What it does:
   * Copies one raw `shared_ptr<SSessionSaveData>` pair and retains the source
   * owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::SSessionSaveData>* CopySharedSSessionSaveDataRetained(
    boost::SharedPtrRaw<moho::SSessionSaveData>* const destination,
    const boost::SharedPtrRaw<moho::SSessionSaveData>* const source
  ) noexcept
  {
    return CopySharedPtrRawRetained(destination, source);
  }

  /**
   * Address: 0x00885110 (FUN_00885110, func_CastLaunchInfoBase)
   *
   * What it does:
   * Upcasts one reflected reference to `LaunchInfoBase` and returns the typed
   * object pointer when the source is compatible.
   */
  [[nodiscard]] moho::LaunchInfoBase* func_CastLaunchInfoBase(const gpg::RRef& source)
  {
    const gpg::RRef upcast = gpg::REF_UpcastPtr(source, CachedLaunchInfoBaseType());
    return static_cast<moho::LaunchInfoBase*>(upcast.mObj);
  }

  /**
   * Address: 0x00885150 (FUN_00885150)
   *
   * What it does:
   * Copies one raw `shared_ptr<LaunchInfoBase>` pair and retains the source
   * owner.
   */
  [[nodiscard]] boost::SharedPtrRaw<moho::LaunchInfoBase>* CopySharedLaunchInfoBaseRetained(
    boost::SharedPtrRaw<moho::LaunchInfoBase>* const destination,
    const boost::SharedPtrRaw<moho::LaunchInfoBase>* const source
  ) noexcept
  {
    return CopySharedPtrRawRetained(destination, source);
  }

  /**
   * Address: 0x0094F5A0 (FUN_0094F5A0)
   *
   * What it does:
   * Releases one retained tracked-pointer shared control lane without changing
   * the raw object/type payload.
   */
  void ReleaseTrackedPointerSharedControl(gpg::TrackedPointerInfo* const tracked) noexcept
  {
    if (tracked != nullptr && tracked->sharedControl != nullptr) {
      tracked->sharedControl->release();
    }
  }

  /**
   * Address: 0x00550670 (FUN_00550670, ??1WeakPtr_CIntelGrid@Moho@@QAE@@Z)
   *
   * What it does:
   * Releases one retained `boost::shared_ptr<CIntelGrid>` control-block owner
   * from raw `(px,pi)` storage and clears both lanes.
   */
  void ReleaseSharedCIntelGrid(boost::SharedPtrRaw<moho::CIntelGrid>& pointer) noexcept
  {
    pointer.release();
  }

  [[nodiscard]] bool IsPointerCompatibleWithExpectedType(
    const gpg::TrackedPointerInfo& tracked, gpg::RType* const expectedType
  )
  {
    gpg::RRef source{};
    source.mObj = tracked.object;
    source.mType = tracked.type;

    const gpg::RRef upcast = gpg::REF_UpcastPtr(source, expectedType);
    return upcast.mObj != nullptr;
  }

  void PromoteTrackedPointerToShared(gpg::TrackedPointerInfo& tracked)
  {
    GPG_ASSERT(tracked.type != nullptr);
    GPG_ASSERT(tracked.type != nullptr && tracked.type->deleteFunc_ != nullptr);
    if (!tracked.type || !tracked.type->deleteFunc_) {
      ThrowSerializationError("Ownership conflict while loading archive");
    }

    auto* const control = new boost::detail::sp_counted_impl_pd<void*, ReflectedObjectDeleter>(
      tracked.object, ReflectedObjectDeleter{tracked.type->deleteFunc_}
    );

    tracked.sharedObject = tracked.object;
    tracked.sharedControl = control;
    tracked.state = gpg::TrackedPointerState::Shared;
  }

  void EnsureTrackedPointerSharedOwnership(gpg::TrackedPointerInfo& tracked)
  {
    if (tracked.state == gpg::TrackedPointerState::Unowned) {
      PromoteTrackedPointerToShared(tracked);
      return;
    }

    if (tracked.state != gpg::TrackedPointerState::Shared) {
      ThrowSerializationError("Ownership conflict while loading archive");
    }

    if (!tracked.sharedObject || !tracked.sharedControl) {
      ThrowSerializationError("Can't mix boost::shared_ptr with other shared pointers.");
    }
  }

  [[noreturn]]
  void ThrowTypeMismatch(const gpg::TrackedPointerInfo& tracked, gpg::RType* const expectedType)
  {
    const char* const expectedName = expectedType ? expectedType->GetName() : "LaunchInfoBase";
    const char* const actualName = tracked.type ? tracked.type->GetName() : "null";

    ThrowSerializationError(STR_Printf(
      "Error detected in archive: expected a pointer to an object of type \"%s\" but got an object of type \"%s\" "
      "instead",
      expectedName ? expectedName : "LaunchInfoBase",
      actualName ? actualName : "null"
    ));
  }

  template <class T>
  void AssignRetainedRawSharedPointer(
    boost::SharedPtrRaw<T>& outPointer, const gpg::TrackedPointerInfo& tracked
  )
  {
    boost::SharedPtrRaw<T> source{};
    source.px = static_cast<T*>(tracked.sharedObject);
    source.pi = tracked.sharedControl;
    outPointer.assign_retain(source);
  }
} // namespace

/**
 * Address: 0x0094F5E0 (FUN_0094F5E0, gpg::SerConstructResult::SetOwned)
 *
 * What it does:
 * Transitions one construct-result lane from `RESERVED` to `OWNED`, stores the
 * reflected object reference, and clears the shared-flag byte when bit 0 in
 * `flags` is set.
 */
void gpg::SerConstructResult::SetOwned(const RRef& ref, const unsigned int flags)
{
  auto* const view = reinterpret_cast<SerConstructResultView*>(this);
  if (view->mState != TrackedPointerState::Reserved) {
    gpg::HandleAssertFailure("mInfo.mState == RESERVED", 196, kSerializationCppPath);
  }

  view->mRef = ref;
  view->mState = TrackedPointerState::Owned;
  if ((flags & 1u) != 0u) {
    view->mSharedFlag = 0;
  }
}

/**
 * Address: 0x0094F630 (FUN_0094F630, gpg::SerConstructResult::SetUnowned)
 *
 * What it does:
 * Transitions one construct-result lane from `RESERVED` to `UNOWNED`, stores
 * the reflected object reference, and clears the shared-flag byte when bit 0
 * in `flags` is set.
 */
void gpg::SerConstructResult::SetUnowned(const RRef& ref, const unsigned int flags)
{
  auto* const view = reinterpret_cast<SerConstructResultView*>(this);
  if (view->mState != TrackedPointerState::Reserved) {
    gpg::HandleAssertFailure("mInfo.mState == RESERVED", 204, kSerializationCppPath);
  }

  view->mRef = ref;
  view->mState = TrackedPointerState::Unowned;
  if ((flags & 1u) != 0u) {
    view->mSharedFlag = 0;
  }
}

/**
 * Address: 0x0094F680 (FUN_0094F680, gpg::SerConstructResult::SetShared)
 * Mangled: ?SetShared@SerConstructResult@gpg@@QAEXABVRRef@2@I@Z_0
 *
 * What it does:
 * Transitions one construct-result lane from `RESERVED` to `SHARED`, stores
 * the reflected object reference lane directly, and clears the shared-flag
 * byte when bit 0 in `flags` is set.
 */
void gpg::SerConstructResult::SetShared(const RRef& ref, const unsigned int flags)
{
  auto* const view = reinterpret_cast<SerConstructResultView*>(this);
  if (view->mState != TrackedPointerState::Reserved) {
    gpg::HandleAssertFailure("mInfo.mState == RESERVED", 212, kSerializationCppPath);
  }

  view->mRef = ref;
  view->mState = TrackedPointerState::Shared;
  if ((flags & 1u) != 0u) {
    view->mSharedFlag = 0;
  }
}

/**
 * Address: 0x0094F6D0 (FUN_0094F6D0, gpg::SerConstructResult::SetShared)
 *
 * What it does:
 * Transitions one construct-result lane from `RESERVED` to `SHARED`, retains
 * the incoming shared control block, stores the reflected object reference,
 * and clears the shared-flag byte when bit 0 in `flags` is set.
 */
void gpg::SerConstructResult::SetShared(
  const boost::shared_ptr<void>& object,
  RType* const type,
  const unsigned int flags
)
{
  auto* const view = reinterpret_cast<SerConstructResultView*>(this);
  if (view->mState != TrackedPointerState::Reserved) {
    gpg::HandleAssertFailure("mInfo.mState == RESERVED", 220, kSerializationCppPath);
  }

  const boost::SharedPtrRaw<void> sourceShared = boost::SharedPtrRawFromSharedBorrow(object);
  view->mSharedPtr.assign_retain(sourceShared);
  view->mRef.mObj = sourceShared.px;
  view->mRef.mType = type;
  view->mState = TrackedPointerState::Shared;
  if ((flags & 1u) != 0u) {
    view->mSharedFlag = 0;
  }
}

/**
 * Address: 0x0094F750 (FUN_0094F750, gpg::SerSaveConstructArgsResult::SetOwned)
 * Mangled: ?SetOwned@SerSaveConstructArgsResult@gpg@@QAEXI@Z_0
 *
 * What it does:
 * Transitions one save-construct result lane from `RESERVED` to `OWNED`
 * and clears the byte-at-+4 lane when bit 0 in `flags` is set.
 */
void gpg::SerSaveConstructArgsResult::SetOwned(const unsigned int flags)
{
  auto* const view = reinterpret_cast<SerSaveConstructArgsResultView*>(this);
  if (view->mOwnership != TrackedPointerState::Reserved) {
    gpg::HandleAssertFailure("mOwnership == RESERVED", 402, kSerializationCppPath);
  }

  view->mOwnership = TrackedPointerState::Owned;
  if ((flags & 1u) != 0u) {
    view->mFlagByte4 = 0;
  }
}

/**
 * Address: 0x0094F790 (FUN_0094F790, gpg::SerSaveConstructArgsResult::SetUnowned)
 *
 * What it does:
 * Transitions one save-construct result lane from `RESERVED` to `UNOWNED`
 * and clears the byte-at-+4 lane when bit 0 in `flags` is set.
 */
void gpg::SerSaveConstructArgsResult::SetUnowned(const unsigned int flags)
{
  auto* const view = reinterpret_cast<SerSaveConstructArgsResultView*>(this);
  if (view->mOwnership != TrackedPointerState::Reserved) {
    gpg::HandleAssertFailure("mOwnership == RESERVED", 409, kSerializationCppPath);
  }

  view->mOwnership = TrackedPointerState::Unowned;
  if ((flags & 1u) != 0u) {
    view->mFlagByte4 = 0;
  }
}

/**
 * Address: 0x0094F7D0 (FUN_0094F7D0, gpg::SerSaveConstructArgsResult::SetShared)
 *
 * What it does:
 * Transitions one save-construct result lane from `RESERVED` to `SHARED`
 * and clears the byte-at-+4 lane when bit 0 in `flags` is set.
 */
void gpg::SerSaveConstructArgsResult::SetShared(const unsigned int flags)
{
  auto* const view = reinterpret_cast<SerSaveConstructArgsResultView*>(this);
  if (view->mOwnership != TrackedPointerState::Reserved) {
    gpg::HandleAssertFailure("mOwnership == RESERVED", 416, kSerializationCppPath);
  }

  view->mOwnership = TrackedPointerState::Shared;
  if ((flags & 1u) != 0u) {
    view->mFlagByte4 = 0;
  }
}

/**
 * Address: 0x00953320 (FUN_00953320)
 * Demangled: gpg::WriteArchive::WriteRawPointer
 *
 * What it does:
 * Writes tracked-pointer token payload and serializes newly seen pointees.
 */
void gpg::WriteRawPointer(
  WriteArchive* const archive, const RRef& objectRef, const TrackedPointerState state, const RRef& ownerRef
)
{
  if (!archive) {
    ThrowSerializationError("Error while creating archive: null WriteArchive.");
  }

  if (!objectRef.mObj) {
    archive->WriteMarker(static_cast<int>(ArchiveToken::NullPointer));
    return;
  }

  std::map<const void*, WriteArchive::TrackedPointerRecord>::iterator it = archive->mObjRefs.find(objectRef.mObj);
  WriteArchive::TrackedPointerRecord* record = nullptr;

  if (it == archive->mObjRefs.end()) {
    WriteArchive::TrackedPointerRecord fresh{};
    fresh.type = objectRef.mType;
    fresh.index = static_cast<int>(archive->mObjRefs.size());
    fresh.ownership = TrackedPointerState::Reserved;

    const std::pair<std::map<const void*, WriteArchive::TrackedPointerRecord>::iterator, bool> inserted =
      archive->mObjRefs.insert(std::make_pair(objectRef.mObj, fresh));
    record = &inserted.first->second;

    archive->WriteMarker(static_cast<int>(ArchiveToken::NewObject));
    archive->WriteRefCounts(objectRef.mType);

    if (!objectRef.mType || !objectRef.mType->serSaveFunc_) {
      ThrowSerializationError(STR_Printf(
        "Error while creating archive: encounted an object of type \"%s\", but we don't have a save function for it.",
        SafeTypeName(objectRef.mType)
      ));
    }

    objectRef.mType->serSaveFunc_(
      archive, reinterpret_cast<int>(objectRef.mObj), objectRef.mType->version_, const_cast<RRef*>(&ownerRef)
    );

    if (record->ownership == TrackedPointerState::Reserved) {
      record->ownership = TrackedPointerState::Unowned;
    }

    archive->WriteMarker(static_cast<int>(ArchiveToken::ObjectTerminator));
  } else {
    record = &it->second;
    if (record->ownership == TrackedPointerState::Reserved) {
      ThrowSerializationError(
        "Error while creating archive: recursively encountered a pointer to an object for which construction data is "
        "still being written"
      );
    }

    archive->WriteMarker(static_cast<int>(ArchiveToken::ExistingPointer));
    archive->WriteInt(record->index);
  }

  if (state == TrackedPointerState::Owned) {
    if (record->ownership != TrackedPointerState::Unowned) {
      ThrowSerializationError("Ownership conflict while writing archive.");
    }
    record->ownership = TrackedPointerState::Owned;
  } else if (state == TrackedPointerState::Shared) {
    if (record->ownership == TrackedPointerState::Owned) {
      ThrowSerializationError("Shared/owned conflict while writing archive.");
    }
    record->ownership = TrackedPointerState::Shared;
  }
}

/**
 * Address: 0x00953720 (FUN_00953720)
 * Demangled: gpg::ReadArchive::ReadRawPointer
 *
 * What it does:
 * Reads pointer token payload and resolves a tracked pointer reference.
 */
TrackedPointerInfo& gpg::ReadRawPointer(ReadArchive* const archive, const RRef& ownerRef)
{
  if (!archive) {
    ThrowSerializationError("Error detected in archive: null ReadArchive.");
  }

  const ArchiveToken token = static_cast<ArchiveToken>(archive->NextMarker());
  if (token == ArchiveToken::NullPointer) {
    archive->mNullTrackedPointer = {};
    return archive->mNullTrackedPointer;
  }

  if (token == ArchiveToken::ExistingPointer) {
    int index = -1;
    archive->ReadInt(&index);

    if (index < 0 || static_cast<size_t>(index) >= archive->mTrackedPtrs.size()) {
      ThrowSerializationError(STR_Printf(
        "Error detected in archive: found a reference to an existing pointer of index %d, but only %d pointers have "
        "been created.",
        index,
        static_cast<int>(archive->mTrackedPtrs.size())
      ));
    }

    TrackedPointerInfo& tracked = archive->mTrackedPtrs[static_cast<size_t>(index)];
    if (tracked.state == TrackedPointerState::Reserved) {
      ThrowSerializationError(
        "Error detected in archive: found a reference to an existing pointer that has not been constructed yet."
      );
    }
    return tracked;
  }

  if (token != ArchiveToken::NewObject) {
    ThrowSerializationError(
      STR_Printf("Error detected in archive: found an invalid token value of %d", static_cast<int>(token))
    );
  }

  const TypeHandle handle = archive->ReadTypeHandle();
  if (!handle.type) {
    ThrowSerializationError("Error detected in archive: null type handle.");
  }

  if (!handle.type->newRefFunc_) {
    ThrowSerializationError(STR_Printf(
      "Error detected in archive: found a pointer to an object of type \"%s\", but we don't have a constructor for it.",
      SafeTypeName(handle.type)
    ));
  }

  const RRef objectRef = handle.type->newRefFunc_();
  TrackedPointerInfo tracked{};
  tracked.object = objectRef.mObj;
  tracked.type = objectRef.mType ? objectRef.mType : handle.type;
  tracked.state = TrackedPointerState::Reserved;
  tracked.sharedObject = nullptr;
  tracked.sharedControl = nullptr;

  const size_t trackedIndex = archive->mTrackedPtrs.size();
  archive->mTrackedPtrs.push_back(tracked);

  RType* const loadedType = archive->mTrackedPtrs[trackedIndex].type;
  void* const loadedObject = archive->mTrackedPtrs[trackedIndex].object;
  if (!loadedType || !loadedType->serLoadFunc_) {
    ThrowSerializationError(STR_Printf(
      "Error detected in archive: found an object of type \"%s\", but we don't have a loader for it.",
      SafeTypeName(loadedType)
    ));
  }

  loadedType->serLoadFunc_(archive, reinterpret_cast<int>(loadedObject), handle.version, const_cast<RRef*>(&ownerRef));

  TrackedPointerInfo& trackedRef = archive->mTrackedPtrs[trackedIndex];

  if (archive->NextMarker() != static_cast<int>(ArchiveToken::ObjectTerminator)) {
    ThrowSerializationError(STR_Printf(
      "Error detected in archive: data for object of type \"%s\" did not terminate properly.",
      SafeTypeName(trackedRef.type)
    ));
  }

  if (trackedRef.state == TrackedPointerState::Reserved) {
    trackedRef.state = TrackedPointerState::Unowned;
  }

  return trackedRef;
}

/**
 * Address: 0x00884C90 (FUN_00884C90)
 *
 * What it does:
 * Reads one tracked pointer lane as `boost::shared_ptr<LaunchInfoBase>`,
 * promotes unowned entries to shared ownership, and validates pointee type.
 */
void gpg::ReadPointerShared_LaunchInfoBase(
  boost::SharedPtrRaw<moho::LaunchInfoBase>& outPointer, ReadArchive* const archive, const RRef& ownerRef
)
{
  if (!archive) {
    ThrowSerializationError("Error detected in archive: null ReadArchive.");
  }

  TrackedPointerInfo& tracked = ReadRawPointer(archive, ownerRef);
  if (!tracked.object) {
    outPointer.release();
    return;
  }

  EnsureTrackedPointerSharedOwnership(tracked);

  gpg::RRef trackedRef{};
  trackedRef.mObj = tracked.object;
  trackedRef.mType = tracked.type;

  gpg::RType* const expectedType = CachedLaunchInfoBaseType();
  if (func_CastLaunchInfoBase(trackedRef) == nullptr) {
    ThrowTypeMismatch(tracked, expectedType);
  }

  AssignRetainedRawSharedPointer(outPointer, tracked);
}

/**
 * Address: 0x008843F0 (FUN_008843F0)
 *
 * What it does:
 * Reads one tracked pointer lane as `boost::shared_ptr<SSessionSaveData>`,
 * promotes unowned entries to shared ownership, and validates pointee type.
 */
void gpg::ReadPointerShared_SSessionSaveData(
  boost::SharedPtrRaw<moho::SSessionSaveData>& outPointer, ReadArchive* const archive, const RRef& ownerRef
)
{
  if (!archive) {
    ThrowSerializationError("Error detected in archive: null ReadArchive.");
  }

  TrackedPointerInfo& tracked = ReadRawPointer(archive, ownerRef);
  if (!tracked.object) {
    outPointer.release();
    return;
  }

  EnsureTrackedPointerSharedOwnership(tracked);

  gpg::RRef trackedRef{};
  trackedRef.mObj = tracked.object;
  trackedRef.mType = tracked.type;
  if (func_CastSSessionSaveData(trackedRef) == nullptr) {
    gpg::RType* const expectedType = CachedSessionSaveDataType();
    ThrowTypeMismatch(tracked, expectedType);
  }

  AssignRetainedRawSharedPointer(outPointer, tracked);
}

/**
 * Address: 0x0055F990 (FUN_0055F990)
 *
 * What it does:
 * Reads one tracked pointer lane as `boost::shared_ptr<CAniPose>`,
 * promotes unowned lanes to shared ownership, and validates pointee type.
 */
void gpg::ReadPointerShared_CAniPose(
  boost::SharedPtrRaw<moho::CAniPose>& outPointer, ReadArchive* const archive, const RRef& ownerRef
)
{
  if (!archive) {
    ThrowSerializationError("Error detected in archive: null ReadArchive.");
  }

  TrackedPointerInfo& tracked = ReadRawPointer(archive, ownerRef);
  if (!tracked.object) {
    outPointer.release();
    return;
  }

  EnsureTrackedPointerSharedOwnership(tracked);

  gpg::RType* const expectedType = CachedCAniPoseType();
  if (!IsPointerCompatibleWithExpectedType(tracked, expectedType)) {
    ThrowTypeMismatch(tracked, expectedType);
  }

  AssignRetainedRawSharedPointer(outPointer, tracked);
}

/**
 * Address: 0x0054FF20 (FUN_0054FF20)
 *
 * What it does:
 * Reads one tracked pointer lane as `boost::shared_ptr<CAniSkel>`,
 * promotes unowned lanes to shared ownership, and validates pointee type.
 */
void gpg::ReadPointerShared_CAniSkel(
  boost::SharedPtrRaw<moho::CAniSkel>& outPointer, ReadArchive* const archive, const RRef& ownerRef
)
{
  if (!archive) {
    ThrowSerializationError("Error detected in archive: null ReadArchive.");
  }

  TrackedPointerInfo& tracked = ReadRawPointer(archive, ownerRef);
  if (!tracked.object) {
    outPointer.release();
    return;
  }

  EnsureTrackedPointerSharedOwnership(tracked);

  gpg::RType* const expectedType = CachedCAniSkelType();
  if (!IsPointerCompatibleWithExpectedType(tracked, expectedType)) {
    ThrowTypeMismatch(tracked, expectedType);
  }

  AssignRetainedRawSharedPointer(outPointer, tracked);
}

/**
 * Address: 0x0055F780 (FUN_0055F780)
 *
 * What it does:
 * Reads one tracked pointer lane as `boost::shared_ptr<Stats<StatItem>>`,
 * promotes unowned lanes to shared ownership, and validates pointee type.
 */
void gpg::ReadPointerShared_Stats_StatItem(
  boost::SharedPtrRaw<moho::Stats<moho::StatItem>>& outPointer, ReadArchive* const archive, const RRef& ownerRef
)
{
  if (!archive) {
    ThrowSerializationError("Error detected in archive: null ReadArchive.");
  }

  TrackedPointerInfo& tracked = ReadRawPointer(archive, ownerRef);
  if (!tracked.object) {
    outPointer.release();
    return;
  }

  EnsureTrackedPointerSharedOwnership(tracked);

  gpg::RType* const expectedType = CachedStatsStatItemType();
  if (!IsPointerCompatibleWithExpectedType(tracked, expectedType)) {
    ThrowTypeMismatch(tracked, expectedType);
  }

  AssignRetainedRawSharedPointer(outPointer, tracked);
}

/**
 * Address: 0x00757900 (FUN_00757900)
 *
 * What it does:
 * Reads one tracked pointer lane as `boost::shared_ptr<ISimResources>`,
 * promotes unowned lanes to shared ownership, and validates pointee type.
 */
void gpg::ReadPointerShared_ISimResources(
  boost::SharedPtrRaw<moho::ISimResources>& outPointer, ReadArchive* const archive, const RRef& ownerRef
)
{
  if (!archive) {
    ThrowSerializationError("Error detected in archive: null ReadArchive.");
  }

  TrackedPointerInfo& tracked = ReadRawPointer(archive, ownerRef);
  if (!tracked.object) {
    outPointer.release();
    return;
  }

  EnsureTrackedPointerSharedOwnership(tracked);

  gpg::RRef trackedRef{};
  trackedRef.mObj = tracked.object;
  trackedRef.mType = tracked.type;

  gpg::RType* const expectedType = CachedISimResourcesType();
  if (func_CastISimResources(trackedRef) == nullptr) {
    ThrowTypeMismatch(tracked, expectedType);
  }

  AssignRetainedRawSharedPointer(outPointer, tracked);
}

/**
 * Address: 0x00551CC0 (FUN_00551CC0)
 *
 * What it does:
 * Reads one tracked pointer lane as `boost::shared_ptr<CIntelGrid>`,
 * promotes unowned lanes to shared ownership, and validates pointee type.
 */
void gpg::ReadPointerShared_CIntelGrid(
  boost::SharedPtrRaw<moho::CIntelGrid>& outPointer, ReadArchive* const archive, const RRef& ownerRef
)
{
  if (!archive) {
    ThrowSerializationError("Error detected in archive: null ReadArchive.");
  }

  TrackedPointerInfo& tracked = ReadRawPointer(archive, ownerRef);
  if (!tracked.object) {
    ReleaseSharedCIntelGrid(outPointer);
    return;
  }

  EnsureTrackedPointerSharedOwnership(tracked);

  gpg::RType* const expectedType = CachedCIntelGridType();
  if (!IsPointerCompatibleWithExpectedType(tracked, expectedType)) {
    ThrowTypeMismatch(tracked, expectedType);
  }

  AssignRetainedRawSharedPointer(outPointer, tracked);
}

/**
 * Address: 0x005CE220 (FUN_005CE220, gpg::ReadArchive::ReadPointerShared_CIntelGrid2)
 *
 * What it does:
 * Reads one tracked pointer lane as `boost::shared_ptr<CIntelGrid>` for the
 * legacy CIntelPosHandle serializer lane.
 */
void gpg::ReadPointerShared_CIntelGrid2(
  boost::SharedPtrRaw<moho::CIntelGrid>& outPointer, ReadArchive* const archive, const RRef& ownerRef
)
{
  if (!archive) {
    ThrowSerializationError("Error detected in archive: null ReadArchive.");
  }

  TrackedPointerInfo& tracked = ReadRawPointer(archive, ownerRef);
  if (!tracked.object) {
    ReleaseSharedCIntelGrid(outPointer);
    return;
  }

  EnsureTrackedPointerSharedOwnership(tracked);

  gpg::RType* const expectedType = CachedCIntelGridType();
  if (!IsPointerCompatibleWithExpectedType(tracked, expectedType)) {
    ThrowTypeMismatch(tracked, expectedType);
  }

  AssignRetainedRawSharedPointer(outPointer, tracked);
}

/**
 * Address: 0x0055A5D0 (FUN_0055A5D0)
 *
 * What it does:
 * Reads one tracked pointer lane as `boost::shared_ptr<RScmResource>`,
 * promotes unowned lanes to shared ownership, and validates pointee type.
 */
void gpg::ReadPointerShared_RScmResource(
  boost::SharedPtrRaw<moho::RScmResource>& outPointer, ReadArchive* const archive, const RRef& ownerRef
)
{
  if (!archive) {
    ThrowSerializationError("Error detected in archive: null ReadArchive.");
  }

  TrackedPointerInfo& tracked = ReadRawPointer(archive, ownerRef);
  if (!tracked.object) {
    (void)boost::DestroySharedPtrRScmResource(&outPointer);
    return;
  }

  EnsureTrackedPointerSharedOwnership(tracked);

  gpg::RType* const expectedType = CachedRScmResourceType();
  if (!IsPointerCompatibleWithExpectedType(tracked, expectedType)) {
    ThrowTypeMismatch(tracked, expectedType);
  }

  AssignRetainedRawSharedPointer(outPointer, tracked);
}

/**
 * Address: 0x007142F0 (FUN_007142F0)
 *
 * What it does:
 * Reads one tracked pointer lane as `boost::shared_ptr<STrigger>`,
 * promotes unowned lanes to shared ownership, and validates pointee type.
 */
void gpg::ReadPointerShared_STrigger(
  boost::SharedPtrRaw<moho::STrigger>& outPointer, ReadArchive* const archive, const RRef& ownerRef
)
{
  if (!archive) {
    ThrowSerializationError("Error detected in archive: null ReadArchive.");
  }

  TrackedPointerInfo& tracked = ReadRawPointer(archive, ownerRef);
  if (!tracked.object) {
    outPointer.release();
    return;
  }

  EnsureTrackedPointerSharedOwnership(tracked);

  gpg::RType* const expectedType = CachedSTriggerType();
  if (!IsPointerCompatibleWithExpectedType(tracked, expectedType)) {
    ThrowTypeMismatch(tracked, expectedType);
  }

  AssignRetainedRawSharedPointer(outPointer, tracked);
}

namespace
{
  template <class TValue>
  [[nodiscard]] gpg::WriteArchive* WriteTrackedPointerFromRefBuilder(
    gpg::WriteArchive* const archive,
    gpg::RRef* (*const buildRef)(gpg::RRef*, TValue),
    TValue value,
    const gpg::TrackedPointerState trackedState
  )
  {
    gpg::RRef objectRef{};
    buildRef(&objectRef, value);
    gpg::WriteRawPointer(archive, objectRef, trackedState, gpg::RRef{});
    return archive;
  }

  template <class TValue>
  void SaveTrackedPointerFromRefBuilder(
    gpg::WriteArchive* const archive,
    gpg::RRef* (*const buildRef)(gpg::RRef*, TValue),
    TValue value,
    const gpg::TrackedPointerState trackedState
  )
  {
    (void)WriteTrackedPointerFromRefBuilder(archive, buildRef, value, trackedState);
  }

  /**
   * Address: 0x004E5920 (FUN_004E5920)
   *
   * What it does:
   * Writes one reflected `RRef_HSound` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromHSoundSlotLane1(moho::HSound** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_HSound, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x004E5B00 (FUN_004E5B00)
   *
   * What it does:
   * Writes one reflected `RRef_CSndParams` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCSndParamsSlotLane1(moho::CSndParams** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CSndParams, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x004E5B40 (FUN_004E5B40)
   *
   * What it does:
   * Writes one reflected `RRef_HSound` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromHSoundSlotLane1(moho::HSound** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_HSound, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x004E65F0 (FUN_004E65F0)
   *
   * What it does:
   * Writes one reflected `RRef_HSound` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromHSoundValueLane1(moho::HSound* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_HSound, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00511070 (FUN_00511070)
   *
   * What it does:
   * Writes one reflected `RRef_RRuleGameRules` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRRuleGameRulesSlotLane1(moho::RRuleGameRules** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RRuleGameRules, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00511220 (FUN_00511220)
   *
   * What it does:
   * Writes one reflected `RRef_RRuleGameRules` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromRRuleGameRulesSlotLane1(moho::RRuleGameRules** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_RRuleGameRules, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005118A0 (FUN_005118A0)
   *
   * What it does:
   * Writes one reflected `RRef_RRuleGameRules` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRRuleGameRulesValueLane1(moho::RRuleGameRules* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RRuleGameRules, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00525DD0 (FUN_00525DD0)
   *
   * What it does:
   * Writes one reflected `RRef_RUnitBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRUnitBlueprintSlotLane1(moho::RUnitBlueprint** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RUnitBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00526650 (FUN_00526650)
   *
   * What it does:
   * Writes one reflected `RRef_RUnitBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromRUnitBlueprintSlotLane1(moho::RUnitBlueprint** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_RUnitBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00527590 (FUN_00527590)
   *
   * What it does:
   * Writes one reflected `RRef_RUnitBlueprint` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRUnitBlueprintValueLane1(moho::RUnitBlueprint* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RUnitBlueprint, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00541AD0 (FUN_00541AD0)
   *
   * What it does:
   * Writes one reflected `RRef_IUnit` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIUnitSlotLane1(moho::IUnit** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IUnit, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00541BC0 (FUN_00541BC0)
   *
   * What it does:
   * Writes one reflected `RRef_IUnit` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromIUnitSlotLane1(moho::IUnit** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IUnit, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00541F10 (FUN_00541F10)
   *
   * What it does:
   * Writes one reflected `RRef_IUnit` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIUnitValueLane1(moho::IUnit* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IUnit, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0055A260 (FUN_0055A260)
   *
   * What it does:
   * Writes one reflected `RRef_RScmResource` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromRScmResourceSlotLane1(moho::RScmResource** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RScmResource, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x0055A300 (FUN_0055A300)
   *
   * What it does:
   * Writes one reflected `RRef_CSndParams` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCSndParamsSlotLane1(moho::CSndParams** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CSndParams, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0055A3C0 (FUN_0055A3C0)
   *
   * What it does:
   * Writes one reflected `RRef_RScmResource` slot as `shared` tracked-pointer state into one write archive lane.
   */
  void SaveSharedRawPointerFromRScmResourceSlotLane1(moho::RScmResource** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_RScmResource, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x0055A4A0 (FUN_0055A4A0)
   *
   * What it does:
   * Writes one reflected `RRef_CSndParams` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCSndParamsSlotLane2(moho::CSndParams** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CSndParams, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0055A7B0 (FUN_0055A7B0)
   *
   * What it does:
   * Writes one reflected `RRef_RScmResource` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromRScmResourceSlotLane2(moho::RScmResource** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RScmResource, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x0055AA30 (FUN_0055AA30)
   *
   * What it does:
   * Writes one reflected `RRef_CSndParams` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCSndParamsValueLane1(moho::CSndParams* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CSndParams, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0055EF50 (FUN_0055EF50)
   *
   * What it does:
   * Writes one reflected `RRef_CAniPose` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromCAniPoseSlotLane1(moho::CAniPose** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniPose, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x0055F4F0 (FUN_0055F4F0)
   *
   * What it does:
   * Writes one reflected `RRef_CAniPose` slot as `shared` tracked-pointer state into one write archive lane.
   */
  void SaveSharedRawPointerFromCAniPoseSlotLane1(moho::CAniPose** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniPose, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x0055FB70 (FUN_0055FB70)
   *
   * What it does:
   * Writes one reflected `RRef_CAniPose` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromCAniPoseSlotLane2(moho::CAniPose** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniPose, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x00584720 (FUN_00584720)
   *
   * What it does:
   * Writes one reflected `RRef_SimArmy` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromSimArmySlotLane1(moho::SimArmy** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_SimArmy, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00584750 (FUN_00584750)
   *
   * What it does:
   * Writes one reflected `RRef_CAiPersonality` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCAiPersonalitySlotLane1(moho::CAiPersonality** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiPersonality, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005849F0 (FUN_005849F0)
   *
   * What it does:
   * Writes one reflected `RRef_SimArmy` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSimArmySlotLane1(moho::SimArmy** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_SimArmy, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00584A50 (FUN_00584A50)
   *
   * What it does:
   * Writes one reflected `RRef_CAiPersonality` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCAiPersonalitySlotLane1(moho::CAiPersonality** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiPersonality, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00584E40 (FUN_00584E40)
   *
   * What it does:
   * Writes one reflected `RRef_SimArmy` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromSimArmyValueLane1(moho::SimArmy* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_SimArmy, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00584F80 (FUN_00584F80)
   *
   * What it does:
   * Writes one reflected `RRef_CAiPersonality` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCAiPersonalityValueLane1(moho::CAiPersonality* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiPersonality, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00599E00 (FUN_00599E00)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitCommandQueue` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCUnitCommandQueueSlotLane1(moho::CUnitCommandQueue** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitCommandQueue, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00599EA0 (FUN_00599EA0)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitCommandQueue` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCUnitCommandQueueSlotLane1(moho::CUnitCommandQueue** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitCommandQueue, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00599FE0 (FUN_00599FE0)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitCommandQueue` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCUnitCommandQueueValueLane1(moho::CUnitCommandQueue* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitCommandQueue, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005A2710 (FUN_005A2710)
   *
   * What it does:
   * Writes one reflected `RRef_Unit` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromUnitSlotLane1(moho::Unit** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Unit, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005A27D0 (FUN_005A27D0)
   *
   * What it does:
   * Writes one reflected `RRef_Unit` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromUnitSlotLane1(moho::Unit** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Unit, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005A2A10 (FUN_005A2A10)
   *
   * What it does:
   * Writes one reflected `RRef_Unit` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromUnitValueLane1(moho::Unit* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Unit, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005A9550 (FUN_005A9550)
   *
   * What it does:
   * Writes one reflected `RRef_CAiPathNavigator` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCAiPathNavigatorSlotLane1(moho::CAiPathNavigator** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiPathNavigator, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005A9750 (FUN_005A9750)
   *
   * What it does:
   * Writes one reflected `RRef_CAiPathNavigator` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCAiPathNavigatorSlotLane1(moho::CAiPathNavigator** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiPathNavigator, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005A99B0 (FUN_005A99B0)
   *
   * What it does:
   * Writes one reflected `RRef_CAiPathNavigator` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCAiPathNavigatorValueLane1(moho::CAiPathNavigator* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiPathNavigator, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005AC5A0 (FUN_005AC5A0)
   *
   * What it does:
   * Writes one reflected `RRef_PathQueue` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromPathQueueSlotLane1(moho::PathQueue** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_PathQueue, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005AC5D0 (FUN_005AC5D0)
   *
   * What it does:
   * Writes one reflected `RRef_COGrid` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCOGridSlotLane1(moho::COGrid** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_COGrid, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005AC770 (FUN_005AC770)
   *
   * What it does:
   * Writes one reflected `RRef_PathQueue` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromPathQueueSlotLane1(moho::PathQueue** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_PathQueue, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005AC7B0 (FUN_005AC7B0)
   *
   * What it does:
   * Writes one reflected `RRef_COGrid` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCOGridSlotLane1(moho::COGrid** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_COGrid, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005ACAB0 (FUN_005ACAB0)
   *
   * What it does:
   * Writes one reflected `RRef_PathQueue` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromPathQueueValueLane1(moho::PathQueue* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_PathQueue, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005ACBF0 (FUN_005ACBF0)
   *
   * What it does:
   * Writes one reflected `RRef_COGrid` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCOGridValueLane1(moho::COGrid* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_COGrid, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005B1820 (FUN_005B1820)
   *
   * What it does:
   * Writes one reflected `RRef_CAiPathFinder` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCAiPathFinderSlotLane1(moho::CAiPathFinder** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiPathFinder, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005B1930 (FUN_005B1930)
   *
   * What it does:
   * Writes one reflected `RRef_CAiPathFinder` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCAiPathFinderSlotLane1(moho::CAiPathFinder** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiPathFinder, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005B1B60 (FUN_005B1B60)
   *
   * What it does:
   * Writes one reflected `RRef_CAiPathFinder` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCAiPathFinderValueLane1(moho::CAiPathFinder* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiPathFinder, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005C9C40 (FUN_005C9C40)
   *
   * What it does:
   * Writes one reflected `RRef_ReconBlip` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromReconBlipSlotLane1(moho::ReconBlip** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_ReconBlip, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005CA720 (FUN_005CA720)
   *
   * What it does:
   * Writes one reflected `RRef_ReconBlip` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromReconBlipSlotLane1(moho::ReconBlip** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_ReconBlip, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005CC480 (FUN_005CC480)
   *
   * What it does:
   * Writes one reflected `RRef_ReconBlip` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromReconBlipValueLane1(moho::ReconBlip* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_ReconBlip, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005CD950 (FUN_005CD950)
   *
   * What it does:
   * Writes one reflected `RRef_CInfluenceMap` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCInfluenceMapSlotLane1(moho::CInfluenceMap** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CInfluenceMap, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005CDE00 (FUN_005CDE00)
   *
   * What it does:
   * Writes one reflected `RRef_CInfluenceMap` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCInfluenceMapSlotLane1(moho::CInfluenceMap** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CInfluenceMap, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005CE1F0 (FUN_005CE1F0)
   *
   * What it does:
   * Writes one reflected `RRef_CInfluenceMap` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCInfluenceMapValueLane1(moho::CInfluenceMap* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CInfluenceMap, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005D0CD0 (FUN_005D0CD0)
   *
   * What it does:
   * Writes one reflected `RRef_UnitWeapon` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromUnitWeaponSlotLane1(moho::UnitWeapon** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_UnitWeapon, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005D1050 (FUN_005D1050)
   *
   * What it does:
   * Writes one reflected `RRef_UnitWeapon` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromUnitWeaponSlotLane1(moho::UnitWeapon** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_UnitWeapon, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005D14F0 (FUN_005D14F0)
   *
   * What it does:
   * Writes one reflected `RRef_UnitWeapon` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromUnitWeaponValueLane1(moho::UnitWeapon* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_UnitWeapon, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005D4E50 (FUN_005D4E50)
   *
   * What it does:
   * Writes one reflected `RRef_CAiPathSpline` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCAiPathSplineSlotLane1(moho::CAiPathSpline** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiPathSpline, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005D4E80 (FUN_005D4E80)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitMotion` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCUnitMotionSlotLane1(moho::CUnitMotion** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitMotion, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005D4F10 (FUN_005D4F10)
   *
   * What it does:
   * Writes one reflected `RRef_CAiPathSpline` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCAiPathSplineSlotLane1(moho::CAiPathSpline** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiPathSpline, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005D4F50 (FUN_005D4F50)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitMotion` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCUnitMotionSlotLane1(moho::CUnitMotion** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitMotion, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005D50F0 (FUN_005D50F0)
   *
   * What it does:
   * Writes one reflected `RRef_CAiPathSpline` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCAiPathSplineValueLane1(moho::CAiPathSpline* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiPathSpline, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005D5230 (FUN_005D5230)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitMotion` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCUnitMotionValueLane1(moho::CUnitMotion* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitMotion, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005DF2A0 (FUN_005DF2A0)
   *
   * What it does:
   * Writes one reflected `RRef_CAcquireTargetTask` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCAcquireTargetTaskSlotLane1(moho::CAcquireTargetTask** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAcquireTargetTask, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005DFB60 (FUN_005DFB60)
   *
   * What it does:
   * Writes one reflected `RRef_CAcquireTargetTask` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCAcquireTargetTaskSlotLane1(moho::CAcquireTargetTask** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAcquireTargetTask, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005E11C0 (FUN_005E11C0)
   *
   * What it does:
   * Writes one reflected `RRef_CAcquireTargetTask` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCAcquireTargetTaskValueLane1(moho::CAcquireTargetTask* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAcquireTargetTask, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005E1AC0 (FUN_005E1AC0)
   *
   * What it does:
   * Writes one reflected `RRef_CAiAttackerImpl` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCAiAttackerImplSlotLane1(moho::CAiAttackerImpl** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiAttackerImpl, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005E1F00 (FUN_005E1F00)
   *
   * What it does:
   * Writes one reflected `RRef_CAiAttackerImpl` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCAiAttackerImplSlotLane1(moho::CAiAttackerImpl** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiAttackerImpl, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005E22E0 (FUN_005E22E0)
   *
   * What it does:
   * Writes one reflected `RRef_CAiAttackerImpl` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCAiAttackerImplValueLane1(moho::CAiAttackerImpl* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiAttackerImpl, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005F5090 (FUN_005F5090)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitCommand` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCUnitCommandSlotLane1(moho::CUnitCommand** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitCommand, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005F50D0 (FUN_005F50D0)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitCommand` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCUnitCommandSlotLane1(moho::CUnitCommand** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitCommand, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005F5210 (FUN_005F5210)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitCommand` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCUnitCommandValueLane1(moho::CUnitCommand* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitCommand, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0060D6D0 (FUN_0060D6D0)
   *
   * What it does:
   * Writes one reflected `RRef_EAiResult` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromEAiResultSlotLane1(moho::EAiResult** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_EAiResult, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0060D770 (FUN_0060D770)
   *
   * What it does:
   * Writes one reflected `RRef_EAiResult` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromEAiResultSlotLane1(moho::EAiResult** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_EAiResult, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0060DA50 (FUN_0060DA50)
   *
   * What it does:
   * Writes one reflected `RRef_EAiResult` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromEAiResultValueLane1(moho::EAiResult* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_EAiResult, value, gpg::TrackedPointerState::Unowned);
  }

} // namespace
namespace
{
  /**
   * Address: 0x005332D0 (FUN_005332D0)
   *
   * What it does:
   * Writes one reflected `RRef_RRuleGameRules` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromRRuleGameRulesSlotLane2(gpg::WriteArchive* archive, moho::RRuleGameRules** valueSlot)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_RRuleGameRules, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00537220 (FUN_00537220)
   *
   * What it does:
   * Writes one reflected `RRef_RRuleGameRules` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromRRuleGameRulesSlotLane3(moho::RRuleGameRules** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_RRuleGameRules, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0055A400 (FUN_0055A400)
   *
   * What it does:
   * Writes one reflected `RRef_RMeshBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromRMeshBlueprintSlotLane1(moho::RMeshBlueprint** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_RMeshBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0055F290 (FUN_0055F290)
   *
   * What it does:
   * Writes one reflected `RRef_RUnitBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromRUnitBlueprintSlotLane2(moho::RUnitBlueprint** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_RUnitBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005DEBD0 (FUN_005DEBD0)
   *
   * What it does:
   * Writes one reflected `RRef_UnitWeapon` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromUnitWeaponValueLane1(moho::UnitWeapon* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_UnitWeapon, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005DEC00 (FUN_005DEC00)
   *
   * What it does:
   * Writes one reflected `RRef_CAcquireTargetTask` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCAcquireTargetTaskValueLane1(moho::CAcquireTargetTask* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAcquireTargetTask, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005DF200 (FUN_005DF200)
   *
   * What it does:
   * Writes one reflected `RRef_Listener_EAiAttackerEvent` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromListener_EAiAttackerEventValueLane1(moho::Listener<moho::EAiAttackerEvent>* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Listener_EAiAttackerEvent, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005EC650 (FUN_005EC650)
   *
   * What it does:
   * Writes one reflected `RRef_Listener_EAiTransportEvent` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromListener_EAiTransportEventValueLane1(moho::Listener<moho::EAiTransportEvent>* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Listener_EAiTransportEvent, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00633F20 (FUN_00633F20)
   *
   * What it does:
   * Writes one reflected `RRef_RUnitBlueprintWeapon` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromRUnitBlueprintWeaponSlotLane1(moho::RUnitBlueprintWeapon** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_RUnitBlueprintWeapon, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00633F60 (FUN_00633F60)
   *
   * What it does:
   * Writes one reflected `RRef_RProjectileBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromRProjectileBlueprintSlotLane1(moho::RProjectileBlueprint** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_RProjectileBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0063CB30 (FUN_0063CB30)
   *
   * What it does:
   * Writes one reflected `RRef_IAniManipulator` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAniManipulatorValueLane1(moho::IAniManipulator* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAniManipulator, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x0063D790 (FUN_0063D790)
   *
   * What it does:
   * Writes one reflected `RRef_CAniPose` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCAniPoseSlotLane1(moho::CAniPose** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniPose, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0063DAD0 (FUN_0063DAD0)
   *
   * What it does:
   * Writes one reflected `RRef_CAniPose` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCAniPoseSlotLane1(moho::CAniPose** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniPose, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0063E640 (FUN_0063E640)
   *
   * What it does:
   * Writes one reflected `RRef_CAniPose` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCAniPoseValueLane1(moho::CAniPose* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniPose, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0063E900 (FUN_0063E900)
   *
   * What it does:
   * Writes one reflected `RRef_CAniActor` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCAniActorSlotLane1(moho::CAniActor** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniActor, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0063EA00 (FUN_0063EA00)
   *
   * What it does:
   * Writes one reflected `RRef_CAniActor` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCAniActorSlotLane1(moho::CAniActor** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniActor, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0063ED80 (FUN_0063ED80)
   *
   * What it does:
   * Writes one reflected `RRef_CAniActor` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCAniActorValueLane1(moho::CAniActor* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniActor, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x006605A0 (FUN_006605A0)
   *
   * What it does:
   * Writes one reflected `RRef_REmitterBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromREmitterBlueprintSlotLane1(moho::REmitterBlueprint** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_REmitterBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x006606E0 (FUN_006606E0)
   *
   * What it does:
   * Writes one reflected `RRef_REmitterBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromREmitterBlueprintSlotLane1(moho::REmitterBlueprint** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_REmitterBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x006608C0 (FUN_006608C0)
   *
   * What it does:
   * Writes one reflected `RRef_REmitterBlueprint` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromREmitterBlueprintValueLane1(moho::REmitterBlueprint* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_REmitterBlueprint, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00672950 (FUN_00672950)
   *
   * What it does:
   * Writes one reflected `RRef_RTrailBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRTrailBlueprintSlotLane1(moho::RTrailBlueprint** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RTrailBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00672990 (FUN_00672990)
   *
   * What it does:
   * Writes one reflected `RRef_RTrailBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromRTrailBlueprintSlotLane1(moho::RTrailBlueprint** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_RTrailBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00672AD0 (FUN_00672AD0)
   *
   * What it does:
   * Writes one reflected `RRef_RTrailBlueprint` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRTrailBlueprintValueLane1(moho::RTrailBlueprint* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RTrailBlueprint, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0067F890 (FUN_0067F890)
   *
   * What it does:
   * Writes one reflected `RRef_Entity` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromEntitySlotLane1(moho::Entity** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Entity, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x006810C0 (FUN_006810C0)
   *
   * What it does:
   * Writes one reflected `RRef_Entity` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromEntityValueLane1(moho::Entity* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Entity, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00682110 (FUN_00682110)
   *
   * What it does:
   * Writes one reflected `RRef_PositionHistory` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromPositionHistorySlotLane1(moho::PositionHistory** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_PositionHistory, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00682140 (FUN_00682140)
   *
   * What it does:
   * Writes one reflected `RRef_CColPrimitiveBase` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCColPrimitiveBaseSlotLane1(moho::CColPrimitiveBase** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CColPrimitiveBase, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006821F0 (FUN_006821F0)
   *
   * What it does:
   * Writes one reflected `RRef_CIntel` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCIntelSlotLane1(moho::CIntel** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CIntel, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00682250 (FUN_00682250)
   *
   * What it does:
   * Writes one reflected `RRef_SPhysBody` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromSPhysBodySlotLane1(moho::SPhysBody** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_SPhysBody, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00682280 (FUN_00682280)
   *
   * What it does:
   * Writes one reflected `RRef_Motor` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromMotorSlotLane1(moho::EntityMotor** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Motor, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00682520 (FUN_00682520)
   *
   * What it does:
   * Writes one reflected `RRef_PositionHistory` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromPositionHistorySlotLane1(moho::PositionHistory** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_PositionHistory, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00682570 (FUN_00682570)
   *
   * What it does:
   * Writes one reflected `RRef_CColPrimitiveBase` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCColPrimitiveBaseSlotLane1(moho::CColPrimitiveBase** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CColPrimitiveBase, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006826A0 (FUN_006826A0)
   *
   * What it does:
   * Writes one reflected `RRef_CIntel` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCIntelSlotLane1(moho::CIntel** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CIntel, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00682750 (FUN_00682750)
   *
   * What it does:
   * Writes one reflected `RRef_SPhysBody` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromSPhysBodySlotLane1(moho::SPhysBody** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_SPhysBody, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006827B0 (FUN_006827B0)
   *
   * What it does:
   * Writes one reflected `RRef_Motor` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromMotorSlotLane1(moho::EntityMotor** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Motor, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00682B00 (FUN_00682B00)
   *
   * What it does:
   * Writes one reflected `RRef_PositionHistory` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromPositionHistoryValueLane1(moho::PositionHistory* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_PositionHistory, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00682C40 (FUN_00682C40)
   *
   * What it does:
   * Writes one reflected `RRef_CColPrimitiveBase` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCColPrimitiveBaseValueLane1(moho::CColPrimitiveBase* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CColPrimitiveBase, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00682D80 (FUN_00682D80)
   *
   * What it does:
   * Writes one reflected `RRef_CIntel` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCIntelValueLane1(moho::CIntel* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CIntel, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00683000 (FUN_00683000)
   *
   * What it does:
   * Writes one reflected `RRef_SPhysBody` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromSPhysBodyValueLane1(moho::SPhysBody* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_SPhysBody, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00683140 (FUN_00683140)
   *
   * What it does:
   * Writes one reflected `RRef_Motor` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromMotorValueLane1(moho::EntityMotor* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Motor, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00688BE0 (FUN_00688BE0)
   *
   * What it does:
   * Writes one reflected `RRef_EntitySetBase` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromEntitySetBaseSlotLane1(moho::EntitySetBase** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_EntitySetBase, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00689160 (FUN_00689160)
   *
   * What it does:
   * Writes one reflected `RRef_EntitySetBase` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromEntitySetBaseSlotLane1(moho::EntitySetBase** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_EntitySetBase, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x006896B0 (FUN_006896B0)
   *
   * What it does:
   * Writes one reflected `RRef_EntitySetBase` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromEntitySetBaseValueLane1(moho::EntitySetBase* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_EntitySetBase, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x006988B0 (FUN_006988B0)
   *
   * What it does:
   * Writes one reflected `RRef_SPhysConstants` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSPhysConstantsSlotLane1(moho::SPhysConstants** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_SPhysConstants, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x006B40D0 (FUN_006B40D0)
   *
   * What it does:
   * Writes one reflected `RRef_IAiSteering` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiSteeringSlotLane1(moho::IAiSteering** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiSteering, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4100 (FUN_006B4100)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitMotion` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCUnitMotionSlotLane1(moho::CUnitMotion** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitMotion, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4130 (FUN_006B4130)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitCommandQueue` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCUnitCommandQueueSlotLane1(moho::CUnitCommandQueue** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitCommandQueue, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B41C0 (FUN_006B41C0)
   *
   * What it does:
   * Writes one reflected `RRef_CAniActor` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCAniActorSlotLane1(moho::CAniActor** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniActor, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B41F0 (FUN_006B41F0)
   *
   * What it does:
   * Writes one reflected `RRef_IAiAttacker` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiAttackerSlotLane1(moho::IAiAttacker** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiAttacker, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4220 (FUN_006B4220)
   *
   * What it does:
   * Writes one reflected `RRef_IAiCommandDispatch` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiCommandDispatchSlotLane1(moho::IAiCommandDispatch** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiCommandDispatch, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4250 (FUN_006B4250)
   *
   * What it does:
   * Writes one reflected `RRef_IAiNavigator` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiNavigatorSlotLane1(moho::IAiNavigator** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiNavigator, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4280 (FUN_006B4280)
   *
   * What it does:
   * Writes one reflected `RRef_IAiBuilder` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiBuilderSlotLane1(moho::IAiBuilder** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiBuilder, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B42B0 (FUN_006B42B0)
   *
   * What it does:
   * Writes one reflected `RRef_IAiSiloBuild` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiSiloBuildSlotLane1(moho::IAiSiloBuild** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiSiloBuild, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B42E0 (FUN_006B42E0)
   *
   * What it does:
   * Writes one reflected `RRef_IAiTransport` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiTransportSlotLane1(moho::IAiTransport** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiTransport, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4450 (FUN_006B4450)
   *
   * What it does:
   * Writes one reflected `RRef_IAiSteering` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromIAiSteeringSlotLane1(moho::IAiSteering** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiSteering, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B44B0 (FUN_006B44B0)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitMotion` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCUnitMotionSlotLane1(moho::CUnitMotion** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitMotion, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4510 (FUN_006B4510)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitCommandQueue` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCUnitCommandQueueSlotLane1(moho::CUnitCommandQueue** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitCommandQueue, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4630 (FUN_006B4630)
   *
   * What it does:
   * Writes one reflected `RRef_CAniActor` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCAniActorSlotLane1(moho::CAniActor** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniActor, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4680 (FUN_006B4680)
   *
   * What it does:
   * Writes one reflected `RRef_IAiAttacker` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromIAiAttackerSlotLane1(moho::IAiAttacker** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiAttacker, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B46D0 (FUN_006B46D0)
   *
   * What it does:
   * Writes one reflected `RRef_IAiCommandDispatch` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromIAiCommandDispatchSlotLane1(moho::IAiCommandDispatch** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiCommandDispatch, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4720 (FUN_006B4720)
   *
   * What it does:
   * Writes one reflected `RRef_IAiNavigator` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromIAiNavigatorSlotLane1(moho::IAiNavigator** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiNavigator, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4770 (FUN_006B4770)
   *
   * What it does:
   * Writes one reflected `RRef_IAiBuilder` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromIAiBuilderSlotLane1(moho::IAiBuilder** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiBuilder, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B47C0 (FUN_006B47C0)
   *
   * What it does:
   * Writes one reflected `RRef_IAiSiloBuild` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromIAiSiloBuildSlotLane1(moho::IAiSiloBuild** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiSiloBuild, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4810 (FUN_006B4810)
   *
   * What it does:
   * Writes one reflected `RRef_IAiTransport` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromIAiTransportSlotLane1(moho::IAiTransport** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiTransport, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4B80 (FUN_006B4B80)
   *
   * What it does:
   * Writes one reflected `RRef_IAiSteering` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiSteeringValueLane1(moho::IAiSteering* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiSteering, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4CC0 (FUN_006B4CC0)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitMotion` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCUnitMotionValueLane1(moho::CUnitMotion* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitMotion, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4E00 (FUN_006B4E00)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitCommandQueue` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCUnitCommandQueueValueLane1(moho::CUnitCommandQueue* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitCommandQueue, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B51C0 (FUN_006B51C0)
   *
   * What it does:
   * Writes one reflected `RRef_CAniActor` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCAniActorValueLane1(moho::CAniActor* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniActor, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B5300 (FUN_006B5300)
   *
   * What it does:
   * Writes one reflected `RRef_IAiAttacker` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiAttackerValueLane1(moho::IAiAttacker* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiAttacker, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B5440 (FUN_006B5440)
   *
   * What it does:
   * Writes one reflected `RRef_IAiCommandDispatch` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiCommandDispatchValueLane1(moho::IAiCommandDispatch* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiCommandDispatch, value, gpg::TrackedPointerState::Owned);
  }

} // namespace
namespace
{
  /**
   * Address: 0x006B5580 (FUN_006B5580)
   *
   * What it does:
   * Writes one reflected `RRef_IAiNavigator` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiNavigatorValueLane1(moho::IAiNavigator* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiNavigator, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B56C0 (FUN_006B56C0)
   *
   * What it does:
   * Writes one reflected `RRef_IAiBuilder` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiBuilderValueLane1(moho::IAiBuilder* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiBuilder, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B5800 (FUN_006B5800)
   *
   * What it does:
   * Writes one reflected `RRef_IAiSiloBuild` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiSiloBuildValueLane1(moho::IAiSiloBuild* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiSiloBuild, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B5940 (FUN_006B5940)
   *
   * What it does:
   * Writes one reflected `RRef_IAiTransport` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiTransportValueLane1(moho::IAiTransport* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiTransport, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006BBF70 (FUN_006BBF70)
   *
   * What it does:
   * Writes one reflected `RRef_CPathPoint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCPathPointSlotLane1(moho::CPathPoint** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CPathPoint, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x006DFE60 (FUN_006DFE60)
   *
   * What it does:
   * Writes one reflected `RRef_IAiAttacker` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIAiAttackerSlotLane1(moho::IAiAttacker** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiAttacker, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x006DFED0 (FUN_006DFED0)
   *
   * What it does:
   * Writes one reflected `RRef_CFireWeaponTask` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCFireWeaponTaskSlotLane1(moho::CFireWeaponTask** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CFireWeaponTask, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006E0210 (FUN_006E0210)
   *
   * What it does:
   * Writes one reflected `RRef_IAiAttacker` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromIAiAttackerSlotLane1(moho::IAiAttacker** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiAttacker, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x006E02C0 (FUN_006E02C0)
   *
   * What it does:
   * Writes one reflected `RRef_CFireWeaponTask` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCFireWeaponTaskSlotLane1(moho::CFireWeaponTask** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CFireWeaponTask, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006E0610 (FUN_006E0610)
   *
   * What it does:
   * Writes one reflected `RRef_IAiAttacker` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIAiAttackerValueLane1(moho::IAiAttacker* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiAttacker, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x006E0750 (FUN_006E0750)
   *
   * What it does:
   * Writes one reflected `RRef_CFireWeaponTask` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCFireWeaponTaskValueLane1(moho::CFireWeaponTask* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CFireWeaponTask, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006E2B00 (FUN_006E2B00)
   *
   * What it does:
   * Writes one reflected `RRef_CUnitCommand` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCUnitCommandValueLane1(moho::CUnitCommand* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitCommand, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006EB980 (FUN_006EB980)
   *
   * What it does:
   * Writes one reflected `RRef_Listener_ECommandEvent` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromListener_ECommandEventValueLane1(moho::Listener<moho::ECommandEvent>* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Listener_ECommandEvent, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00706650 (FUN_00706650)
   *
   * What it does:
   * Writes one reflected `RRef_CAiBrain` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCAiBrainSlotLane1(moho::CAiBrain** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiBrain, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00706680 (FUN_00706680)
   *
   * What it does:
   * Writes one reflected `RRef_IAiReconDB` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiReconDBSlotLane1(moho::IAiReconDB** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiReconDB, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x007066E0 (FUN_007066E0)
   *
   * What it does:
   * Writes one reflected `RRef_CArmyStats` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCArmyStatsSlotLane1(moho::CArmyStats** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CArmyStats, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00706710 (FUN_00706710)
   *
   * What it does:
   * Writes one reflected `RRef_CInfluenceMap` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCInfluenceMapSlotLane1(moho::CInfluenceMap** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CInfluenceMap, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00706740 (FUN_00706740)
   *
   * What it does:
   * Writes one reflected `RRef_PathQueue` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromPathQueueSlotLane1(moho::PathQueue** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_PathQueue, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00706A80 (FUN_00706A80)
   *
   * What it does:
   * Writes one reflected `RRef_CAiBrain` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCAiBrainSlotLane1(moho::CAiBrain** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiBrain, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00706AD0 (FUN_00706AD0)
   *
   * What it does:
   * Writes one reflected `RRef_IAiReconDB` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromIAiReconDBSlotLane1(moho::IAiReconDB** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiReconDB, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00706B90 (FUN_00706B90)
   *
   * What it does:
   * Writes one reflected `RRef_CArmyStats` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCArmyStatsSlotLane1(moho::CArmyStats** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CArmyStats, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00706BF0 (FUN_00706BF0)
   *
   * What it does:
   * Writes one reflected `RRef_CInfluenceMap` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCInfluenceMapSlotLane1(moho::CInfluenceMap** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CInfluenceMap, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00706F30 (FUN_00706F30)
   *
   * What it does:
   * Writes one reflected `RRef_CAiBrain` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCAiBrainValueLane1(moho::CAiBrain* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiBrain, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00707070 (FUN_00707070)
   *
   * What it does:
   * Writes one reflected `RRef_IAiReconDB` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiReconDBValueLane1(moho::IAiReconDB* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiReconDB, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x007072F0 (FUN_007072F0)
   *
   * What it does:
   * Writes one reflected `RRef_CArmyStats` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCArmyStatsValueLane1(moho::CArmyStats* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CArmyStats, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00707430 (FUN_00707430)
   *
   * What it does:
   * Writes one reflected `RRef_CInfluenceMap` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCInfluenceMapValueLane1(moho::CInfluenceMap* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CInfluenceMap, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00707570 (FUN_00707570)
   *
   * What it does:
   * Writes one reflected `RRef_PathQueue` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromPathQueueValueLane1(moho::PathQueue* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_PathQueue, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00712650 (FUN_00712650)
   *
   * What it does:
   * Writes one reflected `RRef_CAiBrain` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCAiBrainSlotLane1(moho::CAiBrain** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiBrain, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00712980 (FUN_00712980)
   *
   * What it does:
   * Writes one reflected `RRef_STrigger` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromSTriggerSlotLane1(moho::STrigger** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_STrigger, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x00712FD0 (FUN_00712FD0)
   *
   * What it does:
   * Writes one reflected `RRef_CAiBrain` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCAiBrainSlotLane1(moho::CAiBrain** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiBrain, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00713100 (FUN_00713100)
   *
   * What it does:
   * Writes one reflected `RRef_STrigger` slot as `shared` tracked-pointer state into one write archive lane.
   */
  void SaveSharedRawPointerFromSTriggerSlotLane1(moho::STrigger** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_STrigger, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x00714040 (FUN_00714040)
   *
   * What it does:
   * Writes one reflected `RRef_CAiBrain` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCAiBrainValueLane1(moho::CAiBrain* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiBrain, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x007144D0 (FUN_007144D0)
   *
   * What it does:
   * Writes one reflected `RRef_STrigger` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromSTriggerSlotLane2(moho::STrigger** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_STrigger, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x00750FA0 (FUN_00750FA0)
   *
   * What it does:
   * Writes one reflected `RRef_SimArmy` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromSimArmyValueLane1(moho::SimArmy* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_SimArmy, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00751870 (FUN_00751870)
   *
   * What it does:
   * Writes one reflected `RRef_Shield` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromShieldSlotLane1(moho::Shield** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Shield, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x007523F0 (FUN_007523F0)
   *
   * What it does:
   * Writes one reflected `RRef_Shield` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromShieldSlotLane1(moho::Shield** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Shield, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x007546B0 (FUN_007546B0)
   *
   * What it does:
   * Writes one reflected `RRef_Shield` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromShieldValueLane1(moho::Shield* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Shield, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00756460 (FUN_00756460)
   *
   * What it does:
   * Writes one reflected `RRef_CRandomStream` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCRandomStreamSlotLane1(moho::CRandomStream** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CRandomStream, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00756490 (FUN_00756490)
   *
   * What it does:
   * Writes one reflected `RRef_SPhysConstants` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromSPhysConstantsSlotLane1(moho::SPhysConstants** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_SPhysConstants, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00756500 (FUN_00756500)
   *
   * What it does:
   * Writes one reflected `RRef_IAiFormationDB` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiFormationDBSlotLane1(moho::IAiFormationDB** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiFormationDB, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00756530 (FUN_00756530)
   *
   * What it does:
   * Writes one reflected `RRef_ISimResources` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromISimResourcesSlotLane1(moho::ISimResources** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_ISimResources, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x00756560 (FUN_00756560)
   *
   * What it does:
   * Writes one reflected `RRef_CCommandDB` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCCommandDBSlotLane1(moho::CCommandDb** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CCommandDB, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00756590 (FUN_00756590)
   *
   * What it does:
   * Writes one reflected `RRef_CDecalBuffer` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCDecalBufferSlotLane1(moho::CDecalBuffer** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CDecalBuffer, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x007565C0 (FUN_007565C0)
   *
   * What it does:
   * Writes one reflected `RRef_IEffectManager` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIEffectManagerSlotLane1(moho::IEffectManager** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IEffectManager, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x007565F0 (FUN_007565F0)
   *
   * What it does:
   * Writes one reflected `RRef_ISoundManager` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromISoundManagerSlotLane1(moho::ISoundManager** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_ISoundManager, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00756660 (FUN_00756660)
   *
   * What it does:
   * Writes one reflected `RRef_EntityDB` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromEntityDBSlotLane1(moho::CEntityDb** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_EntityDB, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00756E20 (FUN_00756E20)
   *
   * What it does:
   * Writes one reflected `RRef_CRandomStream` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCRandomStreamSlotLane1(moho::CRandomStream** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CRandomStream, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00756E70 (FUN_00756E70)
   *
   * What it does:
   * Writes one reflected `RRef_SPhysConstants` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromSPhysConstantsSlotLane1(moho::SPhysConstants** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_SPhysConstants, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00756F20 (FUN_00756F20)
   *
   * What it does:
   * Writes one reflected `RRef_IAiFormationDB` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromIAiFormationDBSlotLane1(moho::IAiFormationDB** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiFormationDB, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00756F60 (FUN_00756F60)
   *
   * What it does:
   * Writes one reflected `RRef_ISimResources` slot as `shared` tracked-pointer state into one write archive lane.
   */
  void SaveSharedRawPointerFromISimResourcesSlotLane1(moho::ISimResources** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_ISimResources, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x00756FC0 (FUN_00756FC0)
   *
   * What it does:
   * Writes one reflected `RRef_CCommandDB` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCCommandDBSlotLane1(moho::CCommandDb** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CCommandDB, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00757020 (FUN_00757020)
   *
   * What it does:
   * Writes one reflected `RRef_CDecalBuffer` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCDecalBufferSlotLane1(moho::CDecalBuffer** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CDecalBuffer, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00757070 (FUN_00757070)
   *
   * What it does:
   * Writes one reflected `RRef_IEffectManager` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromIEffectManagerSlotLane1(moho::IEffectManager** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IEffectManager, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x007570D0 (FUN_007570D0)
   *
   * What it does:
   * Writes one reflected `RRef_ISoundManager` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromISoundManagerSlotLane1(moho::ISoundManager** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_ISoundManager, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00757190 (FUN_00757190)
   *
   * What it does:
   * Writes one reflected `RRef_EntityDB` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromEntityDBSlotLane1(moho::CEntityDb** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_EntityDB, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00757650 (FUN_00757650)
   *
   * What it does:
   * Writes one reflected `RRef_CRandomStream` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCRandomStreamValueLane1(moho::CRandomStream* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CRandomStream, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00757790 (FUN_00757790)
   *
   * What it does:
   * Writes one reflected `RRef_SPhysConstants` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromSPhysConstantsValueLane1(moho::SPhysConstants* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_SPhysConstants, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x007578D0 (FUN_007578D0)
   *
   * What it does:
   * Writes one reflected `RRef_IAiFormationDB` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAiFormationDBValueLane1(moho::IAiFormationDB* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiFormationDB, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00757AE0 (FUN_00757AE0)
   *
   * What it does:
   * Writes one reflected `RRef_ISimResources` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromISimResourcesSlotLane2(moho::ISimResources** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_ISimResources, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x00757C20 (FUN_00757C20)
   *
   * What it does:
   * Writes one reflected `RRef_CCommandDB` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCCommandDBValueLane1(moho::CCommandDb* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CCommandDB, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00757D60 (FUN_00757D60)
   *
   * What it does:
   * Writes one reflected `RRef_CDecalBuffer` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCDecalBufferValueLane1(moho::CDecalBuffer* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CDecalBuffer, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00757EA0 (FUN_00757EA0)
   *
   * What it does:
   * Writes one reflected `RRef_IEffectManager` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIEffectManagerValueLane1(moho::IEffectManager* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IEffectManager, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00757FE0 (FUN_00757FE0)
   *
   * What it does:
   * Writes one reflected `RRef_ISoundManager` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromISoundManagerValueLane1(moho::ISoundManager* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_ISoundManager, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00758120 (FUN_00758120)
   *
   * What it does:
   * Writes one reflected `RRef_EntityDB` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromEntityDBValueLane1(moho::CEntityDb* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_EntityDB, value, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00763D90 (FUN_00763D90)
   *
   * What it does:
   * Writes one reflected `RRef_Listener_NavPath` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromListener_NavPathValueLane1(moho::Listener<const moho::SNavPath&>* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Listener_NavPath, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00768C10 (FUN_00768C10)
   *
   * What it does:
   * Writes one reflected `RRef_IPathTraveler` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIPathTravelerSlotLane1(moho::IPathTraveler** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IPathTraveler, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00769220 (FUN_00769220)
   *
   * What it does:
   * Writes one reflected `RRef_IPathTraveler` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromIPathTravelerSlotLane1(moho::IPathTraveler** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IPathTraveler, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00769260 (FUN_00769260)
   *
   * What it does:
   * Writes one reflected `RRef_IPathTraveler` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromIPathTravelerSlotLane2(moho::IPathTraveler** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IPathTraveler, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0076A4E0 (FUN_0076A4E0)
   *
   * What it does:
   * Writes one reflected `RRef_PathTables` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromPathTablesSlotLane1(moho::PathTables** valueSlot, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_PathTables, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0076A9F0 (FUN_0076A9F0)
   *
   * What it does:
   * Writes one reflected `RRef_IPathTraveler` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIPathTravelerValueLane1(moho::IPathTraveler* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IPathTraveler, value, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0076ABD0 (FUN_0076ABD0)
   *
   * What it does:
   * Writes one reflected `RRef_PathTables` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromPathTablesSlotLane1(moho::PathTables** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_PathTables, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0076B2D0 (FUN_0076B2D0)
   *
   * What it does:
   * Writes one reflected `RRef_PathTables` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromPathTablesValueLane1(moho::PathTables* value, gpg::WriteArchive* archive)
  {
    return WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_PathTables, value, gpg::TrackedPointerState::Unowned);
  }

} // namespace
namespace
{
  /**
   * Address: 0x004E58F0 (FUN_004E58F0)
   *
   * What it does:
   * Writes one reflected `RRef_CSndParams` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCSndParamsSlotLane2(moho::CSndParams** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CSndParams, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x004E64B0 (FUN_004E64B0)
   *
   * What it does:
   * Writes one reflected `RRef_CSndParams` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCSndParamsValueLane2(moho::CSndParams* value, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CSndParams, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00536D50 (FUN_00536D50)
   *
   * What it does:
   * Writes one reflected `RRef_RRuleGameRules` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRRuleGameRulesSlotLane2(moho::RRuleGameRules** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RRuleGameRules, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x005376D0 (FUN_005376D0)
   *
   * What it does:
   * Writes one reflected `RRef_RRuleGameRules` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRRuleGameRulesValueLane2(moho::RRuleGameRules* value, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RRuleGameRules, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0055A290 (FUN_0055A290)
   *
   * What it does:
   * Writes one reflected `RRef_RMeshBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRMeshBlueprintSlotLane1(moho::RMeshBlueprint** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RMeshBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0055A8F0 (FUN_0055A8F0)
   *
   * What it does:
   * Writes one reflected `RRef_RMeshBlueprint` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRMeshBlueprintValueLane1(moho::RMeshBlueprint* value, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RMeshBlueprint, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0055EC40 (FUN_0055EC40)
   *
   * What it does:
   * Writes one reflected `RRef_RUnitBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRUnitBlueprintSlotLane2(moho::RUnitBlueprint** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RUnitBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0055F750 (FUN_0055F750)
   *
   * What it does:
   * Writes one reflected `RRef_RUnitBlueprint` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRUnitBlueprintValueLane2(moho::RUnitBlueprint* value, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RUnitBlueprint, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x005DEAC0 (FUN_005DEAC0)
   *
   * What it does:
   * Writes one reflected `RRef_CAiAttackerImpl` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCAiAttackerImplOwnerFieldLane1(gpg::WriteArchive* archive, int ownerToken)
  {

    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x1C]{};
      moho::CAiAttackerImpl* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x1C, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiAttackerImpl, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005E02C0 (FUN_005E02C0)
   *
   * What it does:
   * Writes one reflected `RRef_CAiAttackerImpl` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCAiAttackerImplOwnerFieldLane2(gpg::WriteArchive* archive, int ownerToken)
  {

    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x1C]{};
      moho::CAiAttackerImpl* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x1C, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiAttackerImpl, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005E1370 (FUN_005E1370)
   *
   * What it does:
   * Writes one reflected `RRef_CAiAttackerImpl` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCAiAttackerImplOwnerFieldLane3(int ownerToken, gpg::WriteArchive* archive)
  {

    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x1C]{};
      moho::CAiAttackerImpl* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x1C, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiAttackerImpl, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00633DF0 (FUN_00633DF0)
   *
   * What it does:
   * Writes one reflected `RRef_RUnitBlueprintWeapon` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRUnitBlueprintWeaponSlotLane1(moho::RUnitBlueprintWeapon** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RUnitBlueprintWeapon, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00633E20 (FUN_00633E20)
   *
   * What it does:
   * Writes one reflected `RRef_RProjectileBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRProjectileBlueprintSlotLane1(moho::RProjectileBlueprint** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RProjectileBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x006340C0 (FUN_006340C0)
   *
   * What it does:
   * Writes one reflected `RRef_RUnitBlueprintWeapon` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRUnitBlueprintWeaponValueLane1(moho::RUnitBlueprintWeapon* value, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RUnitBlueprintWeapon, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00634200 (FUN_00634200)
   *
   * What it does:
   * Writes one reflected `RRef_RProjectileBlueprint` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromRProjectileBlueprintValueLane1(moho::RProjectileBlueprint* value, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RProjectileBlueprint, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0063CB60 (FUN_0063CB60)
   *
   * What it does:
   * Writes one null reflected `RRef_IAniManipulator_P` lane as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIAniManipulator_PNullLane1(gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAniManipulator_P, static_cast<moho::IAniManipulator**>(nullptr), gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x0066C320 (FUN_0066C320)
   *
   * What it does:
   * Writes one null reflected `RRef_IEffect_P` lane as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIEffect_PNullLane1(gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IEffect_P, static_cast<moho::IEffect**>(nullptr), gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x00698800 (FUN_00698800)
   *
   * What it does:
   * Writes one reflected `RRef_SPhysConstants` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromSPhysConstantsSlotLane1(moho::SPhysConstants** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_SPhysConstants, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00698A10 (FUN_00698A10)
   *
   * What it does:
   * Writes one reflected `RRef_SPhysConstants` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromSPhysConstantsValueLane1(moho::SPhysConstants* value, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_SPhysConstants, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0069EF60 (FUN_0069EF60)
   *
   * What it does:
   * Writes one intrusive-list-head-adjusted
   * `gpg::RRef_ManyToOneListener_EProjectileImpactEvent` lane as `unowned`
   * tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromManyToOneListener_EProjectileImpactEventIntrusiveHeadLane1(
    gpg::WriteArchive* archive,
    std::uint32_t* intrusiveListHeadSlot
  )
  {
    moho::ManyToOneListener<moho::EProjectileImpactEvent>* listener = nullptr;
    if (intrusiveListHeadSlot != nullptr && *intrusiveListHeadSlot != 0u) {
      listener = reinterpret_cast<moho::ManyToOneListener<moho::EProjectileImpactEvent>*>(
        *intrusiveListHeadSlot - sizeof(std::uint32_t)
      );
    }

    gpg::RRef listenerRef{};
    (void)gpg::RRef_ManyToOneListener_EProjectileImpactEvent(&listenerRef, listener);
    gpg::WriteRawPointer(archive, listenerRef, gpg::TrackedPointerState::Unowned, gpg::RRef{});
  }

  /**
   * Address: 0x0069FA30 (FUN_0069FA30)
   *
   * What it does:
   * Writes one reflected `RRef_ManyToOneListener_EProjectileImpactEvent` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromManyToOneListener_EProjectileImpactEventValueLane1(moho::ManyToOneListener<moho::EProjectileImpactEvent>* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_ManyToOneListener_EProjectileImpactEvent, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x006B1090 (FUN_006B1090)
   *
   * What it does:
   * Writes one reflected `RRef_CEconomyEvent` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCEconomyEventValueLane1(moho::CEconomyEvent* value, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconomyEvent, value, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x006B10C0 (FUN_006B10C0)
   *
   * What it does:
   * Writes one null reflected `RRef_CEconomyEvent_P` lane as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCEconomyEvent_PNullLane1(gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconomyEvent_P, static_cast<moho::CEconomyEvent**>(nullptr), gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x006BBDB0 (FUN_006BBDB0)
   *
   * What it does:
   * Writes one reflected `RRef_CPathPoint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCPathPointSlotLane1(moho::CPathPoint** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CPathPoint, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x006BC2F0 (FUN_006BC2F0)
   *
   * What it does:
   * Writes one reflected `RRef_CPathPoint` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCPathPointValueLane1(moho::CPathPoint* value, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CPathPoint, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x006E2B30 (FUN_006E2B30)
   *
   * What it does:
   * Writes one null reflected `RRef_CUnitCommand_P` lane as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCUnitCommand_PNullLane1(gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CUnitCommand_P, static_cast<moho::CUnitCommand**>(nullptr), gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x006F8CB0 (FUN_006F8CB0)
   *
   * What it does:
   * Writes one reflected `RRef_Unit` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromUnitOwnerFieldLane1(int ownerToken, gpg::WriteArchive* archive)
  {

    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x8]{};
      moho::Unit* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x8, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Unit, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
    reinterpret_cast<gpg::SerSaveConstructArgsResult*>(archive)->SetUnowned(0);
  }

  /**
   * Address: 0x006F9070 (FUN_006F9070)
   *
   * What it does:
   * Writes one reflected `RRef_Listener_EUnitCommandQueueStatus` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromListener_EUnitCommandQueueStatusValueLane1(moho::Listener<moho::EUnitCommandQueueStatus>* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Listener_EUnitCommandQueueStatus, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0070E010 (FUN_0070E010)
   *
   * What it does:
   * Writes one reflected `RRef_CAiBrain` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCAiBrainOwnerFieldLane1(int ownerToken, gpg::WriteArchive* archive)
  {

    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x10]{};
      moho::CAiBrain* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x10, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiBrain, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
    reinterpret_cast<gpg::SerSaveConstructArgsResult*>(archive)->SetUnowned(0);
  }

  /**
   * Address: 0x0070E060 (FUN_0070E060)
   *
   * What it does:
   * Writes one reflected `RRef_CAiBrain` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCAiBrainOwnerFieldLane2(int ownerToken, gpg::WriteArchive* archive, int a3)
  {
    (void)a3;

    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x10]{};
      moho::CAiBrain* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x10, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiBrain, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
    reinterpret_cast<gpg::SerSaveConstructArgsResult*>(archive)->SetUnowned(0);
  }

  /**
   * Address: 0x00712600 (FUN_00712600)
   *
   * What it does:
   * Writes one reflected `RRef_CAiBrain` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCAiBrainOwnerFieldLane3(int ownerToken, gpg::WriteArchive* archive)
  {

    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x10]{};
      moho::CAiBrain* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x10, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAiBrain, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
    reinterpret_cast<gpg::SerSaveConstructArgsResult*>(archive)->SetUnowned(0);
  }

  /**
   * Address: 0x00768BE0 (FUN_00768BE0)
   *
   * What it does:
   * Writes one reflected `RRef_IPathTraveler` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIPathTravelerSlotLane2(moho::IPathTraveler** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IPathTraveler, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0076A8B0 (FUN_0076A8B0)
   *
   * What it does:
   * Writes one reflected `RRef_IPathTraveler` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIPathTravelerValueLane2(moho::IPathTraveler* value, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IPathTraveler, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0076EB90 (FUN_0076EB90)
   *
   * What it does:
   * Writes one reflected `RRef_CIntelPosHandle` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCIntelPosHandleSlotLane1(moho::CIntelPosHandle** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CIntelPosHandle, *valueSlot, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x0076EBF0 (FUN_0076EBF0)
   *
   * What it does:
   * Writes one reflected `RRef_CIntelPosHandle` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCIntelPosHandleSlotLane1(moho::CIntelPosHandle** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CIntelPosHandle, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x0076ED40 (FUN_0076ED40)
   *
   * What it does:
   * Writes one reflected `RRef_CIntelPosHandle` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCIntelPosHandleValueLane1(moho::CIntelPosHandle* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CIntelPosHandle, value, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x00770290 (FUN_00770290)
   *
   * What it does:
   * Writes one reflected `RRef_IAiReconDB` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIAiReconDBSlotLane1(moho::IAiReconDB** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiReconDB, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00770310 (FUN_00770310)
   *
   * What it does:
   * Writes one reflected `RRef_IAiReconDB` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromIAiReconDBSlotLane1(moho::IAiReconDB** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiReconDB, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x007704B0 (FUN_007704B0)
   *
   * What it does:
   * Writes one reflected `RRef_IAiReconDB` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIAiReconDBValueLane1(moho::IAiReconDB* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IAiReconDB, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00771520 (FUN_00771520)
   *
   * What it does:
   * Writes one reflected `RRef_IEffectManager` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromIEffectManagerSlotLane1(moho::IEffectManager** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IEffectManager, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00771660 (FUN_00771660)
   *
   * What it does:
   * Writes one reflected `RRef_IEffectManager` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIEffectManagerValueLane1(moho::IEffectManager* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IEffectManager, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0077D740 (FUN_0077D740)
   *
   * What it does:
   * Writes one reflected `RRef_CDecalHandle` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCDecalHandleValueLane1(moho::CDecalHandle* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CDecalHandle, value, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x0077D770 (FUN_0077D770)
   *
   * What it does:
   * Writes one null reflected `RRef_CDecalHandle_P` lane as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCDecalHandle_PNullLane1(gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CDecalHandle_P, static_cast<moho::CDecalHandle**>(nullptr), gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x00883630 (FUN_00883630)
   *
   * What it does:
   * Writes one reflected `RRef_SSessionSaveData` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromSSessionSaveDataSlotLane1(moho::SSessionSaveData** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_SSessionSaveData, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x00883A40 (FUN_00883A40)
   *
   * What it does:
   * Writes one reflected `RRef_SSessionSaveData` slot as `shared` tracked-pointer state into one write archive lane.
   */
  void SaveSharedRawPointerFromSSessionSaveDataSlotLane1(moho::SSessionSaveData** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_SSessionSaveData, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x008845D0 (FUN_008845D0)
   *
   * What it does:
   * Writes one reflected `RRef_SSessionSaveData` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromSSessionSaveDataSlotLane2(moho::SSessionSaveData** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_SSessionSaveData, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x0090B7E0 (FUN_0090B7E0)
   *
   * What it does:
   * Writes one reflected `RRef_LuaState` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromLuaStateValueLane1(gpg::WriteArchive* archive, LuaPlus::LuaState* value, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_LuaState, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0091E8D0 (FUN_0091E8D0)
   *
   * What it does:
   * Writes one reflected `RRef_Table` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromTableValueLane1(gpg::WriteArchive* archive, Table* value, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Table, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0091EDE0 (FUN_0091EDE0)
   *
   * What it does:
   * Writes one reflected `RRef_TString` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromTStringValueLane1(gpg::WriteArchive* archive, TString* value, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_TString, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00920970 (FUN_00920970)
   *
   * What it does:
   * Writes one reflected `RRef_Proto` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromProtoValueLane1(gpg::WriteArchive* archive, Proto* value, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Proto, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x009209A0 (FUN_009209A0)
   *
   * What it does:
   * Writes one reflected `RRef_UpVal` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromUpValValueLane1(gpg::WriteArchive* archive, UpVal* value, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_UpVal, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00921210 (FUN_00921210)
   *
   * What it does:
   * Writes one reflected `RRef_TString` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromTStringSlotLane1(gpg::WriteArchive* archive, TString** valueSlot)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_TString, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00921420 (FUN_00921420)
   *
   * What it does:
   * Writes one reflected `RRef_TString` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromTStringSlotLane1(gpg::WriteArchive* archive, TString** valueSlot, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_TString, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0051DBB0 (FUN_0051DBB0)
   *
   * What it does:
   * Writes one tracked pointer lane, serializes one companion name string, and updates save-construct ownership metadata.
   */
  void SaveUnownedRawPointerAndNameFromRRuleGameRulesOwnerFieldLane1(int ownerToken, gpg::WriteArchive* archive, gpg::SerSaveConstructArgsResult* constructResult)
  {
    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x4]{};
      moho::RRuleGameRules* ownerField;
      msvc8::string ownerName;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x4, "OwnerFieldView::ownerField offset must match evidence");
    static_assert(offsetof(OwnerFieldView, ownerName) == 0x8, "OwnerFieldView::ownerName offset must match evidence");

    auto* const ownerView = reinterpret_cast<OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_RRuleGameRules, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
    archive->WriteString(&ownerView->ownerName);
    constructResult->SetOwned(1);
  }

  /**
   * Address: 0x0054FAC0 (FUN_0054FAC0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CAniSkel` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromCAniSkelSlotLane1(moho::CAniSkel** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniSkel, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x0054FBA0 (FUN_0054FBA0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CAniSkel` slot as `shared` tracked-pointer state into one write archive lane.
   */
  void SaveSharedRawPointerFromCAniSkelSlotLane1(moho::CAniSkel** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniSkel, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x00550100 (FUN_00550100)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CAniSkel` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromCAniSkelSlotLane2(moho::CAniSkel** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CAniSkel, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x005518B0 (FUN_005518B0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CIntelGrid` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromCIntelGridSlotLane1(moho::CIntelGrid** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CIntelGrid, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x00551AF0 (FUN_00551AF0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CIntelGrid` slot as `shared` tracked-pointer state into one write archive lane.
   */
  void SaveSharedRawPointerFromCIntelGridSlotLane1(moho::CIntelGrid** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CIntelGrid, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x00551EA0 (FUN_00551EA0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CIntelGrid` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromCIntelGridSlotLane2(moho::CIntelGrid** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CIntelGrid, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x005549F0 (FUN_005549F0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_REntityBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromREntityBlueprintSlotLane1(moho::REntityBlueprint** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_REntityBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00554C30 (FUN_00554C30)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_REntityBlueprint` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromREntityBlueprintSlotLane1(moho::REntityBlueprint** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_REntityBlueprint, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00554F90 (FUN_00554F90)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_REntityBlueprint` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromREntityBlueprintValueLane1(moho::REntityBlueprint* value, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_REntityBlueprint, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0055ED00 (FUN_0055ED00)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Stats_StatItem` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromStats_StatItemSlotLane1(moho::Stats_StatItem** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Stats_StatItem, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x0055F390 (FUN_0055F390)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Stats_StatItem` slot as `shared` tracked-pointer state into one write archive lane.
   */
  void SaveSharedRawPointerFromStats_StatItemSlotLane1(moho::Stats_StatItem** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Stats_StatItem, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x0055F960 (FUN_0055F960)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Stats_StatItem` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromStats_StatItemSlotLane2(moho::Stats_StatItem** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Stats_StatItem, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x00571340 (FUN_00571340)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Listener_EFormationdStatus` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromListener_EFormationdStatusValueLane1(moho::Listener_EFormationdStatus* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Listener_EFormationdStatus, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00584800 (FUN_00584800)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromSimSlotLane1(moho::Sim** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00584830 (FUN_00584830)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CTaskStage` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCTaskStageSlotLane1(moho::CTaskStage** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CTaskStage, *valueSlot, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x00584B50 (FUN_00584B50)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSimSlotLane1(moho::Sim** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00584BB0 (FUN_00584BB0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CTaskStage` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCTaskStageSlotLane1(moho::CTaskStage** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CTaskStage, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005850C0 (FUN_005850C0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromSimValueLane1(moho::Sim* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00585200 (FUN_00585200)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CTaskStage` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCTaskStageValueLane1(moho::CTaskStage* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CTaskStage, value, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x0059DD30 (FUN_0059DD30)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_IFormationInstance` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIFormationInstanceSlotLane1(moho::IFormationInstance** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IFormationInstance, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0059DF70 (FUN_0059DF70)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_IFormationInstance` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromIFormationInstanceSlotLane1(moho::IFormationInstance** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IFormationInstance, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0059E920 (FUN_0059E920)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_IFormationInstance` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIFormationInstanceValueLane1(moho::IFormationInstance* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IFormationInstance, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x005CD980 (FUN_005CD980)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CIntelGrid` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromCIntelGridSlotLane3(moho::CIntelGrid** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CIntelGrid, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x005CDE40 (FUN_005CDE40)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CIntelGrid` slot as `shared` tracked-pointer state into one write archive lane.
   */
  void SaveSharedRawPointerFromCIntelGridSlotLane2(moho::CIntelGrid** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CIntelGrid, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x005CE400 (FUN_005CE400)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CIntelGrid` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromCIntelGridSlotLane4(moho::CIntelGrid** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CIntelGrid, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x005D16A0 (FUN_005D16A0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CEconRequest` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCEconRequestSlotLane1(moho::CEconRequest** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconRequest, *valueSlot, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x005D1A00 (FUN_005D1A00)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CEconRequest` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCEconRequestSlotLane1(moho::CEconRequest** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconRequest, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x005D1BE0 (FUN_005D1BE0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CEconRequest` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCEconRequestValueLane1(moho::CEconRequest* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconRequest, value, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x005F2100 (FUN_005F2100)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CCommandTask` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCCommandTaskSlotLane1(moho::CCommandTask** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CCommandTask, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x005F2140 (FUN_005F2140)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CCommandTask` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCCommandTaskSlotLane1(moho::CCommandTask** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CCommandTask, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x005F2280 (FUN_005F2280)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CCommandTask` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCCommandTaskValueLane1(moho::CCommandTask* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CCommandTask, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00642E00 (FUN_00642E00)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_RScaResource` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromRScaResourceSlotLane1(moho::RScaResource** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RScaResource, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x00642ED0 (FUN_00642ED0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_RScaResource` slot as `shared` tracked-pointer state into one write archive lane.
   */
  void SaveSharedRawPointerFromRScaResourceSlotLane1(moho::RScaResource** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_RScaResource, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x00643140 (FUN_00643140)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_RScaResource` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromRScaResourceSlotLane2(moho::RScaResource** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_RScaResource, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x0065A930 (FUN_0065A930)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CParticleTexture` value as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromCParticleTextureValueLane1(moho::CParticleTexture* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CParticleTexture, value, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x0066C2F0 (FUN_0066C2F0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_IEffect` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIEffectValueLane1(moho::IEffect* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IEffect, value, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x00673950 (FUN_00673950)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSimOwnerFieldLane1(int ownerToken, gpg::WriteArchive* archive)
  {
    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x148]{};
      moho::Sim* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x148, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
    reinterpret_cast<gpg::SerSaveConstructArgsResult*>(archive)->SetUnowned(0);
  }

  /**
   * Address: 0x00675170 (FUN_00675170)
   *
   * What it does:
   * Writes one intrusive-list-head-adjusted
   * `gpg::RRef_ManyToOneListener_ECollisionBeamEvent` lane as `unowned`
   * tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromManyToOneListener_ECollisionBeamEventIntrusiveHeadLane1(
    gpg::WriteArchive* archive,
    std::uint32_t* intrusiveListHeadSlot
  )
  {
    moho::ManyToOneListener<moho::ECollisionBeamEvent>* listener = nullptr;
    if (intrusiveListHeadSlot != nullptr && *intrusiveListHeadSlot != 0u) {
      listener = reinterpret_cast<moho::ManyToOneListener<moho::ECollisionBeamEvent>*>(
        *intrusiveListHeadSlot - sizeof(std::uint32_t)
      );
    }

    gpg::RRef listenerRef{};
    (void)gpg::RRef_ManyToOneListener_ECollisionBeamEvent(&listenerRef, listener);
    gpg::WriteRawPointer(archive, listenerRef, gpg::TrackedPointerState::Unowned, gpg::RRef{});
  }

  /**
   * Address: 0x006757F0 (FUN_006757F0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_ManyToOneListener_ECollisionBeamEvent` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* SaveUnownedRawPointerFromManyToOneListener_ECollisionBeamEventValueLane1(
    moho::ManyToOneListener<moho::ECollisionBeamEvent>* value,
    gpg::WriteArchive* archive
  )
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(
      archive,
      gpg::RRef_ManyToOneListener_ECollisionBeamEvent,
      value,
      gpg::TrackedPointerState::Unowned
    );
    return writeResult;
  }

  /**
   * Address: 0x00675830 (FUN_00675830)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_IEffect` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIEffectSlotLane1(moho::IEffect** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IEffect, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00675B40 (FUN_00675B40)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_IEffect` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromIEffectSlotLane1(moho::IEffect** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IEffect, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x006762C0 (FUN_006762C0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_IEffect` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromIEffectValueLane1(moho::IEffect* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IEffect, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00682220 (FUN_00682220)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CTextureScroller` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCTextureScrollerSlotLane1(moho::CTextureScroller** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CTextureScroller, *valueSlot, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x00682700 (FUN_00682700)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CTextureScroller` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCTextureScrollerSlotLane1(moho::CTextureScroller** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CTextureScroller, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00682EC0 (FUN_00682EC0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CTextureScroller` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCTextureScrollerValueLane1(moho::CTextureScroller* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CTextureScroller, value, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x0069E420 (FUN_0069E420)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSimOwnerFieldLane2(int ownerToken, gpg::WriteArchive* archive)
  {
    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x148]{};
      moho::Sim* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x148, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
    reinterpret_cast<gpg::SerSaveConstructArgsResult*>(archive)->SetUnowned(0);
  }

  /**
   * Address: 0x006AD2C0 (FUN_006AD2C0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSimOwnerFieldLane3(int ownerToken, gpg::WriteArchive* archive)
  {
    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x150]{};
      moho::Sim* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x150, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
    reinterpret_cast<gpg::SerSaveConstructArgsResult*>(archive)->SetUnowned(0);
  }

  /**
   * Address: 0x006B4160 (FUN_006B4160)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_IFormationInstance` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIFormationInstanceSlotLane1(moho::IFormationInstance** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IFormationInstance, *valueSlot, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x006B4190 (FUN_006B4190)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CEconStorage` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCEconStorageSlotLane1(moho::CEconStorage** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconStorage, *valueSlot, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x006B4560 (FUN_006B4560)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_IFormationInstance` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromIFormationInstanceSlotLane1(moho::IFormationInstance** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_IFormationInstance, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B45D0 (FUN_006B45D0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CEconStorage` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCEconStorageSlotLane1(moho::CEconStorage** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconStorage, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x006B4F40 (FUN_006B4F40)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_IFormationInstance` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromIFormationInstanceValueLane1(moho::IFormationInstance* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IFormationInstance, value, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x006B5080 (FUN_006B5080)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CEconStorage` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCEconStorageValueLane1(moho::CEconStorage* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconStorage, value, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x006E10F0 (FUN_006E10F0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSimSlotLane1Variant2(moho::Sim** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, *valueSlot, gpg::TrackedPointerState::Unowned);
    reinterpret_cast<gpg::SerSaveConstructArgsResult*>(archive)->SetUnowned(0);
  }

  /**
   * Address: 0x006E1140 (FUN_006E1140)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSimSlotLane2(moho::Sim** valueSlot, gpg::WriteArchive* archive, int a3)
  {
    (void)a3;
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, *valueSlot, gpg::TrackedPointerState::Unowned);
    reinterpret_cast<gpg::SerSaveConstructArgsResult*>(archive)->SetUnowned(0);
  }

  /**
   * Address: 0x006E2A40 (FUN_006E2A40)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSimSlotLane3(moho::Sim** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, *valueSlot, gpg::TrackedPointerState::Unowned);
    reinterpret_cast<gpg::SerSaveConstructArgsResult*>(archive)->SetUnowned(0);
  }

  /**
   * Address: 0x006EBB00 (FUN_006EBB00)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_IFormationInstance` value as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromIFormationInstanceValueLane1(moho::IFormationInstance* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_IFormationInstance, value, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x006FA5B0 (FUN_006FA5B0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSimOwnerFieldLane4(int ownerToken, gpg::WriteArchive* archive)
  {
    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x148]{};
      moho::Sim* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x148, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
    reinterpret_cast<gpg::SerSaveConstructArgsResult*>(archive)->SetUnowned(0);
  }

  /**
   * Address: 0x007066B0 (FUN_007066B0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CEconomy` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCEconomySlotLane1(moho::CEconomy** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconomy, *valueSlot, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x00706B30 (FUN_00706B30)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CEconomy` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCEconomySlotLane1(moho::CEconomy** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconomy, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x00706C40 (FUN_00706C40)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_PathQueue` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromPathQueueSlotLane1(moho::PathQueue** valueSlot, gpg::RRef* ownerRef, gpg::WriteArchive* archive)
  {
    gpg::RRef objectRef{};
    gpg::RRef_PathQueue(&objectRef, *valueSlot);
    gpg::WriteRawPointer(archive, objectRef, gpg::TrackedPointerState::Owned, *ownerRef);
  }

  /**
   * Address: 0x007071B0 (FUN_007071B0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CEconomy` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCEconomyValueLane1(moho::CEconomy* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconomy, value, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x00710340 (FUN_00710340)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CArmyStatItem` owner-field lane as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCArmyStatItemOwnerFieldLane1(gpg::WriteArchive* archive, int ownerToken)
  {
    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x4]{};
      moho::CArmyStatItem* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x4, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CArmyStatItem, ownerView->ownerField, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x007128D0 (FUN_007128D0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CArmyStatItem` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCArmyStatItemSlotLane1(moho::CArmyStatItem** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CArmyStatItem, *valueSlot, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x00712940 (FUN_00712940)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CArmyStatItem` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCArmyStatItemSlotLane1(moho::CArmyStatItem** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CArmyStatItem, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00713080 (FUN_00713080)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CArmyStatItem` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromCArmyStatItemSlotLane1(moho::CArmyStatItem** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CArmyStatItem, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x007130C0 (FUN_007130C0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CArmyStatItem` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCArmyStatItemSlotLane1(moho::CArmyStatItem** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CArmyStatItem, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00714180 (FUN_00714180)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CArmyStatItem` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCArmyStatItemValueLane1(moho::CArmyStatItem* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CArmyStatItem, value, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x007142C0 (FUN_007142C0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CArmyStatItem` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCArmyStatItemValueLane1(moho::CArmyStatItem* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CArmyStatItem, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0072ADE0 (FUN_0072ADE0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CSquad` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromCSquadValueLane1(moho::CSquad* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CSquad, value, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x0072AE10 (FUN_0072AE10)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CSquad` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCSquadSlotLane1(moho::CSquad** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CSquad, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x0072AED0 (FUN_0072AED0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CSquad` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCSquadSlotLane1(moho::CSquad** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CSquad, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x0072B1A0 (FUN_0072B1A0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CSquad` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCSquadValueLane1(moho::CSquad* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CSquad, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x007610B0 (FUN_007610B0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSimOwnerFieldLane5(gpg::WriteArchive* archive, int ownerToken, int a3, gpg::SerSaveConstructArgsResult* constructResult)
  {
    (void)a3;
    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x4]{};
      moho::Sim* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x4, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
    constructResult->SetUnowned(0);
  }

  /**
   * Address: 0x00761160 (FUN_00761160)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSimOwnerFieldLane6(int ownerToken, gpg::WriteArchive* archive)
  {
    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x4]{};
      moho::Sim* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x4, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
    reinterpret_cast<gpg::SerSaveConstructArgsResult*>(archive)->SetUnowned(0);
  }

  /**
   * Address: 0x007689D0 (FUN_007689D0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_PathQueue_Impl` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromPathQueue_ImplSlotLane1(gpg::WriteArchive* archive, moho::PathQueue::Impl** valueSlot)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_PathQueue_Impl, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x0076A450 (FUN_0076A450)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_PathQueue_Impl` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromPathQueue_ImplSlotLane2(gpg::WriteArchive* archive, moho::PathQueue::Impl** valueSlot)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_PathQueue_Impl, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x0076ADA0 (FUN_0076ADA0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_PathQueue_Impl` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromPathQueue_ImplSlotLane3(moho::PathQueue::Impl** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_PathQueue_Impl, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x0076B330 (FUN_0076B330)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_PathQueue_Impl` slot as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromPathQueue_ImplSlotLane1(moho::PathQueue::Impl** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_PathQueue_Impl, *valueSlot, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x0076B490 (FUN_0076B490)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_PathQueue_Impl` slot as `owned` tracked-pointer state into one write archive lane.
   */
  void SaveOwnedRawPointerFromPathQueue_ImplSlotLane4(moho::PathQueue::Impl** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_PathQueue_Impl, *valueSlot, gpg::TrackedPointerState::Owned);
  }

  /**
   * Address: 0x0076B680 (FUN_0076B680)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_PathQueue_Impl` value as `owned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteOwnedRawPointerFromPathQueue_ImplValueLane1(moho::PathQueue::Impl* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_PathQueue_Impl, value, gpg::TrackedPointerState::Owned);
    return writeResult;
  }

  /**
   * Address: 0x00774320 (FUN_00774320)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CEconRequest` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCEconRequestSlotLane1(moho::CEconRequest** valueSlot, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconRequest, *valueSlot, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x007744E0 (FUN_007744E0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CEconRequest` slot as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromCEconRequestSlotLane1(moho::CEconRequest** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconRequest, *valueSlot, gpg::TrackedPointerState::Unowned);
  }

  /**
   * Address: 0x00774700 (FUN_00774700)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CEconRequest` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCEconRequestValueLane1(moho::CEconRequest* value, gpg::WriteArchive* archive, int a5)
  {
    (void)a5;
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconRequest, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x00774D00 (FUN_00774D00)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_CEconomy` value as `unowned` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteUnownedRawPointerFromCEconomyValueLane1(moho::CEconomy* value, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_CEconomy, value, gpg::TrackedPointerState::Unowned);
    return writeResult;
  }

  /**
   * Address: 0x007766B0 (FUN_007766B0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSimOwnerFieldLane7(gpg::WriteArchive* archive, int ownerToken, int a3, gpg::SerSaveConstructArgsResult* constructResult)
  {
    (void)a3;
    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x148]{};
      moho::Sim* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x148, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
    constructResult->SetUnowned(0);
  }

  /**
   * Address: 0x00776760 (FUN_00776760)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_Sim` owner-field lane as `unowned` tracked-pointer state into one write archive lane.
   */
  void SaveUnownedRawPointerFromSimOwnerFieldLane8(int ownerToken, gpg::WriteArchive* archive)
  {
    struct OwnerFieldView
    {
      std::uint8_t reserved00[0x148]{};
      moho::Sim* ownerField;
    };
    static_assert(offsetof(OwnerFieldView, ownerField) == 0x148, "OwnerFieldView::ownerField offset must match evidence");

    const auto* const ownerView = reinterpret_cast<const OwnerFieldView*>(static_cast<std::uintptr_t>(ownerToken));
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_Sim, ownerView->ownerField, gpg::TrackedPointerState::Unowned);
    reinterpret_cast<gpg::SerSaveConstructArgsResult*>(archive)->SetUnowned(0);
  }

  /**
   * Address: 0x00883EA0 (FUN_00883EA0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_LaunchInfoBase` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromLaunchInfoBaseSlotLane1(moho::LaunchInfoBase** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_LaunchInfoBase, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

  /**
   * Address: 0x008847C0 (FUN_008847C0)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_LaunchInfoBase` slot as `shared` tracked-pointer state into one write archive lane.
   */
  void SaveSharedRawPointerFromLaunchInfoBaseSlotLane1(moho::LaunchInfoBase** valueSlot, gpg::WriteArchive* archive)
  {
    SaveTrackedPointerFromRefBuilder(archive, gpg::RRef_LaunchInfoBase, *valueSlot, gpg::TrackedPointerState::Shared);
  }

  /**
   * Address: 0x00884E70 (FUN_00884E70)
   *
   * What it does:
   * Writes one reflected `gpg::RRef_LaunchInfoBase` slot as `shared` tracked-pointer state into one write archive lane.
   */
  gpg::WriteArchive* WriteSharedRawPointerFromLaunchInfoBaseSlotLane2(moho::LaunchInfoBase** valueSlot, gpg::WriteArchive* archive)
  {
    auto* const writeResult = WriteTrackedPointerFromRefBuilder(archive, gpg::RRef_LaunchInfoBase, *valueSlot, gpg::TrackedPointerState::Shared);
    return writeResult;
  }

} // namespace
namespace
{
  constexpr const char* kSerializationHeaderPath =
    "c:\\work\\rts\\main\\code\\src\\libs\\gpgcore\\reflection\\serialization.h";

  struct SerSaveLoadHelperInitView
  {
    void* vtable = nullptr;                     // +0x00
    gpg::SerHelperBase* helperNext = nullptr;  // +0x04
    gpg::SerHelperBase* helperPrev = nullptr;  // +0x08
    gpg::RType::load_func_t loadCallback = nullptr;
    gpg::RType::save_func_t saveCallback = nullptr;
  };
  static_assert(offsetof(SerSaveLoadHelperInitView, helperNext) == 0x04,
    "SerSaveLoadHelperInitView::helperNext offset must be 0x04");
  static_assert(offsetof(SerSaveLoadHelperInitView, helperPrev) == 0x08,
    "SerSaveLoadHelperInitView::helperPrev offset must be 0x08");
  static_assert(offsetof(SerSaveLoadHelperInitView, loadCallback) == 0x0C,
    "SerSaveLoadHelperInitView::loadCallback offset must be 0x0C");
  static_assert(offsetof(SerSaveLoadHelperInitView, saveCallback) == 0x10,
    "SerSaveLoadHelperInitView::saveCallback offset must be 0x10");
  static_assert(sizeof(SerSaveLoadHelperInitView) == 0x14,
    "SerSaveLoadHelperInitView size must be 0x14");

  [[nodiscard]] gpg::RType::load_func_t InstallSerSaveLoadHelperCallbacksByTypeName(
    SerSaveLoadHelperInitView* const helper,
    const char* const reflectedTypeName
  )
  {
    GPG_ASSERT(helper != nullptr);
    GPG_ASSERT(reflectedTypeName != nullptr);

    gpg::RType* const type = gpg::REF_FindTypeNamed(reflectedTypeName);
    GPG_ASSERT(type != nullptr);

    if (type->serLoadFunc_ != nullptr) {
      gpg::HandleAssertFailure("!type->mSerLoadFunc", 84, kSerializationHeaderPath);
    }

    const bool saveWasNull = type->serSaveFunc_ == nullptr;
    const gpg::RType::load_func_t loadCallback = helper->loadCallback;
    type->serLoadFunc_ = loadCallback;

    if (!saveWasNull) {
      gpg::HandleAssertFailure("!type->mSerSaveFunc", 87, kSerializationHeaderPath);
    }

    type->serSaveFunc_ = helper->saveCallback;
    return loadCallback;
  }

  /**
   * Address: 0x004ED140 (FUN_004ED140)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Wm3::AxisAlignedBox3f`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallWm3AxisAlignedBox3fSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Wm3::AxisAlignedBox3f");
  }

  /**
   * Address: 0x004ED1E0 (FUN_004ED1E0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Wm3::IVector2i`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallWm3IVector2iSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Wm3::IVector2i");
  }

  /**
   * Address: 0x004ED280 (FUN_004ED280)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Wm3::IVector3i`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallWm3IVector3iSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Wm3::IVector3i");
  }

  /**
   * Address: 0x004ED320 (FUN_004ED320)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Wm3::Vector2f`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallWm3Vector2fSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Wm3::Vector2f");
  }

  /**
   * Address: 0x004ED3C0 (FUN_004ED3C0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Wm3::Vector3f`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallWm3Vector3fSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Wm3::Vector3f");
  }

  /**
   * Address: 0x004ED460 (FUN_004ED460)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::Vector4f`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoVector4fSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::Vector4f");
  }

  /**
   * Address: 0x004ED500 (FUN_004ED500)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Wm3::Quaternionf`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallWm3QuaternionfSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Wm3::Quaternionf");
  }

  /**
   * Address: 0x004ED5A0 (FUN_004ED5A0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::VEulers3`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoVEulers3SerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::VEulers3");
  }

  /**
   * Address: 0x004ED640 (FUN_004ED640)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::VAxes3`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoVAxes3SerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::VAxes3");
  }

  /**
   * Address: 0x004F0840 (FUN_004F0840)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::VTransform`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoVTransformSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::VTransform");
  }

  /**
   * Address: 0x0050C730 (FUN_0050C730)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SCoordsVec2`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSCoordsVec2SerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SCoordsVec2");
  }

  /**
   * Address: 0x0050C7D0 (FUN_0050C7D0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SOCellPos`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSOCellPosSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SOCellPos");
  }

  /**
   * Address: 0x0050C910 (FUN_0050C910)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SPointVector`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSPointVectorSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SPointVector");
  }

  /**
   * Address: 0x0050C9B0 (FUN_0050C9B0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SFootprint`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSFootprintSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SFootprint");
  }

  /**
   * Address: 0x00523110 (FUN_00523110)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::ERuleBPUnitMovementType`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoERuleBPUnitMovementTypeSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::ERuleBPUnitMovementType");
  }

  /**
   * Address: 0x005231B0 (FUN_005231B0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::ERuleBPUnitCommandCaps`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoERuleBPUnitCommandCapsSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::ERuleBPUnitCommandCaps");
  }

  /**
   * Address: 0x00523250 (FUN_00523250)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::ERuleBPUnitToggleCaps`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoERuleBPUnitToggleCapsSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::ERuleBPUnitToggleCaps");
  }

  /**
   * Address: 0x00543260 (FUN_00543260)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::LaunchInfoNew`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoLaunchInfoNewSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::LaunchInfoNew");
  }

  /**
   * Address: 0x005473B0 (FUN_005473B0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EResourceType`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEResourceTypeSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EResourceType");
  }

  /**
   * Address: 0x00547450 (FUN_00547450)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::ResourceDeposit`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoResourceDepositSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::ResourceDeposit");
  }

  /**
   * Address: 0x0054C610 (FUN_0054C610)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CAniPose`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCAniPoseSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CAniPose");
  }

  /**
   * Address: 0x0054C8F0 (FUN_0054C8F0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CAniPoseBone`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCAniPoseBoneSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CAniPoseBone");
  }

  /**
   * Address: 0x00550CF0 (FUN_00550CF0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SSTIArmyConstantData`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSSTIArmyConstantDataSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SSTIArmyConstantData");
  }

  /**
   * Address: 0x005589E0 (FUN_005589E0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EntId`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEntIdSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EntId");
  }

  /**
   * Address: 0x00558A80 (FUN_00558A80)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SSTIEntityConstantData`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSSTIEntityConstantDataSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SSTIEntityConstantData");
  }

  /**
   * Address: 0x0055B200 (FUN_0055B200)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::ESTITargetType`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoESTITargetTypeSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::ESTITargetType");
  }

  /**
   * Address: 0x0055B2A0 (FUN_0055B2A0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SSTITarget`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSSTITargetSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SSTITarget");
  }

  /**
   * Address: 0x0055C860 (FUN_0055C860)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EJobType`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEJobTypeSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EJobType");
  }

  /**
   * Address: 0x0055C900 (FUN_0055C900)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EFireState`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEFireStateSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EFireState");
  }

  /**
   * Address: 0x0055C9A0 (FUN_0055C9A0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EUnitState`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEUnitStateSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EUnitState");
  }

  /**
   * Address: 0x0055CA40 (FUN_0055CA40)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::UnitWeaponInfo`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoUnitWeaponInfoSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::UnitWeaponInfo");
  }

  /**
   * Address: 0x0055D100 (FUN_0055D100)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SSTIUnitVariableData`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSSTIUnitVariableDataSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SSTIUnitVariableData");
  }

  /**
   * Address: 0x00563F70 (FUN_00563F70)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EEconResource`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEEconResourceSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EEconResource");
  }

  /**
   * Address: 0x00564010 (FUN_00564010)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SEconValue`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSEconValueSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SEconValue");
  }

  /**
   * Address: 0x005640B0 (FUN_005640B0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SEconTotals`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSEconTotalsSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SEconTotals");
  }

  /**
   * Address: 0x0056B8C0 (FUN_0056B8C0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SUnitOffsetInfo`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSUnitOffsetInfoSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SUnitOffsetInfo");
  }

  /**
   * Address: 0x0056BAD0 (FUN_0056BAD0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SOffsetInfo`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSOffsetInfoSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SOffsetInfo");
  }

  /**
   * Address: 0x0056BCE0 (FUN_0056BCE0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::IFormationInstance`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoIFormationInstanceSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::IFormationInstance");
  }

  /**
   * Address: 0x0056BD80 (FUN_0056BD80)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SAssignedLocInfo`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSAssignedLocInfoSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SAssignedLocInfo");
  }

  /**
   * Address: 0x0056CA10 (FUN_0056CA10)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CFormationInstance`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCFormationInstanceSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CFormationInstance");
  }

  /**
   * Address: 0x00591B90 (FUN_00591B90)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SMassInfo`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSMassInfoSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SMassInfo");
  }

  /**
   * Address: 0x005982F0 (FUN_005982F0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::ECollisionType`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoECollisionTypeSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::ECollisionType");
  }

  /**
   * Address: 0x005A6EF0 (FUN_005A6EF0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EAiNavigatorStatus`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEAiNavigatorStatusSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EAiNavigatorStatus");
  }

  /**
   * Address: 0x005A6F90 (FUN_005A6F90)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EAiNavigatorEvent`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEAiNavigatorEventSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EAiNavigatorEvent");
  }

  /**
   * Address: 0x005B9230 (FUN_005B9230)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SValuePair`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSValuePairSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SValuePair");
  }

  /**
   * Address: 0x005DB720 (FUN_005DB720)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EAiAttackerEvent`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEAiAttackerEventSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EAiAttackerEvent");
  }

  /**
   * Address: 0x005E34A0 (FUN_005E34A0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EAiTargetType`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEAiTargetTypeSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EAiTargetType");
  }

  /**
   * Address: 0x005E8B90 (FUN_005E8B90)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EAiTransportEvent`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEAiTransportEventSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EAiTransportEvent");
  }

  /**
   * Address: 0x005E91E0 (FUN_005E91E0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SAttachPoint`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSAttachPointSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SAttachPoint");
  }

  /**
   * Address: 0x005E9490 (FUN_005E9490)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::STransportPickUpInfo`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSTransportPickUpInfoSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::STransportPickUpInfo");
  }

  /**
   * Address: 0x005F1A70 (FUN_005F1A70)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitAssistMoveTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitAssistMoveTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitAssistMoveTask");
  }

  /**
   * Address: 0x005F44F0 (FUN_005F44F0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitAttackTargetTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitAttackTargetTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitAttackTargetTask");
  }

  /**
   * Address: 0x005FBAE0 (FUN_005FBAE0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CBuildTaskHelper`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCBuildTaskHelperSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CBuildTaskHelper");
  }

  /**
   * Address: 0x005FBBA0 (FUN_005FBBA0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitMobileBuildTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitMobileBuildTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitMobileBuildTask");
  }

  /**
   * Address: 0x005FBC90 (FUN_005FBC90)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitUpgradeTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitUpgradeTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitUpgradeTask");
  }

  /**
   * Address: 0x005FBD50 (FUN_005FBD50)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitRepairTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitRepairTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitRepairTask");
  }

  /**
   * Address: 0x005FBE10 (FUN_005FBE10)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CFactoryBuildTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCFactoryBuildTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CFactoryBuildTask");
  }

  /**
   * Address: 0x005FBED0 (FUN_005FBED0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitSacrificeTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitSacrificeTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitSacrificeTask");
  }

  /**
   * Address: 0x00605320 (FUN_00605320)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitCaptureTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitCaptureTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitCaptureTask");
  }

  /**
   * Address: 0x00607730 (FUN_00607730)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitCarrierRetrieve`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitCarrierRetrieveSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitCarrierRetrieve");
  }

  /**
   * Address: 0x006077F0 (FUN_006077F0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitCarrierLand`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitCarrierLandSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitCarrierLand");
  }

  /**
   * Address: 0x006078B0 (FUN_006078B0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitCarrierLaunch`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitCarrierLaunchSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitCarrierLaunch");
  }

  /**
   * Address: 0x0060B980 (FUN_0060B980)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EAiResult`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEAiResultSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EAiResult");
  }

  /**
   * Address: 0x0060BAE0 (FUN_0060BAE0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitGetBuiltTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitGetBuiltTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitGetBuiltTask");
  }
} // namespace

namespace
{

  /**
   * Address: 0x0060BBA0 (FUN_0060BBA0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitTeleportTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitTeleportTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitTeleportTask");
  }

  /**
   * Address: 0x0060BC60 (FUN_0060BC60)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitFireAtTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitFireAtTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitFireAtTask");
  }

  /**
   * Address: 0x00610000 (FUN_00610000)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitFerryTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitFerryTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitFerryTask");
  }

  /**
   * Address: 0x006100C0 (FUN_006100C0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitWaitForFerryTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitWaitForFerryTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitWaitForFerryTask");
  }

  /**
   * Address: 0x006148A0 (FUN_006148A0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitGuardTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitGuardTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitGuardTask");
  }

  /**
   * Address: 0x00619C20 (FUN_00619C20)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitMoveTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitMoveTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitMoveTask");
  }

  /**
   * Address: 0x00619CE0 (FUN_00619CE0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitFormAndMoveTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitFormAndMoveTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitFormAndMoveTask");
  }

  /**
   * Address: 0x0061C6E0 (FUN_0061C6E0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitPatrolTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitPatrolTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitPatrolTask");
  }

  /**
   * Address: 0x0061E500 (FUN_0061E500)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitPodAssist`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitPodAssistSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitPodAssist");
  }

  /**
   * Address: 0x006204B0 (FUN_006204B0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitReclaimTask`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitReclaimTaskSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitReclaimTask");
  }

  /**
   * Address: 0x00622210 (FUN_00622210)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitRefuel`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitRefuelSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitRefuel");
  }

  /**
   * Address: 0x00626B30 (FUN_00626B30)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SPickUpInfo`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSPickUpInfoSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SPickUpInfo");
  }

  /**
   * Address: 0x00626F90 (FUN_00626F90)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitLoadUnits`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitLoadUnitsSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitLoadUnits");
  }

  /**
   * Address: 0x00627050 (FUN_00627050)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CUnitUnloadUnits`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCUnitUnloadUnitsSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CUnitUnloadUnits");
  }

  /**
   * Address: 0x0062F870 (FUN_0062F870)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EPathPointState`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEPathPointStateSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EPathPointState");
  }

  /**
   * Address: 0x00635140 (FUN_00635140)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CBoneEntityManipulator`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCBoneEntityManipulatorSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CBoneEntityManipulator");
  }

  /**
   * Address: 0x00636F80 (FUN_00636F80)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CBuilderArmManipulator`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCBuilderArmManipulatorSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CBuilderArmManipulator");
  }

  /**
   * Address: 0x00639FA0 (FUN_00639FA0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CFootPlantManipulator`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCFootPlantManipulatorSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CFootPlantManipulator");
  }

  /**
   * Address: 0x00645310 (FUN_00645310)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CRotateManipulator`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCRotateManipulatorSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CRotateManipulator");
  }

  /**
   * Address: 0x006466B0 (FUN_006466B0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CSlaveManipulator`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCSlaveManipulatorSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CSlaveManipulator");
  }

  /**
   * Address: 0x006484C0 (FUN_006484C0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CSlideManipulator`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCSlideManipulatorSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CSlideManipulator");
  }

  /**
   * Address: 0x00649930 (FUN_00649930)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CStorageManipulator`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCStorageManipulatorSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CStorageManipulator");
  }

  /**
   * Address: 0x0064B150 (FUN_0064B150)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CThrustManipulator`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCThrustManipulatorSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CThrustManipulator");
  }

  /**
   * Address: 0x0065F150 (FUN_0065F150)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CEfxEmitter`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCEfxEmitterSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CEfxEmitter");
  }

  /**
   * Address: 0x006722F0 (FUN_006722F0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CEfxTrailEmitter`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCEfxTrailEmitterSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CEfxTrailEmitter");
  }

  /**
   * Address: 0x00693DE0 (FUN_00693DE0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EntitySetBase`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEntitySetBaseSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EntitySetBase");
  }

  /**
   * Address: 0x00693E80 (FUN_00693E80)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EntitySetTemplate_Entity`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEntitySetTemplateEntitySerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EntitySetTemplate_Entity");
  }

  /**
   * Address: 0x00693F20 (FUN_00693F20)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::WeakEntitySetTemplate_Entity`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoWeakEntitySetTemplateEntitySerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::WeakEntitySetTemplate_Entity");
  }

  /**
   * Address: 0x00696CE0 (FUN_00696CE0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::MotorSinkAway`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoMotorSinkAwaySerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::MotorSinkAway");
  }

  /**
   * Address: 0x0069E860 (FUN_0069E860)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EProjectileImpactEvent`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEProjectileImpactEventSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EProjectileImpactEvent");
  }

  /**
   * Address: 0x006AE810 (FUN_006AE810)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SInfoCache`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSInfoCacheSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SInfoCache");
  }

  /**
   * Address: 0x006AEA20 (FUN_006AEA20)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::Unit`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoUnitSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::Unit");
  }

  /**
   * Address: 0x006BA420 (FUN_006BA420)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EUnitMotionState`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEUnitMotionStateSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EUnitMotionState");
  }

  /**
   * Address: 0x006BA4C0 (FUN_006BA4C0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EUnitMotionCarrierEvent`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEUnitMotionCarrierEventSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EUnitMotionCarrierEvent");
  }

  /**
   * Address: 0x006BA560 (FUN_006BA560)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EUnitMotionHorzEvent`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEUnitMotionHorzEventSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EUnitMotionHorzEvent");
  }

  /**
   * Address: 0x006BA600 (FUN_006BA600)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EUnitMotionVertEvent`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEUnitMotionVertEventSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EUnitMotionVertEvent");
  }

  /**
   * Address: 0x006BA6A0 (FUN_006BA6A0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EUnitMotionTurnEvent`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEUnitMotionTurnEventSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EUnitMotionTurnEvent");
  }

  /**
   * Address: 0x006BA740 (FUN_006BA740)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EAirCombatState`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEAirCombatStateSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EAirCombatState");
  }

  /**
   * Address: 0x006E9760 (FUN_006E9760)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::ECommandEvent`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoECommandEventSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::ECommandEvent");
  }

  /**
   * Address: 0x0070E510 (FUN_0070E510)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::ETriggerOperator`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoETriggerOperatorSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::ETriggerOperator");
  }

  /**
   * Address: 0x0070E5B0 (FUN_0070E5B0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SCondition`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSConditionSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SCondition");
  }

  /**
   * Address: 0x0070E9F0 (FUN_0070E9F0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::STrigger`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSTriggerSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::STrigger");
  }

  /**
   * Address: 0x00718900 (FUN_00718900)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EThreatType`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEThreatTypeSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EThreatType");
  }

  /**
   * Address: 0x0072A4D0 (FUN_0072A4D0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::ESquadClass`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoESquadClassSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::ESquadClass");
  }

  /**
   * Address: 0x0072A5F0 (FUN_0072A5F0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CSquad`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCSquadSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CSquad");
  }

  /**
   * Address: 0x0072A710 (FUN_0072A710)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CPlatoon`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCPlatoonSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CPlatoon");
  }

  /**
   * Address: 0x007632D0 (FUN_007632D0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::HPathCell`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoHPathCellSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::HPathCell");
  }

  /**
   * Address: 0x00763370 (FUN_00763370)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::NavPath`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoNavPathSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::NavPath");
  }

  /**
   * Address: 0x00767080 (FUN_00767080)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::PathQueue`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoPathQueueSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::PathQueue");
  }

  /**
   * Address: 0x00773D00 (FUN_00773D00)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CEconomy`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCEconomySerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CEconomy");
  }

  /**
   * Address: 0x00773E20 (FUN_00773E20)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CEconStorage`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCEconStorageSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CEconStorage");
  }

  /**
   * Address: 0x00773F40 (FUN_00773F40)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::CEconRequest`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoCEconRequestSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::CEconRequest");
  }

  /**
   * Address: 0x00777E20 (FUN_00777E20)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::EScrollType`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoEScrollTypeSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::EScrollType");
  }

  /**
   * Address: 0x00777EC0 (FUN_00777EC0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SScroller`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSScrollerSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SScroller");
  }

  /**
   * Address: 0x0077A6D0 (FUN_0077A6D0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SDecalInfo`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSDecalInfoSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SDecalInfo");
  }

  /**
   * Address: 0x00899220 (FUN_00899220)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `Moho::SSessionSaveData`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallMohoSSessionSaveDataSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "Moho::SSessionSaveData");
  }

  /**
   * Address: 0x0091FA30 (FUN_0091FA30)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `lua::TString`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallluaTStringSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "lua::TString");
  }

  /**
   * Address: 0x0091FBC0 (FUN_0091FBC0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `lua::Table`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallluaTableSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "lua::Table");
  }

  /**
   * Address: 0x0091FD50 (FUN_0091FD50)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `lua::LClosure`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallluaLClosureSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "lua::LClosure");
  }

  /**
   * Address: 0x0091FEE0 (FUN_0091FEE0)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `lua::UpVal`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallluaUpValSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "lua::UpVal");
  }

  /**
   * Address: 0x00920070 (FUN_00920070)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `lua::Proto`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallluaProtoSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "lua::Proto");
  }

  /**
   * Address: 0x00920200 (FUN_00920200)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `lua::lua_State`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstalllualuaStateSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "lua::lua_State");
  }

  /**
   * Address: 0x00920390 (FUN_00920390)
   *
   * What it does:
   * Installs serializer load/save callbacks for reflected type `lua::Udata`.
   */
  [[nodiscard]] gpg::RType::load_func_t InstallluaUdataSerializerCallbacks(
    SerSaveLoadHelperInitView* const helper
  )
  {
    return InstallSerSaveLoadHelperCallbacksByTypeName(helper, "lua::Udata");
  }

} // namespace
