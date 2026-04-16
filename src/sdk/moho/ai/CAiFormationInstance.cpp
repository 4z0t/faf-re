#include "moho/ai/CAiFormationInstance.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <map>
#include <new>
#include <stdexcept>
#include <typeinfo>
#include <vector>

#include "gpg/core/containers/ArchiveSerialization.h"
#include "gpg/core/containers/ReadArchive.h"
#include "gpg/core/containers/String.h"
#include "gpg/core/containers/WriteArchive.h"
#include "gpg/core/reflection/Reflection.h"
#include "gpg/core/reflection/SerializationError.h"
#include "legacy/containers/Tree.h"
#include "moho/ai/CAiFormationDBImpl.h"
#include "moho/ai/EFormationdStatusTypeInfo.h"
#include "moho/ai/IAiNavigator.h"
#include "moho/command/SSTICommandIssueData.h"
#include "moho/misc/Listener.h"
#include "moho/resource/blueprints/RUnitBlueprint.h"
#include "moho/sim/CArmyImpl.h"
#include "moho/sim/Sim.h"
#include "moho/sim/SOCellPos.h"
#include "moho/unit/Broadcaster.h"
#include "moho/unit/CUnitCommand.h"
#include "moho/unit/CUnitCommandQueue.h"
#include "moho/unit/core/Unit.h"

namespace moho
{
  Wm3::Vector3f* MultQuadVec(Wm3::Vector3f* dest, const Wm3::Vector3f* vec, const Wm3::Quaternionf* quat);
}

namespace
{
  using FormationUnitOffsetMap = std::map<moho::EntId, moho::SUnitOffsetInfo>;
  using FormationCoordMap = std::map<moho::EntId, moho::SCoordsVec2>;

  [[nodiscard]] gpg::RType* CachedEntIdType();
  [[nodiscard]] gpg::RType* CachedSUnitOffsetInfoType();
  [[nodiscard]] gpg::RType* CachedSCoordsVec2Type();

