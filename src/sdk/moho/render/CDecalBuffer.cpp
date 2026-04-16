#include "moho/render/CDecalBuffer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <typeinfo>

#include "gpg/core/containers/Rect2.h"
#include "gpg/core/utils/Global.h"
#include "moho/ai/CAiReconDBImpl.h"
#include "moho/render/CDecalHandle.h"
#include "moho/sim/CArmyImpl.h"
#include "moho/sim/Sim.h"

using namespace moho;

gpg::RType* CDecalBuffer::sType = nullptr;

namespace
{
  struct DecalBucketNode
  {
    DecalBucketNode* left;
    DecalBucketNode* parent;
    DecalBucketNode* right;
    CDecalHandle* handle;
    std::uint8_t color;
    std::uint8_t isNil;
    std::uint8_t reserved12[2];
  };
  static_assert(sizeof(DecalBucketNode) == 0x14, "DecalBucketNode size must be 0x14");

  struct DecalMapNode
  {
    DecalMapNode* left;
    DecalMapNode* parent;
    DecalMapNode* right;
    std::uint32_t startTick;
    void* bucketAllocatorCookie;
    DecalBucketNode* bucketHead;
    std::uint32_t bucketSize;
    std::uint8_t color;
    std::uint8_t isNil;
    std::uint8_t reserved1E[2];
  };
  static_assert(sizeof(DecalMapNode) == 0x20, "DecalMapNode size must be 0x20");

  struct DecalBucketTreeStorage
  {
    void* allocatorCookie;  // +0x00
    DecalBucketNode* head;  // +0x04
    std::uint32_t size;     // +0x08
  };
  static_assert(sizeof(DecalBucketTreeStorage) == 0x0C, "DecalBucketTreeStorage size must be 0x0C");

  /**
   * Address: 0x0077CD00 (FUN_0077CD00)
   *
   * What it does:
   * Allocates one decal-bucket RB-tree node lane with null links/payload and
   * default marker bytes (`color=black`, `isNil=0`).
   */
  [[nodiscard]] DecalBucketNode* AllocateDecalBucketNode()
  {
    auto* const node = static_cast<DecalBucketNode*>(::operator new(sizeof(DecalBucketNode)));
    node->left = nullptr;
    node->parent = nullptr;
    node->right = nullptr;
    node->handle = nullptr;
    node->color = 1u;
    node->isNil = 0u;
    node->reserved12[0] = 0u;
    node->reserved12[1] = 0u;
    return node;
  }

  [[nodiscard]]
  DecalMapNode* AllocateMapHeadNode()
  {
    auto* const node = static_cast<DecalMapNode*>(::operator new(sizeof(DecalMapNode)));
    node->left = node;
    node->parent = node;
    node->right = node;
    node->startTick = 0;
    node->bucketAllocatorCookie = nullptr;
    node->bucketHead = nullptr;
    node->bucketSize = 0;
    node->color = 1;
    node->isNil = 1;
    node->reserved1E[0] = 0;
    node->reserved1E[1] = 0;
    return node;
  }

  void DestroyMapNodes(DecalMapNode* node, const DecalMapNode* const head);

  /**
   * Address: 0x0077BCD0 (FUN_0077BCD0)
   *
   * What it does:
   * Finds the lower-bound start-tick bucket node for a given decal start tick
   * using the sentinel-backed RB-tree layout.
   */
  [[maybe_unused]] [[nodiscard]] DecalMapNode* FindStartTickBucketNode(
    DecalMapNode* const head, const std::uint32_t startTick
  ) noexcept
  {
    if (head == nullptr) {
      return nullptr;
    }

    DecalMapNode* candidate = head;
    for (DecalMapNode* node = head->parent; node != nullptr && node->isNil == 0u;) {
      candidate = node;
      if (startTick < node->startTick) {
        node = node->left;
      } else {
        node = node->right;
      }
    }

    return candidate;
  }

  /**
   * Address: 0x0077A0A0 (FUN_0077A0A0)
   *
   * What it does:
   * Appends one visible decal payload into the pending publish vector.
   */
  void AppendVisibleDecal(msvc8::vector<SDecalInfo>& visibleDecals, const SDecalInfo& decalInfo)
  {
    visibleDecals.push_back(decalInfo);
  }

