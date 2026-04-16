#include "moho/sim/IdPool.h"

#include "gpg/core/time/Timer.h"
#include "lua/LuaObject.h"
#include "legacy/containers/String.h"
#include "moho/render/camera/GeomCamera3.h"
#include "moho/sim/ArmyUnitSet.h"
#include "moho/sim/SSTIArmyConstantData.h"
#include "moho/sim/SSTIArmyVariableData.h"
#include "moho/sim/SimDriver.h"
#include "moho/render/CDecalTypes.h"
#include "moho/unit/core/Unit.h"

#include <Windows.h>

#include <algorithm>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <exception>
#include <limits>
#include <new>
#include <memory>
#include <string>
#include <type_traits>
#include <xmmintrin.h>

namespace
{
  constexpr std::size_t kIdPoolHistoryCapacity = 100u;

  struct Element12Runtime
  {
    std::uint32_t lane0;
    std::uint32_t lane1;
    std::uint32_t lane2;
  };
  static_assert(sizeof(Element12Runtime) == 0x0C, "Element12Runtime size must be 0x0C");

  struct Element8Runtime
  {
    std::uint32_t lane0;
    std::uint32_t lane1;
  };
  static_assert(sizeof(Element8Runtime) == 0x08, "Element8Runtime size must be 0x08");

  struct Float7Runtime
  {
    float lanes[7];
  };
  static_assert(sizeof(Float7Runtime) == 0x1C, "Float7Runtime size must be 0x1C");

  template <typename T>
  struct LegacyVectorStorageRuntime
  {
    std::uint32_t allocatorCookie;
    T* begin;
    T* end;
    T* capacity;
  };

  template <typename T>
  [[nodiscard]] std::size_t VectorSize(const LegacyVectorStorageRuntime<T>& vector) noexcept
  {
    if (vector.begin == nullptr || vector.end == nullptr || vector.end < vector.begin) {
      return 0u;
    }

    return static_cast<std::size_t>(vector.end - vector.begin);
  }

  template <typename T>
  [[nodiscard]] std::size_t VectorCapacity(const LegacyVectorStorageRuntime<T>& vector) noexcept
  {
    if (vector.begin == nullptr || vector.capacity == nullptr || vector.capacity < vector.begin) {
      return 0u;
    }

    return static_cast<std::size_t>(vector.capacity - vector.begin);
  }

  template <typename T>
  [[nodiscard]] bool ReserveTrivialVector(
    LegacyVectorStorageRuntime<T>* const vector,
    const std::size_t desiredCapacity
  )
  {
    static_assert(std::is_trivially_copyable_v<T>, "ReserveTrivialVector requires trivially copyable element types");

    if (vector == nullptr) {
      return false;
    }

    const std::size_t currentCapacity = VectorCapacity(*vector);
    if (desiredCapacity <= currentCapacity) {
      return true;
    }

    T* const newStorage = static_cast<T*>(::operator new(sizeof(T) * desiredCapacity, std::nothrow));
    if (newStorage == nullptr) {
      return false;
    }

    const std::size_t currentSize = VectorSize(*vector);
    if (vector->begin != nullptr && currentSize != 0u) {
      std::memcpy(newStorage, vector->begin, currentSize * sizeof(T));
    }

    ::operator delete(vector->begin);
    vector->begin = newStorage;
    vector->end = newStorage + currentSize;
    vector->capacity = newStorage + desiredCapacity;
    return true;
  }

  template <typename T>
  [[nodiscard]] T* AppendTrivialValue(
    LegacyVectorStorageRuntime<T>* const vector,
    const T& value
  )
  {
    static_assert(std::is_trivially_copyable_v<T>, "AppendTrivialValue requires trivially copyable element types");

    if (vector == nullptr) {
      return nullptr;
    }

    const std::size_t previousSize = VectorSize(*vector);
    if (!ReserveTrivialVector(vector, previousSize + 1u)) {
      return nullptr;
    }

    T* const inserted = vector->begin + previousSize;
    *inserted = value;
    vector->end = inserted + 1;
    return inserted;
  }

  template <typename T>
  [[nodiscard]] T* ResizeTrivialVectorWithFill(
    LegacyVectorStorageRuntime<T>* const vector,
    const std::size_t desiredSize,
    const T& fillValue
  )
  {
    static_assert(std::is_trivially_copyable_v<T>, "ResizeTrivialVectorWithFill requires trivially copyable element types");

    if (vector == nullptr) {
      return nullptr;
    }

    const std::size_t previousSize = VectorSize(*vector);
    if (desiredSize > previousSize) {
      if (!ReserveTrivialVector(vector, desiredSize)) {
        return vector->begin;
      }

      for (std::size_t index = previousSize; index < desiredSize; ++index) {
        vector->begin[index] = fillValue;
      }
    }

    if (vector->begin != nullptr) {
      vector->end = vector->begin + desiredSize;
    } else {
      vector->end = nullptr;
    }
    return vector->begin;
  }

  struct OwnedBufferRuntime
  {
    std::uint32_t allocatorCookie;
    std::byte* storage;
    std::uint32_t logicalState;
  };

  [[nodiscard]] std::int32_t ResetOwnedBufferRuntime(OwnedBufferRuntime* const owner) noexcept
  {
    if (owner == nullptr) {
      return 0;
    }

    ::operator delete(owner->storage);
    owner->storage = nullptr;
    owner->logicalState = 0u;
    return 0;
  }

  struct LookupCacheRuntime
  {
    bool (*containsFn)(void* state, std::uint32_t key);
    std::uint32_t (*resolveFn)(std::uint32_t context, std::uint32_t key, std::uint32_t argument);
    void* containsState;
    std::uint32_t context;
    std::uint32_t cachedValue;
  };

  struct MapInsertStatusRuntime
  {
    void* node;
    std::uint8_t inserted;
    std::uint8_t reserved[3];
  };

#if INTPTR_MAX == INT32_MAX
  static_assert(offsetof(MapInsertStatusRuntime, inserted) == 0x04, "MapInsertStatusRuntime::inserted offset must be 0x04");
  static_assert(sizeof(MapInsertStatusRuntime) == 0x08, "MapInsertStatusRuntime size must be 0x08");
#endif

#pragma pack(push, 1)
  struct MapNodeNil21Runtime
  {
    MapNodeNil21Runtime* left;
    MapNodeNil21Runtime* parent;
    MapNodeNil21Runtime* right;
    std::uint32_t key;
    std::uint8_t pad10[0x05];
    std::uint8_t isNil;
  };

  struct MapNodeNil61Runtime
  {
    MapNodeNil61Runtime* left;
    MapNodeNil61Runtime* parent;
    MapNodeNil61Runtime* right;
    std::uint32_t key;
    std::uint8_t pad10[0x2D];
    std::uint8_t isNil;
  };
#pragma pack(pop)

#if INTPTR_MAX == INT32_MAX
  static_assert(offsetof(MapNodeNil21Runtime, key) == 0x0C, "MapNodeNil21Runtime::key offset must be 0x0C");
  static_assert(offsetof(MapNodeNil21Runtime, isNil) == 0x15, "MapNodeNil21Runtime::isNil offset must be 0x15");
  static_assert(offsetof(MapNodeNil61Runtime, key) == 0x0C, "MapNodeNil61Runtime::key offset must be 0x0C");
  static_assert(offsetof(MapNodeNil61Runtime, isNil) == 0x3D, "MapNodeNil61Runtime::isNil offset must be 0x3D");
#endif

  template <typename NodeT>
  struct LegacyMapStorageRuntime
  {
    void* allocatorCookie;
    NodeT* head;
    std::uint32_t size;
  };

  template <typename NodeT>
  [[nodiscard]] NodeT* EnsureMapHead(LegacyMapStorageRuntime<NodeT>* const map)
  {
    if (map == nullptr) {
      return nullptr;
    }

    if (map->head != nullptr) {
      return map->head;
    }

    NodeT* const head = static_cast<NodeT*>(::operator new(sizeof(NodeT), std::nothrow));
    if (head == nullptr) {
      return nullptr;
    }

    std::memset(head, 0, sizeof(NodeT));
    head->left = head;
    head->parent = head;
    head->right = head;
    head->isNil = 1u;

    map->head = head;
    map->size = 0u;
    return head;
  }

  template <typename NodeT>
  [[nodiscard]] NodeT* InsertMapNode(
    LegacyMapStorageRuntime<NodeT>* const map,
    NodeT* const parent,
    const bool insertLeft,
    const std::uint32_t key
  )
  {
    if (map == nullptr) {
      return nullptr;
    }

    NodeT* const head = EnsureMapHead(map);
    if (head == nullptr) {
      return nullptr;
    }

    NodeT* const inserted = static_cast<NodeT*>(::operator new(sizeof(NodeT), std::nothrow));
    if (inserted == nullptr) {
      return nullptr;
    }

    std::memset(inserted, 0, sizeof(NodeT));
    inserted->left = head;
    inserted->right = head;
    inserted->parent = parent != nullptr ? parent : head;
    inserted->key = key;
    inserted->isNil = 0u;

    if (parent == nullptr || parent == head || parent->isNil != 0u) {
      head->parent = inserted;
      head->left = inserted;
      head->right = inserted;
    } else if (insertLeft) {
      parent->left = inserted;
      if (head->left == parent || head->left == head) {
        head->left = inserted;
      }
    } else {
      parent->right = inserted;
      if (head->right == parent || head->right == head) {
        head->right = inserted;
      }
    }

    ++map->size;
    return inserted;
  }

  template <typename NodeT>
  [[nodiscard]] MapInsertStatusRuntime* FindOrInsertMapNodeByKey(
    LegacyMapStorageRuntime<NodeT>* const map,
    const std::uint32_t* const key,
    MapInsertStatusRuntime* const outResult
  )
  {
    if (outResult == nullptr) {
      return nullptr;
    }

    outResult->node = nullptr;
    outResult->inserted = 0u;
    outResult->reserved[0] = 0u;
    outResult->reserved[1] = 0u;
    outResult->reserved[2] = 0u;

    if (map == nullptr || key == nullptr) {
      return outResult;
    }

    NodeT* const head = EnsureMapHead(map);
    if (head == nullptr) {
      return outResult;
    }

    NodeT* parent = head;
    NodeT* cursor = head->parent;
    bool goLeft = true;

    while (cursor != nullptr && cursor != head && cursor->isNil == 0u) {
      parent = cursor;
      if (*key < cursor->key) {
        goLeft = true;
        cursor = cursor->left;
      } else if (cursor->key < *key) {
        goLeft = false;
        cursor = cursor->right;
      } else {
        outResult->node = cursor;
        return outResult;
      }
    }

    NodeT* const inserted = InsertMapNode(map, parent, goLeft, *key);
    outResult->node = inserted;
    outResult->inserted = inserted != nullptr ? 1u : 0u;
    return outResult;
  }

  [[nodiscard]] const moho::BVIntSet& AsIdPoolSnapshot(const moho::SimSubRes3& slot) noexcept
  {
    return *reinterpret_cast<const moho::BVIntSet*>(&slot);
  }

  struct TimerAccumulatorRuntime
  {
    std::uintptr_t counterOwner;
    gpg::time::Timer elapsedTimer;
  };

  struct SSTIUnitVariableDataSlotRuntime
  {
    std::uint32_t mHeadWord0;
    std::uint32_t mHeadWord1;
    moho::SSTIUnitVariableData mVariableData;
    std::uint32_t mTailWord0;
    std::uint32_t mTailWord1;
  };
  static_assert(
    offsetof(SSTIUnitVariableDataSlotRuntime, mVariableData) == 0x08,
    "SSTIUnitVariableDataSlotRuntime::mVariableData offset must be 0x08"
  );
  static_assert(
    offsetof(SSTIUnitVariableDataSlotRuntime, mTailWord0) == 0x230,
    "SSTIUnitVariableDataSlotRuntime::mTailWord0 offset must be 0x230"
  );
  static_assert(sizeof(SSTIUnitVariableDataSlotRuntime) == 0x238, "SSTIUnitVariableDataSlotRuntime size must be 0x238");

  struct CameraCopyContextRuntime
  {
    std::uint32_t lane0;
    std::uint32_t lane4;
    moho::GeomCamera3* destinationEnd;
  };

  struct OpaqueLaneRebuildRuntime
  {
    std::uint32_t lane0;
    std::uint32_t lane4;
    std::byte* storage;
  };

  [[nodiscard]] std::byte* RebuildOpaqueLaneStorage(
    std::byte* const previousStorage,
    const std::size_t requestedBytes,
    const bool zeroFill
  )
  {
    std::byte* replacement = nullptr;
    if (requestedBytes != 0u) {
      replacement = static_cast<std::byte*>(::operator new(requestedBytes, std::nothrow));
      if (replacement != nullptr && zeroFill) {
        std::memset(replacement, 0, requestedBytes);
      }
    }

    ::operator delete(previousStorage);
    return replacement;
  }

  struct CacheWordVectorRuntime
  {
    std::uint32_t lane0;
    std::uint32_t* begin;
    std::uint32_t* end;
    std::uint32_t lane3;
    std::uint32_t lane4;
    std::int32_t stagedBeginIndex;
    std::int32_t stagedEndIndex;
    std::uint32_t lane7;
    std::int32_t cachedIndex;
  };

  template <typename T>
  struct FourLanePagedRuntime
  {
    std::uint32_t reserved0;
    T** pages;
    std::uint32_t pageCount;
    std::uint32_t baseIndex;
    std::uint32_t size;
  };

  template <typename T>
  struct RangeOwnerRuntime
  {
    std::uint32_t reserved0;
    T* begin;
    T* end;
    std::uint32_t reserved12;
  };

  struct StringRangeBlock16Runtime
  {
    std::uint32_t reserved0;
    msvc8::string* begin;
    msvc8::string* end;
    std::uint32_t reserved12;
  };

  struct SharedControlLane12Runtime
  {
    std::uint32_t reserved0;
    std::uint32_t reserved4;
    volatile long* control;
  };

  static_assert(sizeof(StringRangeBlock16Runtime) == 0x10, "StringRangeBlock16Runtime size must be 0x10");
  static_assert(sizeof(SharedControlLane12Runtime) == 0x0C, "SharedControlLane12Runtime size must be 0x0C");

  template <typename T>
  [[nodiscard]] T** GrowPagedArray(T** const pages, const std::uint32_t currentPageCount, const std::uint32_t desiredPageCount)
  {
    if (desiredPageCount <= currentPageCount) {
      return pages;
    }

    auto* const newPages = static_cast<T**>(::operator new(sizeof(T*) * desiredPageCount, std::nothrow));
    if (newPages == nullptr) {
      return pages;
    }

    for (std::uint32_t i = 0u; i < desiredPageCount; ++i) {
      newPages[i] = nullptr;
    }

    for (std::uint32_t i = 0u; i < currentPageCount; ++i) {
      newPages[i] = pages != nullptr ? pages[i] : nullptr;
    }

    ::operator delete(static_cast<void*>(pages));
    return newPages;
  }

  template <typename T>
  [[nodiscard]] T** EnsurePagedFourLanePage(
    FourLanePagedRuntime<T>* const runtime,
    const std::uint32_t logicalIndex
  )
  {
    if (runtime == nullptr) {
      return nullptr;
    }

    const std::uint32_t pageIndex = logicalIndex >> 2u;
    if (runtime->pages == nullptr || pageIndex >= runtime->pageCount) {
      const std::uint32_t desiredPageCount = std::max(runtime->pageCount == 0u ? 8u : runtime->pageCount * 2u, pageIndex + 1u);
      runtime->pages = GrowPagedArray(runtime->pages, runtime->pageCount, desiredPageCount);
      if (runtime->pages == nullptr) {
        return nullptr;
      }
      runtime->pageCount = desiredPageCount;
    }

    if (runtime->pages[pageIndex] == nullptr) {
      runtime->pages[pageIndex] = static_cast<T*>(::operator new(sizeof(T) * 4u, std::nothrow));
      if (runtime->pages[pageIndex] == nullptr) {
        return nullptr;
      }
    }

    return &runtime->pages[pageIndex];
  }

  template <typename T>
  void DestroyRangeAndRelease(T* begin, T* end)
  {
    if (begin == nullptr) {
      return;
    }

    for (T* cursor = begin; cursor != end; ++cursor) {
      std::destroy_at(cursor);
    }
  }

  template <typename T>
  void DestroyPagedFourLaneRange(
    FourLanePagedRuntime<T>* const runtime,
    std::uint32_t beginIndex,
    const std::uint32_t endIndex
  )
  {
    if (runtime == nullptr || runtime->pages == nullptr) {
      return;
    }

    while (beginIndex != endIndex) {
      const std::uint32_t pageIndex = beginIndex >> 2u;
      const std::uint32_t laneIndex = beginIndex & 3u;
      if (pageIndex < runtime->pageCount && runtime->pages[pageIndex] != nullptr) {
        T& entry = runtime->pages[pageIndex][laneIndex];
        if constexpr (std::is_pointer_v<T>) {
          if (entry != nullptr) {
            delete entry;
          }
        } else {
          std::destroy_at(&entry);
        }
      }
      ++beginIndex;
    }
  }

  template <typename T>
  [[nodiscard]] T* AllocateZeroedRuntimeNode() noexcept
  {
    auto* const node = static_cast<T*>(::operator new(sizeof(T), std::nothrow));
    if (node != nullptr) {
      std::memset(node, 0, sizeof(T));
    }
    return node;
  }

#pragma pack(push, 1)
  struct RbNodeFlag45Runtime
  {
    RbNodeFlag45Runtime* left;
    RbNodeFlag45Runtime* parent;
    RbNodeFlag45Runtime* right;
    std::uint32_t lane0C;
    std::uint32_t storageWord0;
    std::uint32_t storageWord1;
    std::uint32_t storageWord2;
    std::uint32_t storageWord3;
    std::uint32_t stringSize;
    std::uint32_t stringCapacity;
    std::uint32_t lane28;
    std::uint8_t sentinel44;
    std::uint8_t isNil45;
    std::uint8_t pad46[2];
  };

  struct RbNodeFlag21Runtime
  {
    RbNodeFlag21Runtime* left;
    RbNodeFlag21Runtime* parent;
    RbNodeFlag21Runtime* right;
    std::uint32_t lane0C;
    std::uint32_t lane10;
    std::uint8_t sentinel20;
    std::uint8_t isNil21;
    std::uint8_t pad22[2];
  };

  struct RbNodeFlag17Runtime
  {
    RbNodeFlag17Runtime* left;
    RbNodeFlag17Runtime* parent;
    RbNodeFlag17Runtime* right;
    std::uint32_t lane0C;
    std::uint8_t sentinel16;
    std::uint8_t isNil17;
    std::uint8_t pad18[2];
  };

  struct RbNodeFlag29Runtime
  {
    RbNodeFlag29Runtime* left;
    RbNodeFlag29Runtime* parent;
    RbNodeFlag29Runtime* right;
    std::uint32_t lane0C;
    std::uint32_t lane10;
    std::uint32_t lane14;
    std::uint32_t lane18;
    std::uint8_t sentinel28;
    std::uint8_t isNil29;
    std::uint8_t pad2A[2];
  };

  struct LinkedTreeNode37Runtime
  {
    LinkedTreeNode37Runtime* left;
    LinkedTreeNode37Runtime* parent;
    LinkedTreeNode37Runtime* right;
    std::uint32_t lane0C;
    std::uint32_t lane10;
    std::uint32_t lane14;
    std::uint32_t lane18;
    std::uint32_t lane1C;
    std::uint32_t lane20;
    std::uint32_t lane24;
  };

  struct RbNodeFlag65Runtime
  {
    RbNodeFlag65Runtime* left;
    RbNodeFlag65Runtime* parent;
    RbNodeFlag65Runtime* right;
    std::byte payload[53];
    std::uint8_t isNil65;
  };
#pragma pack(pop)

#if INTPTR_MAX == INT32_MAX
  static_assert(offsetof(RbNodeFlag45Runtime, sentinel44) == 0x2C, "RbNodeFlag45Runtime::sentinel44 offset must be 0x2C");
  static_assert(offsetof(RbNodeFlag45Runtime, isNil45) == 0x2D, "RbNodeFlag45Runtime::isNil45 offset must be 0x2D");
  static_assert(offsetof(RbNodeFlag21Runtime, sentinel20) == 0x14, "RbNodeFlag21Runtime::sentinel20 offset must be 0x14");
  static_assert(offsetof(RbNodeFlag21Runtime, isNil21) == 0x15, "RbNodeFlag21Runtime::isNil21 offset must be 0x15");
  static_assert(offsetof(RbNodeFlag17Runtime, sentinel16) == 0x10, "RbNodeFlag17Runtime::sentinel16 offset must be 0x10");
  static_assert(offsetof(RbNodeFlag17Runtime, isNil17) == 0x11, "RbNodeFlag17Runtime::isNil17 offset must be 0x11");
  static_assert(offsetof(RbNodeFlag29Runtime, sentinel28) == 0x1C, "RbNodeFlag29Runtime::sentinel28 offset must be 0x1C");
  static_assert(offsetof(RbNodeFlag29Runtime, isNil29) == 0x1D, "RbNodeFlag29Runtime::isNil29 offset must be 0x1D");
  static_assert(offsetof(RbNodeFlag65Runtime, isNil65) == 0x41, "RbNodeFlag65Runtime::isNil65 offset must be 0x41");
  static_assert(offsetof(LinkedTreeNode37Runtime, lane24) == 0x24, "LinkedTreeNode37Runtime::lane24 offset must be 0x24");
#endif

  struct LinearTreeNodeRuntime
  {
    LinearTreeNodeRuntime* next;
    LinearTreeNodeRuntime* prev;
  };

  struct LinearTreeStorageRuntime
  {
    std::uint32_t lane0;
    LinearTreeNodeRuntime* head;
    std::uint32_t size;
  };

  struct SwapBackedArrayRuntimeA
  {
    std::uint32_t lane0;
    std::uint32_t lane4;
    std::uint32_t* activeBuffer;  // +0x08
    std::uint32_t* cursor;        // +0x0C
    std::uint32_t cachedFirst;    // +0x10
    std::uint32_t* fallbackBuffer; // +0x14
  };

  struct SwapBackedArrayRuntimeB
  {
    std::uint32_t lane0;
    std::uint32_t lane4;
    std::uint32_t lane8;
    std::uint32_t laneC;
    std::uint32_t* activeBuffer;   // +0x10
    std::uint32_t* cursor;         // +0x14
    std::uint32_t cachedFirst;     // +0x18
    std::uint32_t* fallbackBuffer; // +0x1C
  };

  struct LinkedBufferOwnerRuntime
  {
    LinkedBufferOwnerRuntime* next; // +0x00
    LinkedBufferOwnerRuntime* prev; // +0x04
    std::uint32_t* activeBuffer;    // +0x08
    std::uint32_t* cursor;          // +0x0C
    std::uint32_t cachedFirst;      // +0x10
    std::uint32_t* fallbackBuffer;  // +0x14
  };

  struct TripleIntNodeRuntime
  {
    std::int32_t lane0;
    std::int32_t lane4;
    std::int32_t lane8;
  };

  struct StringFloatMapNodeRuntime
  {
    StringFloatMapNodeRuntime* left;
    StringFloatMapNodeRuntime* parent;
    StringFloatMapNodeRuntime* right;
    std::uint8_t color;
    std::uint8_t isNil;
    std::uint8_t pad0E[2];
    std::string key;
    float value;
  };

  struct StringFloatMapRuntime
  {
    StringFloatMapNodeRuntime* head;
  };

  struct RbMapFlag65Runtime
  {
    std::uint32_t allocatorCookie;
    RbNodeFlag65Runtime* head;
    std::uint32_t size;
  };

  struct RbMapFlag21Runtime
  {
    std::uint32_t allocatorCookie;
    RbNodeFlag21Runtime* head;
    std::uint32_t size;
  };

  struct Float4Runtime
  {
    float lanes[4];
  };