  [[nodiscard]] gpg::RType* CachedBroadcasterEFormationdStatusType()
  {
    static gpg::RType* type = nullptr;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::BroadcasterEventTag<moho::EFormationdStatus>));
    }
    return type;
  }

  /**
   * Address: 0x00570D20 (FUN_00570D20)
   *
   * What it does:
   * Registers `Broadcaster<EFormationdStatus>` as one reflected base lane for
   * `IFormationInstance` at offset `+0x08`.
   */
  void AddBroadcasterEFormationdStatusBaseToIFormationInstanceType(gpg::RType* const typeInfo)
  {
    gpg::RType* const baseType = CachedBroadcasterEFormationdStatusType();
    if (!baseType) {
      return;
    }

    gpg::RField baseField{};
    baseField.mName = baseType->GetName();
    baseField.mType = baseType;
    baseField.mOffset = 8;
    baseField.v4 = 0;
    baseField.mDesc = nullptr;
    typeInfo->AddBase(baseField);
  }

  class SUnitOffsetInfoTypeInfo final : public gpg::RType
  {
  public:
    [[nodiscard]] const char* GetName() const override
    {
      return "SUnitOffsetInfo";
    }

    void Init() override
    {
      size_ = sizeof(moho::SUnitOffsetInfo);
      gpg::RType::Init();
      Finish();
    }
  };

  class IFormationInstanceTypeInfo final : public gpg::RType
  {
  public:
    [[nodiscard]] const char* GetName() const override
    {
      return "IFormationInstance";
    }

    void Init() override
    {
      size_ = sizeof(moho::IFormationInstance);
      gpg::RType::Init();
      AddBroadcasterEFormationdStatusBaseToIFormationInstanceType(this);
      Finish();
    }
  };

  class RMapType_EntId_SUnitOffsetInfo final : public gpg::RType
  {
  public:
    [[nodiscard]] const char* GetName() const override
    {
      return "map<EntId,SUnitOffsetInfo>";
    }

    /**
     * Address: 0x0056DC00 (FUN_0056DC00, gpg::RMapType_EntId_SUnitOffsetInfo::SerSave)
     *
     * What it does:
     * Serializes one `std::map<EntId,SUnitOffsetInfo>` payload by writing key/value
     * pairs with reflected EntId and SUnitOffsetInfo RTTI lanes.
     */
    static void SerSave(gpg::WriteArchive* const archive, const int objectPtr, const int, gpg::RRef* const ownerRef)
    {
      const auto* const mapObject = reinterpret_cast<const FormationUnitOffsetMap*>(
        static_cast<std::uintptr_t>(static_cast<std::uint32_t>(objectPtr))
      );
      if (!archive || !mapObject) {
        return;
      }

      archive->WriteUInt(static_cast<unsigned int>(mapObject->size()));

      gpg::RType* const keyType = CachedEntIdType();
      gpg::RType* const valueType = CachedSUnitOffsetInfoType();
      GPG_ASSERT(keyType != nullptr);
      GPG_ASSERT(valueType != nullptr);
      if (!keyType || !valueType) {
        return;
      }

      const gpg::RRef owner = ownerRef ? *ownerRef : gpg::RRef{};
      for (const auto& [key, value] : *mapObject) {
        archive->Write(keyType, &key, owner);
        archive->Write(valueType, &value, owner);
      }
    }

    void Init() override
    {
      size_ = 0x0C;
      version_ = 1;
      serSaveFunc_ = &RMapType_EntId_SUnitOffsetInfo::SerSave;
      gpg::RType::Init();
      Finish();
    }
  };

  class RBroadcasterRType_EFormationdStatus final : public gpg::RType
  {
  public:
    [[nodiscard]] const char* GetName() const override
    {
      return "Broadcaster<EFormationdStatus>";
    }

    void Init() override
    {
      size_ = sizeof(moho::BroadcasterEventTag<moho::EFormationdStatus>);
      gpg::RType::Init();
      Finish();
    }
  };

  class RListenerRType_EFormationdStatus final : public gpg::RType
  {
  public:
    [[nodiscard]] const char* GetName() const override
    {
      return "Listener<EFormationdStatus>";
    }

    void Init() override
    {
      size_ = sizeof(moho::Listener<moho::EFormationdStatus>);
      gpg::RType::Init();
      Finish();
    }
  };

  class RMapType_EntId_SCoordsVec2 final : public gpg::RType
  {
  public:
    [[nodiscard]] const char* GetName() const override
    {
      return "map<EntId,SCoordsVec2>";
    }

    /**
     * Address: 0x0056E220 (FUN_0056E220, gpg::RMapType_EntId_SCoordsVec2::SerSave)
     *
     * What it does:
     * Serializes one `std::map<EntId,SCoordsVec2>` payload by writing key/value
     * pairs with reflected EntId and SCoordsVec2 RTTI lanes.
     */
    static void SerSave(gpg::WriteArchive* const archive, const int objectPtr, const int, gpg::RRef* const ownerRef)
    {
      const auto* const mapObject = reinterpret_cast<const FormationCoordMap*>(
        static_cast<std::uintptr_t>(static_cast<std::uint32_t>(objectPtr))
      );
      if (!archive || !mapObject) {
        return;
      }

      archive->WriteUInt(static_cast<unsigned int>(mapObject->size()));

      gpg::RType* const keyType = CachedEntIdType();
      gpg::RType* const valueType = CachedSCoordsVec2Type();
      GPG_ASSERT(keyType != nullptr);
      GPG_ASSERT(valueType != nullptr);
      if (!keyType || !valueType) {
        return;
      }

      const gpg::RRef owner = ownerRef ? *ownerRef : gpg::RRef{};
      for (const auto& [key, value] : *mapObject) {
        archive->Write(keyType, &key, owner);
        archive->Write(valueType, &value, owner);
      }
    }

    void Init() override
    {
      size_ = 0x0C;
      version_ = 1;
      serSaveFunc_ = &RMapType_EntId_SCoordsVec2::SerSave;
      gpg::RType::Init();
      Finish();
    }
  };

  [[nodiscard]] gpg::RType* ResolveTypeByAnyName(const std::initializer_list<const char*> names)
  {
    for (const char* const name : names) {
      if (!name) {
        continue;
      }

      if (gpg::RType* const type = gpg::REF_FindTypeNamed(name)) {
        return type;
      }
    }

    return nullptr;
  }

  [[nodiscard]] gpg::RType* CachedCFormationInstanceType()
  {
    static gpg::RType* type = nullptr;
    if (!type) {
      type = ResolveTypeByAnyName({"CFormationInstance", "Moho::CFormationInstance"});
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedSimType()
  {
    if (!moho::Sim::sType) {
      moho::Sim::sType = gpg::LookupRType(typeid(moho::Sim));
    }
    return moho::Sim::sType;
  }

  [[nodiscard]] gpg::RType* CachedWeakPtrIUnitType()
  {
    if (!moho::WeakPtr<moho::IUnit>::sType) {
      moho::WeakPtr<moho::IUnit>::sType = gpg::LookupRType(typeid(moho::WeakPtr<moho::IUnit>));
    }
    return moho::WeakPtr<moho::IUnit>::sType;
  }

  [[nodiscard]] gpg::RType* CachedEntIdType()
  {
    static gpg::RType* type = nullptr;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::EntId));
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedSUnitOffsetInfoType()
  {
    gpg::RType* type = moho::SUnitOffsetInfo::sType;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::SUnitOffsetInfo));
      if (!type) {
        type = moho::preregister_SUnitOffsetInfoTypeInfo();
      }
      moho::SUnitOffsetInfo::sType = type;
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedSCoordsVec2Type()
  {
    static gpg::RType* type = nullptr;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::SCoordsVec2));
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedVector3fType()
  {
    static gpg::RType* type = nullptr;
    if (!type) {
      type = gpg::LookupRType(typeid(Wm3::Vector3<float>));
    }
    return type;
  }

  [[nodiscard]] gpg::RRef MakeSimRef(moho::Sim* sim)
  {
    gpg::RRef out{};
    gpg::RType* const staticType = CachedSimType();
    out.mObj = nullptr;
    out.mType = staticType;
    if (!sim || !staticType) {
      out.mObj = sim;
      return out;
    }

    gpg::RType* dynamicType = staticType;
    try {
      dynamicType = gpg::LookupRType(typeid(*sim));
    } catch (...) {
      dynamicType = staticType;
    }

    std::int32_t baseOffset = 0;
    const bool isDerived = dynamicType != nullptr && dynamicType->IsDerivedFrom(staticType, &baseOffset);
    if (!isDerived) {
      out.mObj = sim;
      out.mType = dynamicType ? dynamicType : staticType;
      return out;
    }

    out.mObj = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(sim) - static_cast<std::uintptr_t>(baseOffset));
    out.mType = dynamicType;
    return out;
  }

  [[nodiscard]] moho::Sim* ReadPointerSim(gpg::ReadArchive* const archive, const gpg::RRef& ownerRef)
  {
    if (!archive) {
      return nullptr;
    }

    const gpg::TrackedPointerInfo& tracked = gpg::ReadRawPointer(archive, ownerRef);
    if (!tracked.object) {
      return nullptr;
    }

    gpg::RType* const expectedType = CachedSimType();
    if (!expectedType || !tracked.type) {
      return static_cast<moho::Sim*>(tracked.object);
    }

    gpg::RRef source{};
    source.mObj = tracked.object;
    source.mType = tracked.type;
    const gpg::RRef upcast = gpg::REF_UpcastPtr(source, expectedType);
    if (upcast.mObj) {
      return static_cast<moho::Sim*>(upcast.mObj);
    }

    const char* const expected = expectedType->GetName();
    const char* const actual = source.GetTypeName();
    const msvc8::string message = gpg::STR_Printf(
      "Error detected in archive: expected a pointer to an object of type \"%s\" but got an object of type \"%s\" "
      "instead",
      expected ? expected : "Sim",
      actual ? actual : "null"
    );
    throw gpg::SerializationError(message.c_str());
  }

  void WritePointerSim(gpg::WriteArchive* const archive, moho::Sim* const sim, const gpg::RRef& ownerRef)
  {
    if (!archive) {
      return;
    }

    const gpg::RRef objectRef = MakeSimRef(sim);
    gpg::WriteRawPointer(archive, objectRef, gpg::TrackedPointerState::Unowned, ownerRef);
  }

  constexpr Wm3::Vec3f kZeroForwardVector{0.0f, 0.0f, 0.0f};
  constexpr Wm3::Quatf kZeroQuaternion{0.0f, 0.0f, 0.0f, 0.0f};

  struct SFormationLinkedUnitRefWordView
  {
    std::uint32_t ownerChainHeadWord;
    std::uint32_t nextChainLinkWord;
  };
  static_assert(
    sizeof(SFormationLinkedUnitRefWordView) == sizeof(moho::SFormationLinkedUnitRef),
    "SFormationLinkedUnitRefWordView size must match SFormationLinkedUnitRef"
  );

  [[nodiscard]] bool BinaryFloatNotEqual(const float lhs, const float rhs) noexcept
  {
    // Matches the recovered x87 `ucomiss` compare shape:
    // true only when values are different and both are not NaN.
    return ((std::isnan(lhs) || std::isnan(rhs)) == (lhs == rhs));
  }

  [[nodiscard]] bool QuaternionEqualsExact(const Wm3::Quatf& lhs, const Wm3::Quatf& rhs) noexcept
  {
    return lhs.w == rhs.w && lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
  }

  /**
   * Address: 0x0056D8E0 (FUN_0056D8E0, sub_56D8E0)
   *
   * What it does:
   * Destroys one coord-cache tree lane by recursively tearing down right
   * branches and iterating through the left spine.
   */
  void DestroyCoordCacheSubtree(moho::SFormationCoordCacheNode* node, const moho::SFormationCoordCacheNode* head)
  {
    while (node != nullptr && node != head && node->isNil == 0u) {
      moho::SFormationCoordCacheNode* const current = node;
      DestroyCoordCacheSubtree(node->right, head);
      node = node->left;
      delete current;
    }
  }

  void ResetCoordCacheMap(moho::SFormationCoordCacheMap& cache)
  {
    moho::SFormationCoordCacheNode* const head = cache.head;
    if (head == nullptr) {
      cache.size = 0;
      return;
    }

    DestroyCoordCacheSubtree(head->parent, head);
    head->parent = head;
    head->left = head;
    head->right = head;
    cache.size = 0;
  }

  template <class T>
  [[nodiscard]] std::uint32_t PtrToWord(T* const ptr) noexcept
  {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ptr));
  }

  template <class T>
  [[nodiscard]] T* WordToPtr(const std::uint32_t word) noexcept
  {
    return reinterpret_cast<T*>(static_cast<std::uintptr_t>(word));
  }

  [[nodiscard]] std::uint32_t EncodeUnitOwnerSlotWord(moho::Unit* const unit) noexcept
  {
    if (!unit) {
      return 0;
    }

    constexpr std::uintptr_t kWeakOwnerLinkOffset = 0x4u;
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(unit) + kWeakOwnerLinkOffset);
  }

  [[nodiscard]] moho::Unit* DecodeUnitOwnerSlotWord(const std::uint32_t ownerWord) noexcept
  {
    constexpr std::uintptr_t kWeakOwnerLinkOffset = 0x4u;
    const auto encoded = static_cast<std::uintptr_t>(ownerWord);
    if (encoded <= kWeakOwnerLinkOffset) {
      return nullptr;
    }

    return reinterpret_cast<moho::Unit*>(encoded - kWeakOwnerLinkOffset);
  }

  void UnlinkWeakWordNode(std::uint32_t& ownerWord, std::uint32_t& nextWord) noexcept
  {
    if (ownerWord == 0u) {
      nextWord = 0u;
      return;
    }

    std::uint32_t* cursor = WordToPtr<std::uint32_t>(ownerWord);
    if (!cursor) {
      ownerWord = 0u;
      nextWord = 0u;
      return;
    }

    const std::uint32_t selfWord = PtrToWord(&ownerWord);
    constexpr int kMaxFollowSteps = 1 << 20;
    for (int i = 0; i < kMaxFollowSteps && *cursor != 0u && *cursor != selfWord; ++i) {
      cursor = moho::SFormationLinkedUnitRef::NextChainLinkSlot(*cursor);
      if (!cursor) {
        break;
      }
    }

    if (cursor && *cursor == selfWord) {
      *cursor = nextWord;
    }

    ownerWord = 0u;
    nextWord = 0u;
  }

  void RelinkWeakWordNode(std::uint32_t& ownerWord, std::uint32_t& nextWord, moho::Unit* const owner) noexcept
  {
    ownerWord = EncodeUnitOwnerSlotWord(owner);
    if (ownerWord == 0u) {
      nextWord = 0u;
      return;
    }

    std::uint32_t* const head = WordToPtr<std::uint32_t>(ownerWord);
    if (!head) {
      ownerWord = 0u;
      nextWord = 0u;
      return;
    }

    nextWord = *head;
    *head = PtrToWord(&ownerWord);
  }

  [[nodiscard]] moho::Unit* DecodeLinkedRefUnit(const moho::SFormationLinkedUnitRef& link) noexcept
  {
    if (!link.ownerChainHead) {
      return nullptr;
    }
    return DecodeUnitOwnerSlotWord(PtrToWord(link.ownerChainHead));
  }

  void UnlinkLinkedRef(moho::SFormationLinkedUnitRef& link) noexcept
  {
    if (!link.ownerChainHead) {
      link.nextChainLink = 0u;
      return;
    }

    std::uint32_t* cursor = link.ownerChainHead;
    const std::uint32_t selfWord = PtrToWord(&link);
    constexpr int kMaxFollowSteps = 1 << 20;
    for (int i = 0; i < kMaxFollowSteps && *cursor != 0u && *cursor != selfWord; ++i) {
      cursor = moho::SFormationLinkedUnitRef::NextChainLinkSlot(*cursor);
      if (!cursor) {
        break;
      }
    }

    if (cursor && *cursor == selfWord) {
      *cursor = link.nextChainLink;
    }

    link.ownerChainHead = nullptr;
    link.nextChainLink = 0u;
  }

  void RelinkLinkedRef(moho::SFormationLinkedUnitRef& link, moho::Unit* const owner) noexcept
  {
    if (!owner) {
      link.ownerChainHead = nullptr;
      link.nextChainLink = 0u;
      return;
    }

    auto* const ownerHead = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(owner) + 0x4u);
    link.ownerChainHead = ownerHead;
    link.nextChainLink = *ownerHead;
    *ownerHead = PtrToWord(&link);
  }

  [[nodiscard]] std::uint32_t UnitEntityIdWord(const moho::Unit* const unit) noexcept
  {
    if (!unit) {
      return 0u;
    }

    return static_cast<std::uint32_t>(unit->GetEntityId());
  }

  void EnsureLaneMapHead(moho::SFormationLaneUnitMap& map)
  {
    if (map.head != nullptr) {
      return;
    }

    auto* const head = new moho::SFormationLaneUnitNode{};
    head->left = head;
    head->parent = head;
    head->right = head;
    head->isNil = 1u;
    map.head = head;
    map.size = 0u;
  }

  [[nodiscard]] moho::SFormationLaneUnitNode* LaneMapFindNode(
    const moho::SFormationLaneUnitMap& map,
    const std::uint32_t unitEntityId
  )
  {
    const moho::SFormationLaneUnitNode* const head = map.head;
    if (!head) {
      return nullptr;
    }

    moho::SFormationLaneUnitNode* node = head->parent;
    while (node && node != head && node->isNil == 0u) {
      if (unitEntityId < node->unitEntityId) {
        node = node->left;
      } else if (node->unitEntityId < unitEntityId) {
        node = node->right;
      } else {
        return node;
      }
    }

    return nullptr;
  }

  void DestroyLaneMapSubtree(moho::SFormationLaneUnitNode* node, const moho::SFormationLaneUnitNode* head)
  {
    if (!node || node == head || node->isNil != 0u) {
      return;
    }

    DestroyLaneMapSubtree(node->left, head);
    DestroyLaneMapSubtree(node->right, head);
    UnlinkWeakWordNode(node->linkedUnitOwnerWord, node->linkedUnitNextWord);
    delete node;
  }

  void ResetLaneMap(moho::SFormationLaneUnitMap& map)
  {
    moho::SFormationLaneUnitNode* const head = map.head;
    if (!head) {
      map.size = 0u;
      return;
    }

    DestroyLaneMapSubtree(head->parent, head);
    head->parent = head;
    head->left = head;
    head->right = head;
    map.size = 0u;
  }

  /**
   * Address: 0x0056CF50 (FUN_0056CF50, sub_56CF50)
   *
   * What it does:
   * Walks one lane-map subtree to its left-most (minimum-key) node.
   */
  [[nodiscard]] moho::SFormationLaneUnitNode* LaneMapLeftmostNode(
    moho::SFormationLaneUnitNode* node
  ) noexcept
  {
    if (node == nullptr || node->isNil != 0u) {
      return node;
    }

    moho::SFormationLaneUnitNode* child = node->left;
    while (child != nullptr && child->isNil == 0u) {
      node = child;
      child = node->left;
    }
    return node;
  }

  /**
   * Address: 0x0056CF30 (FUN_0056CF30, sub_56CF30)
   *
   * What it does:
   * Walks one lane-map subtree to its right-most (maximum-key) node.
   */
  [[nodiscard]] moho::SFormationLaneUnitNode* LaneMapRightmostNode(
    moho::SFormationLaneUnitNode* node
  ) noexcept
  {
    if (node == nullptr || node->isNil != 0u) {
      return node;
    }

    moho::SFormationLaneUnitNode* child = node->right;
    while (child != nullptr && child->isNil == 0u) {
      node = child;
      child = node->right;
    }
    return node;
  }

  [[nodiscard]] moho::SFormationLaneUnitNode* LaneMapMinimumNode(
    moho::SFormationLaneUnitNode* node,
    const moho::SFormationLaneUnitNode* const head
  ) noexcept
  {
    if (node == nullptr || node == head || node->isNil != 0u) {
      return const_cast<moho::SFormationLaneUnitNode*>(head);
    }

    return LaneMapLeftmostNode(node);
  }

  [[nodiscard]] moho::SFormationLaneUnitNode* LaneMapMaximumNode(
    moho::SFormationLaneUnitNode* node,
    const moho::SFormationLaneUnitNode* const head
  ) noexcept
  {
    if (node == nullptr || node == head || node->isNil != 0u) {
      return const_cast<moho::SFormationLaneUnitNode*>(head);
    }

    return LaneMapRightmostNode(node);
  }

  void LaneMapTransplantNode(
    moho::SFormationLaneUnitMap& map,
    moho::SFormationLaneUnitNode* const from,
    moho::SFormationLaneUnitNode* const to
  ) noexcept
  {
    moho::SFormationLaneUnitNode* const head = map.head;
    if (from->parent == head) {
      head->parent = to != nullptr ? to : head;
    } else if (from == from->parent->left) {
      from->parent->left = to;
    } else {
      from->parent->right = to;
    }

    if (to != nullptr && to != head && to->isNil == 0u) {
      to->parent = from->parent;
    }
  }

  /**
   * Address: 0x0056CEE0 (FUN_0056CEE0, sub_56CEE0)
   *
   * What it does:
   * Performs a left rotation around `pivot` inside one lane-map tree.
   */
  [[maybe_unused]] void RotateLaneMapLeft(
    moho::SFormationLaneUnitNode* const pivot,
    moho::SFormationLaneUnitMap& map
  ) noexcept
  {
    if (pivot == nullptr) {
      return;
    }

    moho::SFormationLaneUnitNode* const promoted = pivot->right;
    if (promoted == nullptr) {
      return;
    }

    pivot->right = promoted->left;
    if (pivot->right != nullptr && pivot->right->isNil == 0u) {
      pivot->right->parent = pivot;
    }

    promoted->parent = pivot->parent;
    moho::SFormationLaneUnitNode* const head = map.head;
    if (head == nullptr) {
      return;
    }

    if (pivot == head->parent) {
      head->parent = promoted;
    } else if (pivot == pivot->parent->left) {
      pivot->parent->left = promoted;
    } else {
      pivot->parent->right = promoted;
    }

    promoted->left = pivot;
    pivot->parent = promoted;
  }

  /**
   * Address: 0x0056CF90 (FUN_0056CF90, sub_56CF90)
   *
   * What it does:
   * Performs a right rotation around `pivot` inside one lane-map tree.
   */
  [[maybe_unused]] void RotateLaneMapRight(
    moho::SFormationLaneUnitNode* const pivot,
    moho::SFormationLaneUnitMap& map
  ) noexcept
  {
    if (pivot == nullptr) {
      return;
    }

    moho::SFormationLaneUnitNode* const promoted = pivot->left;
    if (promoted == nullptr) {
      return;
    }

    pivot->left = promoted->right;
    if (pivot->left != nullptr && pivot->left->isNil == 0u) {
      pivot->left->parent = pivot;
    }

    promoted->parent = pivot->parent;
    moho::SFormationLaneUnitNode* const head = map.head;
    if (head == nullptr) {
      return;
    }

    if (pivot == head->parent) {
      head->parent = promoted;
    } else if (pivot == pivot->parent->right) {
      pivot->parent->right = promoted;
    } else {
      pivot->parent->left = promoted;
    }

    promoted->right = pivot;
    pivot->parent = promoted;
  }

  /**
   * Address: 0x0056D090 (FUN_0056D090, sub_56D090)
   *
   * What it does:
   * Advances one lane-map node cursor to its in-order successor.
   */
  [[nodiscard]] moho::SFormationLaneUnitNode* AdvanceLaneMapNodeCursor(
    moho::SFormationLaneUnitNode*& nodeCursor
  ) noexcept
  {
    moho::SFormationLaneUnitNode* result = nodeCursor;
    if (result == nullptr || result->isNil != 0u) {
      return result;
    }

    moho::SFormationLaneUnitNode* child = result->right;
    if (child != nullptr && child->isNil == 0u) {
      nodeCursor = LaneMapLeftmostNode(child);
      return nodeCursor;
    }

    result = result->parent;
    while (result != nullptr && result->isNil == 0u) {
      if (nodeCursor != result->right) {
        break;
      }
      nodeCursor = result;
      result = result->parent;
    }
    nodeCursor = result;
    return result;
  }

  [[nodiscard]] moho::SFormationLaneUnitNode* NextLaneMapNodeInOrder(
    moho::SFormationLaneUnitNode* const node,
    moho::SFormationLaneUnitNode* const head
  ) noexcept;

  /**
   * Address: 0x0056AC60 (FUN_0056AC60, sub_56AC60)
   *
   * What it does:
   * Erases one validated lane-map iterator, repairs the tree links, and
   * returns the in-order successor so callers can continue traversal.
   */
  [[nodiscard]] moho::SFormationLaneUnitNode* EraseLaneMapNodeAndAdvance(
    moho::SFormationLaneUnitMap& map,
    moho::SFormationLaneUnitNode* const node
  )
  {
    moho::SFormationLaneUnitNode* const head = map.head;
    if (head == nullptr || node == nullptr || node == head || node->isNil != 0u) {
      throw std::out_of_range("invalid map/set<T> iterator");
    }

    moho::SFormationLaneUnitNode* const successor = NextLaneMapNodeInOrder(node, head);

    if (node->left == nullptr || node->left == head || node->left->isNil != 0u) {
      LaneMapTransplantNode(map, node, node->right);
    } else if (node->right == nullptr || node->right == head || node->right->isNil != 0u) {
      LaneMapTransplantNode(map, node, node->left);
    } else {
      moho::SFormationLaneUnitNode* const replacement = LaneMapMinimumNode(node->right, head);
      if (replacement->parent != node) {
        LaneMapTransplantNode(map, replacement, replacement->right);
        replacement->right = node->right;
        if (replacement->right != nullptr && replacement->right != head && replacement->right->isNil == 0u) {
          replacement->right->parent = replacement;
        }
      }

      LaneMapTransplantNode(map, node, replacement);
      replacement->left = node->left;
      if (replacement->left != nullptr && replacement->left != head && replacement->left->isNil == 0u) {
        replacement->left->parent = replacement;
      }
    }

    UnlinkWeakWordNode(node->linkedUnitOwnerWord, node->linkedUnitNextWord);
    delete node;
    if (map.size > 0u) {
      --map.size;
    }

    if (map.size == 0u) {
      head->parent = head;
      head->left = head;
      head->right = head;
    } else {
      head->left = LaneMapMinimumNode(head->parent, head);
      head->right = LaneMapMaximumNode(head->parent, head);
    }

    return successor;
  }

  void DestroyLaneMapStorage(moho::SFormationLaneUnitMap& map)
  {
    if (map.head == nullptr) {
      map.size = 0u;
      return;
    }

    ResetLaneMap(map);
    delete map.head;
    map.head = nullptr;
    map.size = 0u;
  }

  void DestroyCoordCacheMapStorage(moho::SFormationCoordCacheMap& cache)
  {
    if (cache.head == nullptr) {
      cache.size = 0u;
      return;
    }

    ResetCoordCacheMap(cache);
    delete cache.head;
    cache.head = nullptr;
    cache.size = 0u;
  }

  void CleanupFormationTransientState(moho::CAiFormationInstance& formation)
  {
    formation.mOccupiedSlots.ResetStorageToInline();
    DestroyCoordCacheMapStorage(formation.mCoordCachePrimary);
    DestroyCoordCacheMapStorage(formation.mCoordCacheSecondary);
    formation.mOrientationBaseline = kZeroQuaternion;

    for (std::int32_t laneIndex = 0; laneIndex < 2; ++laneIndex) {
      moho::SFormationLaneEntry* lane = formation.mLanes[laneIndex].begin();
      const moho::SFormationLaneEntry* const laneEnd = formation.mLanes[laneIndex].end();
      while (lane != laneEnd) {
        UnlinkWeakWordNode(lane->linkedUnitBackLinkHeadWord, lane->linkedUnitBackLinkNextWord);
        DestroyLaneMapStorage(lane->unitMap);
        ++lane;
      }

      formation.mLanes[laneIndex].ResetStorageToInline();
    }
  }

  void CleanupFormationUnitLinks(moho::CAiFormationInstance& formation)
  {
    moho::SFormationLinkedUnitRef* unitRef = formation.mUnits.begin();
    const moho::SFormationLinkedUnitRef* const endRef = formation.mUnits.end();
    while (unitRef != endRef) {
      UnlinkLinkedRef(*unitRef);
      ++unitRef;
    }
    formation.mUnits.ResetStorageToInline();
  }

  void CollectLaneMapNodes(
    const moho::SFormationLaneUnitNode* node,
    const moho::SFormationLaneUnitNode* head,
    std::vector<moho::SFormationLaneUnitNode>& out
  )
  {
    if (!node || node == head || node->isNil != 0u) {
      return;
    }

    CollectLaneMapNodes(node->left, head, out);

    moho::SFormationLaneUnitNode value = *node;
    value.left = nullptr;
    value.parent = nullptr;
    value.right = nullptr;
    value.color = 0u;
    value.isNil = 0u;
    out.push_back(value);

    CollectLaneMapNodes(node->right, head, out);
  }

  [[nodiscard]] moho::SFormationLaneUnitNode* InsertLaneMapNode(
    moho::SFormationLaneUnitMap& map,
    const moho::SFormationLaneUnitNode& src
  )
  {
    EnsureLaneMapHead(map);
    moho::SFormationLaneUnitNode* const head = map.head;

    moho::SFormationLaneUnitNode* parent = head;
    moho::SFormationLaneUnitNode* node = head->parent;
    bool insertLeft = true;

    while (node && node != head && node->isNil == 0u) {
      parent = node;
      if (src.unitEntityId < node->unitEntityId) {
        insertLeft = true;
        node = node->left;
      } else if (node->unitEntityId < src.unitEntityId) {
        insertLeft = false;
        node = node->right;
      } else {
        UnlinkWeakWordNode(node->linkedUnitOwnerWord, node->linkedUnitNextWord);

        node->unitEntityId = src.unitEntityId;
        node->leaderPriority = src.leaderPriority;
        node->formationOffsetX = src.formationOffsetX;
        node->formationOffsetZ = src.formationOffsetZ;
        node->formationVector = src.formationVector;
        node->formationWeight = src.formationWeight;
        node->speedBandLow = src.speedBandLow;
        node->speedBandMid = src.speedBandMid;
        node->speedBandHigh = src.speedBandHigh;
        node->color = src.color;

        RelinkWeakWordNode(
          node->linkedUnitOwnerWord,
          node->linkedUnitNextWord,
          DecodeUnitOwnerSlotWord(src.linkedUnitOwnerWord)
        );
        return node;
      }
    }

    auto* const inserted = new moho::SFormationLaneUnitNode{};
    *inserted = src;
    inserted->left = head;
    inserted->right = head;
    inserted->parent = parent;
    inserted->color = 0u;
    inserted->isNil = 0u;
    inserted->linkedUnitOwnerWord = 0u;
    inserted->linkedUnitNextWord = 0u;

    RelinkWeakWordNode(
      inserted->linkedUnitOwnerWord,
      inserted->linkedUnitNextWord,
      DecodeUnitOwnerSlotWord(src.linkedUnitOwnerWord)
    );

    if (parent == head) {
      head->parent = inserted;
      head->left = inserted;
      head->right = inserted;
    } else if (insertLeft) {
      parent->left = inserted;
      if (head->left == head || inserted->unitEntityId < head->left->unitEntityId) {
        head->left = inserted;
      }
    } else {
      parent->right = inserted;
      if (head->right == head || head->right->unitEntityId < inserted->unitEntityId) {
        head->right = inserted;
      }
    }

    ++map.size;
    return inserted;
  }

  void EraseLaneMapNodeByEntityId(moho::SFormationLaneUnitMap& map, const std::uint32_t unitEntityId)
  {
    moho::SFormationLaneUnitNode* const head = map.head;
    if (!head) {
      return;
    }

    if (moho::SFormationLaneUnitNode* const node = LaneMapFindNode(map, unitEntityId); node != nullptr) {
      (void)EraseLaneMapNodeAndAdvance(map, node);
    }
  }

  /**
   * Address: 0x00570300 (FUN_00570300)
   *
   * What it does:
   * Allocates one `SFormationCoordCacheNode`, clears link lanes, and seeds
   * the marker bytes used by the coord-cache map-head initialization path.
   */
  [[nodiscard]] moho::SFormationCoordCacheNode* AllocateFormationCoordCacheHeadNode()
  {
    auto* const node = new moho::SFormationCoordCacheNode{};
    node->left = nullptr;
    node->parent = nullptr;
    node->right = nullptr;
    node->color = 1u;
    node->isNil = 0u;
    return node;
  }

  void EnsureCoordCacheHead(moho::SFormationCoordCacheMap& cache)
  {
    if (cache.head != nullptr) {
      return;
    }

    auto* const head = AllocateFormationCoordCacheHeadNode();
    head->left = head;
    head->parent = head;
    head->right = head;
    head->isNil = 1u;
    cache.head = head;
    cache.size = 0u;
  }

  /**
   * Address: 0x0056B7B0 (FUN_0056B7B0, sub_56B7B0)
   *
   * What it does:
   * Finds the first coord-cache node whose key is not less than
   * `unitEntityId`.
   */
  [[nodiscard]] moho::SFormationCoordCacheNode* CoordCacheLowerBoundNode(
    const moho::SFormationCoordCacheMap& cache,
    const std::uint32_t unitEntityId
  ) noexcept
  {
    moho::SFormationCoordCacheNode* const head = cache.head;
    if (head == nullptr) {
      return nullptr;
    }

    return msvc8::lower_bound_node<
      moho::SFormationCoordCacheNode,
      &moho::SFormationCoordCacheNode::isNil
    >(head, unitEntityId, [](const moho::SFormationCoordCacheNode& node, const std::uint32_t key) noexcept {
      return node.unitEntityId < key;
    });
  }

  [[nodiscard]] bool IsCoordCacheSentinel(
    const moho::SFormationCoordCacheNode* const node,
    const moho::SFormationCoordCacheNode* const head
  ) noexcept
  {
    return node == nullptr || node == head || node->isNil != 0u;
  }

  [[nodiscard]] moho::SFormationCoordCacheNode* CoordCacheLeftmostNode(
    moho::SFormationCoordCacheNode* node,
    const moho::SFormationCoordCacheNode* const head
  ) noexcept
  {
    while (!IsCoordCacheSentinel(node, head) && !IsCoordCacheSentinel(node->left, head)) {
      node = node->left;
    }
    return node;
  }

  [[nodiscard]] moho::SFormationCoordCacheNode* CoordCacheRightmostNode(
    moho::SFormationCoordCacheNode* node,
    const moho::SFormationCoordCacheNode* const head
  ) noexcept
  {
    while (!IsCoordCacheSentinel(node, head) && !IsCoordCacheSentinel(node->right, head)) {
      node = node->right;
    }
    return node;
  }

  [[nodiscard]] moho::SFormationCoordCacheNode* CoordCachePredecessor(
    moho::SFormationCoordCacheNode* node,
    moho::SFormationCoordCacheNode* const head
  ) noexcept
  {
    if (node == nullptr || head == nullptr) {
      return head;
    }
    if (node == head) {
      return head->right;
    }
    if (!IsCoordCacheSentinel(node->left, head)) {
      return CoordCacheRightmostNode(node->left, head);
    }

    moho::SFormationCoordCacheNode* parent = node->parent;
    while (!IsCoordCacheSentinel(parent, head) && node == parent->left) {
      node = parent;
      parent = parent->parent;
    }
    return parent ? parent : head;
  }

  [[nodiscard]] moho::SFormationCoordCacheNode* CoordCacheSuccessor(
    moho::SFormationCoordCacheNode* node,
    moho::SFormationCoordCacheNode* const head
  ) noexcept
  {
    if (node == nullptr || head == nullptr) {
      return head;
    }
    if (node == head) {
      return head->left;
    }
    if (!IsCoordCacheSentinel(node->right, head)) {
      return CoordCacheLeftmostNode(node->right, head);
    }

    moho::SFormationCoordCacheNode* parent = node->parent;
    while (!IsCoordCacheSentinel(parent, head) && node == parent->right) {
      node = parent;
      parent = parent->parent;
    }
    return parent ? parent : head;
  }

  [[nodiscard]] moho::SFormationCoordCacheNode* AllocateCoordCacheNode(
    moho::SFormationCoordCacheNode* const head,
    const std::uint32_t unitEntityId,
    const moho::SCoordsVec2& position
  )
  {
    auto* const inserted = new moho::SFormationCoordCacheNode{};
    inserted->left = head;
    inserted->parent = head;
    inserted->right = head;
    inserted->unitEntityId = unitEntityId;
    inserted->position = position;
    inserted->color = 0u;
    inserted->isNil = 0u;
    return inserted;
  }

  void LinkCoordCacheNode(
    moho::SFormationCoordCacheMap& cache,
    moho::SFormationCoordCacheNode* const parent,
    moho::SFormationCoordCacheNode* const inserted,
    const bool insertLeft
  ) noexcept
  {
    moho::SFormationCoordCacheNode* const head = cache.head;
    if (head == nullptr || inserted == nullptr) {
      return;
    }

    inserted->parent = parent;
    if (parent == head) {
      head->parent = inserted;
      head->left = inserted;
      head->right = inserted;
      ++cache.size;
      return;
    }

    if (insertLeft) {
      parent->left = inserted;
      if (head->left == head || head->left == parent || inserted->unitEntityId < head->left->unitEntityId) {
        head->left = inserted;
      }
    } else {
      parent->right = inserted;
      if (head->right == head || head->right == parent || head->right->unitEntityId < inserted->unitEntityId) {
        head->right = inserted;
      }
    }
    ++cache.size;
  }

  [[nodiscard]] moho::SFormationCoordCacheNode* CoordCacheInsertBySearch(
    moho::SFormationCoordCacheMap& cache,
    const std::uint32_t unitEntityId,
    const moho::SCoordsVec2& position
  )
  {
    moho::SFormationCoordCacheNode* const head = cache.head;
    if (head == nullptr) {
      return nullptr;
    }

    moho::SFormationCoordCacheNode* parent = head;
    moho::SFormationCoordCacheNode* cursor = head->parent;
    bool insertLeft = true;
    while (!IsCoordCacheSentinel(cursor, head)) {
      parent = cursor;
      if (unitEntityId < cursor->unitEntityId) {
        insertLeft = true;
        cursor = cursor->left;
      } else {
        insertLeft = false;
        cursor = cursor->right;
      }
    }

    moho::SFormationCoordCacheNode* const inserted = AllocateCoordCacheNode(head, unitEntityId, position);
    LinkCoordCacheNode(cache, parent, inserted, insertLeft);
    return inserted;
  }

  [[nodiscard]] moho::SFormationCoordCacheNode* CoordCacheInsertBeforeHint(
    moho::SFormationCoordCacheMap& cache,
    moho::SFormationCoordCacheNode* hint,
    const std::uint32_t unitEntityId,
    const moho::SCoordsVec2& position
  )
  {
    moho::SFormationCoordCacheNode* const head = cache.head;
    if (head == nullptr) {
      return nullptr;
    }

    if (hint == nullptr) {
      hint = head;
    }

    moho::SFormationCoordCacheNode* parent = head;
    bool insertLeft = true;
    if (hint == head) {
      parent = head->right;
      insertLeft = false;
      if (IsCoordCacheSentinel(parent, head)) {
        parent = head;
        insertLeft = true;
      }
    } else if (IsCoordCacheSentinel(hint->left, head)) {
      parent = hint;
      insertLeft = true;
    } else {
      parent = CoordCacheRightmostNode(hint->left, head);
      insertLeft = false;
    }

    moho::SFormationCoordCacheNode* const inserted = AllocateCoordCacheNode(head, unitEntityId, position);
    LinkCoordCacheNode(cache, parent, inserted, insertLeft);
    return inserted;
  }

  [[nodiscard]] moho::SFormationCoordCacheNode* CoordCacheInsertAfterHint(
    moho::SFormationCoordCacheMap& cache,
    moho::SFormationCoordCacheNode* hint,
    const std::uint32_t unitEntityId,
    const moho::SCoordsVec2& position
  )
  {
    moho::SFormationCoordCacheNode* const head = cache.head;
    if (head == nullptr) {
      return nullptr;
    }

    if (hint == nullptr || hint == head) {
      return CoordCacheInsertBySearch(cache, unitEntityId, position);
    }

    moho::SFormationCoordCacheNode* parent = head;
    bool insertLeft = false;
    if (IsCoordCacheSentinel(hint->right, head)) {
      parent = hint;
      insertLeft = false;
    } else {
      parent = CoordCacheLeftmostNode(hint->right, head);
      insertLeft = true;
    }

    moho::SFormationCoordCacheNode* const inserted = AllocateCoordCacheNode(head, unitEntityId, position);
    LinkCoordCacheNode(cache, parent, inserted, insertLeft);
    return inserted;
  }

  /**
   * Address: 0x0056D790 (FUN_0056D790, sub_56D790)
   *
   * What it does:
   * Uses one coord-cache insertion hint to resolve an existing node by key or
   * inserts a new node with `position` when the key is missing.
   */
  [[nodiscard]] moho::SFormationCoordCacheNode* ResolveCoordCacheNodeWithHint(
    moho::SFormationCoordCacheMap& cache,
    const std::uint32_t unitEntityId,
    const moho::SCoordsVec2& position,
    moho::SFormationCoordCacheNode* hint
  )
  {
    moho::SFormationCoordCacheNode* const head = cache.head;
    if (head == nullptr) {
      return nullptr;
    }

    if (cache.size == 0u) {
      return CoordCacheInsertBySearch(cache, unitEntityId, position);
    }

    if (hint == nullptr) {
      hint = head;
    }

    if (hint == head->left) {
      if (!IsCoordCacheSentinel(hint, head) && unitEntityId < hint->unitEntityId) {
        return CoordCacheInsertBeforeHint(cache, hint, unitEntityId, position);
      }
    } else if (hint == head) {
      moho::SFormationCoordCacheNode* const rightMost = head->right;
      if (!IsCoordCacheSentinel(rightMost, head) && rightMost->unitEntityId < unitEntityId) {
        return CoordCacheInsertAfterHint(cache, rightMost, unitEntityId, position);
      }
    } else if (unitEntityId < hint->unitEntityId) {
      moho::SFormationCoordCacheNode* const predecessor = CoordCachePredecessor(hint, head);
      if (!IsCoordCacheSentinel(predecessor, head) && predecessor->unitEntityId < unitEntityId) {
        if (IsCoordCacheSentinel(predecessor->right, head)) {
          return CoordCacheInsertBeforeHint(cache, hint, unitEntityId, position);
        }
        return CoordCacheInsertAfterHint(cache, predecessor, unitEntityId, position);
      }
    } else if (hint->unitEntityId < unitEntityId) {
      moho::SFormationCoordCacheNode* const successor = CoordCacheSuccessor(hint, head);
      if (successor == head || unitEntityId < successor->unitEntityId) {
        if (IsCoordCacheSentinel(hint->right, head)) {
          return CoordCacheInsertBeforeHint(cache, successor, unitEntityId, position);
        }
        return CoordCacheInsertAfterHint(cache, hint, unitEntityId, position);
      }
    } else {
      return hint;
    }

    moho::SFormationCoordCacheNode* const node = CoordCacheLowerBoundNode(cache, unitEntityId);
    if (node != nullptr && node != head && node->isNil == 0u && node->unitEntityId == unitEntityId) {
      return node;
    }
    return CoordCacheInsertBySearch(cache, unitEntityId, position);
  }

  [[nodiscard]] moho::SFormationCoordCacheNode* CoordCacheFindNode(
    const moho::SFormationCoordCacheMap& cache,
    const std::uint32_t unitEntityId
  ) noexcept
  {
    moho::SFormationCoordCacheNode* const node = CoordCacheLowerBoundNode(cache, unitEntityId);
    if (node == nullptr || node == cache.head || node->isNil != 0u || node->unitEntityId != unitEntityId) {
      return nullptr;
    }

    return node;
  }

  /**
   * Address: 0x0056B6C0 (FUN_0056B6C0, sub_56B6C0)
   *
   * What it does:
   * Looks up the cache slot for `unitEntityId` in the coord-cache tree and
   * either returns the existing position reference or inserts a new node and
   * returns the new position reference.
   */
  [[nodiscard]] moho::SCoordsVec2* CoordCacheInsertOrAssign(
    moho::SFormationCoordCacheMap& cache,
    const std::uint32_t unitEntityId,
    const moho::SCoordsVec2& position
  )
  {
    EnsureCoordCacheHead(cache);
    moho::SFormationCoordCacheNode* const head = cache.head;
    moho::SFormationCoordCacheNode* const lowerBound = CoordCacheLowerBoundNode(cache, unitEntityId);
    if (lowerBound != nullptr && lowerBound != head && lowerBound->isNil == 0u && lowerBound->unitEntityId == unitEntityId) {
      lowerBound->position = position;
      return &lowerBound->position;
    }

    moho::SFormationCoordCacheNode* const resolved =
      ResolveCoordCacheNodeWithHint(cache, unitEntityId, position, lowerBound);
    if (resolved == nullptr) {
      return nullptr;
    }
    resolved->position = position;
    return &resolved->position;
  }

  /**
   * Address: 0x005688C0 (FUN_005688C0, sub_5688C0)
   *
   * What it does:
   * Returns true when two formation lane rectangles overlap on both axes.
   */
  [[nodiscard]] bool LaneEntriesOverlap(
    const moho::SFormationLaneEntry& lhs,
    const moho::SFormationLaneEntry& rhs
  ) noexcept
  {
    const bool overlapX = (lhs.overlapRadiusX - lhs.overlapAnchorX) <= (rhs.overlapAnchorX + rhs.overlapRadiusX)
      && (rhs.overlapRadiusX - rhs.overlapAnchorX) <= (lhs.overlapAnchorX + lhs.overlapRadiusX);
    if (!overlapX) {
      return false;
    }

    const bool overlapZ = (lhs.overlapRadiusZ - lhs.overlapAnchorZ) <= (rhs.overlapAnchorZ + rhs.overlapRadiusZ)
      && (rhs.overlapRadiusZ - rhs.overlapAnchorZ) <= (lhs.overlapAnchorZ + lhs.overlapRadiusZ);
    return overlapZ;
  }

  /**
   * Address: 0x005725A0 (FUN_005725A0, sub_5725A0)
   *
   * What it does:
   * Rebuilds a linked-unit reference lane from a compacted list of surviving
   * units while preserving intrusive owner-chain wiring for each entry.
   */
  void ResetLinkedUnitRefsFromUnits(
    moho::CAiFormationInstance& formation,
    const std::vector<moho::Unit*>& keptUnits
  )
  {
    formation.mUnits.ResetStorageToInline();
    for (moho::Unit* const keptUnit : keptUnits) {
      moho::SFormationLinkedUnitRef linked{};
      formation.mUnits.push_back(linked);
      RelinkLinkedRef(formation.mUnits.back(), keptUnit);
    }
  }

  struct FormationUpdateListenerNode
  {
    void** vtable;                                // +0x00
    moho::TDatListItem<void, void> updateLink;   // +0x04
  };
  static_assert(
    offsetof(FormationUpdateListenerNode, updateLink) == 0x04,
    "FormationUpdateListenerNode::updateLink offset must be 0x04"
  );

  [[nodiscard]] FormationUpdateListenerNode* ListenerOwnerFromLink(
    moho::TDatListItem<void, void>* const link
  ) noexcept
  {
    if (link == nullptr) {
      return nullptr;
    }
    return reinterpret_cast<FormationUpdateListenerNode*>(
      reinterpret_cast<std::uintptr_t>(link) - offsetof(FormationUpdateListenerNode, updateLink)
    );
  }

  /**
   * Address: 0x0056B070 (FUN_0056B070, sub_56B070)
   *
   * What it does:
   * Detaches one intrusive listener ring, relinks listeners back to the owner
   * head one-by-one, and dispatches one integer update event through each
   * listener's vtable slot-0 callback.
   */
  [[maybe_unused]] void DispatchFormationUpdateEvent(
    const std::int32_t eventCode,
    moho::TDatListItem<void, void>& listenerHead
  )
  {
    moho::TDatListItem<void, void> detached{};
    if (listenerHead.mNext == &listenerHead) {
      return;
    }

    detached.mNext = listenerHead.mNext;
    detached.mPrev = listenerHead.mPrev;
    detached.mNext->mPrev = &detached;
    detached.mPrev->mNext = &detached;
    listenerHead.ListResetLinks();

    while (detached.mNext != &detached) {
      auto* const listenerLink = detached.mNext;
      listenerLink->ListLinkAfter(&listenerHead);

      using OnEventFn = void(__thiscall*)(FormationUpdateListenerNode*, std::int32_t);
      if (FormationUpdateListenerNode* const listener = ListenerOwnerFromLink(listenerLink);
          listener != nullptr && listener->vtable != nullptr && listener->vtable[0] != nullptr) {
        reinterpret_cast<OnEventFn>(listener->vtable[0])(listener, eventCode);
      }
    }

    detached.mNext->mPrev = detached.mPrev;
    detached.mPrev->mNext = detached.mNext;
  }

  [[nodiscard]] moho::SFormationLaneUnitNode* NextLaneMapNodeInOrder(
    moho::SFormationLaneUnitNode* const node,
    moho::SFormationLaneUnitNode* const head
  ) noexcept
  {
    if (node == nullptr || head == nullptr || node == head || node->isNil != 0u) {
      return head;
    }

    moho::SFormationLaneUnitNode* cursor = node;
    (void)AdvanceLaneMapNodeCursor(cursor);
    if (cursor == nullptr || cursor->isNil != 0u) {
      return head;
    }
    return cursor;
  }

  /**
   * Address: 0x0056EB40 (FUN_0056EB40, sub_56EB40)
   *
   * What it does:
   * Erases one lane-map node range [`beginNode`, `endNode`) and returns the
   * next in-order node after the erased span; includes full-map clear fast path.
   */
  [[maybe_unused]] moho::SFormationLaneUnitNode* EraseLaneMapNodeRange(
    moho::SFormationLaneUnitMap& map,
    moho::SFormationLaneUnitNode*& outNextNode,
    moho::SFormationLaneUnitNode* beginNode,
    moho::SFormationLaneUnitNode* endNode
  )
  {
    moho::SFormationLaneUnitNode* const head = map.head;
    if (head == nullptr) {
      outNextNode = nullptr;
      return outNextNode;
    }

    if (beginNode == head->left && endNode == head) {
      ResetLaneMap(map);
      outNextNode = head->left;
      return outNextNode;
    }

    const bool endIsHead = (endNode == head);
    const std::uint32_t endEntityId =
      (!endIsHead && endNode != nullptr && endNode != head && endNode->isNil == 0u) ? endNode->unitEntityId : 0u;

    auto resolveEndNode = [&map, endIsHead, endEntityId]() -> moho::SFormationLaneUnitNode* {
      moho::SFormationLaneUnitNode* const currentHead = map.head;
      if (currentHead == nullptr) {
        return nullptr;
      }
      if (endIsHead) {
        return currentHead;
      }
      if (moho::SFormationLaneUnitNode* const resolved = LaneMapFindNode(map, endEntityId); resolved != nullptr) {
        return resolved;
      }
      return currentHead;
    };

    moho::SFormationLaneUnitNode* current = beginNode;
    moho::SFormationLaneUnitNode* resolvedEnd = resolveEndNode();
    while (current != nullptr && current != resolvedEnd) {
      if (current == map.head || current->isNil != 0u) {
        break;
      }

      const moho::SFormationLaneUnitNode* const eraseNode = current;
      moho::SFormationLaneUnitNode* const successor = NextLaneMapNodeInOrder(current, map.head);
      const bool successorIsHead = successor == nullptr || successor == map.head || successor->isNil != 0u;
      const std::uint32_t successorEntityId = successorIsHead ? 0u : successor->unitEntityId;

      EraseLaneMapNodeByEntityId(map, eraseNode->unitEntityId);

      if (map.head == nullptr) {
        current = nullptr;
        resolvedEnd = nullptr;
        break;
      }

      resolvedEnd = resolveEndNode();
      if (successorIsHead) {
        current = map.head;
      } else if (moho::SFormationLaneUnitNode* const resolved = LaneMapFindNode(map, successorEntityId);
                 resolved != nullptr) {
        current = resolved;
      } else {
        current = map.head;
      }
    }

    outNextNode = current;
    return outNextNode;
  }

  /**
   * Address: 0x00568980 (FUN_00568980, sub_568980)
   *
   * What it does:
   * For non-guard formation commands, scans overlap between lane-0 entries and
   * both lane groups, then merges overlap extents/speed bands with a minimum
   * floor to keep coupled lane movement consistent.
   */
  [[maybe_unused]] void MergeOverlappingLaneBands(moho::CAiFormationInstance& formation)
  {
    if (formation.mCommandType == moho::EUnitCommandType::UNITCOMMAND_Guard) {
      return;
    }

    constexpr float kBandFloor = 10.0f;
    moho::SFormationLaneEntry* lane0Entry = formation.mLanes[0].begin();
    const moho::SFormationLaneEntry* const lane0End = formation.mLanes[0].end();
    while (lane0Entry != lane0End) {
      for (std::int32_t laneIndex = 0; laneIndex < 2; ++laneIndex) {
        moho::SFormationLaneEntry* candidate = formation.mLanes[laneIndex].begin();
        const moho::SFormationLaneEntry* const laneEnd = formation.mLanes[laneIndex].end();
        while (candidate != laneEnd) {
          if (LaneEntriesOverlap(*candidate, *lane0Entry)) {
            float mergedBandA = std::max(lane0Entry->overlapAnchorX, candidate->overlapAnchorX);
            if (mergedBandA < kBandFloor) {
              mergedBandA = kBandFloor;
            }

            float mergedBandB = std::max(lane0Entry->overlapAnchorZ, candidate->overlapAnchorZ);
            if (mergedBandB < kBandFloor) {
              mergedBandB = kBandFloor;
            }

            const float mergedSpeed = std::min(lane0Entry->preferredSpeed, candidate->preferredSpeed);

            candidate->overlapRadiusX = lane0Entry->overlapRadiusX;
            candidate->overlapRadiusZ = lane0Entry->overlapRadiusZ;
            candidate->overlapAnchorX = mergedBandA;
            candidate->overlapAnchorZ = mergedBandB;
            candidate->preferredSpeed = mergedSpeed;

            lane0Entry->overlapAnchorX = mergedBandA;
            lane0Entry->overlapAnchorZ = mergedBandB;
            lane0Entry->preferredSpeed = mergedSpeed;
          }
          ++candidate;
        }
      }
      ++lane0Entry;
    }
  }

  void FindBestLeaderInLane(
    const moho::SFormationLaneUnitNode* node,
    const moho::SFormationLaneUnitNode* head,
    std::int32_t& bestPriority,
    moho::Unit*& bestUnit
  )
  {
    if (!node || node == head || node->isNil != 0u) {
      return;
    }

    FindBestLeaderInLane(node->left, head, bestPriority, bestUnit);
    if (moho::Unit* const candidate = DecodeUnitOwnerSlotWord(node->linkedUnitOwnerWord);
        candidate != nullptr && node->leaderPriority > bestPriority) {
      bestPriority = node->leaderPriority;
      bestUnit = candidate;
    }
    FindBestLeaderInLane(node->right, head, bestPriority, bestUnit);
  }

  /**
   * Address: 0x0059A300 (FUN_0059A300, sub_59A300)
   *
   * What it does:
   * Returns one lane leader resolved from `laneEntry.unitMap`; when no cached
   * weak backlink is active, it recomputes the best-priority unit and rewires
   * the backlink chain to that owner.
   */
  [[nodiscard]] moho::Unit* SelectLaneLeader(moho::SFormationLaneEntry& laneEntry)
  {
    if (laneEntry.linkedUnitBackLinkHeadWord == 0u || laneEntry.linkedUnitBackLinkHeadWord == 0x4u) {
      std::int32_t bestPriority = 0;
      moho::Unit* bestUnit = nullptr;

      const moho::SFormationLaneUnitNode* const head = laneEntry.unitMap.head;
      if (head) {
        FindBestLeaderInLane(head->parent, head, bestPriority, bestUnit);
      }

      const std::uint32_t desiredOwnerWord = EncodeUnitOwnerSlotWord(bestUnit);
      if (desiredOwnerWord != laneEntry.linkedUnitBackLinkHeadWord) {
        UnlinkWeakWordNode(laneEntry.linkedUnitBackLinkHeadWord, laneEntry.linkedUnitBackLinkNextWord);
        RelinkWeakWordNode(laneEntry.linkedUnitBackLinkHeadWord, laneEntry.linkedUnitBackLinkNextWord, bestUnit);
      }
    }

    return DecodeUnitOwnerSlotWord(laneEntry.linkedUnitBackLinkHeadWord);
  }

  /**
   * Address: 0x0059A970 (FUN_0059A970, sub_59A970)
   *
   * What it does:
   * Resolves the effective lane-leader unit for one lane entry during update:
   * when processing lane 1 it first checks overlap against lane-0 entries and
   * may switch leader to the first overlapping lane, then applies guard-command
   * remap to guarded target unit.
   */
  [[nodiscard]] moho::Unit* ResolveUpdateLaneLeader(
    const std::int32_t laneIndex,
    moho::CAiFormationInstance& formation,
    moho::SFormationLaneEntry& laneEntry
  )
  {
    moho::Unit* leader = SelectLaneLeader(laneEntry);

    if (laneIndex == 1) {
      moho::SFormationLaneEntry* candidate = formation.mLanes[0].begin();
      const moho::SFormationLaneEntry* const end = formation.mLanes[0].end();
      while (candidate != end) {
        if (LaneEntriesOverlap(*candidate, laneEntry)) {
          leader = SelectLaneLeader(*candidate);
          break;
        }
        ++candidate;
      }
    }

    if (formation.mCommandType != moho::EUnitCommandType::UNITCOMMAND_Guard || leader == nullptr) {
      return leader;
    }

    moho::Unit* const runtimeLeader = leader->IsUnit();
    if (runtimeLeader == nullptr) {
      return nullptr;
    }
    return runtimeLeader->GuardedUnitRef.ResolveObjectPtr<moho::Unit>();
  }

  void RefreshFormationPlanIfRequested(moho::CAiFormationInstance& formation)
  {
    if (formation.mPlanUpdateRequested == 0u) {
      return;
    }

    formation.mPlanUpdateRequested = 0u;
    (void)formation.RemoveDeadUnits(nullptr);

    // Binary also executes CFormationInstance::CleanupFormation (0x00568AC0)
    // and CAiFormationInstance::UpdateFormation (0x00568CA0) here; those
    // dependencies are tracked separately.
  }

  [[nodiscard]] bool IsBusyFormationQueueCommand(const moho::EUnitCommandType commandType) noexcept
  {
    switch (commandType) {
    case moho::EUnitCommandType::UNITCOMMAND_Move:
    case moho::EUnitCommandType::UNITCOMMAND_Attack:
    case moho::EUnitCommandType::UNITCOMMAND_Patrol:
    case moho::EUnitCommandType::UNITCOMMAND_FormMove:
    case moho::EUnitCommandType::UNITCOMMAND_FormAttack:
    case moho::EUnitCommandType::UNITCOMMAND_FormPatrol:
    case moho::EUnitCommandType::UNITCOMMAND_Guard:
      return true;
    default:
      return false;
    }
  }

  [[nodiscard]] bool CanPlaceFormationSlot(
    const moho::CAiFormationInstance& formation,
    const moho::SCoordsVec2& position,
    const moho::SFootprint& footprint,
    const std::int32_t footprintSize,
    const bool useWholeMap,
    const std::int32_t laneToken
  )
  {
    if (formation.mSim == nullptr || formation.mSim->mOGrid == nullptr || formation.mSim->mMapData == nullptr) {
      return false;
    }

    if (footprint.FitsAt(position, *formation.mSim->mOGrid) != static_cast<moho::EOccupancyCaps>(0u)) {
      return false;
    }

    const Wm3::Vec3f worldPos{position.x, 0.0f, position.z};
    if (!formation.mSim->mMapData->IsWithin(worldPos, static_cast<float>(footprintSize), useWholeMap)) {
      return false;
    }

    return formation.Func27(position, footprintSize, laneToken);
  }
} // namespace