  void DestroyBucketTreeNodes(DecalBucketNode* node, const DecalBucketNode* const head)
  {
    if (!node || node == head) {
      return;
    }

    DestroyBucketTreeNodes(node->left, head);
    DestroyBucketTreeNodes(node->right, head);
    ::operator delete(node);
  }

  /**
   * Address: 0x00779B80 (FUN_00779B80, sub_779B80)
   *
   * What it does:
   * Releases one decal-bucket RB-tree storage lane by erasing all nodes,
   * deleting the head sentinel, and zeroing `{head,size}`.
   */
  std::int32_t ReleaseDecalBucketTreeStorage(DecalBucketTreeStorage* const storage)
  {
    if (storage == nullptr) {
      return 0;
    }

    DecalBucketNode* const head = storage->head;
    if (head != nullptr) {
      DestroyBucketTreeNodes(head->left, head);
      ::operator delete(head);
    }

    storage->head = nullptr;
    storage->size = 0u;
    return 0;
  }

  /**
   * Address: 0x00779240 (FUN_00779240, sub_779240)
   *
   * What it does:
   * Releases one start-tick map RB-tree storage lane by erasing all map nodes,
   * deleting the head sentinel, and zeroing `{head,size}`.
   */
  std::int32_t ReleaseDecalStartTickMapStorage(CDecalStartTickMapStorage* const storage)
  {
    if (storage == nullptr) {
      return 0;
    }

    auto* const head = static_cast<DecalMapNode*>(storage->head);
    if (head != nullptr) {
      DestroyMapNodes(head->left, head);
      ::operator delete(head);
    }

    storage->head = nullptr;
    storage->size = 0u;
    return 0;
  }

  /**
   * Address: 0x0077B7D0 (FUN_0077B7D0)
   *
   * What it does:
   * Alias wrapper for the decal-bucket RB-tree storage teardown lane.
   */
  std::int32_t DestroyDecalBucketTreeStorage(DecalBucketTreeStorage* const storage)
  {
    return ReleaseDecalBucketTreeStorage(storage);
  }

  /**
   * Address: 0x0077AC30 (FUN_0077AC30)
   *
   * What it does:
   * Alias wrapper for the start-tick map RB-tree storage teardown lane.
   */
  std::int32_t DestroyStartTickMapStorage(CDecalStartTickMapStorage* const storage)
  {
    return ReleaseDecalStartTickMapStorage(storage);
  }

  void DestroyBucketHead(DecalBucketNode* const head)
  {
    DecalBucketTreeStorage storage{};
    storage.allocatorCookie = nullptr;
    storage.head = head;
    storage.size = 0u;
    (void)DestroyDecalBucketTreeStorage(&storage);
  }

  void DestroyMapNodes(DecalMapNode* node, const DecalMapNode* const head)
  {
    if (!node || node == head) {
      return;
    }

    DestroyMapNodes(node->left, head);
    DestroyMapNodes(node->right, head);
    DestroyBucketHead(node->bucketHead);
    ::operator delete(node);
  }

  [[nodiscard]]
  std::uint32_t AllocateDecalObjectId(IdPool& pool)
  {
    if (pool.mReleasedLows.mWords.Empty()) {
      const std::uint32_t nextId = static_cast<std::uint32_t>(pool.mNextLowId);
      ++pool.mNextLowId;
      return nextId;
    }

    const std::uint32_t recycled = pool.mReleasedLows.GetNext(std::numeric_limits<std::uint32_t>::max());
    pool.mReleasedLows.Remove(recycled);
    return recycled;
  }

  [[nodiscard]]
  std::size_t ResolveArmyCount(CArmyImpl* const* const armiesBegin, CArmyImpl* const* const armiesEnd) noexcept
  {
    if (!armiesBegin || !armiesEnd || armiesEnd < armiesBegin) {
      return 0u;
    }
    return static_cast<std::size_t>(armiesEnd - armiesBegin);
  }

  void SetArmyVisibilityFlag(CDecalHandle& handle, const std::size_t armyIndex) noexcept
  {
    if (armyIndex < 32u) {
      handle.mArmyVisibilityFlags |= (1u << static_cast<std::uint32_t>(armyIndex));
    }
  }

  void PropagateVisibilityToObserverAllies(
    CDecalHandle& handle,
    CArmyImpl* const* const armiesBegin,
    const std::size_t armyCount,
    const std::size_t observerIndex
  )
  {
    for (std::size_t allyIndex = observerIndex; allyIndex < armyCount; ++allyIndex) {
      CArmyImpl* const allyArmy = armiesBegin[allyIndex];
      if (allyArmy && allyArmy->Allies.Contains(static_cast<std::uint32_t>(observerIndex))) {
        SetArmyVisibilityFlag(handle, allyIndex);
      }
    }
  }
} // namespace