  using Float4FinalizeFn = std::int32_t (*)(
    Float4Runtime* heapBase,
    std::int32_t arg4,
    std::int32_t arg5,
    std::int32_t arg6,
    std::int32_t arg7
  );

  using RangeEraseRuntimeFn = std::int32_t (*)(void* owner, void* begin, void* rangeBegin, void* rangeEnd);
  using ForwardCleanupFn = void (*)(void* owner);
  using TaggedInsertRuntimeFn = std::int32_t (*)(std::uint32_t* begin, std::uint32_t tag, const std::uint32_t* value);
  using LaneConstructFn52 = void (*)(void* destination, const void* sourceContext);
  using CloneTree65Fn = RbNodeFlag65Runtime* (*)(RbNodeFlag65Runtime* sourceRoot, RbNodeFlag65Runtime* destinationHead);
  using CloneTree21Fn = RbNodeFlag21Runtime* (*)(RbNodeFlag21Runtime* sourceRoot, RbNodeFlag21Runtime* destinationHead);

  [[nodiscard]] bool NodeHasSentinelFlag(const void* const node, const std::size_t flagOffset) noexcept
  {
    if (node == nullptr) {
      return true;
    }

    const auto* const bytes = static_cast<const std::uint8_t*>(node);
    return bytes[flagOffset] != 0u;
  }

  template <typename NodeT>
  void RecomputeHeadExtrema(NodeT* const head, const std::size_t nilFlagOffset)
  {
    if (head == nullptr) {
      return;
    }

    NodeT* const root = head->parent;
    if (root == nullptr || root == head || NodeHasSentinelFlag(root, nilFlagOffset)) {
      head->left = head;
      head->right = head;
      return;
    }

    NodeT* leftmost = root;
    while (leftmost->left != nullptr && !NodeHasSentinelFlag(leftmost->left, nilFlagOffset)) {
      leftmost = leftmost->left;
    }

    NodeT* rightmost = root;
    while (rightmost->right != nullptr && !NodeHasSentinelFlag(rightmost->right, nilFlagOffset)) {
      rightmost = rightmost->right;
    }

    head->left = leftmost;
    head->right = rightmost;
  }

  void DestroyRecursiveStringTree(RbNodeFlag45Runtime* node)
  {
    RbNodeFlag45Runtime* previous = node;
    RbNodeFlag45Runtime* cursor = node;
    while (cursor != nullptr && cursor->isNil45 == 0u) {
      DestroyRecursiveStringTree(cursor->right);
      cursor = cursor->left;

      if (previous->stringCapacity >= 16u) {
        const auto storageAddress = static_cast<std::uintptr_t>(previous->storageWord0);
        ::operator delete(reinterpret_cast<void*>(storageAddress));
      }

      previous->stringCapacity = 15u;
      previous->stringSize = 0u;
      previous->storageWord0 &= 0xFFFFFF00u;
      ::operator delete(previous);
      previous = cursor;
    }
  }

  void PatchBackReferenceChain(
    const std::uint32_t startWord,
    const std::uint32_t* const targetFieldAddress,
    const std::uint32_t replacementWord
  )
  {
    std::uint32_t* cursor = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(startWord));
    std::uint32_t guard = 0u;
    while (cursor != nullptr && guard < 0x100000u) {
      const auto* const pointedWord = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(*cursor));
      if (pointedWord == targetFieldAddress) {
        *cursor = replacementWord;
        return;
      }

      cursor = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(*cursor + 4u));
      ++guard;
    }
  }

  struct RangeOwnerByteRuntime
  {
    std::uint32_t lane0;
    std::uint32_t lane4;
    std::uint32_t lane8;
    std::byte* rangeBegin;
    std::uint32_t rangeByteSize;
  };

  struct TaggedInsertCursorRuntime
  {
    std::uint32_t lane0;
    std::uint32_t* begin;
    std::uint32_t* end;
  };

  struct FloatPayloadNodeRuntime
  {
    std::int32_t lane0;
    std::int32_t lane4;
    float lanes[7];
  };

  struct RbNodeFlag25Runtime
  {
    RbNodeFlag25Runtime* left;
    RbNodeFlag25Runtime* parent;
    RbNodeFlag25Runtime* right;
    std::byte payload[13];
    std::uint8_t isNil25;
  };

  struct WxArrayStringLaneRuntime
  {
    std::uint32_t lane0;
    std::uint32_t count;
    const wchar_t** entries;
  };

  struct WxLookupOwnerRuntime
  {
    std::byte pad00[36];
    WxArrayStringLaneRuntime* arrayLane;
  };

  struct DispatchWindowRuntime
  {
    void** vtable;
    std::byte payload[312];
    WNDPROC previousWindowProc;
  };

  struct DistanceVector2fRuntime
  {
    void* vtable;
    std::int32_t dimension;
    float epsilon;
    float initialDistance;
    float pad10;
    float pad14;
    float pad18;
    float pad1C;
    std::uint8_t hasRawResult;
    std::uint8_t hasFinalResult;
    std::uint8_t pad22[2];
    float minClamp;
    float maxClamp;
  };

  struct DistanceVector2dRuntime
  {
    void* vtable;
    std::int32_t dimension;
    std::byte pad0C[4];
    double epsilon;
    double initialDistance;
    double minClamp;
    double maxClamp;
    std::byte pad34[0x10];
    std::uint8_t hasRawResult;
    std::uint8_t hasFinalResult;
    std::uint8_t pad42[6];
  };

  struct BasisPointerResetRuntimeF
  {
    std::byte pad00[20];
    float** basisPair;
    float* outputPrimary;
    float* outputSecondary;
    std::uint8_t wasReset;
  };

  struct BasisPointerResetRuntimeD
  {
    std::byte pad00[20];
    double** basisPair;
    double* outputPrimary;
    double* outputSecondary;
    std::uint8_t wasReset;
  };

  struct IntArrayLookupRuntime
  {
    std::int32_t lane0;
    std::uint32_t count;
    std::int32_t* values;
  };

  struct VirtualDispatch44Runtime
  {
    void** vtable;
  };

  struct LinkPatchRuntime
  {
    std::byte pad00[8];
    std::uint32_t lane08;
    std::uint32_t lane0C;
    std::uint32_t lane10;
    std::uint32_t lane14;
  };

#pragma pack(push, 1)
  struct RefCountedPayload49Runtime
  {
    std::uint32_t lane00;
    std::uint32_t lane04;
    float lane08;
    float lane0C;
    float lane10;
    std::uint32_t lane14;
    std::uint32_t ref18;
    std::uint32_t lane1C;
    std::uint32_t ref20;
    std::uint32_t lane24;
    std::uint32_t ref28;
    std::uint8_t tail2C;
    std::uint8_t tail2D;
    std::uint8_t tail2E;
    std::uint8_t tail2F;
    std::uint8_t tail30;
  };
#pragma pack(pop)

  struct WxObjectRuntime
  {
    void* vtable;
  };

  struct WxFontDescriptorRuntime
  {
    std::uint32_t lane00;
    std::uint32_t lane04;
    std::int32_t pointSize;
    std::int32_t family;
    std::int32_t style;
    std::int32_t weight;
    std::uint8_t underlined;
    std::uint8_t pad19[3];
    std::byte faceNameStorage[8];
    std::int32_t encoding;
    std::int32_t lane24;
    std::byte pad28[0x58];
    std::uint8_t lane84;
  };

  struct WxSharedStringOwnerRuntime
  {
    std::uint32_t lane00;
    std::uint32_t stringLane04;
    std::uint32_t stringLane08;
    std::uint32_t stringLane0C;
    std::byte pad10[16];
    std::uint32_t stringLane20;
  };

  struct Div10PairOwnerRuntime
  {
    std::byte pad00[0x0C];
    std::int32_t lane0C;
    std::int32_t lane10;
    std::byte pad14[0x58];
    std::uint32_t sourceLane6C;
  };

  struct VtableOnlyRuntime
  {
    void* vtable;
  };

  struct WxSocketOutputStreamRuntime
  {
    void* vtable;
    std::byte pad04[8];
    std::int32_t socketHandle;
  };

  struct WxSocketInputStreamRuntime
  {
    void* vtable;
    std::byte pad04[0x14];
    std::int32_t socketHandle;
  };

  struct CartographicDecalNodeRuntime
  {
    CartographicDecalNodeRuntime* next;
    CartographicDecalNodeRuntime* prev;
    void* vtable;
  };

  struct CartographicDecalListRuntime
  {
    std::uint32_t lane00;
    CartographicDecalNodeRuntime* sentinel;
    std::uint32_t size;
  };

  struct SmallStringSboRuntime
  {
    union
    {
      char inlineStorage[16];
      char* heapStorage;
    };
    std::uint32_t size;
    std::uint32_t capacity;
  };

  struct WaveParametersRuntime
  {
    void* vtable;
    std::uint32_t lane04;
    SmallStringSboRuntime lane08;
    std::uint32_t pad20;
    SmallStringSboRuntime lane24;
  };

  struct WindowTextMetricOwnerRuntime
  {
    std::byte pad00[264];
    HWND windowHandle;
  };

  struct RegionNodeRuntime
  {
    std::byte pad00[8];
    HRGN regionHandle;
  };

  struct RegionOwnerRuntime
  {
    std::uint32_t lane00;
    RegionNodeRuntime* node;
  };

  struct EmitterCurveKeyRuntime
  {
    void* vtable;
    float lane04;
    float lane08;
    float lane0C;
  };

  struct TimeSplitRuntime
  {
    std::byte pad00[0x24];
    std::uint32_t seconds;
    std::uint32_t microseconds;
  };

  struct BuildQueueSnapshotRuntime
  {
    std::uint32_t lane00;
    const std::byte* begin;
    const std::byte* end;
  };

  struct BuildQueueRangeRuntime
  {
    const std::byte* start;
    const std::byte* end;
  };

  struct BuildQueueCompareStateRuntime
  {
    std::uint8_t lane00;
    std::uint8_t pad01[3];
  };

  struct BuildQueueCompareResultRuntime
  {
    const std::byte* cursor;
  };

  struct OccupySourceBindingRuntime
  {
    void* vtable;
    std::uint32_t lane04;
    std::uint32_t lane08;
  };

  struct ClutterSeedRuntime
  {
    void* vtable;
    float lane04;
    float lane08;
    std::uint32_t lane0C;
  };

  struct WideIoStreamOffsetsRuntime
  {
    std::ptrdiff_t wiosOffset;
    std::ptrdiff_t iosbOffset;
    std::ptrdiff_t wistreamOffset;
  };

  struct TreeStorageOwnerRuntime
  {
    std::uint32_t lane00;
    void* treeStorage;
    std::uint32_t size;
  };

#pragma pack(push, 1)
  struct MapNodeNil17Runtime
  {
    MapNodeNil17Runtime* left;
    MapNodeNil17Runtime* parent;
    MapNodeNil17Runtime* right;
    std::uint32_t key;
    std::uint8_t pad10;
    std::uint8_t isNil;
  };

  struct MapNodeNil29Runtime
  {
    MapNodeNil29Runtime* left;
    MapNodeNil29Runtime* parent;
    MapNodeNil29Runtime* right;
    std::uint32_t key;
    std::uint8_t pad10[0x0D];
    std::uint8_t isNil;
  };

  struct SetCharNodeNil14Runtime
  {
    SetCharNodeNil14Runtime* left;
    SetCharNodeNil14Runtime* parent;
    SetCharNodeNil14Runtime* right;
    std::int8_t value;
    std::uint8_t color;
    std::uint8_t isNil;
  };

  struct PairKeyNodeNil37Runtime
  {
    PairKeyNodeNil37Runtime* left;
    PairKeyNodeNil37Runtime* parent;
    PairKeyNodeNil37Runtime* right;
    std::uint8_t pad0C[0x04];
    std::uint32_t keyHigh;
    std::uint32_t keyLow;
    std::uint8_t pad18[0x0D];
    std::uint8_t isNil;
  };
#pragma pack(pop)

  struct WrappedPointerArrayRuntime
  {
    std::uint32_t lane00;
    std::uint32_t* entries;
    std::uint32_t baseIndex;
  };

  struct WrappedArrayCursorRuntime
  {
    WrappedPointerArrayRuntime* owner;
    std::uint32_t logicalIndex;
  };

  struct NetCommandRecordRuntime
  {
    std::byte storage[0x30];
  };

  struct Vector16ByteOwnerRuntime
  {
    std::uint32_t lane00;
    std::byte* begin;
    std::byte* end;
    std::byte* capacity;
  };

  struct SharedRefRuntime
  {
    void* object;
    void* counter;
  };

  struct SharedRefInitRuntime
  {
    void* object;
    void* counter;
  };

  struct PairKeyRuntime
  {
    std::uint32_t high;
    std::uint32_t low;
  };

  struct PairNodeRuntime
  {
    std::int32_t lane00;
    std::int32_t lane04;
    std::int32_t lane08;
    std::byte payload0C[0x18];
    std::uint8_t color;
    std::uint8_t isNil;
  };

  struct IntrusiveListNodeRuntime
  {
    IntrusiveListNodeRuntime* next;
    IntrusiveListNodeRuntime* prev;
  };

  struct IntrusiveListRuntime
  {
    std::uint32_t lane00;
    IntrusiveListNodeRuntime* head;
    std::uint32_t size;
  };

  struct MeshThumbnailListNodeRuntime
  {
    MeshThumbnailListNodeRuntime* next;
    MeshThumbnailListNodeRuntime* prev;
    std::byte thumbnailStorage[1];
  };

  struct Stride136VectorRuntime
  {
    std::uint32_t lane00;
    std::byte* begin;
    std::byte* end;
    std::byte* capacity;
  };

  class CameraSnapshotViewRuntime
  {
  public:
    explicit CameraSnapshotViewRuntime(void* storage) noexcept
      : bytes_(static_cast<std::byte*>(storage))
    {
    }

    explicit CameraSnapshotViewRuntime(const void* storage) noexcept
      : bytes_(const_cast<std::byte*>(static_cast<const std::byte*>(storage)))
    {
    }

    [[nodiscard]] std::uint32_t& lane08() const noexcept
    {
      return *reinterpret_cast<std::uint32_t*>(bytes_ + 0x08);
    }

    [[nodiscard]] void* cameraStorage() const noexcept
    {
      return bytes_ + 0x10;
    }

    [[nodiscard]] std::uint32_t& lane2D8() const noexcept
    {
      return *reinterpret_cast<std::uint32_t*>(bytes_ + 0x2D8);
    }

    [[nodiscard]] float& lane2DC() const noexcept
    {
      return *reinterpret_cast<float*>(bytes_ + 0x2DC);
    }

    [[nodiscard]] float& lane2E0() const noexcept
    {
      return *reinterpret_cast<float*>(bytes_ + 0x2E0);
    }

    [[nodiscard]] float& lane2E4() const noexcept
    {
      return *reinterpret_cast<float*>(bytes_ + 0x2E4);
    }

    [[nodiscard]] float& lane2E8() const noexcept
    {
      return *reinterpret_cast<float*>(bytes_ + 0x2E8);
    }

    [[nodiscard]] std::uint32_t& lane2EC() const noexcept
    {
      return *reinterpret_cast<std::uint32_t*>(bytes_ + 0x2EC);
    }

    [[nodiscard]] std::uint32_t& lane2F0() const noexcept
    {
      return *reinterpret_cast<std::uint32_t*>(bytes_ + 0x2F0);
    }

    [[nodiscard]] std::uint32_t& lane2F4() const noexcept
    {
      return *reinterpret_cast<std::uint32_t*>(bytes_ + 0x2F4);
    }

    [[nodiscard]] std::uint32_t& lane2F8() const noexcept
    {
      return *reinterpret_cast<std::uint32_t*>(bytes_ + 0x2F8);
    }

    [[nodiscard]] std::uint32_t& lane2FC() const noexcept
    {
      return *reinterpret_cast<std::uint32_t*>(bytes_ + 0x2FC);
    }

    [[nodiscard]] std::uint32_t& lane300() const noexcept
    {
      return *reinterpret_cast<std::uint32_t*>(bytes_ + 0x300);
    }

    [[nodiscard]] void*& weakCounter304() const noexcept
    {
      return *reinterpret_cast<void**>(bytes_ + 0x304);
    }

  private:
    std::byte* bytes_;
  };

  class UnitSelectionStateViewRuntime
  {
  public:
    explicit UnitSelectionStateViewRuntime(const void* storage) noexcept
      : bytes_(static_cast<const std::byte*>(storage))
    {
    }

    [[nodiscard]] std::int32_t selectedIndex() const noexcept
    {
      return *reinterpret_cast<const std::int32_t*>(bytes_ + 0x488);
    }

    [[nodiscard]] void* const* entries() const noexcept
    {
      const auto entriesWord = *reinterpret_cast<const std::uint32_t*>(bytes_ + 0x3F0);
      return reinterpret_cast<void* const*>(static_cast<std::uintptr_t>(entriesWord));
    }

  private:
    const std::byte* bytes_;
  };

  class UnitSelectionEntryViewRuntime
  {
  public:
    explicit UnitSelectionEntryViewRuntime(const void* storage) noexcept
      : bytes_(static_cast<const std::byte*>(storage))
    {
    }

    [[nodiscard]] std::int32_t sampleCount() const noexcept
    {
      return *reinterpret_cast<const std::int32_t*>(bytes_ + 0x1C4);
    }

    [[nodiscard]] float minX() const noexcept
    {
      return *reinterpret_cast<const float*>(bytes_ + 0x1BC);
    }

    [[nodiscard]] float minY() const noexcept
    {
      return *reinterpret_cast<const float*>(bytes_ + 0x1C0);
    }

    [[nodiscard]] float minZ() const noexcept
    {
      return *reinterpret_cast<const float*>(bytes_ + 0x1C8);
    }

    [[nodiscard]] float extX() const noexcept
    {
      return *reinterpret_cast<const float*>(bytes_ + 0x1CC);
    }

    [[nodiscard]] float extY() const noexcept
    {
      return *reinterpret_cast<const float*>(bytes_ + 0x1D0);
    }

  private:
    const std::byte* bytes_;
  };

  using WxUnrefFn = void (*)(void*);
  using WxGetDefaultPointSizeFn = std::int32_t (*)();
  using WxStringAssignFn = void (*)(void* destination, const void* source);
  using WxControlDtorFn = int (*)(void*);
  using WxProtocolInitializeFn = void (*)(int lane0);
  using WxSocketOutputBaseCtorFn = void (*)(void* stream);
  using WxInputStreamCtorFn = void (*)(void* stream);
  using PairLookupFn = void (*)(std::int32_t outPair[2], std::uint32_t key);
  using NormalizePackedDoubleFn = std::int16_t (*)(std::uint16_t* words);
  using BuildQueueCompareFn = BuildQueueCompareResultRuntime* (*)(BuildQueueCompareStateRuntime* state, const std::byte* lhsBegin, const std::byte* lhsEnd, const std::byte* rhsBegin, std::uint32_t lane4, std::uint32_t lane5);
  using IosBaseDtorFn = void (*)(void* iosBaseLane);
  using RuntimeFailureDispatchFn = void (*)(int arg0, int arg1);
  using OwnerTreeClearFn = void (*)(TreeStorageOwnerRuntime* owner);
  using TreeClearFn = void (*)(void* scratch, void* root, void* head);
  using TreeClearWithOwnerFn = void (*)(void* owner, void* scratch, void* root, void* head);
  using NetCommandRecordCopyFn = void (*)(void* destinationRecord, const void* sourceRecord);
  using Vector16ConstructFn = int (*)(void* destination, std::uint32_t lane0, std::uint32_t lane1);
  using Vector16GrowFn = int (*)(Vector16ByteOwnerRuntime* owner, void* tail, std::uint32_t inputWord);
  using LookupNodeByTextFn = void* (*)(void* owner, const void* key);
  using AppendLookupTextFn = int (*)(void* sink, const void* node);
  using InitSharedRefFn = void (*)(SharedRefInitRuntime* outRef, void* object);
  using EnableSharedFromThisFn = void (*)(SharedRefInitRuntime* outRef, void* sharedOwner, void* rawObject);
  using PairMapInsertNodeFn = PairKeyNodeNil37Runtime* (*)(PairKeyNodeNil37Runtime** parentSlot, std::uint8_t insertLeft, const PairKeyRuntime* key);
  using PairMapFixupFn = void (*)(PairKeyNodeNil37Runtime** parentSlot);
  using PairNodeAllocFn = PairNodeRuntime* (*)(std::uint32_t count);
  using PairNodePayloadInitFn = void (*)(void* payloadStorage, std::int32_t sourceWord);
  using ObjectPreDeleteFn = void (*)(void* object);
  using ListClearFn = void (*)(IntrusiveListRuntime* list);
  using ListSpliceFn = void (*)(IntrusiveListRuntime* destination, IntrusiveListNodeRuntime* destinationPosition, IntrusiveListNodeRuntime* first, IntrusiveListNodeRuntime* last, IntrusiveListNodeRuntime* sourceNext);
  using CameraCopyFn = void (*)(void* destinationCamera, const void* sourceCamera);
  using WeakReleaseFn = void (*)(void* counter);
  using MeshThumbnailDtorFn = void (*)(void* thumbnail);
  using BuildSelectionRangeFn = void (*)(void* outRange, void* owner, void* begin, void* end);
  using SubmitSelectionQuadFn = int (*)(const float* quadVertices, void* owner);
  using ConstructStride136Fn = void (*)(std::byte* destination, std::uint32_t count, std::uint32_t lane4, std::uint32_t lane5);
  using GrowStride136Fn = void (*)(Stride136VectorRuntime* owner, void* scratch, std::byte* tail, void* source);
  using CloneTreeStorageFn = void* (*)(std::uint8_t lane0, std::uint8_t lane1, std::uint8_t lane2, void* sourceRoot);
  using CloneTreePayloadFn = void (*)(void* destinationRoot, void* sourceRoot);
  using SimpleDtorFn = void (*)(void* object);
  using TesselatorGetIndexFn = std::uint16_t (*)(void* tesselator, std::uint32_t size, const std::uint8_t* rowToken, std::int32_t column);
  using TesselatorAddTriangleFn = void (*)(void* tesselator, std::uint32_t source, std::uint32_t middle, std::uint32_t destination);

  [[nodiscard]] std::uint32_t DivideBy1000Fast(const std::uint32_t value) noexcept
  {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(value) * 0x10624DD3ull) >> 38u);
  }

  [[nodiscard]] std::ptrdiff_t CountStride48Elements(
    const std::byte* const begin,
    const std::byte* const end
  ) noexcept
  {
    if (begin == nullptr || end == nullptr || end < begin) {
      return 0;
    }
    return (end - begin) / 48;
  }

  void ReleaseSharedWxStringLane(const std::uint32_t laneWord) noexcept
  {
    if (laneWord == 0u) {
      return;
    }

    auto* const header = reinterpret_cast<std::int32_t*>(static_cast<std::uintptr_t>(laneWord) - 12u);
    const std::int32_t refCount = *header;
    if (refCount == -1) {
      return;
    }

    *header = refCount - 1;
    if (refCount == 1) {
      ::operator delete(header);
    }
  }

  void ResetSmallStringLane(SmallStringSboRuntime* const value)
  {
    if (value == nullptr) {
      return;
    }

    if (value->capacity >= 16u) {
      ::operator delete(value->heapStorage);
    }

    value->capacity = 15u;
    value->size = 0u;
    value->inlineStorage[0] = '\0';
  }

  [[nodiscard]] std::uint32_t ResolveWrappedPointerWord(
    const WrappedPointerArrayRuntime* const cursorOwner,
    const std::uint32_t logicalIndex
  ) noexcept
  {
    if (cursorOwner == nullptr || cursorOwner->entries == nullptr) {
      return 0u;
    }

    std::uint32_t resolvedIndex = logicalIndex;
    if (cursorOwner->baseIndex <= logicalIndex) {
      resolvedIndex = logicalIndex - cursorOwner->baseIndex;
    }

    return cursorOwner->entries[resolvedIndex];
  }

  [[nodiscard]] PairKeyNodeNil37Runtime* EnsurePairMapHeadRuntime(
    LegacyMapStorageRuntime<PairKeyNodeNil37Runtime>* const map
  )
  {
    if (map == nullptr) {
      return nullptr;
    }

    if (map->head != nullptr) {
      return map->head;
    }

    auto* const head = static_cast<PairKeyNodeNil37Runtime*>(::operator new(sizeof(PairKeyNodeNil37Runtime), std::nothrow));
    if (head == nullptr) {
      return nullptr;
    }

    std::memset(head, 0, sizeof(PairKeyNodeNil37Runtime));
    head->left = head;
    head->parent = head;
    head->right = head;
    head->isNil = 1u;

    map->head = head;
    map->size = 0u;
    return head;
  }

  [[nodiscard]] bool PairKeyLessRuntime(const PairKeyRuntime& lhs, const PairKeyRuntime& rhs) noexcept
  {
    if (lhs.high < rhs.high) {
      return true;
    }
    if (rhs.high < lhs.high) {
      return false;
    }
    return lhs.low < rhs.low;
  }

  void ReleaseSharedCounterRuntime(void* counter) noexcept
  {
    if (counter == nullptr) {
      return;
    }

    auto* const bytes = static_cast<std::byte*>(counter);
    auto* const strong = reinterpret_cast<volatile LONG*>(bytes + 4);
    if (::InterlockedExchangeAdd(strong, -1) == 1) {
      auto** const vtable = *reinterpret_cast<void***>(counter);
      using DisposeFn = void (__thiscall*)(void*);
      const auto dispose = reinterpret_cast<DisposeFn>(vtable[1]);
      if (dispose != nullptr) {
        dispose(counter);
      }

      auto* const weak = reinterpret_cast<volatile LONG*>(bytes + 8);
      if (::InterlockedExchangeAdd(weak, -1) == 1) {
        using DestroyFn = void (__thiscall*)(void*);
        const auto destroy = reinterpret_cast<DestroyFn>(vtable[2]);
        if (destroy != nullptr) {
          destroy(counter);
        }
      }
    }
  }

  [[nodiscard]] std::ptrdiff_t CountStride136Elements(
    const std::byte* const begin,
    const std::byte* const end
  ) noexcept
  {
    if (begin == nullptr || end == nullptr || end < begin) {
      return 0;
    }
    return (end - begin) / 136;
  }