namespace moho
{
  /**
   * Address: 0x005661C0 (FUN_005661C0, preregister_SUnitOffsetInfoTypeInfo)
   *
   * What it does:
   * Constructs/preregisters RTTI metadata for `SUnitOffsetInfo`.
   */
  gpg::RType* preregister_SUnitOffsetInfoTypeInfo()
  {
    static SUnitOffsetInfoTypeInfo typeInfo;
    gpg::PreRegisterRType(typeid(SUnitOffsetInfo), &typeInfo);
    SUnitOffsetInfo::sType = &typeInfo;
    return &typeInfo;
  }

  /**
   * Address: 0x005665B0 (FUN_005665B0, preregister_IFormationInstanceTypeInfo)
   *
   * What it does:
   * Constructs/preregisters RTTI metadata for `IFormationInstance`.
   */
  gpg::RType* preregister_IFormationInstanceTypeInfo()
  {
    static IFormationInstanceTypeInfo typeInfo;
    gpg::PreRegisterRType(typeid(IFormationInstance), &typeInfo);
    IFormationInstance::sType = &typeInfo;
    return &typeInfo;
  }

  /**
   * Address: 0x00571A70 (FUN_00571A70, preregister_RMapType_EntId_SUnitOffsetInfo)
   *
   * What it does:
   * Constructs/preregisters RTTI metadata for
   * `std::map<EntId,SUnitOffsetInfo>`.
   */
  gpg::RType* preregister_RMapType_EntId_SUnitOffsetInfo()
  {
    static RMapType_EntId_SUnitOffsetInfo typeInfo;
    gpg::PreRegisterRType(typeid(FormationUnitOffsetMap), &typeInfo);
    return &typeInfo;
  }