gpg::RType* CDecalBuffer::StaticGetClass()
{
  if (!sType) {
    sType = gpg::LookupRType(typeid(CDecalBuffer));
  }
  return sType;
}

/**
 * Address: 0x00779170 (FUN_00779170)
 *
 * What it does:
 * Initializes decal runtime storage (id pool, handle list, start-tick buckets).
 */
CDecalBuffer::CDecalBuffer()
  : CDecalBuffer(nullptr)
{}

/**
    * Alias of FUN_00779170 (non-canonical helper lane).
 *
 * What it does:
 * Initializes decal runtime storage bound to a Sim owner.
 */
CDecalBuffer::CDecalBuffer(Sim* const sim)
  : mSim(sim)
  , mReserved04(0)
  , mPool()
  , mHandleListHead{}
  , mStartTickBuckets{}
  , mVisibleDecals()
  , mPendingHideObjectIds()
  , mPendingHideObjectIdsAux(0)
{
  mHandleListHead.ListResetLinks();

  mStartTickBuckets.allocatorCookie = nullptr;
  mStartTickBuckets.head = AllocateMapHeadNode();
  mStartTickBuckets.size = 0;
}

/**
 * Address: 0x00779270 (FUN_00779270)
 *
 * What it does:
 * Destroys live decal handles and releases container backing storage.
 */
CDecalBuffer::~CDecalBuffer()
{
  auto* const listHeadNode = static_cast<CDecalHandleListNode*>(&mHandleListHead);
  while (mHandleListHead.mNext != listHeadNode) {
    CDecalHandleListNode* const node = mHandleListHead.mNext;
    CDecalHandle* const handle = CDecalHandle::FromListNode(node);
    delete handle;
  }

  auto* const mapHead = static_cast<DecalMapNode*>(mStartTickBuckets.head);
  if (mapHead) {
    (void)DestroyStartTickMapStorage(&mStartTickBuckets);
  }

  mStartTickBuckets.head = nullptr;
  mStartTickBuckets.size = 0;
  mStartTickBuckets.allocatorCookie = nullptr;
  mHandleListHead.ListResetLinks();
}

/**
 * Address: 0x00779BB0 (FUN_00779BB0, Moho::CDecalBuffer::SwapVectors)
 *
 * What it does:
 * Swaps runtime storage pointers for both decal publish vectors:
 * visible decals and pending hide object-id lanes.
 */
void CDecalBuffer::SwapVectors(msvc8::vector<SDecalInfo>* const addDecals, msvc8::vector<std::uint32_t>* const removeDecals)
{
  auto& visibleView = msvc8::AsVectorRuntimeView(mVisibleDecals);
  auto& addView = msvc8::AsVectorRuntimeView(*addDecals);

  std::swap(visibleView.begin, addView.begin);
  std::swap(visibleView.end, addView.end);
  std::swap(visibleView.capacityEnd, addView.capacityEnd);

  auto& pendingHideView = msvc8::AsVectorRuntimeView(mPendingHideObjectIds);
  auto& removeView = msvc8::AsVectorRuntimeView(*removeDecals);

  std::swap(pendingHideView.begin, removeView.begin);
  std::swap(pendingHideView.end, removeView.end);
  std::swap(pendingHideView.capacityEnd, removeView.capacityEnd);
}

/**
 * Address: 0x007793D0 (FUN_007793D0, Moho::CDecalBuffer::CreateHandle)
 *
 * What it does:
 * Creates one script-visible decal handle, links it into active tracking, and
 * initializes per-army visibility flags for the new decal.
 */