#if INTPTR_MAX == INT32_MAX
  static_assert(offsetof(RbNodeFlag25Runtime, isNil25) == 0x19, "RbNodeFlag25Runtime::isNil25 offset must be 0x19");
  static_assert(offsetof(DistanceVector2fRuntime, hasRawResult) == 0x20, "DistanceVector2fRuntime::hasRawResult offset must be 0x20");
  static_assert(offsetof(DistanceVector2dRuntime, hasRawResult) == 0x40, "DistanceVector2dRuntime::hasRawResult offset must be 0x40");
  static_assert(offsetof(LinkPatchRuntime, lane10) == 0x10, "LinkPatchRuntime::lane10 offset must be 0x10");
  static_assert(sizeof(RefCountedPayload49Runtime) == 0x31, "RefCountedPayload49Runtime size must be 0x31");
  static_assert(offsetof(WxFontDescriptorRuntime, pointSize) == 0x08, "WxFontDescriptorRuntime::pointSize offset must be 0x08");
  static_assert(offsetof(WxFontDescriptorRuntime, faceNameStorage) == 0x1C, "WxFontDescriptorRuntime::faceNameStorage offset must be 0x1C");
  static_assert(offsetof(WxFontDescriptorRuntime, lane84) == 0x84, "WxFontDescriptorRuntime::lane84 offset must be 0x84");
  static_assert(offsetof(WxSharedStringOwnerRuntime, stringLane20) == 0x20, "WxSharedStringOwnerRuntime::stringLane20 offset must be 0x20");
  static_assert(offsetof(Div10PairOwnerRuntime, sourceLane6C) == 0x6C, "Div10PairOwnerRuntime::sourceLane6C offset must be 0x6C");
  static_assert(offsetof(WxSocketOutputStreamRuntime, socketHandle) == 0x0C, "WxSocketOutputStreamRuntime::socketHandle offset must be 0x0C");
  static_assert(offsetof(WxSocketInputStreamRuntime, socketHandle) == 0x18, "WxSocketInputStreamRuntime::socketHandle offset must be 0x18");
  static_assert(sizeof(SmallStringSboRuntime) == 0x18, "SmallStringSboRuntime size must be 0x18");
  static_assert(offsetof(WaveParametersRuntime, lane08) == 0x08, "WaveParametersRuntime::lane08 offset must be 0x08");
  static_assert(offsetof(WaveParametersRuntime, lane24) == 0x24, "WaveParametersRuntime::lane24 offset must be 0x24");
  static_assert(offsetof(WindowTextMetricOwnerRuntime, windowHandle) == 0x108, "WindowTextMetricOwnerRuntime::windowHandle offset must be 0x108");
  static_assert(offsetof(RegionNodeRuntime, regionHandle) == 0x08, "RegionNodeRuntime::regionHandle offset must be 0x08");
  static_assert(sizeof(EmitterCurveKeyRuntime) == 0x10, "EmitterCurveKeyRuntime size must be 0x10");
  static_assert(offsetof(TimeSplitRuntime, seconds) == 0x24, "TimeSplitRuntime::seconds offset must be 0x24");
  static_assert(offsetof(TimeSplitRuntime, microseconds) == 0x28, "TimeSplitRuntime::microseconds offset must be 0x28");
  static_assert(offsetof(BuildQueueSnapshotRuntime, begin) == 0x04, "BuildQueueSnapshotRuntime::begin offset must be 0x04");
  static_assert(offsetof(BuildQueueSnapshotRuntime, end) == 0x08, "BuildQueueSnapshotRuntime::end offset must be 0x08");
  static_assert(sizeof(OccupySourceBindingRuntime) == 0x0C, "OccupySourceBindingRuntime size must be 0x0C");
  static_assert(sizeof(ClutterSeedRuntime) == 0x10, "ClutterSeedRuntime size must be 0x10");
  static_assert(offsetof(MapNodeNil17Runtime, key) == 0x0C, "MapNodeNil17Runtime::key offset must be 0x0C");
  static_assert(offsetof(MapNodeNil17Runtime, isNil) == 0x11, "MapNodeNil17Runtime::isNil offset must be 0x11");
  static_assert(offsetof(MapNodeNil29Runtime, key) == 0x0C, "MapNodeNil29Runtime::key offset must be 0x0C");
  static_assert(offsetof(MapNodeNil29Runtime, isNil) == 0x1D, "MapNodeNil29Runtime::isNil offset must be 0x1D");
  static_assert(offsetof(SetCharNodeNil14Runtime, value) == 0x0C, "SetCharNodeNil14Runtime::value offset must be 0x0C");
  static_assert(offsetof(SetCharNodeNil14Runtime, isNil) == 0x0E, "SetCharNodeNil14Runtime::isNil offset must be 0x0E");
  static_assert(offsetof(PairKeyNodeNil37Runtime, keyHigh) == 0x10, "PairKeyNodeNil37Runtime::keyHigh offset must be 0x10");
  static_assert(offsetof(PairKeyNodeNil37Runtime, keyLow) == 0x14, "PairKeyNodeNil37Runtime::keyLow offset must be 0x14");
  static_assert(offsetof(PairKeyNodeNil37Runtime, isNil) == 0x25, "PairKeyNodeNil37Runtime::isNil offset must be 0x25");
  static_assert(offsetof(PairNodeRuntime, payload0C) == 0x0C, "PairNodeRuntime::payload0C offset must be 0x0C");
  static_assert(offsetof(PairNodeRuntime, color) == 0x24, "PairNodeRuntime::color offset must be 0x24");
  static_assert(offsetof(PairNodeRuntime, isNil) == 0x25, "PairNodeRuntime::isNil offset must be 0x25");
#endif

  template <typename NodeT, std::size_t NilOffset>
  [[nodiscard]] NodeT* AdvanceRbIteratorRuntime(NodeT** const cursor) noexcept
  {
    if (cursor == nullptr || *cursor == nullptr) {
      return nullptr;
    }

    NodeT* const node = *cursor;
    if (NodeHasSentinelFlag(node, NilOffset)) {
      return node;
    }

    NodeT* right = node->right;
    if (NodeHasSentinelFlag(right, NilOffset)) {
      NodeT* parent = node->parent;
      while (!NodeHasSentinelFlag(parent, NilOffset)) {
        if (*cursor != parent->right) {
          break;
        }
        *cursor = parent;
        parent = parent->parent;
      }

      *cursor = parent;
      return parent;
    }

    while (!NodeHasSentinelFlag(right->left, NilOffset)) {
      right = right->left;
    }
    *cursor = right;
    return right;
  }

  [[nodiscard]] std::uint32_t SignalingExponentMask(const double value) noexcept
  {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    return static_cast<std::uint32_t>((bits >> 32u) & 0x7FF00000u);
  }
}

/**
 * Address: 0x00626E10 (FUN_00626E10)
 *
 * What it does:
 * Appends one 12-byte pickup-info lane into a legacy growth vector,
 * expanding storage when the current capacity is exhausted.
 */
[[maybe_unused]] Element12Runtime* AppendPickupInfoLaneRuntime(
  const Element12Runtime* const value,
  LegacyVectorStorageRuntime<Element12Runtime>* const vector
)
{
  const Element12Runtime copy = value != nullptr ? *value : Element12Runtime{};
  return AppendTrivialValue(vector, copy);
}

/**
 * Address: 0x00642180 (FUN_00642180)
 *
 * What it does:
 * Verifies one key in a lookup cache and refreshes the cached resolved lane
 * when the key is currently present.
 */
[[maybe_unused]] bool TryResolveLookupAndCacheRuntime(
  const std::uint32_t key,
  LookupCacheRuntime* const cache,
  const std::uint32_t argument
)
{
  if (cache == nullptr || cache->containsFn == nullptr) {
    return false;
  }

  if (!cache->containsFn(cache->containsState, key)) {
    return false;
  }

  if (cache->resolveFn != nullptr) {
    cache->cachedValue = cache->resolveFn(cache->context, key, argument);
  }

  return true;
}

/**
 * Address: 0x0067DAA0 (FUN_0067DAA0)
 *
 * What it does:
 * Resizes one 32-bit pointer/id vector to `desiredCount` and zero-fills any
 * newly appended lanes.
 */
[[maybe_unused]] std::uint32_t* ResizePointerVectorRuntime(
  const std::uint32_t desiredCount,
  LegacyVectorStorageRuntime<std::uint32_t>* const vector
)
{
  return ResizeTrivialVectorWithFill(vector, desiredCount, 0u);
}

/**
 * Address: 0x006842E0 (FUN_006842E0)
 *
 * What it does:
 * Releases one owned node-buffer lane and resets the owning storage metadata.
 */
[[maybe_unused]] std::int32_t ReleaseEntityDbNodeBufferRuntime(
  OwnedBufferRuntime* const owner
)
{
  return ResetOwnedBufferRuntime(owner);
}

/**
 * Address: 0x00685350 (FUN_00685350)
 *
 * What it does:
 * Finds-or-inserts one entity-db tree node by `EntId` and returns the
 * iterator/insert-status pair lane.
 */
[[maybe_unused]] MapInsertStatusRuntime* FindOrInsertEntityNodeByIdRuntime(
  LegacyMapStorageRuntime<MapNodeNil21Runtime>* const map,
  const std::uint32_t* const key,
  MapInsertStatusRuntime* const outResult
)
{
  return FindOrInsertMapNodeByKey(map, key, outResult);
}

/**
 * Address: 0x006870D0 (FUN_006870D0)
 *
 * What it does:
 * Finds-or-inserts one id-pool map node by key and emits the
 * `(node, inserted)` status pair.
 */
[[maybe_unused]] MapInsertStatusRuntime* FindOrInsertIdPoolNodeByKeyRuntime(
  LegacyMapStorageRuntime<MapNodeNil21Runtime>* const map,
  const std::uint32_t* const key,
  MapInsertStatusRuntime* const outResult
)
{
  return FindOrInsertMapNodeByKey(map, key, outResult);
}

/**
 * Address: 0x00687AF0 (FUN_00687AF0)
 *
 * What it does:
 * Rebuilds one 100-slot IdPool history ring from another ring by replaying
 * each active snapshot lane in order.
 */
[[maybe_unused]] moho::SimSubRes2* CopyIdPoolHistoryRingRuntime(
  moho::SimSubRes2* const destination,
  const moho::SimSubRes2* const source
)
{
  if (destination == nullptr || source == nullptr || destination == source) {
    return destination;
  }

  destination->Reset();
  for (int index = source->mStart; index != source->mEnd; index = (index + 1) % static_cast<int>(kIdPoolHistoryCapacity)) {
    destination->PushSnapshot(AsIdPoolSnapshot(source->mData[index]));
  }
  return destination;
}

/**
 * Address: 0x0069E6D0 (FUN_0069E6D0)
 *
 * What it does:
 * Appends one 12-byte projectile lane into a legacy growth vector with
 * automatic capacity expansion.
 */
[[maybe_unused]] Element12Runtime* AppendProjectileLaneRuntime(
  const Element12Runtime* const value,
  LegacyVectorStorageRuntime<Element12Runtime>* const vector
)
{
  const Element12Runtime copy = value != nullptr ? *value : Element12Runtime{};
  return AppendTrivialValue(vector, copy);
}

/**
 * Address: 0x006AF120 (FUN_006AF120)
 *
 * What it does:
 * Resizes one recon-blip pointer vector and fills newly exposed lanes with the
 * caller-provided pointer value.
 */
[[maybe_unused]] std::uint32_t ResizeReconBlipPointerVectorRuntime(
  const std::uint32_t desiredCount,
  const std::uint32_t* const fillValue,
  LegacyVectorStorageRuntime<std::uint32_t>* const vector
)
{
  if (vector == nullptr) {
    return 0u;
  }

  const std::uint32_t value = fillValue != nullptr ? *fillValue : 0u;
  (void)ResizeTrivialVectorWithFill(vector, desiredCount, value);
  return static_cast<std::uint32_t>(VectorSize(*vector));
}

/**
 * Address: 0x006D1960 (FUN_006D1960)
 *
 * What it does:
 * Appends one 8-byte pair lane into a legacy vector used by upgrade-notify
 * pipelines.
 */
[[maybe_unused]] Element8Runtime* AppendUpgradePairLaneRuntime(
  const Element8Runtime* const value,
  LegacyVectorStorageRuntime<Element8Runtime>* const vector
)
{
  const Element8Runtime copy = value != nullptr ? *value : Element8Runtime{};
  return AppendTrivialValue(vector, copy);
}

/**
 * Address: 0x006DB150 (FUN_006DB150)
 *
 * What it does:
 * Appends one 12-byte command lane into a legacy vector with on-demand growth.
 */
[[maybe_unused]] Element12Runtime* AppendUnitCommandLaneRuntime(
  const Element12Runtime* const value,
  LegacyVectorStorageRuntime<Element12Runtime>* const vector
)
{
  const Element12Runtime copy = value != nullptr ? *value : Element12Runtime{};
  return AppendTrivialValue(vector, copy);
}

/**
 * Address: 0x006E0A40 (FUN_006E0A40)
 *
 * What it does:
 * Releases one CommandDatabase-owned node buffer and clears ownership lanes.
 */
[[maybe_unused]] std::int32_t ReleaseCommandDatabaseNodeBufferRuntime(
  OwnedBufferRuntime* const owner
)
{
  return ResetOwnedBufferRuntime(owner);
}

/**
 * Address: 0x006E15B0 (FUN_006E15B0)
 *
 * What it does:
 * Finds-or-inserts one command-db tree node by command id and writes
 * `(node, inserted)` to the caller-provided status lane.
 */
[[maybe_unused]] MapInsertStatusRuntime* FindOrInsertCommandNodeByIdRuntime(
  LegacyMapStorageRuntime<MapNodeNil21Runtime>* const map,
  const std::uint32_t* const key,
  MapInsertStatusRuntime* const outResult
)
{
  return FindOrInsertMapNodeByKey(map, key, outResult);
}

/**
 * Address: 0x006FD8B0 (FUN_006FD8B0)
 *
 * What it does:
 * Releases one CArmyStats-owned node buffer and clears ownership lanes.
 */
[[maybe_unused]] std::int32_t ReleaseArmyStatsNodeBufferRuntime(
  OwnedBufferRuntime* const owner
)
{
  return ResetOwnedBufferRuntime(owner);
}

/**
 * Address: 0x007108D0 (FUN_007108D0)
 *
 * What it does:
 * Finds-or-inserts one army-stats tree node by key and emits the insertion
 * status pair for callers.
 */
[[maybe_unused]] MapInsertStatusRuntime* FindOrInsertArmyStatsNodeByKeyRuntime(
  LegacyMapStorageRuntime<MapNodeNil21Runtime>* const map,
  const std::uint32_t* const key,
  MapInsertStatusRuntime* const outResult
)
{
  return FindOrInsertMapNodeByKey(map, key, outResult);
}

/**
 * Address: 0x00715440 (FUN_00715440)
 *
 * What it does:
 * Releases one influence-grid entry tree head and clears set ownership lanes.
 */
[[maybe_unused]] std::int32_t ResetInfluenceGridEntryStorageRuntime(
  OwnedBufferRuntime* const owner
)
{
  return ResetOwnedBufferRuntime(owner);
}

/**
 * Address: 0x0071A9A0 (FUN_0071A9A0)
 *
 * What it does:
 * Finds-or-inserts one wide-node influence map entry (sentinel flag lane at
 * +0x3D) and reports insertion status.
 */
[[maybe_unused]] MapInsertStatusRuntime* FindOrInsertInfluenceNodeWideRuntime(
  LegacyMapStorageRuntime<MapNodeNil61Runtime>* const map,
  const std::uint32_t* const key,
  MapInsertStatusRuntime* const outResult
)
{
  return FindOrInsertMapNodeByKey(map, key, outResult);
}

/**
 * Address: 0x0071B360 (FUN_0071B360)
 *
 * What it does:
 * Finds-or-inserts one influence map entry node (sentinel flag lane at +0x15)
 * and returns the iterator/insert-status pair.
 */
[[maybe_unused]] MapInsertStatusRuntime* FindOrInsertInfluenceNodeRuntime(
  LegacyMapStorageRuntime<MapNodeNil21Runtime>* const map,
  const std::uint32_t* const key,
  MapInsertStatusRuntime* const outResult
)
{
  return FindOrInsertMapNodeByKey(map, key, outResult);
}

/**
 * Address: 0x0071C300 (FUN_0071C300)
 *
 * What it does:
 * Allocates one influence-node storage lane and, on success, invokes the
 * caller-provided link/init callback.
 */
[[maybe_unused]] void* AllocateInfluenceNodeAndInitRuntime(
  const std::size_t nodeSize,
  void (*const initFn)(std::int32_t, std::int32_t, std::uint8_t),
  const std::int32_t initArg0,
  const std::int32_t initArg1,
  const std::uint8_t initSide
)
{
  const std::size_t allocationSize = nodeSize == 0u ? 1u : nodeSize;
  void* const node = ::operator new(allocationSize, std::nothrow);
  if (node != nullptr && initFn != nullptr) {
    initFn(initArg0, initArg1, initSide);
  }
  return node;
}

/**
 * Address: 0x0073B060 (FUN_0073B060)
 *
 * What it does:
 * Converts elapsed cycles from the embedded timer lane into microseconds and
 * atomically accumulates them into the owner counter at `+0x24`.
 */
[[maybe_unused]] std::int32_t AccumulateTimerElapsedMicrosecondsRuntime(
  TimerAccumulatorRuntime* const runtime
)
{
  if (runtime == nullptr || runtime->counterOwner == 0u) {
    return 0;
  }

  const LONGLONG elapsedCycles = runtime->elapsedTimer.ElapsedCycles();
  const std::int32_t elapsedMicros = static_cast<std::int32_t>(gpg::time::CyclesToMicroseconds(elapsedCycles));
  auto* const ownerBase = reinterpret_cast<std::uint8_t*>(runtime->counterOwner);
  auto* const target = reinterpret_cast<volatile LONG*>(ownerBase + 36u);
  return static_cast<std::int32_t>(::InterlockedExchangeAdd(const_cast<LONG*>(target), elapsedMicros));
}

/**
 * Address: 0x00740AF0 (FUN_00740AF0)
 *
 * What it does:
 * Destroys one vector lane of `SSTIArmyConstantData` entries and releases the
 * backing storage block.
 */
[[maybe_unused]] void DestroyArmyConstantDataVectorRuntime(
  LegacyVectorStorageRuntime<moho::SSTIArmyConstantData>* const vector
)
{
  if (vector == nullptr) {
    return;
  }

  if (vector->begin != nullptr) {
    for (moho::SSTIArmyConstantData* cursor = vector->begin; cursor != vector->end; ++cursor) {
      cursor->~SSTIArmyConstantData();
    }
    ::operator delete(vector->begin);
  }

  vector->begin = nullptr;
  vector->end = nullptr;
  vector->capacity = nullptr;
}

/**
 * Address: 0x00740B40 (FUN_00740B40)
 *
 * What it does:
 * Destroys one vector lane of `SSTIArmyVariableData` entries and releases the
 * backing storage block.
 */
[[maybe_unused]] void DestroyArmyVariableDataVectorRuntime(
  LegacyVectorStorageRuntime<moho::SSTIArmyVariableData>* const vector
)
{
  if (vector == nullptr) {
    return;
  }

  if (vector->begin != nullptr) {
    for (moho::SSTIArmyVariableData* cursor = vector->begin; cursor != vector->end; ++cursor) {
      cursor->~SSTIArmyVariableData();
    }
    ::operator delete(vector->begin);
  }

  vector->begin = nullptr;
  vector->end = nullptr;
  vector->capacity = nullptr;
}

/**
 * Address: 0x00740C50 (FUN_00740C50)
 *
 * What it does:
 * Destroys one vector lane of `SSTIUnitVariableData` slot wrappers
 * (`0x8-byte header + payload + tail`) and releases storage.
 */
[[maybe_unused]] void DestroyUnitVariableDataSlotVectorRuntime(
  LegacyVectorStorageRuntime<SSTIUnitVariableDataSlotRuntime>* const vector
)
{
  if (vector == nullptr) {
    return;
  }

  if (vector->begin != nullptr) {
    for (SSTIUnitVariableDataSlotRuntime* cursor = vector->begin; cursor != vector->end; ++cursor) {
      cursor->mVariableData.~SSTIUnitVariableData();
    }
    ::operator delete(vector->begin);
  }

  vector->begin = nullptr;
  vector->end = nullptr;
  vector->capacity = nullptr;
}

/**
 * Address: 0x00740F00 (FUN_00740F00)
 *
 * What it does:
 * Copies one GeomCamera range into destination lanes and destroys any now-extra
 * destination tail entries.
 */
[[maybe_unused]] moho::GeomCamera3** CopyGeomCameraRangeAndPruneTailRuntime(
  CameraCopyContextRuntime* const context,
  moho::GeomCamera3** const outIterator,
  moho::GeomCamera3* const destinationBegin,
  const moho::GeomCamera3* const sourceBegin
)
{
  moho::GeomCamera3* destination = destinationBegin;
  if (context != nullptr && destinationBegin != sourceBegin) {
    moho::GeomCamera3* const previousEnd = context->destinationEnd;
    moho::GeomCamera3* const copiedEnd =
      moho::CopyGeomCameraRangeAndReturnEnd(sourceBegin, destinationBegin, previousEnd);

    if (previousEnd != nullptr) {
      for (moho::GeomCamera3* cursor = copiedEnd; cursor != previousEnd; ++cursor) {
        cursor->~GeomCamera3();
      }
    }

    context->destinationEnd = copiedEnd;
  }

  if (outIterator != nullptr) {
    *outIterator = destination;
  }
  return outIterator;
}

