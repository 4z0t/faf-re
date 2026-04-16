#include "CInfluenceMap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <typeinfo>

#include "gpg/core/algorithms/MD5.h"
#include "gpg/core/containers/String.h"
#include "lua/LuaObject.h"
#include "moho/ai/IAiReconDB.h"
#include "moho/console/CConAlias.h"
#include "moho/entity/Entity.h"
#include "moho/entity/EntityCategoryLookupResolver.h"
#include "moho/entity/EntityDb.h"
#include "moho/resource/blueprints/RUnitBlueprint.h"
#include "moho/sim/CArmyImpl.h"
#include "moho/sim/CSimConVarBase.h"
#include "moho/sim/RRuleGameRules.h"
#include "moho/sim/ReconBlip.h"
#include "moho/sim/STIMap.h"
#include "moho/sim/Sim.h"

namespace gpg
{
  class RMapType_uint_int final : public gpg::RType
  {
  public:
    /**
     * Address: 0x00718C70 (FUN_00718C70, gpg::RMapType_uint_int::GetName)
     */
    [[nodiscard]] const char* GetName() const override;

    /**
     * Address: 0x00718D50 (FUN_00718D50, gpg::RMapType_uint_int::GetLexical)
     *
     * What it does:
     * Formats inherited map lexical text with current element count.
     */
    [[nodiscard]] msvc8::string GetLexical(const gpg::RRef& ref) const override;

    /**
     * Address: 0x00718D30 (FUN_00718D30, gpg::RMapType_uint_int::Init)
     *
     * What it does:
     * Initializes map reflection metadata and binds typed archive callbacks.
     */
    void Init() override;
  };

  class RMapType_uint_InfluenceMapEntry final : public gpg::RType
  {
  public:
    /**
     * Address: 0x00718FE0 (FUN_00718FE0, gpg::RMapType_uint_InfluenceMapEntry::GetName)
     */
    [[nodiscard]] const char* GetName() const override;

    /**
     * Address: 0x007190C0 (FUN_007190C0, gpg::RMapType_uint_InfluenceMapEntry::GetLexical)
     *
     * What it does:
     * Formats inherited map lexical text with current element count.
     */
    [[nodiscard]] msvc8::string GetLexical(const gpg::RRef& ref) const override;

    /**
     * Address: 0x007190A0 (FUN_007190A0, gpg::RMapType_uint_InfluenceMapEntry::Init)
     *
     * What it does:
     * Initializes map reflection metadata and binds typed archive callbacks.
     */
    void Init() override;
  };

  class RVectorType_InfluenceGrid final : public gpg::RType
  {
  public:
    /**
     * Address: 0x00718DE0 (FUN_00718DE0, gpg::RVectorType_InfluenceGrid::GetName)
     */
    [[nodiscard]] const char* GetName() const override;

    /**
     * Address: 0x00718EA0 (FUN_00718EA0, gpg::RVectorType_InfluenceGrid::GetLexical)
     *
     * What it does:
     * Formats inherited vector lexical text with current `InfluenceGrid` count.
     */
    [[nodiscard]] msvc8::string GetLexical(const gpg::RRef& ref) const override;

    void Init() override;
  };

  class RVectorType_SThreat final : public gpg::RType
  {
  public:
    /**
     * Address: 0x00719150 (FUN_00719150, gpg::RVectorType_SThreat::GetName)
     */
    [[nodiscard]] const char* GetName() const override;

    /**
     * Address: 0x00719210 (FUN_00719210, gpg::RVectorType_SThreat::GetLexical)
     *
     * What it does:
     * Formats inherited vector lexical text with current `SThreat` count.
     */
    [[nodiscard]] msvc8::string GetLexical(const gpg::RRef& ref) const override;

    void Init() override;
  };
} // namespace gpg

namespace
{
  using UIntIntMap = std::map<std::uint32_t, int>;
  using UIntInfluenceMapEntryMap = std::map<std::uint32_t, moho::InfluenceMapEntry>;
  using InfluenceGridVector = msvc8::vector<moho::InfluenceGrid>;
  using SThreatVector = msvc8::vector<moho::SThreat>;
  using InfluenceEntrySet = msvc8::set<moho::InfluenceMapEntry, moho::InfluenceMapEntryLess>;
  using InfluenceMapCellSet = msvc8::set<moho::InfluenceMapCellIndex, moho::InfluenceMapCellIndexLess>;
  using InfluenceEntryIterator = InfluenceEntrySet::iterator;
  using InfluenceMapCellIterator = InfluenceMapCellSet::iterator;

  void DestroyInfluenceEntryRange(
    InfluenceEntrySet& entries,
    InfluenceEntryIterator first,
    InfluenceEntryIterator last
  ) noexcept;

  /**
   * Address: 0x0071B860 (FUN_0071B860)
   *
   * What it does:
   * Adjusts one `vector<InfluenceGrid>` length to `requestedCount` and uses
   * one caller-provided fill lane for growth.
   */
  [[maybe_unused]] std::size_t ResizeInfluenceGridVectorWithFill(
    InfluenceGridVector& storage,
    const std::size_t requestedCount,
    const moho::InfluenceGrid& fillValue
  )
  {
    (void)fillValue;

    const std::size_t currentCount = storage.size();
    if (currentCount < requestedCount) {
      storage.resize(requestedCount);
      return requestedCount;
    }

    if (requestedCount < currentCount) {
      storage.resize(requestedCount);
    }

    return requestedCount;
  }

  /**
   * Address: 0x00719790 (FUN_00719790)
   *
   * What it does:
   * Destroys one `InfluenceGrid::entries` tree payload before the set object's
   * own storage release runs at scope teardown.
   */
  void ClearInfluenceGridEntryTree(InfluenceEntrySet& entries) noexcept
  {
    DestroyInfluenceEntryRange(entries, entries.begin(), entries.end());
  }

  /**
   * Address: 0x00719F20 (FUN_00719F20)
   *
   * What it does:
   * Clears one `vector<InfluenceGrid>` payload before the vector member
   * releases its backing storage during destruction.
   */
  void ClearInfluenceGridVectorStorage(InfluenceGridVector& storage) noexcept
  {
    storage.clear();
  }

  struct LegacyMapRuntimeView
  {
    void* allocProxy;
    void* head;
    std::uint32_t size;
  };

  template <class TValue>
  [[nodiscard]] std::size_t CountLegacyVectorElements(const void* const object) noexcept
  {
    if (object == nullptr) {
      return 0u;
    }

    const auto* const vector = static_cast<const msvc8::vector<TValue>*>(object);
    return vector->size();
  }

  [[nodiscard]] std::size_t CountLegacyMapElements(const void* const object) noexcept
  {
    if (object == nullptr) {
      return 0u;
    }

    const auto* const mapView = static_cast<const LegacyMapRuntimeView*>(object);
    return mapView->size;
  }