CDecalHandle* CDecalBuffer::CreateHandle(const SDecalInfo& info)
{
  if (!mSim) {
    return nullptr;
  }

  const std::uint32_t objectId = AllocateDecalObjectId(mPool);

  CDecalHandle* const handle = new CDecalHandle(mSim->mLuaState, objectId, info, mSim->mCurTick);
  if (handle == nullptr) {
    return nullptr;
  }

  handle->mListNode.ListLinkBefore(&mHandleListHead);

  CArmyImpl** const armiesBegin = mSim->mArmiesList.begin();
  CArmyImpl** const armiesEnd = mSim->mArmiesList.end();
  const std::size_t armyCount = ResolveArmyCount(armiesBegin, armiesEnd);

  CArmyImpl* sourceArmy = nullptr;
  if (handle->mInfo.mArmy < armyCount) {
    sourceArmy = armiesBegin[handle->mInfo.mArmy];
  }

  if (sourceArmy != nullptr && handle->mInfo.mIsSplat != 0u) {
    const bool sourceIsCivilian = sourceArmy->IsCivilian != 0u;
    for (std::size_t i = 0; i < armyCount; ++i) {
      if (sourceArmy->Allies.Contains(static_cast<std::uint32_t>(i)) || !sourceIsCivilian) {
        SetArmyVisibilityFlag(*handle, i);
      }
    }
    return handle;
  }

  for (std::size_t i = 0; i < armyCount; ++i) {
    if (i < 32u && ((handle->mArmyVisibilityFlags & (1u << static_cast<std::uint32_t>(i))) != 0u)) {
      continue;
    }

    CArmyImpl* const observerArmy = armiesBegin[i];
    if (!observerArmy || observerArmy->IsCivilian != 0u) {
      continue;
    }

    if (sourceArmy && !IsDecalVisibleForArmy(sourceArmy, handle->mInfo, observerArmy)) {
      continue;
    }

    PropagateVisibilityToObserverAllies(*handle, armiesBegin, armyCount, i);
  }

  return handle;
}

/**
 * Address: 0x00779680 (FUN_00779680, sub_779680)
 *
 * What it does:
 * Removes one handle from active tracking, queues object-id retirement, and deletes the handle.
 */
void CDecalBuffer::DestroyHandle(CDecalHandle* const handleOpaque)
{
  if (!handleOpaque) {
    return;
  }
  if (handleOpaque->mVisibleInFocus != 0u) {
    mPendingHideObjectIds.push_back(handleOpaque->mInfo.mObj);
  }

  mPool.QueueReleasedLowId(handleOpaque->mInfo.mObj);

  delete handleOpaque;
}

/**
 * What it does:
 * Delegates one recycle-window tick to `IdPool::Update`.
 */
void CDecalBuffer::AdvanceIdPoolWindow()
{
  mPool.Update();
}

/**
 * Address: 0x00778730 (FUN_00778730, sub_778730)
 *
 * What it does:
 * Computes world-space XZ AABB bounds for a rotated decal quad.
 */
void CDecalBuffer::ProjectDecalToBoundsXZ(const SDecalInfo& info, Wm3::Vec2f& outMax, Wm3::Vec2f& outMin)
{
  const float c = std::cos(info.mRot.y);
  const float s = std::sin(info.mRot.y);

  const float xAxisX = info.mSize.x * c;
  const float xAxisZ = info.mSize.x * s;
  const float zAxisX = -(info.mSize.z * s);
  const float zAxisZ = info.mSize.z * c;

  const float minXOffset = std::min({0.0f, xAxisX, zAxisX, xAxisX + zAxisX});
  const float minZOffset = std::min({0.0f, xAxisZ, zAxisZ, xAxisZ + zAxisZ});
  const float maxXOffset = std::max({0.0f, xAxisX, zAxisX, xAxisX + zAxisX});
  const float maxZOffset = std::max({0.0f, xAxisZ, zAxisZ, xAxisZ + zAxisZ});

  outMin.x = info.mPos.x + minXOffset;
  outMin.y = info.mPos.z + minZOffset;
  outMax.x = info.mPos.x + maxXOffset;
  outMax.y = info.mPos.z + maxZOffset;
}

/**
 * Address: 0x00779040 (FUN_00779040, sub_779040)
 *
 * What it does:
 * Tests whether an observer army may currently detect a decal owned by `sourceArmy`.
 */
bool CDecalBuffer::IsDecalVisibleForArmy(
  const CArmyImpl* const sourceArmy, const SDecalInfo& info, CArmyImpl* const observerArmy
) const
{
  if (!observerArmy) {
    return false;
  }

  if (sourceArmy && observerArmy->Allies.Contains(static_cast<std::uint32_t>(sourceArmy->ArmyId))) {
    return true;
  }

  Wm3::Vec2f maxBounds{};
  Wm3::Vec2f minBounds{};
  ProjectDecalToBoundsXZ(info, maxBounds, minBounds);

  const moho::Rect2<int> queryRect{
    static_cast<int>(std::floor(minBounds.x)),
    static_cast<int>(std::floor(minBounds.y)),
    static_cast<int>(std::ceil(maxBounds.x)),
    static_cast<int>(std::ceil(maxBounds.y)),
  };

  const CAiReconDBImpl* const reconDb = observerArmy->GetReconDB();
  if (!reconDb) {
    return false;
  }

  return reconDb->ReconCanDetect(queryRect, info.mPos.y, 8) != 0;
}