/**
 * Address: 0x00753630 (FUN_00753630)
 *
 * What it does:
 * Rebuilds one opaque pointer lane when requested/current lanes differ, then
 * returns the requested lane through `outValue`.
 */
[[maybe_unused]] std::uint32_t* AssignRebuiltOpaqueLaneRuntimeA(
  OpaqueLaneRebuildRuntime* const context,
  std::uint32_t* const outValue,
  const std::uint32_t requestedLane,
  const std::uint32_t currentLane
)
{
  if (context != nullptr && requestedLane != currentLane) {
    context->storage = RebuildOpaqueLaneStorage(context->storage, static_cast<std::size_t>(requestedLane), false);
  }

  if (outValue != nullptr) {
    *outValue = requestedLane;
  }
  return outValue;
}

/**
 * Address: 0x007536D0 (FUN_007536D0)
 *
 * What it does:
 * Rebuilds one opaque pointer lane with zero-initialized replacement storage
 * when requested/current lanes differ, then writes the requested lane out.
 */
[[maybe_unused]] std::uint32_t* AssignRebuiltOpaqueLaneRuntimeB(
  OpaqueLaneRebuildRuntime* const context,
  std::uint32_t* const outValue,
  const std::uint32_t requestedLane,
  const std::uint32_t currentLane
)
{
  if (context != nullptr && requestedLane != currentLane) {
    context->storage = RebuildOpaqueLaneStorage(context->storage, static_cast<std::size_t>(requestedLane), true);
  }

  if (outValue != nullptr) {
    *outValue = requestedLane;
  }
  return outValue;
}

/**
 * Address: 0x0075F050 (FUN_0075F050)
 *
 * What it does:
 * Appends one 12-byte pose-copy lane into a legacy growth vector.
 */
[[maybe_unused]] Element12Runtime* AppendPoseCopyLaneRuntime(
  const Element12Runtime* const value,
  LegacyVectorStorageRuntime<Element12Runtime>* const vector
)
{
  const Element12Runtime copy = value != nullptr ? *value : Element12Runtime{};
  return AppendTrivialValue(vector, copy);
}

/**
 * Address: 0x00762120 (FUN_00762120)
 *
 * What it does:
 * Resizes one vector of packed seven-float payload lanes (`0x1C` each),
 * filling new lanes from the caller-provided sample value.
 */
[[maybe_unused]] std::uint32_t ResizeFloat7VectorWithFillRuntime(
  const std::uint32_t desiredCount,
  LegacyVectorStorageRuntime<Float7Runtime>* const vector,
  const Float7Runtime* const fillValue
)
{
  if (vector == nullptr) {
    return 0u;
  }

  const Float7Runtime value = fillValue != nullptr ? *fillValue : Float7Runtime{};
  (void)ResizeTrivialVectorWithFill(vector, desiredCount, value);
  return static_cast<std::uint32_t>(VectorSize(*vector));
}

/**
 * Address: 0x00765130 (FUN_00765130)
 *
 * What it does:
 * Resizes one word vector to `desiredCount`, trimming tail lanes when shrinking
 * and filling appended lanes with `fillByte` when growing.
 */
[[maybe_unused]] std::uint32_t* ResizeWordVectorWithFillByteRuntime(
  const std::uint32_t desiredCount,
  LegacyVectorStorageRuntime<std::uint32_t>* const vector,
  const std::uint8_t fillByte
)
{
  const std::uint32_t fillWord = static_cast<std::uint32_t>(fillByte);
  return ResizeTrivialVectorWithFill(vector, desiredCount, fillWord);
}

/**
 * Address: 0x007672E0 (FUN_007672E0)
 *
 * What it does:
 * Finalizes one cached word-vector lane, synchronizes staged begin/end cursors,
 * and invalidates the cached index lane.
 */
[[maybe_unused]] std::int32_t FinalizeWordVectorCacheStateRuntime(
  CacheWordVectorRuntime* const runtime
)
{
  if (runtime == nullptr) {
    return 0;
  }

  std::int32_t result = 0;
  if (runtime->begin != nullptr && runtime->end != nullptr && runtime->end >= runtime->begin) {
    result = static_cast<std::int32_t>(runtime->end - runtime->begin);
  }

  if (runtime->stagedBeginIndex != runtime->stagedEndIndex) {
    result = 0;
    runtime->stagedEndIndex = runtime->stagedBeginIndex;
  }

  runtime->cachedIndex = -1;
  return result;
}

/**
 * Address: 0x00530D30 (FUN_00530D30)
 *
 * What it does:
 * Allocates one RB-tree node lane with null links and marks it as the
 * sentinel-style root marker (`+0x2C=1`, `+0x2D=0`).
 */
[[maybe_unused]] RbNodeFlag45Runtime* AllocateRuleTreeNodeRuntime()
{
  auto* const node = AllocateZeroedRuntimeNode<RbNodeFlag45Runtime>();
  if (node == nullptr) {
    return nullptr;
  }

  node->left = nullptr;
  node->parent = nullptr;
  node->right = nullptr;
  node->sentinel44 = 1u;
  node->isNil45 = 0u;
  return node;
}

/**
 * Address: 0x00530FA0 (FUN_00530FA0)
 *
 * What it does:
 * Recursively destroys one RB-tree lane and releases dynamic string storage
 * when capacity is heap-backed (`capacity >= 16`).
 */
[[maybe_unused]] void DestroyRuleTreeNodeRecursiveRuntime(
  RbNodeFlag45Runtime* const node
)
{
  DestroyRecursiveStringTree(node);
}

/**
 * Address: 0x005347A0 (FUN_005347A0)
 *
 * What it does:
 * Appends one 32-bit blueprint-registry word into a legacy vector lane.
 */
[[maybe_unused]] std::uint32_t AppendBlueprintRegistryWordRuntime(
  const std::uint32_t* const value,
  LegacyVectorStorageRuntime<std::uint32_t>* const vector
)
{
  if (value == nullptr || vector == nullptr) {
    return 0u;
  }

  std::uint32_t* const inserted = AppendTrivialValue(vector, *value);
  return inserted != nullptr ? *inserted : *value;
}

/**
 * Address: 0x00545280 (FUN_00545280)
 *
 * What it does:
 * Resets one swap-backed dynamic array lane to its fallback storage block and
 * refreshes cached cursor/first-value lanes.
 */
[[maybe_unused]] std::uint32_t ResetSwapBackedArrayRuntimeA(
  SwapBackedArrayRuntimeA* const runtime
)
{
  if (runtime == nullptr) {
    return 0u;
  }

  if (runtime->activeBuffer == runtime->fallbackBuffer) {
    runtime->cursor = runtime->activeBuffer;
    return runtime->activeBuffer != nullptr ? *runtime->activeBuffer : 0u;
  }

  ::operator delete[](runtime->activeBuffer);
  runtime->activeBuffer = runtime->fallbackBuffer;
  runtime->cachedFirst = runtime->activeBuffer != nullptr ? *runtime->activeBuffer : 0u;
  runtime->cursor = runtime->activeBuffer;
  return runtime->cachedFirst;
}

/**
 * Address: 0x00556DE0 (FUN_00556DE0)
 *
 * What it does:
 * Allocates one category-map node lane with cleared links and sentinel-state
 * flags (`+0x14=1`, `+0x15=0`).
 */
[[maybe_unused]] RbNodeFlag21Runtime* AllocateCategoryMapNodeRuntime()
{
  auto* const node = AllocateZeroedRuntimeNode<RbNodeFlag21Runtime>();
  if (node == nullptr) {
    return nullptr;
  }

  node->left = nullptr;
  node->parent = nullptr;
  node->right = nullptr;
  node->sentinel20 = 1u;
  node->isNil21 = 0u;
  return node;
}

/**
 * Address: 0x0055D940 (FUN_0055D940)
 *
 * What it does:
 * Destroys one contiguous range of `UnitWeaponInfo` entries (`0x98` bytes per
 * lane).
 */
[[maybe_unused]] std::uint8_t* DestroyUnitWeaponInfoRangeRuntime(
  std::uint8_t* const begin,
  const std::uint8_t* const end
)
{
  if (begin == nullptr) {
    return nullptr;
  }

  for (std::uint8_t* cursor = begin; cursor != end; cursor += 152u) {
    reinterpret_cast<moho::UnitWeaponInfo*>(cursor)->~UnitWeaponInfo();
  }
  return begin;
}

/**
 * Address: 0x0056EC00 (FUN_0056EC00)
 *
 * What it does:
 * Copies one RB-map header lane (size/root) and recomputes cached
 * leftmost/rightmost pointers for `isNil` flag offset `+0x41`.
 */
[[maybe_unused]] RbMapFlag65Runtime* CopyMapHeaderAndExtremaFlag65Runtime(
  RbMapFlag65Runtime* const destination,
  const RbMapFlag65Runtime* const source,
  const CloneTree65Fn cloneFn
)
{
  if (destination == nullptr || source == nullptr || destination->head == nullptr || source->head == nullptr) {
    return destination;
  }

  RbNodeFlag65Runtime* sourceRoot = source->head->parent;
  if (cloneFn != nullptr && !NodeHasSentinelFlag(sourceRoot, 0x41u)) {
    sourceRoot = cloneFn(sourceRoot, destination->head);
  }
  destination->head->parent = sourceRoot;
  destination->size = source->size;
  RecomputeHeadExtrema(destination->head, 0x41u);
  return destination;
}

/**
 * Address: 0x00578FE0 (FUN_00578FE0)
 *
 * What it does:
 * Copy-constructs `count` consecutive `LuaObject` lanes from one source
 * object.
 */
[[maybe_unused]] void CopyConstructLuaObjectRangeRuntime(
  std::int32_t count,
  LuaPlus::LuaObject* destination,
  const LuaPlus::LuaObject* const source
)
{
  while (count > 0) {
    if (destination != nullptr && source != nullptr) {
      ::new (destination) LuaPlus::LuaObject(*source);
    }
    --count;
    ++destination;
  }
}

/**
 * Address: 0x005812C0 (FUN_005812C0)
 *
 * What it does:
 * Recursively destroys one linked tree lane and patches both back-reference
 * chains stored in words `+0x14/+0x18` and `+0x1C/+0x20`.
 */
[[maybe_unused]] void DestroyLinkedTreeNodeRecursiveRuntime(
  LinkedTreeNode37Runtime* node
)
{
  LinkedTreeNode37Runtime* previous = node;
  LinkedTreeNode37Runtime* cursor = node;
  while (cursor != nullptr && !NodeHasSentinelFlag(cursor, 0x25u)) {
    DestroyLinkedTreeNodeRecursiveRuntime(cursor->right);
    cursor = cursor->left;

    PatchBackReferenceChain(previous->lane1C, &previous->lane1C, previous->lane20);
    PatchBackReferenceChain(previous->lane14, &previous->lane14, previous->lane18);
    ::operator delete(previous);
    previous = cursor;
  }
}

/**
 * Address: 0x005CC2D0 (FUN_005CC2D0)
 *
 * What it does:
 * Initializes `count` contiguous lanes with stride `0x34` using one
 * caller-supplied construction callback.
 */
[[maybe_unused]] std::int32_t ConstructStride52RangeRuntime(
  std::int32_t count,
  std::byte* destination,
  const void* const sourceContext,
  const LaneConstructFn52 constructFn
)
{
  std::int32_t constructed = 0;
  while (count > 0) {
    if (destination != nullptr && constructFn != nullptr) {
      constructFn(destination, sourceContext);
      ++constructed;
    }
    destination += 52;
    --count;
  }
  return constructed;
}

/**
 * Address: 0x005D02F0 (FUN_005D02F0)
 *
 * What it does:
 * Allocates one 3-word node and writes `{a1, a2, *a3}` payload lanes.
 */
[[maybe_unused]] TripleIntNodeRuntime* AllocateTripleIntNodeRuntime(
  const std::int32_t lane0,
  const std::int32_t lane4,
  const std::int32_t* const lane8Source
)
{
  auto* const node = AllocateZeroedRuntimeNode<TripleIntNodeRuntime>();
  if (node == nullptr) {
    return nullptr;
  }

  node->lane0 = lane0;
  node->lane4 = lane4;
  node->lane8 = lane8Source != nullptr ? *lane8Source : 0;
  return node;
}

/**
 * Address: 0x006874E0 (FUN_006874E0)
 *
 * What it does:
 * Clears one linearized tree list lane by unlinking head sentinels and
 * deleting each chained node.
 */
[[maybe_unused]] LinearTreeNodeRuntime* ClearLinearTreeStorageRuntime(
  LinearTreeStorageRuntime* const storage
)
{
  if (storage == nullptr || storage->head == nullptr) {
    if (storage != nullptr) {
      storage->size = 0u;
    }
    return nullptr;
  }

  LinearTreeNodeRuntime* const head = storage->head;
  LinearTreeNodeRuntime* cursor = head->next;
  head->next = head;
  head->prev = head;
  storage->size = 0u;

  while (cursor != nullptr && cursor != head) {
    LinearTreeNodeRuntime* const next = cursor->next;
    ::operator delete(cursor);
    cursor = next;
  }

  return cursor;
}

/**
 * Address: 0x00687BC0 (FUN_00687BC0)
 *
 * What it does:
 * Allocates one IdPool-map node lane with cleared links and sentinel-state
 * flags (`+0x14=1`, `+0x15=0`).
 */
[[maybe_unused]] RbNodeFlag21Runtime* AllocateIdPoolMapNodeRuntime()
{
  auto* const node = AllocateZeroedRuntimeNode<RbNodeFlag21Runtime>();
  if (node == nullptr) {
    return nullptr;
  }

  node->left = nullptr;
  node->parent = nullptr;
  node->right = nullptr;
  node->sentinel20 = 1u;
  node->isNil21 = 0u;
  return node;
}

/**
 * Address: 0x00688180 (FUN_00688180)
 *
 * What it does:
 * Allocates one `map<uint, IdPool>` node lane with cleared links and
 * sentinel-state flags (`+0x14=1`, `+0x15=0`).
 */
[[maybe_unused]] RbNodeFlag21Runtime* AllocateUintIdPoolMapNodeRuntime()
{
  auto* const node = AllocateZeroedRuntimeNode<RbNodeFlag21Runtime>();
  if (node == nullptr) {
    return nullptr;
  }

  node->left = nullptr;
  node->parent = nullptr;
  node->right = nullptr;
  node->sentinel20 = 1u;
  node->isNil21 = 0u;
  return node;
}

/**
 * Address: 0x006AFBF0 (FUN_006AFBF0)
 *
 * What it does:
 * Returns lower-bound candidate node for one key in a string->float RB-tree.
 */
[[maybe_unused]] StringFloatMapNodeRuntime* LowerBoundStringFloatMapRuntime(
  StringFloatMapRuntime* const map,
  const std::string* const key
)
{
  if (map == nullptr || map->head == nullptr) {
    return nullptr;
  }

  const std::string emptyKey;
  const std::string& lookupKey = key != nullptr ? *key : emptyKey;

  StringFloatMapNodeRuntime* candidate = map->head;
  StringFloatMapNodeRuntime* cursor = map->head->parent;
  while (cursor != nullptr && cursor->isNil == 0u) {
    const int compare = cursor->key.compare(lookupKey);
    if (compare >= 0) {
      candidate = cursor;
      cursor = cursor->left;
    } else {
      cursor = cursor->right;
    }
  }
  return candidate;
}

/**
 * Address: 0x006DF040 (FUN_006DF040)
 *
 * What it does:
 * Resets one swap-backed dynamic array lane (`+0x10` storage block) to fallback
 * storage and refreshes cached lanes.
 */
[[maybe_unused]] std::uint32_t ResetSwapBackedArrayRuntimeB(
  SwapBackedArrayRuntimeB* const runtime
)
{
  if (runtime == nullptr) {
    return 0u;
  }

  if (runtime->activeBuffer == runtime->fallbackBuffer) {
    runtime->cursor = runtime->activeBuffer;
    return runtime->activeBuffer != nullptr ? *runtime->activeBuffer : 0u;
  }

  ::operator delete[](runtime->activeBuffer);
  runtime->activeBuffer = runtime->fallbackBuffer;
  runtime->cachedFirst = runtime->activeBuffer != nullptr ? *runtime->activeBuffer : 0u;
  runtime->cursor = runtime->activeBuffer;
  return runtime->cachedFirst;
}

/**
 * Address: 0x006E2840 (FUN_006E2840)
 *
 * What it does:
 * Allocates one command-db map node lane with cleared links and sentinel-state
 * flags (`+0x14=1`, `+0x15=0`).
 */
[[maybe_unused]] RbNodeFlag21Runtime* AllocateCommandDbMapNodeRuntime()
{
  auto* const node = AllocateZeroedRuntimeNode<RbNodeFlag21Runtime>();
  if (node == nullptr) {
    return nullptr;
  }

  node->left = nullptr;
  node->parent = nullptr;
  node->right = nullptr;
  node->sentinel20 = 1u;
  node->isNil21 = 0u;
  return node;
}

/**
 * Address: 0x00703A10 (FUN_00703A10)
 *
 * What it does:
 * Allocates one RB-tree node lane with null links and marks it as
 * sentinel-root style (`+0x2C=1`, `+0x2D=0`).
 */
[[maybe_unused]] RbNodeFlag45Runtime* AllocateArmyStatsTreeNodeRuntime()
{
  auto* const node = AllocateZeroedRuntimeNode<RbNodeFlag45Runtime>();
  if (node == nullptr) {
    return nullptr;
  }

  node->left = nullptr;
  node->parent = nullptr;
  node->right = nullptr;
  node->sentinel44 = 1u;
  node->isNil45 = 0u;
  return node;
}

/**
 * Address: 0x00703C30 (FUN_00703C30)
 *
 * What it does:
 * Recursively destroys one RB-tree lane and releases dynamic string storage
 * when capacity is heap-backed (`capacity >= 16`).
 */
[[maybe_unused]] void DestroyArmyStatsTreeNodeRecursiveRuntime(
  RbNodeFlag45Runtime* const node
)
{
  DestroyRecursiveStringTree(node);
}

/**
 * Address: 0x00705B30 (FUN_00705B30)
 *
 * What it does:
 * Resets one linked owner lane to fallback array storage and unlinks the node
 * from its intrusive next/prev chain.
 */
[[maybe_unused]] LinkedBufferOwnerRuntime* ResetLinkedBufferOwnerRuntime(
  LinkedBufferOwnerRuntime* const owner
)
{
  if (owner == nullptr) {
    return nullptr;
  }

  if (owner->activeBuffer != owner->fallbackBuffer) {
    ::operator delete[](owner->activeBuffer);
    owner->activeBuffer = owner->fallbackBuffer;
    owner->cachedFirst = owner->activeBuffer != nullptr ? *owner->activeBuffer : 0u;
  }
  owner->cursor = owner->activeBuffer;

  LinkedBufferOwnerRuntime* const previous = owner->prev;
  LinkedBufferOwnerRuntime* const next = owner->next;
  if (next != nullptr) {
    next->prev = previous;
  }
  if (previous != nullptr) {
    previous->next = next;
  }

  owner->prev = owner;
  owner->next = owner;
  return previous;
}

/**
 * Address: 0x0070F810 (FUN_0070F810)
 *
 * What it does:
 * Copies one RB-map header lane (size/root) and recomputes cached
 * leftmost/rightmost pointers for `isNil` flag offset `+0x15`.
 */
[[maybe_unused]] RbMapFlag21Runtime* CopyMapHeaderAndExtremaFlag21Runtime(
  RbMapFlag21Runtime* const destination,
  const RbMapFlag21Runtime* const source,
  const CloneTree21Fn cloneFn
)
{
  if (destination == nullptr || source == nullptr || destination->head == nullptr || source->head == nullptr) {
    return destination;
  }

  RbNodeFlag21Runtime* sourceRoot = source->head->parent;
  if (cloneFn != nullptr && !NodeHasSentinelFlag(sourceRoot, 0x15u)) {
    sourceRoot = cloneFn(sourceRoot, destination->head);
  }
  destination->head->parent = sourceRoot;
  destination->size = source->size;
  RecomputeHeadExtrema(destination->head, 0x15u);
  return destination;
}

/**
 * Address: 0x00711EE0 (FUN_00711EE0)
 *
 * What it does:
 * Allocates one compact RB-tree node lane with null links and sentinel-state
 * flags (`+0x10=1`, `+0x11=0`).
 */
[[maybe_unused]] RbNodeFlag17Runtime* AllocateCompactTreeNodeRuntime()
{
  auto* const node = AllocateZeroedRuntimeNode<RbNodeFlag17Runtime>();
  if (node == nullptr) {
    return nullptr;
  }

  node->left = nullptr;
  node->parent = nullptr;
  node->right = nullptr;
  node->sentinel16 = 1u;
  node->isNil17 = 0u;
  return node;
}

/**
 * Address: 0x00720010 (FUN_00720010)
 *
 * What it does:
 * Performs one sift-down step on a 4-float heap lane and then invokes the
 * caller-provided finalize callback.
 */
[[maybe_unused]] std::int32_t SiftDownFloat4HeapAndFinalizeRuntime(
  std::int32_t heapIndex,
  const std::int32_t heapLast,
  Float4Runtime* const heapBase,
  const std::int32_t arg4,
  const std::int32_t arg5,
  const std::int32_t arg6,
  const std::int32_t arg7,
  const Float4FinalizeFn finalizeFn
)
{
  if (heapBase == nullptr) {
    return finalizeFn != nullptr ? finalizeFn(heapBase, arg4, arg5, arg6, arg7) : 0;
  }

  std::int32_t child = (heapIndex * 2) + 2;
  while (child < heapLast) {
    if (heapBase[child].lanes[3] > heapBase[child - 1].lanes[3]) {
      --child;
    }

    heapBase[heapIndex] = heapBase[child];
    heapIndex = child;
    child = (child * 2) + 2;
  }

  if (child == heapLast) {
    heapBase[heapIndex] = heapBase[heapLast - 1];
  }

  return finalizeFn != nullptr ? finalizeFn(heapBase, arg4, arg5, arg6, arg7) : 0;
}

/**
 * Address: 0x00739E50 (FUN_00739E50)
 *
 * What it does:
 * Allocates one doubly-linked sentinel lane and self-links both first words.
 */
[[maybe_unused]] LinearTreeNodeRuntime* AllocateSelfLinkedPairNodeRuntime()
{
  auto* const node = AllocateZeroedRuntimeNode<LinearTreeNodeRuntime>();
  if (node == nullptr) {
    return nullptr;
  }

  node->next = node;
  node->prev = node;
  return node;
}

/**
 * Address: 0x007407F0 (FUN_007407F0)
 *
 * What it does:
 * Forwards one owner range `[begin, begin + size)` into the supplied range
 * erase callback.
 */
[[maybe_unused]] std::int32_t EraseOwnerRangeRuntime(
  RangeOwnerByteRuntime* const owner,
  const RangeEraseRuntimeFn eraseFn
)
{
  if (owner == nullptr || eraseFn == nullptr) {
    return 0;
  }

  std::byte* const begin = owner->rangeBegin;
  return eraseFn(owner, begin, owner, begin + owner->rangeByteSize);
}

/**
 * Address: 0x00740860 (FUN_00740860)
 *
 * What it does:
 * Pure forwarding thunk to one owner cleanup callback.
 */
[[maybe_unused]] void ForwardOwnerCleanupThunkRuntime(
  void* const owner,
  const ForwardCleanupFn cleanupFn
)
{
  if (cleanupFn != nullptr) {
    cleanupFn(owner);
  }
}

/**
 * Address: 0x00767C70 (FUN_00767C70)
 *
 * What it does:
 * Collapses one tagged-insert cursor (`end = begin` when non-empty) and emits
 * one tagged insert call with key `9`.
 */