  /**
   * Address: 0x00571AD0 (FUN_00571AD0, preregister_RBroadcasterRType_EFormationdStatus)
   *
   * What it does:
   * Constructs/preregisters RTTI metadata for
   * `Broadcaster<EFormationdStatus>`.
   */
  gpg::RType* preregister_RBroadcasterRType_EFormationdStatus()
  {
    static RBroadcasterRType_EFormationdStatus typeInfo;
    gpg::PreRegisterRType(typeid(BroadcasterEventTag<EFormationdStatus>), &typeInfo);
    return &typeInfo;
  }

  /**
   * Address: 0x00571B30 (FUN_00571B30, preregister_RListenerRType_EFormationdStatus)
   *
   * What it does:
   * Constructs/preregisters RTTI metadata for `Listener<EFormationdStatus>`.
   */
  gpg::RType* preregister_RListenerRType_EFormationdStatus()
  {
    static RListenerRType_EFormationdStatus typeInfo;
    gpg::PreRegisterRType(typeid(Listener<EFormationdStatus>), &typeInfo);
    return &typeInfo;
  }

  /**
   * Address: 0x00571CE0 (FUN_00571CE0, preregister_RMapType_EntId_SCoordsVec2)
   *
   * What it does:
   * Constructs/preregisters RTTI metadata for `std::map<EntId,SCoordsVec2>`.
   */
  gpg::RType* preregister_RMapType_EntId_SCoordsVec2()
  {
    static RMapType_EntId_SCoordsVec2 typeInfo;
    gpg::PreRegisterRType(typeid(FormationCoordMap), &typeInfo);
    return &typeInfo;
  }