  template <class TObject>
  [[nodiscard]] TObject* PointerFromArchiveInt(const int objectPtr)
  {
    return reinterpret_cast<TObject*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(objectPtr)));
  }

  template <class TObject>
  [[nodiscard]] const TObject* ConstPointerFromArchiveInt(const int objectPtr)
  {
    return reinterpret_cast<const TObject*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(objectPtr)));
  }

  [[nodiscard]] float DecayThreatLane(const float value, const float decay) noexcept
  {
    if (value <= 0.0f) {
      return value;
    }

    float candidate = value + decay;
    if (candidate > 0.0f) {
      candidate = 0.0f;
    }

    const float reduced = value - decay;
    if (reduced > candidate) {
      candidate = reduced;
    }

    return candidate;
  }

  [[nodiscard]] moho::Entity* FindEntityById(moho::CEntityDb* const entityDb, const std::int32_t id) noexcept
  {
    if (!entityDb) {
      return nullptr;
    }

    for (auto it = entityDb->Entities().begin(); it != entityDb->Entities().end(); ++it) {
      moho::Entity* const entity = *it;
      if (entity && entity->id_ == id) {
        return entity;
      }
    }

    return nullptr;
  }

  [[nodiscard]] bool IsAlliedOrSameArmy(const moho::CArmyImpl* const owner, const moho::CArmyImpl* const source) noexcept
  {
    if (!owner || !source) {
      return false;
    }

    if (owner == source) {
      return true;
    }

    if (source->ArmyId < 0) {
      return false;
    }

    return owner->Allies.Contains(static_cast<std::uint32_t>(source->ArmyId));
  }

  [[nodiscard]] moho::CConAlias& ConAlias_imap_debug()
  {
    static moho::CConAlias sAlias;
    return sAlias;
  }

  [[nodiscard]] moho::CConAlias& ConAlias_imap_debug_grid()
  {
    static moho::CConAlias sAlias;
    return sAlias;
  }

  [[nodiscard]] moho::CConAlias& ConAlias_imap_debug_path_graph()
  {
    static moho::CConAlias sAlias;
    return sAlias;
  }

  [[nodiscard]] moho::CConAlias& ConAlias_imap_debug_grid_type()
  {
    static moho::CConAlias sAlias;
    return sAlias;
  }

  [[nodiscard]] moho::CConAlias& ConAlias_imap_debug_grid_army()
  {
    static moho::CConAlias sAlias;
    return sAlias;
  }

  [[nodiscard]] moho::TSimConVar<bool>& SimConVar_imap_debug()
  {
    static moho::TSimConVar<bool> sVar(false, "imap_debug", false);
    return sVar;
  }

  [[nodiscard]] moho::TSimConVar<bool>& SimConVar_imap_debug_grid()
  {
    static moho::TSimConVar<bool> sVar(false, "imap_debug_grid", false);
    return sVar;
  }

  [[nodiscard]] moho::TSimConVar<bool>& SimConVar_imap_debug_path_graph()
  {
    static moho::TSimConVar<bool> sVar(false, "imap_debug_path_graph", false);
    return sVar;
  }

  [[nodiscard]] moho::TSimConVar<int>& SimConVar_imap_debug_grid_type()
  {
    static moho::TSimConVar<int> sVar(false, "imap_debug_grid_type", 0);
    return sVar;
  }

  [[nodiscard]] moho::TSimConVar<int>& SimConVar_imap_debug_grid_army()
  {
    static moho::TSimConVar<int> sVar(false, "imap_debug_grid_army", -1);
    return sVar;
  }

  msvc8::string gInfluenceGridVectorTypeName{};
  std::uint32_t gInfluenceGridVectorTypeNameInitGuard = 0u;
  msvc8::string gMapUintIntTypeName{};
  std::uint32_t gMapUintIntTypeNameInitGuard = 0u;
  msvc8::string gMapUintInfluenceMapEntryTypeName{};
  std::uint32_t gMapUintInfluenceMapEntryTypeNameInitGuard = 0u;
  msvc8::string gSThreatVectorTypeName{};
  std::uint32_t gSThreatVectorTypeNameInitGuard = 0u;

  [[nodiscard]] gpg::RType* CachedInfluenceGridType()
  {
    gpg::RType* type = moho::InfluenceGrid::sType;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::InfluenceGrid));
      moho::InfluenceGrid::sType = type;
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedSThreatType()
  {
    gpg::RType* type = moho::SThreat::sType;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::SThreat));
      moho::SThreat::sType = type;
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedUIntType()
  {
    static gpg::RType* type = nullptr;
    if (!type) {
      type = gpg::LookupRType(typeid(unsigned int));
      if (!type) {
        type = gpg::REF_FindTypeNamed("unsigned int");
      }
      if (!type) {
        type = gpg::REF_FindTypeNamed("uint");
      }
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedIntType()
  {
    static gpg::RType* type = nullptr;
    if (!type) {
      type = gpg::LookupRType(typeid(int));
      if (!type) {
        type = gpg::REF_FindTypeNamed("int");
      }
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedInfluenceMapEntryType()
  {
    gpg::RType* type = moho::InfluenceMapEntry::sType;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::InfluenceMapEntry));
      moho::InfluenceMapEntry::sType = type;
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedInfluenceMapEntryMapType()
  {
    static gpg::RType* type = nullptr;
    if (!type) {
      type = gpg::LookupRType(typeid(UIntInfluenceMapEntryMap));
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedSThreatVectorType()
  {
    static gpg::RType* type = nullptr;
    if (!type) {
      type = gpg::LookupRType(typeid(msvc8::vector<moho::SThreat>));
    }
    return type;
  }

  void cleanup_InfluenceGridVectorTypeName()
  {
    gInfluenceGridVectorTypeName.clear();
    gInfluenceGridVectorTypeNameInitGuard = 0u;
  }

  void cleanup_SThreatVectorTypeName()
  {
    gSThreatVectorTypeName.clear();
    gSThreatVectorTypeNameInitGuard = 0u;
  }

  void cleanup_MapUintIntTypeName()
  {
    gMapUintIntTypeName.clear();
    gMapUintIntTypeNameInitGuard = 0u;
  }

  void cleanup_MapUintInfluenceMapEntryTypeName()
  {
    gMapUintInfluenceMapEntryTypeName.clear();
    gMapUintInfluenceMapEntryTypeNameInitGuard = 0u;
  }

  /**
   * Address: 0x0071A220 (FUN_0071A220)
   *
   * What it does:
   * Loads one `map<unsigned int,int>` payload from archive lanes.
   */
  void LoadUIntIntMap(gpg::ReadArchive* const archive, const int objectPtr, const int, gpg::RRef*)
  {
    auto* const mapObject = PointerFromArchiveInt<UIntIntMap>(objectPtr);
    GPG_ASSERT(archive != nullptr);
    GPG_ASSERT(mapObject != nullptr);
    if (!archive || !mapObject) {
      return;
    }

    unsigned int count = 0;
    archive->ReadUInt(&count);

    mapObject->clear();
    for (unsigned int i = 0; i < count; ++i) {
      unsigned int key = 0;
      int value = 0;
      archive->ReadUInt(&key);
      archive->ReadInt(&value);
      (*mapObject)[key] = value;
    }
  }

  /**
   * Address: 0x0071A2D0 (FUN_0071A2D0)
   *
   * What it does:
   * Saves one `map<unsigned int,int>` payload into archive lanes.
   */
  void SaveUIntIntMap(gpg::WriteArchive* const archive, const int objectPtr, const int, gpg::RRef*)
  {
    const auto* const mapObject = ConstPointerFromArchiveInt<UIntIntMap>(objectPtr);
    GPG_ASSERT(archive != nullptr);
    GPG_ASSERT(mapObject != nullptr);
    if (!archive || !mapObject) {
      return;
    }

    archive->WriteUInt(static_cast<unsigned int>(mapObject->size()));
    for (auto it = mapObject->begin(); it != mapObject->end(); ++it) {
      archive->WriteUInt(it->first);
      archive->WriteInt(it->second);
    }
  }

  /**
   * Address: 0x0071A530 (FUN_0071A530)
   *
   * What it does:
   * Loads one `map<unsigned int,InfluenceMapEntry>` payload from archive lanes.
   */
  void LoadUIntInfluenceMapEntryMap(gpg::ReadArchive* const archive, const int objectPtr, const int, gpg::RRef* const ownerRef)
  {
    auto* const mapObject = PointerFromArchiveInt<UIntInfluenceMapEntryMap>(objectPtr);
    GPG_ASSERT(archive != nullptr);
    GPG_ASSERT(mapObject != nullptr);
    if (!archive || !mapObject) {
      return;
    }

    unsigned int count = 0;
    archive->ReadUInt(&count);

    mapObject->clear();
    gpg::RType* const valueType = CachedInfluenceMapEntryType();
    GPG_ASSERT(valueType != nullptr);
    if (!valueType) {
      return;
    }

    const gpg::RRef owner = ownerRef ? *ownerRef : gpg::RRef{};
    for (unsigned int i = 0; i < count; ++i) {
      unsigned int key = 0;
      moho::InfluenceMapEntry value{};
      archive->ReadUInt(&key);
      archive->Read(valueType, &value, owner);
      (*mapObject)[key] = value;
    }
  }

  /**
   * Address: 0x0071A670 (FUN_0071A670)
   *
   * What it does:
   * Saves one `map<unsigned int,InfluenceMapEntry>` payload into archive lanes.
   */
  void SaveUIntInfluenceMapEntryMap(
    gpg::WriteArchive* const archive,
    const int objectPtr,
    const int,
    gpg::RRef* const ownerRef
  )
  {
    const auto* const mapObject = ConstPointerFromArchiveInt<UIntInfluenceMapEntryMap>(objectPtr);
    GPG_ASSERT(archive != nullptr);
    GPG_ASSERT(mapObject != nullptr);
    if (!archive || !mapObject) {
      return;
    }

    archive->WriteUInt(static_cast<unsigned int>(mapObject->size()));

    gpg::RType* const valueType = CachedInfluenceMapEntryType();
    GPG_ASSERT(valueType != nullptr);
    if (!valueType) {
      return;
    }

    const gpg::RRef owner = ownerRef ? *ownerRef : gpg::RRef{};
    for (auto it = mapObject->begin(); it != mapObject->end(); ++it) {
      archive->WriteUInt(it->first);
      archive->Write(valueType, &(it->second), owner);
    }
  }

  /**
   * Address: 0x0071CF30 (FUN_0071CF30, deserialize_InfluenceGrid_record)
   *
   * What it does:
   * Deserializes one `InfluenceGrid` payload in archive field order:
   * `entries`, `threats`, aggregate threat, and decay lanes.
   */
  [[maybe_unused]] void DeserializeInfluenceGridRecord(
    gpg::ReadArchive* const archive,
    moho::InfluenceGrid* const grid,
    gpg::RRef* const ownerRef
  )
  {
    if (archive == nullptr || grid == nullptr) {
      return;
    }

    const gpg::RRef owner = ownerRef ? *ownerRef : gpg::RRef{};

    gpg::RType* const entryMapType = CachedInfluenceMapEntryMapType();
    GPG_ASSERT(entryMapType != nullptr);
    if (!entryMapType) {
      return;
    }
    archive->Read(entryMapType, &grid->entries, owner);

    gpg::RType* const threatVectorType = CachedSThreatVectorType();
    GPG_ASSERT(threatVectorType != nullptr);
    if (!threatVectorType) {
      return;
    }
    archive->Read(threatVectorType, &grid->threats, owner);

    gpg::RType* const threatType = CachedSThreatType();
    GPG_ASSERT(threatType != nullptr);
    if (!threatType) {
      return;
    }
    archive->Read(threatType, &grid->threat, owner);
    archive->Read(threatType, &grid->decay, owner);
  }

  /**
   * Address: 0x0071D010 (FUN_0071D010, serialize_InfluenceGrid_record)
   *
   * What it does:
   * Serializes one `InfluenceGrid` payload in archive field order:
   * `entries`, `threats`, aggregate threat, and decay lanes.
   */
  [[maybe_unused]] void SerializeInfluenceGridRecord(
    gpg::WriteArchive* const archive,
    const moho::InfluenceGrid* const grid,
    gpg::RRef* const ownerRef
  )
  {
    if (archive == nullptr || grid == nullptr) {
      return;
    }

    const gpg::RRef owner = ownerRef ? *ownerRef : gpg::RRef{};

    gpg::RType* const entryMapType = CachedInfluenceMapEntryMapType();
    GPG_ASSERT(entryMapType != nullptr);
    if (!entryMapType) {
      return;
    }
    archive->Write(entryMapType, grid, owner);

    gpg::RType* const threatVectorType = CachedSThreatVectorType();
    GPG_ASSERT(threatVectorType != nullptr);
    if (!threatVectorType) {
      return;
    }
    archive->Write(threatVectorType, &grid->threats, owner);

    gpg::RType* const threatType = CachedSThreatType();
    GPG_ASSERT(threatType != nullptr);
    if (!threatType) {
      return;
    }
    archive->Write(threatType, &grid->threat, owner);
    archive->Write(threatType, &grid->decay, owner);
  }

  /**
   * Address: 0x0071CB20 (FUN_0071CB20, deserialize_InfluenceMapEntry_record)
   *
   * What it does:
   * Deserializes one `InfluenceMapEntry` payload in archive field order:
   * `EntId`, `SimArmy*`, `Vector3f` position, `RUnitBlueprint*`, `ELayer`,
   * detail flag, threat magnitude/decay, and decay tick count.
   */
  void DeserializeInfluenceMapEntryRecord(
    gpg::ReadArchive* const archive,
    const int objectPtr,
    const int,
    gpg::RRef* const ownerRef
  )
  {
    auto* const entry = PointerFromArchiveInt<moho::InfluenceMapEntry>(objectPtr);
    if (archive == nullptr || entry == nullptr) {
      return;
    }

    const gpg::RRef owner = ownerRef ? *ownerRef : gpg::RRef{};

    static gpg::RType* entIdType = nullptr;
    if (entIdType == nullptr) {
      entIdType = gpg::LookupRType(typeid(moho::EntId));
    }
    archive->Read(entIdType, &entry->entityId, owner);

    moho::SimArmy* sourceArmy = nullptr;
    archive->ReadPointer_SimArmy(&sourceArmy, &owner);
    entry->sourceArmy = reinterpret_cast<moho::CArmyImpl*>(sourceArmy);

    static gpg::RType* vector3fType = nullptr;
    if (vector3fType == nullptr) {
      vector3fType = gpg::LookupRType(typeid(Wm3::Vector3f));
    }
    archive->Read(vector3fType, &entry->lastPosition, owner);

    moho::RUnitBlueprint* sourceBlueprint = nullptr;
    archive->ReadPointer_RUnitBlueprint(&sourceBlueprint, &owner);
    entry->sourceBlueprint = sourceBlueprint;

    static gpg::RType* layerType = nullptr;
    if (layerType == nullptr) {
      layerType = gpg::LookupRType(typeid(moho::ELayer));
    }
    archive->Read(layerType, &entry->sourceLayer, owner);

    bool isDetailed = false;
    archive->ReadBool(&isDetailed);
    entry->isDetailed = isDetailed ? 1u : 0u;

    archive->ReadFloat(&entry->threatStrength);
    archive->ReadFloat(&entry->threatDecay);
    archive->ReadInt(&entry->decayTicks);
  }

  /**
   * Address: 0x0071CC30 (FUN_0071CC30, serialize_InfluenceMapEntry_record)
   *
   * What it does:
   * Serializes one `InfluenceMapEntry` payload in archive field order using
   * unowned pointer lanes for `SimArmy*` and `RUnitBlueprint*`.
   */
  void SerializeInfluenceMapEntryRecord(
    gpg::WriteArchive* const archive,
    const int objectPtr,
    const int,
    gpg::RRef* const ownerRef
  )
  {
    const auto* const entry = ConstPointerFromArchiveInt<moho::InfluenceMapEntry>(objectPtr);
    if (archive == nullptr || entry == nullptr) {
      return;
    }

    const gpg::RRef owner = ownerRef ? *ownerRef : gpg::RRef{};

    static gpg::RType* entIdType = nullptr;
    if (entIdType == nullptr) {
      entIdType = gpg::LookupRType(typeid(moho::EntId));
    }
    archive->Write(entIdType, &entry->entityId, owner);

    gpg::RRef armyRef{};
    (void)gpg::RRef_SimArmy(&armyRef, reinterpret_cast<moho::SimArmy*>(entry->sourceArmy));
    gpg::WriteRawPointer(archive, armyRef, gpg::TrackedPointerState::Unowned, owner);

    static gpg::RType* vector3fType = nullptr;
    if (vector3fType == nullptr) {
      vector3fType = gpg::LookupRType(typeid(Wm3::Vector3f));
    }
    archive->Write(vector3fType, &entry->lastPosition, owner);

    gpg::RRef blueprintRef{};
    (void)gpg::RRef_RUnitBlueprint(&blueprintRef, const_cast<moho::RUnitBlueprint*>(entry->sourceBlueprint));
    gpg::WriteRawPointer(archive, blueprintRef, gpg::TrackedPointerState::Unowned, owner);

    static gpg::RType* layerType = nullptr;
    if (layerType == nullptr) {
      layerType = gpg::LookupRType(typeid(moho::ELayer));
    }
    archive->Write(layerType, &entry->sourceLayer, owner);

    archive->WriteBool(entry->isDetailed != 0u);
    archive->WriteFloat(entry->threatStrength);
    archive->WriteFloat(entry->threatDecay);
    archive->WriteInt(entry->decayTicks);
  }

  /**
   * Address: 0x0071BE10 (FUN_0071BE10, sub_71BE10)
   *
   * What it does:
   * Advances one `InfluenceGrid::entries` iterator to its in-order successor.
   */
  void AdvanceInfluenceEntryIterator(InfluenceEntryIterator& it, const InfluenceEntryIterator end) noexcept
  {
    if (it != end) {
      ++it;
    }
  }

  /**
   * Address: 0x00717EF0 (FUN_00717EF0, sub_717EF0)
   *
   * What it does:
   * Erases one `InfluenceGrid::entries` node and returns the successor iterator.
   */
  [[nodiscard]] InfluenceEntryIterator EraseInfluenceEntryAndAdvance(
    moho::InfluenceGrid& grid,
    const InfluenceEntryIterator current
  )
  {
    if (current == grid.entries.end()) {
      throw std::out_of_range("invalid map/set<T> iterator");
    }

    InfluenceEntryIterator next = current;
    AdvanceInfluenceEntryIterator(next, grid.entries.end());
    grid.entries.erase(current);
    return next;
  }

  /**
   * Address: 0x0071C280 (FUN_0071C280, sub_71C280)
   *
   * What it does:
   * Destroys one ordered range of `InfluenceGrid::entries` nodes.
   */
  void DestroyInfluenceEntryRange(
    InfluenceEntrySet& entries,
    InfluenceEntryIterator first,
    const InfluenceEntryIterator last
  ) noexcept
  {
    while (first != last) {
      const InfluenceEntryIterator eraseIt = first;
      AdvanceInfluenceEntryIterator(first, last);
      entries.erase(eraseIt);
    }
  }

  /**
   * Address: 0x0071C590 (FUN_0071C590, sub_71C590)
   *
   * What it does:
   * Advances one `CInfluenceMap::mBlipCells` iterator to its in-order successor.
   */
  void AdvanceBlipCellIterator(InfluenceMapCellIterator& it, const InfluenceMapCellIterator end) noexcept
  {
    if (it != end) {
      ++it;
    }
  }

  /**
   * Address: 0x0071B420 (FUN_0071B420, sub_71B420)
   *
   * What it does:
   * Erases one `mBlipCells` iterator range and returns the first non-erased
   * successor.
   */
  [[nodiscard]] InfluenceMapCellIterator EraseBlipCellRange(
    InfluenceMapCellSet& blipCells,
    InfluenceMapCellIterator first,
    const InfluenceMapCellIterator last
  ) noexcept
  {
    if (first == blipCells.begin() && last == blipCells.end()) {
      blipCells.clear();
      return blipCells.begin();
    }

    while (first != last) {
      const InfluenceMapCellIterator eraseIt = first;
      AdvanceBlipCellIterator(first, last);
      blipCells.erase(eraseIt);
    }

    return first;
  }

  /**
   * Address: 0x0071D7B0 (FUN_0071D7B0, sub_71D7B0)
   *
   * What it does:
   * Allocates legacy red-black-tree node storage for blip-cell set lanes with
   * VC8-style overflow guard semantics (`0x18` bytes per node).
   */
  [[maybe_unused]] void* AllocateBlipCellNodeBlock(const unsigned int count)
  {
    constexpr unsigned int kNodeBytes = 0x18u;
    if (count != 0u && (std::numeric_limits<unsigned int>::max() / count) < kNodeBytes) {
      throw std::bad_alloc{};
    }

    return ::operator new(static_cast<std::size_t>(count) * static_cast<std::size_t>(kNodeBytes));
  }

  /**
   * Address: 0x0071A330 (FUN_0071A330, sub_71A330)
   *
   * What it does:
   * Loads one reflected `vector<InfluenceGrid>` payload from archive lanes.
   */
  [[maybe_unused]] void LoadInfluenceGridVectorArchive(
    gpg::ReadArchive* const archive,
    const int objectPtr,
    const int,
    gpg::RRef* const ownerRef
  )
  {
    auto* const vectorObject = PointerFromArchiveInt<InfluenceGridVector>(objectPtr);
    GPG_ASSERT(archive != nullptr);
    GPG_ASSERT(vectorObject != nullptr);
    if (!archive || !vectorObject) {
      return;
    }

    unsigned int count = 0;
    archive->ReadUInt(&count);

    vectorObject->clear();
    if (count == 0u) {
      return;
    }

    vectorObject->resize(count);

    gpg::RType* const valueType = CachedInfluenceGridType();
    GPG_ASSERT(valueType != nullptr);
    if (!valueType) {
      vectorObject->clear();
      return;
    }

    const gpg::RRef owner = ownerRef ? *ownerRef : gpg::RRef{};
    for (unsigned int i = 0; i < count; ++i) {
      archive->Read(valueType, &(*vectorObject)[static_cast<std::size_t>(i)], owner);
    }
  }

  /**
   * Address: 0x0071A4A0 (FUN_0071A4A0)
   *
   * What it does:
   * Serializes one reflected `vector<InfluenceGrid>` payload by writing count
   * and then each `InfluenceGrid` element lane.
   */
  [[maybe_unused]] void SaveInfluenceGridVectorArchive(
    gpg::WriteArchive* const archive,
    const int objectPtr,
    const int,
    gpg::RRef* const ownerRef
  )
  {
    const auto* const vectorObject = ConstPointerFromArchiveInt<InfluenceGridVector>(objectPtr);
    GPG_ASSERT(archive != nullptr);
    if (!archive) {
      return;
    }

    const unsigned int count = vectorObject != nullptr ? static_cast<unsigned int>(vectorObject->size()) : 0u;
    archive->WriteUInt(count);
    if (count == 0u || vectorObject == nullptr) {
      return;
    }

    gpg::RType* const valueType = CachedInfluenceGridType();
    GPG_ASSERT(valueType != nullptr);
    if (!valueType) {
      return;
    }

    const gpg::RRef owner = ownerRef ? *ownerRef : gpg::RRef{};
    for (unsigned int i = 0; i < count; ++i) {
      archive->Write(valueType, const_cast<moho::InfluenceGrid*>(&(*vectorObject)[static_cast<std::size_t>(i)]), owner);
    }
  }

  /**
   * Address: 0x0071A830 (FUN_0071A830)
   *
   * What it does:
   * Serializes one reflected `vector<SThreat>` payload by writing count and
   * then each threat-element lane.
   */
  [[maybe_unused]] void SaveSThreatVectorArchive(
    gpg::WriteArchive* const archive,
    const int objectPtr,
    const int,
    gpg::RRef* const ownerRef
  )
  {
    const auto* const vectorObject = ConstPointerFromArchiveInt<SThreatVector>(objectPtr);
    GPG_ASSERT(archive != nullptr);
    if (!archive) {
      return;
    }

    const unsigned int count = vectorObject != nullptr ? static_cast<unsigned int>(vectorObject->size()) : 0u;
    archive->WriteUInt(count);
    if (count == 0u || vectorObject == nullptr) {
      return;
    }

    gpg::RType* const valueType = CachedSThreatType();
    GPG_ASSERT(valueType != nullptr);
    if (!valueType) {
      return;
    }

    const gpg::RRef owner = ownerRef ? *ownerRef : gpg::RRef{};
    for (unsigned int i = 0; i < count; ++i) {
      archive->Write(valueType, const_cast<moho::SThreat*>(&(*vectorObject)[static_cast<std::size_t>(i)]), owner);
    }
  }

  /**
   * Address: 0x0071AEC0 (FUN_0071AEC0)
   *
   * What it does:
   * Resizes one `vector<SThreat>` with fill semantics, trimming or appending
   * zeroed threat lanes as needed.
   */
  void ResizeSThreatVectorWithFill(
    SThreatVector& storage,
    const std::size_t requestedCount,
    const moho::SThreat& fillValue
  )
  {
    const std::size_t currentCount = storage.size();
    if (currentCount < requestedCount) {
      storage.resize(requestedCount, fillValue);
      return;
    }

    if (requestedCount < currentCount) {
      storage.resize(requestedCount);
    }
  }

  /**
   * Address: 0x0071C6C0 (FUN_0071C6C0, sub_71C6C0)
   *
   * What it does:
   * Clones one `InfluenceGrid::entries` ordered-set tree into destination
   * storage, preserving ordered contents and node count.
   */
  void CopyInfluenceEntryTreeStorage(InfluenceEntrySet& destination, const InfluenceEntrySet& source)
  {
    if (&destination == &source) {
      return;
    }

    destination.clear();
    for (InfluenceEntrySet::const_iterator it = source.begin(); it != source.end(); ++it) {
      destination.insert(*it);
    }
  }

  /**
   * Address: 0x0071AA60 (FUN_0071AA60, sub_71AA60)
   *
   * What it does:
   * Rebuilds one `InfluenceGrid::entries` tree from a source grid by copying
   * each stored `InfluenceMapEntry` into the destination set.
   */
  void CopyInfluenceGridEntries(const moho::InfluenceGrid& source, moho::InfluenceGrid& destination)
  {
    if (&source == &destination) {
      return;
    }

    new (&destination.entries) decltype(destination.entries)();
    CopyInfluenceEntryTreeStorage(destination.entries, source.entries);
  }

  /**
   * Address: 0x007186F0 (FUN_007186F0, sub_7186F0)
   *
   * What it does:
   * Finds the exact `InfluenceMapEntry` for one entity id using the ordered
   * set lookup lane.
   */
  template <class TEntries>
  [[nodiscard]] auto FindInfluenceMapEntry(TEntries& entries, const std::uint32_t entityId)
  {
    moho::InfluenceMapEntry key{};
    key.entityId = entityId;

    auto it = entries.lower_bound(key);
    if (it == entries.end() || it->entityId != entityId) {
      return entries.end();
    }

    return it;
  }

  /**
   * Address: 0x00719C00 (FUN_00719C00, sub_719C00)
   *
   * What it does:
   * Releases the entries tree for one `InfluenceGrid`.
   */
  void DestroyInfluenceGridEntries(moho::InfluenceGrid& grid) noexcept
  {
    ClearInfluenceGridEntryTree(grid.entries);
  }

  /**
   * Address: 0x0071C4A0 (FUN_0071C4A0, sub_71C4A0)
   *
   * What it does:
   * Initializes the legacy blip-cell set into the empty-tree state used by
   * the constructor lane.
   */
  void InitializeBlipCellSet(InfluenceMapCellSet& blipCells) noexcept
  {
    (void)EraseBlipCellRange(blipCells, blipCells.begin(), blipCells.end());
  }

  /**
   * Address: 0x00715C30 (FUN_00715C30, sub_715C30)
   *
   * What it does:
   * Releases the legacy blip-cell set from the destructor lane.
   */
  void ReleaseBlipCellSet(InfluenceMapCellSet& blipCells) noexcept
  {
    (void)EraseBlipCellRange(blipCells, blipCells.begin(), blipCells.end());
  }
} // namespace

/**
 * Address: 0x00718C70 (FUN_00718C70, gpg::RMapType_uint_int::GetName)
 *
 * What it does:
 * Lazily builds and caches the reflected type label `map<unsigned int,int>`.
 */
const char* gpg::RMapType_uint_int::GetName() const
{
  if ((gMapUintIntTypeNameInitGuard & 1u) == 0u) {
    gMapUintIntTypeNameInitGuard |= 1u;

    const gpg::RType* const keyType = CachedUIntType();
    const gpg::RType* const valueType = CachedIntType();
    const char* const valueTypeName = valueType ? valueType->GetName() : "int";
    const char* const keyTypeName = keyType ? keyType->GetName() : "unsigned int";
    gMapUintIntTypeName = gpg::STR_Printf("map<%s,%s>", keyTypeName, valueTypeName);
    (void)std::atexit(&cleanup_MapUintIntTypeName);
  }

  return gMapUintIntTypeName.c_str();
}

/**
 * Address: 0x00718D50 (FUN_00718D50, gpg::RMapType_uint_int::GetLexical)
 *
 * What it does:
 * Formats inherited map lexical text with current element count.
 */
msvc8::string gpg::RMapType_uint_int::GetLexical(const gpg::RRef& ref) const
{
  const msvc8::string base = gpg::RType::GetLexical(ref);
  return gpg::STR_Printf("%s, size=%d", base.c_str(), static_cast<int>(CountLegacyMapElements(ref.mObj)));
}

/**
 * Address: 0x00718D30 (FUN_00718D30, gpg::RMapType_uint_int::Init)
 *
 * What it does:
 * Initializes map reflection metadata and binds typed archive callbacks.
 */
void gpg::RMapType_uint_int::Init()
{
  size_ = 0x0C;
  version_ = 1;
  serLoadFunc_ = &LoadUIntIntMap;
  serSaveFunc_ = &SaveUIntIntMap;
}

/**
 * Address: 0x00718FE0 (FUN_00718FE0, gpg::RMapType_uint_InfluenceMapEntry::GetName)
 *
 * What it does:
 * Lazily builds and caches the reflected type label
 * `map<unsigned int,InfluenceMapEntry>`.
 */
const char* gpg::RMapType_uint_InfluenceMapEntry::GetName() const
{
  if ((gMapUintInfluenceMapEntryTypeNameInitGuard & 1u) == 0u) {
    gMapUintInfluenceMapEntryTypeNameInitGuard |= 1u;

    const gpg::RType* const keyType = CachedUIntType();
    const gpg::RType* const valueType = CachedInfluenceMapEntryType();
    const char* const valueTypeName = valueType ? valueType->GetName() : "InfluenceMapEntry";
    const char* const keyTypeName = keyType ? keyType->GetName() : "unsigned int";
    gMapUintInfluenceMapEntryTypeName = gpg::STR_Printf("map<%s,%s>", keyTypeName, valueTypeName);
    (void)std::atexit(&cleanup_MapUintInfluenceMapEntryTypeName);
  }

  return gMapUintInfluenceMapEntryTypeName.c_str();
}

/**
 * Address: 0x007190C0 (FUN_007190C0, gpg::RMapType_uint_InfluenceMapEntry::GetLexical)
 *
 * What it does:
 * Formats inherited map lexical text with current element count.
 */
msvc8::string gpg::RMapType_uint_InfluenceMapEntry::GetLexical(const gpg::RRef& ref) const
{
  const msvc8::string base = gpg::RType::GetLexical(ref);
  return gpg::STR_Printf("%s, size=%d", base.c_str(), static_cast<int>(CountLegacyMapElements(ref.mObj)));
}

/**
 * Address: 0x007190A0 (FUN_007190A0, gpg::RMapType_uint_InfluenceMapEntry::Init)
 *
 * What it does:
 * Initializes map reflection metadata and binds typed archive callbacks.
 */
void gpg::RMapType_uint_InfluenceMapEntry::Init()
{
  size_ = 0x0C;
  version_ = 1;
  serLoadFunc_ = &LoadUIntInfluenceMapEntryMap;
  serSaveFunc_ = &SaveUIntInfluenceMapEntryMap;
}

/**
 * Address: 0x0071D980 (FUN_0071D980, preregister_RMapType_uint_int)
 *
 * What it does:
 * Constructs/preregisters RTTI metadata for `std::map<std::uint32_t,int>`.
 */
[[nodiscard]] gpg::RType* preregister_RMapType_uint_int()
{
  static gpg::RMapType_uint_int typeInfo;
  gpg::PreRegisterRType(typeid(UIntIntMap), &typeInfo);
  return &typeInfo;
}

/**
 * Address: 0x0071DA50 (FUN_0071DA50, preregister_RMapType_uint_InfluenceMapEntry)
 *
 * What it does:
 * Constructs/preregisters RTTI metadata for
 * `std::map<std::uint32_t,moho::InfluenceMapEntry>`.
 */
[[nodiscard]] gpg::RType* preregister_RMapType_uint_InfluenceMapEntry()
{
  static gpg::RMapType_uint_InfluenceMapEntry typeInfo;
  gpg::PreRegisterRType(typeid(UIntInfluenceMapEntryMap), &typeInfo);
  return &typeInfo;
}

/**
 * Address: 0x00718DE0 (FUN_00718DE0, gpg::RVectorType_InfluenceGrid::GetName)
 *
 * What it does:
 * Lazily builds and caches the reflected lexical type label
 * `vector<InfluenceGrid>` from runtime RTTI metadata.
 */
const char* gpg::RVectorType_InfluenceGrid::GetName() const
{
  if ((gInfluenceGridVectorTypeNameInitGuard & 1u) == 0u) {
    gInfluenceGridVectorTypeNameInitGuard |= 1u;

    gpg::RType* const valueType = CachedInfluenceGridType();
    const char* const valueTypeName = valueType ? valueType->GetName() : "InfluenceGrid";
    gInfluenceGridVectorTypeName = gpg::STR_Printf("vector<%s>", valueTypeName ? valueTypeName : "InfluenceGrid");
    (void)std::atexit(&cleanup_InfluenceGridVectorTypeName);
  }

  return gInfluenceGridVectorTypeName.c_str();
}

/**
 * Address: 0x00718EA0 (FUN_00718EA0, gpg::RVectorType_InfluenceGrid::GetLexical)
 *
 * What it does:
 * Formats inherited vector lexical text with current `InfluenceGrid` count.
 */
msvc8::string gpg::RVectorType_InfluenceGrid::GetLexical(const gpg::RRef& ref) const
{
  const msvc8::string base = gpg::RType::GetLexical(ref);
  return gpg::STR_Printf(
    "%s, size=%d",
    base.c_str(),
    static_cast<int>(CountLegacyVectorElements<moho::InfluenceGrid>(ref.mObj))
  );
}

void gpg::RVectorType_InfluenceGrid::Init()
{
  size_ = 0x0C;
  version_ = 1;
  serLoadFunc_ = &LoadInfluenceGridVectorArchive;
  serSaveFunc_ = &SaveInfluenceGridVectorArchive;
}

/**
 * Address: 0x00719150 (FUN_00719150, gpg::RVectorType_SThreat::GetName)
 *
 * What it does:
 * Lazily builds and caches the reflected lexical type label
 * `vector<SThreat>` from runtime RTTI metadata.
 */
const char* gpg::RVectorType_SThreat::GetName() const
{
  if ((gSThreatVectorTypeNameInitGuard & 1u) == 0u) {
    gSThreatVectorTypeNameInitGuard |= 1u;

    gpg::RType* const valueType = CachedSThreatType();
    const char* const valueTypeName = valueType ? valueType->GetName() : "SThreat";
    gSThreatVectorTypeName = gpg::STR_Printf("vector<%s>", valueTypeName ? valueTypeName : "SThreat");
    (void)std::atexit(&cleanup_SThreatVectorTypeName);
  }

  return gSThreatVectorTypeName.c_str();
}

/**
 * Address: 0x00719210 (FUN_00719210, gpg::RVectorType_SThreat::GetLexical)
 *
 * What it does:
 * Formats inherited vector lexical text with current `SThreat` count.
 */
msvc8::string gpg::RVectorType_SThreat::GetLexical(const gpg::RRef& ref) const
{
  const msvc8::string base = gpg::RType::GetLexical(ref);
  return gpg::STR_Printf("%s, size=%d", base.c_str(), static_cast<int>(CountLegacyVectorElements<moho::SThreat>(ref.mObj)));
}

void gpg::RVectorType_SThreat::Init()
{
  size_ = 0x0C;
  version_ = 1;
  serSaveFunc_ = &SaveSThreatVectorArchive;
}

namespace moho
{
  gpg::RType* SThreat::sType = nullptr;
  gpg::RType* InfluenceMapEntry::sType = nullptr;
  gpg::RType* InfluenceGrid::sType = nullptr;
  gpg::RType* CInfluenceMap::sType = nullptr;

  gpg::RType* SThreat::StaticGetClass()
  {
    if (!sType) {
      sType = gpg::LookupRType(typeid(SThreat));
    }
    return sType;
  }

  gpg::RType* InfluenceMapEntry::StaticGetClass()
  {
    if (!sType) {
      sType = gpg::LookupRType(typeid(InfluenceMapEntry));
    }
    return sType;
  }

  gpg::RType* InfluenceGrid::StaticGetClass()
  {
    if (!sType) {
      sType = gpg::LookupRType(typeid(InfluenceGrid));
    }
    return sType;
  }

  gpg::RType* CInfluenceMap::StaticGetClass()
  {
    if (!sType) {
      sType = gpg::LookupRType(typeid(CInfluenceMap));
    }
    return sType;
  }

  /**
   * Address: 0x00BDA3E0 (FUN_00BDA3E0, register_imap_debug_ConAliasDef)
   */
  void register_imap_debug_ConAliasDef()
  {
    static bool sInitialized = false;
    if (sInitialized) {
      return;
    }

    sInitialized = true;
    ConAlias_imap_debug().InitializeRecovered(
      "Toggle influence map debug info.",
      "imap_debug",
      "DoSimCommand imap_debug"
    );
  }

  /**
   * Address: 0x00BDA410 (FUN_00BDA410, register_imap_debug_SimConVarDef)
   */
  void register_imap_debug_SimConVarDef()
  {
    (void)SimConVar_imap_debug();
  }

  /**
   * Address: 0x00BDA460 (FUN_00BDA460, register_imap_debug_grid_ConAliasDef)
   */
  void register_imap_debug_grid_ConAliasDef()
  {
    static bool sInitialized = false;
    if (sInitialized) {
      return;
    }

    sInitialized = true;
    ConAlias_imap_debug_grid().InitializeRecovered(
      "Toggle influence map debug grid info.",
      "imap_debug_grid",
      "DoSimCommand imap_debug_grid"
    );
  }

  /**
   * Address: 0x00BDA490 (FUN_00BDA490, func_imap_debug_grid_SimConVarDef)
   */
  void func_imap_debug_grid_SimConVarDef()
  {
    (void)SimConVar_imap_debug_grid();
  }

  /**
   * Address: 0x00BDA4E0 (FUN_00BDA4E0, register_imap_debug_path_graph_ConAliasDef)
   */
  void register_imap_debug_path_graph_ConAliasDef()
  {
    static bool sInitialized = false;
    if (sInitialized) {
      return;
    }

    sInitialized = true;
    ConAlias_imap_debug_path_graph().InitializeRecovered(
      "Toggle map hints path graph.",
      "imap_debug_path_graph",
      "DoSimCommand imap_debug_path_graph"
    );
  }

  /**
   * Address: 0x00BDA510 (FUN_00BDA510, func_imap_debug_path_graph_SimConVarDef)
   */
  void func_imap_debug_path_graph_SimConVarDef()
  {
    (void)SimConVar_imap_debug_path_graph();
  }

  /**
   * Address: 0x00BDA560 (FUN_00BDA560, register_imap_debug_grid_type_ConAliasDef)
   */
  void register_imap_debug_grid_type_ConAliasDef()
  {
    static bool sInitialized = false;
    if (sInitialized) {
      return;
    }

    sInitialized = true;
    ConAlias_imap_debug_grid_type().InitializeRecovered(
      "Set influence map debug grid threat type.",
      "imap_debug_grid_type",
      "DoSimCommand imap_debug_grid_type"
    );
  }

  /**
   * Address: 0x00BDA590 (FUN_00BDA590, func_imap_debug_grid_type_SimConVarDef)
   */
  void func_imap_debug_grid_type_SimConVarDef()
  {
    (void)SimConVar_imap_debug_grid_type();
  }

  /**
   * Address: 0x00BDA5E0 (FUN_00BDA5E0, register_imap_debug_grid_army_ConAliasDef)
   */
  void register_imap_debug_grid_army_ConAliasDef()
  {
    static bool sInitialized = false;
    if (sInitialized) {
      return;
    }

    sInitialized = true;
    ConAlias_imap_debug_grid_army().InitializeRecovered(
      "Set influence map debug grid for which army threat type.",
      "imap_debug_grid_army",
      "DoSimCommand imap_debug_grid_army"
    );
  }

  /**
   * Address: 0x00BDA610 (FUN_00BDA610, func_imap_debug_grid_army_SimConVarDef)
   */
  void func_imap_debug_grid_army_SimConVarDef()
  {
    (void)SimConVar_imap_debug_grid_army();
  }

  void SThreat::Clear() noexcept
  {
    overallInfluence = 0.0f;
    influenceStructuresNotMex = 0.0f;
    influenceStructures = 0.0f;
    navalInfluence = 0.0f;
    airInfluence = 0.0f;
    landInfluence = 0.0f;
    experimentalInfluence = 0.0f;
    commanderInfluence = 0.0f;
    artilleryInfluence = 0.0f;
    antiAirInfluence = 0.0f;
    antiSurfaceInfluence = 0.0f;
    antiSubInfluence = 0.0f;
    economyInfluence = 0.0f;
    unknownInfluence = 0.0f;
  }

  void SThreat::RecomputeOverall() noexcept
  {
    overallInfluence = antiSurfaceInfluence + experimentalInfluence + influenceStructures + antiSubInfluence
      + commanderInfluence + navalInfluence + economyInfluence + artilleryInfluence + airInfluence + unknownInfluence
      + antiAirInfluence + landInfluence + influenceStructuresNotMex;
  }

  void SThreat::DecayBy(const SThreat& decayRate) noexcept
  {
    influenceStructuresNotMex = DecayThreatLane(influenceStructuresNotMex, decayRate.influenceStructuresNotMex);
    influenceStructures = DecayThreatLane(influenceStructures, decayRate.influenceStructures);
    navalInfluence = DecayThreatLane(navalInfluence, decayRate.navalInfluence);
    airInfluence = DecayThreatLane(airInfluence, decayRate.airInfluence);
    landInfluence = DecayThreatLane(landInfluence, decayRate.landInfluence);
    experimentalInfluence = DecayThreatLane(experimentalInfluence, decayRate.experimentalInfluence);
    commanderInfluence = DecayThreatLane(commanderInfluence, decayRate.commanderInfluence);
    artilleryInfluence = DecayThreatLane(artilleryInfluence, decayRate.artilleryInfluence);
    antiAirInfluence = DecayThreatLane(antiAirInfluence, decayRate.antiAirInfluence);
    antiSurfaceInfluence = DecayThreatLane(antiSurfaceInfluence, decayRate.antiSurfaceInfluence);
    antiSubInfluence = DecayThreatLane(antiSubInfluence, decayRate.antiSubInfluence);
    economyInfluence = DecayThreatLane(economyInfluence, decayRate.economyInfluence);
    unknownInfluence = DecayThreatLane(unknownInfluence, decayRate.unknownInfluence);
    RecomputeOverall();
  }

  [[nodiscard]] float SThreat::ValueByType(const EThreatType threatType) const noexcept
  {
    switch (threatType) {
      case THREATTYPE_Overall:
      case THREATTYPE_OverallNotAssigned:
        return overallInfluence;
      case THREATTYPE_StructuresNotMex:
        return influenceStructuresNotMex;
      case THREATTYPE_Structures:
        return influenceStructures;
      case THREATTYPE_Naval:
        return navalInfluence;
      case THREATTYPE_Air:
        return airInfluence;
      case THREATTYPE_Land:
        return landInfluence;
      case THREATTYPE_Experimental:
        return experimentalInfluence;
      case THREATTYPE_Commander:
        return commanderInfluence;
      case THREATTYPE_Artillery:
        return artilleryInfluence;
      case THREATTYPE_AntiAir:
        return antiAirInfluence;
      case THREATTYPE_AntiSurface:
        return antiSurfaceInfluence;
      case THREATTYPE_AntiSub:
        return antiSubInfluence;
      case THREATTYPE_Economy:
        return economyInfluence;
      case THREATTYPE_Unknown:
      default:
        return unknownInfluence;
    }
  }

  /**
   * Address: 0x0071E760 (FUN_0071E760, func_VectorCpy_SThreat)
   *
   * What it does:
   * Copies one `SThreat` source value into `count` consecutive destination
   * slots while preserving the original helper's per-iteration null check on
   * the destination address.
   */
  void CopySThreatValueRange(SThreat* destination, std::uint32_t count, const SThreat* const source) noexcept
  {
    std::uintptr_t destinationAddress = reinterpret_cast<std::uintptr_t>(destination);
    while (count != 0u) {
      if (destinationAddress != 0u) {
        *reinterpret_cast<SThreat*>(destinationAddress) = *source;
      }
      --count;
      destinationAddress += sizeof(SThreat);
    }
  }

  /**
   * Address: 0x0071F6A0 (FUN_0071F6A0, func_VectorMemCpy_SThreat)
   *
   * What it does:
   * Copies one contiguous `SThreat` source range `[sourceBegin, sourceEnd)`
   * into destination storage and returns one-past the last destination slot,
   * preserving the helper's original per-iteration null-destination guard.
   */
  SThreat* CopySThreatRangeNullable(
    SThreat* destination,
    const SThreat* const sourceBegin,
    const SThreat* const sourceEnd
  ) noexcept
  {
    const SThreat* source = sourceBegin;
    std::uintptr_t destinationAddress = reinterpret_cast<std::uintptr_t>(destination);
    while (source != sourceEnd) {
      if (destinationAddress != 0u) {
        *reinterpret_cast<SThreat*>(destinationAddress) = *source;
      }

      ++source;
      destinationAddress += sizeof(SThreat);
    }

    return reinterpret_cast<SThreat*>(destinationAddress);
  }

  /**
   * Address: 0x00715030 (FUN_00715030, ??0InfluenceGrid@Moho@@QAE@@Z)
   */
  InfluenceGrid::InfluenceGrid()
    : entries()
    , threats()
    , threat{}
    , decay{}
  {
    threat.Clear();
    decay.Clear();
  }

  /**
   * Address: 0x00716350 (FUN_00716350, ??1InfluenceGrid@Moho@@QAE@@Z)
   */
  InfluenceGrid::~InfluenceGrid()
  {
    threats.clear();
    DestroyInfluenceGridEntries(*this);
  }

  /**
   * Address: 0x0071EA00 (FUN_0071EA00, std::vector_InfluenceGrid::~vector_InfluenceGrid)
   *
   * What it does:
   * Destroys one contiguous range of `InfluenceGrid` elements by releasing
   * per-grid threat vectors and entry maps.
   */
  [[maybe_unused]] static void DestroyInfluenceGridRange(InfluenceGrid* const start, InfluenceGrid* const end)
  {
    for (InfluenceGrid* cursor = start; cursor != end; ++cursor) {
      cursor->threats.clear();
      cursor->entries.clear();
    }
  }

  /**
   * Address: 0x0071EAA0 (FUN_0071EAA0, fill_InfluenceGrid_range)
   *
   * What it does:
   * Assigns one shared `InfluenceGrid` value across `[destinationBegin,
   * destinationEnd)` by cloning entries, per-army threats, and aggregate/decay
   * threat lanes into each destination element.
   */
  [[maybe_unused]] static void FillInfluenceGridRange(
    InfluenceGrid* const destinationBegin,
    InfluenceGrid* const destinationEnd,
    const InfluenceGrid& fillValue
  )
  {
    for (InfluenceGrid* cursor = destinationBegin; cursor != destinationEnd; ++cursor) {
      if (cursor != &fillValue) {
        CopyInfluenceGridEntries(fillValue, *cursor);
        cursor->threats.clear();
        for (const SThreat* it = fillValue.threats.begin(); it != fillValue.threats.end(); ++it) {
          cursor->threats.push_back(*it);
        }
      }

      cursor->threat = fillValue.threat;
      cursor->decay = fillValue.decay;
    }
  }

  /**
   * Address: 0x0071F5B0 (FUN_0071F5B0, copy_InfluenceGrid_range_backward)
   *
   * What it does:
   * Copies one `InfluenceGrid` range backward from `[sourceBegin, sourceEnd)`
   * into the destination range ending at `destinationEnd`, preserving overlap
   * semantics used by legacy vector insert/shift lanes.
   */
  [[maybe_unused]] static InfluenceGrid* CopyInfluenceGridRangeBackward(
    InfluenceGrid* const sourceEnd,
    InfluenceGrid* const sourceBegin,
    InfluenceGrid* const destinationEnd
  )
  {
    InfluenceGrid* sourceCursor = sourceEnd;
    InfluenceGrid* destinationCursor = destinationEnd;
    while (sourceCursor != sourceBegin) {
      --sourceCursor;
      --destinationCursor;

      if (destinationCursor != sourceCursor) {
        CopyInfluenceGridEntries(*sourceCursor, *destinationCursor);
        destinationCursor->threats.clear();
        for (const SThreat* it = sourceCursor->threats.begin(); it != sourceCursor->threats.end(); ++it) {
          destinationCursor->threats.push_back(*it);
        }
      }

      destinationCursor->threat = sourceCursor->threat;
      destinationCursor->decay = sourceCursor->decay;
    }

    return destinationCursor;
  }

  /**
   * Address: 0x0071E7B0 (FUN_0071E7B0, Moho::InfluenceGrid::ThreatDeconstruct)
   *
   * What it does:
   * Copies one contiguous `InfluenceGrid` range into destination storage for
   * vector relocation/copy lanes, preserving per-grid entry map, threat vector,
   * aggregate threat, and decay state.
   */
  [[maybe_unused]] static InfluenceGrid*
  CopyInfluenceGridRange(const InfluenceGrid* start, const InfluenceGrid* end, InfluenceGrid* dest)
  {
    for (const InfluenceGrid* source = start; source != end; ++source, ++dest) {
      if (dest != source) {
        CopyInfluenceGridEntries(*source, *dest);
        new (&dest->threats) decltype(dest->threats)();
        for (const SThreat* it = source->threats.begin(); it != source->threats.end(); ++it) {
          dest->threats.push_back(*it);
        }
      }
      dest->threat = source->threat;
      dest->decay = source->decay;
    }
    return dest;
  }

  /**
   * Address: 0x00720180 (FUN_00720180, copy_InfluenceGrid_range_with_rollback)
   *
   * What it does:
   * Copy-constructs one contiguous `InfluenceGrid` range into destination
   * storage and destroys already-constructed grids before rethrowing if a copy
   * step throws.
   */
  [[maybe_unused]] static InfluenceGrid*
  CopyInfluenceGridRangeWithRollback(const InfluenceGrid* start, const InfluenceGrid* end, InfluenceGrid* dest)
  {
    InfluenceGrid* cursor = dest;
    try {
      for (const InfluenceGrid* source = start; source != end; ++source, ++cursor) {
        if (cursor != source) {
          CopyInfluenceGridEntries(*source, *cursor);
          new (&cursor->threats) decltype(cursor->threats)();
          for (const SThreat* it = source->threats.begin(); it != source->threats.end(); ++it) {
            cursor->threats.push_back(*it);
          }
        }
        cursor->threat = source->threat;
        cursor->decay = source->decay;
      }
      return cursor;
    } catch (...) {
      for (InfluenceGrid* destroyCursor = dest; destroyCursor != cursor; ++destroyCursor) {
        destroyCursor->~InfluenceGrid();
      }
      throw;
    }
  }

  /**
   * Address: 0x0071E840 (FUN_0071E840, copy_InfluenceGrid_counted_range_with_rollback)
   *
   * What it does:
   * Copy-constructs `count` contiguous `InfluenceGrid` elements from `source`
   * into destination storage and destroys already-constructed lanes before
   * rethrowing if a copy step throws.
   */
  [[maybe_unused]] static InfluenceGrid* CopyInfluenceGridCountedRangeWithRollback(
    const std::uint32_t count,
    InfluenceGrid* const destination,
    const InfluenceGrid* const source
  )
  {
    if (count == 0u) {
      return destination;
    }

    if (destination == nullptr || source == nullptr) {
      return destination;
    }

    return CopyInfluenceGridRangeWithRollback(source, source + count, destination);
  }

  /**
   * Address: 0x0071FCA0 (FUN_0071FCA0, copy_InfluenceGrid_range_with_rollback_alt)
   *
   * What it does:
   * Alternate guarded contiguous `InfluenceGrid` range-copy lane that copies
   * `[sourceBegin, sourceEnd)` into destination storage and destroys already
   * constructed grids before rethrowing on copy failure.
   */
  [[maybe_unused]] static InfluenceGrid* CopyInfluenceGridRangeWithRollbackAlt(
    const InfluenceGrid* const sourceBegin,
    const InfluenceGrid* const sourceEnd,
    InfluenceGrid* const destinationBegin
  )
  {
    InfluenceGrid* destinationCursor = destinationBegin;
    try {
      for (const InfluenceGrid* sourceCursor = sourceBegin;
           sourceCursor != sourceEnd;
           ++sourceCursor, ++destinationCursor) {
        if (destinationCursor != nullptr) {
          (void)CopyInfluenceGridRange(sourceCursor, sourceCursor + 1, destinationCursor);
        }
      }
      return destinationCursor;
    } catch (...) {
      for (InfluenceGrid* destroyCursor = destinationBegin;
           destroyCursor != destinationCursor;
           ++destroyCursor) {
        destroyCursor->~InfluenceGrid();
      }
      throw;
    }
  }

  /**
   * Address: 0x0071FD40 (FUN_0071FD40, copy_InfluenceGrid_counted_range)
   *
   * What it does:
   * Copies `count` contiguous `InfluenceGrid` elements from `source` into
   * `destination` using the recovered range-copy helper.
   */
  void CopyInfluenceGridCountedRange(
    InfluenceGrid* const destination,
    const InfluenceGrid* const source,
    const int count
  )
  {
    if (destination == nullptr || source == nullptr || count <= 0) {
      return;
    }

    (void)CopyInfluenceGridRange(source, source + count, destination);
  }

  /**
   * Address: 0x0071D4B0 (FUN_0071D4B0, func_NewArray_SThreat)
   *
   * What it does:
   * Allocates contiguous storage for `count` `SThreat` elements with the same
   * overflow guard semantics as the original VC8 array-allocation helper.
   */
  [[maybe_unused]] static SThreat* func_NewArray_SThreat(const unsigned int count)
  {
    if (count != 0u && (0xFFFFFFFFu / count) < sizeof(SThreat)) {
      throw std::bad_alloc{};
    }

    return static_cast<SThreat*>(::operator new(sizeof(SThreat) * static_cast<std::size_t>(count)));
  }

  /**
   * Address: 0x0071D5E0 (FUN_0071D5E0, func_NewArray_InfluenceMap)
   *
   * What it does:
   * Allocates contiguous storage for `count` `InfluenceGrid` elements with the
   * same overflow guard semantics as the original VC8 array-allocation helper.
   */
  [[maybe_unused]] static InfluenceGrid* func_NewArray_InfluenceMap(const unsigned int count)
  {
    if (count != 0u && (0xFFFFFFFFu / count) < sizeof(InfluenceGrid)) {
      throw std::bad_alloc{};
    }

    return static_cast<InfluenceGrid*>(::operator new(sizeof(InfluenceGrid) * static_cast<std::size_t>(count)));
  }

  /**
   * Address: 0x00715750 (FUN_00715750, ?GetThreat@InfluenceGrid@Moho@@QBEMW4EThreatType@2@H@Z)
   */
  float InfluenceGrid::GetThreat(const EThreatType threatType, const int army) const
  {
    float result = (threatType == THREATTYPE_OverallNotAssigned) ? 0.0f : threat.ValueByType(threatType);

    if (army >= 0) {
      const std::size_t armyIndex = static_cast<std::size_t>(army);
      if (armyIndex < threats.size()) {
        result += threats[armyIndex].ValueByType(threatType);
      }
      return result;
    }

    for (const SThreat* it = threats.begin(); it != threats.end(); ++it) {
      result += it->ValueByType(threatType);
    }
    return result;
  }

  /**
   * Address: 0x00715130 (FUN_00715130, ?DecayInfluence@InfluenceGrid@Moho@@QAEPAV12@XZ)
   */
  void InfluenceGrid::DecayInfluence()
  {
    threat.DecayBy(decay);
  }

  void InfluenceGrid::EnsureThreatSlots(const std::size_t armyCount)
  {
    const moho::SThreat fillValue{};
    ResizeSThreatVectorWithFill(threats, armyCount, fillValue);
  }

  void InfluenceGrid::ClearPerArmyThreats()
  {
    for (SThreat* it = threats.begin(); it != threats.end(); ++it) {
      it->Clear();
    }
  }

  InfluenceMapEntry* InfluenceGrid::FindEntry(const std::uint32_t entityId)
  {
    const auto it = FindInfluenceMapEntry(entries, entityId);
    if (it == entries.end()) {
      return nullptr;
    }

    return const_cast<InfluenceMapEntry*>(&(*it));
  }

  const InfluenceMapEntry* InfluenceGrid::FindEntry(const std::uint32_t entityId) const
  {
    const auto it = FindInfluenceMapEntry(entries, entityId);
    if (it == entries.end()) {
      return nullptr;
    }

    return &(*it);
  }

  bool InfluenceGrid::RemoveEntry(const std::uint32_t entityId)
  {
    const auto it = FindInfluenceMapEntry(entries, entityId);
    if (it == entries.end()) {
      return false;
    }

    entries.erase(it);
    return true;
  }

  /**
   * Address: 0x00715BC0 (FUN_00715BC0, ??0CInfluenceMap@Moho@@QAE@XZ)
   */
  CInfluenceMap::CInfluenceMap()
    : mArmy(nullptr)
    , mTotal(0)
    , mWidth(0)
    , mHeight(0)
    , mGridSize(0)
    , mBlipCells()
    , mMapEntries()
  {
    InitializeBlipCellSet(mBlipCells);
    mMapEntries.clear();
  }

  /**
   * Address: 0x00716140 (FUN_00716140, ??0CInfluenceMap@Moho@@QAE@Z)
   */
  CInfluenceMap::CInfluenceMap(const std::int32_t gridSize, Sim* const sim, CArmyImpl* const army)
    : mArmy(army)
    , mTotal(0)
    , mWidth(0)
    , mHeight(0)
    , mGridSize(gridSize)
    , mBlipCells()
    , mMapEntries()
  {
    mMapEntries.clear();
    InitializeBlipCellSet(mBlipCells);

    const STIMap* const mapData = sim ? sim->mMapData : nullptr;
    const CHeightField* const heightField = mapData ? mapData->mHeightField.get() : nullptr;
    if (!heightField || mGridSize <= 0) {
      return;
    }

    mWidth = (heightField->width - 1) / mGridSize;
    mHeight = (heightField->height - 1) / mGridSize;
    mTotal = mWidth * mHeight;

    if (mTotal <= 0) {
      return;
    }

    mMapEntries.resize(static_cast<std::size_t>(mTotal));
    const std::size_t armyCount = sim ? static_cast<std::size_t>(sim->ArmyCount()) : 0u;
    for (InfluenceGrid* cell = mMapEntries.begin(); cell != mMapEntries.end(); ++cell) {
      cell->EnsureThreatSlots(armyCount);
    }
  }

  /**
   * Address: 0x007163A0 (FUN_007163A0, ??1CInfluenceMap@Moho@@QAE@Z)
   */
  CInfluenceMap::~CInfluenceMap()
  {
    ClearInfluenceGridVectorStorage(mMapEntries);
    ReleaseBlipCellSet(mBlipCells);
  }

  /**
   * Address: 0x00715C60 (FUN_00715C60, ?VectorToCoords@CInfluenceMap@Moho@@AAEHPAV?$Vector3@M@Wm3@@@Z)
   */
  std::int32_t CInfluenceMap::VectorToCoords(const Wm3::Vec3f& pos) const
  {
    if (mGridSize <= 0 || mWidth <= 0 || mHeight <= 0) {
      return 0;
    }

    std::int32_t x = static_cast<std::int32_t>(pos.x) / mGridSize;
    if (x >= (mWidth - 1)) {
      x = mWidth - 1;
    }
    if (x < 0) {
      x = 0;
    }

    std::int32_t z = static_cast<std::int32_t>(pos.z) / mGridSize;
    if (z >= (mHeight - 1)) {
      z = mHeight - 1;
    }
    if (z < 0) {
      z = 0;
    }

    return x + z * mWidth;
  }

  /**
   * Address: 0x00715F30 (FUN_00715F30, ?UpdateBlipPosition@CInfluenceMap@Moho@@QAEXHABV?$Vector3@M@Wm3@@PBVRUnitBlueprint@2@@Z)
   */
  void CInfluenceMap::UpdateBlipPosition(
    const std::uint32_t blipId, const Wm3::Vec3f& position, const RUnitBlueprint* const sourceBlueprint
  )
  {
    const InfluenceMapCellIndex* const knownCell = FindBlipCell(blipId);
    const std::int32_t newCellIndex = VectorToCoords(position);

    if (!knownCell) {
      InsertEntry(blipId, position, sourceBlueprint);
      return;
    }

    const std::int32_t oldCellIndex = knownCell->cellIndex;
    if (oldCellIndex == newCellIndex && oldCellIndex >= 0 && oldCellIndex < mTotal) {
      InfluenceGrid& cell = mMapEntries[static_cast<std::size_t>(oldCellIndex)];
      if (InfluenceMapEntry* const entry = cell.FindEntry(blipId)) {
        entry->threatStrength = 1.0f;
        entry->decayTicks = 10;
        entry->lastPosition = position;
      }
      return;
    }

    RemoveEntry(blipId);
    InsertEntry(blipId, position, sourceBlueprint);
  }

  /**
   * Address: 0x00715FF0 (FUN_00715FF0, ?GetThreatRect@CInfluenceMap@Moho@@QBEMHHH_W4EThreatType@2@H@Z)
   */
  float CInfluenceMap::GetThreatRect(
    const int x, const int z, const int radius, const bool onMap, const EThreatType threatType, const int army
  ) const
  {
    if (mWidth <= 0 || mHeight <= 0 || mMapEntries.empty()) {
      return 0.0f;
    }

    int mapX0 = 0;
    int mapX1 = mWidth - 1;
    int mapZ0 = 0;
    int mapZ1 = mHeight - 1;

    if (onMap && mArmy) {
      const Sim* const sim = mArmy->GetSim();
      const STIMap* const mapData = sim ? sim->mMapData : nullptr;
      if (mapData && mGridSize > 0) {
        mapX0 = mapData->mPlayableRect.x0 / mGridSize;
        mapX1 = mapData->mPlayableRect.x1 / mGridSize;
        mapZ0 = mapData->mPlayableRect.z0 / mGridSize;
        mapZ1 = mapData->mPlayableRect.z1 / mGridSize;
      }
    }

    float totalThreat = 0.0f;
    const int zStart = z - radius;
    const int zEnd = z + radius;

    for (int curZ = zStart; curZ <= zEnd; ++curZ) {
      if (curZ < 0 || curZ >= mHeight) {
        continue;
      }
      if (onMap && (curZ < mapZ0 || curZ > mapZ1)) {
        continue;
      }

      const int xStart = x - radius;
      const int xEnd = x + radius;
      for (int curX = xStart; curX <= xEnd; ++curX) {
        if (curX < 0 || curX >= mWidth) {
          continue;
        }
        if (onMap && (curX < mapX0 || curX > mapX1)) {
          continue;
        }

        const std::int32_t index = curX + curZ * mWidth;
        totalThreat += mMapEntries[static_cast<std::size_t>(index)].GetThreat(threatType, army);
      }
    }

    return totalThreat;
  }

  /**
   * Address: 0x00716E60 (FUN_00716E60, ?GetThreatBetweenPositions@CInfluenceMap@Moho@@QBEMABV?$Vector3@M@Wm3@@0_W4EThreatType@2@H@Z)
   */
  float CInfluenceMap::GetThreatBetweenPositions(
    const Wm3::Vec3f& pos1,
    const Wm3::Vec3f& pos2,
    const bool ring,
    const EThreatType threatType,
    const int armyIndex
  ) const
  {
    if (mWidth <= 0 || mHeight <= 0) {
      return 0.0f;
    }

    const std::int32_t index0 = VectorToCoords(pos1);
    const std::int32_t index1 = VectorToCoords(pos2);

    int x0 = index0 % mWidth;
    int z0 = index0 / mWidth;
    const int x1 = index1 % mWidth;
    const int z1 = index1 / mWidth;

    const int dx = std::abs(x1 - x0);
    const int dz = std::abs(z1 - z0);
    const int sx = (x0 < x1) ? 1 : -1;
    const int sz = (z0 < z1) ? 1 : -1;

    float totalThreat = 0.0f;
    int err = dx - dz;
    while (true) {
      totalThreat += GetThreatRect(x0, z0, 0, ring, threatType, armyIndex);
      if (x0 == x1 && z0 == z1) {
        break;
      }

      const int err2 = err * 2;
      if (err2 > -dz) {
        err -= dz;
        x0 += sx;
      }
      if (err2 < dx) {
        err += dx;
        z0 += sz;
      }
    }

    return totalThreat;
  }

  /**
   * Address: 0x00718A40 (FUN_00718A40)
   *
   * What it does:
   * Builds one Lua threat-sample row and appends it at the next array index in
   * the caller-owned result table.
   */
  void AppendThreatSampleRow(
    LuaPlus::LuaObject* const outObj,
    std::int32_t& luaIndex,
    LuaPlus::LuaState* const state,
    const float worldX,
    const float worldZ,
    const float threat
  )
  {
    LuaPlus::LuaObject point;
    point.AssignNewTable(state, 0, 4);
    point.SetNumber("x", worldX);
    point.SetNumber("y", 0.0f);
    point.SetNumber("z", worldZ);
    point.SetNumber("threat", threat);
    outObj->SetObject(luaIndex, point);
    ++luaIndex;
  }

  /**
   * Address: 0x007171D0 (FUN_007171D0, ?GetThreatsAroundPosition@CInfluenceMap@Moho@@QAE?AVLuaObject@LuaPlus@@AAV42@ABV?$Vector3@M@Wm3@@HHW4EThreatType@2@H@Z)
   */
  LuaPlus::LuaObject* CInfluenceMap::GetThreatsAroundPosition(
    LuaPlus::LuaObject* const outObj,
    const Wm3::Vec3f& pos,
    const int ring,
    const bool restrictToPlayable,
    const EThreatType threatType,
    const int armyIndex
  ) const
  {
    if (!outObj) {
      return nullptr;
    }

    LuaPlus::LuaState* const state = outObj->m_state;
    if (!state) {
      return outObj;
    }

    outObj->AssignNewTable(state, 0, 0);

    const std::int32_t centerIndex = VectorToCoords(pos);
    const int centerX = centerIndex % mWidth;
    const int centerZ = centerIndex / mWidth;

    int mapX0 = 0;
    int mapX1 = mWidth - 1;
    int mapZ0 = 0;
    int mapZ1 = mHeight - 1;

    Sim* const sim = mArmy ? mArmy->GetSim() : nullptr;
    if (restrictToPlayable && sim && sim->mMapData && mGridSize > 0) {
      mapX0 = sim->mMapData->mPlayableRect.x0 / mGridSize;
      mapX1 = sim->mMapData->mPlayableRect.x1 / mGridSize;
      mapZ0 = sim->mMapData->mPlayableRect.z0 / mGridSize;
      mapZ1 = sim->mMapData->mPlayableRect.z1 / mGridSize;
    }

    std::int32_t luaIndex = 1;
    for (int z = centerZ - ring; z <= centerZ + ring; ++z) {
      if (z < 0 || z >= mHeight) {
        continue;
      }
      if (restrictToPlayable && (z < mapZ0 || z > mapZ1)) {
        continue;
      }

      for (int x = centerX - ring; x <= centerX + ring; ++x) {
        if (x < 0 || x >= mWidth) {
          continue;
        }
        if (restrictToPlayable && (x < mapX0 || x > mapX1)) {
          continue;
        }

        const std::int32_t cellIndex = x + z * mWidth;
        const float threat = mMapEntries[static_cast<std::size_t>(cellIndex)].GetThreat(threatType, armyIndex);
        if (threat <= 0.0f) {
          continue;
        }

        const float worldX = static_cast<float>((mGridSize / 2) + (x * mGridSize));
        const float worldZ = static_cast<float>((mGridSize / 2) + (z * mGridSize));
        AppendThreatSampleRow(outObj, luaIndex, state, worldX, worldZ, threat);

        if (sim) {
          const float coords[3] = {worldX, 0.0f, worldZ};
          sim->mContext.Update(&threat, sizeof(threat));
          sim->mContext.Update(coords, sizeof(coords));
        }
      }
    }

    if (sim) {
      const gpg::MD5Digest digest = sim->mContext.Digest();
      const msvc8::string checksum = digest.ToString();
      sim->Logf("after GetThreatsAroundPosition checksum=%s\n", checksum.c_str());
    }

    return outObj;
  }

  /**
   * Address: 0x00716480 (FUN_00716480, ?Update@CInfluenceMap@Moho@@QAEXXZ)
   */
  void CInfluenceMap::Update()
  {
    Sim* const sim = mArmy ? mArmy->GetSim() : nullptr;
    const CategoryWordRangeView* commandCategory = nullptr;
    const CategoryWordRangeView* experimentalCategory = nullptr;
    const CategoryWordRangeView* artilleryCategory = nullptr;
    const CategoryWordRangeView* massExtractorCategory = nullptr;
    if (sim && sim->mRules) {
      commandCategory = sim->mRules->GetEntityCategory("COMMAND");
      experimentalCategory = sim->mRules->GetEntityCategory("EXPERIMENTAL");
      artilleryCategory = sim->mRules->GetEntityCategory("ARTILLERY, STRATEGIC");
      massExtractorCategory = sim->mRules->GetEntityCategory("MASSEXTRACTION");
    }

    for (InfluenceGrid* cell = mMapEntries.begin(); cell != mMapEntries.end(); ++cell) {
      cell->DecayInfluence();
      cell->ClearPerArmyThreats();

      for (auto it = cell->entries.begin(); it != cell->entries.end();) {
        InfluenceMapEntry& entry = const_cast<InfluenceMapEntry&>(*it);

        if (entry.decayTicks > 0) {
          --entry.decayTicks;
        }
        if (entry.decayTicks == 0) {
          entry.threatStrength = DecayThreatLane(entry.threatStrength, entry.threatDecay);
        }

        if (entry.threatStrength <= 0.0f) {
          const float threatStrengthChecksum = entry.threatStrength;
          RemoveBlipCell(entry.entityId);
          it = EraseInfluenceEntryAndAdvance(*cell, it);
          if (sim) {
            sim->mContext.Update(&threatStrengthChecksum, sizeof(threatStrengthChecksum));
          }
          continue;
        }

        if (!IsAlliedOrSameArmy(mArmy, entry.sourceArmy) && sim && sim->mEntityDB) {
          Entity* const entity = FindEntityById(sim->mEntityDB, static_cast<std::int32_t>(entry.entityId));
          if (entity) {
            if (ReconBlip* const blip = entity->IsReconBlip()) {
              entry.sourceLayer = static_cast<std::int32_t>(entity->mCurrentLayer);

              const std::int32_t sourceArmyIndex = entry.sourceArmy ? entry.sourceArmy->ArmyId : -1;
              if (sourceArmyIndex >= 0) {
                const SPerArmyReconInfo* const sourceArmyRecon = blip->GetPerArmyReconInfo(sourceArmyIndex);
                if (sourceArmyRecon) {
                  const std::uint32_t flags = sourceArmyRecon->mReconFlags;
                  if ((flags & RECON_KnownFake) != 0u) {
                    entry.threatStrength = 0.0f;
                  } else if ((flags & RECON_Omni) != 0u || (flags & RECON_LOSEver) != 0u) {
                    entry.isDetailed = 1u;
                  }
                }
              }
            }
          }
        }

        const std::int32_t sourceArmyIndex = entry.sourceArmy ? entry.sourceArmy->ArmyId : -1;
        if (
          sourceArmyIndex >= 0 && static_cast<std::size_t>(sourceArmyIndex) < cell->threats.size()
          && entry.sourceBlueprint != nullptr
        ) {
          SThreat& armyThreat = cell->threats[static_cast<std::size_t>(sourceArmyIndex)];
          const float strength = entry.threatStrength;

          const float antiAir = entry.sourceBlueprint->Defense.AirThreatLevel * strength;
          const float antiSurface = entry.sourceBlueprint->Defense.SurfaceThreatLevel * strength;
          const float antiSub = entry.sourceBlueprint->Defense.SubThreatLevel * strength;
          const float economy = entry.sourceBlueprint->Defense.EconomyThreatLevel * strength;
          const float total = antiAir + antiSurface + antiSub + economy;
          armyThreat.overallInfluence += total;

          if (!entry.sourceBlueprint->IsMobile()) {
            if (IsInCategory(massExtractorCategory, entry.sourceBlueprint->mCategoryBitIndex)) {
              armyThreat.influenceStructuresNotMex += total;
            } else {
              armyThreat.influenceStructures += total;
              armyThreat.influenceStructuresNotMex += total;
            }
          } else {
            if (entry.sourceBlueprint->Air.CanFly != 0u) {
              armyThreat.airInfluence += total;
            } else if (entry.sourceLayer == LAYER_Land) {
              armyThreat.landInfluence += total;
            } else if (entry.sourceLayer == LAYER_Water || entry.sourceLayer == LAYER_Seabed || entry.sourceLayer == LAYER_Sub) {
              armyThreat.navalInfluence += total;
            }
          }

          if (entry.isDetailed != 0u) {
            if (IsInCategory(experimentalCategory, entry.sourceBlueprint->mCategoryBitIndex)) {
              armyThreat.experimentalInfluence += total;
            }
            if (IsInCategory(commandCategory, entry.sourceBlueprint->mCategoryBitIndex)) {
              armyThreat.commanderInfluence += total;
            }
            if (IsInCategory(artilleryCategory, entry.sourceBlueprint->mCategoryBitIndex)) {
              armyThreat.artilleryInfluence += total;
            }

            armyThreat.antiAirInfluence += antiAir;
            armyThreat.antiSurfaceInfluence += antiSurface;
            armyThreat.antiSubInfluence += antiSub;
            armyThreat.economyInfluence += economy;
          } else {
            armyThreat.unknownInfluence += total;
          }
        }

        if (sim) {
          sim->mContext.Update(&entry.threatStrength, sizeof(entry.threatStrength));
        }
        ++it;
      }
    }

    if (sim) {
      const gpg::MD5Digest digest = sim->mContext.Digest();
      const msvc8::string checksum = digest.ToString();
      sim->Logf("after inf checksum=%s\n", checksum.c_str());
    }
  }

  CArmyImpl* CInfluenceMap::ResolveSourceArmy(const std::uint32_t blipId) const
  {
    if (!mArmy) {
      return nullptr;
    }

    Sim* const sim = mArmy->GetSim();
    if (!sim) {
      return nullptr;
    }

    const std::uint32_t armyIndex = (blipId >> 20u) & 0xFFu;
    if (armyIndex == 0xFFu || armyIndex >= sim->mArmiesList.size()) {
      return nullptr;
    }

    return sim->mArmiesList[armyIndex];
  }

  const InfluenceMapCellIndex* CInfluenceMap::FindBlipCell(const std::uint32_t blipId) const
  {
    InfluenceMapCellIndex key{};
    key.entityId = blipId;
    const auto it = mBlipCells.find(key);
    if (it == mBlipCells.end()) {
      return nullptr;
    }

    return &(*it);
  }

  void CInfluenceMap::UpsertBlipCell(const std::uint32_t blipId, const std::int32_t cellIndex)
  {
    RemoveBlipCell(blipId);
    mBlipCells.insert(InfluenceMapCellIndex{blipId, cellIndex});
  }

  void CInfluenceMap::RemoveBlipCell(const std::uint32_t blipId)
  {
    InfluenceMapCellIndex key{};
    key.entityId = blipId;
    const auto it = mBlipCells.find(key);
    if (it != mBlipCells.end()) {
      mBlipCells.erase(it);
    }
  }

  /**
   * Address: 0x00715D10 (FUN_00715D10, Moho::CInfluenceMap::InsertEntry)
   *
   * What it does:
   * Builds one per-blip influence entry at `position`, inserts/updates it in
   * the owning cell lane, and stores the blip-to-cell lookup mapping.
   */
  void CInfluenceMap::InsertEntry(
    const std::uint32_t blipId, const Wm3::Vec3f& position, const RUnitBlueprint* const sourceBlueprint
  )
  {
    const std::int32_t cellIndex = VectorToCoords(position);
    if (cellIndex < 0 || cellIndex >= mTotal) {
      return;
    }

    InfluenceMapEntry entry{};
    entry.entityId = blipId;
    entry.sourceArmy = ResolveSourceArmy(blipId);
    entry.lastPosition = position;
    entry.sourceBlueprint = sourceBlueprint;
    entry.sourceLayer = LAYER_None;
    entry.isDetailed = 0u;
    entry.pad_1D_1F[0] = 0u;
    entry.pad_1D_1F[1] = 0u;
    entry.pad_1D_1F[2] = 0u;
    entry.threatStrength = 1.0f;
    entry.threatDecay = (sourceBlueprint && sourceBlueprint->IsMobile()) ? 0.02f : 0.0f;
    entry.decayTicks = 10;

    InfluenceGrid& cell = mMapEntries[static_cast<std::size_t>(cellIndex)];
    const auto [it, inserted] = cell.entries.insert(entry);
    if (!inserted) {
      InfluenceMapEntry& mutableEntry = const_cast<InfluenceMapEntry&>(*it);
      mutableEntry = entry;
    }

    UpsertBlipCell(blipId, cellIndex);
  }

  void CInfluenceMap::RemoveEntry(const std::uint32_t blipId)
  {
    const InfluenceMapCellIndex* const blipCell = FindBlipCell(blipId);
    if (!blipCell) {
      return;
    }

    const std::int32_t cellIndex = blipCell->cellIndex;
    if (cellIndex >= 0 && cellIndex < mTotal) {
      mMapEntries[static_cast<std::size_t>(cellIndex)].RemoveEntry(blipId);
    }

    RemoveBlipCell(blipId);
  }

  bool CInfluenceMap::IsInCategory(const CategoryWordRangeView* const category, const std::uint32_t categoryBitIndex)
  {
    return category && category->ContainsBit(categoryBitIndex);
  }

  /**
   * Address: 0x00716B00 (FUN_00716B00, Moho::CInfluenceMap::AssignThreatAtPosition)
   *
   * IDA signature:
   * void __userpurge Moho::CInfluenceMap::AssignThreatAtPosition(
   *   Wm3::Vector3f *pos@<eax>, Moho::CInfluenceMap *this@<ecx>,
   *   Moho::EThreatType threatType@<esi>, float assignedThreat, float assignedDecay);
   *
   * What it does:
   * Adds `assignedThreat` to the per-type threat lane of the cell
   * containing `position`, then re-derives the matching decay lane as
   * `(updated threat) * assignedDecay`. Negative `assignedDecay`
   * substitutes a default `0.01` rate. The Overall and Unknown enum
   * values both map to the cell's `unknownInfluence` lane to match
   * the binary's switch fallthrough.
   */
  void CInfluenceMap::AssignThreatAtPosition(
    const Wm3::Vec3f& position,
    const EThreatType threatType,
    const float assignedThreat,
    float assignedDecay
  )
  {
    const std::int32_t cellIndex = VectorToCoords(position);
    if (cellIndex < 0 || cellIndex >= static_cast<std::int32_t>(mMapEntries.size())) {
      return;
    }

    if (assignedDecay < 0.0f) {
      assignedDecay = 0.01f;
    }

    InfluenceGrid& cell = mMapEntries[static_cast<std::size_t>(cellIndex)];

    auto applyThreat = [&assignedThreat, &assignedDecay](float& threatLane, float& decayLane) {
      threatLane += assignedThreat;
      decayLane = threatLane * assignedDecay;
    };

    switch (threatType) {
      case THREATTYPE_Overall:
      case THREATTYPE_Unknown:
        applyThreat(cell.threat.unknownInfluence, cell.decay.unknownInfluence);
        break;
      case THREATTYPE_StructuresNotMex:
        applyThreat(cell.threat.influenceStructuresNotMex, cell.decay.influenceStructuresNotMex);
        break;
      case THREATTYPE_Structures:
        applyThreat(cell.threat.influenceStructures, cell.decay.influenceStructures);
        break;
      case THREATTYPE_Naval:
        applyThreat(cell.threat.navalInfluence, cell.decay.navalInfluence);
        break;
      case THREATTYPE_Air:
        applyThreat(cell.threat.airInfluence, cell.decay.airInfluence);
        break;
      case THREATTYPE_Land:
        applyThreat(cell.threat.landInfluence, cell.decay.landInfluence);
        break;
      case THREATTYPE_Experimental:
        applyThreat(cell.threat.experimentalInfluence, cell.decay.experimentalInfluence);
        break;
      case THREATTYPE_Commander:
        applyThreat(cell.threat.commanderInfluence, cell.decay.commanderInfluence);
        break;
      case THREATTYPE_Artillery:
        applyThreat(cell.threat.artilleryInfluence, cell.decay.artilleryInfluence);
        break;
      case THREATTYPE_AntiAir:
        applyThreat(cell.threat.antiAirInfluence, cell.decay.antiAirInfluence);
        break;
      case THREATTYPE_AntiSurface:
        applyThreat(cell.threat.antiSurfaceInfluence, cell.decay.antiSurfaceInfluence);
        break;
      case THREATTYPE_AntiSub:
        applyThreat(cell.threat.antiSubInfluence, cell.decay.antiSubInfluence);
        break;
      case THREATTYPE_Economy:
        applyThreat(cell.threat.economyInfluence, cell.decay.economyInfluence);
        break;
      default:
        break;
    }
  }

  /**
   * Address: 0x00716FC0 (FUN_00716FC0, Moho::CInfluenceMap::GetHighestThreatPosition)
   *
   * IDA signature:
   * Wm3::Vector3f *__userpurge Moho::CInfluenceMap::GetHighestThreatPosition@<eax>(
   *   Moho::CInfluenceMap *this@<eax>, Wm3::Vector3f *outPos, float *outThreat,
   *   int radius, char onMap, Moho::EThreatType threatType, int armyIndex);
   *
   * What it does:
   * Walks every cell of the influence grid, computes that cell's
   * threat value (rectangle aggregate when `radius > 0`, otherwise
   * the cell's own per-type sample), and tracks the cell with the
   * highest value. Ties are broken by squared XZ distance from this
   * army's start position (closer wins). The peak value is written
   * into `outThreat` and the chosen cell's world-space center
   * (with `y = 0`) is written into `outPosition`.
   *
   * Initial threat seed is `-200.0f` (binary's `nInf_200` constant)
   * so any positive sample wins.
   */
  Wm3::Vec3f* CInfluenceMap::GetHighestThreatPosition(
    Wm3::Vec3f* const outPosition,
    float* const outThreat,
    const int radius,
    const bool onMap,
    const EThreatType threatType,
    const int armyIndex
  )
  {
    constexpr float kInitialThreat = -200.0f;

    Wm3::Vector2f armyStart{};
    mArmy->GetArmyStartPos(armyStart);
    const float startX = armyStart.x;
    const float startZ = armyStart.y;

    float bestThreat = kInitialThreat;
    float bestDistanceSq = kInitialThreat;
    std::int32_t bestCellIndex = 0;

    const std::int32_t cellCount = static_cast<std::int32_t>(mMapEntries.size());
    for (std::int32_t cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
      InfluenceGrid& cell = mMapEntries[static_cast<std::size_t>(cellIndex)];
      const std::int32_t cellX = cellIndex % mWidth;
      const std::int32_t cellZ = cellIndex / mWidth;

      const float currentThreat = (radius != 0)
        ? GetThreatRect(cellX, cellZ, radius, onMap, threatType, armyIndex)
        : cell.GetThreat(threatType, armyIndex);

      const std::int32_t halfStep = mGridSize / 2;
      const float cellCenterX = static_cast<float>(halfStep + cellX * mGridSize);
      const float cellCenterZ = static_cast<float>(halfStep + cellZ * mGridSize);
      const float deltaX = startX - cellCenterX;
      const float deltaZ = startZ - cellCenterZ;
      const float distanceSq = deltaX * deltaX + deltaZ * deltaZ;

      if (currentThreat > bestThreat) {
        bestThreat = currentThreat;
        bestCellIndex = cellIndex;
        bestDistanceSq = distanceSq;
      } else if (currentThreat == bestThreat && distanceSq < bestDistanceSq) {
        bestThreat = currentThreat;
        bestCellIndex = cellIndex;
        bestDistanceSq = distanceSq;
      }
    }

    *outThreat = bestThreat;

    const std::int32_t halfStep = mGridSize / 2;
    const std::int32_t bestX = bestCellIndex % mWidth;
    const std::int32_t bestZ = bestCellIndex / mWidth;
    outPosition->x = static_cast<float>(halfStep + bestX * mGridSize);
    outPosition->y = 0.0f;
    outPosition->z = static_cast<float>(halfStep + bestZ * mGridSize);
    return outPosition;
  }
} // namespace moho

namespace
{
  struct CInfluenceMapDebugBootstrap
  {
    CInfluenceMapDebugBootstrap()
    {
      moho::register_imap_debug_ConAliasDef();
      moho::register_imap_debug_SimConVarDef();
      moho::register_imap_debug_grid_ConAliasDef();
      moho::func_imap_debug_grid_SimConVarDef();
      moho::register_imap_debug_path_graph_ConAliasDef();
      moho::func_imap_debug_path_graph_SimConVarDef();
      moho::register_imap_debug_grid_type_ConAliasDef();
      moho::func_imap_debug_grid_type_SimConVarDef();
      moho::register_imap_debug_grid_army_ConAliasDef();
      moho::func_imap_debug_grid_army_SimConVarDef();
    }
  };

  CInfluenceMapDebugBootstrap gCInfluenceMapDebugBootstrap;
} // namespace