[[maybe_unused]] std::int32_t ResetCursorAndInsertTaggedWordRuntime(
  const std::uint32_t* const value,
  TaggedInsertCursorRuntime* const cursor,
  const TaggedInsertRuntimeFn insertFn
)
{
  if (value == nullptr || cursor == nullptr) {
    return 0;
  }

  std::uint32_t localValue = *value;
  if (cursor->begin != cursor->end) {
    cursor->end = cursor->begin;
  }

  if (insertFn != nullptr) {
    return insertFn(cursor->begin, 9u, &localValue);
  }

  if (cursor->begin != nullptr) {
    *cursor->begin = localValue;
    return static_cast<std::int32_t>(localValue);
  }
  return 0;
}

/**
 * Address: 0x0076A2E0 (FUN_0076A2E0)
 *
 * What it does:
 * Allocates one payload node lane and copies two integer lanes plus seven
 * float lanes from source.
 */
[[maybe_unused]] FloatPayloadNodeRuntime* AllocateFloatPayloadNodeRuntime(
  const float* const sourceFloats,
  const std::int32_t lane0,
  const std::int32_t lane4
)
{
  auto* const node = AllocateZeroedRuntimeNode<FloatPayloadNodeRuntime>();
  if (node == nullptr) {
    return nullptr;
  }

  node->lane0 = lane0;
  node->lane4 = lane4;
  if (sourceFloats != nullptr) {
    for (std::size_t index = 0; index < 7u; ++index) {
      node->lanes[index] = sourceFloats[index];
    }
  }
  return node;
}

/**
 * Address: 0x0077CAA0 (FUN_0077CAA0)
 *
 * What it does:
 * Allocates one decal-buffer tree node lane with null links and sentinel-state
 * flags (`+0x1C=1`, `+0x1D=0`).
 */
[[maybe_unused]] RbNodeFlag29Runtime* AllocateDecalBufferTreeNodeRuntime()
{
  auto* const node = AllocateZeroedRuntimeNode<RbNodeFlag29Runtime>();
  if (node == nullptr) {
    return nullptr;
  }

  node->left = nullptr;
  node->parent = nullptr;
  node->right = nullptr;
  node->sentinel28 = 1u;
  node->isNil29 = 0u;
  return node;
}

/**
 * Address: 0x0077CC20 (FUN_0077CC20)
 *
 * What it does:
 * Recursively destroys one compact RB-tree lane where `isNil` lives at
 * offset `+0x11`.
 */
[[maybe_unused]] void DestroyCompactTreeNodeRecursiveRuntime(
  RbNodeFlag17Runtime* node
)
{
  RbNodeFlag17Runtime* previous = node;
  RbNodeFlag17Runtime* cursor = node;
  while (cursor != nullptr && cursor->isNil17 == 0u) {
    DestroyCompactTreeNodeRecursiveRuntime(cursor->right);
    cursor = cursor->left;
    ::operator delete(previous);
    previous = cursor;
  }
}

/**
 * Address: 0x00A9A4B1 (FUN_00A9A4B1)
 *
 * What it does:
 * Maps math-domain/range classification codes into `errno` (`EDOM`/`ERANGE`)
 * and returns the original pointer lane.
 */
[[maybe_unused]] int* MapErrnoForMathInputRuntime(
  const int classificationCode
)
{
  int* const result = reinterpret_cast<int*>(static_cast<std::uintptr_t>(classificationCode));
  if (classificationCode == 1) {
    *_errno() = EDOM;
  } else if (classificationCode > 1 && classificationCode <= 3) {
    *_errno() = ERANGE;
  }
  return result;
}

/**
 * Address: 0x006F8F10 (FUN_006F8F10)
 *
 * What it does:
 * Iterates one pointer-word range and adds each resolved `Unit*` lane into one
 * unit-set container.
 */
[[maybe_unused]] void AddUnitRangeFromPointerWordsRuntime(
  moho::SEntitySetTemplateUnit* const unitSet,
  const std::uint32_t* pointerBegin,
  const std::uint32_t* const pointerEnd
)
{
  if (unitSet == nullptr) {
    return;
  }

  while (pointerBegin != pointerEnd) {
    moho::Unit* unit = nullptr;
    if (*pointerBegin != 0u) {
      unit = reinterpret_cast<moho::Unit*>(static_cast<std::uintptr_t>(*pointerBegin) - 8u);
    }
    (void)unitSet->AddUnit(unit);
    ++pointerBegin;
  }
}

/**
 * Address: 0x00686E80 (FUN_00686E80)
 *
 * What it does:
 * Appends one integer lane into a legacy vector payload.
 */
[[maybe_unused]] std::int32_t* AppendLegacyIntVectorLaneRuntime(
  LegacyVectorStorageRuntime<std::int32_t>* const vector,
  const std::int32_t* const value
)
{
  if (vector == nullptr || value == nullptr) {
    return nullptr;
  }

  std::int32_t* const inserted = AppendTrivialValue(vector, *value);
  return inserted != nullptr ? inserted + 1 : vector->end;
}

/**
 * Address: 0x00982080 (FUN_00982080)
 *
 * What it does:
 * Resolves one owner-backed string-array index, advances by one lane, and
 * returns the mapped value when in range.
 */
[[maybe_unused]] std::uint32_t* ResolveWxOwnerArrayValueRuntime(
  std::uint32_t* const outValue,
  const std::uint32_t* const objectHandleWord
)
{
  if (outValue == nullptr) {
    return nullptr;
  }

  *outValue = 0u;
  if (objectHandleWord == nullptr || *objectHandleWord == 0u) {
    return outValue;
  }

  const std::uintptr_t objectAddress = static_cast<std::uintptr_t>(*objectHandleWord);
  const auto* const owner = *reinterpret_cast<WxLookupOwnerRuntime* const*>(objectAddress + 36u);
  if (owner == nullptr || owner->arrayLane == nullptr) {
    return outValue;
  }

  const auto* const array = owner->arrayLane;
  const wchar_t* const lookupValue = reinterpret_cast<const wchar_t*>(objectAddress);
  std::size_t index = static_cast<std::size_t>(array->count);
  for (std::size_t i = 0; i < static_cast<std::size_t>(array->count); ++i) {
    const wchar_t* const entry = array->entries != nullptr ? array->entries[i] : nullptr;
    if ((entry == lookupValue) || (entry != nullptr && lookupValue != nullptr && std::wcscmp(entry, lookupValue) == 0)) {
      index = i;
      break;
    }
  }

  const std::size_t mappedIndex = index + 1u;
  if (mappedIndex >= static_cast<std::size_t>(array->count) || array->entries == nullptr) {
    *outValue = 0u;
    return outValue;
  }

  *outValue = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(array->entries[mappedIndex]));
  return outValue;
}

/**
 * Address: 0x0056E7E0 (FUN_0056E7E0)
 *
 * What it does:
 * Normalizes one contiguous formation lane range by restoring each record to
 * fallback storage and updating cached cursors.
 */
[[maybe_unused]] std::uint32_t* NormalizeFormationLaneRangeRuntime(
  std::uint32_t* laneBegin,
  std::uint32_t* const laneEnd
)
{
  std::uint32_t* lastResult = laneBegin;
  while (laneBegin != laneEnd) {
    const std::uint32_t activeWord = laneBegin[12];
    const std::uint32_t fallbackWord = laneBegin[15];
    if (activeWord != fallbackWord) {
      ::operator delete[](reinterpret_cast<void*>(static_cast<std::uintptr_t>(activeWord)));
      laneBegin[12] = fallbackWord;
      const auto* const fallback = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(fallbackWord));
      laneBegin[14] = fallback != nullptr ? *fallback : 0u;
      lastResult = reinterpret_cast<std::uint32_t*>(laneBegin[14]);
    }

    laneBegin[13] = laneBegin[12];
    laneBegin += 18;
  }
  return lastResult;
}

/**
 * Address: 0x009600E0 (FUN_009600E0)
 *
 * What it does:
 * Parses one full wide-string double lane and reports strict-consume success.
 */
[[maybe_unused]] bool ParseWideDoubleStrictRuntime(
  const wchar_t** const sourceText,
  double* const outValue
)
{
  if (sourceText == nullptr || outValue == nullptr || *sourceText == nullptr) {
    return false;
  }

  const wchar_t* const begin = *sourceText;
  wchar_t* end = nullptr;
  *outValue = std::wcstod(begin, &end);
  return end != begin && end != nullptr && *end == L'\0';
}

/**
 * Address: 0x00960040 (FUN_00960040)
 *
 * What it does:
 * Parses one full wide-string integer lane using the supplied radix and
 * reports strict-consume success.
 */
[[maybe_unused]] bool ParseWideLongStrictRuntime(
  const wchar_t** const sourceText,
  std::uint32_t* const outValue,
  const std::uint32_t radix
)
{
  if (sourceText == nullptr || outValue == nullptr || *sourceText == nullptr) {
    return false;
  }

  const wchar_t* const begin = *sourceText;
  wchar_t* end = nullptr;
  *outValue = static_cast<std::uint32_t>(std::wcstol(begin, &end, static_cast<int>(radix)));
  return end != begin && end != nullptr && *end == L'\0';
}

/**
 * Address: 0x007AE180 (FUN_007AE180)
 *
 * What it does:
 * Initializes one command-mode RB-tree storage head and resets size lanes.
 */
[[maybe_unused]] std::uint32_t InitializeCommandModeTreeRuntime(
  std::uint8_t* const ownerBytes,
  RbNodeFlag25Runtime* (*const allocateNodeFn)()
)
{
  if (ownerBytes == nullptr || allocateNodeFn == nullptr) {
    return 0u;
  }

  RbNodeFlag25Runtime* const head = allocateNodeFn();
  if (head == nullptr) {
    return 0u;
  }

  *reinterpret_cast<RbNodeFlag25Runtime**>(ownerBytes + 4u) = head;
  head->isNil25 = 1u;
  head->parent = head;
  head->left = head;
  head->right = head;
  *reinterpret_cast<std::uint32_t*>(ownerBytes + 8u) = 0u;
  return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ownerBytes));
}

/**
 * Address: 0x009A0A10 (FUN_009A0A10)
 *
 * What it does:
 * Resolves one runtime object from context and dispatches virtual slot `+0x20`
 * when available.
 */
[[maybe_unused]] int DispatchResolvedObjectSlot32Runtime(
  const int context,
  const int dispatchArg,
  void* (*const resolveFn)(int, int)
)
{
  if (resolveFn == nullptr) {
    return 0;
  }

  void* const object = resolveFn(context, 0);
  if (object == nullptr) {
    return 0;
  }

  using DispatchFn = int (__thiscall*)(void*, int);
  auto* const vtable = *reinterpret_cast<void***>(object);
  const auto dispatch = reinterpret_cast<DispatchFn>(vtable[8]);
  return dispatch != nullptr ? dispatch(object, dispatchArg) : 0;
}

/**
 * Address: 0x009D3710 (FUN_009D3710)
 *
 * What it does:
 * Window-proc hook lane that forwards selected messages through one object
 * dispatcher before chaining to the previous window procedure.
 */
[[maybe_unused]] LRESULT CALLBACK DispatchWindowMessageHookRuntime(
  HWND window,
  UINT message,
  WPARAM wParam,
  LPARAM lParam
)
{
  auto* const runtime = reinterpret_cast<DispatchWindowRuntime*>(::GetWindowLongW(window, GWL_USERDATA));
  if (runtime == nullptr) {
    return 0;
  }

  switch (message) {
  case WM_SETFOCUS:
    if (runtime->previousWindowProc == reinterpret_cast<WNDPROC>(wParam)) {
      break;
    }
    [[fallthrough]];
  case WM_KILLFOCUS:
  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_CHAR:
  case WM_DEADCHAR: {
    using MsgDispatchFn = void (__thiscall*)(void*, UINT, WPARAM, LPARAM);
    auto* const vtable = runtime->vtable;
    const auto dispatch = reinterpret_cast<MsgDispatchFn>(vtable[124]);
    if (dispatch != nullptr) {
      dispatch(runtime, message, wParam, lParam);
      if (!::IsWindow(window) || reinterpret_cast<DispatchWindowRuntime*>(::GetWindowLongW(window, GWL_USERDATA)) != runtime) {
        return 0;
      }
    }
    break;
  }
  case WM_GETDLGCODE:
    return 128;
  default:
    break;
  }

  return ::CallWindowProcW(runtime->previousWindowProc, window, message, wParam, lParam);
}

/**
 * Address: 0x009C7700 (FUN_009C7700)
 *
 * What it does:
 * Routes profile-string writes to global-profile or private-profile API based
 * on whether target filename equals `L\"Default\"`.
 */
[[maybe_unused]] BOOL WriteProfileStringDispatchRuntime(
  LPCWSTR* const section,
  LPCWSTR* const key,
  LPCWSTR* const value,
  LPCWSTR* const fileName
)
{
  static const wchar_t kDefaultName[] = L"Default";

  const LPCWSTR sectionText = section != nullptr ? *section : nullptr;
  const LPCWSTR keyText = key != nullptr ? *key : nullptr;
  const LPCWSTR valueText = value != nullptr ? *value : nullptr;
  const LPCWSTR fileText = fileName != nullptr ? *fileName : nullptr;

  if (fileText != nullptr && std::wcscmp(fileText, kDefaultName) == 0) {
    return ::WriteProfileStringW(sectionText, keyText, valueText);
  }
  return ::WritePrivateProfileStringW(sectionText, keyText, valueText, fileText);
}

/**
 * Address: 0x00A39EE0 (FUN_00A39EE0)
 *
 * What it does:
 * Initializes one float-distance runtime lane with default tolerance/clamp
 * bounds.
 */
[[maybe_unused]] DistanceVector2fRuntime* InitializeDistanceVector2fRuntime(
  DistanceVector2fRuntime* const runtime,
  void* const vtableToken
)
{
  if (runtime == nullptr) {
    return nullptr;
  }

  runtime->vtable = vtableToken;
  runtime->dimension = 8;
  runtime->epsilon = 0.000001f;
  runtime->minClamp = 0.001f;
  runtime->maxClamp = 499.99997f;
  runtime->hasRawResult = 0u;
  runtime->initialDistance = std::numeric_limits<float>::max();
  runtime->hasFinalResult = 0u;
  return runtime;
}

/**
 * Address: 0x00A39F60 (FUN_00A39F60)
 *
 * What it does:
 * Initializes one double-distance runtime lane with default tolerance/clamp
 * bounds.
 */
[[maybe_unused]] DistanceVector2dRuntime* InitializeDistanceVector2dRuntime(
  DistanceVector2dRuntime* const runtime,
  void* const vtableToken
)
{
  if (runtime == nullptr) {
    return nullptr;
  }

  runtime->vtable = vtableToken;
  runtime->dimension = 8;
  runtime->epsilon = 0.00000001;
  runtime->minClamp = 0.001;
  runtime->maxClamp = 500.0;
  runtime->hasRawResult = 0u;
  runtime->initialDistance = std::numeric_limits<double>::max();
  runtime->hasFinalResult = 0u;
  return runtime;
}

/**
 * Address: 0x00A4BB00 (FUN_00A4BB00)
 *
 * What it does:
 * Computes one triangle-derived closest-point lane and squared distance; emits
 * `DBL_MAX` sentinel output when determinant is under tolerance.
 */
[[maybe_unused]] double* ComputeTriangleClosestPointRuntime(
  const double* const toleranceLane,
  double* const outPoint4,
  const double* const p3,
  const double* const p4,
  const double* const p5
)
{
  if (toleranceLane == nullptr || outPoint4 == nullptr || p3 == nullptr || p4 == nullptr || p5 == nullptr) {
    return outPoint4;
  }

  const double v5 = p3[0] - p5[0];
  const double v6 = p3[1] - p5[1];
  const double v24 = p3[2] - p5[2];
  const double v20 = p4[0] - p5[0];
  const double v21 = p4[1] - p5[1];
  const double v22 = p4[2] - p5[2];
  const double v7 = (v24 * v22) + (v20 * v5) + (v21 * v6);
  const double v8 = (v5 * v5) + (v6 * v6) + (v24 * v24);
  const double v9 = (v21 * v21) + (v20 * v20) + (v22 * v22);
  const long double determinant = static_cast<long double>((v9 * v8) - (v7 * v7));

  if (std::fabs(static_cast<double>(determinant)) <= *toleranceLane) {
    outPoint4[0] = std::numeric_limits<double>::max();
    outPoint4[1] = std::numeric_limits<double>::max();
    outPoint4[2] = std::numeric_limits<double>::max();
    outPoint4[3] = std::numeric_limits<double>::max();
    return outPoint4;
  }

  const long double halfInvDeterminant = 0.5L / determinant;
  const long double baryS = static_cast<long double>((v8 - v7)) * (halfInvDeterminant * static_cast<long double>(v9));
  const long double baryT = static_cast<long double>(v8) * halfInvDeterminant * static_cast<long double>(v9 - v7);
  const long double baryU = 1.0L - baryS - baryT;

  const long double x = (static_cast<long double>(p5[0]) * baryU)
                      + (static_cast<long double>(p3[0]) * baryS)
                      + (static_cast<long double>(p4[0]) * baryT);
  const long double y = (static_cast<long double>(p5[1]) * baryU)
                      + (static_cast<long double>(p3[1]) * baryS)
                      + (static_cast<long double>(p4[1]) * baryT);
  const long double z = (static_cast<long double>(p5[2]) * baryU)
                      + (static_cast<long double>(p3[2]) * baryS)
                      + (static_cast<long double>(p4[2]) * baryT);

  outPoint4[0] = static_cast<double>(x);
  outPoint4[1] = static_cast<double>(y);
  outPoint4[2] = static_cast<double>(z);

  const long double dz = (baryS * static_cast<long double>(v24)) + (baryT * static_cast<long double>(v22));
  const long double dx = (static_cast<long double>(v20) * baryT) + (static_cast<long double>(v5) * baryS);
  const long double dy = (static_cast<long double>(v21) * baryT) + (static_cast<long double>(v6) * baryS);
  outPoint4[3] = static_cast<double>((dz * dz) + (dx * dx) + (dy * dy));
  return outPoint4;
}

/**
 * Address: 0x00570410 (FUN_00570410)
 *
 * What it does:
 * Advances one RB-tree iterator lane using sentinel flag offset `+0x19`.
 */
[[maybe_unused]] RbNodeFlag25Runtime* AdvanceTreeIteratorFlag25Runtime(
  const std::uint32_t /*unused*/,
  RbNodeFlag25Runtime** const iteratorLane
)
{
  return AdvanceRbIteratorRuntime<RbNodeFlag25Runtime, 0x19u>(iteratorLane);
}

/**
 * Address: 0x006B02E0 (FUN_006B02E0)
 *
 * What it does:
 * Advances one RB-tree iterator lane using sentinel flag offset `+0x2D`.
 */
[[maybe_unused]] RbNodeFlag45Runtime* AdvanceTreeIteratorFlag45RuntimeA(
  const std::uint32_t /*unused*/,
  RbNodeFlag45Runtime** const iteratorLane
)
{
  return AdvanceRbIteratorRuntime<RbNodeFlag45Runtime, 0x2Du>(iteratorLane);
}

/**
 * Address: 0x007CA280 (FUN_007CA280)
 *
 * What it does:
 * Advances one map iterator lane using sentinel flag offset `+0x15`.
 */
[[maybe_unused]] void AdvanceMapIteratorFlag21RuntimeA(
  RbNodeFlag21Runtime** const iteratorLane
)
{
  (void)AdvanceRbIteratorRuntime<RbNodeFlag21Runtime, 0x15u>(iteratorLane);
}

/**
 * Address: 0x007F2CD0 (FUN_007F2CD0)
 *
 * What it does:
 * Advances one string-map iterator lane using sentinel flag offset `+0x2D`.
 */
[[maybe_unused]] RbNodeFlag45Runtime* AdvanceRangeExtractorIteratorRuntime(
  RbNodeFlag45Runtime** const iteratorLane
)
{
  return AdvanceRbIteratorRuntime<RbNodeFlag45Runtime, 0x2Du>(iteratorLane);
}

/**
 * Address: 0x0083C0B0 (FUN_0083C0B0)
 *
 * What it does:
 * Advances one RB-tree iterator lane using sentinel flag offset `+0x2D`.
 */
[[maybe_unused]] RbNodeFlag45Runtime* AdvanceTreeIteratorFlag45RuntimeB(
  const std::uint32_t /*unused*/,
  RbNodeFlag45Runtime** const iteratorLane
)
{
  return AdvanceRbIteratorRuntime<RbNodeFlag45Runtime, 0x2Du>(iteratorLane);
}

/**
 * Address: 0x008495B0 (FUN_008495B0)
 *
 * What it does:
 * Advances one entity-map iterator lane using sentinel flag offset `+0x15`.
 */
[[maybe_unused]] void AdvanceMapIteratorFlag21RuntimeB(
  RbNodeFlag21Runtime** const iteratorLane
)
{
  (void)AdvanceRbIteratorRuntime<RbNodeFlag21Runtime, 0x15u>(iteratorLane);
}

/**
 * Address: 0x0085F740 (FUN_0085F740)
 *
 * What it does:
 * Copies one 0x31-byte payload lane and increments embedded intrusive
 * reference counters (`+0x18`, `+0x20`, `+0x28`) when present.
 */
[[maybe_unused]] RefCountedPayload49Runtime* CopyRefCountedPayload49Runtime(
  RefCountedPayload49Runtime* const destination,
  const RefCountedPayload49Runtime* const source
)
{
  if (destination == nullptr || source == nullptr) {
    return destination;
  }

  *destination = *source;
  if (destination->ref18 != 0u) {
    (void)::InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(destination->ref18 + 4u), 1);
  }
  if (destination->ref20 != 0u) {
    (void)::InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(destination->ref20 + 4u), 1);
  }
  if (destination->ref28 != 0u) {
    (void)::InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(destination->ref28 + 4u), 1);
  }
  return destination;
}

/**
 * Address: 0x00899940 (FUN_00899940)
 *
 * What it does:
 * Advances one RB-tree iterator lane using sentinel flag offset `+0x2D`.
 */
[[maybe_unused]] void AdvanceTreeIteratorFlag45RuntimeC(
  RbNodeFlag45Runtime** const iteratorLane
)
{
  (void)AdvanceRbIteratorRuntime<RbNodeFlag45Runtime, 0x2Du>(iteratorLane);
}

/**
 * Address: 0x009A8550 (FUN_009A8550)
 *
 * What it does:
 * Finds one integer value lane in a runtime array either forward or reverse.
 */
