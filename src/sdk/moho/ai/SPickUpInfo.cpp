#include "moho/ai/SPickUpInfo.h"

#include <typeinfo>

#include "gpg/core/containers/ReadArchive.h"
#include "gpg/core/containers/WriteArchive.h"
#include "gpg/core/reflection/Reflection.h"

namespace
{
  [[nodiscard]] gpg::RType* ResolveWeakPtrUnitType()
  {
    gpg::RType* type = moho::WeakPtr<moho::Unit>::sType;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::WeakPtr<moho::Unit>));
      moho::WeakPtr<moho::Unit>::sType = type;
    }
    return type;
  }
} // namespace

namespace moho
{
  gpg::RType* SPickUpInfo::sType = nullptr;

  SPickUpInfo::SPickUpInfo() noexcept
    : mUnit{}
    , mDistanceSq(0.0f)
  {}

  SPickUpInfo::SPickUpInfo(Unit* const unit, const float distanceSquared) noexcept
    : SPickUpInfo()
  {
    BindUnitAndDistanceSquared(unit, distanceSquared);
  }

  SPickUpInfo::SPickUpInfo(const SPickUpInfo& source) noexcept
    : SPickUpInfo()
  {
    mUnit.ResetFromOwnerLinkSlot(source.mUnit.ownerLinkSlot);
    mDistanceSq = source.mDistanceSq;
  }

  SPickUpInfo& SPickUpInfo::operator=(const SPickUpInfo& source) noexcept
  {
    if (this == &source) {
      return *this;
    }

    if (mUnit.ownerLinkSlot != source.mUnit.ownerLinkSlot) {
      mUnit.ResetFromOwnerLinkSlot(source.mUnit.ownerLinkSlot);
    }
    mDistanceSq = source.mDistanceSq;
    return *this;
  }

  SPickUpInfo::~SPickUpInfo()
  {
    UnlinkWeakUnitLane();
  }

  /**
   * Address: 0x006246A0 (FUN_006246A0)
   *
   * What it does:
   * Binds this entry's weak-unit link from `unit` and stores the provided
   * distance-squared lane.
   */
  void SPickUpInfo::BindUnitAndDistanceSquared(Unit* const unit, const float distanceSquared) noexcept
  {
    mUnit.BindObjectUnlinked(unit);
    (void)mUnit.LinkIntoOwnerChainHeadUnlinked();
    mDistanceSq = distanceSquared;
  }

  /**
   * Address: 0x00624AA0 (FUN_00624AA0)
   *
   * What it does:
   * Unlinks this entry from the current unit weak-owner intrusive chain.
   */
  void SPickUpInfo::UnlinkWeakUnitLane() noexcept
  {
    if (mUnit.IsLinkedInOwnerChain()) {
      (void)mUnit.ReplaceInOwnerChain(mUnit.nextInOwner);
    }
  }

  /**
   * Address: 0x00627EB0 (FUN_00627EB0)
   *
   * What it does:
   * Deserializes one pickup entry by reading weak-unit lane then distance.
   */
  void SPickUpInfo::MemberDeserialize(gpg::ReadArchive* const archive)
  {
    GPG_ASSERT(archive != nullptr);
    if (!archive) {
      return;
    }

    gpg::RType* const weakUnitType = ResolveWeakPtrUnitType();
    GPG_ASSERT(weakUnitType != nullptr);

    const gpg::RRef ownerRef{};
    if (weakUnitType) {
      archive->Read(weakUnitType, &mUnit, ownerRef);
    }
    archive->ReadFloat(&mDistanceSq);
  }

  /**
   * Address: 0x00627F00 (FUN_00627F00)
   *
   * What it does:
   * Serializes one pickup entry by writing weak-unit lane then distance.
   */
  void SPickUpInfo::MemberSerialize(gpg::WriteArchive* const archive) const
  {
    GPG_ASSERT(archive != nullptr);
    if (!archive) {
      return;
    }

    gpg::RType* const weakUnitType = ResolveWeakPtrUnitType();
    GPG_ASSERT(weakUnitType != nullptr);

    const gpg::RRef ownerRef{};
    if (weakUnitType) {
      archive->Write(weakUnitType, &mUnit, ownerRef);
    }
    archive->WriteFloat(mDistanceSq);
  }
} // namespace moho