  /**
   * Address: 0x00569CA0 (FUN_00569CA0, Moho::CFormationInstance::CalcFormationSpeed)
   *
   * What it does:
   * Models the base `CFormationInstance` speed-stub lane that returns zero
   * speed for non-specialized formation owners.
   */
  float CFormationInstanceCalcFormationSpeedFallback(Unit*, float*, SFormationLaneEntry*)
  {
    return 0.0f;
  }

  std::uint32_t* SFormationLinkedUnitRef::NextChainLinkSlot(const std::uint32_t linkWord) noexcept
  {
    auto* const link = reinterpret_cast<SFormationLinkedUnitRefWordView*>(static_cast<std::uintptr_t>(linkWord));
    return &link->nextChainLinkWord;
  }

  /**
   * Address: 0x0059A500 (FUN_0059A500, ??1CAiFormationInstance@Moho@@QAE@@Z)
   * Mangled: ??1CAiFormationInstance@Moho@@QAE@@Z
   *
   * What it does:
   * Clears transient formation caches and lane ownership state, unregisters
   * this instance from the owning formation DB, then tears down unit links.
   */
  CAiFormationInstance::~CAiFormationInstance()
  {
    CleanupFormationTransientState(*this);
    mSim->mFormationDB->RemoveFormation(this);
    CleanupFormationUnitLinks(*this);
    mUnitLinkListHead.ListUnlink();
  }

  /**
   * Address: 0x00570E20 (FUN_00570E20, Moho::SAssignedLocInfo::MemberDeserialize)
   *
   * What it does:
   * Loads one occupied-slot lane: assigned 2D position, footprint size, and
   * lane token.
   */
  void SFormationOccupiedSlot::MemberDeserialize(SFormationOccupiedSlot* const slot, gpg::ReadArchive* const archive)
  {
    if (!archive || !slot) {
      return;
    }

    const gpg::RRef ownerRef{};
    gpg::RType* const coordsType = CachedSCoordsVec2Type();
    GPG_ASSERT(coordsType != nullptr);
    if (coordsType) {
      archive->Read(coordsType, &slot->position, ownerRef);
    }

    archive->ReadInt(&slot->footprintSize);
    archive->ReadInt(&slot->laneToken);
  }

  /**
   * Address: 0x00570E80 (FUN_00570E80, Moho::SAssignedLocInfo::MemberSerialize)
   *
   * What it does:
   * Stores one occupied-slot lane: assigned 2D position, footprint size, and
   * lane token.
   */
  void SFormationOccupiedSlot::MemberSerialize(const SFormationOccupiedSlot* const slot, gpg::WriteArchive* const archive)
  {
    if (!archive || !slot) {
      return;
    }

    const gpg::RRef ownerRef{};
    gpg::RType* const coordsType = CachedSCoordsVec2Type();
    GPG_ASSERT(coordsType != nullptr);
    if (coordsType) {
      archive->Write(coordsType, &slot->position, ownerRef);
    }

    archive->WriteInt(slot->footprintSize);
    archive->WriteInt(slot->laneToken);
  }