[[maybe_unused]] int FindIntArrayIndexRuntime(
  const IntArrayLookupRuntime* const runtime,
  const int value,
  const bool reverseSearch
)
{
  if (runtime == nullptr || runtime->values == nullptr || runtime->count == 0u) {
    return -1;
  }

  if (reverseSearch) {
    for (int index = static_cast<int>(runtime->count) - 1; index >= 0; --index) {
      if (runtime->values[index] == value) {
        return index;
      }
    }
    return -1;
  }

  for (std::uint32_t index = 0; index < runtime->count; ++index) {
    if (runtime->values[index] == value) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

/**
 * Address: 0x009B36D0 (FUN_009B36D0)
 *
 * What it does:
 * Dispatches virtual slot `+0x2C` on one polymorphic runtime object.
 */
[[maybe_unused]] int DispatchVirtualSlot44Runtime(
  VirtualDispatch44Runtime* const runtime
)
{
  if (runtime == nullptr || runtime->vtable == nullptr) {
    return 0;
  }

  using SlotFn = int (__thiscall*)(void*);
  const auto fn = reinterpret_cast<SlotFn>(runtime->vtable[11]);
  return fn != nullptr ? fn(runtime) : 0;
}

/**
 * Address: 0x00A6D420 (FUN_00A6D420)
 *
 * What it does:
 * Snapshots one 2x2 float basis lane into outputs and resets the live basis to
 * identity.
 */
[[maybe_unused]] std::uint32_t SnapshotAndResetBasis2fRuntime(
  BasisPointerResetRuntimeF* const runtime
)
{
  if (runtime == nullptr || runtime->basisPair == nullptr || runtime->outputPrimary == nullptr || runtime->outputSecondary == nullptr) {
    return 0u;
  }

  float* const basis0 = runtime->basisPair[0];
  float* const basis1 = runtime->basisPair[1];
  if (basis0 == nullptr || basis1 == nullptr) {
    return 0u;
  }

  runtime->outputPrimary[0] = basis0[0];
  runtime->outputPrimary[1] = basis1[1];
  runtime->outputSecondary[0] = basis0[1];
  runtime->outputSecondary[1] = 0.0f;

  basis0[0] = 1.0f;
  basis0[1] = 0.0f;
  basis1[0] = 0.0f;
  basis1[1] = 1.0f;
  runtime->wasReset = 1u;
  return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(runtime->basisPair));
}

/**
 * Address: 0x00A6EF10 (FUN_00A6EF10)
 *
 * What it does:
 * Snapshots one 2x2 double basis lane into outputs and resets the live basis
 * to identity.
 */
[[maybe_unused]] std::uint32_t SnapshotAndResetBasis2dRuntime(
  BasisPointerResetRuntimeD* const runtime
)
{
  if (runtime == nullptr || runtime->basisPair == nullptr || runtime->outputPrimary == nullptr || runtime->outputSecondary == nullptr) {
    return 0u;
  }

  double* const basis0 = runtime->basisPair[0];
  double* const basis1 = runtime->basisPair[1];
  if (basis0 == nullptr || basis1 == nullptr) {
    return 0u;
  }

  runtime->outputPrimary[0] = basis0[0];
  runtime->outputPrimary[1] = basis1[1];
  runtime->outputSecondary[0] = basis0[1];
  runtime->outputSecondary[1] = 0.0;

  basis0[0] = 1.0;
  basis0[1] = 0.0;
  basis1[0] = 0.0;
  basis1[1] = 1.0;
  runtime->wasReset = 1u;
  return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(runtime->basisPair));
}

/**
 * Address: 0x00AA7C48 (FUN_00AA7C48)
 *
 * What it does:
 * Returns the IEEE-754 exponent-mask lane for one double, or full high dword
 * when the value is NaN/Inf-class.
 */
[[maybe_unused]] std::uint32_t ClassifyDoubleExponentMaskRuntime(
  const double value
)
{
  const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
  const std::uint32_t highWord = static_cast<std::uint32_t>(bits >> 32u);
  const std::uint32_t exponentMask = highWord & 0x7FF00000u;
  return exponentMask == 0x7FF00000u ? highWord : exponentMask;
}

/**
 * Address: 0x00549AD0 (FUN_00549AD0)
 *
 * What it does:
 * Copies one `[source, end)` lane sequence of 5-dword records into
 * destination.
 */
[[maybe_unused]] std::uint32_t* CopyStride5DwordRangeRuntime(
  std::uint32_t* destination,
  const std::uint32_t* const sourceEnd,
  const std::uint32_t* source
)
{
  while (source != sourceEnd) {
    if (destination != nullptr) {
      destination[0] = source[0];
      destination[1] = source[1];
      destination[2] = source[2];
      destination[3] = source[3];
      destination[4] = source[4];
    }
    source += 5;
    if (destination != nullptr) {
      destination += 5;
    }
  }
  return destination;
}

/**
 * Address: 0x0057EA30 (FUN_0057EA30)
 *
 * What it does:
 * Unlinks one dual back-reference lane by patching both owner chains rooted at
 * `+0x08` and `+0x10`.
 */
[[maybe_unused]] std::uint32_t* UnlinkDualBackReferenceRuntime(
  const std::uint32_t nodeAddress
)
{
  auto* const link = reinterpret_cast<LinkPatchRuntime*>(static_cast<std::uintptr_t>(nodeAddress + 8u));
  if (link == nullptr) {
    return nullptr;
  }

  std::uint32_t* result = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(link->lane08));

  if (link->lane10 != 0u) {
    auto* cursor = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(link->lane10));
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(&link->lane10);
    std::uint32_t guard = 0u;
    while (cursor != nullptr && guard < 0x100000u) {
      if (*cursor == static_cast<std::uint32_t>(target)) {
        *cursor = link->lane14;
        break;
      }
      cursor = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(*cursor + 4u));
      ++guard;
    }
  }

  if (link->lane08 != 0u) {
    result = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(link->lane08));
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(&link->lane08);
    std::uint32_t guard = 0u;
    while (result != nullptr && guard < 0x100000u) {
      if (*result == static_cast<std::uint32_t>(target)) {
        *result = link->lane0C;
        break;
      }
      result = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(*result + 4u));
      ++guard;
    }
  }

  return result;
}

/**
 * Address: 0x00584920 (FUN_00584920)
 *
 * What it does:
 * Copies one `[source, sourceEnd)` sequence of 6-float lanes into destination.
 */
[[maybe_unused]] float* CopyStride6FloatRangeRuntime(
  float* destination,
  const float* source,
  const float* const sourceEnd
)
{
  while (source != sourceEnd) {
    if (destination != nullptr) {
      destination[0] = source[0];
      destination[1] = source[1];
      destination[2] = source[2];
      destination[3] = source[3];
      destination[4] = source[4];
      destination[5] = source[5];
      destination += 6;
    }
    source += 6;
  }
  return destination;
}

/**
 * Address: 0x00962B30 (FUN_00962B30)
 *
 * What it does:
 * Rebinds one wx object lane to `wxObject` vtable and drops the shared
 * reference.
 */
[[maybe_unused]] void ResetWxObjectAndUnrefForCloseRuntime(
  WxObjectRuntime* const object,
  void* const wxObjectVtable,
  const WxUnrefFn unrefFn
)
{
  if (object == nullptr) {
    return;
  }

  object->vtable = wxObjectVtable;
  if (unrefFn != nullptr) {
    unrefFn(object);
  }
}

/**
 * Address: 0x00962CA0 (FUN_00962CA0)
 *
 * What it does:
 * Rebinds one wx object lane to `wxObject` vtable and drops the shared
 * reference.
 */
[[maybe_unused]] void ResetWxObjectAndUnrefForQueryEndSessionRuntime(
  WxObjectRuntime* const object,
  void* const wxObjectVtable,
  const WxUnrefFn unrefFn
)
{
  if (object == nullptr) {
    return;
  }

  object->vtable = wxObjectVtable;
  if (unrefFn != nullptr) {
    unrefFn(object);
  }
}

/**
 * Address: 0x00966CC0 (FUN_00966CC0)
 *
 * What it does:
 * Rebinds one wx object lane to `wxObject` vtable and drops the shared
 * reference.
 */
[[maybe_unused]] void ResetWxObjectAndUnrefForEndSessionRuntime(
  WxObjectRuntime* const object,
  void* const wxObjectVtable,
  const WxUnrefFn unrefFn
)
{
  if (object == nullptr) {
    return;
  }

  object->vtable = wxObjectVtable;
  if (unrefFn != nullptr) {
    unrefFn(object);
  }
}

/**
 * Address: 0x0096E0D0 (FUN_0096E0D0)
 *
 * What it does:
 * Initializes one wx font-descriptor lane using caller values and falls back
 * to the normal-font point size when input size is `-1`.
 */
[[maybe_unused]] int InitializeWxFontDescriptorRuntime(
  WxFontDescriptorRuntime* const descriptor,
  std::int32_t pointSize,
  const std::int32_t family,
  const std::int32_t style,
  const std::int32_t weight,
  const std::uint8_t underlined,
  const void* const faceName,
  const std::int32_t encoding,
  const WxGetDefaultPointSizeFn getDefaultPointSizeFn,
  const WxStringAssignFn assignStringFn
)
{
  if (descriptor == nullptr) {
    return 0;
  }

  descriptor->style = style;
  if (pointSize == -1 && getDefaultPointSizeFn != nullptr) {
    pointSize = getDefaultPointSizeFn();
  }

  descriptor->pointSize = pointSize;
  descriptor->family = family;
  descriptor->weight = weight;
  descriptor->style = style;
  descriptor->underlined = underlined;
  if (assignStringFn != nullptr) {
    assignStringFn(descriptor->faceNameStorage, faceName);
  }
  descriptor->encoding = encoding;
  descriptor->lane24 = 0;
  descriptor->lane84 = 0;
  return 0;
}

/**
 * Address: 0x009EE1C0 (FUN_009EE1C0)
 *
 * What it does:
 * Rebinds one wx spin-button lane to its own vtable and forwards destruction
 * to `wxControl`.
 */
[[maybe_unused]] int DestroyWxSpinButtonRuntime(
  WxObjectRuntime* const spinButton,
  void* const wxSpinButtonVtable,
  const WxControlDtorFn wxControlDtorFn
)
{
  if (spinButton == nullptr) {
    return 0;
  }

  spinButton->vtable = wxSpinButtonVtable;
  return wxControlDtorFn != nullptr ? wxControlDtorFn(spinButton) : 0;
}

/**
 * Address: 0x009F7700 (FUN_009F7700)
 *
 * What it does:
 * Releases four wx shared-string lanes in destruction order (`+0x20`, `+0x0C`,
 * `+0x08`, `+0x04`).
 */
[[maybe_unused]] void ReleaseWxSharedStringBundleRuntime(
  WxSharedStringOwnerRuntime* const owner
)
{
  if (owner == nullptr) {
    return;
  }

  ReleaseSharedWxStringLane(owner->stringLane20);
  ReleaseSharedWxStringLane(owner->stringLane0C);
  ReleaseSharedWxStringLane(owner->stringLane08);
  ReleaseSharedWxStringLane(owner->stringLane04);
}

/**
 * Address: 0x00A16360 (FUN_00A16360)
 *
 * What it does:
 * Reads one pair lane from lookup source at `+0x6C` and stores both values
 * scaled down by 10 into `+0x0C/+0x10`.
 */
[[maybe_unused]] void UpdatePairTenthsFromLookupRuntime(
  Div10PairOwnerRuntime* const owner,
  const PairLookupFn lookupFn
)
{
  if (owner == nullptr || lookupFn == nullptr) {
    return;
  }

  std::int32_t pair[2] = {0, 0};
  lookupFn(pair, owner->sourceLane6C);
  owner->lane0C = pair[0] / 10;
  owner->lane10 = pair[1] / 10;
}

/**
 * Address: 0x00A2F550 (FUN_00A2F550)
 *
 * What it does:
 * Runs wx-protocol base initialization with lane `0` and installs the
 * protocol vtable token.
 */
[[maybe_unused]] VtableOnlyRuntime* InitializeWxProtocolRuntime(
  VtableOnlyRuntime* const protocol,
  void* const wxProtocolVtable,
  const WxProtocolInitializeFn initializeFn
)
{
  if (initializeFn != nullptr) {
    initializeFn(0);
  }

  if (protocol != nullptr) {
    protocol->vtable = wxProtocolVtable;
  }
  return protocol;
}

/**
 * Address: 0x00A2F9C0 (FUN_00A2F9C0)
 *
 * What it does:
 * Constructs one wx socket-output stream lane, stores the socket handle, and
 * binds the stream vtable.
 */
[[maybe_unused]] WxSocketOutputStreamRuntime* InitializeWxSocketOutputStreamRuntime(
  WxSocketOutputStreamRuntime* const stream,
  const std::int32_t socketHandle,
  void* const wxSocketOutputStreamVtable,
  const WxSocketOutputBaseCtorFn baseCtorFn
)
{
  if (stream == nullptr) {
    return nullptr;
  }

  if (baseCtorFn != nullptr) {
    baseCtorFn(stream);
  }

  stream->socketHandle = socketHandle;
  stream->vtable = wxSocketOutputStreamVtable;
  return stream;
}

/**
 * Address: 0x00A2FA20 (FUN_00A2FA20)
 *
 * What it does:
 * Constructs one wx socket-input stream lane, stores the socket handle at
 * `+0x18`, and binds the input-stream vtable.
 */
[[maybe_unused]] WxSocketInputStreamRuntime* InitializeWxSocketInputStreamRuntime(
  WxSocketInputStreamRuntime* const stream,
  const std::int32_t socketHandle,
  void* const wxSocketInputStreamVtable,
  const WxInputStreamCtorFn baseCtorFn
)
{
  if (stream == nullptr) {
    return nullptr;
  }

  if (baseCtorFn != nullptr) {
    baseCtorFn(stream);
  }

  stream->socketHandle = socketHandle;
  stream->vtable = wxSocketInputStreamVtable;
  return stream;
}

/**
 * Address: 0x00AC1138 (FUN_00AC1138)
 *
 * What it does:
 * Scales one packed 64-bit floating-point lane (represented as four 16-bit
 * words) by a signed exponent delta and returns the CRT classification code.
 */
[[maybe_unused]] std::int16_t ScalePackedDoubleWordsRuntime(
  std::uint16_t* const words,
  const int exponentDelta,
  const NormalizePackedDoubleFn normalizeFn,
  const double overflowMagnitude
)
{
  if (words == nullptr) {
    return 0;
  }

  const std::uint16_t highWord = words[3];
  std::int16_t exponent = static_cast<std::int16_t>((highWord >> 4u) & 0x7FFu);
  if (exponent == 0x07FF) {
    if ((highWord & 0x000Fu) != 0u || words[2] != 0u || words[1] != 0u || words[0] != 0u) {
      return 2;
    }
    return 1;
  }

  if (exponent == 0) {
    if (normalizeFn == nullptr) {
      return 0;
    }

    exponent = normalizeFn(words);
    if (exponent > 0) {
      return 0;
    }
  }

  if (exponentDelta > 0 && (0x07FF - exponent) <= exponentDelta) {
    double saturated = overflowMagnitude;
    if ((words[3] & 0x8000u) != 0u) {
      saturated = -saturated;
    }
    std::memcpy(words, &saturated, sizeof(saturated));
    return 1;
  }

  if (-exponent < exponentDelta) {
    const std::int32_t adjustedExponent = exponent + exponentDelta;
    const std::uint16_t preserved = static_cast<std::uint16_t>(words[3] & 0x800Fu);
    words[3] = static_cast<std::uint16_t>(preserved | ((adjustedExponent << 4) & 0x7FF0));
    return -1;
  }

  const std::uint16_t sign = static_cast<std::uint16_t>(words[3] & 0x8000u);
  std::uint16_t normalizedHigh = static_cast<std::uint16_t>((words[3] & 0x000Fu) | 0x0010u);
  words[3] = normalizedHigh;

  int shift = exponentDelta + exponent - 1;
  if (static_cast<unsigned int>(shift + 53) > 0x34u) {
    words[3] = sign;
    words[2] = 0u;
    words[1] = 0u;
    words[0] = 0u;
    return 0;
  }

  std::uint16_t sticky = 0u;
  if (shift <= -16) {
    std::uint16_t w2 = words[2];
    std::uint16_t w1 = words[1];
    const int iterations = ((-16 - shift) >> 4) + 1;
    std::uint16_t w3 = normalizedHigh;
    std::uint16_t w0 = words[0];
    const int shifted = shift + (iterations * 16);

    for (int i = 0; i < iterations; ++i) {
      const std::uint16_t nextSticky = static_cast<std::uint16_t>(w0 | (sticky != 0u ? 1u : 0u));
      w0 = w1;
      w1 = w2;
      w2 = w3;
      w3 = 0u;
      sticky = nextSticky;
    }

    words[3] = 0u;
    words[2] = w2;
    words[1] = w1;
    words[0] = w0;
    shift = shifted;
  }

  if (shift != 0) {
    const int rightShift = -shift;
    const int leftShift = shift + 16;
    const std::uint32_t w0 = words[0];
    const std::uint32_t w1 = words[1];
    const std::uint32_t w2 = words[2];
    const std::uint32_t w3 = words[3];

    sticky = static_cast<std::uint16_t>((sticky != 0u ? 1u : 0u) | static_cast<std::uint16_t>((w0 << leftShift) & 0xFFFFu));
    words[0] = static_cast<std::uint16_t>(((w1 << leftShift) | (w0 >> rightShift)) & 0xFFFFu);
    words[1] = static_cast<std::uint16_t>(((w2 << leftShift) | (w1 >> rightShift)) & 0xFFFFu);
    words[2] = static_cast<std::uint16_t>(((w3 << leftShift) | (w2 >> rightShift)) & 0xFFFFu);
    words[3] = static_cast<std::uint16_t>((w3 >> rightShift) & 0xFFFFu);
  }

  words[3] = static_cast<std::uint16_t>(words[3] | sign);
  const std::uint16_t mergedHigh = words[3];
  if ((sticky > 0x8000u || (sticky == 0x8000u && (words[0] & 1u) != 0u))
      && ++words[0] == 0u
      && ++words[1] == 0u
      && ++words[2] == 0u) {
    words[3] = static_cast<std::uint16_t>(mergedHigh + 1u);
    return -1;
  }

  if (mergedHigh != sign || words[2] != 0u || words[1] != 0u || words[0] != 0u) {
    return -1;
  }

  return 0;
}

/**
 * Address: 0x007D4380 (FUN_007D4380)
 *
 * What it does:
 * Clears one intrusive cartographic-decal list lane and destroys each dynamic
 * node after rebinding its vtable token.
 */
[[maybe_unused]] CartographicDecalNodeRuntime* ClearCartographicDecalListRuntime(
  CartographicDecalListRuntime* const list,
  void* const cartographicDecalVtable
)
{
  if (list == nullptr || list->sentinel == nullptr) {
    return nullptr;
  }

  CartographicDecalNodeRuntime* const sentinel = list->sentinel;
  CartographicDecalNodeRuntime* node = sentinel->next;
  sentinel->next = sentinel;
  sentinel->prev = sentinel;
  list->size = 0u;

  while (node != nullptr && node != sentinel) {
    CartographicDecalNodeRuntime* const next = node->next;
    node->vtable = cartographicDecalVtable;
    ::operator delete(node);
    node = next;
  }

  return node;
}

/**
 * Address: 0x00886FB0 (FUN_00886FB0)
 *
 * What it does:
 * Releases both SSO-capable string lanes in one wave-parameters object and
 * resets them to empty-inline form.
 */
[[maybe_unused]] void ResetWaveParametersStringsRuntime(
  WaveParametersRuntime* const parameters,
  void* const waveParametersVtable
)
{
  if (parameters == nullptr) {
    return;
  }

  parameters->vtable = waveParametersVtable;
  ResetSmallStringLane(&parameters->lane24);
  ResetSmallStringLane(&parameters->lane08);
}

/**
 * Address: 0x009A4AA0 (FUN_009A4AA0)
 *
 * What it does:
 * Rebinds one wx button lane to its own vtable and forwards destruction to
 * `wxControl`.
 */
[[maybe_unused]] int DestroyWxButtonRuntime(
  WxObjectRuntime* const button,
  void* const wxButtonVtable,
  const WxControlDtorFn wxControlDtorFn
)
{
  if (button == nullptr) {
    return 0;
  }

  button->vtable = wxButtonVtable;
  return wxControlDtorFn != nullptr ? wxControlDtorFn(button) : 0;
}

/**
 * Address: 0x004EAA50 (FUN_004EAA50)
 *
 * What it does:
 * Initializes one shared NaN word-pair lane once and returns the pair base.
 */
[[maybe_unused]] std::uint32_t* GetOrInitializeNaNWordPairRuntime(
  const std::uint32_t nanWord
)
{
  static std::uint32_t initializationFlags = 0u;
  static std::uint32_t nanWords[2] = {0u, 0u};

  if ((initializationFlags & 1u) == 0u) {
    initializationFlags |= 1u;
    nanWords[0] = nanWord;
    nanWords[1] = nanWord;
  }

  return nanWords;
}

/**
 * Address: 0x00967FE0 (FUN_00967FE0)
 *
 * What it does:
 * Maps wx input/style bitflags into output style bits used by the caller lane.
 */
[[maybe_unused]] int MapWxStyleFlagsRuntime(
  const int flags,
  const bool skipExtendedMappings
)
{
  int result = 0;
  if ((flags & 0x00100000) != 0) {
    result = 0x20;
  }

  if (!skipExtendedMappings) {
    if ((flags & 0x08000000) != 0) {
      result |= 0x200;
    }
    if ((flags & 0x10000000) != 0) {
      result |= 0x1;
    }
    if ((flags & 0x04000000) != 0) {
      result |= 0x1;
    }
    if ((flags & 0x01000000) != 0) {
      result |= 0x20000;
    }
  }
  return result;
}

/**
 * Address: 0x0096B530 (FUN_0096B530)
 *
 * What it does:
 * Reads one window text-metric lane from its HWND and writes into the caller
 * output structure.
 */
[[maybe_unused]] TEXTMETRICW* ReadWindowTextMetricsRuntime(
  const WindowTextMetricOwnerRuntime* const owner,
  TEXTMETRICW* const outMetrics
)
{
  if (owner == nullptr || outMetrics == nullptr || owner->windowHandle == nullptr) {
    return outMetrics;
  }

  HDC const deviceContext = ::GetDC(owner->windowHandle);
  if (deviceContext == nullptr) {
    return outMetrics;
  }

  ::GetTextMetricsW(deviceContext, outMetrics);
  ::ReleaseDC(owner->windowHandle, deviceContext);
  return outMetrics;
}

/**
 * Address: 0x0097C070 (FUN_0097C070)
 *
 * What it does:
 * Writes region bounding-box coordinates (`left`, `top`, `width`, `height`)
 * when region data is present, otherwise zeroes all outputs.
 */
[[maybe_unused]] LONG* GetRegionBoundsRuntime(
  const RegionOwnerRuntime* const owner,
  LONG* const outLeft,
  LONG* const outTop,
  LONG* const outWidth,
  LONG* const outHeight
)
{
  if (owner == nullptr || owner->node == nullptr || owner->node->regionHandle == nullptr) {
    if (outHeight != nullptr) {
      *outHeight = 0;
    }
    if (outWidth != nullptr) {
      *outWidth = 0;
    }
    if (outTop != nullptr) {
      *outTop = 0;
    }
    if (outLeft != nullptr) {
      *outLeft = 0;
    }
    return outTop;
  }

  RECT box{};
  ::GetRgnBox(owner->node->regionHandle, &box);
  const LONG left = box.left;
  const LONG top = box.top;

  if (outLeft != nullptr) {
    *outLeft = left;
  }
  if (outTop != nullptr) {
    *outTop = top;
  }
  if (outWidth != nullptr) {
    *outWidth = box.right - left;
  }
  if (outHeight != nullptr) {
    *outHeight = box.bottom - top;
  }
  return outHeight;
}

/**
 * Address: 0x0097C1C0 (FUN_0097C1C0)
 *
 * What it does:
 * Tests whether one rectangle lane intersects the stored region; returns `2`
 * on hit and `0` otherwise.
 */
[[maybe_unused]] int TestRectangleInRegionRuntime(
  const RegionOwnerRuntime* const owner,
  const LONG left,
  const LONG top,
  const int width,
  const int height
)
{
  if (owner == nullptr || owner->node == nullptr || owner->node->regionHandle == nullptr) {
    return 0;
  }

  RECT rectangle{};
  rectangle.left = left;
  rectangle.right = left + width;
  rectangle.top = top;
  rectangle.bottom = top + height;
  return ::RectInRegion(owner->node->regionHandle, &rectangle) != FALSE ? 2 : 0;
}

/**
 * Address: 0x00518370 (FUN_00518370)
 *
 * What it does:
 * Copies one `[source, sourceEnd)` range of emitter-curve key lanes and
 * assigns the key vtable token for each destination lane.
 */
[[maybe_unused]] EmitterCurveKeyRuntime* CopyEmitterCurveKeyRangeRuntime(
  EmitterCurveKeyRuntime* destination,
  const EmitterCurveKeyRuntime* source,
  const EmitterCurveKeyRuntime* const sourceEnd,
  void* const emitterCurveKeyVtable
)
{
  while (source != sourceEnd) {
    if (destination != nullptr) {
      destination->vtable = emitterCurveKeyVtable;
      destination->lane04 = source->lane04;
      destination->lane08 = source->lane08;
      destination->lane0C = source->lane0C;
      ++destination;
    }
    ++source;
  }
  return destination;
}

