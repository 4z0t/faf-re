#include "Vector.h"
#include <limits>
#include <new>

using namespace msvc8;

namespace msvc8::detail
{
namespace
{
struct VectorVoidStorageView
{
  void* proxy;
  void** first;
  void** last;
  void** end;
};

static_assert(sizeof(VectorVoidStorageView) == 0x10, "VectorVoidStorageView size must be 0x10");
static_assert(offsetof(VectorVoidStorageView, first) == 0x04, "VectorVoidStorageView::first offset must be 0x04");
static_assert(offsetof(VectorVoidStorageView, last) == 0x08, "VectorVoidStorageView::last offset must be 0x08");
static_assert(offsetof(VectorVoidStorageView, end) == 0x0C, "VectorVoidStorageView::end offset must be 0x0C");

[[nodiscard]] void* AllocateCheckedElementBlock(const std::uint32_t count, const std::uint32_t elementSize)
{
  if (elementSize == 0u || count > (std::numeric_limits<std::uint32_t>::max() / elementSize)) {
    throw std::bad_alloc{};
  }

  const std::size_t byteCount = static_cast<std::size_t>(count) * static_cast<std::size_t>(elementSize);
  return ::operator new(byteCount);
}
} // namespace

/**
 * Address: 0x004DC780 (FUN_004DC780)
 *
 * What it does:
 * Relocates one contiguous `void*` vector segment from `sourceBegin` to
 * `destinationBegin`, updates `_Mylast` by the copied element count, and
 * returns the destination-begin output slot.
 */
[[maybe_unused]] static void*** RelocateVectorVoidSegment(
  VectorVoidStorageView& storage,
  void*** const outDestinationBegin,
  void** const destinationBegin,
  void** const sourceBegin
) noexcept
{
  if (destinationBegin != sourceBegin) {
    const std::ptrdiff_t elementCount = storage.last - sourceBegin;
    if (elementCount > 0) {
      const std::size_t byteCount = static_cast<std::size_t>(elementCount) * sizeof(void*);
      (void)memmove_s(destinationBegin, byteCount, sourceBegin, byteCount);
    }
    storage.last = destinationBegin + elementCount;
  }

  *outDestinationBegin = destinationBegin;
  return outDestinationBegin;
}

/**
 * Address: 0x00540F40 (FUN_00540F40, func_ArraySet)
 *
 * What it does:
 * Writes one dword value from `valuePtr` into `count` consecutive destination
 * dword lanes, preserving the original null-destination guard semantics.
 */
std::uint32_t FillDwordArrayFromValuePointerNullable(
  std::uint32_t count,
  const std::uint32_t* const valuePtr,
  std::uint32_t* destination
) noexcept
{
  std::uintptr_t destinationAddress = reinterpret_cast<std::uintptr_t>(destination);
  while (count != 0u) {
    if (destinationAddress != 0u) {
      *reinterpret_cast<std::uint32_t*>(destinationAddress) = *valuePtr;
    }

    --count;
    destinationAddress += sizeof(std::uint32_t);
  }

  return count;
}

/**
 * Address: 0x0054F6B0 (FUN_0054F6B0, func_intp_memcpy)
 *
 * What it does:
 * Copies one half-open dword range `[sourceBegin, sourceEnd)` into
 * destination storage and returns one-past the last written destination lane,
 * preserving the original null-destination guard semantics.
 */
std::uint32_t* CopyDwordRangeNullable(
  std::uint32_t* destination,
  const std::uint32_t* sourceBegin,
  const std::uint32_t* sourceEnd
) noexcept
{
  const std::uint32_t* source = sourceBegin;
  std::uintptr_t destinationAddress = reinterpret_cast<std::uintptr_t>(destination);
  while (source != sourceEnd) {
    if (destinationAddress != 0u) {
      *reinterpret_cast<std::uint32_t*>(destinationAddress) = *source;
    }

    ++source;
    destinationAddress += sizeof(std::uint32_t);
  }

  return reinterpret_cast<std::uint32_t*>(destinationAddress);
}

/**
 * Address: 0x005628C0 (FUN_005628C0)
 * Address: 0x005822E0 (FUN_005822E0)
 * Address: 0x006E2CB0 (FUN_006E2CB0)
 * Address: 0x00768EB0 (FUN_00768EB0)
 *
 * What it does:
 * Allocates one checked raw block for 4-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked4ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 4u);
}

/**
 * Address: 0x006D25B0 (FUN_006D25B0)
 *
 * What it does:
 * Allocates one checked raw block for 8-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked8ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 8u);
}

/**
 * Address: 0x00628260 (FUN_00628260)
 * Address: 0x006DDD70 (FUN_006DDD70)
 *
 * What it does:
 * Allocates one checked raw block for 12-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked12ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 12u);
}

/**
 * Address: 0x005111C0 (FUN_005111C0)
 *
 * What it does:
 * Allocates one checked raw block for 16-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked16ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 16u);
}

/**
 * Address: 0x00548B80 (FUN_00548B80)
 *
 * What it does:
 * Allocates one checked raw block for 20-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked20ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 20u);
}

/**
 * Address: 0x005821F0 (FUN_005821F0)
 *
 * What it does:
 * Allocates one checked raw block for 24-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked24ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 24u);
}

/**
 * Address: 0x00693190 (FUN_00693190)
 *
 * What it does:
 * Allocates one checked raw block for 28-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked28ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 28u);
}

/**
 * Address: 0x00544610 (FUN_00544610)
 * Address: 0x005EC960 (FUN_005EC960)
 *
 * What it does:
 * Allocates one checked raw block for 32-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked32ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 32u);
}

/**
 * Address: 0x004FA650 (FUN_004FA650)
 * Address: 0x006DDC80 (FUN_006DDC80)
 * Address: 0x00704750 (FUN_00704750)
 *
 * What it does:
 * Allocates one checked raw block for 40-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked40ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 40u);
}

/**
 * Address: 0x005C9F40 (FUN_005C9F40)
 *
 * What it does:
 * Allocates one checked raw block for 52-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked52ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 52u);
}

/**
 * Address: 0x00562850 (FUN_00562850)
 *
 * What it does:
 * Allocates one checked raw block for 120-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked120ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 120u);
}

/**
 * Address: 0x00562770 (FUN_00562770)
 *
 * What it does:
 * Allocates one checked raw block for 216-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked216ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 216u);
}

/**
 * Address: 0x00562700 (FUN_00562700)
 *
 * What it does:
 * Allocates one checked raw block for 352-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked352ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 352u);
}

/**
 * Address: 0x005627E0 (FUN_005627E0)
 *
 * What it does:
 * Allocates one checked raw block for 568-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked568ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 568u);
}

/**
 * Address: 0x007419E0 (FUN_007419E0)
 *
 * What it does:
 * Allocates one checked raw block for 712-byte elements using the VC8
 * `_Allocate(count, element*)` overflow guard semantics.
 */
[[maybe_unused]] void* AllocateChecked712ByteElements(const std::uint32_t count)
{
  return AllocateCheckedElementBlock(count, 712u);
}

[[noreturn]] void ThrowVectorLengthError()
{
  throw std::length_error("vector<T> too long");
}

[[nodiscard]] bool BuyVectorStorageByElementWidth(
  VectorVoidStorageView& storage,
  const std::uint32_t count,
  const std::uint32_t elementSize
)
{
  storage.first = nullptr;
  storage.last = nullptr;
  storage.end = nullptr;

  if (elementSize == 0u || count > (std::numeric_limits<std::uint32_t>::max() / elementSize)) {
    ThrowVectorLengthError();
  }

  std::byte* const begin = (count != 0u)
    ? static_cast<std::byte*>(AllocateCheckedElementBlock(count, elementSize))
    : static_cast<std::byte*>(::operator new(0));

  const std::size_t byteCount = static_cast<std::size_t>(count) * static_cast<std::size_t>(elementSize);
  storage.first = reinterpret_cast<void**>(begin);
  storage.last = storage.first;
  storage.end = reinterpret_cast<void**>(begin + byteCount);
  return true;
}

/**
 * Address: 0x007402B0 (FUN_007402B0)
 *
 * What it does:
 * Allocates one float-vector storage lane of `count` elements, fills every
 * element with `fillValue`, and marks `_Mylast` as fully initialized.
 */
[[maybe_unused]] void FillFloatVectorStorage(
  VectorVoidStorageView& storage,
  const float fillValue,
  const std::uint32_t count
)
{
  storage.first = nullptr;
  storage.last = nullptr;
  storage.end = nullptr;

  if (count == 0u) {
    return;
  }

  if (count > (std::numeric_limits<std::uint32_t>::max() / sizeof(float))) {
    ThrowVectorLengthError();
  }

  auto* const begin = static_cast<float*>(AllocateCheckedElementBlock(count, static_cast<std::uint32_t>(sizeof(float))));
  const std::size_t byteCount = static_cast<std::size_t>(count) * sizeof(float);
  storage.first = reinterpret_cast<void**>(begin);
  storage.last = storage.first;
  storage.end = reinterpret_cast<void**>(reinterpret_cast<std::byte*>(begin) + byteCount);
  std::fill_n(begin, static_cast<std::size_t>(count), fillValue);
  storage.last = storage.end;
}

/**
 * Address: 0x00507FF0 (FUN_00507FF0)
 * Address: 0x00547BB0 (FUN_00547BB0)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 20-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage20Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 20u);
}

/**
 * Address: 0x00543480 (FUN_00543480)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 36-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage36Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 36u);
}

/**
 * Address: 0x0074D6C0 (FUN_0074D6C0)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 144-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage144Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 144u);
}

/**
 * Address: 0x00510850 (FUN_00510850)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 16-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage16Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 16u);
}

/**
 * Address: 0x005433A0 (FUN_005433A0)
 * Address: 0x005EA4E0 (FUN_005EA4E0)
 * Address: 0x0074DBA0 (FUN_0074DBA0)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 32-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage32Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 32u);
}

/**
 * Address: 0x0057EE70 (FUN_0057EE70)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 24-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage24Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 24u);
}

/**
 * Address: 0x005C5530 (FUN_005C5530)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 52-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage52Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 52u);
}

/**
 * Address: 0x00627410 (FUN_00627410)
 * Address: 0x006DBDD0 (FUN_006DBDD0)
 * Address: 0x0074D790 (FUN_0074D790)
 * Address: 0x0074D8C0 (FUN_0074D8C0)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 12-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage12Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 12u);
}

/**
 * Address: 0x006DBBB0 (FUN_006DBBB0)
 * Address: 0x00702590 (FUN_00702590)
 * Address: 0x0074DA70 (FUN_0074DA70)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 40-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage40Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 40u);
}

/**
 * Address: 0x00719950 (FUN_00719950)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 56-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage56Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 56u);
}

/**
 * Address: 0x0074D800 (FUN_0074D800)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 8-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage8Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 8u);
}

/**
 * Address: 0x00719EE0 (FUN_00719EE0)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 140-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage140Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 140u);
}

/**
 * Address: 0x007406A0 (FUN_007406A0)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 712-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage712Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 712u);
}

/**
 * Address: 0x00561300 (FUN_00561300)
 * Address: 0x005C5F10 (FUN_005C5F10)
 * Address: 0x005DC8D0 (FUN_005DC8D0)
 * Address: 0x005DCA70 (FUN_005DCA70)
 * Address: 0x0067CB00 (FUN_0067CB00)
 * Address: 0x006E2140 (FUN_006E2140)
 * Address: 0x00701F30 (FUN_00701F30)
 * Address: 0x0074D730 (FUN_0074D730)
 *
 * What it does:
 * Acquires `_Myfirst/_Mylast/_Myend` storage for one 4-byte element vector
 * lane and initializes the logical range to empty.
 */
[[maybe_unused]] bool BuyVectorStorage4Byte(VectorVoidStorageView& storage, const std::uint32_t count)
{
  return BuyVectorStorageByElementWidth(storage, count, 4u);
}

[[nodiscard]] bool BuyVectorStorageByElementWidthRequireNonZero(
  VectorVoidStorageView& storage,
  const std::uint32_t count,
  const std::uint32_t elementSize
)
{
  storage.first = nullptr;
  storage.last = nullptr;
  storage.end = nullptr;

  if (count == 0u) {
    return false;
  }

  if (elementSize == 0u || count > (std::numeric_limits<std::uint32_t>::max() / elementSize)) {
    ThrowVectorLengthError();
  }

  auto* const begin = static_cast<std::byte*>(AllocateCheckedElementBlock(count, elementSize));
  const std::size_t byteCount = static_cast<std::size_t>(count) * static_cast<std::size_t>(elementSize);
  storage.first = reinterpret_cast<void**>(begin);
  storage.last = storage.first;
  storage.end = reinterpret_cast<void**>(begin + byteCount);
  return true;
}

/**
 * Address: 0x00540270 (FUN_00540270)
 * Address: 0x00540640 (FUN_00540640)
 * Address: 0x00848B70 (FUN_00848B70)
 *
 * What it does:
 * Initializes one 4-byte vector storage lane and reports failure when
 * `count == 0`, matching the legacy callsites that treat empty input as
 * a no-op without allocation.
 */
[[maybe_unused]] bool BuyVectorStorage4ByteRequireNonZero(
  VectorVoidStorageView& storage,
  const std::uint32_t count
)
{
  return BuyVectorStorageByElementWidthRequireNonZero(storage, count, 4u);
}

/**
 * Address: 0x00741270 (FUN_00741270)
 *
 * What it does:
 * Initializes one 16-byte vector storage lane and reports failure when
 * `count == 0`, matching the legacy callsites that treat empty input as
 * a no-op without allocation.
 */
[[maybe_unused]] bool BuyVectorStorage16ByteRequireNonZero(
  VectorVoidStorageView& storage,
  const std::uint32_t count
)
{
  return BuyVectorStorageByElementWidthRequireNonZero(storage, count, 16u);
}
} // namespace msvc8::detail