  /**
   * Address: 0x005707B0 (FUN_005707B0, Moho::SUnitOffsetInfo::MemberDeserialize)
   *
   * What it does:
   * Loads one unit-offset payload lane: weak-unit link, leader priority,
   * 2D offset, 3D direction, and speed-band weights.
   */
  void SUnitOffsetInfo::MemberDeserialize(gpg::ReadArchive* const archive)
  {
    if (!archive) {
      return;
    }

    const gpg::RRef ownerRef{};

    gpg::RType* const weakPtrType = CachedWeakPtrIUnitType();
    GPG_ASSERT(weakPtrType != nullptr);
    if (weakPtrType) {
      archive->Read(weakPtrType, &mUnit, ownerRef);
    }

    archive->ReadInt(&mLeaderPriority);

    gpg::RType* const coordsType = CachedSCoordsVec2Type();
    GPG_ASSERT(coordsType != nullptr);
    if (coordsType) {
      archive->Read(coordsType, &mOffset, ownerRef);
    }

    gpg::RType* const vectorType = CachedVector3fType();
    GPG_ASSERT(vectorType != nullptr);
    if (vectorType) {
      archive->Read(vectorType, &mDirection, ownerRef);
    }

    archive->ReadFloat(&mWeight);
    archive->ReadFloat(&mSpeedBandLow);
    archive->ReadFloat(&mSpeedBandMid);
    archive->ReadFloat(&mSpeedBandHigh);
  }

  /**
   * Address: 0x005708A0 (FUN_005708A0, Moho::SUnitOffsetInfo::MemberSerialize)
   *
   * What it does:
   * Stores one unit-offset payload lane: weak-unit link, leader priority,
   * 2D offset, 3D direction, and speed-band weights.
   */
  void SUnitOffsetInfo::MemberSerialize(gpg::WriteArchive* const archive) const
  {
    if (!archive) {
      return;
    }

    const gpg::RRef ownerRef{};

    gpg::RType* const weakPtrType = CachedWeakPtrIUnitType();
    GPG_ASSERT(weakPtrType != nullptr);
    if (weakPtrType) {
      archive->Write(weakPtrType, &mUnit, ownerRef);
    }

    archive->WriteInt(mLeaderPriority);

    gpg::RType* const coordsType = CachedSCoordsVec2Type();
    GPG_ASSERT(coordsType != nullptr);
    if (coordsType) {
      archive->Write(coordsType, &mOffset, ownerRef);
    }

    gpg::RType* const vectorType = CachedVector3fType();
    GPG_ASSERT(vectorType != nullptr);
    if (vectorType) {
      archive->Write(vectorType, &mDirection, ownerRef);
    }

    archive->WriteFloat(mWeight);
    archive->WriteFloat(mSpeedBandLow);
    archive->WriteFloat(mSpeedBandMid);
    archive->WriteFloat(mSpeedBandHigh);
  }

  /**
   * Address: 0x0059BD60 (FUN_0059BD60, ??3CAiFormationInstance@Moho@@QAE@@Z)
   *
   * What it does:
   * Executes CAiFormationInstance teardown and conditionally frees this object
   * when `deleteFlags & 1` is set.
   */
  void CAiFormationInstance::operator_delete(const std::int32_t deleteFlags)
  {
    this->~CAiFormationInstance();
    if ((deleteFlags & 1) != 0) {
      ::operator delete(this);
    }
  }

  /**
   * Address: 0x0059E950 (FUN_0059E950, Moho::CAiFormationInstance::MemberDeserialize)
   *
   * What it does:
   * Reads serialized base-formation payload, then restores the `Sim*` lane as
   * an unowned tracked pointer.
   */
  void CAiFormationInstance::MemberDeserialize(gpg::ReadArchive* const archive)
  {
    if (!archive) {
      return;
    }

    const gpg::RRef owner{};
    gpg::RType* const baseType = CachedCFormationInstanceType();
    GPG_ASSERT(baseType != nullptr);
    if (baseType) {
      archive->Read(baseType, this, owner);
    }

    mSim = ReadPointerSim(archive, owner);
  }

  /**
   * Address: 0x0059DB60 (FUN_0059DB60)
   *
   * What it does:
   * Serializer bridge thunk that forwards to `CAiFormationInstance::MemberDeserialize`.
   */
  [[maybe_unused]] void CAiFormationInstanceMemberDeserializeBridgeA(
    gpg::ReadArchive* const archive,
    CAiFormationInstance* const formation
  )
  {
    if (formation != nullptr) {
      formation->MemberDeserialize(archive);
    }
  }

  /**
   * Address: 0x0059E9B0 (FUN_0059E9B0, Moho::CAiFormationInstance::MemberSerialize)
   *
   * What it does:
   * Writes serialized base-formation payload, then saves the `Sim*` lane as
   * an unowned tracked pointer.
   */
  void CAiFormationInstance::MemberSerialize(gpg::WriteArchive* const archive) const
  {
    if (!archive) {
      return;
    }

    const gpg::RRef owner{};
    gpg::RType* const baseType = CachedCFormationInstanceType();
    GPG_ASSERT(baseType != nullptr);
    if (baseType) {
      archive->Write(baseType, this, owner);
    }

    WritePointerSim(archive, mSim, owner);
  }

  /**
   * Address: 0x0059DB70 (FUN_0059DB70)
   *
   * What it does:
   * Serializer bridge thunk that forwards to `CAiFormationInstance::MemberSerialize`.
   */
  [[maybe_unused]] void CAiFormationInstanceMemberSerializeBridgeA(
    const CAiFormationInstance* const formation,
    gpg::WriteArchive* const archive
  )
  {
    if (formation != nullptr) {
      formation->MemberSerialize(archive);
    }
  }

  /**
   * Address: 0x0059E000 (FUN_0059E000)
   *
   * What it does:
   * Serializer bridge thunk that forwards to `CAiFormationInstance::MemberDeserialize`.
   */
  [[maybe_unused]] void CAiFormationInstanceMemberDeserializeBridgeB(
    gpg::ReadArchive* const archive,
    CAiFormationInstance* const formation
  )
  {
    if (formation != nullptr) {
      formation->MemberDeserialize(archive);
    }
  }

  /**
   * Address: 0x0059E010 (FUN_0059E010)
   *
   * What it does:
   * Serializer bridge thunk that forwards to `CAiFormationInstance::MemberSerialize`.
   */
  [[maybe_unused]] void CAiFormationInstanceMemberSerializeBridgeB(
    const CAiFormationInstance* const formation,
    gpg::WriteArchive* const archive
  )
  {
    if (formation != nullptr) {
      formation->MemberSerialize(archive);
    }
  }

  /**
   * Address: 0x00569A10 (FUN_00569A10)
   *
   * Moho::SCoordsVec2*
   *
   * What it does:
   * Copies the current formation center into `outCenter`.
   */
  SCoordsVec2* CAiFormationInstance::Func2(SCoordsVec2* const outCenter) const
  {
    outCenter->x = mFormationCenter.x;
    outCenter->z = mFormationCenter.z;
    return outCenter;
  }

  /**
   * Address: 0x00569A30 (FUN_00569A30)
   *
   * Moho::SCoordsVec2 const&
   *
   * What it does:
   * Applies a new center (if finite and changed), then invalidates slot and coord caches.
   */
  void CAiFormationInstance::Func3(const SCoordsVec2& center)
  {
    if (!BinaryFloatNotEqual(mFormationCenter.x, center.x) && !BinaryFloatNotEqual(mFormationCenter.z, center.z)) {
      return;
    }
    if (std::isnan(center.x) || std::isnan(center.z)) {
      return;
    }

    mFormationCenter = center;
    mOccupiedSlots.ResetStorageToInline();
    ResetCoordCacheMap(mCoordCachePrimary);
    ResetCoordCacheMap(mCoordCacheSecondary);
  }

  /**
   * Address: 0x0056A210 (FUN_0056A210)
   *
   * What it does:
   * Returns number of linked unit references currently tracked by this formation.
   */
  int CAiFormationInstance::UnitCount() const
  {
    return static_cast<int>(mUnits.end() - mUnits.begin());
  }

  /**
   * Address: 0x00569BD0 (FUN_00569BD0)
   *
   * Moho::Unit*
   *
   * What it does:
   * Classifies the unit into the air-motion bucket.
   */
  bool CAiFormationInstance::Func5(Unit* const unit) const
  {
    if (unit == nullptr) {
      return false;
    }

    const RUnitBlueprint* const blueprint = unit->GetBlueprint();
    return blueprint != nullptr && blueprint->Physics.MotionType == RULEUMT_Air;
  }

  /**
   * Address: 0x005669A0 (FUN_005669A0, Moho::CFormationInstance::Func6)
   *
   * What it does:
   * Resolves and returns the lane entry that currently owns `unit`.
   */
  SFormationLaneEntry* CAiFormationInstance::Func6(Unit* const unit)
  {
    if (!unit) {
      return nullptr;
    }

    const std::int32_t laneIndex = Func5(unit) ? 1 : 0;
    SFormationLaneEntry* lane = mLanes[laneIndex].begin();
    SFormationLaneEntry* const laneEnd = mLanes[laneIndex].end();
    const std::uint32_t unitEntityId = UnitEntityIdWord(unit);
    while (lane != laneEnd) {
      if (LaneMapFindNode(lane->unitMap, unitEntityId) != nullptr) {
        return lane;
      }
      ++lane;
    }

    const RUnitBlueprint* const blueprint = unit->GetBlueprint();
    gpg::Warnf(
      "unit %s not part of formation.",
      blueprint != nullptr ? blueprint->mBlueprintId.c_str() : "<null>"
    );
    return nullptr;
  }

  /**
   * Address: 0x00566A30 (FUN_00566A30, Moho::CAiFormationInstance::ComputeRunScriptOffset)
   *
   * What it does:
   * Scales one script-local formation offset by update scale, rotates by
   * current orientation when non-zero, then applies slot-span scaling.
   */
  SCoordsVec2* CAiFormationInstance::ComputeRunScriptOffset(
    const SCoordsVec2* const sourceOffset,
    SCoordsVec2* const dest
  ) const
  {
    if (!sourceOffset || !dest) {
      return dest;
    }

    Wm3::Vec3f scaled{};
    scaled.x = sourceOffset->x * mFormationUpdateScale;
    scaled.y = 0.0f;
    scaled.z = sourceOffset->z * mFormationUpdateScale;

    float rotatedX = scaled.x;
    float rotatedZ = scaled.z;
    if (mOrientation != kZeroQuaternion) {
      Wm3::Vec3f rotated{};
      (void)MultQuadVec(&rotated, &scaled, &mOrientation);
      rotatedX = rotated.x;
      rotatedZ = rotated.z;
    }

    const float slotSpanScale = static_cast<float>(mMaxUnitSlotCount + 2);
    dest->x = rotatedX * slotSpanScale;
    dest->z = rotatedZ * slotSpanScale;
    return dest;
  }

  /**
   * Address: 0x00569CB0 (FUN_00569CB0, Moho::CFormationInstance::GetFormationPosition)
   *
   * What it does:
   * Computes one formation target position for `unit` and updates the primary
   * coord cache.
   */
  SCoordsVec2* CAiFormationInstance::GetFormationPosition(
    SCoordsVec2* const dest,
    Unit* const unit,
    SFormationLaneEntry* laneEntry
  )
  {
    if (!dest || !unit) {
      return dest;
    }

    RefreshFormationPlanIfRequested(*this);

    const Wm3::Vec3f& unitPos = unit->GetPosition();
    SCoordsVec2 position{unitPos.x, unitPos.z};
    if (unit->IsDead()) {
      *dest = position;
      return dest;
    }

    const std::uint32_t unitEntityId = UnitEntityIdWord(unit);
    if (SFormationCoordCacheNode* const cached = CoordCacheFindNode(mCoordCachePrimary, unitEntityId)) {
      *dest = cached->position;
      return dest;
    }

    SFormationLaneEntry* lane = laneEntry;
    if (lane == nullptr) {
      if (!Func17(unit, false)) {
        *dest = position;
        return dest;
      }
      lane = Func6(unit);
    }

    if (lane != nullptr) {
      if (const SFormationLaneUnitNode* const node = LaneMapFindNode(lane->unitMap, unitEntityId)) {
        float localX = node->formationOffsetX;
        float localZ = node->formationOffsetZ;
        if (lane->applyDynamicOffset != 0u) {
          localX += lane->dynamicOffsetX;
          localZ += lane->dynamicOffsetZ;
        }

        SCoordsVec2 requested{};
        requested.x = mFormationCenter.x + localX;
        requested.z = mFormationCenter.z + localZ;

        SCoordsVec2 snapped{};
        FindSlotFor(&snapped, &requested, unit);
        position = snapped;
      }
    }

    (void)CoordCacheInsertOrAssign(mCoordCachePrimary, unitEntityId, position);
    *dest = position;
    return dest;
  }

  /**
   * Address: 0x00569EA0 (FUN_00569EA0, Moho::CFormationInstance::GetAdjustedFormationPosition)
   *
   * What it does:
   * Converts formation world coordinates to footprint-min cell coordinates.
   */
  SOCellPos* CAiFormationInstance::GetAdjustedFormationPosition(
    SOCellPos* const dest,
    Unit* const unit,
    SFormationLaneEntry* laneEntry
  )
  {
    if (!dest) {
      return dest;
    }

    dest->x = 0;
    dest->z = 0;
    if (!unit || unit->IsDead()) {
      return dest;
    }

    SCoordsVec2 position{};
    GetFormationPosition(&position, unit, laneEntry);

    const RUnitBlueprint* const blueprint = unit->GetBlueprint();
    if (!blueprint) {
      return dest;
    }

    const float halfSizeX = static_cast<float>(blueprint->mFootprint.mSizeX) * 0.5f;
    const float halfSizeZ = static_cast<float>(blueprint->mFootprint.mSizeZ) * 0.5f;
    const int adjustedX = static_cast<int>(std::lround(position.x - halfSizeX));
    const int adjustedZ = static_cast<int>(std::lround(position.z - halfSizeZ));
    dest->x = static_cast<std::int16_t>(adjustedX);
    dest->z = static_cast<std::int16_t>(adjustedZ);
    return dest;
  }