/**
 * Address: 0x00A2FE30 (FUN_00A2FE30)
 *
 * What it does:
 * Splits one millisecond lane into whole seconds (`+0x24`) and microseconds
 * remainder (`+0x28`).
 */
[[maybe_unused]] TimeSplitRuntime* SplitMillisecondsToTimeRuntime(
  TimeSplitRuntime* const destination,
  const std::uint32_t milliseconds
)
{
  if (destination == nullptr) {
    return nullptr;
  }

  destination->seconds = DivideBy1000Fast(milliseconds);
  destination->microseconds = (milliseconds - (destination->seconds * 1000u)) * 1000u;
  return destination;
}

/**
 * Address: 0x00A38620 (FUN_00A38620)
 *
 * What it does:
 * Rebinds one distance-lane object to the `Distance<float, Vector2<float>>`
 * vtable token.
 */
[[maybe_unused]] void ResetDistanceFloat2VtableRuntime(
  VtableOnlyRuntime* const distance,
  void* const distance2fVtable
)
{
  if (distance != nullptr) {
    distance->vtable = distance2fVtable;
  }
}

/**
 * Address: 0x00A39260 (FUN_00A39260)
 *
 * What it does:
 * Rebinds one distance-lane object to the `Distance<double, Vector2<double>>`
 * vtable token.
 */
[[maybe_unused]] void ResetDistanceDouble2VtableRuntime(
  VtableOnlyRuntime* const distance,
  void* const distance2dVtable
)
{
  if (distance != nullptr) {
    distance->vtable = distance2dVtable;
  }
}

/**
 * Address: 0x00A9A890 (FUN_00A9A890)
 *
 * What it does:
 * Returns MXCSR when compatibility mode is enabled; otherwise returns zero.
 */
[[maybe_unused]] int ReadMxcsrWhenCompatEnabledRuntime(
  const bool compatibilityEnabled
)
{
  return compatibilityEnabled ? static_cast<int>(_mm_getcsr()) : 0;
}

/**
 * Address: 0x00AA3B35 (FUN_00AA3B35)
 *
 * What it does:
 * Captures one runtime-frame context lane into global scratch words and
 * returns the original result lane.
 */
[[maybe_unused]] int CaptureRuntimeFrameContextRuntime(
  const int result,
  const int frameBase,
  const int arg3,
  std::uint32_t* const contextWords
)
{
  if (contextWords != nullptr) {
    contextWords[2] = static_cast<std::uint32_t>(arg3);
    contextWords[1] = static_cast<std::uint32_t>(result);
    contextWords[3] = static_cast<std::uint32_t>(frameBase);
  }
  return result;
}

/**
 * Address: 0x0076D300 (FUN_0076D300)
 *
 * What it does:
 * Copies one `[source, sourceEnd)` range of occupy-source bindings, installing
 * the binding vtable token for each copied lane.
 */
[[maybe_unused]] OccupySourceBindingRuntime* CopyOccupySourceBindingRangeRuntime(
  OccupySourceBindingRuntime* destination,
  const OccupySourceBindingRuntime* source,
  const OccupySourceBindingRuntime* const sourceEnd,
  void* const occupySourceBindingVtable
)
{
  while (source != sourceEnd) {
    if (destination != nullptr) {
      destination->vtable = occupySourceBindingVtable;
      destination->lane04 = source->lane04;
      destination->lane08 = source->lane08;
      ++destination;
    }
    ++source;
  }
  return destination;
}

/**
 * Address: 0x007D9B40 (FUN_007D9B40)
 *
 * What it does:
 * Copies one `[source, sourceEnd)` range of clutter seed lanes, installing the
 * seed vtable token for each copied lane.
 */
[[maybe_unused]] ClutterSeedRuntime* CopyClutterSeedRangeRuntime(
  ClutterSeedRuntime* destination,
  const ClutterSeedRuntime* source,
  const ClutterSeedRuntime* const sourceEnd,
  void* const clutterSeedVtable
)
{
  while (source != sourceEnd) {
    if (destination != nullptr) {
      destination->vtable = clutterSeedVtable;
      destination->lane04 = source->lane04;
      destination->lane08 = source->lane08;
      destination->lane0C = source->lane0C;
      ++destination;
    }
    ++source;
  }
  return destination;
}

/**
 * Address: 0x004F7CF0 (FUN_004F7CF0)
 *
 * What it does:
 * Rebinds wios/ostream/istream subobject lanes to teardown vtables and invokes
 * `ios_base` final destruction on the `+0x0C` subobject lane.
 */
[[maybe_unused]] void DestroyWideIoStreamBaseRuntime(
  std::byte* const completeObject,
  const WideIoStreamOffsetsRuntime& offsets,
  void* const wiosVtable,
  void* const wostreamVtable,
  void* const wistreamVtable,
  void* const iosBaseVtable,
  const IosBaseDtorFn iosBaseDtorFn
)
{
  if (completeObject == nullptr || iosBaseDtorFn == nullptr) {
    return;
  }

  *reinterpret_cast<void**>(completeObject + offsets.wiosOffset) = wiosVtable;
  *reinterpret_cast<void**>(completeObject + offsets.iosbOffset) = wostreamVtable;
  *reinterpret_cast<void**>(completeObject + offsets.wistreamOffset) = wistreamVtable;

  std::byte* const iosBaseLane = completeObject + 12;
  *reinterpret_cast<void**>(iosBaseLane) = iosBaseVtable;
  iosBaseDtorFn(iosBaseLane);
}

/**
 * Address: 0x004F8240 (FUN_004F8240)
 *
 * What it does:
 * Forwards one runtime-failure lane to the core dispatcher and never returns.
 */
[[maybe_unused]] [[noreturn]] void DispatchRuntimeFailureAndTerminateRuntime(
  const int arg0,
  const int arg1,
  const RuntimeFailureDispatchFn dispatchFn
)
{
  if (dispatchFn != nullptr) {
    dispatchFn(arg0, arg1);
  }
  std::terminate();
}

/**
 * Address: 0x00837750 (FUN_00837750)
 *
 * What it does:
 * Compares one 48-byte-stride build-queue snapshot against the current queue
 * and reports equality when comparator output reaches the snapshot end.
 */
[[maybe_unused]] BOOL CompareBuildQueueSnapshotRuntime(
  const BuildQueueSnapshotRuntime* const snapshot,
  const BuildQueueRangeRuntime* const currentQueue,
  const BuildQueueCompareFn compareFn
)
{
  if (snapshot == nullptr || currentQueue == nullptr || compareFn == nullptr) {
    return FALSE;
  }

  const std::ptrdiff_t snapshotCount = CountStride48Elements(snapshot->begin, snapshot->end);
  const std::ptrdiff_t currentCount = CountStride48Elements(currentQueue->start, currentQueue->end);
  if (snapshotCount != currentCount) {
    return FALSE;
  }

  BuildQueueCompareStateRuntime state{};
  auto* const compareResult = compareFn(&state, snapshot->begin, snapshot->end, currentQueue->start, 0u, 0u);
  return (compareResult != nullptr && compareResult->cursor == snapshot->end) ? TRUE : FALSE;
}

/**
 * Address: 0x007B2560 (FUN_007B2560)
 *
 * What it does:
 * Clears one RB-tree storage lane, frees the storage block, and resets owner
 * pointers/count.
 */
[[maybe_unused]] int ClearTreeStorageLaneA17Runtime(
  TreeStorageOwnerRuntime* const owner,
  const TreeClearFn clearFn
)
{
  if (owner == nullptr) {
    return 0;
  }

  std::uint32_t scratch = 0u;
  if (owner->treeStorage != nullptr && clearFn != nullptr) {
    void* const root = *reinterpret_cast<void**>(owner->treeStorage);
    clearFn(&scratch, root, owner->treeStorage);
  }

  ::operator delete(owner->treeStorage);
  owner->treeStorage = nullptr;
  owner->size = 0u;
  return 0;
}

/**
 * Address: 0x007B2590 (FUN_007B2590)
 *
 * What it does:
 * Clears one map-backed tree storage lane, frees the storage block, and
 * resets owner pointers/count.
 */
[[maybe_unused]] int ClearTreeStorageLaneB17Runtime(
  TreeStorageOwnerRuntime* const owner,
  const TreeClearFn clearFn
)
{
  if (owner == nullptr) {
    return 0;
  }

  std::uint32_t scratch = 0u;
  if (owner->treeStorage != nullptr && clearFn != nullptr) {
    void* const root = *reinterpret_cast<void**>(owner->treeStorage);
    clearFn(&scratch, root, owner->treeStorage);
  }

  ::operator delete(owner->treeStorage);
  owner->treeStorage = nullptr;
  owner->size = 0u;
  return 0;
}

/**
 * Address: 0x007B26B0 (FUN_007B26B0)
 *
 * What it does:
 * Finds-or-inserts one `uint` key in an RB-tree lane whose nil flag lives at
 * `+0x11`, returning `(node, inserted)`.
 */
[[maybe_unused]] MapInsertStatusRuntime* FindOrInsertMapNodeNil17Runtime(
  LegacyMapStorageRuntime<MapNodeNil17Runtime>* const map,
  const std::uint32_t* const key,
  MapInsertStatusRuntime* const outResult
)
{
  return FindOrInsertMapNodeByKey(map, key, outResult);
}

/**
 * Address: 0x007B2940 (FUN_007B2940)
 *
 * What it does:
 * Clears one RB-tree storage lane, frees the storage block, and resets owner
 * pointers/count.
 */
[[maybe_unused]] int ClearTreeStorageLaneC21Runtime(
  TreeStorageOwnerRuntime* const owner,
  const TreeClearFn clearFn
)
{
  if (owner == nullptr) {
    return 0;
  }

  std::uint32_t scratch = 0u;
  if (owner->treeStorage != nullptr && clearFn != nullptr) {
    void* const root = *reinterpret_cast<void**>(owner->treeStorage);
    clearFn(&scratch, root, owner->treeStorage);
  }

  ::operator delete(owner->treeStorage);
  owner->treeStorage = nullptr;
  owner->size = 0u;
  return 0;
}

/**
 * Address: 0x007B2970 (FUN_007B2970)
 *
 * What it does:
 * Clears one embedded secondary RB-tree lane at owner offset `+0x08`, frees
 * its storage block, and resets owner pointers/count.
 */
[[maybe_unused]] int ClearEmbeddedSecondaryTreeLaneRuntime(
  std::byte* const ownerBytes,
  const TreeClearFn clearFn
)
{
  if (ownerBytes == nullptr) {
    return 0;
  }

  auto* const embeddedOwner = reinterpret_cast<TreeStorageOwnerRuntime*>(ownerBytes + 4u);
  std::uint32_t scratch = 0u;
  if (embeddedOwner->treeStorage != nullptr && clearFn != nullptr) {
    void* const root = *reinterpret_cast<void**>(embeddedOwner->treeStorage);
    clearFn(&scratch, root, embeddedOwner->treeStorage);
  }

  ::operator delete(embeddedOwner->treeStorage);
  embeddedOwner->treeStorage = nullptr;
  embeddedOwner->size = 0u;
  return 0;
}

/**
 * Address: 0x007B36A0 (FUN_007B36A0)
 *
 * What it does:
 * Clears one RB-tree storage lane, frees the storage block, and resets owner
 * pointers/count.
 */
[[maybe_unused]] int ClearTreeStorageLaneD21Runtime(
  TreeStorageOwnerRuntime* const owner,
  const TreeClearFn clearFn
)
{
  if (owner == nullptr) {
    return 0;
  }

  std::uint32_t scratch = 0u;
  if (owner->treeStorage != nullptr && clearFn != nullptr) {
    void* const root = *reinterpret_cast<void**>(owner->treeStorage);
    clearFn(&scratch, root, owner->treeStorage);
  }

  ::operator delete(owner->treeStorage);
  owner->treeStorage = nullptr;
  owner->size = 0u;
  return 0;
}

/**
 * Address: 0x007B3760 (FUN_007B3760)
 *
 * What it does:
 * Finds-or-inserts one `uint` key in an RB-tree lane whose nil flag lives at
 * `+0x1D`, returning `(node, inserted)`.
 */
[[maybe_unused]] MapInsertStatusRuntime* FindOrInsertMapNodeNil29Runtime(
  LegacyMapStorageRuntime<MapNodeNil29Runtime>* const map,
  const std::uint32_t* const key,
  MapInsertStatusRuntime* const outResult
)
{
  return FindOrInsertMapNodeByKey(map, key, outResult);
}

/**
 * Address: 0x007BE3B0 (FUN_007BE3B0)
 *
 * What it does:
 * Copies records from wrapped pointer arrays while walking source/destination
 * cursors backward.
 */
[[maybe_unused]] WrappedArrayCursorRuntime* CopyWrappedRecordRangeReverseRuntime(
  WrappedArrayCursorRuntime* const outCursor,
  const WrappedArrayCursorRuntime stopCursor,
  WrappedArrayCursorRuntime sourceCursor,
  WrappedArrayCursorRuntime destinationCursor,
  const NetCommandRecordCopyFn copyRecordFn
)
{
  if (outCursor == nullptr) {
    return nullptr;
  }

  while (stopCursor.owner != sourceCursor.owner || stopCursor.logicalIndex != sourceCursor.logicalIndex) {
    --sourceCursor.logicalIndex;
    const auto sourceWord = ResolveWrappedPointerWord(sourceCursor.owner, sourceCursor.logicalIndex);
    auto* const sourceRecord = reinterpret_cast<const NetCommandRecordRuntime*>(static_cast<std::uintptr_t>(sourceWord));

    --destinationCursor.logicalIndex;
    const auto destinationWord = ResolveWrappedPointerWord(destinationCursor.owner, destinationCursor.logicalIndex);
    auto* const destinationRecord = reinterpret_cast<NetCommandRecordRuntime*>(static_cast<std::uintptr_t>(destinationWord));

    if (copyRecordFn != nullptr && destinationRecord != nullptr && sourceRecord != nullptr) {
      copyRecordFn(destinationRecord, sourceRecord);
    }
  }

  outCursor->owner = destinationCursor.owner;
  outCursor->logicalIndex = destinationCursor.logicalIndex;
  return outCursor;
}

/**
 * Address: 0x007BE430 (FUN_007BE430)
 *
 * What it does:
 * Copies records from wrapped pointer arrays while walking source/destination
 * cursors forward.
 */
[[maybe_unused]] WrappedArrayCursorRuntime* CopyWrappedRecordRangeForwardRuntime(
  WrappedArrayCursorRuntime* const outCursor,
  const WrappedArrayCursorRuntime stopCursor,
  WrappedArrayCursorRuntime sourceCursor,
  WrappedArrayCursorRuntime destinationCursor,
  const NetCommandRecordCopyFn copyRecordFn
)
{
  if (outCursor == nullptr) {
    return nullptr;
  }

  while (sourceCursor.owner != stopCursor.owner || sourceCursor.logicalIndex != stopCursor.logicalIndex) {
    const auto sourceWord = ResolveWrappedPointerWord(sourceCursor.owner, sourceCursor.logicalIndex);
    auto* const sourceRecord = reinterpret_cast<const NetCommandRecordRuntime*>(static_cast<std::uintptr_t>(sourceWord));
    const auto destinationWord = ResolveWrappedPointerWord(destinationCursor.owner, destinationCursor.logicalIndex);
    auto* const destinationRecord = reinterpret_cast<NetCommandRecordRuntime*>(static_cast<std::uintptr_t>(destinationWord));

    if (copyRecordFn != nullptr && destinationRecord != nullptr && sourceRecord != nullptr) {
      copyRecordFn(destinationRecord, sourceRecord);
    }

    ++sourceCursor.logicalIndex;
    ++destinationCursor.logicalIndex;
  }

  outCursor->owner = destinationCursor.owner;
  outCursor->logicalIndex = destinationCursor.logicalIndex;
  return outCursor;
}

/**
 * Address: 0x007C87E0 (FUN_007C87E0)
 *
 * What it does:
 * Appends one 16-byte lane in-place when capacity remains, otherwise routes
 * through the vector growth path.
 */
[[maybe_unused]] int Append16ByteLaneWithGrowRuntime(
  const std::uint32_t inputWord,
  Vector16ByteOwnerRuntime* const owner,
  const Vector16ConstructFn constructFn,
  const Vector16GrowFn growFn
)
{
  if (owner == nullptr) {
    return 0;
  }

  const std::ptrdiff_t size = (owner->begin != nullptr && owner->end != nullptr) ? ((owner->end - owner->begin) >> 4) : 0;
  const std::ptrdiff_t capacity = (owner->begin != nullptr && owner->capacity != nullptr) ? ((owner->capacity - owner->begin) >> 4) : 0;
  if (owner->begin == nullptr || size >= capacity) {
    return growFn != nullptr ? growFn(owner, owner->end, inputWord) : 0;
  }

  std::byte* const tail = owner->end;
  const int result = constructFn != nullptr ? constructFn(tail, 0u, 0u) : 0;
  owner->end = tail + 16;
  return result;
}

/**
 * Address: 0x007C9010 (FUN_007C9010)
 *
 * What it does:
 * Clears one owner-coupled tree storage lane via the 4-argument clear helper,
 * frees storage, and resets owner pointers/count.
 */
[[maybe_unused]] int ClearOwnedTreeStorageLaneARuntime(
  TreeStorageOwnerRuntime* const owner,
  const TreeClearWithOwnerFn clearFn
)
{
  if (owner == nullptr) {
    return 0;
  }

  std::uint32_t scratch = 0u;
  if (owner->treeStorage != nullptr && clearFn != nullptr) {
    void* const root = *reinterpret_cast<void**>(owner->treeStorage);
    clearFn(owner, &scratch, root, owner->treeStorage);
  }

  ::operator delete(owner->treeStorage);
  owner->treeStorage = nullptr;
  owner->size = 0u;
  return 0;
}

/**
 * Address: 0x007C9950 (FUN_007C9950)
 *
 * What it does:
 * Clears one owner-coupled tree storage lane via the 4-argument clear helper,
 * frees storage, and resets owner pointers/count.
 */
[[maybe_unused]] int ClearOwnedTreeStorageLaneBRuntime(
  TreeStorageOwnerRuntime* const owner,
  const TreeClearWithOwnerFn clearFn
)
{
  if (owner == nullptr) {
    return 0;
  }

  std::uint32_t scratch = 0u;
  if (owner->treeStorage != nullptr && clearFn != nullptr) {
    void* const root = *reinterpret_cast<void**>(owner->treeStorage);
    clearFn(owner, &scratch, root, owner->treeStorage);
  }

  ::operator delete(owner->treeStorage);
  owner->treeStorage = nullptr;
  owner->size = 0u;
  return 0;
}

/**
 * Address: 0x007CDED0 (FUN_007CDED0)
 *
 * What it does:
 * Finds-or-inserts one `char` key in a set-style RB-tree lane whose nil flag
 * lives at `+0x0E`, returning `(node, inserted)`.
 */
[[maybe_unused]] MapInsertStatusRuntime* FindOrInsertSetCharNodeRuntime(
  LegacyMapStorageRuntime<SetCharNodeNil14Runtime>* const setStorage,
  const std::int8_t* const key,
  MapInsertStatusRuntime* const outResult
)
{
  if (outResult == nullptr) {
    return nullptr;
  }

  outResult->node = nullptr;
  outResult->inserted = 0u;
  outResult->reserved[0] = 0u;
  outResult->reserved[1] = 0u;
  outResult->reserved[2] = 0u;
  if (setStorage == nullptr || key == nullptr) {
    return outResult;
  }

  SetCharNodeNil14Runtime* head = setStorage->head;
  if (head == nullptr) {
    head = static_cast<SetCharNodeNil14Runtime*>(::operator new(sizeof(SetCharNodeNil14Runtime), std::nothrow));
    if (head == nullptr) {
      return outResult;
    }

    std::memset(head, 0, sizeof(SetCharNodeNil14Runtime));
    head->left = head;
    head->parent = head;
    head->right = head;
    head->isNil = 1u;
    setStorage->head = head;
    setStorage->size = 0u;
  }

  SetCharNodeNil14Runtime* parent = head;
  SetCharNodeNil14Runtime* cursor = head->parent;
  bool goLeft = true;
  while (cursor != nullptr && cursor != head && cursor->isNil == 0u) {
    parent = cursor;
    if (*key < cursor->value) {
      goLeft = true;
      cursor = cursor->left;
    } else if (cursor->value < *key) {
      goLeft = false;
      cursor = cursor->right;
    } else {
      outResult->node = cursor;
      return outResult;
    }
  }

  auto* const inserted = static_cast<SetCharNodeNil14Runtime*>(::operator new(sizeof(SetCharNodeNil14Runtime), std::nothrow));
  if (inserted == nullptr) {
    return outResult;
  }

  std::memset(inserted, 0, sizeof(SetCharNodeNil14Runtime));
  inserted->left = head;
  inserted->right = head;
  inserted->parent = (parent != nullptr) ? parent : head;
  inserted->value = *key;
  inserted->color = 0u;
  inserted->isNil = 0u;

  if (parent == nullptr || parent == head || parent->isNil != 0u) {
    head->parent = inserted;
    head->left = inserted;
    head->right = inserted;
  } else if (goLeft) {
    parent->left = inserted;
    if (head->left == parent || head->left == head) {
      head->left = inserted;
    }
  } else {
    parent->right = inserted;
    if (head->right == parent || head->right == head) {
      head->right = inserted;
    }
  }

  ++setStorage->size;
  outResult->node = inserted;
  outResult->inserted = 1u;
  return outResult;
}

/**
 * Address: 0x007CEDB0 (FUN_007CEDB0)
 *
 * What it does:
 * Performs upward heap insertion for one `(priority, LuaObject)` lane in a
 * 24-byte stride heap array.
 */
[[maybe_unused]] void InsertLuaHeapPairRuntime(
  std::int32_t insertIndex,
  const std::int32_t firstIndex,
  std::byte* const heapStorage,
  const std::int32_t priority,
  LuaPlus::LuaObject value
)
{
  if (heapStorage == nullptr) {
    return;
  }

  std::int32_t parent = (insertIndex - 1) / 2;
  while (firstIndex < insertIndex) {
    std::byte* const parentEntry = heapStorage + (static_cast<std::size_t>(parent) * 24u);
    const auto parentPriority = *reinterpret_cast<std::int32_t*>(parentEntry);
    if (parentPriority >= priority) {
      break;
    }

    std::byte* const targetEntry = heapStorage + (static_cast<std::size_t>(insertIndex) * 24u);
    *reinterpret_cast<std::int32_t*>(targetEntry) = parentPriority;
    *reinterpret_cast<LuaPlus::LuaObject*>(targetEntry + 4u) = *reinterpret_cast<LuaPlus::LuaObject*>(parentEntry + 4u);
    insertIndex = parent;
    parent = (parent - 1) / 2;
  }

  std::byte* const insertEntry = heapStorage + (static_cast<std::size_t>(insertIndex) * 24u);
  *reinterpret_cast<std::int32_t*>(insertEntry) = priority;
  *reinterpret_cast<LuaPlus::LuaObject*>(insertEntry + 4u) = value;
}

/**
 * Address: 0x007CF8C0 (FUN_007CF8C0)
 *
 * What it does:
 * Clears one owner-coupled tree storage lane via the 4-argument clear helper,
 * frees storage, and resets owner pointers/count.
 */
[[maybe_unused]] TreeStorageOwnerRuntime* ClearOwnedTreeStorageLaneAndReturnOwnerRuntime(
  TreeStorageOwnerRuntime* const owner,
  const TreeClearWithOwnerFn clearFn
)
{
  if (owner == nullptr) {
    return nullptr;
  }

  std::uint32_t scratch = 0u;
  if (owner->treeStorage != nullptr && clearFn != nullptr) {
    void* const root = *reinterpret_cast<void**>(owner->treeStorage);
    clearFn(owner, &scratch, root, owner->treeStorage);
  }

  ::operator delete(owner->treeStorage);
  owner->treeStorage = nullptr;
  owner->size = 0u;
  return owner;
}