/**
 * Address: 0x00779710 (FUN_00779710)
 *
 * What it does:
 * Advances decal lifetime queues and performs per-tick decal cleanup.
 */
void CDecalBuffer::CleanupTick()
{
  if (!mSim) {
    AdvanceIdPoolWindow();
    return;
  }

  const std::uint32_t curTick = mSim->mCurTick;

  // Pass 1: expire handles whose start tick has elapsed.
  const auto* const listHeadNode = static_cast<const CDecalHandleListNode*>(&mHandleListHead);
  for (CDecalHandleListNode* node = mHandleListHead.mNext; node != listHeadNode;) {
    CDecalHandleListNode* const next = node->mNext;
    CDecalHandle* const handle = CDecalHandle::FromListNode(node);
    if (handle->mInfo.mStartTick != 0u && handle->mInfo.mStartTick <= curTick) {
      handle->mInfo.mStartTick = 0u;
      DestroyHandle(handle);
    }
    node = next;
  }

  CArmyImpl** const armiesBegin = mSim->mArmiesList.begin();
  CArmyImpl** const armiesEnd = mSim->mArmiesList.end();
  const std::size_t armyCount = armiesBegin ? static_cast<std::size_t>(armiesEnd - armiesBegin) : 0u;

  if (armyCount != 0u) {
    const std::uint32_t rotatingArmyIndex = curTick % static_cast<std::uint32_t>(armyCount);
    CArmyImpl* const rotatingArmy = armiesBegin[rotatingArmyIndex];

    if (rotatingArmy && rotatingArmy->IsCivilian == 0u) {
      const std::int32_t focusArmy = mSim->mSyncFilter.focusArmy;
      const std::uint32_t rotatingArmyMask = rotatingArmyIndex < 32u ? (1u << rotatingArmyIndex) : 0u;

      for (CDecalHandleListNode* node = mHandleListHead.mNext; node != listHeadNode; node = node->mNext) {
        CDecalHandle* const handle = CDecalHandle::FromListNode(node);

        if (rotatingArmyMask != 0u && (handle->mArmyVisibilityFlags & rotatingArmyMask) == 0u) {
          const bool bypassRecon = handle->mInfo.mIsSplat == 0u;
          const bool graceWindow = handle->mInfo.mStartTick != 0u && (handle->mCreatedAtTick + 10u > curTick);

          if (bypassRecon || graceWindow) {
            CArmyImpl* sourceArmy = nullptr;
            if (handle->mInfo.mArmy < armyCount) {
              sourceArmy = armiesBegin[handle->mInfo.mArmy];
            }

            if (!sourceArmy || IsDecalVisibleForArmy(sourceArmy, handle->mInfo, rotatingArmy)) {
              for (std::size_t i = 0; i < armyCount; ++i) {
                CArmyImpl* const army = armiesBegin[i];
                if (!army) {
                  continue;
                }

                if (army->Allies.Contains(rotatingArmyIndex)) {
                  const std::uint32_t armyIndex = static_cast<std::uint32_t>(army->ArmyId);
                  if (armyIndex < 32u) {
                    handle->mArmyVisibilityFlags |= (1u << armyIndex);
                  }
                }
              }
            }
          }
        }

        bool shouldBeVisible = focusArmy == -1;
        if (!shouldBeVisible && focusArmy >= 0 && focusArmy < 32) {
          shouldBeVisible = (handle->mArmyVisibilityFlags & (1u << focusArmy)) != 0u;
        }

        if (shouldBeVisible) {
          if (handle->mVisibleInFocus == 0u) {
            AppendVisibleDecal(mVisibleDecals, handle->mInfo);
            handle->mVisibleInFocus = 1u;
          }
        } else if (handle->mVisibleInFocus != 0u) {
          mPendingHideObjectIds.push_back(handle->mInfo.mObj);
          handle->mVisibleInFocus = 0u;
        }
      }
    }
  }

  AdvanceIdPoolWindow();
}