  /**
   * Address: 0x00569F70 (FUN_00569F70, Moho::CFormationInstance::Func9)
   *
   * What it does:
   * Computes one formation/steering hint coordinate and updates the secondary
   * coord cache.
   */
  SCoordsVec2* CAiFormationInstance::Func9(SCoordsVec2* const dest, Unit* const unit, SFormationLaneEntry* laneEntry)
  {
    if (!dest || !unit) {
      return dest;
    }

    RefreshFormationPlanIfRequested(*this);

    const Wm3::Vec3f& unitPos = unit->GetPosition();
    SCoordsVec2 position{unitPos.x, unitPos.z};
    if (unit->IsDead()) {
      *dest = position;
      return dest;
    }

    const std::uint32_t unitEntityId = UnitEntityIdWord(unit);
    if (SFormationCoordCacheNode* const cached = CoordCacheFindNode(mCoordCacheSecondary, unitEntityId)) {
      *dest = cached->position;
      return dest;
    }

    if (laneEntry != nullptr) {
      if (!unit->IsMobile()) {
        position.x = unitPos.x;
        position.z = unitPos.z;
      } else if (const SFormationLaneUnitNode* const node = LaneMapFindNode(laneEntry->unitMap, unitEntityId)) {
        float localX = node->formationOffsetX;
        float localZ = node->formationOffsetZ;
        if (laneEntry->applyDynamicOffset != 0u) {
          localX += laneEntry->dynamicOffsetX;
          localZ += laneEntry->dynamicOffsetZ;
        }
        position.x = mFormationCenter.x + localX;
        position.z = mFormationCenter.z + localZ;
      }
    }

    (void)CoordCacheInsertOrAssign(mCoordCacheSecondary, unitEntityId, position);
    *dest = position;
    return dest;
  }

  /**
   * Address: 0x0056A150 (FUN_0056A150, Moho::CFormationInstance::Func10)
   *
   * What it does:
   * Returns lane-provided formation vector when present, else unit position.
   */
  Wm3::Vec3f* CAiFormationInstance::Func10(Wm3::Vec3f* const out, Unit* const unit, SFormationLaneEntry* laneEntry)
  {
    if (!out) {
      return out;
    }

    if (!unit || unit->IsDead()) {
      out->x = 0.0f;
      out->y = 0.0f;
      out->z = 0.0f;
      return out;
    }

    if (laneEntry != nullptr) {
      const std::uint32_t unitEntityId = UnitEntityIdWord(unit);
      if (const SFormationLaneUnitNode* const node = LaneMapFindNode(laneEntry->unitMap, unitEntityId)) {
        if (node->formationVector.x != 0.0f || node->formationVector.y != 0.0f || node->formationVector.z != 0.0f) {
          *out = node->formationVector;
          return out;
        }
      }
    }

    *out = unit->GetPosition();
    return out;
  }

  /**
   * Address: 0x0056A220 (FUN_0056A220, Moho::CFormationInstance::AddUnit)
   *
   * What it does:
   * Adds one live unit weak-ref to this formation and marks plan rebuild.
   */
  void CAiFormationInstance::AddUnit(Unit* const unit)
  {
    if (!unit || unit->IsDead()) {
      return;
    }

    bool alreadyPresent = false;
    std::vector<Unit*> kept;
    kept.reserve(mUnits.size());
    for (SFormationLinkedUnitRef* it = mUnits.begin(); it != mUnits.end(); ++it) {
      Unit* const linkedUnit = DecodeLinkedRefUnit(*it);
      if (linkedUnit == nullptr || linkedUnit->IsDead() || linkedUnit->DestroyQueued()) {
        UnlinkLinkedRef(*it);
        continue;
      }

      if (linkedUnit == unit) {
        alreadyPresent = true;
      }

      kept.push_back(linkedUnit);
      UnlinkLinkedRef(*it);
    }

    ResetLinkedUnitRefsFromUnits(*this, kept);

    if (alreadyPresent) {
      const RUnitBlueprint* const blueprint = unit->GetBlueprint();
      gpg::Warnf(
        "Attempted to re-add existing unit (%d - %s) to formation (%d)",
        static_cast<int>(reinterpret_cast<std::uintptr_t>(unit)),
        blueprint != nullptr ? blueprint->mBlueprintId.c_str() : "<null>",
        static_cast<int>(reinterpret_cast<std::uintptr_t>(this))
      );
      return;
    }

    SFormationLinkedUnitRef linked{};
    mUnits.push_back(linked);
    RelinkLinkedRef(mUnits.back(), unit);
    mPlanUpdateRequested = 1u;
  }

  /**
   * Address: 0x0056A300 (FUN_0056A300, Moho::CFormationInstance::RemoveUnit)
   *
   * What it does:
   * Removes one unit from lane maps and linked unit-reference storage.
   */
  void CAiFormationInstance::RemoveUnit(Unit* const unit)
  {
    if (!unit) {
      return;
    }

    const std::uint32_t unitEntityId = UnitEntityIdWord(unit);
    const std::int32_t laneIndex = Func5(unit) ? 1 : 0;
    SFormationLaneEntry* lane = mLanes[laneIndex].begin();
    SFormationLaneEntry* const laneEnd = mLanes[laneIndex].end();
    while (lane != laneEnd) {
      if (LaneMapFindNode(lane->unitMap, unitEntityId) != nullptr) {
        EraseLaneMapNodeByEntityId(lane->unitMap, unitEntityId);
        if (DecodeUnitOwnerSlotWord(lane->linkedUnitBackLinkHeadWord) == unit) {
          UnlinkWeakWordNode(lane->linkedUnitBackLinkHeadWord, lane->linkedUnitBackLinkNextWord);
        }
      }
      ++lane;
    }

    std::vector<Unit*> kept;
    kept.reserve(mUnits.size());
    for (SFormationLinkedUnitRef* it = mUnits.begin(); it != mUnits.end(); ++it) {
      Unit* const linkedUnit = DecodeLinkedRefUnit(*it);
      if (linkedUnit != nullptr && linkedUnit != unit) {
        kept.push_back(linkedUnit);
      }
      UnlinkLinkedRef(*it);
    }

    ResetLinkedUnitRefsFromUnits(*this, kept);
  }

  /**
   * Address: 0x0056A440 (FUN_0056A440, Moho::CFormationInstance::Func17)
   *
   * What it does:
   * Returns true if `unit` exists in the lane map (or full linked set when
   * `checkAll` is true).
   */
  bool CAiFormationInstance::Func17(Unit* const unit, const bool checkAll) const
  {
    if (!unit) {
      return false;
    }

    const std::uint32_t unitEntityId = UnitEntityIdWord(unit);
    const std::int32_t laneIndex = Func5(unit) ? 1 : 0;
    const SFormationLaneEntry* lane = mLanes[laneIndex].begin();
    const SFormationLaneEntry* const laneEnd = mLanes[laneIndex].end();
    while (lane != laneEnd) {
      if (LaneMapFindNode(lane->unitMap, unitEntityId) != nullptr) {
        return true;
      }
      ++lane;
    }

    if (!checkAll) {
      return false;
    }

    const SFormationLinkedUnitRef* it = mUnits.begin();
    const SFormationLinkedUnitRef* const end = mUnits.end();
    while (it != end) {
      if (DecodeLinkedRefUnit(*it) == unit) {
        return true;
      }
      ++it;
    }

    return false;
  }

  /**
   * Address: 0x005691E0 (FUN_005691E0, Moho::CAiFormationInstance::RemoveDeadUnits)
   *
   * What it does:
   * Compacts linked formation unit refs by removing null/dead/destroy-queued
   * units and returns whether `checkForUnit` is still present after cleanup.
   */
  bool CAiFormationInstance::RemoveDeadUnits(Unit* const checkForUnit)
  {
    bool hasCheckForUnit = false;
    std::vector<Unit*> kept;
    kept.reserve(mUnits.size());

    for (SFormationLinkedUnitRef* it = mUnits.begin(); it != mUnits.end(); ++it) {
      Unit* const linkedUnit = DecodeLinkedRefUnit(*it);
      const bool removeEntry =
        linkedUnit == nullptr || linkedUnit->IsDead() || linkedUnit->DestroyQueued();
      if (!removeEntry) {
        if (checkForUnit != nullptr && linkedUnit == checkForUnit) {
          hasCheckForUnit = true;
        }
        kept.push_back(linkedUnit);
      }
      UnlinkLinkedRef(*it);
    }

    ResetLinkedUnitRefsFromUnits(*this, kept);

    return hasCheckForUnit;
  }

  /**
   * Address: 0x00569B60 (FUN_00569B60, Moho::CFormationInstance::Func19)
   *
   * What it does:
   * Returns formation forward vector for contained units, else zero.
   */
  Wm3::Vec3f* CAiFormationInstance::Func19(Wm3::Vec3f* const out, Unit* const unit) const
  {
    if (!out) {
      return out;
    }

    if (Func17(unit, false)) {
      *out = mForwardVector;
    } else {
      *out = kZeroForwardVector;
    }
    return out;
  }

  /**
   * Address: 0x00569C20 (FUN_00569C20, Moho::CFormationInstance::Func21)
   *
   * What it does:
   * Returns lane slot availability status for `unit`, or aggregate
   * all-lane availability when no valid unit target is provided.
   */
  bool CAiFormationInstance::Func21(Unit* const unit) const
  {
    if (unit != nullptr && !unit->IsDead() && Func17(unit, false)) {
      if (SFormationLaneEntry* const lane = const_cast<CAiFormationInstance*>(this)->Func6(unit); lane != nullptr) {
        return lane->slotAvailable != 0u;
      }
      return true;
    }

    for (std::int32_t laneIndex = 0; laneIndex < 2; ++laneIndex) {
      const SFormationLaneEntry* lane = mLanes[laneIndex].begin();
      const SFormationLaneEntry* const laneEnd = mLanes[laneIndex].end();
      while (lane != laneEnd) {
        if (lane->slotAvailable == 0u) {
          return false;
        }
        ++lane;
      }
    }
    return true;
  }

  /**
   * Address: 0x0059A790 (FUN_0059A790, Moho::CAiFormationInstance::Func11)
   *
   * What it does:
   * Returns one lane-node speed sample for `unit`.
   */
  float CAiFormationInstance::Func11(Unit* const unit, SFormationLaneEntry* const laneEntry)
  {
    if (!unit || laneEntry == nullptr) {
      return 0.0f;
    }

    const std::uint32_t unitEntityId = UnitEntityIdWord(unit);
    const SFormationLaneUnitNode* const node = LaneMapFindNode(laneEntry->unitMap, unitEntityId);
    return node ? node->speedBandMid : 0.0f;
  }

  /**
   * Address: 0x0059A7D0 (FUN_0059A7D0, Moho::CAiFormationInstance::Func12)
   *
   * What it does:
   * Computes one integer move-priority weight from lane speed data.
   */
  std::int32_t CAiFormationInstance::Func12(Unit* const unit, SFormationLaneEntry* laneEntry)
  {
    if (!unit) {
      return 1;
    }

    Unit* const runtimeUnit = unit->IsUnit();
    if (runtimeUnit == nullptr) {
      return 1;
    }

    if (runtimeUnit->GuardedUnitRef.ResolveObjectPtr<Unit>() != nullptr) {
      return 1;
    }

    if (laneEntry == nullptr) {
      laneEntry = Func6(unit);
      if (laneEntry == nullptr) {
        return 1;
      }
    }

    const std::uint32_t unitEntityId = UnitEntityIdWord(unit);
    const SFormationLaneUnitNode* const node = LaneMapFindNode(laneEntry->unitMap, unitEntityId);
    if (!node || node->speedBandHigh <= 0.0f) {
      return 1;
    }

    const std::int32_t scaled = static_cast<std::int32_t>(node->speedBandHigh) * 10;
    return scaled > 1 ? scaled : 1;
  }

  /**
   * Address: 0x0059A620 (FUN_0059A620, Moho::CAiFormationInstance::CalcFormationSpeed)
   *
   * What it does:
   * Computes one lane speed and per-unit speed scale for formation movement.
   */
  float CAiFormationInstance::CalcFormationSpeed(
    Unit* const unit,
    float* const speedScaleOut,
    SFormationLaneEntry* const laneEntry
  )
  {
    if (!unit || !CommandIsForm()) {
      return 0.0f;
    }

    Unit* const laneLeader = Func14(unit, laneEntry);
    if (laneLeader != nullptr) {
      if (!Func17(laneLeader, false) && !laneLeader->IsMobile()) {
        return 0.0f;
      }
    }

    Unit* const runtimeUnit = unit->IsUnit();
    if (!runtimeUnit || !runtimeUnit->AiNavigator || runtimeUnit->AiNavigator->IsIgnoringFormation() || laneEntry == nullptr) {
      return 0.0f;
    }

    *speedScaleOut = 0.85f;
    bool canScaleWithLaneDelta = runtimeUnit->AiNavigator->FollowingLeader() || laneLeader == unit;
    if (laneEntry->speedAnchor > 0.0f && canScaleWithLaneDelta) {
      const std::uint32_t unitEntityId = UnitEntityIdWord(unit);
      if (const SFormationLaneUnitNode* const node = LaneMapFindNode(laneEntry->unitMap, unitEntityId); node != nullptr) {
        const RUnitBlueprint* const blueprint = unit->GetBlueprint();
        const float laneFactor = (blueprint != nullptr && blueprint->Air.CanFly != 0u) ? 1.5f : 4.0f;
        float delta = (node->speedBandLow - laneEntry->speedAnchor) * laneFactor;
        if (delta > 20.0f) {
          delta = 20.0f;
        } else if (delta < -5.0f) {
          delta = -5.0f;
        }
        *speedScaleOut = (delta * 0.1f) + 1.0f;
      }
    }

    return laneEntry->preferredSpeed;
  }