/**
 * Address: 0x007D4320 (FUN_007D4320)
 *
 * What it does:
 * Clears one cartographic-decal owner lane via its list clear routine, frees
 * list storage, and resets the storage pointer.
 */
[[maybe_unused]] void ClearAndReleaseCartographicDecalOwnerRuntime(
  TreeStorageOwnerRuntime* const owner,
  const OwnerTreeClearFn clearOwnerFn
)
{
  if (owner == nullptr) {
    return;
  }

  if (clearOwnerFn != nullptr) {
    clearOwnerFn(owner);
  }

  ::operator delete(owner->treeStorage);
  owner->treeStorage = nullptr;
}

/**
 * Address: 0x007E2E60 (FUN_007E2E60)
 *
 * What it does:
 * Finds one lookup node by text key and returns either the matched node or the
 * owner sentinel when append-to-sink fails.
 */
[[maybe_unused]] void** FindLookupNodeAndAppendTextRuntime(
  void* const textSink,
  void** const outNode,
  void* const owner,
  const void* const key,
  const LookupNodeByTextFn lookupFn,
  const AppendLookupTextFn appendFn
)
{
  if (outNode == nullptr) {
    return nullptr;
  }

  void* sentinel = nullptr;
  if (owner != nullptr) {
    sentinel = *reinterpret_cast<void**>(static_cast<std::byte*>(owner) + 4u);
  }

  void* node = sentinel;
  if (lookupFn != nullptr) {
    node = lookupFn(owner, key);
  }

  if (node == sentinel || appendFn == nullptr || appendFn(textSink, node) < 0) {
    *outNode = sentinel;
    return outNode;
  }

  *outNode = node;
  return outNode;
}

/**
 * Address: 0x007E5170 (FUN_007E5170)
 *
 * What it does:
 * Initializes a shared-ref pair from one object lane, enables
 * shared-from-this, then releases the previous counter lane.
 */
[[maybe_unused]] void AssignSharedRefWithEnableRuntime(
  void* const object,
  SharedRefRuntime* const destination,
  const InitSharedRefFn initFn,
  const EnableSharedFromThisFn enableFn
)
{
  if (destination == nullptr) {
    return;
  }

  SharedRefInitRuntime temporary{object, nullptr};
  if (initFn != nullptr) {
    initFn(&temporary, object);
  }
  if (enableFn != nullptr) {
    enableFn(&temporary, object, object);
  }

  void* const previousCounter = destination->counter;
  destination->object = object;
  destination->counter = temporary.counter;
  ReleaseSharedCounterRuntime(previousCounter);
}

/**
 * Address: 0x007E5B20 (FUN_007E5B20)
 *
 * What it does:
 * Finds-or-inserts one 2-word key in a pair-key RB-tree lane whose nil flag
 * lives at `+0x25`, returning `(node, inserted)`.
 */
[[maybe_unused]] MapInsertStatusRuntime* FindOrInsertPairKeyNodeRuntime(
  const PairKeyRuntime* const key,
  LegacyMapStorageRuntime<PairKeyNodeNil37Runtime>* const map,
  MapInsertStatusRuntime* const outResult
)
{
  if (outResult == nullptr) {
    return nullptr;
  }

  outResult->node = nullptr;
  outResult->inserted = 0u;
  outResult->reserved[0] = 0u;
  outResult->reserved[1] = 0u;
  outResult->reserved[2] = 0u;
  if (map == nullptr || key == nullptr) {
    return outResult;
  }

  PairKeyNodeNil37Runtime* const head = EnsurePairMapHeadRuntime(map);
  if (head == nullptr) {
    return outResult;
  }

  PairKeyNodeNil37Runtime* parent = head;
  PairKeyNodeNil37Runtime* cursor = head->parent;
  bool goLeft = true;
  while (cursor != nullptr && cursor != head && cursor->isNil == 0u) {
    parent = cursor;
    const PairKeyRuntime cursorKey{cursor->keyHigh, cursor->keyLow};
    if (PairKeyLessRuntime(*key, cursorKey)) {
      goLeft = true;
      cursor = cursor->left;
    } else if (PairKeyLessRuntime(cursorKey, *key)) {
      goLeft = false;
      cursor = cursor->right;
    } else {
      outResult->node = cursor;
      return outResult;
    }
  }

  auto* const inserted = static_cast<PairKeyNodeNil37Runtime*>(::operator new(sizeof(PairKeyNodeNil37Runtime), std::nothrow));
  if (inserted == nullptr) {
    return outResult;
  }

  std::memset(inserted, 0, sizeof(PairKeyNodeNil37Runtime));
  inserted->left = head;
  inserted->right = head;
  inserted->parent = (parent != nullptr) ? parent : head;
  inserted->keyHigh = key->high;
  inserted->keyLow = key->low;
  inserted->isNil = 0u;

  if (parent == nullptr || parent == head || parent->isNil != 0u) {
    head->parent = inserted;
    head->left = inserted;
    head->right = inserted;
  } else if (goLeft) {
    parent->left = inserted;
    if (head->left == parent || head->left == head) {
      head->left = inserted;
    }
  } else {
    parent->right = inserted;
    if (head->right == parent || head->right == head) {
      head->right = inserted;
    }
  }

  ++map->size;
  outResult->node = inserted;
  outResult->inserted = 1u;
  return outResult;
}

/**
 * Address: 0x007E6090 (FUN_007E6090)
 *
 * What it does:
 * Allocates one pair-key map node lane, writes keys/payload, and clears
 * color/nil state bytes.
 */
[[maybe_unused]] PairNodeRuntime* AllocatePairNodeRuntime(
  const std::int32_t keyHigh,
  const std::int32_t keyLow,
  const std::int32_t keyExtra,
  const std::int32_t payloadSource,
  const PairNodeAllocFn allocFn,
  const PairNodePayloadInitFn initPayloadFn
)
{
  if (allocFn == nullptr) {
    return nullptr;
  }

  PairNodeRuntime* const node = allocFn(1u);
  if (node == nullptr) {
    return nullptr;
  }

  node->lane00 = keyHigh;
  node->lane04 = keyLow;
  node->lane08 = keyExtra;
  if (initPayloadFn != nullptr) {
    initPayloadFn(node->payload0C, payloadSource);
  }
  node->color = 0u;
  node->isNil = 0u;
  return node;
}

/**
 * Address: 0x007E6AD0 (FUN_007E6AD0)
 *
 * What it does:
 * Runs one pre-delete hook for an object lane, then frees the object storage.
 */
[[maybe_unused]] void RunCleanupThenDeleteObjectRuntime(
  void* const object,
  const ObjectPreDeleteFn preDeleteFn
)
{
  if (object == nullptr) {
    return;
  }

  if (preDeleteFn != nullptr) {
    preDeleteFn(object);
  }
  ::operator delete(object);
}

/**
 * Address: 0x007EB4F0 (FUN_007EB4F0)
 *
 * What it does:
 * Replaces one destination intrusive-list lane with source contents when the
 * lists differ, then clears the source list.
 */
[[maybe_unused]] std::size_t TransferListContentAndClearSourceRuntime(
  IntrusiveListRuntime* const destination,
  std::byte* const sourceOwnerBytes,
  const ListClearFn clearFn,
  const ListSpliceFn spliceFn
)
{
  if (destination == nullptr || sourceOwnerBytes == nullptr || clearFn == nullptr) {
    return 0u;
  }

  auto* const source = reinterpret_cast<IntrusiveListRuntime*>(sourceOwnerBytes + 0x30);
  if (destination != source && source->head != nullptr && spliceFn != nullptr) {
    IntrusiveListNodeRuntime* const sourceHead = source->head;
    IntrusiveListNodeRuntime* const first = sourceHead->next;
    clearFn(destination);
    if (destination->head != nullptr) {
      spliceFn(destination, destination->head->next, first, sourceHead, first);
    }
  }

  clearFn(source);
  return static_cast<std::size_t>(destination->size);
}

/**
 * Address: 0x007EB5B0 (FUN_007EB5B0)
 *
 * What it does:
 * Copies one camera-snapshot lane and updates the weak-counted pointer lane
 * with retain/release semantics.
 */
[[maybe_unused]] void* CopyCameraSnapshotAndWeakCounterRuntime(
  const void* const source,
  void* const destination,
  const CameraCopyFn copyCameraFn,
  const WeakReleaseFn weakReleaseFn
)
{
  if (source == nullptr || destination == nullptr) {
    return destination;
  }

  const CameraSnapshotViewRuntime srcView(source);
  const CameraSnapshotViewRuntime dstView(destination);
  dstView.lane08() = srcView.lane08();
  if (copyCameraFn != nullptr) {
    copyCameraFn(dstView.cameraStorage(), srcView.cameraStorage());
  }

  dstView.lane2D8() = srcView.lane2D8();
  dstView.lane2DC() = srcView.lane2DC();
  dstView.lane2E0() = srcView.lane2E0();
  dstView.lane2E4() = srcView.lane2E4();
  dstView.lane2E8() = srcView.lane2E8();
  dstView.lane2EC() = srcView.lane2EC();
  dstView.lane2F0() = srcView.lane2F0();
  dstView.lane2F4() = srcView.lane2F4();
  dstView.lane2F8() = srcView.lane2F8();
  dstView.lane2FC() = srcView.lane2FC();
  dstView.lane300() = srcView.lane300();

  void* const incomingCounter = srcView.weakCounter304();
  if (incomingCounter != dstView.weakCounter304()) {
    if (incomingCounter != nullptr) {
      auto* const strong = reinterpret_cast<volatile LONG*>(static_cast<std::byte*>(incomingCounter) + 4u);
      (void)::InterlockedExchangeAdd(strong, 1);
    }

    void* const previousCounter = dstView.weakCounter304();
    if (previousCounter != nullptr && weakReleaseFn != nullptr) {
      weakReleaseFn(previousCounter);
    }
    dstView.weakCounter304() = incomingCounter;
  }

  return destination;
}

/**
 * Address: 0x007EBB20 (FUN_007EBB20)
 *
 * What it does:
 * Erases one mesh-thumbnail intrusive-list node (except sentinel), destroys
 * payload, and decrements list size.
 */
[[maybe_unused]] MeshThumbnailListNodeRuntime** EraseMeshThumbnailListNodeRuntime(
  MeshThumbnailListNodeRuntime** const outNext,
  TreeStorageOwnerRuntime* const listOwner,
  MeshThumbnailListNodeRuntime* const node,
  const MeshThumbnailDtorFn thumbnailDtorFn
)
{
  if (outNext == nullptr || listOwner == nullptr || node == nullptr) {
    return outNext;
  }

  auto* const sentinel = reinterpret_cast<MeshThumbnailListNodeRuntime*>(listOwner->treeStorage);
  MeshThumbnailListNodeRuntime* const next = node->next;
  if (node != sentinel) {
    if (node->prev != nullptr) {
      node->prev->next = node->next;
    }
    if (node->next != nullptr) {
      node->next->prev = node->prev;
    }

    if (thumbnailDtorFn != nullptr) {
      thumbnailDtorFn(node->thumbnailStorage);
    }
    ::operator delete(node);
    --listOwner->size;
  }

  *outNext = next;
  return outNext;
}

/**
 * Address: 0x007EF1C0 (FUN_007EF1C0)
 *
 * What it does:
 * Builds one current-selection range lane and, when valid, submits an
 * axis-aligned quad derived from selected entry bounds.
 */
[[maybe_unused]] int UpdateSelectedEntryBoundsRuntime(
  void* const owner,
  const void* const selectionState,
  const BuildSelectionRangeFn buildRangeFn,
  const SubmitSelectionQuadFn submitQuadFn
)
{
  if (owner == nullptr || selectionState == nullptr) {
    return 0;
  }

  auto* const ownerBytes = static_cast<std::byte*>(owner);
  void* const begin = *reinterpret_cast<void**>(ownerBytes + 4u);
  void* const end = *reinterpret_cast<void**>(ownerBytes + 8u);
  std::uint32_t rangeState[2]{};
  if (buildRangeFn != nullptr) {
    buildRangeFn(rangeState, owner, begin, end);
  }

  const UnitSelectionStateViewRuntime stateView(selectionState);
  int result = stateView.selectedIndex();
  if (result < 0) {
    return result;
  }

  void* const* const entries = stateView.entries();
  if (entries == nullptr) {
    return result;
  }

  const auto entryWord = reinterpret_cast<std::uintptr_t>(entries[result]);
  result = static_cast<int>(entryWord);
  if (entryWord == 0u) {
    return result;
  }

  const UnitSelectionEntryViewRuntime entryView(reinterpret_cast<const void*>(entryWord));
  if (entryView.sampleCount() <= 0) {
    return result;
  }

  float quad[4]{};
  quad[0] = entryView.minX() + entryView.extX();
  quad[1] = entryView.minY() + entryView.extY();
  quad[2] = 0.0f;
  quad[3] = entryView.minZ();
  return submitQuadFn != nullptr ? submitQuadFn(quad, owner) : result;
}

/**
 * Address: 0x007EFFA0 (FUN_007EFFA0)
 *
 * What it does:
 * Appends one 136-byte lane in-place when capacity remains, otherwise routes
 * through vector growth/reallocation path.
 */
[[maybe_unused]] void AppendOrGrowStride136VectorRuntime(
  Stride136VectorRuntime* const vector,
  void* const source,
  const ConstructStride136Fn constructFn,
  const GrowStride136Fn growFn
)
{
  if (vector == nullptr) {
    return;
  }

  const std::ptrdiff_t size = CountStride136Elements(vector->begin, vector->end);
  const std::ptrdiff_t capacity = CountStride136Elements(vector->begin, vector->capacity);
  if (vector->begin != nullptr && size < capacity) {
    std::byte* const tail = vector->end;
    if (constructFn != nullptr) {
      constructFn(tail, 1u, 0u, 0u);
    }
    vector->end = tail + 136u;
    return;
  }

  std::uint32_t scratch = 0u;
  if (growFn != nullptr) {
    growFn(vector, &scratch, vector->end, source);
  }
}

/**
 * Address: 0x007F2DA0 (FUN_007F2DA0)
 *
 * What it does:
 * Clones one tree storage lane when source root differs from current token and
 * swaps the owner root pointer to the new clone.
 */
[[maybe_unused]] void* CloneTreeStorageIntoOwnerRuntime(
  void* const currentToken,
  TreeStorageOwnerRuntime* const owner,
  void* const sourceRoot,
  const CloneTreeStorageFn cloneStorageFn,
  const CloneTreePayloadFn clonePayloadFn
)
{
  if (owner == nullptr) {
    return sourceRoot;
  }

  if (sourceRoot != currentToken && cloneStorageFn != nullptr) {
    void* const previousRoot = owner->treeStorage;
    void* const clonedRoot = cloneStorageFn(0u, 0u, 0u, sourceRoot);
    if (clonedRoot != nullptr) {
      if (clonePayloadFn != nullptr) {
        clonePayloadFn(clonedRoot, previousRoot);
      }
      owner->treeStorage = clonedRoot;
    }
  }
  return sourceRoot;
}

/**
 * Address: 0x007FC250 (FUN_007FC250)
 *
 * What it does:
 * Destroys one D3D texture-batcher lane and frees its storage.
 */
[[maybe_unused]] void DestroyTextureBatcherObjectRuntime(
  void* const batcher,
  const SimpleDtorFn destructorFn
)
{
  if (batcher == nullptr) {
    return;
  }

  if (destructorFn != nullptr) {
    destructorFn(batcher);
  }
  ::operator delete(batcher);
}

/**
 * Address: 0x007FC270 (FUN_007FC270)
 *
 * What it does:
 * Destroys one D3D prim-batcher lane and frees its storage.
 */
[[maybe_unused]] void DestroyPrimBatcherObjectRuntime(
  void* const batcher,
  const SimpleDtorFn destructorFn
)
{
  if (batcher == nullptr) {
    return;
  }

  if (destructorFn != nullptr) {
    destructorFn(batcher);
  }
  ::operator delete(batcher);
}

/**
 * Address: 0x0080DE80 (FUN_0080DE80)
 *
 * What it does:
 * Reads a 3x3 index neighborhood from the tesselator and emits 8 triangles
 * covering the patch around `(column, rowToken)`.
 */
[[maybe_unused]] void EmitPatchTrianglesFromTesselatorRuntime(
  const std::int32_t column,
  void* const tesselator,
  const std::uint8_t* const rowToken,
  const TesselatorGetIndexFn getIndexFn,
  const TesselatorAddTriangleFn addTriangleFn
)
{
  if (tesselator == nullptr || rowToken == nullptr || getIndexFn == nullptr || addTriangleFn == nullptr) {
    return;
  }

  const std::uint32_t source = getIndexFn(tesselator, 0u, rowToken + 0u, column);
  const std::uint32_t topMid = getIndexFn(tesselator, 0u, rowToken + 1u, column);
  const std::uint32_t topRight = getIndexFn(tesselator, 0u, rowToken + 2u, column);

  const std::uint32_t midLeft = getIndexFn(tesselator, 0u, rowToken + 0u, column + 1);
  const std::uint32_t center = getIndexFn(tesselator, 0u, rowToken + 1u, column + 1);
  const std::uint32_t midRight = getIndexFn(tesselator, 0u, rowToken + 2u, column + 1);

  const std::uint32_t bottomLeft = getIndexFn(tesselator, 0u, rowToken + 0u, column + 2);
  const std::uint32_t bottomMid = getIndexFn(tesselator, 0u, rowToken + 1u, column + 2);
  const std::uint32_t bottomRight = getIndexFn(tesselator, 0u, rowToken + 2u, column + 2);

  addTriangleFn(tesselator, source, topMid, center);
  addTriangleFn(tesselator, source, center, midLeft);
  addTriangleFn(tesselator, topMid, topRight, midRight);
  addTriangleFn(tesselator, topMid, midRight, center);
  addTriangleFn(tesselator, midLeft, center, bottomMid);
  addTriangleFn(tesselator, midLeft, bottomMid, bottomLeft);
  addTriangleFn(tesselator, center, midRight, bottomRight);
  addTriangleFn(tesselator, center, bottomRight, bottomMid);
}

/**
 * Address: 0x007408F0 (FUN_007408F0)
 *
 * What it does:
 * Appends one 32-bit lane into a paged four-slot runtime buffer, allocating a
 * backing page when the destination page has not been materialized yet.
 */
[[maybe_unused]] std::uint32_t* AppendPagedWordRuntime(
  FourLanePagedRuntime<std::uint32_t>* const runtime,
  const std::uint32_t* const value
)
{
  if (runtime == nullptr || value == nullptr) {
    return nullptr;
  }

  const std::uint32_t logicalIndex = runtime->baseIndex + runtime->size;
  std::uint32_t** const pageSlot = EnsurePagedFourLanePage(runtime, logicalIndex);
  if (pageSlot == nullptr || *pageSlot == nullptr) {
    return nullptr;
  }

  std::uint32_t* const lane = *pageSlot + (logicalIndex & 3u);
  *lane = *value;
  ++runtime->size;
  return lane;
}

/**
 * Address: 0x00740A60 (FUN_00740A60)
 *
 * What it does:
 * Releases one 16-byte string-range owner array, destroying each owned string
 * subrange before deleting the backing owner storage.
 */
[[maybe_unused]] void DestroyStringRangeBlock16OwnerRuntime(
  RangeOwnerRuntime<StringRangeBlock16Runtime>* const owner
)
{
  if (owner == nullptr) {
    return;
  }

  if (owner->begin != nullptr) {
    for (StringRangeBlock16Runtime* cursor = owner->begin; cursor != owner->end; ++cursor) {
      if (cursor->begin != nullptr) {
        DestroyRangeAndRelease(cursor->begin, cursor->end);
        ::operator delete(static_cast<void*>(cursor->begin));
      }

      cursor->begin = nullptr;
      cursor->end = nullptr;
      cursor->reserved12 = 0u;
    }

    ::operator delete(static_cast<void*>(owner->begin));
  }

  owner->begin = nullptr;
  owner->end = nullptr;
  owner->reserved12 = 0u;
}

/**
 * Address: 0x00740D50 (FUN_00740D50)
 *
 * What it does:
 * Destroys one `SDecalInfo` range and then releases the owning storage block.
 */
[[maybe_unused]] void DestroySDecalInfoRangeOwnerRuntime(
  RangeOwnerRuntime<moho::SDecalInfo>* const owner
)
{
  if (owner == nullptr) {
    return;
  }

  if (owner->begin != nullptr) {
    DestroyRangeAndRelease(owner->begin, owner->end);
    ::operator delete(static_cast<void*>(owner->begin));
  }

  owner->begin = nullptr;
  owner->end = nullptr;
  owner->reserved12 = 0u;
}

/**
 * Address: 0x00740E20 (FUN_00740E20)
 *
 * What it does:
 * Releases one 12-byte shared-control owner range, decrements the embedded
 * control lanes, and deletes the backing storage block.
 */
[[maybe_unused]] void DestroySharedControlRangeOwnerRuntime(
  RangeOwnerRuntime<SharedControlLane12Runtime>* const owner
)
{
  if (owner == nullptr) {
    return;
  }

  if (owner->begin != nullptr) {
    for (SharedControlLane12Runtime* cursor = owner->begin; cursor != owner->end; ++cursor) {
      volatile long* const control = cursor->control;
      if (control != nullptr) {
        if (::InterlockedExchangeAdd(control + 1, -1) == 0) {
          using ReleaseFn = std::intptr_t(__thiscall*)(volatile long*);
          auto* const vtable = reinterpret_cast<ReleaseFn*>(*reinterpret_cast<void**>(const_cast<long*>(control)));
          (void)vtable[1](control);
          if (::InterlockedExchangeAdd(control + 2, -1) == 0) {
            (void)vtable[2](control);
          }
        }
      }
    }

    ::operator delete(static_cast<void*>(owner->begin));
  }

  owner->begin = nullptr;
  owner->end = nullptr;
  owner->reserved12 = 0u;
}

/**
 * Address: 0x00740E90 (FUN_00740E90)
 *
 * What it does:
 * Releases one contiguous `msvc8::string` range and deletes the backing
 * storage block.
 */
[[maybe_unused]] void DestroyStringRangeOwnerRuntime(
  RangeOwnerRuntime<msvc8::string>* const owner
)
{
  if (owner == nullptr) {
    return;
  }

  if (owner->begin != nullptr) {
    DestroyRangeAndRelease(owner->begin, owner->end);
    ::operator delete(static_cast<void*>(owner->begin));
  }

  owner->begin = nullptr;
  owner->end = nullptr;
  owner->reserved12 = 0u;
}

/**
 * Address: 0x00741980 (FUN_00741980)
 *
 * What it does:
 * Destroys every live `SSyncData` pointer in one four-slot paged range.
 */
[[maybe_unused]] void DestroyPagedSyncDataRangeRuntime(
  FourLanePagedRuntime<moho::SSyncData*>* const runtime,
  const std::uint32_t beginIndex,
  const std::uint32_t endIndex
)
{
  if (runtime == nullptr || runtime->pages == nullptr) {
    return;
  }

  for (std::uint32_t logicalIndex = beginIndex; logicalIndex != endIndex; ++logicalIndex) {
    const std::uint32_t pageIndex = logicalIndex >> 2u;
    const std::uint32_t laneIndex = logicalIndex & 3u;
    if (pageIndex >= runtime->pageCount || runtime->pages[pageIndex] == nullptr) {
      continue;
    }

    moho::SSyncData*& queued = runtime->pages[pageIndex][laneIndex];
    if (queued != nullptr) {
      delete queued;
      queued = nullptr;
    }
  }
}

/**
 * Address: 0x007424A0 (FUN_007424A0)
 *
 * What it does:
 * Releases one contiguous `msvc8::string` range owner and deletes the backing
 * storage block.
 */
[[maybe_unused]] void DestroyStringRangeOwnerRuntimeLegacy(
  RangeOwnerRuntime<msvc8::string>* const owner
)
{
  DestroyStringRangeOwnerRuntime(owner);
}