  /**
   * Address: 0x0059A870 (FUN_0059A870, Moho::CAiFormationInstance::Func14)
   *
   * What it does:
   * Resolves lane leader unit for one member unit and lane context.
   */
  Unit* CAiFormationInstance::Func14(Unit* const unit, SFormationLaneEntry* const laneEntry)
  {
    if (!unit) {
      return nullptr;
    }

    if (mCommandType == EUnitCommandType::UNITCOMMAND_Guard) {
      if (Unit* const runtimeUnit = unit->IsUnit(); runtimeUnit != nullptr) {
        return runtimeUnit->GuardedUnitRef.ResolveObjectPtr<Unit>();
      }
      return nullptr;
    }

    if (laneEntry == nullptr || unit->IsDead()) {
      return nullptr;
    }

    const std::int32_t laneIndex = Func5(unit) ? 1 : 0;
    return ResolveUpdateLaneLeader(laneIndex, *this, *laneEntry);
  }

  /**
   * Address: 0x0059AE80 (FUN_0059AE80, Moho::CAiFormationInstance::Update)
   *
   * What it does:
   * Refreshes any pending lane plan work, then walks both formation lane sets
   * to resolve leaders, update per-unit lane metrics, and emit formation
   * change events when a lane stays actionable.
   */
  void CAiFormationInstance::Update()
  {
    if (mPlanUpdateRequested != 0u) {
      mPlanUpdateRequested = 0u;
      (void)RemoveDeadUnits(nullptr);
    }

    if (!CommandIsForm() || UnitCount() == 0) {
      return;
    }

    if (mCommandType != EUnitCommandType::UNITCOMMAND_Guard) {
      MergeOverlappingLaneBands(*this);
    }

    const int unitCount = UnitCount();
    const bool overCapacity = mMaxUnitSlotCount > 0 && unitCount > mMaxUnitSlotCount;

    for (std::int32_t laneIndex = 0; laneIndex < 2; ++laneIndex) {
      SFormationLaneEntry* lane = mLanes[laneIndex].begin();
      SFormationLaneEntry* const laneEnd = mLanes[laneIndex].end();
      while (lane != laneEnd) {
        lane->slotAvailable = 0u;
        lane->applyDynamicOffset = 0u;

        Unit* const leader = ResolveUpdateLaneLeader(laneIndex, *this, *lane);
        if (leader == nullptr || leader->IsDead()) {
          ++lane;
          continue;
        }

        float leaderSpeedScale = 0.0f;
        const float leaderSpeed = CalcFormationSpeed(leader, &leaderSpeedScale, lane);
        lane->preferredSpeed = leaderSpeed;
        lane->speedAnchor = leaderSpeedScale;

        SCoordsVec2 laneTarget{};
        if (mCommandType == EUnitCommandType::UNITCOMMAND_Guard) {
          laneTarget.x = mFormationCenter.x;
          laneTarget.z = mFormationCenter.z;
        } else {
          (void)Func9(&laneTarget, leader, lane);
        }

        SFormationLaneUnitMap& unitMap = lane->unitMap;
        SFormationLaneUnitNode* const head = unitMap.head;
        bool hasLiveUnit = false;
        if (head != nullptr) {
          SFormationLaneUnitNode* node = head->left;
          while (node != nullptr && node != head && node->isNil == 0u) {
            Unit* const unit = DecodeUnitOwnerSlotWord(node->linkedUnitOwnerWord);
            if (unit != nullptr && !unit->IsDead() && !unit->DestroyQueued()) {
              hasLiveUnit = true;

              SCoordsVec2 desiredPos{};
              if (mCommandType == EUnitCommandType::UNITCOMMAND_Guard) {
                (void)GetFormationPosition(&desiredPos, unit, lane);
              } else {
                (void)Func9(&desiredPos, unit, lane);

                if (Unit* const runtimeUnit = unit->IsUnit();
                    runtimeUnit != nullptr && runtimeUnit->AiNavigator != nullptr
                    && runtimeUnit->AiNavigator->IsIgnoringFormation()) {
                  SOCellPos adjustedCell{};
                  (void)GetAdjustedFormationPosition(&adjustedCell, unit, lane);
                  desiredPos.x = static_cast<float>(adjustedCell.x);
                  desiredPos.z = static_cast<float>(adjustedCell.z);
                }
              }

              const Wm3::Vec3f& currentPos = unit->GetPosition();
              const float dx = desiredPos.x - currentPos.x;
              const float dz = desiredPos.z - currentPos.z;
              const float targetDx = desiredPos.x - laneTarget.x;
              const float targetDz = desiredPos.z - laneTarget.z;
              float memberSpeedScale = 0.0f;

              node->formationOffsetX = targetDx;
              node->formationOffsetZ = targetDz;
              node->formationVector.x = dx;
              node->formationVector.y = 0.0f;
              node->formationVector.z = dz;
              node->formationWeight = std::sqrt((dx * dx) + (dz * dz));
              node->speedBandLow = node->formationWeight;
              node->speedBandMid = Func11(unit, lane);
              node->speedBandHigh = CalcFormationSpeed(unit, &memberSpeedScale, lane);
              node->leaderPriority = Func12(unit, lane);
            }

            node = NextLaneMapNodeInOrder(node, head);
          }
        }

        if (hasLiveUnit && !overCapacity && leaderSpeed > 0.0f) {
          lane->applyDynamicOffset = 1u;
          lane->slotAvailable = 1u;
          lane->dynamicOffsetX = laneTarget.x - mFormationCenter.x;
          lane->dynamicOffsetZ = laneTarget.z - mFormationCenter.z;
          lane->overlapAnchorX = std::fabs(lane->dynamicOffsetX);
          lane->overlapAnchorZ = std::fabs(lane->dynamicOffsetZ);
          DispatchFormationUpdateEvent(1, mUnitLinkListHead);
        }

        ++lane;
      }
    }
  }

  /**
   * Address: 0x00569BF0 (FUN_00569BF0)
   *
   * What it does:
   * Returns true when current command type is one of the formation commands.
   */
  bool CAiFormationInstance::CommandIsForm() const
  {
    switch (mCommandType) {
    case EUnitCommandType::UNITCOMMAND_FormMove:
    case EUnitCommandType::UNITCOMMAND_FormAggressiveMove:
    case EUnitCommandType::UNITCOMMAND_FormPatrol:
    case EUnitCommandType::UNITCOMMAND_FormAttack:
    case EUnitCommandType::UNITCOMMAND_Guard:
      return true;
    default:
      return false;
    }
  }

  /**
   * Address: 0x0056A4F0 (FUN_0056A4F0)
   *
   * float
   *
   * What it does:
   * Updates formation scale and marks the plan for rebuild when value changed.
   */
  void CAiFormationInstance::Func22(const float scale)
  {
    if (!BinaryFloatNotEqual(mFormationUpdateScale, scale)) {
      return;
    }

    mFormationUpdateScale = scale;
    mPlanUpdateRequested = 1;
  }

  /**
   * Address: 0x0056A520 (FUN_0056A520)
   *
   * Wm3::Quaternion<float> const&
   *
   * What it does:
   * Sets formation orientation, recomputes forward vector, and requests a plan rebuild.
   */
  void CAiFormationInstance::SetOrientation(const Wm3::Quatf& orientation)
  {
    if (QuaternionEqualsExact(mOrientation, orientation)) {
      return;
    }

    mOrientation = orientation;
    if (QuaternionEqualsExact(mOrientation, kZeroQuaternion) || mCommandType == EUnitCommandType::UNITCOMMAND_Move) {
      mForwardVector = kZeroForwardVector;
    } else {
      const float x = mOrientation.x;
      const float y = mOrientation.y;
      const float z = mOrientation.z;
      const float w = mOrientation.w;
      mForwardVector.x = ((x * z) + (y * w)) * 2.0f;
      mForwardVector.y = ((z * w) - (x * y)) * 2.0f;
      mForwardVector.z = 1.0f - (((y * y) + (z * z)) * 2.0f);
    }

    mPlanUpdateRequested = 1;
  }

  /**
   * Address: 0x0056A680 (FUN_0056A680)
   *
   * Wm3::Quaternion<float>*
   *
   * What it does:
   * Copies the current orientation into `outOrientation`.
   */
  Wm3::Quatf* CAiFormationInstance::GetOrientation(Wm3::Quatf* const outOrientation) const
  {
    *outOrientation = mOrientation;
    return outOrientation;
  }

  /**
   * Address: 0x00569A00 (FUN_00569A00)
   *
   * What it does:
   * Returns the active command type for this formation.
   */
  EUnitCommandType CAiFormationInstance::GetCommandType() const
  {
    return mCommandType;
  }

  /**
   * Address: 0x0059AA20 (FUN_0059AA20, Moho::CAiFormationInstance::FindSlotFor)
   *
   * What it does:
   * Finds one valid slot near `pos` (spiral search capped at 2000 probes),
   * records it in `mOccupiedSlots`, and falls back to current unit position
   * when no free slot can be found.
   */
  SCoordsVec2* CAiFormationInstance::FindSlotFor(SCoordsVec2* const dest, const SCoordsVec2* const pos, Unit* const unit)
  {
    if (dest == nullptr || pos == nullptr || unit == nullptr) {
      return dest;
    }

    Unit* const runtimeUnit = unit->IsUnit();
    const bool fallbackToInputPos = runtimeUnit == nullptr
      || runtimeUnit->IsDead()
      || runtimeUnit->CommandQueue == nullptr
      || mCommandType == EUnitCommandType::UNITCOMMAND_Guard
      || mFormationUpdateScale < 1.0f
      || Func5(runtimeUnit);
    if (fallbackToInputPos) {
      dest->x = pos->x;
      dest->z = pos->z;
      return dest;
    }

    const RUnitBlueprint* const blueprint = runtimeUnit->GetBlueprint();
    if (blueprint == nullptr || mSim == nullptr || mSim->mOGrid == nullptr || mSim->mMapData == nullptr) {
      dest->x = pos->x;
      dest->z = pos->z;
      return dest;
    }

    const SFootprint footprint = blueprint->mFootprint;
    const std::int32_t footprintSize = std::max<int>(footprint.mSizeX, footprint.mSizeZ);
    const std::int32_t laneToken = Func5(runtimeUnit) ? 1 : 0;
    const bool useWholeMap = (runtimeUnit->ArmyRef != nullptr) ? runtimeUnit->ArmyRef->UseWholeMap() : false;

    auto reserveSlot = [this, footprintSize, laneToken](const SCoordsVec2& slotPos) {
      SFormationOccupiedSlot slot{};
      slot.position = slotPos;
      slot.footprintSize = footprintSize;
      slot.laneToken = laneToken;
      mOccupiedSlots.push_back(slot);
    };

    if (CanPlaceFormationSlot(*this, *pos, footprint, footprintSize, useWholeMap, laneToken)) {
      reserveSlot(*pos);
      dest->x = pos->x;
      dest->z = pos->z;
      return dest;
    }

    std::int32_t attempts = 0;
    for (std::int32_t radius = 1; attempts < 2000; ++radius) {
      for (std::int32_t dx = -radius; dx <= radius && attempts < 2000; ++dx) {
        const std::int32_t step = (dx == -radius || dx == radius) ? 1 : (radius * 2);
        for (std::int32_t dz = -radius; dz <= radius && attempts < 2000; dz += step) {
          ++attempts;

          SCoordsVec2 candidate{};
          candidate.x = pos->x + static_cast<float>(dx);
          candidate.z = pos->z + static_cast<float>(dz);
          if (!CanPlaceFormationSlot(*this, candidate, footprint, footprintSize, useWholeMap, laneToken)) {
            continue;
          }

          reserveSlot(candidate);
          dest->x = candidate.x;
          dest->z = candidate.z;
          return dest;
        }
      }
    }

    if (CUnitCommand* const nextCommand = runtimeUnit->CommandQueue->GetNextCommand();
        nextCommand != nullptr && IsBusyFormationQueueCommand(nextCommand->mVarDat.mCmdType)) {
      reserveSlot(*pos);
      dest->x = pos->x;
      dest->z = pos->z;
      return dest;
    }

    const Wm3::Vec3f& unitPos = runtimeUnit->GetPosition();
    dest->x = unitPos.x;
    dest->z = unitPos.z;
    return dest;
  }

  /**
   * Address: 0x0059A570 (FUN_0059A570)
   *
   * Moho::SCoordsVec2 const&, int, int
   *
   * What it does:
   * Returns true when no occupied slot for `laneToken` overlaps `position` by `footprintSize`.
   */
  bool CAiFormationInstance::Func27(
    const SCoordsVec2& position,
    const std::int32_t footprintSize,
    const std::int32_t laneToken
  ) const
  {
    const SFormationOccupiedSlot* slot = mOccupiedSlots.begin();
    const SFormationOccupiedSlot* const slotEnd = mOccupiedSlots.end();
    while (slot != slotEnd) {
      if (slot->laneToken == laneToken) {
        const std::int32_t maxFootprint =
          slot->footprintSize < footprintSize ? footprintSize : slot->footprintSize;
        const float dx = std::fabs(position.x - slot->position.x);
        if (dx < static_cast<float>(maxFootprint)) {
          const float dz = std::fabs(position.z - slot->position.z);
          if (dz < static_cast<float>(maxFootprint)) {
            return false;
          }
        }
      }
      ++slot;
    }

    return true;
  }
} // namespace moho
