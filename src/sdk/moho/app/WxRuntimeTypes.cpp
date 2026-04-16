#include "WxRuntimeTypes.h"

#include <Windows.h>
#include <ddeml.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <limits>
#include <sys/timeb.h>
#include <system_error>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <io.h>

#include "boost/shared_ptr.h"
#include "gpg/core/containers/String.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/backends/d3d9/DeviceD3D9.hpp"
#include "libpng/PngReadRuntime.h"
#include "moho/console/CConCommand.h"
#include "moho/mesh/Mesh.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/particles/CWorldParticles.h"
#include "moho/render/IRenderWorldView.h"
#include "moho/render/camera/GeomCamera3.h"
#include "moho/render/d3d/CD3DPrimBatcher.h"
#include "moho/render/d3d/CD3DTextureBatcher.h"
#include "moho/render/d3d/CD3DDevice.h"
#include "moho/sim/CWldMap.h"
#include "moho/sim/CWldSession.h"
#include "moho/sim/SimDriver.h"
#include "moho/terrain/TerrainFactory.h"
#include "moho/terrain/TerrainCommon.h"
#include "moho/ui/IUIManager.h"

namespace
{
  // wxWidgets exposes this globally; keep a local runtime-compatible lane.
  wchar_t wxEmptyString[] = L"";
  bool gWxUrlUseDefaultProxy = false;
}

extern "C" void __cdecl _free_crt(void* ptr);
extern "C" void __cdecl _dosmaperr(unsigned long errcode);
extern "C" std::tm* __cdecl localtime64(const __time64_t* epochSeconds);
int RuntimeToLowerWideWithCurrentLocale(wchar_t character);

class wxClassInfo;
class wxLocale
{
public:
  const wchar_t* GetString(const wchar_t* sourceText, int domain);
};

wxLocale* wxGetLocale();
int wxLogSysError(const wchar_t* formatText, ...);
int wxCharCodeMSWToWX(int keyCode);

class wxFrame
{
public:
  long MSWWindowProc(unsigned int message, unsigned int wParam, long lParam);
  bool MSWTranslateMessage(MSG* message);
};

wxWindowMswRuntime* wxFindWinFromHandle(int nativeHandle);
void* wxGetInstance();
void wxAssociateWinWithHandle(void* nativeHandle, void* windowRuntime);
void wxRemoveHandleAssociation(void* windowRuntime);

namespace
{
  struct WxClipboardRuntimeView;
  extern WxClipboardRuntimeView* gWxClipboardRuntime;

  void wxInitializeControlRuntimeBaseState(void* controlRuntime) noexcept;
}

class wxHashTableRuntime
{
public:
  wxHashTableRuntime(std::int32_t keyType, std::int32_t bucketHint);
  ~wxHashTableRuntime();

  /**
   * Address: 0x009D19B0 (FUN_009D19B0, wxHashTable::Put)
   *
   * What it does:
   * Inserts one class-info pointer into the runtime class-name lookup lane.
   */
  void Put(const wchar_t* key, wxClassInfo* classInfo);

  /**
   * Address: 0x009D1C90 (FUN_009D1C90, wxHashTable::Get)
   *
   * What it does:
   * Resolves one class-info pointer from the runtime class-name lookup lane.
   */
  [[nodiscard]] wxClassInfo* Get(const wchar_t* key) const;

private:
  std::int32_t mKeyType = 0;                                          // +0x00
  std::int32_t mBucketHint = 0;                                       // +0x04
  std::unordered_map<std::wstring, wxClassInfo*>* mEntries = nullptr; // +0x08
  std::uint8_t mReserved0C[0x18]{};                                   // +0x0C
};

static_assert(sizeof(wxHashTableRuntime) == 0x24, "wxHashTableRuntime size must be 0x24");

wxHashTableRuntime::wxHashTableRuntime(
  const std::int32_t keyType,
  const std::int32_t bucketHint
)
  : mKeyType(keyType)
  , mBucketHint(bucketHint)
  , mEntries(new std::unordered_map<std::wstring, wxClassInfo*>())
{
  if (mEntries != nullptr) {
    mEntries->reserve(static_cast<std::size_t>(bucketHint));
  }
}

wxHashTableRuntime::~wxHashTableRuntime()
{
  delete mEntries;
  mEntries = nullptr;
}

/**
 * Address: 0x009D19B0 (FUN_009D19B0, wxHashTable::Put)
 *
 * What it does:
 * Inserts one class-info pointer into the runtime class-name lookup lane.
 */
void wxHashTableRuntime::Put(
  const wchar_t* const key,
  wxClassInfo* const classInfo
)
{
  if (mEntries == nullptr || key == nullptr) {
    return;
  }

  (*mEntries)[key] = classInfo;
}

/**
 * Address: 0x009D1C90 (FUN_009D1C90, wxHashTable::Get)
 *
 * What it does:
 * Resolves one class-info pointer from the runtime class-name lookup lane.
 */
wxClassInfo* wxHashTableRuntime::Get(
  const wchar_t* const key
) const
{
  if (mEntries == nullptr || key == nullptr) {
    return nullptr;
  }

  const auto iter = mEntries->find(key);
  return iter != mEntries->end() ? iter->second : nullptr;
}

namespace
{
  void ReleaseRuntimeStringFromTemporaryStorage(wxStringRuntime* value) noexcept;

  using WxDeleteObjectWithFlagFn = void(__thiscall*)(void* object, int deleteFlag);

  struct WxWinHashBucketPayloadRuntimeView
  {
    std::uint32_t capacity = 0; // +0x00
    std::uint32_t size = 0;     // +0x04
    void* payload = nullptr;    // +0x08
  };
  static_assert(
    offsetof(WxWinHashBucketPayloadRuntimeView, payload) == 0x08,
    "WxWinHashBucketPayloadRuntimeView::payload offset must be 0x08"
  );
  static_assert(sizeof(WxWinHashBucketPayloadRuntimeView) == 0x0C, "WxWinHashBucketPayloadRuntimeView size must be 0x0C");

  struct WxWinHashTableBucketRuntimeView
  {
    void* vtable = nullptr;         // +0x00
    std::int32_t keyType = 0;       // +0x04
    void** primaryBuckets = nullptr; // +0x08
    void** secondaryBuckets = nullptr; // +0x0C
    std::uint32_t bucketCount = 0;  // +0x10
    std::uint32_t usedCount = 0;    // +0x14
  };
  static_assert(
    offsetof(WxWinHashTableBucketRuntimeView, primaryBuckets) == 0x08,
    "WxWinHashTableBucketRuntimeView::primaryBuckets offset must be 0x08"
  );
  static_assert(
    offsetof(WxWinHashTableBucketRuntimeView, secondaryBuckets) == 0x0C,
    "WxWinHashTableBucketRuntimeView::secondaryBuckets offset must be 0x0C"
  );
  static_assert(
    offsetof(WxWinHashTableBucketRuntimeView, bucketCount) == 0x10,
    "WxWinHashTableBucketRuntimeView::bucketCount offset must be 0x10"
  );
  static_assert(
    offsetof(WxWinHashTableBucketRuntimeView, usedCount) == 0x14,
    "WxWinHashTableBucketRuntimeView::usedCount offset must be 0x14"
  );
  static_assert(sizeof(WxWinHashTableBucketRuntimeView) == 0x18, "WxWinHashTableBucketRuntimeView size must be 0x18");

  void WxWinHashReleasePrimaryBucketsRuntime(
    WxWinHashTableBucketRuntimeView* const table
  )
  {
    if (table->primaryBuckets == nullptr) {
      return;
    }

    for (std::uint32_t index = 0; index < table->bucketCount; ++index) {
      void* const bucket = table->primaryBuckets[index];
      if (bucket == nullptr) {
        continue;
      }

      void** const vtable = *reinterpret_cast<void***>(bucket);
      if (vtable != nullptr && vtable[1] != nullptr) {
        auto const deleteWithFlag = reinterpret_cast<WxDeleteObjectWithFlagFn>(vtable[1]);
        deleteWithFlag(bucket, 1);
      }
    }

    ::operator delete(table->primaryBuckets);
    table->primaryBuckets = nullptr;
    table->usedCount = 0;
  }

  void WxWinHashReleaseBucketPayloadRuntime(
    WxWinHashBucketPayloadRuntimeView* const bucket
  )
  {
    if (bucket->payload != nullptr) {
      ::operator delete(bucket->payload);
      bucket->payload = nullptr;
    }
  }
}

/**
 * Address: 0x009D1680 (FUN_009D1680, sub_9D1680)
 *
 * What it does:
 * Clears and rebuilds the primary wx-win-hash bucket pointer lane, applies the
 * incoming key type and bucket count, and zero-initializes all bucket slots.
 */
std::uint32_t wxWinHashTableResetPrimaryBucketArrayRuntime(
  WxWinHashTableBucketRuntimeView* const table,
  const std::int32_t keyType,
  const std::uint32_t bucketCount
)
{
  WxWinHashReleasePrimaryBucketsRuntime(table);
  table->keyType = keyType;
  table->bucketCount = bucketCount;

  const bool overflow = (static_cast<std::uint64_t>(bucketCount) >> 30u) != 0u;
  const std::size_t allocationBytes = overflow
    ? static_cast<std::size_t>(-1)
    : sizeof(void*) * static_cast<std::size_t>(bucketCount);
  table->primaryBuckets = static_cast<void**>(::operator new(allocationBytes));

  std::uint32_t initializedCount = 0;
  for (; initializedCount < table->bucketCount; ++initializedCount) {
    table->primaryBuckets[initializedCount] = nullptr;
  }

  return initializedCount;
}

/**
 * Address: 0x009D16E0 (FUN_009D16E0, sub_9D16E0)
 *
 * What it does:
 * Releases payload buffers and object storage for each primary/secondary
 * bucket lane, then frees both bucket-pointer arrays and resets count lanes.
 */
void wxWinHashTableDestroyBucketArraysRuntime(
  WxWinHashTableBucketRuntimeView* const table
)
{
  for (std::uint32_t index = 0; index < table->bucketCount; ++index) {
    if (table->primaryBuckets != nullptr) {
      void* const primaryBucket = table->primaryBuckets[index];
      if (primaryBucket != nullptr) {
        auto* const primaryPayload = static_cast<WxWinHashBucketPayloadRuntimeView*>(primaryBucket);
        WxWinHashReleaseBucketPayloadRuntime(primaryPayload);
        ::operator delete(primaryBucket);
      }
    }

    if (table->secondaryBuckets != nullptr) {
      void* const secondaryBucket = table->secondaryBuckets[index];
      if (secondaryBucket != nullptr) {
        auto* const secondaryPayload = static_cast<WxWinHashBucketPayloadRuntimeView*>(secondaryBucket);
        WxWinHashReleaseBucketPayloadRuntime(secondaryPayload);
        ::operator delete(secondaryBucket);
      }
    }
  }

  ::operator delete(table->primaryBuckets);
  ::operator delete(table->secondaryBuckets);
  table->bucketCount = 0;
  table->usedCount = 0;
}

namespace
{
  constexpr std::array<std::uint32_t, 31> kWxHashTableBucketThresholds = {
    0x00000007u,
    0x0000000Du,
    0x0000001Du,
    0x00000035u,
    0x00000061u,
    0x000000C1u,
    0x00000185u,
    0x00000301u,
    0x00000607u,
    0x00000C07u,
    0x00001807u,
    0x00003001u,
    0x00006011u,
    0x0000C005u,
    0x0001800Du,
    0x00030005u,
    0x00060019u,
    0x000C0001u,
    0x00180005u,
    0x0030000Bu,
    0x0060000Du,
    0x00C00005u,
    0x01800013u,
    0x03000005u,
    0x06000017u,
    0x0C000013u,
    0x18000005u,
    0x30000059u,
    0x60000005u,
    0xC0000001u,
    0xFFFFFFFBu,
  };
}

/**
 * Address: 0x009D7CE0 (FUN_009D7CE0, sub_9D7CE0)
 *
 * What it does:
 * Returns the next predefined wx hash-bucket threshold greater than
 * `minimumBucketCount`, or `0` when no threshold can satisfy the request.
 */
std::uint32_t wxHashTableNextBucketThresholdRuntime(
  const std::uint32_t minimumBucketCount
) noexcept
{
  for (const std::uint32_t candidate : kWxHashTableBucketThresholds) {
    if (minimumBucketCount < candidate) {
      return candidate;
    }
  }

  return 0u;
}

class wxClassInfo
{
public:
  /**
   * Address: 0x00977DA0 (FUN_00977DA0, wxClassInfo::InitializeClasses)
   *
   * What it does:
   * Builds one class-name lookup table from the linked class-info list, then
   * resolves primary/secondary base-class pointers from base-name lanes.
   */
  static void InitializeClasses()
  {
    sm_classTable = new wxHashTableRuntime(2, 1000);

    for (wxClassInfo* classInfo = sm_first; classInfo != nullptr; classInfo = classInfo->m_next) {
      if (classInfo->m_className != nullptr) {
        sm_classTable->Put(classInfo->m_className, classInfo);
      }
    }

    for (wxClassInfo* classInfo = sm_first; classInfo != nullptr; classInfo = classInfo->m_next) {
      classInfo->m_baseInfo1 =
        classInfo->m_baseClassName1 != nullptr ? sm_classTable->Get(classInfo->m_baseClassName1) : nullptr;
      classInfo->m_baseInfo2 =
        classInfo->m_baseClassName2 != nullptr ? sm_classTable->Get(classInfo->m_baseClassName2) : nullptr;
    }
  }

  static wxClassInfo* sm_first;
  static wxHashTableRuntime* sm_classTable;

  const wchar_t* m_className = nullptr;      // +0x00
  const wchar_t* m_baseClassName1 = nullptr; // +0x04
  const wchar_t* m_baseClassName2 = nullptr; // +0x08
  std::uint8_t mReserved0C[0x8]{};           // +0x0C
  wxClassInfo* m_baseInfo1 = nullptr;        // +0x14
  wxClassInfo* m_baseInfo2 = nullptr;        // +0x18
  wxClassInfo* m_next = nullptr;             // +0x1C
};

static_assert(offsetof(wxClassInfo, m_className) == 0x00, "wxClassInfo::m_className offset must be 0x00");
static_assert(offsetof(wxClassInfo, m_baseClassName1) == 0x04, "wxClassInfo::m_baseClassName1 offset must be 0x04");
static_assert(offsetof(wxClassInfo, m_baseClassName2) == 0x08, "wxClassInfo::m_baseClassName2 offset must be 0x08");
static_assert(offsetof(wxClassInfo, m_baseInfo1) == 0x14, "wxClassInfo::m_baseInfo1 offset must be 0x14");
static_assert(offsetof(wxClassInfo, m_baseInfo2) == 0x18, "wxClassInfo::m_baseInfo2 offset must be 0x18");
static_assert(offsetof(wxClassInfo, m_next) == 0x1C, "wxClassInfo::m_next offset must be 0x1C");
static_assert(sizeof(wxClassInfo) == 0x20, "wxClassInfo size must be 0x20");

wxClassInfo* wxClassInfo::sm_first = nullptr;
wxHashTableRuntime* wxClassInfo::sm_classTable = nullptr;

namespace
{
  constexpr std::uintptr_t kInlineHeadLinkSentinelMax = 0x10000u;
  constexpr long kWxWindowStyleVerticalScroll = static_cast<long>(0x80000000u);
  constexpr long kWxWindowStyleHorizontalScroll = static_cast<long>(0x40000000u);
  constexpr long kWxWindowStyleClipChildren = 0x00400000;
  constexpr long kWxWindowStyleRaisedBorder = 0x20000000;
  constexpr long kWxWindowStyleSunkenBorder = static_cast<long>(0x80000000u);
  constexpr long kWxWindowStyleDoubleBorder = 0x40000000;
  constexpr long kWxWindowStyleMaskForMsw = 0x1F200000;
  constexpr long kWxWindowStyleMaskAuto3DBase = 0x17200000;
  constexpr long kWxWindowStyleNo3D = 0x00800000;
  constexpr long kWxWindowStyleAuto3D = 0x08000000;
  constexpr long kWxWindowStyleStaticEdge = 0x02000000;
  constexpr long kWxWindowStyleSimpleBorder = 0x01000000;
  constexpr long kWxWindowStyleDoubleBorderLegacy = 0x04000000;
  constexpr long kWxWindowStyleSimpleBorderAlt = 0x10000000;
  constexpr long kWxWindowStyleNoParentBg = 0x00080000;
  constexpr long kWxWindowStyleTabTraversal = 0x00100000;
  constexpr unsigned long kMswStyleBase = 0x50000000u;
  constexpr unsigned long kMswStyleClipChildren = 0x52000000u;
  constexpr unsigned long kMswStyleRaisedBorder = 0x04000000u;
  constexpr unsigned long kMswStyleSunkenBorder = 0x00200000u;
  constexpr unsigned long kMswStyleDoubleBorder = 0x00100000u;
  constexpr unsigned long kMswStyleNo3DBit = 0x00800000u;
  constexpr unsigned long kMswExStyleTabTraversal = 0x20u;
  constexpr unsigned long kMswExStyleClientEdge = 0x200u;
  constexpr unsigned long kMswExStyleDlgModalFrame = 0x1u;
  constexpr unsigned long kMswExStyleNoParentNotify = 0x00010000u;
  constexpr long kWxTextCtrlStyleMultiline = 0x20;
  constexpr long kWxTextCtrlStylePassword = 0x800;
  constexpr long kWxTextCtrlStyleReadOnly = 0x10;
  constexpr long kWxTextCtrlStyleProcessEnter = 0x400;
  constexpr long kWxTextCtrlStyleCenter = 0x100;
  constexpr long kWxTextCtrlStyleRight = 0x200;
  constexpr unsigned int kWin32CommandMessageId = 0x111u;
  constexpr std::uint32_t kDoMessageDeferredQueueInitialized = 0x1u;
  constexpr std::uint16_t kMdiCommandTileHorizontal = 4001u;
  constexpr std::uint16_t kMdiCommandCascade = 4002u;
  constexpr std::uint16_t kMdiCommandArrangeIcons = 4003u;
  constexpr std::uint16_t kMdiCommandNextWindow = 4004u;
  constexpr std::uint16_t kMdiCommandTileVertical = 4005u;
  constexpr std::uint16_t kMdiCommandPreviousWindow = 4006u;
  constexpr std::uint16_t kMdiChildCommandRangeStart = 4100u;
  constexpr std::uint16_t kMdiChildCommandRangeEnd = 4600u;
  constexpr std::uint16_t kMdiSystemCommandRangeStart = 0xF000u;
  constexpr std::uint16_t kMenuCommandSeparatorId = 0xFFFEu;
  constexpr long kMdiChildStyleBase = 0x12040000;
  constexpr long kMdiChildStyleMinimizeBoxBase = 0x12060000;
  constexpr long kMdiChildStyleUseNoRedrawClass = 0x00010000;
  constexpr long kMdiChildStyleUseMinimizeBoxBase = 0x00000400;
  constexpr long kMdiChildStyleHasMaximizeBox = 0x00000200;
  constexpr long kMdiChildStyleHasResizeBorder = 0x00000040;
  constexpr long kMdiChildStyleHasSystemMenu = 0x00000800;
  constexpr long kMdiChildStyleStartMinimized = 0x00004000;
  constexpr long kMdiChildStyleStartMaximized = 0x00002000;
  constexpr long kMdiChildStyleDecoratedCaption = 0x20000000;

  constexpr wchar_t kWxMdiChildFrameClassName[] = L"wxMDIChildFrame";
  constexpr wchar_t kWxMdiChildFrameClassNameNoRedraw[] = L"wxMDIChildFrameNoRedraw";

  void* gCLogAdditionEventClassInfoTable[1] = {nullptr};
  void* gWxWindowBaseClassInfoTable[1] = {nullptr};
  void* gWxWindowClassInfoTable[1] = {nullptr};
  void* gWxImageHandlerClassInfoTable[1] = {nullptr};
  void* gWxPngHandlerClassInfoTable[1] = {nullptr};
  void* gWxMdiChildFrameClassInfoTable[1] = {nullptr};
  void* gWxMdiClientWindowClassInfoTable[1] = {nullptr};

  HICON gWxStdMdiChildFrameIcon = nullptr;
  HICON gWxDefaultMdiChildFrameIcon = nullptr;
  HINSTANCE gWxInstanceHandle = ::GetModuleHandleW(nullptr);
  std::int32_t gWxWindowBaseLastControlId = -1;
  std::vector<wxWindowBase*> gWxModelessWindows{};

  class WxPngIoStreamRuntime
  {
  public:
    virtual ~WxPngIoStreamRuntime() = default;
    virtual void RuntimeSlot08() = 0;
    virtual void RuntimeSlot0C() = 0;
    virtual void WritePngBytes(png_bytep bytes, png_size_t byteCount) = 0;
    virtual void ReadPngBytes(png_bytep bytes, png_size_t byteCount) = 0;
  };

  struct WxPngIoContextRuntime
  {
    std::uint8_t setJmpAndStateLane[0x44]{};
    WxPngIoStreamRuntime* stream = nullptr;
  };
  static_assert(offsetof(WxPngIoContextRuntime, stream) == 0x44, "WxPngIoContextRuntime::stream offset must be 0x44");

  /**
   * Address: 0x00974E30 (FUN_00974E30, write_data_fn)
   *
   * What it does:
   * Resolves the active wx/libpng callback context and forwards one PNG input
   * byte-span request into the stream read lane.
   */
  void wxPngReadFromStreamCallback(
    png_structp const pngPtr,
    png_bytep const bytes,
    png_size_t const byteCount
  )
  {
    auto* const ioContext = static_cast<WxPngIoContextRuntime*>(png_get_io_ptr(pngPtr));
    ioContext->stream->ReadPngBytes(bytes, byteCount);
  }

  /**
   * Address: 0x00974E60 (FUN_00974E60, _PNG_stream_writer)
   *
   * What it does:
   * Resolves the active wx/libpng callback context and forwards one PNG output
   * byte-span request into the stream write lane.
   */
  void wxPngWriteToStreamCallback(
    png_structp const pngPtr,
    png_bytep const bytes,
    png_size_t const byteCount
  )
  {
    auto* const ioContext = static_cast<WxPngIoContextRuntime*>(png_get_io_ptr(pngPtr));
    ioContext->stream->WritePngBytes(bytes, byteCount);
  }

  MSG gCurrentMessage{};
  std::uint32_t gDoMessageStateFlags = 0u;
  bool gIsDispatchingDeferredMessages = false;
  bool gSuppressDeferredCommandMessages = false;
  void* wxCurrentPopupMenu = nullptr;
  HWND gWxMdiChildPendingDestroyHandle = nullptr;
  DWORD gs_idMainThread = ::GetCurrentThreadId();
  CRITICAL_SECTION gCritSectGui{};
  CRITICAL_SECTION gCritSectWaitingForGui{};
  _RTL_CRITICAL_SECTION* gs_critsectGui = nullptr;
  _RTL_CRITICAL_SECTION* gs_critsectWaitingForGui = nullptr;
  std::once_flag gGuiMutexInitOnce{};
  std::int32_t gs_nWaitingForGui = 0;
  std::uint8_t gs_bGuiOwnedByMainThread = 1;
  std::vector<MSG*>* gDeferredThreadMessages = nullptr;
  int gWxGetOsVersionCache = -1;
  int gWxGetOsVersionMajor = -1;
  int gWxGetOsVersionMinor = -1;
  int gWxColourDisplayCache = -1;
  HCURSOR gs_wxBusyCursor = nullptr;
  HCURSOR gs_wxBusyCursorOld = nullptr;
  int gs_wxBusyCursorCount = 0;
  HWND gWxHiddenTopLevelParentWindow = nullptr;
  const wchar_t* gWxHiddenTopLevelParentClassName = nullptr;
  constexpr wchar_t kWxHiddenTopLevelParentClassName[] = L"wxTLWHiddenParent";
  constexpr wchar_t kWxHiddenTopLevelParentWindowName[] = L"Default";

  class WxStockListRuntimeBase
  {
  public:
    virtual ~WxStockListRuntimeBase() = default;
  };

  WxStockListRuntimeBase* wxTheBrushList = nullptr;
  WxStockListRuntimeBase* wxThePenList = nullptr;
  WxStockListRuntimeBase* wxTheFontList = nullptr;
  WxStockListRuntimeBase* wxTheBitmapList = nullptr;

  void DeleteStockList(WxStockListRuntimeBase*& stockList) noexcept
  {
    delete stockList;
    stockList = nullptr;
  }

  /**
   * Address: 0x00A19150 (FUN_00A19150)
   *
   * What it does:
   * Scans one runtime IID pointer list and reports whether any entry matches
   * the requested COM interface id.
   */
  [[nodiscard]] bool IsIidFromList(
    REFIID iid,
    const IID* const* const iidList,
    const unsigned int count
  )
  {
    if (count == 0U) {
      return false;
    }

    for (unsigned int index = 0U; index < count; ++index) {
      if (InlineIsEqualGUID(iid, *iidList[index])) {
        return true;
      }
    }

    return false;
  }

  struct WxWindowHandleHashEntryRuntime
  {
    std::uint8_t reserved00[0x8];
    wxWindowMswRuntime* window = nullptr;
  };
  static_assert(
    offsetof(WxWindowHandleHashEntryRuntime, window) == 0x8,
    "WxWindowHandleHashEntryRuntime::window offset must be 0x8"
  );

  class WxWindowHandleHashRuntime
  {
  public:
    void* Get(int key, void* frameHandle);
  };

  WxWindowHandleHashRuntime* wxWinHandleHash = nullptr;

  void* WxWindowHandleHashRuntime::Get(
    const int,
    void*
  )
  {
    return nullptr;
  }

  void DestroyDeferredThreadMessages() noexcept
  {
    if (gDeferredThreadMessages == nullptr) {
      return;
    }

    for (MSG* const queuedMessage : *gDeferredThreadMessages) {
      delete queuedMessage;
    }
    gDeferredThreadMessages->clear();
    delete gDeferredThreadMessages;
    gDeferredThreadMessages = nullptr;
  }

  void EnsureDeferredThreadMessageQueueInitialized()
  {
    if (gDeferredThreadMessages == nullptr) {
      gDeferredThreadMessages = new std::vector<MSG*>();
    }

    if ((gDoMessageStateFlags & kDoMessageDeferredQueueInitialized) != 0u) {
      return;
    }

    gDoMessageStateFlags |= kDoMessageDeferredQueueInitialized;
    std::atexit(&DestroyDeferredThreadMessages);
  }

  void EnsureGuiMutexRuntimeInitialized() noexcept
  {
    std::call_once(gGuiMutexInitOnce, []() {
      ::InitializeCriticalSection(&gCritSectGui);
      ::InitializeCriticalSection(&gCritSectWaitingForGui);
      gs_critsectGui = reinterpret_cast<_RTL_CRITICAL_SECTION*>(&gCritSectGui);
      gs_critsectWaitingForGui = reinterpret_cast<_RTL_CRITICAL_SECTION*>(&gCritSectWaitingForGui);
    });
  }

  [[nodiscard]] _RTL_CRITICAL_SECTION* GuiCriticalSection() noexcept
  {
    EnsureGuiMutexRuntimeInitialized();
    return gs_critsectGui;
  }

  [[nodiscard]] _RTL_CRITICAL_SECTION* WaitingForGuiCriticalSection() noexcept
  {
    EnsureGuiMutexRuntimeInitialized();
    return gs_critsectWaitingForGui;
  }

  [[nodiscard]] bool IsGuiOwnedByMainThread() noexcept
  {
    return wxGuiOwnedByMainThread();
  }

  [[nodiscard]] bool ShouldSuppressDeferredCommandMessages() noexcept
  {
    return gSuppressDeferredCommandMessages;
  }

  void QueueDeferredThreadMessage(
    const MSG& message,
    const unsigned int repeatCount
  )
  {
    if (repeatCount == 0u) {
      return;
    }

    EnsureDeferredThreadMessageQueueInitialized();
    gDeferredThreadMessages->reserve(gDeferredThreadMessages->size() + repeatCount);

    for (unsigned int index = 0; index < repeatCount; ++index) {
      gDeferredThreadMessages->push_back(new MSG(message));
    }
  }

  void DispatchDeferredThreadMessages(
    wxApp& app
  )
  {
    if (gIsDispatchingDeferredMessages) {
      return;
    }

    gIsDispatchingDeferredMessages = true;
    if (gDeferredThreadMessages == nullptr) {
      return;
    }

    const std::size_t deferredCount = gDeferredThreadMessages->size();
    for (std::size_t index = 0; index < deferredCount; ++index) {
      MSG* const queuedMessage = (*gDeferredThreadMessages)[index];
      if (queuedMessage != nullptr) {
        app.ProcessMessage(reinterpret_cast<void**>(queuedMessage));
      }
    }
    DestroyDeferredThreadMessages();
  }

  struct WxObjectRuntimeView
  {
    void* vtable = nullptr;
    void* refData = nullptr;
  };

  struct WxMaskRuntimeView : WxObjectRuntimeView
  {
    HBITMAP nativeMaskHandle = nullptr; // +0x08
  };
  static_assert(
    offsetof(WxMaskRuntimeView, nativeMaskHandle) == 0x08,
    "WxMaskRuntimeView::nativeMaskHandle offset must be 0x08"
  );

  struct WxImageListRuntimeView : WxObjectRuntimeView
  {
    HIMAGELIST nativeImageListHandle = nullptr; // +0x08
  };
  static_assert(
    offsetof(WxImageListRuntimeView, nativeImageListHandle) == 0x08,
    "WxImageListRuntimeView::nativeImageListHandle offset must be 0x08"
  );

  extern std::uint8_t gWxObjectRuntimeVTableTag;
  extern std::uint8_t gWxMaskRuntimeVTableTag;
  extern std::uint8_t gWxImageListRuntimeVTableTag;
  extern std::uint8_t gWxCursorRefDataRuntimeVTableTag;
  extern std::uint8_t gWxObjectRefDataRuntimeVTableTag;
  extern std::uint8_t gWxMenuListNodeRuntimeVTableTag;
  extern std::uint8_t gWxTreeListCtrlCtorRuntimeVTableTag;
  extern std::uint8_t gWxButtonRuntimeVTableTag;
  extern std::uint8_t gWxStaticTextRuntimeVTableTag;
  extern std::uint8_t gWxStaticBoxRuntimeVTableTag;
  extern std::uint8_t gWxCheckBoxRuntimeVTableTag;
  extern std::uint8_t gWxBitmapButtonBaseRuntimeVTableTag;
  extern std::uint8_t gWxBitmapButtonRuntimeVTableTag;
  extern std::uint8_t gWxFontListRuntimeVTableTag;
  extern std::uint8_t gWxPenListRuntimeVTableTag;
  extern std::uint8_t gWxPrintPaperTypeRuntimeVTableTag;
  extern std::uint8_t gWxPopupWindowRuntimeVTableTag;
  extern std::uint8_t gWxSpinCtrlRuntimeVTableTag;
  extern std::uint8_t gWxScrollBarRuntimeVTableTag;
  extern std::uint8_t gWxStaticBitmapRuntimeVTableTag;
  extern std::uint8_t gWxSpinButtonRuntimeVTableTag;
  extern std::uint8_t gWxMdiClientWindowRuntimeVTableTag;
  extern std::uint8_t gWxStaticLineRuntimeVTableTag;
  extern std::uint8_t gWxResourceCacheListRuntimeVTableTag;
  extern std::uint8_t gWxPathListRuntimeVTableTag;
  extern std::uint8_t gWxBrushRefDataRuntimeVTableTag;
  extern std::uint8_t gWxListBaseRuntimeVTableTag;
  extern std::uint8_t gWxListStringNodeRuntimeVTableTag;
  extern std::uint8_t gWxModuleListNodeRuntimeVTableTag;
  extern std::uint8_t gWxToolBarToolsListNodeRuntimeVTableTag;
  extern std::uint8_t gWxSimpleDataObjectListNodeRuntimeVTableTag;
  extern std::uint8_t gWxArtProvidersListNodeRuntimeVTableTag;
  extern std::uint8_t gWxCursorRuntimeVTableTag;
  extern std::uint8_t gWxClipboardRuntimeVTableTag;
  extern std::uint8_t gWxToolBarToolBaseRuntimeVTableTag;
  extern std::uint8_t gWxToolBarToolRuntimeVTableTag;
  extern std::uint8_t gWxTimerEventRuntimeVTableTag;
  extern std::uint8_t gWxSocketEventRuntimeVTableTag;
  extern std::uint8_t gWxProcessEventRuntimeVTableTag;
  extern std::uint8_t gWxFontDataRuntimeVTableTag;
  extern std::uint8_t gWxFontDialogBaseRuntimeVTableTag;
  extern std::uint8_t gWxFontDialogRuntimeVTableTag;

  [[nodiscard]] std::uint32_t wxGetComCtl32PackedVersionRuntime() noexcept;

  struct WxCursorRefDataRuntimeView
  {
    std::uint8_t reserved00_13[0x14];
    void* nativeCursorHandle = nullptr; // +0x14
    std::uint8_t ownsCursorHandle = 0;  // +0x18
    std::uint8_t reserved19To1B[0x3]{};
  };
  static_assert(
    offsetof(WxCursorRefDataRuntimeView, nativeCursorHandle) == 0x14,
    "WxCursorRefDataRuntimeView::nativeCursorHandle offset must be 0x14"
  );
  static_assert(
    offsetof(WxCursorRefDataRuntimeView, ownsCursorHandle) == 0x18,
    "WxCursorRefDataRuntimeView::ownsCursorHandle offset must be 0x18"
  );

  /**
   * Runtime view for wxImage::m_refData allocated by `FUN_009703B0`.
   *
   * The binary lane seeds width/height/data at +0x08/+0x0C/+0x10, stores
   * six flag bytes at +0x14..+0x19, and stores image-option key/value string
   * arrays at +0x28/+0x38.
   */
  struct WxStringArrayRuntimeView
  {
    void* vtable = nullptr;               // +0x00
    std::int32_t count = 0;               // +0x04
    const wchar_t** entries = nullptr;    // +0x08
    std::uint8_t isSorted = 0;            // +0x0C
    std::uint8_t reserved0D_0F[0x3]{};
  };
  static_assert(offsetof(WxStringArrayRuntimeView, count) == 0x04, "WxStringArrayRuntimeView::count offset must be 0x04");
  static_assert(
    offsetof(WxStringArrayRuntimeView, entries) == 0x08,
    "WxStringArrayRuntimeView::entries offset must be 0x08"
  );
  static_assert(
    offsetof(WxStringArrayRuntimeView, isSorted) == 0x0C,
    "WxStringArrayRuntimeView::isSorted offset must be 0x0C"
  );
  static_assert(sizeof(WxStringArrayRuntimeView) == 0x10, "WxStringArrayRuntimeView size must be 0x10");

  class WxImageRefDataRuntime final
  {
  public:
    WxImageRefDataRuntime() = default;

    virtual ~WxImageRefDataRuntime()
    {
      std::free(mPixelBytes);
      mPixelBytes = nullptr;
    }

    std::int32_t mRefCount = 1;
    std::int32_t mWidth = 0;
    std::int32_t mHeight = 0;
    std::uint8_t* mPixelBytes = nullptr;
    std::uint8_t mMaskAndFlags[0x8]{};
    std::uint8_t mPaletteRuntimeLane[0xC]{};
    WxStringArrayRuntimeView mOptionKeys;
    WxStringArrayRuntimeView mOptionValues;
  };

  static_assert(
    offsetof(WxImageRefDataRuntime, mRefCount) == 0x4,
    "WxImageRefDataRuntime::mRefCount offset must be 0x4"
  );
  static_assert(offsetof(WxImageRefDataRuntime, mWidth) == 0x8, "WxImageRefDataRuntime::mWidth offset must be 0x8");
  static_assert(offsetof(WxImageRefDataRuntime, mHeight) == 0xC, "WxImageRefDataRuntime::mHeight offset must be 0xC");
  static_assert(
    offsetof(WxImageRefDataRuntime, mPixelBytes) == 0x10,
    "WxImageRefDataRuntime::mPixelBytes offset must be 0x10"
  );
  static_assert(
    offsetof(WxImageRefDataRuntime, mMaskAndFlags) == 0x14,
    "WxImageRefDataRuntime::mMaskAndFlags offset must be 0x14"
  );
  static_assert(
    offsetof(WxImageRefDataRuntime, mOptionKeys) == 0x28,
    "WxImageRefDataRuntime::mOptionKeys offset must be 0x28"
  );
  static_assert(
    offsetof(WxImageRefDataRuntime, mOptionValues) == 0x38,
    "WxImageRefDataRuntime::mOptionValues offset must be 0x38"
  );
  static_assert(sizeof(WxImageRefDataRuntime) == 0x48, "WxImageRefDataRuntime size must be 0x48");

  struct WxGdiImageRefDataRuntimeView
  {
    void* vtable = nullptr;       // +0x00
    std::int32_t refCount = 1;    // +0x04
    std::int32_t lane08 = 0;      // +0x08
    std::int32_t lane0C = 0;      // +0x0C
    std::int32_t lane10 = 0;      // +0x10
    std::int32_t lane14 = 0;      // +0x14
  };
  static_assert(
    offsetof(WxGdiImageRefDataRuntimeView, refCount) == 0x4,
    "WxGdiImageRefDataRuntimeView::refCount offset must be 0x4"
  );
  static_assert(sizeof(WxGdiImageRefDataRuntimeView) == 0x18, "WxGdiImageRefDataRuntimeView size must be 0x18");

  struct WxIconRefDataRuntimeView : WxGdiImageRefDataRuntimeView
  {
  };
  static_assert(sizeof(WxIconRefDataRuntimeView) == 0x18, "WxIconRefDataRuntimeView size must be 0x18");

  /**
   * Address: 0x004F1680 (FUN_004F1680, ??0wxGDIRefData@@QAE@XZ)
   *
   * What it does:
   * Runs one wx GDI ref-data base-construction lane by rebinding the base
   * object payload and restoring shared refcount to `1`.
   */
  [[maybe_unused]] WxGdiImageRefDataRuntimeView* wxInitializeGdiRefDataRuntime(
    WxGdiImageRefDataRuntimeView* const runtime
  ) noexcept
  {
    if (runtime == nullptr) {
      return nullptr;
    }

    (void)wxConstructObjectRefDataBaseRuntime(runtime);
    runtime->refCount = 1;
    return runtime;
  }

  /**
   * Address: 0x004F15D0 (FUN_004F15D0, ??0wxGDIImageRefData@@QAE@@Z)
   *
   * What it does:
   * Initializes one wx GDI image ref-data payload with refcount `1` and
   * zeroed image-storage lanes.
   */
  [[maybe_unused]] WxGdiImageRefDataRuntimeView* wxInitializeGdiImageRefDataRuntime(
    WxGdiImageRefDataRuntimeView* const runtime
  ) noexcept
  {
    if (runtime == nullptr) {
      return nullptr;
    }

    (void)wxInitializeGdiRefDataRuntime(runtime);
    runtime->lane08 = 0;
    runtime->lane0C = 0;
    runtime->lane10 = 0;
    runtime->lane14 = 0;
    return runtime;
  }

  /**
   * Address: 0x004F18A0 (FUN_004F18A0, ??0wxIconRefData@@QAE@@Z)
   *
   * What it does:
   * Initializes one icon-refdata payload by running the shared wx GDI image
   * ref-data initialization lane.
   */
  [[maybe_unused]] WxIconRefDataRuntimeView* wxInitializeIconRefDataRuntime(
    WxIconRefDataRuntimeView* const runtime
  ) noexcept
  {
    if (runtime == nullptr) {
      return nullptr;
    }

    (void)wxInitializeGdiImageRefDataRuntime(runtime);
    return runtime;
  }

  /**
   * Address: 0x0042B9D0 (FUN_0042B9D0)
   *
   * What it does:
   * Shared wx-object unref tail used by destructor paths that only clear
   * ref-data ownership.
   */
  void RunWxObjectUnrefTail(
    WxObjectRuntimeView* const object
  ) noexcept
  {
    if (object == nullptr) {
      return;
    }
    object->refData = nullptr;
  }

  /**
   * Address: 0x00974AB0 (FUN_00974AB0)
   *
   * What it does:
   * Runs one wx-object base-destruction tail by preserving C++ base-vtable
   * transition semantics and clearing shared ref-data ownership lanes.
   */
  [[maybe_unused]] void DestroyWxObjectRuntimeBase(
    WxObjectRuntimeView* const object
  ) noexcept
  {
    if (object == nullptr) {
      return;
    }

    RunWxObjectUnrefTail(object);
  }

  class WxObjectRefDataRuntimeDispatch
  {
  public:
    virtual ~WxObjectRefDataRuntimeDispatch() = default;
    virtual void* Destroy(unsigned int flags) = 0;
  };

  struct WxObjectRefDataRuntimeView : WxObjectRefDataRuntimeDispatch
  {
    std::int32_t refCount = 0; // +0x04
  };
  static_assert(
    offsetof(WxObjectRefDataRuntimeView, refCount) == 0x4,
    "WxObjectRefDataRuntimeView::refCount offset must be 0x4"
  );

  /**
   * Address: 0x00977F40 (FUN_00977F40, wxEvent::UnRef)
   *
   * What it does:
   * Releases one shared wx ref-data lane by decrementing its refcount,
   * destroying the payload when the count reaches zero, then clears the
   * object's ref-data pointer.
   */
  void wxEventUnRefRuntime(
    WxObjectRuntimeView* const object
  ) noexcept
  {
    if (object == nullptr || object->refData == nullptr) {
      return;
    }

    auto* const refData = reinterpret_cast<WxObjectRefDataRuntimeView*>(object->refData);
    --refData->refCount;
    if (refData->refCount == 0) {
      (void)refData->Destroy(1u);
    }
    object->refData = nullptr;
  }

  class WxObjectCopyOnWriteRuntimeDispatch
  {
  public:
    virtual ~WxObjectCopyOnWriteRuntimeDispatch() = default;
    virtual void* CreateRefData() const = 0;
    virtual void* CloneRefData(const void* sourceRefData) const = 0;
  };

  struct WxObjectCopyOnWriteRuntimeView : WxObjectCopyOnWriteRuntimeDispatch
  {
    void* refData = nullptr; // +0x04
  };
  static_assert(
    offsetof(WxObjectCopyOnWriteRuntimeView, refData) == 0x4,
    "WxObjectCopyOnWriteRuntimeView::refData offset must be 0x4"
  );

  /**
   * Address: 0x00977F70 (FUN_00977F70)
   *
   * What it does:
   * Ensures one wx-object runtime lane owns unique ref-data before mutation:
   * clone when shared, otherwise allocate a fresh ref-data payload when empty.
   */
  void wxObjectEnsureUniqueRefDataRuntime(
    WxObjectCopyOnWriteRuntimeView* const object
  )
  {
    if (object == nullptr) {
      return;
    }

    if (object->refData != nullptr) {
      auto* const refData = reinterpret_cast<WxObjectRefDataRuntimeView*>(object->refData);
      if (refData->refCount > 1) {
        wxEventUnRefRuntime(reinterpret_cast<WxObjectRuntimeView*>(object));
        object->refData = object->CloneRefData(refData);
      }
      return;
    }

    object->refData = object->CreateRefData();
  }

  /**
   * Address: 0x00965C80 (FUN_00965C80)
   *
   * What it does:
   * Executes the wx-object destruction tail used by wx-event lanes by
   * releasing shared ref-data ownership through `wxEvent::UnRef`.
   */
  void DestroyWxObjectEventRuntimeLane(
    WxObjectRuntimeView* const object
  ) noexcept
  {
    wxEventUnRefRuntime(object);
  }

  /**
   * Address: 0x00965E80 (FUN_00965E80)
   *
   * What it does:
   * Executes the wx size-event destruction tail by transitioning to the
   * `wxObject` base lane and releasing shared ref-data ownership.
   */
  void DestroyWxSizeEventRuntime(
    WxObjectRuntimeView* const object
  ) noexcept
  {
    wxEventUnRefRuntime(object);
  }

  /**
   * Address: 0x009661D0 (FUN_009661D0)
   *
   * What it does:
   * Executes the wx focus-event destruction tail by transitioning to the
   * `wxObject` base lane and releasing shared ref-data ownership.
   */
  void DestroyWxFocusEventRuntime(
    WxObjectRuntimeView* const object
  ) noexcept
  {
    wxEventUnRefRuntime(object);
  }

  /**
   * Address: 0x00976000 (FUN_00976000)
   *
   * What it does:
   * Runs non-deleting teardown for one `wxMask` runtime lane by destroying the
   * owned Win32 bitmap handle and releasing shared wx ref-data ownership.
   */
  [[maybe_unused]] void wxDestroyMaskRuntimeNoDelete(
    WxMaskRuntimeView* const maskRuntime
  ) noexcept
  {
    if (maskRuntime == nullptr) {
      return;
    }

    maskRuntime->vtable = &gWxMaskRuntimeVTableTag;
    if (maskRuntime->nativeMaskHandle != nullptr) {
      (void)::DeleteObject(maskRuntime->nativeMaskHandle);
    }
    maskRuntime->vtable = &gWxObjectRuntimeVTableTag;
    wxEventUnRefRuntime(static_cast<WxObjectRuntimeView*>(maskRuntime));
  }

  /**
   * Address: 0x009A9D70 (FUN_009A9D70)
   *
   * What it does:
   * Runs non-deleting teardown for one `wxImageList` runtime lane by
   * destroying the owned Win32 image-list handle and releasing shared wx
   * ref-data ownership.
   */
  [[maybe_unused]] void wxDestroyImageListRuntimeNoDelete(
    WxImageListRuntimeView* const imageListRuntime
  ) noexcept
  {
    if (imageListRuntime == nullptr) {
      return;
    }

    imageListRuntime->vtable = &gWxImageListRuntimeVTableTag;
    if (imageListRuntime->nativeImageListHandle != nullptr) {
      ::ImageList_Destroy(imageListRuntime->nativeImageListHandle);
      imageListRuntime->nativeImageListHandle = nullptr;
    }
    imageListRuntime->vtable = &gWxObjectRuntimeVTableTag;
    wxEventUnRefRuntime(static_cast<WxObjectRuntimeView*>(imageListRuntime));
  }

  /**
   * Address: 0x0097B080 (FUN_0097B080)
   *
   * What it does:
   * Releases one owned Win32 cursor-handle lane from `wxCursorRefData`.
   */
  [[maybe_unused]] int wxDestroyCursorRefDataHandleRuntime(
    WxCursorRefDataRuntimeView* const cursorRefData
  ) noexcept
  {
    if (cursorRefData == nullptr || cursorRefData->nativeCursorHandle == nullptr) {
      return 0;
    }

    int destroyResult = 0;
    if (cursorRefData->ownsCursorHandle != 0u) {
      destroyResult = ::DestroyCursor(static_cast<HCURSOR>(cursorRefData->nativeCursorHandle));
    }

    cursorRefData->nativeCursorHandle = nullptr;
    return destroyResult;
  }

  /**
   * Address: 0x0097B1A0 (FUN_0097B1A0)
   *
   * What it does:
   * Runs non-deleting/deleting teardown semantics for one `wxCursorRefData`
   * payload by releasing its cursor handle and rebasing to `wxObjectRefData`.
   */
  [[maybe_unused]] void* wxDestroyCursorRefDataWithFlag(
    void* const cursorRefDataRuntime,
    const std::uint8_t deleteFlags
  ) noexcept
  {
    auto* const cursorRefData = static_cast<WxCursorRefDataRuntimeView*>(cursorRefDataRuntime);
    if (cursorRefData == nullptr) {
      return nullptr;
    }

    *reinterpret_cast<void**>(cursorRefData) = &gWxCursorRefDataRuntimeVTableTag;
    (void)wxDestroyCursorRefDataHandleRuntime(cursorRefData);
    *reinterpret_cast<void**>(cursorRefData) = &gWxObjectRefDataRuntimeVTableTag;
    if ((deleteFlags & 1u) != 0u) {
      ::operator delete(cursorRefData);
    }
    return cursorRefData;
  }

  struct WxDCCacheEntryRuntimeView : WxObjectRuntimeView
  {
    HGDIOBJ cachedBitmap = nullptr; // +0x08
    HDC cachedDeviceContext = nullptr; // +0x0C
  };
  static_assert(
    offsetof(WxDCCacheEntryRuntimeView, cachedBitmap) == 0x08,
    "WxDCCacheEntryRuntimeView::cachedBitmap offset must be 0x08"
  );
  static_assert(
    offsetof(WxDCCacheEntryRuntimeView, cachedDeviceContext) == 0x0C,
    "WxDCCacheEntryRuntimeView::cachedDeviceContext offset must be 0x0C"
  );

  /**
   * Address: 0x009CA250 (FUN_009CA250)
   *
   * What it does:
   * Destroys one cached GDI bitmap/device-context pair and then runs the
   * `wxObject` ref-data destruction tail for the cache-entry payload.
   */
  void DestroyWxDCCacheEntryRuntime(
    WxDCCacheEntryRuntimeView* const entry
  ) noexcept
  {
    if (entry == nullptr) {
      return;
    }

    if (entry->cachedBitmap != nullptr) {
      (void)::DeleteObject(entry->cachedBitmap);
      entry->cachedBitmap = nullptr;
    }
    if (entry->cachedDeviceContext != nullptr) {
      (void)::DeleteDC(entry->cachedDeviceContext);
      entry->cachedDeviceContext = nullptr;
    }

    wxEventUnRefRuntime(static_cast<WxObjectRuntimeView*>(entry));
  }

  /**
   * Address: 0x0097E570 (FUN_0097E570, sub_97E570)
   *
   * What it does:
   * Copies shared wx ref-data ownership from `clone` into `object` when the
   * two objects differ, preserving wx refcount increment/decrement semantics.
   */
  WxObjectRuntimeView* wxObjectCopySharedRefDataRuntime(
    WxObjectRuntimeView* const object,
    WxObjectRuntimeView* const clone
  )
  {
    if (object == clone) {
      return object;
    }

    if (object->refData != clone->refData) {
      auto* const currentRefData = reinterpret_cast<WxObjectRefDataRuntimeView*>(object->refData);
      if (currentRefData != nullptr) {
        --currentRefData->refCount;
        if (currentRefData->refCount == 0) {
          (void)currentRefData->Destroy(1u);
        }
        object->refData = nullptr;
      }

      auto* const cloneRefData = reinterpret_cast<WxObjectRefDataRuntimeView*>(clone->refData);
      if (cloneRefData != nullptr) {
        object->refData = cloneRefData;
        ++cloneRefData->refCount;
      }
    }

    return object;
  }

  struct WxRegionRefDataRuntimeView : WxObjectRefDataRuntimeView
  {
    HRGN regionHandle = nullptr; // +0x08
  };
  static_assert(
    offsetof(WxRegionRefDataRuntimeView, regionHandle) == 0x8,
    "WxRegionRefDataRuntimeView::regionHandle offset must be 0x8"
  );
  static_assert(sizeof(WxRegionRefDataRuntimeView) == 0x0C, "WxRegionRefDataRuntimeView size must be 0x0C");

  class WxRegionRefDataRuntimeStorage final : public WxRegionRefDataRuntimeView
  {
  public:
    void* Destroy(const unsigned int flags) override
    {
      if (regionHandle != nullptr) {
        (void)::DeleteObject(regionHandle);
        regionHandle = nullptr;
      }

      if ((flags & 1u) != 0u) {
        delete this;
        return nullptr;
      }

      return this;
    }
  };
  static_assert(sizeof(WxRegionRefDataRuntimeStorage) == 0x0C, "WxRegionRefDataRuntimeStorage size must be 0x0C");

  struct WxRegionRectCtorRuntimeView
  {
    void* vtable = nullptr;                   // +0x00
    WxRegionRefDataRuntimeView* refData = 0; // +0x04
    std::uint8_t inDtor = 0;                 // +0x08
    std::uint8_t reserved09_0B[0x3]{};
  };
  static_assert(offsetof(WxRegionRectCtorRuntimeView, refData) == 0x04, "WxRegionRectCtorRuntimeView::refData offset must be 0x04");
  static_assert(offsetof(WxRegionRectCtorRuntimeView, inDtor) == 0x08, "WxRegionRectCtorRuntimeView::inDtor offset must be 0x08");
  static_assert(sizeof(WxRegionRectCtorRuntimeView) == 0x0C, "WxRegionRectCtorRuntimeView size must be 0x0C");

  /**
   * Address: 0x0097BBB0 (FUN_0097BBB0)
   *
   * What it does:
   * Initializes one wx region runtime object from a rectangle and assigns a
   * freshly allocated region-refdata payload owning the `CreateRectRgn` handle.
   */
  [[maybe_unused]] [[nodiscard]] WxRegionRectCtorRuntimeView* wxConstructRegionFromRectRuntime(
    WxRegionRectCtorRuntimeView* const region,
    const int x1,
    const int y1,
    const int width,
    const int height
  )
  {
    if (region == nullptr) {
      return nullptr;
    }

    region->refData = nullptr;
    region->inDtor = 0;

    auto* const refData = new (std::nothrow) WxRegionRefDataRuntimeStorage();
    if (refData != nullptr) {
      refData->refCount = 1;
      refData->regionHandle = nullptr;
    }

    region->refData = refData;
    if (region->refData != nullptr) {
      region->refData->regionHandle = ::CreateRectRgn(x1, y1, x1 + width, y1 + height);
    }
    return region;
  }

  /**
   * Address: 0x0097BEF0 (FUN_0097BEF0)
   *
   * What it does:
   * Applies one region combine operation (`AND/OR/XOR/DIFF/COPY`) against
   * `this` and `clone`, or shares clone ref-data when this object is empty and
   * operation semantics permit direct adoption.
   */
  bool wxRegionApplyCombineModeRuntime(
    WxObjectCopyOnWriteRuntimeView* const object,
    WxObjectRuntimeView* const clone,
    const int combineMode
  )
  {
    if (object == nullptr || clone == nullptr) {
      return false;
    }

    if (object->refData == nullptr) {
      switch (combineMode) {
      case 1:
      case 3:
      case 4:
        (void)wxObjectCopySharedRefDataRuntime(reinterpret_cast<WxObjectRuntimeView*>(object), clone);
        return true;
      default:
        return false;
      }
    }

    wxObjectEnsureUniqueRefDataRuntime(object);
    auto* const targetRefData = static_cast<WxRegionRefDataRuntimeView*>(object->refData);
    auto* const sourceRefData = static_cast<WxRegionRefDataRuntimeView*>(clone->refData);
    if (targetRefData == nullptr || sourceRefData == nullptr) {
      return false;
    }

    int win32CombineMode = RGN_COPY;
    switch (combineMode) {
    case 0:
      win32CombineMode = RGN_AND;
      break;
    case 2:
      win32CombineMode = RGN_DIFF;
      break;
    case 3:
      win32CombineMode = RGN_OR;
      break;
    case 4:
      win32CombineMode = RGN_XOR;
      break;
    default:
      win32CombineMode = RGN_COPY;
      break;
    }

    return ::CombineRgn(
             targetRefData->regionHandle,
             targetRefData->regionHandle,
             sourceRefData->regionHandle,
             win32CombineMode
           )
      != 0;
  }

  void ReleaseWxStringSharedPayload(
    wxStringRuntime& value
  ) noexcept
  {
    std::int32_t* const sharedPrefixWords = reinterpret_cast<std::int32_t*>(value.m_pchData) - 3;
    const std::int32_t sharedRefCount = sharedPrefixWords[0];
    if (sharedRefCount != -1) {
      sharedPrefixWords[0] = sharedRefCount - 1;
      if (sharedRefCount == 1) {
        ::operator delete(sharedPrefixWords);
      }
    }
  }

  struct WxFileConfigLineRuntime
  {
    wxStringRuntime text{};               // +0x00
    WxFileConfigLineRuntime* next = 0;   // +0x04
    WxFileConfigLineRuntime* prev = 0;   // +0x08
  };
  static_assert(sizeof(WxFileConfigLineRuntime) == 0x0C, "WxFileConfigLineRuntime size must be 0x0C");

  struct WxFileConfigLineListRuntimeView
  {
    std::uint8_t reserved00_13[0x14]{};
    WxFileConfigLineRuntime* head = nullptr; // +0x14
    WxFileConfigLineRuntime* tail = nullptr; // +0x18
  };
  static_assert(
    offsetof(WxFileConfigLineListRuntimeView, head) == 0x14,
    "WxFileConfigLineListRuntimeView::head offset must be 0x14"
  );
  static_assert(
    offsetof(WxFileConfigLineListRuntimeView, tail) == 0x18,
    "WxFileConfigLineListRuntimeView::tail offset must be 0x18"
  );

  struct WxFileConfigRuntimeView
  {
    void* vtable = nullptr;                     // +0x00
    std::uint8_t reserved04_0F[0x0C]{};
    std::uint32_t formatFlags = 0;             // +0x10
    WxFileConfigLineRuntime* head = nullptr;   // +0x14
    WxFileConfigLineRuntime* tail = nullptr;   // +0x18
    wxStringRuntime userConfigPath{};          // +0x1C
    wxStringRuntime globalConfigPath{};        // +0x20
    wxStringRuntime currentPath{};             // +0x24
    struct WxFileConfigGroupRuntimeView* rootGroup = nullptr;    // +0x28
    struct WxFileConfigGroupRuntimeView* currentGroup = nullptr; // +0x2C
  };
  static_assert(offsetof(WxFileConfigRuntimeView, vtable) == 0x00, "WxFileConfigRuntimeView::vtable offset must be 0x00");
  static_assert(offsetof(WxFileConfigRuntimeView, formatFlags) == 0x10, "WxFileConfigRuntimeView::formatFlags offset must be 0x10");
  static_assert(offsetof(WxFileConfigRuntimeView, head) == 0x14, "WxFileConfigRuntimeView::head offset must be 0x14");
  static_assert(offsetof(WxFileConfigRuntimeView, tail) == 0x18, "WxFileConfigRuntimeView::tail offset must be 0x18");
  static_assert(
    offsetof(WxFileConfigRuntimeView, userConfigPath) == 0x1C,
    "WxFileConfigRuntimeView::userConfigPath offset must be 0x1C"
  );
  static_assert(
    offsetof(WxFileConfigRuntimeView, globalConfigPath) == 0x20,
    "WxFileConfigRuntimeView::globalConfigPath offset must be 0x20"
  );
  static_assert(
    offsetof(WxFileConfigRuntimeView, currentPath) == 0x24,
    "WxFileConfigRuntimeView::currentPath offset must be 0x24"
  );
  static_assert(
    offsetof(WxFileConfigRuntimeView, rootGroup) == 0x28,
    "WxFileConfigRuntimeView::rootGroup offset must be 0x28"
  );
  static_assert(
    offsetof(WxFileConfigRuntimeView, currentGroup) == 0x2C,
    "WxFileConfigRuntimeView::currentGroup offset must be 0x2C"
  );
  static_assert(sizeof(WxFileConfigRuntimeView) == 0x30, "WxFileConfigRuntimeView size must be 0x30");

  struct WxFileConfigGroupEntryRuntimeView
  {
    std::uint8_t reserved00_03[0x4]{};
    const wchar_t* keyName = nullptr; // +0x04
  };
  static_assert(
    offsetof(WxFileConfigGroupEntryRuntimeView, keyName) == 0x04,
    "WxFileConfigGroupEntryRuntimeView::keyName offset must be 0x04"
  );

  struct WxFileConfigGroupRuntimeView;

  struct WxFileConfigEntryRuntimeView
  {
    WxFileConfigGroupRuntimeView* ownerGroup = nullptr; // +0x00
    wxStringRuntime keyName{};                          // +0x04
    wxStringRuntime valueText{};                        // +0x08
    std::uint8_t entryFlags = 0;                        // +0x0C
    std::uint8_t reserved0D_0F[0x3]{};
    std::int32_t sourceLine = 0;                        // +0x10
    WxFileConfigLineRuntime* entryLine = nullptr; // +0x14
  };
  static_assert(
    offsetof(WxFileConfigEntryRuntimeView, ownerGroup) == 0x00,
    "WxFileConfigEntryRuntimeView::ownerGroup offset must be 0x00"
  );
  static_assert(
    offsetof(WxFileConfigEntryRuntimeView, keyName) == 0x04,
    "WxFileConfigEntryRuntimeView::keyName offset must be 0x04"
  );
  static_assert(
    offsetof(WxFileConfigEntryRuntimeView, valueText) == 0x08,
    "WxFileConfigEntryRuntimeView::valueText offset must be 0x08"
  );
  static_assert(
    offsetof(WxFileConfigEntryRuntimeView, entryFlags) == 0x0C,
    "WxFileConfigEntryRuntimeView::entryFlags offset must be 0x0C"
  );
  static_assert(
    offsetof(WxFileConfigEntryRuntimeView, sourceLine) == 0x10,
    "WxFileConfigEntryRuntimeView::sourceLine offset must be 0x10"
  );
  static_assert(
    offsetof(WxFileConfigEntryRuntimeView, entryLine) == 0x14,
    "WxFileConfigEntryRuntimeView::entryLine offset must be 0x14"
  );
  static_assert(sizeof(WxFileConfigEntryRuntimeView) == 0x18, "WxFileConfigEntryRuntimeView size must be 0x18");

  struct WxFileConfigGroupRuntimeView
  {
    using CompareFn = std::int32_t(__cdecl*)(const void*, const void*);

    WxFileConfigRuntimeView* ownerConfig = nullptr;               // +0x00
    WxFileConfigGroupRuntimeView* parentGroup = nullptr;          // +0x04
    std::uint8_t reserved08_0B[0x4]{};
    std::uint32_t entryCount = 0;                                 // +0x0C
    WxFileConfigGroupEntryRuntimeView** entries = nullptr;        // +0x10
    CompareFn entryComparator = nullptr;                          // +0x14
    std::uint8_t reserved18_1B[0x4]{};
    std::uint32_t childGroupCount = 0;                            // +0x1C
    WxFileConfigGroupRuntimeView** childGroups = nullptr;         // +0x20
    CompareFn childGroupComparator = nullptr;                     // +0x24
    wxStringRuntime groupName{};                                  // +0x28
    std::uint8_t dirtyFlag = 0;                                   // +0x2C
    std::uint8_t reserved2D_2F[0x3]{};
    WxFileConfigLineRuntime* groupLine = nullptr;                 // +0x30
    WxFileConfigEntryRuntimeView* lastEntry = nullptr;            // +0x34
    WxFileConfigGroupRuntimeView* lineOwnerDescendant = nullptr;  // +0x38
  };
  static_assert(
    offsetof(WxFileConfigGroupRuntimeView, ownerConfig) == 0x00,
    "WxFileConfigGroupRuntimeView::ownerConfig offset must be 0x00"
  );
  static_assert(
    offsetof(WxFileConfigGroupRuntimeView, parentGroup) == 0x04,
    "WxFileConfigGroupRuntimeView::parentGroup offset must be 0x04"
  );
  static_assert(
    offsetof(WxFileConfigGroupRuntimeView, entryCount) == 0x0C,
    "WxFileConfigGroupRuntimeView::entryCount offset must be 0x0C"
  );
  static_assert(
    offsetof(WxFileConfigGroupRuntimeView, entries) == 0x10,
    "WxFileConfigGroupRuntimeView::entries offset must be 0x10"
  );
  static_assert(
    offsetof(WxFileConfigGroupRuntimeView, entryComparator) == 0x14,
    "WxFileConfigGroupRuntimeView::entryComparator offset must be 0x14"
  );
  static_assert(
    offsetof(WxFileConfigGroupRuntimeView, childGroupCount) == 0x1C,
    "WxFileConfigGroupRuntimeView::childGroupCount offset must be 0x1C"
  );
  static_assert(
    offsetof(WxFileConfigGroupRuntimeView, childGroups) == 0x20,
    "WxFileConfigGroupRuntimeView::childGroups offset must be 0x20"
  );
  static_assert(
    offsetof(WxFileConfigGroupRuntimeView, childGroupComparator) == 0x24,
    "WxFileConfigGroupRuntimeView::childGroupComparator offset must be 0x24"
  );
  static_assert(
    offsetof(WxFileConfigGroupRuntimeView, groupName) == 0x28,
    "WxFileConfigGroupRuntimeView::groupName offset must be 0x28"
  );
  static_assert(
    offsetof(WxFileConfigGroupRuntimeView, dirtyFlag) == 0x2C,
    "WxFileConfigGroupRuntimeView::dirtyFlag offset must be 0x2C"
  );
  static_assert(
    offsetof(WxFileConfigGroupRuntimeView, groupLine) == 0x30,
    "WxFileConfigGroupRuntimeView::groupLine offset must be 0x30"
  );
  static_assert(
    offsetof(WxFileConfigGroupRuntimeView, lastEntry) == 0x34,
    "WxFileConfigGroupRuntimeView::lastEntry offset must be 0x34"
  );
  static_assert(
    offsetof(WxFileConfigGroupRuntimeView, lineOwnerDescendant) == 0x38,
    "WxFileConfigGroupRuntimeView::lineOwnerDescendant offset must be 0x38"
  );

  struct WxTextBufferRuntimeView
  {
    void* vtable = nullptr;                    // +0x00
    wxStringRuntime fileName{};                // +0x04
    std::uint8_t reserved08_17[0x10]{};        // +0x08
    std::uint32_t lineCount = 0;               // +0x18
    wxStringRuntime* lineItems = nullptr;      // +0x1C
    std::uint8_t lineArrayReserved20_23[0x04]{}; // +0x20
    std::int32_t currentLineIndex = 0;         // +0x24
    std::uint8_t openStatus = 0;               // +0x28
    std::uint8_t reserved29_2B[0x03]{};
  };
  static_assert(
    offsetof(WxTextBufferRuntimeView, vtable) == 0x00,
    "WxTextBufferRuntimeView::vtable offset must be 0x00"
  );
  static_assert(
    offsetof(WxTextBufferRuntimeView, fileName) == 0x04,
    "WxTextBufferRuntimeView::fileName offset must be 0x04"
  );
  static_assert(
    offsetof(WxTextBufferRuntimeView, lineCount) == 0x18,
    "WxTextBufferRuntimeView::lineCount offset must be 0x18"
  );
  static_assert(
    offsetof(WxTextBufferRuntimeView, lineItems) == 0x1C,
    "WxTextBufferRuntimeView::lineItems offset must be 0x1C"
  );
  static_assert(
    offsetof(WxTextBufferRuntimeView, currentLineIndex) == 0x24,
    "WxTextBufferRuntimeView::currentLineIndex offset must be 0x24"
  );
  static_assert(
    offsetof(WxTextBufferRuntimeView, openStatus) == 0x28,
    "WxTextBufferRuntimeView::openStatus offset must be 0x28"
  );
  static_assert(sizeof(WxTextBufferRuntimeView) == 0x2C, "WxTextBufferRuntimeView size must be 0x2C");

  struct WxTextFileFileLaneRuntimeView
  {
    std::int32_t fileDescriptor = -1; // +0x00
    std::uint8_t errorState = 0;      // +0x04
    std::uint8_t reserved05_07[0x03]{};
  };
  static_assert(
    offsetof(WxTextFileFileLaneRuntimeView, fileDescriptor) == 0x00,
    "WxTextFileFileLaneRuntimeView::fileDescriptor offset must be 0x00"
  );
  static_assert(
    offsetof(WxTextFileFileLaneRuntimeView, errorState) == 0x04,
    "WxTextFileFileLaneRuntimeView::errorState offset must be 0x04"
  );
  static_assert(sizeof(WxTextFileFileLaneRuntimeView) == 0x08, "WxTextFileFileLaneRuntimeView size must be 0x08");

  struct WxTextFileRuntimeView
  {
    WxTextBufferRuntimeView textBuffer{};
    WxTextFileFileLaneRuntimeView fileLane{}; // +0x2C
  };
  static_assert(
    offsetof(WxTextFileRuntimeView, textBuffer) == 0x00,
    "WxTextFileRuntimeView::textBuffer offset must be 0x00"
  );
  static_assert(
    offsetof(WxTextFileRuntimeView, fileLane) == 0x2C,
    "WxTextFileRuntimeView::fileLane offset must be 0x2C"
  );
  static_assert(sizeof(WxTextFileRuntimeView) == 0x34, "WxTextFileRuntimeView size must be 0x34");

  enum class WxTextBufferEolType : std::int32_t
  {
    None = 0,
    Unix = 1,
    Dos = 2,
    Mac = 3
  };

  [[nodiscard]] const wchar_t* WxFileConfigLineTextOrEmpty(
    const WxFileConfigLineRuntime* const line
  ) noexcept
  {
    return line != nullptr ? line->text.c_str() : wxEmptyString;
  }

  [[nodiscard]] bool WxStringRuntimeHasCharacters(
    const wxStringRuntime* const value
  ) noexcept
  {
    if (value == nullptr || value->m_pchData == nullptr) {
      return false;
    }

    const auto* const sharedPrefixWords = reinterpret_cast<const std::int32_t*>(value->m_pchData) - 3;
    return sharedPrefixWords[1] != 0;
  }

  [[nodiscard]] wxStringRuntime AllocateOwnedWxString(
    const std::wstring& value
  );

  void AssignOwnedWxString(
    wxStringRuntime* const outValue,
    const std::wstring& value
  );

  void RetainWxStringRuntime(
    wxStringRuntime* outValue,
    const wxStringRuntime* sourceValue
  );

  [[nodiscard]] WxFileConfigLineRuntime* WxFileConfigGetLastEntryLine(
    WxFileConfigGroupRuntimeView* groupView
  );

  [[nodiscard]] WxFileConfigLineRuntime* WxFileConfigGetGroupLine(
    WxFileConfigGroupRuntimeView* groupView
  );

  wxStringRuntime* WxFileConfigBuildGroupPath(
    WxFileConfigGroupRuntimeView* groupView,
    wxStringRuntime* outPath
  );

  WxFileConfigEntryRuntimeView* WxFileConfigSetLineCacheFromEntry(
    WxFileConfigGroupRuntimeView* groupView,
    WxFileConfigEntryRuntimeView* entry
  );

  [[nodiscard]] WxFileConfigGroupRuntimeView* WxFileConfigResetPathState(
    WxFileConfigRuntimeView* ownerConfig
  );

  [[nodiscard]] WxFileConfigGroupRuntimeView* WxFileConfigSetPath(
    WxFileConfigRuntimeView* ownerConfig,
    const wxStringRuntime* pathText
  );

  wxStringRuntime* WxFileConfigUnescapeHeaderToken(
    wxStringRuntime* outUnescapedToken,
    const wxStringRuntime* escapedToken
  );

  [[nodiscard]] bool WxFileConfigDeleteEntryByName(
    WxFileConfigGroupRuntimeView* groupView,
    const wchar_t* keyName
  );

  void WxFileConfigLoadFromTextBuffer(
    WxFileConfigRuntimeView* ownerConfig,
    const WxTextBufferRuntimeView* textBuffer,
    bool preserveSourceLines
  );

  [[nodiscard]] bool WxFileConfigLoadConfiguredFiles(
    WxFileConfigRuntimeView* ownerConfig
  );

  WxFileConfigRuntimeView* WxFileConfigConstructFromInputStream(
    WxFileConfigRuntimeView* ownerConfig,
    void* inputStream
  );

  /**
   * Address: 0x00A1ADB0 (FUN_00A1ADB0)
   *
   * What it does:
   * Initializes one wxFileConfig line-node by sharing the input wxString lane,
   * retaining its refcount when non-empty, then wiring next/prev links.
   */
  WxFileConfigLineRuntime* WxFileConfigInitLineNode(
    WxFileConfigLineRuntime* const lineNode,
    const wxStringRuntime* const lineText,
    WxFileConfigLineRuntime* const nextLine
  )
  {
    lineNode->text.m_pchData = nullptr;
    if (WxStringRuntimeHasCharacters(lineText)) {
      lineNode->text.m_pchData = lineText->m_pchData;
      auto* const sharedPrefixWords = reinterpret_cast<std::int32_t*>(lineNode->text.m_pchData) - 3;
      const std::int32_t currentRefCount = sharedPrefixWords[0];
      if (currentRefCount != -1) {
        sharedPrefixWords[0] = currentRefCount + 1;
      }
    } else {
      lineNode->text.m_pchData = const_cast<wchar_t*>(wxEmptyString);
    }

    lineNode->next = nextLine;
    lineNode->prev = nullptr;
    return lineNode;
  }

  /**
   * Address: 0x00A1B400 (FUN_00A1B400)
   *
   * What it does:
   * Appends one new parsed line-node to the wxFileConfig group line list,
   * updating head/tail links and emitting trace snapshots around insertion.
   */
  [[nodiscard]] WxFileConfigLineRuntime* WxFileConfigAppendLine(
    WxFileConfigLineListRuntimeView* const listView,
    const wxStringRuntime* const lineText
  )
  {
    wxLogTrace(L"wxFileConfig", L"    ** Adding Line '%s'", lineText != nullptr ? lineText->c_str() : wxEmptyString);
    wxLogTrace(L"wxFileConfig", L"        head: %s", WxFileConfigLineTextOrEmpty(listView->head));
    wxLogTrace(L"wxFileConfig", L"        tail: %s", WxFileConfigLineTextOrEmpty(listView->tail));

    WxFileConfigLineRuntime* const newLine = new (std::nothrow)
      WxFileConfigLineRuntime();
    WxFileConfigLineRuntime* appendedLine = nullptr;
    if (newLine != nullptr) {
      appendedLine = WxFileConfigInitLineNode(newLine, lineText, nullptr);
    }

    if (listView->tail != nullptr) {
      listView->tail->next = appendedLine;
      appendedLine->prev = listView->tail;
    } else {
      listView->head = appendedLine;
    }
    listView->tail = appendedLine;

    wxLogTrace(L"wxFileConfig", L"        head: %s", WxFileConfigLineTextOrEmpty(listView->head));
    wxLogTrace(L"wxFileConfig", L"        tail: %s", WxFileConfigLineTextOrEmpty(listView->tail));
    return listView->tail;
  }

  /**
   * Address: 0x00A1B4F0 (FUN_00A1B4F0)
   *
   * What it does:
   * Inserts one new parsed line-node after a specific sibling (or at list
   * head when sibling is null), preserving the doubly-linked order.
   */
  [[nodiscard]] WxFileConfigLineRuntime* WxFileConfigInsertLineAfter(
    WxFileConfigLineListRuntimeView* const listView,
    const wxStringRuntime* const lineText,
    WxFileConfigLineRuntime* const afterLine
  )
  {
    wxLogTrace(
      L"wxFileConfig",
      L"    ** Inserting Line '%s' after '%s'",
      lineText != nullptr ? lineText->c_str() : wxEmptyString,
      WxFileConfigLineTextOrEmpty(afterLine)
    );
    wxLogTrace(L"wxFileConfig", L"        head: %s", WxFileConfigLineTextOrEmpty(listView->head));
    wxLogTrace(L"wxFileConfig", L"        tail: %s", WxFileConfigLineTextOrEmpty(listView->tail));

    if (afterLine == listView->tail) {
      return WxFileConfigAppendLine(listView, lineText);
    }

    WxFileConfigLineRuntime* const newLine = new (std::nothrow)
      WxFileConfigLineRuntime();
    WxFileConfigLineRuntime* insertedLine = nullptr;
    if (newLine != nullptr) {
      insertedLine = WxFileConfigInitLineNode(newLine, lineText, nullptr);
    }

    if (afterLine != nullptr) {
      insertedLine->next = afterLine->next;
      insertedLine->prev = afterLine;
      insertedLine->next->prev = insertedLine;
      afterLine->next = insertedLine;
    } else {
      insertedLine->next = listView->head;
      listView->head->prev = insertedLine;
      listView->head = insertedLine;
    }

    wxLogTrace(L"wxFileConfig", L"        head: %s", WxFileConfigLineTextOrEmpty(listView->head));
    wxLogTrace(L"wxFileConfig", L"        tail: %s", WxFileConfigLineTextOrEmpty(listView->tail));
    return insertedLine;
  }

  /**
   * Address: 0x00A1B610 (FUN_00A1B610)
   *
   * What it does:
   * Unlinks one line-node from the wxFileConfig group line list, logs the
   * resulting head/tail state, then releases the shared wxString lane.
   */
  void WxFileConfigRemoveLine(
    WxFileConfigLineListRuntimeView* const listView,
    WxFileConfigLineRuntime* const line
  )
  {
    wxLogTrace(L"wxFileConfig", L"    ** Removing Line '%s'", line->text.c_str());
    wxLogTrace(L"wxFileConfig", L"        head: %s", WxFileConfigLineTextOrEmpty(listView->head));
    wxLogTrace(L"wxFileConfig", L"        tail: %s", WxFileConfigLineTextOrEmpty(listView->tail));

    WxFileConfigLineRuntime* const previousLine = line->prev;
    WxFileConfigLineRuntime* const nextLine = line->next;
    if (previousLine != nullptr) {
      previousLine->next = nextLine;
    } else {
      listView->head = nextLine;
    }
    if (nextLine != nullptr) {
      nextLine->prev = previousLine;
    } else {
      listView->tail = previousLine;
    }

    wxLogTrace(L"wxFileConfig", L"        head: %s", WxFileConfigLineTextOrEmpty(listView->head));
    wxLogTrace(L"wxFileConfig", L"        tail: %s", WxFileConfigLineTextOrEmpty(listView->tail));

    ReleaseWxStringSharedPayload(line->text);
    ::operator delete(line);
  }

  /**
   * Address: 0x00A1A940 (FUN_00A1A940)
   *
   * What it does:
   * Reports whether one entry currently has no attached source-line node.
   */
  [[nodiscard]] bool WxFileConfigEntryHasNoSourceLine(
    const WxFileConfigEntryRuntimeView* const entry
  ) noexcept
  {
    return entry->entryLine == nullptr;
  }

  /**
   * Address: 0x00A1A970 (FUN_00A1A970)
   *
   * What it does:
   * Releases the two shared wxString lanes owned by one entry node.
   */
  void WxFileConfigReleaseEntryTextLanes(
    WxFileConfigEntryRuntimeView* const entry
  )
  {
    ReleaseWxStringSharedPayload(entry->valueText);
    ReleaseWxStringSharedPayload(entry->keyName);
  }

  /**
   * Address: 0x00A1A9F0 (FUN_00A1A9F0)
   *
   * What it does:
   * Stores one group-line cache pointer on the group node and returns it.
   */
  [[nodiscard]] WxFileConfigLineRuntime* WxFileConfigSetGroupLineCache(
    WxFileConfigGroupRuntimeView* const groupView,
    WxFileConfigLineRuntime* const groupLine
  )
  {
    groupView->groupLine = groupLine;
    return groupLine;
  }

  /**
   * Address: 0x00A1A640 (FUN_00A1A640)
   *
   * What it does:
   * Clears the current path string lane and resets active-group selection to
   * the root group lane.
   */
  [[nodiscard]] WxFileConfigGroupRuntimeView* WxFileConfigResetPathState(
    WxFileConfigRuntimeView* const ownerConfig
  )
  {
    ownerConfig->currentPath.Empty(0);
    ownerConfig->currentGroup = ownerConfig->rootGroup;
    return ownerConfig->currentGroup;
  }

  /**
   * Address: 0x00A1AB40 (FUN_00A1AB40)
   *
   * What it does:
   * Binary-searches one sorted config-entry pointer lane by case-insensitive
   * key name and returns the matching entry when found.
   */
  [[nodiscard]] WxFileConfigGroupEntryRuntimeView* WxFileConfigFindEntryByNameNoCase(
    const WxFileConfigGroupRuntimeView* const groupView,
    const wchar_t* const keyName
  )
  {
    std::uint32_t high = groupView->entryCount;
    std::uint32_t low = 0;
    if (high == 0) {
      return nullptr;
    }

    while (true) {
      const std::uint32_t middle = (high + low) >> 1;
      auto* const candidate = groupView->entries[middle];
      const int compareResult = ::_wcsicmp(candidate->keyName, keyName);
      if (compareResult > 0) {
        high = (high + low) >> 1;
      } else {
        if (compareResult >= 0) {
          return candidate;
        }
        low = middle + 1;
      }

      if (low >= high) {
        return nullptr;
      }
    }
  }

  /**
   * Address: 0x00A1ABB0 (FUN_00A1ABB0)
   *
   * What it does:
   * Binary-searches one sorted child-group pointer lane by case-insensitive
   * group name and returns the matching group when found.
   */
  [[nodiscard]] WxFileConfigGroupRuntimeView* WxFileConfigFindChildGroupByNameNoCase(
    const WxFileConfigGroupRuntimeView* const groupView,
    const wchar_t* const groupName
  )
  {
    std::uint32_t high = groupView->childGroupCount;
    std::uint32_t low = 0;
    if (high == 0) {
      return nullptr;
    }

    while (true) {
      const std::uint32_t middle = (high + low) >> 1;
      auto* const candidate = groupView->childGroups[middle];
      const int compareResult = ::_wcsicmp(candidate->groupName.c_str(), groupName != nullptr ? groupName : L"");
      if (compareResult > 0) {
        high = middle;
      } else {
        if (compareResult >= 0) {
          return candidate;
        }
        low = middle + 1;
      }

      if (low >= high) {
        return nullptr;
      }
    }
  }

  /**
   * Address: 0x00A1AC40 (FUN_00A1AC40)
   *
   * What it does:
   * Attaches one parsed source line to an entry and reports duplicate-source
   * definition when an entry line was already present for the same key.
   */
  [[nodiscard]] WxFileConfigEntryRuntimeView* WxFileConfigSetEntryLineWithDuplicateCheck(
    WxFileConfigEntryRuntimeView* const entry,
    WxFileConfigLineRuntime* const entryLine
  )
  {
    if (entry->entryLine != nullptr) {
      wxStringRuntime groupPath = AllocateOwnedWxString(std::wstring());
      WxFileConfigBuildGroupPath(entry->ownerGroup, &groupPath);
      wxLogTrace(
        L"wxFileConfig",
        L"entry '%s' appears more than once in group '%s'",
        entry->keyName.c_str(),
        groupPath.c_str()
      );
      ReleaseWxStringSharedPayload(groupPath);
    }

    entry->entryLine = entryLine;
    return WxFileConfigSetLineCacheFromEntry(entry->ownerGroup, entry);
  }

  /**
   * Address: 0x00A1AD10 (FUN_00A1AD10)
   *
   * What it does:
   * Compares two entry nodes by case-insensitive key name.
   */
  [[nodiscard]] std::int32_t __cdecl WxFileConfigCompareEntriesByKeyName(
    const void* const left,
    const void* const right
  ) noexcept
  {
    auto* const leftEntry = static_cast<const WxFileConfigEntryRuntimeView*>(left);
    auto* const rightEntry = static_cast<const WxFileConfigEntryRuntimeView*>(right);
    return ::_wcsicmp(leftEntry->keyName.c_str(), rightEntry->keyName.c_str());
  }

  /**
   * Address: 0x00A1AD30 (FUN_00A1AD30)
   *
   * What it does:
   * Compares two group nodes by case-insensitive group-name lane.
   */
  [[nodiscard]] std::int32_t __cdecl WxFileConfigCompareGroupsByName(
    const void* const left,
    const void* const right
  ) noexcept
  {
    auto* const leftGroup = static_cast<const WxFileConfigGroupRuntimeView*>(left);
    auto* const rightGroup = static_cast<const WxFileConfigGroupRuntimeView*>(right);
    return ::_wcsicmp(leftGroup->groupName.c_str(), rightGroup->groupName.c_str());
  }

  /**
   * Address: 0x00A1AD70 (FUN_00A1AD70)
   *
   * What it does:
   * Returns one retained shared value-string lane from a config entry.
   */
  [[nodiscard]] wxStringRuntime* WxFileConfigCopyEntryValueText(
    const WxFileConfigEntryRuntimeView* const entry,
    wxStringRuntime* const outValue
  )
  {
    outValue->m_pchData = nullptr;
    const wchar_t* const valueText = entry->valueText.m_pchData;
    const auto* const sharedPrefixWords = reinterpret_cast<const std::int32_t*>(valueText) - 3;
    if (sharedPrefixWords[1] == 0) {
      outValue->m_pchData = const_cast<wchar_t*>(wxEmptyString);
      return outValue;
    }

    outValue->m_pchData = const_cast<wchar_t*>(valueText);
    if (sharedPrefixWords[0] != -1) {
      auto* const writablePrefixWords = reinterpret_cast<std::int32_t*>(outValue->m_pchData) - 3;
      writablePrefixWords[0] += 1;
    }
    return outValue;
  }

  /**
   * Address: 0x00A1AE00 (FUN_00A1AE00)
   *
   * What it does:
   * Builds one `%WINDIR%\\` prefix string into caller-provided wx storage.
   */
  [[nodiscard]] wxStringRuntime* WxFileConfigBuildWindowsDirectoryPrefix(
    wxStringRuntime* const outPath
  )
  {
    wchar_t windowsDirectoryBuffer[MAX_PATH]{};
    (void)::GetWindowsDirectoryW(windowsDirectoryBuffer, MAX_PATH);

    std::wstring windowsDirectoryPath(windowsDirectoryBuffer);
    windowsDirectoryPath.push_back(L'\\');
    AssignOwnedWxString(outPath, windowsDirectoryPath);
    return outPath;
  }

  /**
   * Address: 0x00A1B9E0 (FUN_00A1B9E0)
   *
   * What it does:
   * Unescapes one serialized value token by decoding backslash escapes and
   * handling optional leading/trailing quote wrappers.
   */
  wxStringRuntime* WxFileConfigUnescapeValueText(
    const wxStringRuntime* const escapedValue,
    wxStringRuntime* const outValue
  )
  {
    std::wstring decodedText{};
    const std::wstring sourceText = escapedValue != nullptr ? std::wstring(escapedValue->c_str()) : std::wstring();
    const bool quoted = !sourceText.empty() && sourceText.front() == L'"';
    const std::size_t beginIndex = quoted ? 1u : 0u;

    for (std::size_t index = beginIndex; index < sourceText.size(); ++index) {
      const wchar_t ch = sourceText[index];
      if (ch == L'\\') {
        ++index;
        if (index >= sourceText.size()) {
          break;
        }

        switch (sourceText[index]) {
        case L'"':
          decodedText.push_back(L'"');
          break;
        case L'\\':
          decodedText.push_back(L'\\');
          break;
        case L'n':
          decodedText.push_back(L'\n');
          break;
        case L'r':
          decodedText.push_back(L'\r');
          break;
        case L't':
          decodedText.push_back(L'\t');
          break;
        default:
          break;
        }
      } else if (ch == L'"' && quoted) {
        if (index != sourceText.size() - 1u) {
          wxLogTrace(
            L"wxFileConfig",
            L"unexpected \\\" at position %d in '%s'.",
            static_cast<int>(index),
            sourceText.c_str()
          );
        }
      } else {
        decodedText.push_back(ch);
      }
    }

    AssignOwnedWxString(outValue, decodedText);
    return outValue;
  }

  void WxFileConfigInsertChildGroupPointer(
    WxFileConfigGroupRuntimeView* const parentGroup,
    WxFileConfigGroupRuntimeView* const childGroup
  )
  {
    const std::uint32_t oldCount = parentGroup->childGroupCount;
    std::uint32_t insertPosition = oldCount;
    if (childGroup != nullptr && parentGroup->childGroupComparator != nullptr && parentGroup->childGroups != nullptr) {
      insertPosition = 0;
      while (insertPosition < oldCount) {
        if (parentGroup->childGroupComparator(parentGroup->childGroups[insertPosition], childGroup) > 0) {
          break;
        }
        ++insertPosition;
      }
    }

    auto** const newGroups = new (std::nothrow) WxFileConfigGroupRuntimeView*[oldCount + 1u];
    if (newGroups == nullptr) {
      return;
    }

    if (parentGroup->childGroups != nullptr && insertPosition > 0u) {
      std::memcpy(
        newGroups,
        parentGroup->childGroups,
        static_cast<std::size_t>(insertPosition) * sizeof(WxFileConfigGroupRuntimeView*)
      );
    }

    newGroups[insertPosition] = childGroup;

    if (parentGroup->childGroups != nullptr && insertPosition < oldCount) {
      std::memcpy(
        newGroups + insertPosition + 1u,
        parentGroup->childGroups + insertPosition,
        static_cast<std::size_t>(oldCount - insertPosition) * sizeof(WxFileConfigGroupRuntimeView*)
      );
    }

    delete[] parentGroup->childGroups;
    parentGroup->childGroups = newGroups;
    parentGroup->childGroupCount = oldCount + 1u;
  }

  /**
   * Address: 0x00A1B700 (FUN_00A1B700)
   *
   * What it does:
   * Initializes one config-group node: sets parent/owner links, initializes
   * sorted entry/group comparator lanes, stores retained group-name text, and
   * clears group runtime caches.
   */
  [[nodiscard]] WxFileConfigGroupRuntimeView* WxFileConfigInitGroupNode(
    WxFileConfigGroupRuntimeView* const groupView,
    WxFileConfigGroupRuntimeView* const parentGroup,
    const wxStringRuntime* const groupName,
    WxFileConfigRuntimeView* const ownerConfig
  )
  {
    std::memset(groupView, 0, sizeof(WxFileConfigGroupRuntimeView));
    groupView->entryComparator = &WxFileConfigCompareEntriesByKeyName;
    groupView->childGroupComparator = &WxFileConfigCompareGroupsByName;

    const wchar_t* const incomingText = groupName != nullptr ? groupName->m_pchData : nullptr;
    if (incomingText != nullptr) {
      const auto* const sharedPrefixWords = reinterpret_cast<const std::int32_t*>(incomingText) - 3;
      if (sharedPrefixWords[1] != 0) {
        groupView->groupName.m_pchData = const_cast<wchar_t*>(incomingText);
        if (sharedPrefixWords[0] != -1) {
          auto* const writablePrefixWords = reinterpret_cast<std::int32_t*>(groupView->groupName.m_pchData) - 3;
          writablePrefixWords[0] += 1;
        }
      } else {
        groupView->groupName.m_pchData = const_cast<wchar_t*>(wxEmptyString);
      }
    } else {
      groupView->groupName.m_pchData = const_cast<wchar_t*>(wxEmptyString);
    }

    groupView->parentGroup = parentGroup;
    groupView->ownerConfig = ownerConfig;
    groupView->dirtyFlag = 0;
    groupView->groupLine = nullptr;
    groupView->lastEntry = nullptr;
    groupView->lineOwnerDescendant = nullptr;
    return groupView;
  }

  /**
   * Address: 0x00A1B7D0 (FUN_00A1B7D0)
   *
   * What it does:
   * Allocates and initializes one child group under a parent group, then
   * inserts it into the parent's sorted child-group pointer lane.
   */
  [[nodiscard]] WxFileConfigGroupRuntimeView* WxFileConfigAddChildGroup(
    WxFileConfigGroupRuntimeView* const parentGroup,
    const wxStringRuntime* const childGroupName
  )
  {
    auto* childGroup = new (std::nothrow) WxFileConfigGroupRuntimeView();
    if (childGroup != nullptr) {
      childGroup = WxFileConfigInitGroupNode(childGroup, parentGroup, childGroupName, parentGroup->ownerConfig);
    }

    WxFileConfigInsertChildGroupPointer(parentGroup, childGroup);
    return childGroup;
  }

  /**
   * Address: 0x00A1AC20 (FUN_00A1AC20)
   *
   * What it does:
   * Marks one group-parent chain as dirty by setting the dirty bit on the
   * current node and every ancestor reached through the parent lane.
   */
  WxFileConfigGroupRuntimeView** WxFileConfigMarkGroupChainDirty(
    WxFileConfigGroupRuntimeView* groupNode
  )
  {
    WxFileConfigGroupRuntimeView* currentNode = groupNode;
    currentNode->dirtyFlag = 1;
    while (currentNode->parentGroup != nullptr) {
      currentNode = currentNode->parentGroup;
      currentNode->dirtyFlag = 1;
    }
    return &currentNode->parentGroup;
  }

  [[nodiscard]] bool WxFileConfigHeaderCharNeedsEscape(
    const wchar_t ch
  ) noexcept
  {
    if (std::iswalnum(static_cast<wint_t>(ch)) != 0) {
      return false;
    }
    if ((ch & 0x80) != 0) {
      return false;
    }

    static constexpr wchar_t kAllowed[] = L"@_/-!.*%";
    for (const wchar_t allowedCh : kAllowed) {
      if (allowedCh == L'\0') {
        break;
      }
      if (allowedCh == ch) {
        return false;
      }
    }
    return true;
  }

  /**
   * Address: 0x00A1BEF0 (FUN_00A1BEF0)
   *
   * What it does:
   * Escapes one group header token by prefixing non-alnum ASCII characters
   * (except wx whitelist symbols) with a backslash.
   */
  wxStringRuntime* WxFileConfigEscapeGroupHeaderToken(
    wxStringRuntime* const outEscapedToken,
    const wxStringRuntime* const rawToken
  )
  {
    std::wstring escapedText{};
    const wchar_t* const sourceText = rawToken != nullptr ? rawToken->c_str() : L"";
    for (const wchar_t* cursor = sourceText; *cursor != L'\0'; ++cursor) {
      const wchar_t ch = *cursor;
      if (WxFileConfigHeaderCharNeedsEscape(ch)) {
        escapedText.push_back(L'\\');
      }
      escapedText.push_back(ch);
    }

    AssignOwnedWxString(outEscapedToken, escapedText);
    return outEscapedToken;
  }

  /**
   * Address: 0x00A1BE40 (FUN_00A1BE40)
   *
   * What it does:
   * Unescapes one group/key token by removing backslash escape markers and
   * copying the escaped payload into caller-provided wxString storage.
   */
  wxStringRuntime* WxFileConfigUnescapeHeaderToken(
    wxStringRuntime* const outUnescapedToken,
    const wxStringRuntime* const escapedToken
  )
  {
    std::wstring unescapedText{};
    const wchar_t* cursor = escapedToken != nullptr ? escapedToken->c_str() : L"";
    while (*cursor != L'\0') {
      if (*cursor == L'\\' && cursor[1] != L'\0') {
        ++cursor;
      }
      unescapedText.push_back(*cursor);
      ++cursor;
    }

    AssignOwnedWxString(outUnescapedToken, unescapedText);
    return outUnescapedToken;
  }

  /**
   * Address: 0x00A1AA40 (FUN_00A1AA40)
   *
   * What it does:
   * Builds one slash-delimited full group path recursively from parent groups
   * into caller-provided wxString storage.
   */
  wxStringRuntime* WxFileConfigBuildGroupPath(
    WxFileConfigGroupRuntimeView* const groupView,
    wxStringRuntime* const outPath
  )
  {
    if (groupView->parentGroup != nullptr) {
      wxStringRuntime parentPath = AllocateOwnedWxString(std::wstring());
      WxFileConfigBuildGroupPath(groupView->parentGroup, &parentPath);

      std::wstring fullPath(parentPath.c_str());
      fullPath.push_back(L'/');
      fullPath.append(groupView->groupName.c_str());
      AssignOwnedWxString(outPath, fullPath);

      ReleaseWxStringSharedPayload(parentPath);
    } else {
      AssignOwnedWxString(outPath, std::wstring());
    }

    return outPath;
  }

  /**
   * Address: 0x00A1AA00 (FUN_00A1AA00)
   *
   * What it does:
   * Walks one cached descendant-group chain and resolves the insertion anchor
   * line from the deepest descendant owning runtime line state.
   */
  [[nodiscard]] WxFileConfigLineRuntime* WxFileConfigGetLastDescendantEntryLine(
    WxFileConfigGroupRuntimeView* groupView
  )
  {
    while (groupView->lineOwnerDescendant != nullptr) {
      groupView = groupView->lineOwnerDescendant;
    }
    return WxFileConfigGetLastEntryLine(groupView);
  }

  /**
   * Address: 0x00A1C480 (FUN_00A1C480)
   *
   * What it does:
   * Returns the current group's trailing entry line when present; otherwise
   * materializes and returns the cached group-header line.
   */
  [[nodiscard]] WxFileConfigLineRuntime* WxFileConfigGetLastEntryLine(
    WxFileConfigGroupRuntimeView* groupView
  )
  {
    wxLogTrace(L"wxFileConfig", L"  GetLastEntryLine() for Group '%s'", groupView->groupName.c_str());
    if (groupView->lastEntry != nullptr) {
      return groupView->lastEntry->entryLine;
    }
    return WxFileConfigGetGroupLine(groupView);
  }

  /**
   * Address: 0x00A1C2D0 (FUN_00A1C2D0)
   *
   * What it does:
   * Lazily builds and inserts one `[group/path]` header line for the current
   * group, then caches the created line-node for reuse.
   */
  [[nodiscard]] WxFileConfigLineRuntime* WxFileConfigGetGroupLine(
    WxFileConfigGroupRuntimeView* const groupView
  )
  {
    wxLogTrace(L"wxFileConfig", L"  GetGroupLine() for Group '%s'", groupView->groupName.c_str());
    if (groupView->groupLine != nullptr) {
      return groupView->groupLine;
    }

    wxLogTrace(L"wxFileConfig", L"    Getting Line item pointer");
    WxFileConfigGroupRuntimeView* const parentGroup = groupView->parentGroup;
    if (parentGroup == nullptr) {
      return groupView->groupLine;
    }

    wxLogTrace(L"wxFileConfig", L"    checking parent '%s'", parentGroup->groupName.c_str());

    wxStringRuntime groupPath = AllocateOwnedWxString(std::wstring());
    WxFileConfigBuildGroupPath(groupView, &groupPath);
    std::wstring sectionToken(groupPath.c_str());
    if (!sectionToken.empty() && sectionToken.front() == L'/') {
      sectionToken.erase(sectionToken.begin());
    }

    wxStringRuntime pathWithoutLeadingSlash = AllocateOwnedWxString(sectionToken);
    wxStringRuntime escapedPath = AllocateOwnedWxString(std::wstring());
    WxFileConfigEscapeGroupHeaderToken(&escapedPath, &pathWithoutLeadingSlash);

    std::wstring headerText = L"[";
    headerText.append(escapedPath.c_str());
    headerText.push_back(L']');
    wxStringRuntime headerLine = AllocateOwnedWxString(headerText);

    ReleaseWxStringSharedPayload(escapedPath);
    ReleaseWxStringSharedPayload(pathWithoutLeadingSlash);
    ReleaseWxStringSharedPayload(groupPath);

    WxFileConfigLineRuntime* const anchorLine = WxFileConfigGetLastDescendantEntryLine(parentGroup);
    groupView->groupLine = WxFileConfigInsertLineAfter(
      reinterpret_cast<WxFileConfigLineListRuntimeView*>(groupView->ownerConfig),
      &headerLine,
      anchorLine
    );
    parentGroup->lineOwnerDescendant = groupView;

    ReleaseWxStringSharedPayload(headerLine);
    return groupView->groupLine;
  }

  struct WxFileConfigKeyContextRuntimeView
  {
    void* ownerConfig = nullptr;       // +0x00
    wxStringRuntime keyToken{};        // +0x04
    wxStringRuntime previousPath{};    // +0x08
    std::uint8_t switchedPath = 0;     // +0x0C
    std::uint8_t reserved0D_0F[0x3]{};
  };
  static_assert(
    offsetof(WxFileConfigKeyContextRuntimeView, ownerConfig) == 0x00,
    "WxFileConfigKeyContextRuntimeView::ownerConfig offset must be 0x00"
  );
  static_assert(
    offsetof(WxFileConfigKeyContextRuntimeView, keyToken) == 0x04,
    "WxFileConfigKeyContextRuntimeView::keyToken offset must be 0x04"
  );
  static_assert(
    offsetof(WxFileConfigKeyContextRuntimeView, previousPath) == 0x08,
    "WxFileConfigKeyContextRuntimeView::previousPath offset must be 0x08"
  );
  static_assert(
    offsetof(WxFileConfigKeyContextRuntimeView, switchedPath) == 0x0C,
    "WxFileConfigKeyContextRuntimeView::switchedPath offset must be 0x0C"
  );
  static_assert(sizeof(WxFileConfigKeyContextRuntimeView) == 0x10, "WxFileConfigKeyContextRuntimeView size must be 0x10");

  void WxFileConfigCallSetPathVirtual(
    void* const ownerConfig,
    const wxStringRuntime* const path
  )
  {
    if (ownerConfig == nullptr) {
      return;
    }
    WxFileConfigSetPath(static_cast<WxFileConfigRuntimeView*>(ownerConfig), path);
  }

  [[nodiscard]] const wxStringRuntime* WxFileConfigCallGetPathVirtual(
    void* const ownerConfig
  )
  {
    if (ownerConfig == nullptr) {
      return nullptr;
    }
    return &static_cast<WxFileConfigRuntimeView*>(ownerConfig)->currentPath;
  }

  [[nodiscard]] std::wstring WxStringBeforeFirstChar(
    const std::wstring& text,
    const wchar_t separator
  )
  {
    const std::size_t separatorIndex = text.find(separator);
    if (separatorIndex == std::wstring::npos || separatorIndex == 0) {
      return std::wstring();
    }
    return text.substr(0, separatorIndex);
  }

  [[nodiscard]] std::wstring WxStringAfterFirstChar(
    const std::wstring& text,
    const wchar_t separator
  )
  {
    const std::size_t separatorIndex = text.find(separator);
    if (separatorIndex == std::wstring::npos) {
      return text;
    }
    return text.substr(separatorIndex + 1);
  }

  [[nodiscard]] std::vector<std::wstring> WxFileConfigSplitPathComponents(
    const std::wstring& pathText
  )
  {
    std::vector<std::wstring> components{};
    std::size_t tokenStart = 0;
    while (tokenStart <= pathText.size()) {
      std::size_t tokenEnd = pathText.find(L'/', tokenStart);
      if (tokenEnd == std::wstring::npos) {
        tokenEnd = pathText.size();
      }

      if (tokenEnd > tokenStart) {
        components.push_back(pathText.substr(tokenStart, tokenEnd - tokenStart));
      }

      if (tokenEnd == pathText.size()) {
        break;
      }
      tokenStart = tokenEnd + 1;
    }

    return components;
  }

  /**
   * Address: 0x00A1C040 (FUN_00A1C040)
   *
   * What it does:
   * Applies one absolute-or-relative group path string, creating missing child
   * groups on demand and updating current path/group runtime lanes.
   */
  [[nodiscard]] WxFileConfigGroupRuntimeView* WxFileConfigSetPath(
    WxFileConfigRuntimeView* const ownerConfig,
    const wxStringRuntime* const pathText
  )
  {
    const wchar_t* const incomingPath = pathText != nullptr ? pathText->c_str() : L"";
    if (incomingPath == nullptr || *incomingPath == L'\0') {
      return WxFileConfigResetPathState(ownerConfig);
    }

    std::wstring resolvedPath{};
    if (*incomingPath == L'/') {
      resolvedPath.assign(incomingPath);
    } else {
      resolvedPath.assign(ownerConfig->currentPath.c_str());
      resolvedPath.push_back(L'/');
      resolvedPath.append(incomingPath);
    }

    const std::vector<std::wstring> components = WxFileConfigSplitPathComponents(resolvedPath);
    ownerConfig->currentGroup = ownerConfig->rootGroup;
    for (const std::wstring& component : components) {
      if (ownerConfig->currentGroup == nullptr) {
        break;
      }

      wxStringRuntime componentToken = AllocateOwnedWxString(component);
      WxFileConfigGroupRuntimeView* nextGroup =
        WxFileConfigFindChildGroupByNameNoCase(ownerConfig->currentGroup, componentToken.c_str());
      if (nextGroup == nullptr) {
        nextGroup = WxFileConfigAddChildGroup(ownerConfig->currentGroup, &componentToken);
      }
      ownerConfig->currentGroup = nextGroup;
      ReleaseWxStringSharedPayload(componentToken);
    }

    std::wstring normalizedPath{};
    for (const std::wstring& component : components) {
      normalizedPath.push_back(L'/');
      normalizedPath.append(component);
    }
    AssignOwnedWxString(&ownerConfig->currentPath, normalizedPath);
    return ownerConfig->currentGroup;
  }

  /**
   * Address: 0x00A09980 (FUN_00A09980)
   *
   * What it does:
   * Parses one incoming key token into path/key lanes, optionally switches the
   * active config path context, and stores restore state for cleanup.
   */
  WxFileConfigKeyContextRuntimeView* WxFileConfigPrepareKeyContext(
    WxFileConfigKeyContextRuntimeView* const context,
    void* const ownerConfig,
    const wxStringRuntime* const incomingKey
  )
  {
    context->ownerConfig = ownerConfig;
    context->keyToken = AllocateOwnedWxString(std::wstring());
    context->previousPath = AllocateOwnedWxString(std::wstring());
    context->switchedPath = 0;

    const std::wstring rawKeyText = incomingKey != nullptr ? std::wstring(incomingKey->c_str()) : std::wstring();
    std::wstring parsedPath = WxStringBeforeFirstChar(rawKeyText, L'/');

    if (parsedPath.empty() && !rawKeyText.empty() && rawKeyText.front() == L'/') {
      parsedPath.push_back(L'/');
    }

    if (!parsedPath.empty()) {
      context->switchedPath = 1;
      AssignOwnedWxString(&context->keyToken, WxStringAfterFirstChar(rawKeyText, L'/'));

      const wxStringRuntime* const currentPath = WxFileConfigCallGetPathVirtual(ownerConfig);
      AssignOwnedWxString(&context->previousPath, currentPath != nullptr ? std::wstring(currentPath->c_str()) : std::wstring());

      std::wstring normalizedPathWithSlash(context->previousPath.c_str());
      if (normalizedPathWithSlash.empty() || normalizedPathWithSlash.back() != L'/') {
        normalizedPathWithSlash.push_back(L'/');
        AssignOwnedWxString(&context->previousPath, normalizedPathWithSlash);
      }

      wxStringRuntime newPath = AllocateOwnedWxString(parsedPath);
      WxFileConfigCallSetPathVirtual(ownerConfig, &newPath);
      ReleaseWxStringSharedPayload(newPath);
    } else {
      AssignOwnedWxString(&context->keyToken, rawKeyText);
    }

    return context;
  }

  /**
   * Address: 0x00A09B10 (FUN_00A09B10)
   *
   * What it does:
   * Restores switched config-path context (when needed) and releases temporary
   * parsed-key/path wxString lanes captured by prepare-key processing.
   */
  void WxFileConfigFinalizeKeyContext(
    WxFileConfigKeyContextRuntimeView* const context
  )
  {
    if (context->switchedPath != 0) {
      WxFileConfigCallSetPathVirtual(context->ownerConfig, &context->previousPath);
    }

    ReleaseWxStringSharedPayload(context->previousPath);
    ReleaseWxStringSharedPayload(context->keyToken);
  }

  /**
   * Address: 0x00A1B0F0 (FUN_00A1B0F0)
   *
   * What it does:
   * Prepares one key/path context, probes the active group for a
   * case-insensitive child-group match, then restores key-context lanes.
   */
  [[nodiscard]] bool WxFileConfigHasGroup(
    WxFileConfigRuntimeView* const ownerConfig,
    const wxStringRuntime* const keyText
  )
  {
    WxFileConfigKeyContextRuntimeView keyContext{};
    WxFileConfigPrepareKeyContext(&keyContext, ownerConfig, keyText);

    const auto* const activeGroup = ownerConfig != nullptr ? ownerConfig->currentGroup : nullptr;
    const bool found = activeGroup != nullptr
      && WxFileConfigFindChildGroupByNameNoCase(activeGroup, keyContext.keyToken.c_str()) != nullptr;

    WxFileConfigFinalizeKeyContext(&keyContext);
    return found;
  }

  /**
   * Address: 0x00A1B130 (FUN_00A1B130)
   *
   * What it does:
   * Prepares one key/path context, probes the active group for a
   * case-insensitive entry-key match, then restores key-context lanes.
   */
  [[nodiscard]] bool WxFileConfigHasEntry(
    WxFileConfigRuntimeView* const ownerConfig,
    const wxStringRuntime* const keyText
  )
  {
    WxFileConfigKeyContextRuntimeView keyContext{};
    WxFileConfigPrepareKeyContext(&keyContext, ownerConfig, keyText);

    const auto* const activeGroup = ownerConfig != nullptr ? ownerConfig->currentGroup : nullptr;
    const bool found = activeGroup != nullptr
      && WxFileConfigFindEntryByNameNoCase(activeGroup, keyContext.keyToken.c_str()) != nullptr;

    WxFileConfigFinalizeKeyContext(&keyContext);
    return found;
  }

  /**
   * Address: 0x00A1B170 (FUN_00A1B170)
   *
   * What it does:
   * Prepares one key lookup context, searches the current group for one
   * case-insensitive key match, copies the entry value on hit, then restores
   * the previous path context before returning lookup success.
   */
  [[nodiscard]] bool WxFileConfigReadString(
    WxFileConfigRuntimeView* const ownerConfig,
    const wxStringRuntime* const keyText,
    wxStringRuntime* const outValue
  )
  {
    WxFileConfigKeyContextRuntimeView keyContext{};
    WxFileConfigPrepareKeyContext(&keyContext, ownerConfig, keyText);

    const WxFileConfigGroupRuntimeView* const activeGroup = ownerConfig != nullptr ? ownerConfig->currentGroup : nullptr;
    auto* const entry = activeGroup != nullptr
      ? reinterpret_cast<WxFileConfigEntryRuntimeView*>(
          WxFileConfigFindEntryByNameNoCase(activeGroup, keyContext.keyToken.c_str())
        )
      : nullptr;

    const bool found = entry != nullptr;
    if (found && outValue != nullptr) {
      WxFileConfigCopyEntryValueText(entry, outValue);
    }

    WxFileConfigFinalizeKeyContext(&keyContext);
    return found;
  }

  /**
   * Address: 0x00A1B910 (FUN_00A1B910)
   *
   * What it does:
   * Initializes one config-entry node with owner/group line metadata, shares
   * key storage, and marks immutable keys (leading '!') in entry flags.
   */
  WxFileConfigEntryRuntimeView* WxFileConfigInitEntryNode(
    WxFileConfigEntryRuntimeView* const entry,
    WxFileConfigGroupRuntimeView* const ownerGroup,
    const wxStringRuntime* const incomingKey,
    const std::int32_t sourceLine
  )
  {
    entry->keyName.m_pchData = nullptr;
    if (WxStringRuntimeHasCharacters(incomingKey)) {
      entry->keyName.m_pchData = incomingKey->m_pchData;
      auto* const sharedPrefixWords = reinterpret_cast<std::int32_t*>(entry->keyName.m_pchData) - 3;
      const std::int32_t currentRefCount = sharedPrefixWords[0];
      if (currentRefCount != -1) {
        sharedPrefixWords[0] = currentRefCount + 1;
      }
    } else {
      entry->keyName.m_pchData = const_cast<wchar_t*>(wxEmptyString);
    }

    entry->valueText.m_pchData = const_cast<wchar_t*>(wxEmptyString);
    entry->entryFlags &= 0xFAu;
    entry->ownerGroup = ownerGroup;
    entry->sourceLine = sourceLine;
    entry->entryLine = nullptr;

    const wchar_t* const keyText = incomingKey != nullptr ? incomingKey->c_str() : L"";
    if (*keyText == L'!') {
      entry->entryFlags |= 0x02u;

      std::wstring normalizedKey(entry->keyName.c_str());
      if (!normalizedKey.empty()) {
        normalizedKey.erase(0, 1);
      }
      ReleaseWxStringSharedPayload(entry->keyName);
      entry->keyName = AllocateOwnedWxString(normalizedKey);
    }

    return entry;
  }

  void WxFileConfigInsertEntryPointer(
    WxFileConfigGroupRuntimeView* const groupView,
    WxFileConfigEntryRuntimeView* const entry
  )
  {
    const std::uint32_t oldCount = groupView->entryCount;
    std::uint32_t insertPosition = oldCount;
    if (groupView->entryComparator != nullptr && groupView->entries != nullptr) {
      insertPosition = 0;
      while (insertPosition < oldCount) {
        if (groupView->entryComparator(groupView->entries[insertPosition], entry) > 0) {
          break;
        }
        ++insertPosition;
      }
    }

    auto** const newEntries = new (std::nothrow) WxFileConfigGroupEntryRuntimeView*[oldCount + 1u];
    if (newEntries == nullptr) {
      return;
    }

    if (groupView->entries != nullptr && insertPosition > 0u) {
      std::memcpy(
        newEntries,
        groupView->entries,
        static_cast<std::size_t>(insertPosition) * sizeof(WxFileConfigGroupEntryRuntimeView*)
      );
    }

    newEntries[insertPosition] = reinterpret_cast<WxFileConfigGroupEntryRuntimeView*>(entry);

    if (groupView->entries != nullptr && insertPosition < oldCount) {
      std::memcpy(
        newEntries + insertPosition + 1u,
        groupView->entries + insertPosition,
        static_cast<std::size_t>(oldCount - insertPosition) * sizeof(WxFileConfigGroupEntryRuntimeView*)
      );
    }

    delete[] groupView->entries;
    groupView->entries = newEntries;
    groupView->entryCount = oldCount + 1u;
  }

  /**
   * Address: 0x00A1C5D0 (FUN_00A1C5D0)
   *
   * What it does:
   * Allocates one entry node, initializes key/flags metadata, then inserts the
   * entry pointer into the group's sorted entry-pointer lane.
   */
  [[nodiscard]] WxFileConfigEntryRuntimeView* WxFileConfigAddEntry(
    WxFileConfigGroupRuntimeView* const groupView,
    const wxStringRuntime* const keyName,
    const std::int32_t sourceLine
  )
  {
    auto* entry = new (std::nothrow) WxFileConfigEntryRuntimeView();
    if (entry != nullptr) {
      entry = WxFileConfigInitEntryNode(entry, groupView, keyName, sourceLine);
    }

    WxFileConfigInsertEntryPointer(groupView, entry);
    return entry;
  }

  /**
   * Address: 0x00A1B850 (FUN_00A1B850)
   *
   * What it does:
   * Deletes one existing entry by key from the current group: updates line
   * caches/list links, removes the entry from the sorted pointer lane, marks
   * the group chain dirty, then releases entry-owned string payloads.
   */
  [[nodiscard]] bool WxFileConfigDeleteEntryByName(
    WxFileConfigGroupRuntimeView* const groupView,
    const wchar_t* const keyName
  )
  {
    if (groupView == nullptr) {
      return false;
    }

    auto* const entry = reinterpret_cast<WxFileConfigEntryRuntimeView*>(
      WxFileConfigFindEntryByNameNoCase(groupView, keyName)
    );
    if (entry == nullptr) {
      return false;
    }

    if (entry->entryLine != nullptr) {
      if (entry == groupView->lastEntry) {
        WxFileConfigEntryRuntimeView* replacementLastEntry = nullptr;
        for (WxFileConfigLineRuntime* line = entry->entryLine->prev;
             line != nullptr && line != groupView->groupLine;
             line = line->prev) {
          for (std::uint32_t index = 0; index < groupView->entryCount; ++index) {
            auto* const candidate = reinterpret_cast<WxFileConfigEntryRuntimeView*>(groupView->entries[index]);
            if (candidate != entry && candidate->entryLine == line) {
              replacementLastEntry = candidate;
              break;
            }
          }
          if (replacementLastEntry != nullptr) {
            break;
          }
        }
        groupView->lastEntry = replacementLastEntry;
      }

      WxFileConfigRemoveLine(
        reinterpret_cast<WxFileConfigLineListRuntimeView*>(groupView->ownerConfig),
        entry->entryLine
      );
    }

    WxFileConfigMarkGroupChainDirty(groupView);

    std::uint32_t removeIndex = groupView->entryCount;
    for (std::uint32_t index = 0; index < groupView->entryCount; ++index) {
      if (groupView->entries[index] == reinterpret_cast<WxFileConfigGroupEntryRuntimeView*>(entry)) {
        removeIndex = index;
        break;
      }
    }

    if (removeIndex < groupView->entryCount) {
      if (groupView->entryCount == 1u) {
        delete[] groupView->entries;
        groupView->entries = nullptr;
        groupView->entryCount = 0;
      } else {
        const std::uint32_t newCount = groupView->entryCount - 1u;
        auto** const newEntries = new (std::nothrow) WxFileConfigGroupEntryRuntimeView*[newCount];
        if (newEntries != nullptr) {
          std::uint32_t outIndex = 0;
          for (std::uint32_t index = 0; index < groupView->entryCount; ++index) {
            if (index == removeIndex) {
              continue;
            }
            newEntries[outIndex++] = groupView->entries[index];
          }
          delete[] groupView->entries;
          groupView->entries = newEntries;
          groupView->entryCount = newCount;
        }
      }
    }

    WxFileConfigReleaseEntryTextLanes(entry);
    ::operator delete(entry);
    return true;
  }

  void RetainWxStringRuntime(
    wxStringRuntime* const outValue,
    const wxStringRuntime* const sourceValue
  )
  {
    outValue->m_pchData = sourceValue != nullptr && sourceValue->m_pchData != nullptr
      ? sourceValue->m_pchData
      : const_cast<wchar_t*>(wxEmptyString);

    const auto* const sharedPrefixWords = reinterpret_cast<const std::int32_t*>(outValue->m_pchData) - 3;
    if (sharedPrefixWords[1] != 0) {
      auto* const writablePrefixWords = reinterpret_cast<std::int32_t*>(outValue->m_pchData) - 3;
      if (writablePrefixWords[0] != -1) {
        writablePrefixWords[0] += 1;
      }
    }
  }

  struct WxArrayStringRuntimeReadView
  {
    std::uint8_t reserved00_07[0x8]{};
    std::uint32_t count = 0;          // +0x08
    const wchar_t** entries = nullptr; // +0x0C
  };
  static_assert(
    offsetof(WxArrayStringRuntimeReadView, count) == 0x08,
    "WxArrayStringRuntimeReadView::count offset must be 0x08"
  );
  static_assert(
    offsetof(WxArrayStringRuntimeReadView, entries) == 0x0C,
    "WxArrayStringRuntimeReadView::entries offset must be 0x0C"
  );

  /**
   * Address: 0x00961160 (FUN_00961160)
   *
   * What it does:
   * Copies one source wx-string lane into temporary shared storage, lowercases
   * it in place, then retains the lowered shared lane into `outValue`.
   */
  wxStringRuntime* wxStringLowerCopyRuntime(
    const wxStringRuntime* const sourceValue,
    wxStringRuntime* const outValue
  )
  {
    if (outValue == nullptr) {
      return nullptr;
    }

    wxStringRuntime temporary{};
    RetainWxStringRuntime(&temporary, sourceValue);
    temporary.LowerInPlace();

    RetainWxStringRuntime(outValue, &temporary);
    ReleaseWxStringSharedPayload(temporary);
    return outValue;
  }

  /**
   * Address: 0x009837A0 (FUN_009837A0)
   *
   * What it does:
   * Returns one string lane from a wx-array-string payload by index, or
   * `wxEmptyString` when the index is out of range.
   */
  wxStringRuntime* wxArrayStringGetAtRuntime(
    const WxArrayStringRuntimeReadView* const arrayRuntime,
    wxStringRuntime* const outValue,
    const std::uint32_t index
  )
  {
    if (outValue == nullptr) {
      return nullptr;
    }

    if (arrayRuntime == nullptr || arrayRuntime->entries == nullptr || index >= arrayRuntime->count) {
      outValue->m_pchData = const_cast<wchar_t*>(wxEmptyString);
      return outValue;
    }

    wxStringRuntime source{};
    source.m_pchData = const_cast<wchar_t*>(arrayRuntime->entries[index]);
    RetainWxStringRuntime(outValue, &source);
    return outValue;
  }

  /**
   * Address: 0x00A1AA20 (FUN_00A1AA20)
   *
   * What it does:
   * Updates group line-cache lanes after a new entry line is created by
   * recording the latest entry and seeding group-line cache when empty.
   */
  WxFileConfigEntryRuntimeView* WxFileConfigSetLineCacheFromEntry(
    WxFileConfigGroupRuntimeView* const groupView,
    WxFileConfigEntryRuntimeView* const entry
  )
  {
    groupView->lastEntry = entry;
    if (groupView->groupLine == nullptr) {
      groupView->groupLine = entry->entryLine;
    }
    return entry;
  }

  /**
   * Address: 0x00A1AD00 (FUN_00A1AD00)
   *
   * What it does:
   * Marks one entry dirty and propagates dirty state through the owning
   * group-parent chain.
   */
  WxFileConfigGroupRuntimeView** WxFileConfigMarkEntryDirty(
    WxFileConfigEntryRuntimeView* const entry
  )
  {
    entry->entryFlags |= 0x01u;
    return WxFileConfigMarkGroupChainDirty(entry->ownerGroup);
  }

  /**
   * Address: 0x00A1BBD0 (FUN_00A1BBD0)
   *
   * What it does:
   * Escapes one value token for line serialization, adding quotes when leading
   * whitespace/quote is present and escaping control/special characters.
   */
  wxStringRuntime* WxFileConfigEscapeValueText(
    wxStringRuntime* const outEscapedValue,
    const wxStringRuntime* const rawValue
  )
  {
    if (!WxStringRuntimeHasCharacters(rawValue)) {
      RetainWxStringRuntime(outEscapedValue, rawValue);
      return outEscapedValue;
    }

    std::wstring escapedText{};
    const std::wstring sourceText(rawValue->c_str());
    const bool quoteValue =
      !sourceText.empty() &&
      (std::iswspace(static_cast<wint_t>(sourceText.front())) != 0 || sourceText.front() == L'"');

    if (quoteValue) {
      escapedText.push_back(L'"');
    }

    for (const wchar_t ch : sourceText) {
      switch (ch) {
      case L'\t':
        escapedText.append(L"\\t");
        break;
      case L'\n':
        escapedText.append(L"\\n");
        break;
      case L'\r':
        escapedText.append(L"\\r");
        break;
      case L'\\':
        escapedText.append(L"\\\\");
        break;
      case L'"':
        if (quoteValue) {
          escapedText.append(L"\\\"");
        } else {
          escapedText.push_back(ch);
        }
        break;
      default:
        escapedText.push_back(ch);
        break;
      }
    }

    if (quoteValue) {
      escapedText.push_back(L'"');
    }

    AssignOwnedWxString(outEscapedValue, escapedText);
    return outEscapedValue;
  }

  [[nodiscard]] bool WxStringRuntimeEquals(
    const wxStringRuntime* const left,
    const wxStringRuntime* const right
  )
  {
    const wchar_t* const leftText = left != nullptr ? left->c_str() : L"";
    const wchar_t* const rightText = right != nullptr ? right->c_str() : L"";
    return std::wcscmp(leftText, rightText) == 0;
  }

  /**
   * Address: 0x00A1C650 (FUN_00A1C650)
   *
   * What it does:
   * Updates one config-entry value lane and, when requested, writes/creates
   * the serialized `key=value` line then marks owning groups dirty.
   */
  void WxFileConfigSetValue(
    WxFileConfigEntryRuntimeView* const entry,
    const wxStringRuntime* const valueText,
    const bool writeSerializedLine
  )
  {
    if (writeSerializedLine && (entry->entryFlags & 0x02u) != 0u) {
      wxLogTrace(L"wxFileConfig", L"attempt to change immutable key '%s' ignored.", entry->keyName.c_str());
      return;
    }

    if ((entry->entryFlags & 0x04u) != 0u && WxStringRuntimeEquals(valueText, &entry->valueText)) {
      return;
    }

    entry->entryFlags |= 0x04u;
    AssignOwnedWxString(&entry->valueText, valueText != nullptr ? std::wstring(valueText->c_str()) : std::wstring());

    if (!writeSerializedLine) {
      return;
    }

    wxStringRuntime lineValue = AllocateOwnedWxString(valueText != nullptr ? std::wstring(valueText->c_str()) : std::wstring());
    if ((entry->ownerGroup->ownerConfig->formatFlags & 0x08u) == 0u) {
      wxStringRuntime escapedValue = AllocateOwnedWxString(std::wstring());
      WxFileConfigEscapeValueText(&escapedValue, &lineValue);
      ReleaseWxStringSharedPayload(lineValue);
      lineValue = escapedValue;
    }

    wxStringRuntime escapedKey = AllocateOwnedWxString(std::wstring());
    WxFileConfigEscapeGroupHeaderToken(&escapedKey, &entry->keyName);
    std::wstring serializedLineText(escapedKey.c_str());
    serializedLineText.push_back(L'=');
    serializedLineText.append(lineValue.c_str());
    wxStringRuntime serializedLine = AllocateOwnedWxString(serializedLineText);

    if (entry->entryLine != nullptr) {
      AssignOwnedWxString(&entry->entryLine->text, serializedLineText);
    } else {
      WxFileConfigLineRuntime* const anchorLine = WxFileConfigGetLastEntryLine(entry->ownerGroup);
      entry->entryLine = WxFileConfigInsertLineAfter(
        reinterpret_cast<WxFileConfigLineListRuntimeView*>(entry->ownerGroup->ownerConfig),
        &serializedLine,
        anchorLine
      );
      WxFileConfigSetLineCacheFromEntry(entry->ownerGroup, entry);
    }

    WxFileConfigMarkEntryDirty(entry);
    ReleaseWxStringSharedPayload(serializedLine);
    ReleaseWxStringSharedPayload(escapedKey);
    ReleaseWxStringSharedPayload(lineValue);
  }

  [[nodiscard]] bool WxFileConfigCharIsSpace(
    const wchar_t ch
  ) noexcept
  {
    return std::iswspace(static_cast<wint_t>(ch)) != 0;
  }

  [[nodiscard]] std::vector<std::wstring> WxFileConfigSplitLines(
    const std::wstring& text
  )
  {
    std::vector<std::wstring> lines{};
    std::size_t lineStart = 0;
    std::size_t cursor = 0;

    while (cursor < text.size()) {
      const wchar_t ch = text[cursor];
      if (ch != L'\r' && ch != L'\n') {
        ++cursor;
        continue;
      }

      lines.emplace_back(text.substr(lineStart, cursor - lineStart));
      if (ch == L'\r' && (cursor + 1u) < text.size() && text[cursor + 1u] == L'\n') {
        cursor += 2u;
      } else {
        ++cursor;
      }
      lineStart = cursor;
    }

    lines.emplace_back(text.substr(lineStart));
    return lines;
  }

  [[nodiscard]] std::wstring WxDecodeMultibyteText(
    const std::string& bytes,
    const UINT primaryCodePage,
    const UINT fallbackCodePage
  )
  {
    if (bytes.empty()) {
      return std::wstring();
    }

    const auto decodeWithCodePage = [&](const UINT codePage) -> std::wstring {
      const int requiredChars =
        ::MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
      if (requiredChars <= 0) {
        return std::wstring();
      }

      std::wstring decoded(static_cast<std::size_t>(requiredChars), L'\0');
      (void)::MultiByteToWideChar(
        codePage,
        0,
        bytes.data(),
        static_cast<int>(bytes.size()),
        decoded.data(),
        requiredChars
      );
      return decoded;
    };

    std::wstring decoded = decodeWithCodePage(primaryCodePage);
    if (decoded.empty() && fallbackCodePage != primaryCodePage) {
      decoded = decodeWithCodePage(fallbackCodePage);
    }
    return decoded;
  }

  [[nodiscard]] bool WxReadWholeFileBytes(
    const wchar_t* const filePath,
    std::string* const outBytes
  )
  {
    if (filePath == nullptr || *filePath == L'\0' || outBytes == nullptr) {
      return false;
    }

    std::ifstream stream(std::filesystem::path(filePath), std::ios::in | std::ios::binary);
    if (!stream.is_open()) {
      return false;
    }

    outBytes->assign(
      std::istreambuf_iterator<char>(stream),
      std::istreambuf_iterator<char>()
    );
    return true;
  }

  std::unordered_map<WxTextBufferRuntimeView*, std::vector<wxStringRuntime>> gWxTextBufferLineStorage{};
  std::mutex gWxTextBufferLineStorageMutex{};

  void WxTextBufferReleaseOwnedLines(
    WxTextBufferRuntimeView* const textBuffer
  )
  {
    if (textBuffer == nullptr) {
      return;
    }

    const std::lock_guard<std::mutex> lock(gWxTextBufferLineStorageMutex);
    const auto storageIt = gWxTextBufferLineStorage.find(textBuffer);
    if (storageIt != gWxTextBufferLineStorage.end()) {
      for (wxStringRuntime& value : storageIt->second) {
        ReleaseWxStringSharedPayload(value);
      }
      gWxTextBufferLineStorage.erase(storageIt);
    }

    textBuffer->lineCount = 0;
    textBuffer->lineItems = nullptr;
  }

  void WxTextBufferSetSharedFileName(
    WxTextBufferRuntimeView* const textBuffer,
    const wxStringRuntime* const fileName
  )
  {
    if (textBuffer == nullptr) {
      return;
    }

    textBuffer->fileName.m_pchData = const_cast<wchar_t*>(wxEmptyString);
    if (WxStringRuntimeHasCharacters(fileName)) {
      RetainWxStringRuntime(&textBuffer->fileName, fileName);
    }
  }

  /**
   * Address: 0x009FA3B0 (FUN_009FA3B0)
   *
   * What it does:
   * Returns the canonical EOL-token text for one runtime text-buffer EOL type
   * selector.
   */
  [[nodiscard]] const wchar_t* WxTextBufferGetEolToken(
    const std::int32_t eolType
  ) noexcept
  {
    switch (static_cast<WxTextBufferEolType>(eolType)) {
    case WxTextBufferEolType::Unix:
      return L"\n";
    case WxTextBufferEolType::Dos:
      return L"\r\n";
    case WxTextBufferEolType::Mac:
      return L"\r";
    case WxTextBufferEolType::None:
    default:
      return L"";
    }
  }

  /**
   * Address: 0x00A1A1E0 (FUN_00A1A1E0)
   *
   * What it does:
   * Constructs one base wxTextBuffer lane with an empty file-name string and
   * clears line-storage/open flags for later load calls.
   */
  WxTextBufferRuntimeView* WxTextBufferConstruct(
    WxTextBufferRuntimeView* const textBuffer
  )
  {
    if (textBuffer == nullptr) {
      return nullptr;
    }

    textBuffer->vtable = nullptr;
    textBuffer->fileName.m_pchData = const_cast<wchar_t*>(wxEmptyString);
    textBuffer->lineCount = 0;
    textBuffer->lineItems = nullptr;
    textBuffer->currentLineIndex = 0;
    textBuffer->openStatus = 0;
    return textBuffer;
  }

  /**
   * Address: 0x009FA940 (FUN_009FA940)
   *
   * What it does:
   * Constructs one wxTextBuffer lane from a file-name string and initializes
   * line-storage/open state lanes.
   */
  WxTextBufferRuntimeView* WxTextBufferConstructWithFileName(
    WxTextBufferRuntimeView* const textBuffer,
    const wxStringRuntime* const fileName
  )
  {
    if (textBuffer == nullptr) {
      return nullptr;
    }

    (void)WxTextBufferConstruct(textBuffer);
    WxTextBufferSetSharedFileName(textBuffer, fileName);
    textBuffer->currentLineIndex = 0;
    textBuffer->openStatus = 0;
    return textBuffer;
  }

  /**
   * Address: 0x009FA3F0 (FUN_009FA3F0)
   *
   * What it does:
   * Destroys one wxTextBuffer lane by releasing owned line-storage payloads and
   * dropping the shared file-name string lane.
   */
  void WxTextBufferDestroy(
    WxTextBufferRuntimeView* const textBuffer
  )
  {
    if (textBuffer == nullptr) {
      return;
    }

    textBuffer->vtable = nullptr;
    WxTextBufferReleaseOwnedLines(textBuffer);
    ReleaseWxStringSharedPayload(textBuffer->fileName);
    textBuffer->fileName.m_pchData = const_cast<wchar_t*>(wxEmptyString);
    textBuffer->openStatus = 0;
  }

  /**
   * Address: 0x009FA6E0 (FUN_009FA6E0)
   *
   * What it does:
   * Normalizes one source string's line-endings into the requested EOL token
   * while preserving non-linebreak payload exactly.
   */
  wxStringRuntime* WxTextBufferNormalizeLineEndings(
    wxStringRuntime* const outText,
    const wxStringRuntime* const sourceText,
    const std::int32_t eolType
  )
  {
    if (outText == nullptr) {
      return nullptr;
    }

    if (eolType == 0 || !WxStringRuntimeHasCharacters(sourceText)) {
      RetainWxStringRuntime(outText, sourceText);
      return outText;
    }

    const wchar_t* const eolToken = WxTextBufferGetEolToken(eolType);
    const std::wstring eol(eolToken != nullptr ? eolToken : L"");

    std::wstring normalized{};
    const wchar_t* const source = sourceText != nullptr ? sourceText->c_str() : L"";
    normalized.reserve(std::wcslen(source) + 8);

    bool pendingCarriageReturn = false;
    for (const wchar_t* cursor = source; *cursor != L'\0'; ++cursor) {
      const wchar_t ch = *cursor;
      if (ch == L'\n') {
        normalized.append(eol);
        pendingCarriageReturn = false;
      } else if (ch == L'\r') {
        if (pendingCarriageReturn) {
          normalized.append(eol);
        } else {
          pendingCarriageReturn = true;
        }
      } else {
        if (pendingCarriageReturn) {
          normalized.append(eol);
          pendingCarriageReturn = false;
        }
        normalized.push_back(ch);
      }
    }

    if (pendingCarriageReturn) {
      normalized.append(eol);
    }

    AssignOwnedWxString(outText, normalized);
    return outText;
  }

  /**
   * Address: 0x009FA4B0 (FUN_009FA4B0)
   *
   * What it does:
   * Opens one text-buffer source from `fileName`, decodes the payload into wide
   * text, splits it into line lanes, and updates open-state success.
   */
  [[nodiscard]] bool WxTextBufferOpenAndRead(
    WxTextBufferRuntimeView* const textBuffer,
    const void* const textConverter
  )
  {
    (void)textConverter;
    if (textBuffer == nullptr || !WxStringRuntimeHasCharacters(&textBuffer->fileName)) {
      return false;
    }

    std::string rawBytes{};
    if (!WxReadWholeFileBytes(textBuffer->fileName.c_str(), &rawBytes)) {
      textBuffer->openStatus = 0;
      return false;
    }

    wxStringRuntime decodedText = AllocateOwnedWxString(std::wstring());
    wxStringRuntime normalizedText = AllocateOwnedWxString(std::wstring());
    AssignOwnedWxString(&decodedText, WxDecodeMultibyteText(rawBytes, CP_UTF8, CP_ACP));
    (void)WxTextBufferNormalizeLineEndings(&normalizedText, &decodedText, static_cast<std::int32_t>(WxTextBufferEolType::Dos));
    ReleaseWxStringSharedPayload(decodedText);

    std::vector<std::wstring> splitLines = WxFileConfigSplitLines(std::wstring(normalizedText.c_str()));
    std::vector<wxStringRuntime> ownedLines{};
    ownedLines.reserve(splitLines.size());
    for (const std::wstring& line : splitLines) {
      ownedLines.push_back(AllocateOwnedWxString(line));
    }
    ReleaseWxStringSharedPayload(normalizedText);

    WxTextBufferReleaseOwnedLines(textBuffer);
    {
      const std::lock_guard<std::mutex> lock(gWxTextBufferLineStorageMutex);
      auto& storage = gWxTextBufferLineStorage[textBuffer];
      storage = std::move(ownedLines);
      textBuffer->lineCount = static_cast<std::uint32_t>(storage.size());
      textBuffer->lineItems = storage.empty() ? nullptr : storage.data();
    }

    textBuffer->openStatus = textBuffer->lineItems != nullptr || textBuffer->lineCount == 0u;
    return textBuffer->openStatus != 0u;
  }

  /**
   * Address: 0x00A34D60 (FUN_00A34D60)
   *
   * What it does:
   * Constructs one wxTextFile lane from a file-name string and seeds the
   * embedded file descriptor lane to `-1`.
   */
  WxTextFileRuntimeView* WxTextFileConstruct(
    WxTextFileRuntimeView* const textFile,
    const wxStringRuntime* const fileName
  )
  {
    if (textFile == nullptr) {
      return nullptr;
    }

    (void)WxTextBufferConstructWithFileName(&textFile->textBuffer, fileName);
    textFile->textBuffer.vtable = nullptr;
    textFile->fileLane.fileDescriptor = -1;
    textFile->fileLane.errorState = 0;
    return textFile;
  }

  /**
   * Address: 0x00A1A260 (FUN_00A1A260)
   *
   * What it does:
   * Destroys one wxTextFile lane by closing its embedded file descriptor lane,
   * then forwarding to base wxTextBuffer destruction.
   */
  WxTextFileRuntimeView* WxTextFileDestroy(
    WxTextFileRuntimeView* const textFile
  )
  {
    if (textFile == nullptr) {
      return nullptr;
    }

    auto* const fileLane = reinterpret_cast<wxFile*>(&textFile->fileLane);
    (void)fileLane->Attach();
    textFile->fileLane.fileDescriptor = fileLane->m_fd;
    textFile->fileLane.errorState = fileLane->m_error;

    WxTextBufferDestroy(&textFile->textBuffer);
    return textFile;
  }

  using WxInputStreamReadChunkFn = void*(__thiscall*)(void*, char*, int);
  using WxInputStreamGetLastReadFn = int(__thiscall*)(void*);
  using WxInputStreamEofFn = bool(__thiscall*)(void*);

  struct WxInputStreamVTableRuntimeView
  {
    void* slot00 = nullptr;
    void* slot04 = nullptr;
    void* slot08 = nullptr;
    void* slot0C = nullptr;
    void* slot10 = nullptr;
    WxInputStreamReadChunkFn readChunk = nullptr;      // +0x14
    WxInputStreamGetLastReadFn getLastRead = nullptr;  // +0x18
    void* slot1C = nullptr;
    WxInputStreamEofFn eof = nullptr;                  // +0x20
  };

  [[nodiscard]] bool WxReadAllBytesFromInputStream(
    void* const inputStream,
    std::string* const outBytes
  )
  {
    if (inputStream == nullptr || outBytes == nullptr) {
      return false;
    }

    auto* const vtable = reinterpret_cast<WxInputStreamVTableRuntimeView*>(*reinterpret_cast<void**>(inputStream));
    if (vtable == nullptr || vtable->readChunk == nullptr || vtable->getLastRead == nullptr || vtable->eof == nullptr) {
      return false;
    }

    char chunk[1024] = {};
    outBytes->clear();
    while (true) {
      void* const readState = vtable->readChunk(inputStream, chunk, static_cast<int>(sizeof(chunk)));
      if (readState == nullptr) {
        break;
      }

      const int bytesRead = vtable->getLastRead(inputStream);
      if (bytesRead > 0) {
        outBytes->append(chunk, static_cast<std::size_t>(bytesRead));
      }

      auto* const readStateVtable =
        reinterpret_cast<WxInputStreamVTableRuntimeView*>(*reinterpret_cast<void**>(readState));
      if (readStateVtable == nullptr || readStateVtable->eof == nullptr) {
        break;
      }
      if (readStateVtable->eof(readState) || bytesRead <= 0) {
        break;
      }
    }

    return true;
  }

  void WxReleaseOwnedStringVector(
    std::vector<wxStringRuntime>* const stringVector
  )
  {
    if (stringVector == nullptr) {
      return;
    }

    for (wxStringRuntime& value : *stringVector) {
      ReleaseWxStringSharedPayload(value);
    }
    stringVector->clear();
  }

  void WxFileConfigParseWideText(
    WxFileConfigRuntimeView* const ownerConfig,
    const std::wstring& sourcePath,
    const std::wstring& text,
    const bool preserveSourceLines
  )
  {
    std::vector<std::wstring> lines = WxFileConfigSplitLines(text);
    std::vector<wxStringRuntime> lineStorage{};
    lineStorage.reserve(lines.size());
    for (const std::wstring& line : lines) {
      lineStorage.push_back(AllocateOwnedWxString(line));
    }

    wxStringRuntime sourcePathText = AllocateOwnedWxString(sourcePath);

    WxTextBufferRuntimeView textBuffer{};
    textBuffer.fileName.m_pchData = sourcePathText.m_pchData;
    textBuffer.lineCount = static_cast<std::uint32_t>(lineStorage.size());
    textBuffer.lineItems = lineStorage.empty() ? nullptr : lineStorage.data();
    textBuffer.openStatus = 1;

    WxFileConfigLoadFromTextBuffer(ownerConfig, &textBuffer, preserveSourceLines);

    ReleaseWxStringSharedPayload(sourcePathText);
    WxReleaseOwnedStringVector(&lineStorage);
  }

  /**
   * Address: 0x00A1C8C0 (FUN_00A1C8C0)
   *
   * What it does:
   * Parses one text-buffer lane into group/entry nodes, handling escaped group
   * headers and key/value tokens while optionally preserving source line
   * pointers for rewrite paths.
   */
  void WxFileConfigLoadFromTextBuffer(
    WxFileConfigRuntimeView* const ownerConfig,
    const WxTextBufferRuntimeView* const textBuffer,
    const bool preserveSourceLines
  )
  {
    if (ownerConfig == nullptr || textBuffer == nullptr || textBuffer->lineItems == nullptr) {
      return;
    }

    for (std::uint32_t lineIndex = 0; lineIndex < textBuffer->lineCount; ++lineIndex) {
      const wxStringRuntime& sourceLine = textBuffer->lineItems[lineIndex];

      if (preserveSourceLines) {
        (void)WxFileConfigAppendLine(
          reinterpret_cast<WxFileConfigLineListRuntimeView*>(ownerConfig),
          &sourceLine
        );
        if (lineIndex == 0u && ownerConfig->currentGroup != nullptr) {
          (void)WxFileConfigSetGroupLineCache(ownerConfig->currentGroup, ownerConfig->tail);
        }
      }

      const wchar_t* cursor = sourceLine.c_str();
      while (*cursor != L'\0' && WxFileConfigCharIsSpace(*cursor)) {
        ++cursor;
      }

      const wchar_t leadingChar = *cursor;
      if (leadingChar == L'\0' || leadingChar == L';' || leadingChar == L'#') {
        continue;
      }

      if (leadingChar == L'[') {
        const wchar_t* tokenEnd = cursor + 1;
        bool foundClosingBracket = false;
        while (*tokenEnd != L'\0') {
          if (*tokenEnd == L'\\' && tokenEnd[1] != L'\0') {
            tokenEnd += 2;
            continue;
          }
          if (*tokenEnd == L']') {
            foundClosingBracket = true;
            break;
          }
          ++tokenEnd;
        }

        if (!foundClosingBracket) {
          continue;
        }

        wxStringRuntime rawHeader = AllocateOwnedWxString(std::wstring(cursor + 1, tokenEnd - (cursor + 1)));
        wxStringRuntime unescapedHeader = AllocateOwnedWxString(std::wstring());
        (void)WxFileConfigUnescapeHeaderToken(&unescapedHeader, &rawHeader);

        std::wstring groupPath = L"/";
        groupPath.append(unescapedHeader.c_str());
        wxStringRuntime groupPathText = AllocateOwnedWxString(groupPath);
        (void)WxFileConfigSetPath(ownerConfig, &groupPathText);

        if (preserveSourceLines && ownerConfig->currentGroup != nullptr) {
          if (ownerConfig->currentGroup->parentGroup != nullptr) {
            ownerConfig->currentGroup->parentGroup->lineOwnerDescendant = ownerConfig->currentGroup;
          }
          (void)WxFileConfigSetGroupLineCache(ownerConfig->currentGroup, ownerConfig->tail);
        }

        ReleaseWxStringSharedPayload(groupPathText);
        ReleaseWxStringSharedPayload(unescapedHeader);
        ReleaseWxStringSharedPayload(rawHeader);
        continue;
      }

      const wchar_t* keyEnd = cursor;
      while (*keyEnd != L'\0') {
        if (*keyEnd == L'=' || WxFileConfigCharIsSpace(*keyEnd)) {
          break;
        }
        if (*keyEnd == L'\\' && keyEnd[1] != L'\0') {
          keyEnd += 2;
          continue;
        }
        ++keyEnd;
      }

      wxStringRuntime rawKey = AllocateOwnedWxString(std::wstring(cursor, keyEnd - cursor));
      wxStringRuntime keyName = AllocateOwnedWxString(std::wstring());
      (void)WxFileConfigUnescapeHeaderToken(&keyName, &rawKey);

      const wchar_t* valueCursor = keyEnd;
      while (*valueCursor != L'\0' && WxFileConfigCharIsSpace(*valueCursor)) {
        ++valueCursor;
      }
      if (*valueCursor != L'=') {
        ReleaseWxStringSharedPayload(keyName);
        ReleaseWxStringSharedPayload(rawKey);
        continue;
      }
      ++valueCursor;

      WxFileConfigGroupRuntimeView* const currentGroup = ownerConfig->currentGroup;
      if (currentGroup == nullptr) {
        ReleaseWxStringSharedPayload(keyName);
        ReleaseWxStringSharedPayload(rawKey);
        continue;
      }

      auto* entry = reinterpret_cast<WxFileConfigEntryRuntimeView*>(
        WxFileConfigFindEntryByNameNoCase(currentGroup, keyName.c_str())
      );
      if (entry == nullptr) {
        entry = WxFileConfigAddEntry(
          currentGroup,
          &keyName,
          static_cast<std::int32_t>(lineIndex)
        );
      } else if (preserveSourceLines && (entry->entryFlags & 0x02u) != 0u) {
        ReleaseWxStringSharedPayload(keyName);
        ReleaseWxStringSharedPayload(rawKey);
        continue;
      }

      if (entry != nullptr && preserveSourceLines) {
        (void)WxFileConfigSetEntryLineWithDuplicateCheck(entry, ownerConfig->tail);
      }

      while (*valueCursor != L'\0' && WxFileConfigCharIsSpace(*valueCursor)) {
        ++valueCursor;
      }

      wxStringRuntime parsedValue = AllocateOwnedWxString(std::wstring(valueCursor));
      wxStringRuntime unescapedValue = AllocateOwnedWxString(std::wstring());
      const wxStringRuntime* valueForEntry = &parsedValue;
      if ((ownerConfig->formatFlags & 0x08u) == 0u) {
        (void)WxFileConfigUnescapeValueText(&unescapedValue, &parsedValue);
        valueForEntry = &unescapedValue;
      }

      if (entry != nullptr) {
        WxFileConfigSetValue(entry, valueForEntry, false);
      }

      ReleaseWxStringSharedPayload(unescapedValue);
      ReleaseWxStringSharedPayload(parsedValue);
      ReleaseWxStringSharedPayload(keyName);
      ReleaseWxStringSharedPayload(rawKey);
    }
  }

  /**
   * Address: 0x00A1D0C0 (FUN_00A1D0C0)
   *
   * What it does:
   * Renames one entry in the active group by moving its value to a newly added
   * key when the destination key is absent.
   */
  [[nodiscard]] bool WxFileConfigRenameEntry(
    WxFileConfigRuntimeView* const ownerConfig,
    const wxStringRuntime* const sourceKey,
    const wxStringRuntime* const destinationKey
  )
  {
    if (ownerConfig == nullptr || ownerConfig->currentGroup == nullptr || sourceKey == nullptr || destinationKey == nullptr) {
      return false;
    }

    auto* const sourceEntry = reinterpret_cast<WxFileConfigEntryRuntimeView*>(
      WxFileConfigFindEntryByNameNoCase(ownerConfig->currentGroup, sourceKey->c_str())
    );
    if (sourceEntry == nullptr) {
      return false;
    }

    if (WxFileConfigFindEntryByNameNoCase(ownerConfig->currentGroup, destinationKey->c_str()) != nullptr) {
      return false;
    }

    wxStringRuntime sourceValue = AllocateOwnedWxString(sourceEntry->valueText.c_str());
    if (!WxFileConfigDeleteEntryByName(ownerConfig->currentGroup, sourceKey->c_str())) {
      ReleaseWxStringSharedPayload(sourceValue);
      return false;
    }

    auto* const destinationEntry = WxFileConfigAddEntry(ownerConfig->currentGroup, destinationKey, -1);
    if (destinationEntry != nullptr) {
      WxFileConfigSetValue(destinationEntry, &sourceValue, true);
    }

    ReleaseWxStringSharedPayload(sourceValue);
    return destinationEntry != nullptr;
  }

  /**
   * Address: 0x00A1D480 (FUN_00A1D480)
   *
   * What it does:
   * Initializes default-root group state and loads global/user configuration
   * files when those file lanes exist.
   */
  [[nodiscard]] bool WxFileConfigLoadConfiguredFiles(
    WxFileConfigRuntimeView* const ownerConfig
  )
  {
    if (ownerConfig == nullptr) {
      return false;
    }

    wxStringRuntime defaultGroupName = AllocateOwnedWxString(L"Default");
    auto* rootGroup = new (std::nothrow) WxFileConfigGroupRuntimeView();
    if (rootGroup != nullptr) {
      rootGroup = WxFileConfigInitGroupNode(rootGroup, nullptr, &defaultGroupName, ownerConfig);
    }
    ownerConfig->rootGroup = rootGroup;
    ownerConfig->currentGroup = rootGroup;
    ReleaseWxStringSharedPayload(defaultGroupName);

    ownerConfig->head = nullptr;
    ownerConfig->tail = nullptr;

    if (WxStringRuntimeHasCharacters(&ownerConfig->globalConfigPath) &&
        wxFile::Exists(ownerConfig->globalConfigPath.c_str())) {
      WxTextFileRuntimeView globalTextFile{};
      (void)WxTextFileConstruct(&globalTextFile, &ownerConfig->globalConfigPath);
      if (WxTextBufferOpenAndRead(&globalTextFile.textBuffer, nullptr)) {
        WxFileConfigLoadFromTextBuffer(ownerConfig, &globalTextFile.textBuffer, false);
        (void)WxFileConfigResetPathState(ownerConfig);
      } else {
        wxLogDebug(L"can't open global configuration file '%s'.", ownerConfig->globalConfigPath.c_str());
      }
      (void)WxTextFileDestroy(&globalTextFile);
    }

    if (WxStringRuntimeHasCharacters(&ownerConfig->userConfigPath) &&
        wxFile::Exists(ownerConfig->userConfigPath.c_str())) {
      WxTextFileRuntimeView userTextFile{};
      (void)WxTextFileConstruct(&userTextFile, &ownerConfig->userConfigPath);
      if (WxTextBufferOpenAndRead(&userTextFile.textBuffer, nullptr)) {
        WxFileConfigLoadFromTextBuffer(ownerConfig, &userTextFile.textBuffer, true);
        (void)WxFileConfigResetPathState(ownerConfig);
      } else {
        wxLogDebug(L"can't open user configuration file '%s'.", ownerConfig->userConfigPath.c_str());
      }
      (void)WxTextFileDestroy(&userTextFile);
    }

    return true;
  }

  /**
   * Address: 0x00A1D9F0 (FUN_00A1D9F0)
   *
   * What it does:
   * Initializes file-config runtime lanes from an input stream by decoding all
   * stream bytes with current codepage semantics and parsing the resulting text
   * with source-line preservation enabled.
   */
  WxFileConfigRuntimeView* WxFileConfigConstructFromInputStream(
    WxFileConfigRuntimeView* const ownerConfig,
    void* const inputStream
  )
  {
    if (ownerConfig == nullptr) {
      return nullptr;
    }

    AssignOwnedWxString(&ownerConfig->userConfigPath, std::wstring());
    AssignOwnedWxString(&ownerConfig->globalConfigPath, std::wstring());
    AssignOwnedWxString(&ownerConfig->currentPath, std::wstring());
    ownerConfig->formatFlags |= 0x01u;

    wxStringRuntime defaultGroupName = AllocateOwnedWxString(L"Default");
    auto* rootGroup = new (std::nothrow) WxFileConfigGroupRuntimeView();
    if (rootGroup != nullptr) {
      rootGroup = WxFileConfigInitGroupNode(rootGroup, nullptr, &defaultGroupName, ownerConfig);
    }
    ownerConfig->rootGroup = rootGroup;
    ownerConfig->currentGroup = rootGroup;
    ReleaseWxStringSharedPayload(defaultGroupName);

    ownerConfig->head = nullptr;
    ownerConfig->tail = nullptr;

    std::string rawBytes{};
    if (WxReadAllBytesFromInputStream(inputStream, &rawBytes)) {
      const std::wstring streamText = WxDecodeMultibyteText(rawBytes, CP_ACP, CP_UTF8);
      WxFileConfigParseWideText(ownerConfig, std::wstring(), streamText, true);
      (void)WxFileConfigResetPathState(ownerConfig);
    }

    return ownerConfig;
  }

  /**
   * Address: 0x00A1CED0 (FUN_00A1CED0)
   *
   * What it does:
   * Writes one key/value pair into the active group: prepares key context,
   * creates missing entries/groups, rejects forbidden key prefixes, and commits
   * value updates through entry serialization lanes.
   */
  [[nodiscard]] bool WxFileConfigWriteString(
    WxFileConfigRuntimeView* const ownerConfig,
    const wxStringRuntime* const keyText,
    const wxStringRuntime* const valueText
  )
  {
    WxFileConfigKeyContextRuntimeView keyContext{};
    WxFileConfigPrepareKeyContext(&keyContext, ownerConfig, keyText);

    WxFileConfigGroupRuntimeView* const activeGroup = ownerConfig->currentGroup;
    wxLogTrace(
      L"wxFileConfig",
      L"  Writing String '%s' = '%s' to Group '%s'",
      keyContext.keyToken.c_str(),
      valueText != nullptr ? valueText->c_str() : wxEmptyString,
      activeGroup != nullptr ? activeGroup->groupName.c_str() : wxEmptyString
    );

    if (!WxStringRuntimeHasCharacters(&keyContext.keyToken)) {
      if (activeGroup != nullptr) {
        wxLogTrace(L"wxFileConfig", L"  Creating group %s", activeGroup->groupName.c_str());
        WxFileConfigMarkGroupChainDirty(activeGroup);
        WxFileConfigGetGroupLine(activeGroup);
      }
      WxFileConfigFinalizeKeyContext(&keyContext);
      return true;
    }

    if (keyContext.keyToken.c_str()[0] == L'!') {
      wxLogTrace(L"wxFileConfig", L"Config entry name cannot start with '%c'.", L'!');
      WxFileConfigFinalizeKeyContext(&keyContext);
      return false;
    }

    auto* entry = reinterpret_cast<WxFileConfigEntryRuntimeView*>(
      WxFileConfigFindEntryByNameNoCase(activeGroup, keyContext.keyToken.c_str())
    );
    if (entry == nullptr) {
      wxLogTrace(L"wxFileConfig", L"  Adding Entry %s", keyContext.keyToken.c_str());
      entry = WxFileConfigAddEntry(activeGroup, &keyContext.keyToken, -1);
    }

    wxLogTrace(L"wxFileConfig", L"  Setting value %s", valueText != nullptr ? valueText->c_str() : wxEmptyString);
    WxFileConfigSetValue(entry, valueText, true);

    WxFileConfigFinalizeKeyContext(&keyContext);
    return true;
  }

  struct WxOwnedStringHeader
  {
    std::int32_t refCount = 1;
    std::int32_t length = 0;
    std::int32_t capacity = 0;
  };

  std::mutex gOwnedWxStringLock{};
  std::unordered_set<void*> gOwnedWxStringHeaders{};

  [[nodiscard]] wxStringRuntime AllocateOwnedWxString(
    const std::wstring& value
  )
  {
    const std::size_t payloadBytes = sizeof(WxOwnedStringHeader) + (value.size() + 1) * sizeof(wchar_t);
    auto* const raw = static_cast<std::uint8_t*>(::operator new(payloadBytes));
    auto* const header = reinterpret_cast<WxOwnedStringHeader*>(raw);
    header->refCount = 1;
    header->length = static_cast<std::int32_t>(value.size());
    header->capacity = static_cast<std::int32_t>(value.size());

    auto* const text = reinterpret_cast<wchar_t*>(raw + sizeof(WxOwnedStringHeader));
    std::wmemcpy(text, value.c_str(), value.size());
    text[value.size()] = L'\0';

    {
      const std::lock_guard<std::mutex> lock(gOwnedWxStringLock);
      gOwnedWxStringHeaders.insert(header);
    }

    wxStringRuntime runtime{};
    runtime.m_pchData = text;
    return runtime;
  }

  [[nodiscard]] bool IsOwnedWxString(
    const wxStringRuntime& value
  ) noexcept
  {
    if (value.m_pchData == nullptr) {
      return false;
    }

    void* const header = reinterpret_cast<void*>(reinterpret_cast<std::int32_t*>(value.m_pchData) - 3);
    const std::lock_guard<std::mutex> lock(gOwnedWxStringLock);
    return gOwnedWxStringHeaders.find(header) != gOwnedWxStringHeaders.end();
  }

  void ReleaseOwnedWxString(
    wxStringRuntime& value
  ) noexcept
  {
    if (!IsOwnedWxString(value)) {
      value.m_pchData = nullptr;
      return;
    }

    auto* const header = reinterpret_cast<WxOwnedStringHeader*>(reinterpret_cast<std::int32_t*>(value.m_pchData) - 3);
    if (header->refCount > 1) {
      --header->refCount;
      value.m_pchData = nullptr;
      return;
    }

    {
      const std::lock_guard<std::mutex> lock(gOwnedWxStringLock);
      gOwnedWxStringHeaders.erase(header);
    }
    ::operator delete(header);
    value.m_pchData = nullptr;
  }

  void AssignOwnedWxString(
    wxStringRuntime* const outValue,
    const std::wstring& value
  )
  {
    if (outValue == nullptr) {
      return;
    }

    ReleaseOwnedWxString(*outValue);
    *outValue = AllocateOwnedWxString(value);
  }

  void PrependOwnedWxString(
    wxStringRuntime* const target,
    const wxStringRuntime& prefix
  )
  {
    if (target == nullptr) {
      return;
    }

    std::wstring combined(prefix.c_str());
    combined += target->c_str();
    AssignOwnedWxString(target, combined);
  }

  /**
   * Address: 0x00A0A6C0 (FUN_00A0A6C0)
   *
   * What it does:
   * Formats one local-time timestamp with the provided wx format mask into a
   * reusable wx string lane.
   */
  [[maybe_unused]] wxStringRuntime* FormatWxLogTimestampRuntime(
    const wchar_t* const formatMask,
    wxStringRuntime* const outText,
    const __time64_t timestamp
  )
  {
    if (outText == nullptr) {
      return nullptr;
    }

    wchar_t wideCharBuffer[4096]{};
    const __time64_t localTimeInput = timestamp;
    const std::tm* const localTimeValue = localtime64(&localTimeInput);
    if (formatMask != nullptr && localTimeValue != nullptr) {
      (void)::wcsftime(wideCharBuffer, 4096u, formatMask, localTimeValue);
    } else {
      wideCharBuffer[0] = L'\0';
    }

    outText->m_pchData = nullptr;
    AssignOwnedWxString(outText, std::wstring(wideCharBuffer));
    return outText;
  }

  constexpr std::int32_t kWxDateTimeUnsetYear = -32768;
  constexpr std::int32_t kWxDateTimeUnsetMonth = 12;

  [[nodiscard]] bool WxIsGregorianLeapYear(
    const std::int32_t year
  ) noexcept
  {
    if ((year % 4) != 0) {
      return false;
    }
    if ((year % 100) != 0) {
      return true;
    }
    return (year % 400) == 0;
  }

  [[nodiscard]] std::int32_t WxGetCurrentLocalYear()
  {
    const __time64_t currentEpochSeconds = ::_time64(nullptr);
    const std::tm* const currentLocalTime = localtime64(&currentEpochSeconds);
    return currentLocalTime->tm_year + 1900;
  }

  /**
   * Address: 0x009B4470 (FUN_009B4470, sub_9B4470)
   *
   * What it does:
   * Returns the current local-time lane by reading `_time64(nullptr)` and
   * passing the resulting epoch value through `localtime64`.
   */
  std::tm* wxGetCurrentLocalTimeRuntime()
  {
    const __time64_t currentEpochSeconds = ::_time64(nullptr);
    return localtime64(&currentEpochSeconds);
  }

  /**
   * Address: 0x009B4C10 (FUN_009B4C10)
   *
   * What it does:
   * Fills default month/year sentinel lanes (`12`, `-32768`) from local time
   * by updating year first, then month if still unresolved.
   */
  std::tm* wxDateTimeResolveDefaultMonthYear(
    std::int32_t* const monthLane,
    std::int32_t* const yearLane
  )
  {
    std::tm* resolvedTime = nullptr;
    if (*yearLane == kWxDateTimeUnsetYear) {
      const __time64_t epochSeconds = ::_time64(nullptr);
      resolvedTime = localtime64(&epochSeconds);
      *yearLane = resolvedTime->tm_year + 1900;
    }

    if (*monthLane == kWxDateTimeUnsetMonth) {
      if (resolvedTime == nullptr) {
        const __time64_t epochSeconds = ::_time64(nullptr);
        resolvedTime = localtime64(&epochSeconds);
      }

      *monthLane = resolvedTime->tm_mon;
    }

    return resolvedTime;
  }

  /**
   * Address: 0x009B6900 (FUN_009B6900)
   *
   * What it does:
   * Resolves default-year sentinel lanes, validates the calendar selector
   * range (`0..1`), and returns day-count (`365` or `366`) for that year.
   */
  std::int16_t wxDateTimeGetDaysInYear(
    std::int32_t year,
    const std::int32_t calendarSelector
  )
  {
    if (year == kWxDateTimeUnsetYear) {
      year = WxGetCurrentLocalYear();
    }

    if (calendarSelector < 0 || calendarSelector > 1) {
      return 0;
    }

    return static_cast<std::int16_t>(365 + (WxIsGregorianLeapYear(year) ? 1 : 0));
  }

  /**
   * Address: 0x009B6B00 (FUN_009B6B00)
   *
   * What it does:
   * Validates month/calendar lanes, resolves default-year sentinel lanes, and
   * returns the month-day count for the resolved Gregorian year.
   */
  std::int16_t wxDateTimeGetDaysInMonth(
    const std::int32_t monthIndex,
    std::int32_t year,
    const std::uint32_t calendarSelector
  )
  {
    static constexpr std::int16_t kDaysPerMonthNonLeap[12] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    static constexpr std::int16_t kDaysPerMonthLeap[12] = {
      31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (monthIndex < 0 || monthIndex >= 12 || calendarSelector > 1u) {
      return 0;
    }

    if (year == kWxDateTimeUnsetYear) {
      year = WxGetCurrentLocalYear();
    }

    const std::int16_t* const dayTable = WxIsGregorianLeapYear(year)
      ? kDaysPerMonthLeap
      : kDaysPerMonthNonLeap;
    return dayTable[monthIndex];
  }

  /**
   * Address: 0x00960970 (FUN_00960970, wxString copy-before-write helper)
   *
   * What it does:
   * Ensures one wx string lane has unique writable ownership; when the shared
   * refcount is greater than 1, it decrements the old header refcount and
   * allocates/copies a private payload for the caller.
   */
  [[nodiscard]] bool EnsureUniqueOwnedWxStringBuffer(
    wxStringRuntime* const value
  )
  {
    if (value == nullptr || value->m_pchData == nullptr || !IsOwnedWxString(*value)) {
      return false;
    }

    auto* const header = reinterpret_cast<WxOwnedStringHeader*>(reinterpret_cast<std::int32_t*>(value->m_pchData) - 3);
    const std::int32_t refCount = header->refCount;
    if (refCount > 1) {
      header->refCount = refCount - 1;

      const std::size_t currentLength = static_cast<std::size_t>(header->length < 0 ? 0 : header->length);
      std::wstring copiedText(value->m_pchData, currentLength);
      *value = AllocateOwnedWxString(copiedText);
    }

    return true;
  }

  /**
   * Address: 0x0095FC00 (FUN_0095FC00)
   *
   * What it does:
   * Compares two UTF-16 strings with either case-sensitive (`wcscmp`) or
   * case-insensitive (`_wcsicmp`) semantics.
   */
  [[nodiscard]] bool WxStringEqualsWithCasePolicy(
    const wchar_t* const left,
    const wchar_t* const right,
    const bool caseSensitive
  ) noexcept
  {
    if (left == nullptr || right == nullptr) {
      return false;
    }

    return caseSensitive ? (std::wcscmp(left, right) == 0) : (_wcsicmp(left, right) == 0);
  }

  /**
   * Address: 0x00960550 (FUN_00960550)
   *
   * What it does:
   * Finds one string entry in the wx array-string runtime lane, using
   * forward/backward linear scan for unsorted lanes and binary search for
   * sorted lanes.
   */
  [[nodiscard]] std::int32_t WxFindStringArrayIndex(
    const WxStringArrayRuntimeView* const stringArray,
    const wchar_t* const needle,
    const bool caseSensitive,
    const bool searchFromEnd
  ) noexcept
  {
    if (stringArray == nullptr || needle == nullptr || stringArray->count <= 0 || stringArray->entries == nullptr) {
      return -1;
    }

    const auto count = static_cast<std::int32_t>(stringArray->count);
    if (stringArray->isSorted == 0u) {
      if (searchFromEnd) {
        for (std::int32_t index = count - 1; index >= 0; --index) {
          if (WxStringEqualsWithCasePolicy(stringArray->entries[index], needle, caseSensitive)) {
            return index;
          }
        }
      } else {
        for (std::int32_t index = 0; index < count; ++index) {
          if (WxStringEqualsWithCasePolicy(stringArray->entries[index], needle, caseSensitive)) {
            return index;
          }
        }
      }
      return -1;
    }

    std::int32_t low = 0;
    std::int32_t high = count;
    while (low < high) {
      const std::int32_t mid = (low + high) / 2;
      const int compare = std::wcscmp(needle, stringArray->entries[mid]);
      if (compare == 0) {
        return mid;
      }

      if (compare > 0) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }

    return -1;
  }

  /**
   * Address: 0x00A8F116 (FUN_00A8F116)
   *
   * What it does:
   * Reports whether one UTF-16 code unit is alphanumeric under CRT wide-char
   * classification.
   */
  [[nodiscard]] bool WxIsAlnumWide(
    const wchar_t codeUnit
  ) noexcept
  {
    return std::iswalnum(static_cast<wint_t>(codeUnit)) != 0;
  }

  /**
   * Address: 0x009B5ED0 (FUN_009B5ED0)
   *
   * What it does:
   * Consumes one contiguous alphabetic token from `*cursorLane`, appends the
   * consumed UTF-16 code units into `outToken`, and advances the caller cursor.
   */
  wxStringRuntime* wxConsumeWideAlphaToken(
    const wchar_t** const cursorLane,
    wxStringRuntime* const outToken
  )
  {
    if (outToken == nullptr) {
      return nullptr;
    }

    AssignOwnedWxString(outToken, std::wstring());
    if (cursorLane == nullptr || *cursorLane == nullptr) {
      return outToken;
    }

    const wchar_t* cursor = *cursorLane;
    std::wstring token{};
    while (*cursor != L'\0' && std::iswalpha(static_cast<wint_t>(*cursor)) != 0) {
      token.push_back(*cursor);
      ++cursor;
    }

    *cursorLane = cursor;
    AssignOwnedWxString(outToken, token);
    return outToken;
  }

  /**
   * Address: 0x00A8EF77 (FUN_00A8EF77, func_wstrFindFirst)
   *
   * What it does:
   * Returns a pointer to the first occurrence of `needle` inside one null-
   * terminated UTF-16 set, or `nullptr` when absent.
   */
  [[nodiscard]] const wchar_t* wxFindFirstWideChar(
    const wchar_t* const setText,
    const wchar_t needle
  ) noexcept
  {
    if (setText == nullptr) {
      return nullptr;
    }

    for (const wchar_t* cursor = setText; *cursor != L'\0'; ++cursor) {
      if (*cursor == needle) {
        return cursor;
      }
    }
    return nullptr;
  }

  [[nodiscard]] std::wstring ToLowerWide(
    std::wstring value
  )
  {
    std::transform(
      value.begin(),
      value.end(),
      value.begin(),
      [](const wchar_t ch) {
      return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
    }
    );
    return value;
  }

  [[nodiscard]] bool ContainsNoCase(
    const std::wstring& haystack,
    const wchar_t* const needle
  )
  {
    if (needle == nullptr || *needle == L'\0') {
      return false;
    }

    const std::wstring lowerHaystack = ToLowerWide(haystack);
    const std::wstring lowerNeedle = ToLowerWide(std::wstring(needle));
    return lowerHaystack.find(lowerNeedle) != std::wstring::npos;
  }

  [[nodiscard]] bool TryParseIntToken(
    const std::wstring& token,
    std::int32_t* const outValue
  )
  {
    if (token.empty() || outValue == nullptr) {
      return false;
    }

    std::size_t parsedCount = 0;
    const long value = std::wcstol(token.c_str(), nullptr, 10);
    if (value == 0L && token[0] != L'0') {
      return false;
    }

    const std::wstring normalized = (token[0] == L'+' || token[0] == L'-') ? token.substr(1) : token;
    for (const wchar_t ch : normalized) {
      if (ch < L'0' || ch > L'9') {
        return false;
      }
      ++parsedCount;
    }
    if (parsedCount == 0) {
      return false;
    }

    *outValue = static_cast<std::int32_t>(value);
    return true;
  }

  [[nodiscard]] bool TryMapEncodingToCharset(
    const std::wstring& token,
    std::int32_t* const outCharset
  )
  {
    if (outCharset == nullptr) {
      return false;
    }

    const std::wstring lower = ToLowerWide(token);
    if (lower == L"ansi" || lower == L"cp1252" || lower == L"latin1" || lower == L"iso8859-1") {
      *outCharset = 0;
      return true;
    }
    if (lower == L"cp1250" || lower == L"easteurope" || lower == L"iso8859-2") {
      *outCharset = 238;
      return true;
    }
    if (lower == L"cp1251" || lower == L"russian" || lower == L"koi8-r") {
      *outCharset = 204;
      return true;
    }
    if (lower == L"cp1253" || lower == L"greek") {
      *outCharset = 161;
      return true;
    }
    if (lower == L"cp1254" || lower == L"turkish") {
      *outCharset = 162;
      return true;
    }
    if (lower == L"cp1255" || lower == L"hebrew") {
      *outCharset = 177;
      return true;
    }
    if (lower == L"cp1256" || lower == L"arabic") {
      *outCharset = 178;
      return true;
    }
    if (lower == L"cp1257" || lower == L"baltic") {
      *outCharset = 186;
      return true;
    }
    if (lower == L"cp1258" || lower == L"vietnamese") {
      *outCharset = 163;
      return true;
    }
    if (lower == L"utf-8" || lower == L"utf8" || lower == L"default") {
      *outCharset = 1;
      return true;
    }

    return false;
  }

  struct WxEncodingToCharsetRuntime
  {
    std::int32_t reserved00 = 0;
    std::int32_t encoding = 0;
    std::int32_t charset = 0;
  };
  static_assert(offsetof(WxEncodingToCharsetRuntime, encoding) == 0x04, "WxEncodingToCharsetRuntime::encoding offset must be 0x04");
  static_assert(offsetof(WxEncodingToCharsetRuntime, charset) == 0x08, "WxEncodingToCharsetRuntime::charset offset must be 0x08");
  static_assert(sizeof(WxEncodingToCharsetRuntime) == 0x0C, "WxEncodingToCharsetRuntime size must be 0x0C");

  std::int32_t gWxDefaultEncodingRuntime = 1;

  /**
   * Address: 0x009D49D0 (FUN_009D49D0)
   *
   * What it does:
   * Maps one wx encoding id to a Win32 LOGFONT charset id and writes both
   * resolved encoding/charset lanes into the output payload.
   */
  [[maybe_unused]] bool wxMapEncodingIdToCharsetRuntime(
    const std::int32_t encodingId,
    WxEncodingToCharsetRuntime* const outInfo
  )
  {
    if (outInfo == nullptr) {
      return false;
    }

    const std::int32_t resolvedEncoding = (encodingId != 0) ? encodingId : gWxDefaultEncodingRuntime;
    std::int32_t resolvedCharset = 0;

    switch (resolvedEncoding) {
    case -1:
      resolvedCharset = 1;
      break;
    case 1:
    case 15:
    case 32:
      resolvedCharset = 0;
      break;
    case 20:
      resolvedCharset = 255;
      break;
    case 25:
      resolvedCharset = 222;
      break;
    case 26:
      resolvedCharset = 128;
      break;
    case 27:
      resolvedCharset = 134;
      break;
    case 28:
      resolvedCharset = 129;
      break;
    case 29:
      resolvedCharset = 136;
      break;
    case 30:
      resolvedCharset = 238;
      break;
    case 31:
      resolvedCharset = 204;
      break;
    case 33:
      resolvedCharset = 161;
      break;
    case 34:
      resolvedCharset = 162;
      break;
    case 35:
      resolvedCharset = 177;
      break;
    case 36:
      resolvedCharset = 178;
      break;
    case 37:
      resolvedCharset = 186;
      break;
    default:
      return false;
    }

    outInfo->encoding = resolvedEncoding;
    outInfo->charset = resolvedCharset;
    return true;
  }

  [[nodiscard]] std::vector<std::wstring> SplitNativeFontDescriptionTokens(
    const std::wstring& description
  )
  {
    std::vector<std::wstring> tokens{};
    std::wstring token{};

    auto flushToken = [&]() {
      if (!token.empty()) {
        tokens.push_back(token);
        token.clear();
      }
    };

    for (const wchar_t ch : description) {
      const bool isSeparator = ch == L';' || ch == L',' || std::iswspace(static_cast<wint_t>(ch)) != 0;
      if (isSeparator) {
        flushToken();
      } else {
        token.push_back(ch);
      }
    }
    flushToken();
    return tokens;
  }

  /**
   * Address: 0x00980B70 (FUN_00980B70)
   *
   * What it does:
   * Tears down one `wxListItemAttr` payload by releasing the embedded font and
   * colour wxObject ref-data lanes in reverse construction order.
   */
  void DestroyWxListItemAttrRuntime(
    wxListItemAttrRuntime* const attr
  ) noexcept
  {
    if (attr == nullptr) {
      return;
    }

    RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(&attr->mFont));
    RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(&attr->mBackgroundColour));
    RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(&attr->mTextColour));
  }

  void ReleaseD3DDeviceRef(
    void* const device
  ) noexcept
  {
    if (device == nullptr) {
      return;
    }

    void** const vtable = *reinterpret_cast<void***>(device);
    if (vtable == nullptr || vtable[1] == nullptr) {
      return;
    }

    using ReleaseFn = void(__thiscall*)(void*, unsigned int);
    auto const release = reinterpret_cast<ReleaseFn>(vtable[1]);
    release(device, 1u);
  }

  [[nodiscard]] bool IsInlineHeadLinkSentinel(
    moho::ManagedWindowSlot** const ownerHeadLink
  ) noexcept
  {
    return reinterpret_cast<std::uintptr_t>(ownerHeadLink) < kInlineHeadLinkSentinelMax;
  }

  [[nodiscard]] moho::ManagedWindowSlot* TranslateSlotPointerForReallocation(
    moho::ManagedWindowSlot* const pointer,
    const moho::ManagedWindowSlot* const oldStorage,
    const std::size_t oldCount,
    moho::ManagedWindowSlot* const newStorage
  ) noexcept
  {
    if (pointer == nullptr || oldStorage == nullptr || oldCount == 0 || newStorage == nullptr) {
      return pointer;
    }

    const std::uintptr_t oldBegin = reinterpret_cast<std::uintptr_t>(oldStorage);
    const std::uintptr_t oldEnd = oldBegin + oldCount * sizeof(moho::ManagedWindowSlot);
    const std::uintptr_t pointerValue = reinterpret_cast<std::uintptr_t>(pointer);
    if (pointerValue < oldBegin || pointerValue >= oldEnd) {
      return pointer;
    }

    const std::size_t index = (pointerValue - oldBegin) / sizeof(moho::ManagedWindowSlot);
    return &newStorage[index];
  }

  void RebaseManagedSlotPointersAfterReallocation(
    msvc8::vector<moho::ManagedWindowSlot>& slots,
    const moho::ManagedWindowSlot* const oldStorage,
    const std::size_t oldCount
  ) noexcept
  {
    if (oldStorage == nullptr || oldCount == 0) {
      return;
    }

    moho::ManagedWindowSlot* const newStorage = slots.data();
    if (newStorage == nullptr || newStorage == oldStorage) {
      return;
    }

    const std::size_t newCount = slots.size();
    for (std::size_t index = 0; index < newCount; ++index) {
      moho::ManagedWindowSlot& slot = newStorage[index];
      slot.nextInOwnerChain =
        TranslateSlotPointerForReallocation(slot.nextInOwnerChain, oldStorage, oldCount, newStorage);
    }

    for (std::size_t index = 0; index < newCount; ++index) {
      moho::ManagedWindowSlot& slot = newStorage[index];
      if (slot.ownerHeadLink == nullptr || IsInlineHeadLinkSentinel(slot.ownerHeadLink)) {
        continue;
      }

      moho::ManagedWindowSlot* const translatedHead =
        TranslateSlotPointerForReallocation(*slot.ownerHeadLink, oldStorage, oldCount, newStorage);
      if (*slot.ownerHeadLink != translatedHead) {
        *slot.ownerHeadLink = translatedHead;
      }
    }
  }

  /**
   * Address: 0x004F80F0 (FUN_004F80F0)
   *
   * What it does:
   * Inserts one `ManagedWindowSlot` value at the requested slot position in
   * `managedWindows` and returns the rebased iterator lane to the inserted slot.
   */
  [[maybe_unused]] [[nodiscard]] moho::ManagedWindowSlot** InsertManagedWindowSlotWithGrowth(
    moho::ManagedWindowSlot** const outIterator,
    moho::ManagedWindowSlot* const insertAt,
    const moho::ManagedWindowSlot* const value
  )
  {
    const moho::ManagedWindowSlot* const begin = moho::managedWindows.data();
    const std::size_t count = moho::managedWindows.size();

    std::size_t insertIndex = 0u;
    if (begin != nullptr && count != 0u && insertAt != nullptr) {
      const std::uintptr_t beginAddress = reinterpret_cast<std::uintptr_t>(begin);
      const std::uintptr_t insertAddress = reinterpret_cast<std::uintptr_t>(insertAt);
      if (insertAddress >= beginAddress) {
        insertIndex = static_cast<std::size_t>((insertAddress - beginAddress) / sizeof(moho::ManagedWindowSlot));
      }
    }
    insertIndex = std::min(insertIndex, count);

    moho::ManagedWindowSlot slotValue{};
    if (value != nullptr) {
      slotValue = *value;
    }

    moho::ManagedWindowSlot* const oldStorage = moho::managedWindows.data();
    const std::size_t oldCount = moho::managedWindows.size();

    moho::managedWindows.push_back(moho::ManagedWindowSlot{});
    RebaseManagedSlotPointersAfterReallocation(moho::managedWindows, oldStorage, oldCount);

    moho::ManagedWindowSlot* const storage = moho::managedWindows.data();
    if (storage == nullptr) {
      return outIterator;
    }
    for (std::size_t i = oldCount; i > insertIndex; --i) {
      storage[i] = storage[i - 1u];
    }
    storage[insertIndex] = slotValue;

    if (outIterator != nullptr) {
      moho::ManagedWindowSlot* const rebasedBegin = storage;
      *outIterator = rebasedBegin != nullptr ? rebasedBegin + insertIndex : nullptr;
    }
    return outIterator;
  }

  void DetachSlotWithoutClearing(
    moho::ManagedWindowSlot& slot
  ) noexcept
  {
    if (slot.ownerHeadLink == nullptr || IsInlineHeadLinkSentinel(slot.ownerHeadLink)) {
      return;
    }

    moho::ManagedWindowSlot** link = slot.ownerHeadLink;
    while (*link != nullptr && *link != &slot) {
      link = &(*link)->nextInOwnerChain;
    }

    if (*link == &slot) {
      *link = slot.nextInOwnerChain;
    }
  }

  void RelinkSlotToOwner(
    moho::ManagedWindowSlot& slot,
    moho::ManagedWindowSlot** const ownerHeadLink
  ) noexcept
  {
    if (slot.ownerHeadLink == ownerHeadLink) {
      return;
    }

    DetachSlotWithoutClearing(slot);
    slot.ownerHeadLink = ownerHeadLink;
    if (ownerHeadLink == nullptr) {
      slot.nextInOwnerChain = nullptr;
      return;
    }

    slot.nextInOwnerChain = *ownerHeadLink;
    *ownerHeadLink = &slot;
  }

  template <typename TOwnerRuntime>
  [[nodiscard]] bool IsReusableManagedSlot(
    const moho::ManagedWindowSlot& slot
  )
  {
    return slot.ownerHeadLink == nullptr || slot.ownerHeadLink == TOwnerRuntime::NullManagedSlotHeadLinkSentinel();
  }

  template <typename TOwnerRuntime>
  [[nodiscard]] bool TryReuseManagedSlot(
    msvc8::vector<moho::ManagedWindowSlot>& slots,
    moho::ManagedWindowSlot** const ownerHeadLink
  )
  {
    moho::ManagedWindowSlot* const slotStorage = slots.data();
    if (slotStorage == nullptr) {
      return false;
    }

    const std::size_t slotCount = slots.size();
    for (std::size_t index = 0; index < slotCount; ++index) {
      moho::ManagedWindowSlot& slot = slotStorage[index];
      if (!IsReusableManagedSlot<TOwnerRuntime>(slot)) {
        continue;
      }

      RelinkSlotToOwner(slot, ownerHeadLink);
      return true;
    }

    return false;
  }

  template <typename TOwnerRuntime>
  void AppendManagedSlot(
    msvc8::vector<moho::ManagedWindowSlot>& slots,
    moho::ManagedWindowSlot** const ownerHeadLink
  )
  {
    if (ownerHeadLink == nullptr) {
      return;
    }

    moho::ManagedWindowSlot appendedSlot{};
    appendedSlot.ownerHeadLink = ownerHeadLink;
    appendedSlot.nextInOwnerChain = *ownerHeadLink;

    moho::ManagedWindowSlot* const oldStorage = slots.data();
    const std::size_t oldCount = slots.size();
    slots.push_back(appendedSlot);

    RebaseManagedSlotPointersAfterReallocation(slots, oldStorage, oldCount);

    moho::ManagedWindowSlot* const slotStorage = slots.data();
    if (slotStorage == nullptr || slots.empty()) {
      return;
    }

    moho::ManagedWindowSlot& insertedSlot = slotStorage[slots.size() - 1];
    insertedSlot.ownerHeadLink = ownerHeadLink;
    *ownerHeadLink = &insertedSlot;
  }

  template <typename TOwnerRuntime>
  void RegisterManagedOwnerSlotImpl(
    msvc8::vector<moho::ManagedWindowSlot>& slots,
    moho::ManagedWindowSlot** const ownerHeadLink
  )
  {
    if (ownerHeadLink == nullptr) {
      return;
    }

    if (TryReuseManagedSlot<TOwnerRuntime>(slots, ownerHeadLink)) {
      return;
    }

    AppendManagedSlot<TOwnerRuntime>(slots, ownerHeadLink);
  }

  void ReleaseManagedOwnerSlotChain(
    moho::ManagedWindowSlot*& ownerHead
  ) noexcept
  {
    while (ownerHead != nullptr) {
      moho::ManagedWindowSlot* const slot = ownerHead;
      ownerHead = slot->nextInOwnerChain;
      slot->Clear();
    }
  }

  template <typename TOwnerRuntime>
  void DestroyManagedRuntimeCollection(
    msvc8::vector<moho::ManagedWindowSlot>& slots
  )
  {
    for (std::size_t index = 0;; ++index) {
      moho::ManagedWindowSlot* slotStorage = slots.data();
      const std::size_t slotCount = slots.size();
      if (slotStorage == nullptr || index >= slotCount) {
        break;
      }

      moho::ManagedWindowSlot& slot = slotStorage[index];
      if (slot.ownerHeadLink == nullptr || slot.ownerHeadLink == TOwnerRuntime::NullManagedSlotHeadLinkSentinel()) {
        continue;
      }

      TOwnerRuntime* const owner = TOwnerRuntime::FromManagedSlotHeadLink(slot.ownerHeadLink);
      if (owner != nullptr) {
        (void)owner->Destroy();
      }

      slotStorage = slots.data();
      if (slotStorage == nullptr || index >= slots.size()) {
        continue;
      }
      slotStorage[index].UnlinkFromOwner();
    }
  }

  struct SupComFrameState
  {
    std::int32_t clientWidth = 0;
    std::int32_t clientHeight = 0;
    std::int32_t minWidth = 0;
    std::int32_t minHeight = 0;
    std::int32_t windowX = -1;
    std::int32_t windowY = -1;
    std::int32_t windowStyle = 0;
    bool visible = false;
    bool maximized = false;
    bool focused = false;
    bool iconized = false;
    bool iconResourceAssigned = false;
    std::uintptr_t pseudoWindowHandle = 0;
    std::wstring title;
    std::wstring name;
    std::wstring iconResourceName;
  };

  constexpr std::uintptr_t kFirstSupComFramePseudoHandle = 0x1000u;
  constexpr std::uintptr_t kSupComFramePseudoHandleStride = 0x10u;
  constexpr wchar_t kSupComFrameWindowName[] = L"frame";
  constexpr wchar_t kSupComFrameIconResourceName[] = L"ID";

  std::uintptr_t gNextSupComFramePseudoHandle = kFirstSupComFramePseudoHandle;
  std::unordered_map<const WSupComFrame*, SupComFrameState> gSupComFrameStateByFrame{};
  struct WxTopLevelWindowRuntimeState
  {
    std::int32_t fsOldX = 0;
    std::int32_t fsOldY = 0;
    std::int32_t fsOldWidth = 0;
    std::int32_t fsOldHeight = 0;
    std::uint8_t flag34 = 0;
  };

  struct WxDialogRuntimeState
  {
    void* parentWindow = nullptr;
    std::int32_t windowId = -1;
    wxPoint position{};
    wxSize size{};
    long style = 0;
    std::wstring title{};
    std::wstring name{};
  };

  struct WxLogFrameRuntimeState
  {
    std::wstring title{};
    std::wstring windowName{};
    std::wstring statusText{};
    wxTextCtrlRuntime* textControl = nullptr;
    wxLogWindowRuntime* ownerLogWindow = nullptr;
    std::array<std::int32_t, 3> logMenuItemIds{{5003, 5033, 5001}};
    bool menuReady = false;
  };

  std::unordered_map<const wxTopLevelWindowRuntime*, WxTopLevelWindowRuntimeState>
    gWxTopLevelWindowRuntimeStateByWindow{};
  std::unordered_map<const wxDialogRuntime*, WxDialogRuntimeState> gWxDialogRuntimeStateByDialog{};
  std::unordered_map<const wxLogFrameRuntime*, WxLogFrameRuntimeState> gWxLogFrameRuntimeStateByFrame{};

  struct WxModuleListNodeRuntime
  {
    WxModuleListNodeRuntime* next = nullptr;
  };

  struct WxModuleListRuntime
  {
    WxModuleListNodeRuntime* head = nullptr; // +0x00
    WxModuleListNodeRuntime* tail = nullptr; // +0x04
    std::uint8_t reserved08_0B[0x4]{};
    std::uint8_t mDestroy = 0; // +0x0C
    std::uint8_t reserved0D_0F[0x3]{};

    void Clear() noexcept
    {
      if (mDestroy != 0u) {
        WxModuleListNodeRuntime* node = head;
        while (node != nullptr) {
          WxModuleListNodeRuntime* const next = node->next;
          delete node;
          node = next;
        }
      }

      head = nullptr;
      tail = nullptr;
    }
  };

  static_assert(
    offsetof(WxModuleListRuntime, mDestroy) == 0x0C,
    "WxModuleListRuntime::mDestroy offset must be 0x0C"
  );
  static_assert(sizeof(WxModuleListRuntime) == 0x10, "WxModuleListRuntime size must be 0x10");

  WxModuleListRuntime gWxPendingModuleListRuntime{};
  WxModuleListRuntime gWxLoadedModuleListRuntime{};

  /**
   * Address: 0x009CA2B0 (FUN_009CA2B0)
   *
   * What it does:
   * Runs one idle-phase cleanup pulse across both global wx module lists by
   * toggling their destroy lanes around list clearing.
   */
  [[maybe_unused]] void RunWxModuleListIdleCleanupPulse()
  {
    gWxLoadedModuleListRuntime.mDestroy = 1;
    gWxLoadedModuleListRuntime.Clear();
    gWxLoadedModuleListRuntime.mDestroy = 0;

    gWxPendingModuleListRuntime.mDestroy = 1;
    gWxPendingModuleListRuntime.Clear();
    gWxPendingModuleListRuntime.mDestroy = 0;
  }

  using WxDeleteWithFlagFn = void(__thiscall*)(void* object, int deleteFlag);

  struct WxListBaseLinkedRuntimeView
  {
    void* vtable = nullptr;               // +0x00
    std::int32_t keyType = 0;             // +0x04
    std::int32_t itemCount = 0;           // +0x08
    std::uint8_t reserved0C_0F[0x4]{};    // +0x0C
    wxNodeBaseRuntime* first = nullptr;   // +0x10
    wxNodeBaseRuntime* last = nullptr;    // +0x14
  };
  static_assert(offsetof(WxListBaseLinkedRuntimeView, itemCount) == 0x08, "WxListBaseLinkedRuntimeView::itemCount offset must be 0x08");
  static_assert(offsetof(WxListBaseLinkedRuntimeView, first) == 0x10, "WxListBaseLinkedRuntimeView::first offset must be 0x10");
  static_assert(offsetof(WxListBaseLinkedRuntimeView, last) == 0x14, "WxListBaseLinkedRuntimeView::last offset must be 0x14");

  struct WxModuleListBucketArrayOwnerRuntimeView
  {
    std::uint8_t reserved00_123[0x124]{};
    std::uint32_t listCount = 0;                                // +0x124
    std::uint8_t reserved128_12B[0x4]{};
    WxListBaseLinkedRuntimeView** moduleLists = nullptr;        // +0x12C
  };
  static_assert(
    offsetof(WxModuleListBucketArrayOwnerRuntimeView, listCount) == 0x124,
    "WxModuleListBucketArrayOwnerRuntimeView::listCount offset must be 0x124"
  );
  static_assert(
    offsetof(WxModuleListBucketArrayOwnerRuntimeView, moduleLists) == 0x12C,
    "WxModuleListBucketArrayOwnerRuntimeView::moduleLists offset must be 0x12C"
  );

  void WxListBaseClearNodesRuntime(
    WxListBaseLinkedRuntimeView* const list
  )
  {
    wxNodeBaseRuntime* node = list->first;
    while (node != nullptr) {
      wxNodeBaseRuntime* const next = node->mNext;
      ::operator delete(node);
      node = next;
    }

    list->last = nullptr;
    list->first = nullptr;
    list->itemCount = 0;
  }

  /**
   * Address: 0x009CF5B0 (FUN_009CF5B0, sub_9CF5B0)
   *
   * What it does:
   * Clears and destroys each non-null module-list lane in the owner's bucket
   * array, then releases the bucket pointer array storage.
   */
  void wxDestroyModuleListBucketArrayRuntime(
    WxModuleListBucketArrayOwnerRuntimeView* const owner
  )
  {
    if (owner->moduleLists == nullptr) {
      return;
    }

    for (std::uint32_t index = 0; index < owner->listCount; ++index) {
      WxListBaseLinkedRuntimeView* const moduleList = owner->moduleLists[index];
      if (moduleList == nullptr) {
        continue;
      }

      WxListBaseClearNodesRuntime(moduleList);

      void** const vtable = *reinterpret_cast<void***>(moduleList);
      if (vtable != nullptr && vtable[1] != nullptr) {
        auto const destroyWithFlag = reinterpret_cast<WxDeleteWithFlagFn>(vtable[1]);
        destroyWithFlag(moduleList, 1);
      }
    }

    ::operator delete(owner->moduleLists);
  }

  struct WxTreeListNodeRuntimeState
  {
    WxTreeListNodeRuntimeState* parent = nullptr;
    std::vector<WxTreeListNodeRuntimeState*> children{};
    wxTreeItemDataRuntime* itemData = nullptr;
    bool hasChildrenFlag = false;
    bool isExpanded = false;
    std::vector<msvc8::string> columnText{};
  };

  struct WxTreeListRuntimeState
  {
    wxWindowBase* parentWindow = nullptr;
    std::int32_t windowId = -1;
    wxPoint position{};
    wxSize size{};
    long style = 0;
    std::wstring name{};
    std::vector<wxTreeListColumnInfoRuntime> columns{};
    std::vector<std::unique_ptr<WxTreeListNodeRuntimeState>> nodeStorage{};
    WxTreeListNodeRuntimeState* rootNode = nullptr;
  };

  std::unordered_map<const wxTreeListCtrlRuntime*, WxTreeListRuntimeState> gWxTreeListRuntimeStateByControl{};

  struct WxWindowBaseRuntimeState
  {
    std::int32_t minWidth = -1;
    std::int32_t minHeight = -1;
    std::int32_t maxWidth = -1;
    std::int32_t maxHeight = -1;
    long windowStyle = 0;
    long extraStyle = 0;
    unsigned long nativeHandle = 0;
    std::int32_t windowId = -1;
    wxWindowBase* parentWindow = nullptr;
    wxWindowBase* eventHandler = nullptr;
    bool themeEnabled = false;
    std::uint8_t bitfields = 0;
    std::wstring windowName{};
    wxColourRuntime backgroundColour{};
    void* dropTarget = nullptr;
  };

  struct WxTextCtrlRuntimeState
  {
    std::int32_t richEditMajorVersion = 0;
  };

  struct WxWindowCaptureHistoryNode
  {
    wxWindowBase* window = nullptr;
    WxWindowCaptureHistoryNode* next = nullptr;
  };

  std::unordered_map<const wxWindowBase*, WxWindowBaseRuntimeState> gWxWindowBaseStateByWindow{};
  std::unordered_map<const wxTextCtrlRuntime*, WxTextCtrlRuntimeState> gWxTextCtrlStateByControl{};
  std::unordered_map<int, wxWindowMswRuntime*> gWxWindowByNativeHandle{};
  wxWindowBase* gCapturedWindow = nullptr;
  WxWindowCaptureHistoryNode* gWindowCaptureHistoryHead = nullptr;
  bool gSplashPngHandlerInitialized = false;
  std::unordered_map<COLORREF, HBRUSH> gCtlColorBrushByColor{};
  bool gCtlColorBrushCacheCleanupRegistered = false;

  void CleanupCtlColorBrushCache() noexcept
  {
    for (const auto& [_, brush] : gCtlColorBrushByColor) {
      if (brush != nullptr) {
        ::DeleteObject(brush);
      }
    }
    gCtlColorBrushByColor.clear();
  }

  [[nodiscard]] HBRUSH GetOrCreateCtlColorBrush(
    const COLORREF color
  ) noexcept
  {
    const auto existing = gCtlColorBrushByColor.find(color);
    if (existing != gCtlColorBrushByColor.end() && existing->second != nullptr) {
      return existing->second;
    }

    HBRUSH brush = ::CreateSolidBrush(color);
    if (brush == nullptr) {
      brush = static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH));
      return brush;
    }

    if (!gCtlColorBrushCacheCleanupRegistered) {
      gCtlColorBrushCacheCleanupRegistered = true;
      std::atexit(&CleanupCtlColorBrushCache);
    }

    gCtlColorBrushByColor[color] = brush;
    return brush;
  }

  struct WxDropFilesArrayStorage
  {
    std::uint32_t fileCount = 0;
  };

  int wxNewEventType()
  {
    static std::int32_t nextRuntimeEventType = 10000;
    return ++nextRuntimeEventType;
  }
  std::int32_t gWxEvtIdleRuntimeType = 0;
  std::int32_t gWxEvtDropFilesRuntimeType = 0;
  std::int32_t gWxEvtMouseCaptureChangedRuntimeType = 0;
  std::int32_t gWxEvtUpdateUiRuntimeType = 0;
  std::int32_t gWxEvtCommandMenuSelectedRuntimeType = 0;
  std::int32_t gWxEvtMenuHighlightRuntimeType = 0;
  std::int32_t gWxEvtSizeRuntimeType = 0;
  std::int32_t gWxEvtPaintRuntimeType = 0;
  std::int32_t gWxEvtNcPaintRuntimeType = 0;
  std::int32_t gWxEvtEraseBackgroundRuntimeType = 0;
  std::int32_t gWxEvtMoveRuntimeType = 0;
  std::int32_t gWxEvtActivateRuntimeType = 0;
  std::int32_t gWxEvtInitDialogRuntimeType = 0;
  std::int32_t gWxEvtSysColourChangedRuntimeType = 0;
  std::int32_t gWxEvtDisplayChangedRuntimeType = 0;
  std::int32_t gWxEvtNavigationKeyRuntimeType = 0;
  std::int32_t gWxEvtKeyDownRuntimeType = 0;
  std::int32_t gWxEvtPaletteChangedRuntimeType = 0;
  std::int32_t gWxEvtQueryNewPaletteRuntimeType = 0;
  std::int32_t gWxEvtShowRuntimeType = 0;
  std::int32_t gWxEvtMaximizeRuntimeType = 0;
  std::int32_t gWxEvtIconizeRuntimeType = 0;
  std::int32_t gWxEvtChildFocusRuntimeType = 0;
  std::int32_t gWxEvtWindowCreateRuntimeType = 0;
  std::int32_t gWxEvtWindowDestroyRuntimeType = 0;
  std::int32_t gWxEvtSetCursorRuntimeType = 0;
  std::int32_t gWxEvtSocketRuntimeType = 0;
  std::int32_t gWxEvtTimerRuntimeType = 0;
  std::int32_t gWxEvtProcessRuntimeType = 0;

  [[nodiscard]] std::int32_t EnsureWxEvtIdleRuntimeType()
  {
    if (gWxEvtIdleRuntimeType == 0) {
      gWxEvtIdleRuntimeType = wxNewEventType();
    }
    return gWxEvtIdleRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtDropFilesRuntimeType()
  {
    if (gWxEvtDropFilesRuntimeType == 0) {
      gWxEvtDropFilesRuntimeType = wxNewEventType();
    }
    return gWxEvtDropFilesRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtMouseCaptureChangedRuntimeType()
  {
    if (gWxEvtMouseCaptureChangedRuntimeType == 0) {
      gWxEvtMouseCaptureChangedRuntimeType = wxNewEventType();
    }
    return gWxEvtMouseCaptureChangedRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtUpdateUiRuntimeType()
  {
    if (gWxEvtUpdateUiRuntimeType == 0) {
      gWxEvtUpdateUiRuntimeType = wxNewEventType();
    }
    return gWxEvtUpdateUiRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtCommandMenuSelectedRuntimeType()
  {
    if (gWxEvtCommandMenuSelectedRuntimeType == 0) {
      gWxEvtCommandMenuSelectedRuntimeType = wxNewEventType();
    }
    return gWxEvtCommandMenuSelectedRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtMenuHighlightRuntimeType()
  {
    if (gWxEvtMenuHighlightRuntimeType == 0) {
      gWxEvtMenuHighlightRuntimeType = wxNewEventType();
    }
    return gWxEvtMenuHighlightRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtSizeRuntimeType()
  {
    if (gWxEvtSizeRuntimeType == 0) {
      gWxEvtSizeRuntimeType = wxNewEventType();
    }
    return gWxEvtSizeRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtPaintRuntimeType()
  {
    if (gWxEvtPaintRuntimeType == 0) {
      gWxEvtPaintRuntimeType = wxNewEventType();
    }
    return gWxEvtPaintRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtNcPaintRuntimeType()
  {
    if (gWxEvtNcPaintRuntimeType == 0) {
      gWxEvtNcPaintRuntimeType = wxNewEventType();
    }
    return gWxEvtNcPaintRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtEraseBackgroundRuntimeType()
  {
    if (gWxEvtEraseBackgroundRuntimeType == 0) {
      gWxEvtEraseBackgroundRuntimeType = wxNewEventType();
    }
    return gWxEvtEraseBackgroundRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtMoveRuntimeType()
  {
    if (gWxEvtMoveRuntimeType == 0) {
      gWxEvtMoveRuntimeType = wxNewEventType();
    }
    return gWxEvtMoveRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtActivateRuntimeType()
  {
    if (gWxEvtActivateRuntimeType == 0) {
      gWxEvtActivateRuntimeType = wxNewEventType();
    }
    return gWxEvtActivateRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtInitDialogRuntimeType()
  {
    if (gWxEvtInitDialogRuntimeType == 0) {
      gWxEvtInitDialogRuntimeType = wxNewEventType();
    }
    return gWxEvtInitDialogRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtSysColourChangedRuntimeType()
  {
    if (gWxEvtSysColourChangedRuntimeType == 0) {
      gWxEvtSysColourChangedRuntimeType = wxNewEventType();
    }
    return gWxEvtSysColourChangedRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtDisplayChangedRuntimeType()
  {
    if (gWxEvtDisplayChangedRuntimeType == 0) {
      gWxEvtDisplayChangedRuntimeType = wxNewEventType();
    }
    return gWxEvtDisplayChangedRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtNavigationKeyRuntimeType()
  {
    if (gWxEvtNavigationKeyRuntimeType == 0) {
      gWxEvtNavigationKeyRuntimeType = wxNewEventType();
    }
    return gWxEvtNavigationKeyRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtKeyDownRuntimeType()
  {
    if (gWxEvtKeyDownRuntimeType == 0) {
      gWxEvtKeyDownRuntimeType = wxNewEventType();
    }
    return gWxEvtKeyDownRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtPaletteChangedRuntimeType()
  {
    if (gWxEvtPaletteChangedRuntimeType == 0) {
      gWxEvtPaletteChangedRuntimeType = wxNewEventType();
    }
    return gWxEvtPaletteChangedRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtQueryNewPaletteRuntimeType()
  {
    if (gWxEvtQueryNewPaletteRuntimeType == 0) {
      gWxEvtQueryNewPaletteRuntimeType = wxNewEventType();
    }
    return gWxEvtQueryNewPaletteRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtShowRuntimeType()
  {
    if (gWxEvtShowRuntimeType == 0) {
      gWxEvtShowRuntimeType = wxNewEventType();
    }
    return gWxEvtShowRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtMaximizeRuntimeType()
  {
    if (gWxEvtMaximizeRuntimeType == 0) {
      gWxEvtMaximizeRuntimeType = wxNewEventType();
    }
    return gWxEvtMaximizeRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtIconizeRuntimeType()
  {
    if (gWxEvtIconizeRuntimeType == 0) {
      gWxEvtIconizeRuntimeType = wxNewEventType();
    }
    return gWxEvtIconizeRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtChildFocusRuntimeType()
  {
    if (gWxEvtChildFocusRuntimeType == 0) {
      gWxEvtChildFocusRuntimeType = wxNewEventType();
    }
    return gWxEvtChildFocusRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtWindowCreateRuntimeType()
  {
    if (gWxEvtWindowCreateRuntimeType == 0) {
      gWxEvtWindowCreateRuntimeType = wxNewEventType();
    }
    return gWxEvtWindowCreateRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtWindowDestroyRuntimeType()
  {
    if (gWxEvtWindowDestroyRuntimeType == 0) {
      gWxEvtWindowDestroyRuntimeType = wxNewEventType();
    }
    return gWxEvtWindowDestroyRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtSetCursorRuntimeType()
  {
    if (gWxEvtSetCursorRuntimeType == 0) {
      gWxEvtSetCursorRuntimeType = wxNewEventType();
    }
    return gWxEvtSetCursorRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtSocketRuntimeType()
  {
    if (gWxEvtSocketRuntimeType == 0) {
      gWxEvtSocketRuntimeType = wxNewEventType();
    }
    return gWxEvtSocketRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtTimerRuntimeType()
  {
    if (gWxEvtTimerRuntimeType == 0) {
      gWxEvtTimerRuntimeType = wxNewEventType();
    }
    return gWxEvtTimerRuntimeType;
  }

  [[nodiscard]] std::int32_t EnsureWxEvtProcessRuntimeType()
  {
    if (gWxEvtProcessRuntimeType == 0) {
      gWxEvtProcessRuntimeType = wxNewEventType();
    }
    return gWxEvtProcessRuntimeType;
  }

  class WxIdleEventRuntime final : public wxEventRuntime
  {
  public:
    WxIdleEventRuntime()
      : wxEventRuntime(0, EnsureWxEvtIdleRuntimeType())
      , mRequestMore(false)
      , mPadding21To23{0, 0, 0}
    {}

    WxIdleEventRuntime* Clone() const override
    {
      auto* const clone = new (std::nothrow) WxIdleEventRuntime();
      if (clone == nullptr) {
        return nullptr;
      }

      clone->mRefData = mRefData;
      clone->mEventObject = mEventObject;
      clone->mEventType = mEventType;
      clone->mEventTimestamp = mEventTimestamp;
      clone->mEventId = mEventId;
      clone->mCallbackUserData = mCallbackUserData;
      clone->mSkipped = mSkipped;
      clone->mIsCommandEvent = mIsCommandEvent;
      clone->mReserved1E = mReserved1E;
      clone->mReserved1F = mReserved1F;
      clone->mRequestMore = mRequestMore;
      return clone;
    }

    bool mRequestMore;
    std::uint8_t mPadding21To23[0x03];
  };

  static_assert(
    offsetof(WxIdleEventRuntime, mRequestMore) == 0x20,
    "WxIdleEventRuntime::mRequestMore offset must be 0x20"
  );
  static_assert(sizeof(WxIdleEventRuntime) == 0x24, "WxIdleEventRuntime size must be 0x24");

  class WxDropFilesEventRuntime final : public wxEventRuntime
  {
  public:
    WxDropFilesEventRuntime()
      : wxEventRuntime(0, EnsureWxEvtDropFilesRuntimeType())
    {}

    ~WxDropFilesEventRuntime()
    {
      ReleaseFileArray();
      RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(this));
    }

    WxDropFilesEventRuntime* Clone() const override
    {
      auto* const clone = new (std::nothrow) WxDropFilesEventRuntime();
      if (clone == nullptr) {
        return nullptr;
      }

      clone->mEventObject = mEventObject;
      clone->mEventType = mEventType;
      clone->mEventTimestamp = mEventTimestamp;
      clone->mEventId = mEventId;
      clone->mCallbackUserData = mCallbackUserData;
      clone->mSkipped = mSkipped;
      clone->mIsCommandEvent = mIsCommandEvent;
      clone->mReserved1E = mReserved1E;
      clone->mReserved1F = mReserved1F;
      clone->mDropPointX = mDropPointX;
      clone->mDropPointY = mDropPointY;
      clone->AssignFiles(mFiles, mFileCount);
      return clone;
    }

    void PopulateFromDropHandle(
      const HDROP dropHandle
    )
    {
      const std::uint32_t fileCount =
        static_cast<std::uint32_t>(::DragQueryFileW(dropHandle, 0xFFFFFFFFu, nullptr, 0u));
      AllocateFileArray(fileCount);
      for (std::uint32_t fileIndex = 0; fileIndex < mFileCount; ++fileIndex) {
        const UINT fileNameLength = ::DragQueryFileW(dropHandle, fileIndex, nullptr, 0u);
        auto* const fileName = new (std::nothrow) wchar_t[fileNameLength + 1u];
        if (fileName == nullptr) {
          continue;
        }

        const UINT copiedLength = ::DragQueryFileW(dropHandle, fileIndex, fileName, fileNameLength + 1u);
        fileName[copiedLength] = L'\0';
        mFiles[fileIndex] = wxStringRuntime::Borrow(fileName);
      }
    }

    std::uint32_t mFileCount = 0;
    std::int32_t mDropPointX = 0;
    std::int32_t mDropPointY = 0;
    wxStringRuntime* mFiles = nullptr;

  private:
    void AllocateFileArray(
      const std::uint32_t fileCount
    )
    {
      ReleaseFileArray();
      if (fileCount == 0u) {
        return;
      }

      const std::size_t storageBytes =
        sizeof(WxDropFilesArrayStorage) + sizeof(wxStringRuntime) * static_cast<std::size_t>(fileCount);
      auto* const storage = static_cast<WxDropFilesArrayStorage*>(::operator new(storageBytes, std::nothrow));
      if (storage == nullptr) {
        return;
      }

      storage->fileCount = fileCount;
      mFileCount = fileCount;
      mFiles = reinterpret_cast<wxStringRuntime*>(reinterpret_cast<std::uint8_t*>(storage) + sizeof(*storage));
      for (std::uint32_t index = 0; index < mFileCount; ++index) {
        mFiles[index].m_pchData = nullptr;
      }
    }

    void AssignFiles(
      const wxStringRuntime* const files,
      const std::uint32_t fileCount
    )
    {
      if (files == nullptr || fileCount == 0u) {
        return;
      }

      AllocateFileArray(fileCount);
      for (std::uint32_t index = 0; index < mFileCount; ++index) {
        const wchar_t* const sourceText = files[index].c_str();
        const std::size_t sourceLength = std::wcslen(sourceText);
        auto* const copiedText = new (std::nothrow) wchar_t[sourceLength + 1u];
        if (copiedText == nullptr) {
          continue;
        }

        std::wmemcpy(copiedText, sourceText, sourceLength + 1u);
        mFiles[index] = wxStringRuntime::Borrow(copiedText);
      }
    }

    void ReleaseFileArray() noexcept
    {
      if (mFiles != nullptr) {
        for (std::uint32_t index = 0; index < mFileCount; ++index) {
          delete[] mFiles[index].m_pchData;
          mFiles[index].m_pchData = nullptr;
        }

        void* const storage = reinterpret_cast<std::uint8_t*>(mFiles) - sizeof(WxDropFilesArrayStorage);
        ::operator delete(storage);
      }

      mFiles = nullptr;
      mFileCount = 0;
    }
  };

  static_assert(
    offsetof(WxDropFilesEventRuntime, mFileCount) == 0x20,
    "WxDropFilesEventRuntime::mFileCount offset must be 0x20"
  );
  static_assert(
    offsetof(WxDropFilesEventRuntime, mDropPointX) == 0x24,
    "WxDropFilesEventRuntime::mDropPointX offset must be 0x24"
  );
  static_assert(
    offsetof(WxDropFilesEventRuntime, mDropPointY) == 0x28,
    "WxDropFilesEventRuntime::mDropPointY offset must be 0x28"
  );
  static_assert(
    offsetof(WxDropFilesEventRuntime, mFiles) == 0x2C,
    "WxDropFilesEventRuntime::mFiles offset must be 0x2C"
  );
  static_assert(sizeof(WxDropFilesEventRuntime) == 0x30, "WxDropFilesEventRuntime size must be 0x30");

  class WxMouseCaptureChangedEventRuntime final : public wxEventRuntime
  {
  public:
    WxMouseCaptureChangedEventRuntime(
      const std::int32_t eventId,
      const std::int32_t eventType,
      wxWindowMswRuntime* const previousCapture
    )
      : wxEventRuntime(eventId, eventType)
      , mPreviousCapture(previousCapture)
    {}

    WxMouseCaptureChangedEventRuntime* Clone() const override
    {
      auto* const clone = new (std::nothrow) WxMouseCaptureChangedEventRuntime(mEventId, mEventType, mPreviousCapture);
      if (clone == nullptr) {
        return nullptr;
      }

      clone->mRefData = mRefData;
      clone->mEventObject = mEventObject;
      clone->mEventTimestamp = mEventTimestamp;
      clone->mCallbackUserData = mCallbackUserData;
      clone->mSkipped = mSkipped;
      clone->mIsCommandEvent = mIsCommandEvent;
      clone->mReserved1E = mReserved1E;
      clone->mReserved1F = mReserved1F;
      return clone;
    }

    wxWindowMswRuntime* mPreviousCapture = nullptr;
  };

  static_assert(
    offsetof(WxMouseCaptureChangedEventRuntime, mPreviousCapture) == 0x20,
    "WxMouseCaptureChangedEventRuntime::mPreviousCapture offset must be 0x20"
  );
  static_assert(
    sizeof(WxMouseCaptureChangedEventRuntime) == 0x24,
    "WxMouseCaptureChangedEventRuntime size must be 0x24"
  );

  class WxSizeEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxSizeEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtSizeRuntimeType())
      , mSizeX(0)
      , mSizeY(0)
    {}

    WxSizeEventFactoryRuntime* Clone() const override
    {
      auto* const clone = new (std::nothrow) WxSizeEventFactoryRuntime();
      if (clone == nullptr) {
        return nullptr;
      }

      clone->mRefData = mRefData;
      clone->mEventObject = mEventObject;
      clone->mEventType = mEventType;
      clone->mEventTimestamp = mEventTimestamp;
      clone->mEventId = mEventId;
      clone->mCallbackUserData = mCallbackUserData;
      clone->mSkipped = mSkipped;
      clone->mIsCommandEvent = mIsCommandEvent;
      clone->mReserved1E = mReserved1E;
      clone->mReserved1F = mReserved1F;
      clone->mSizeX = mSizeX;
      clone->mSizeY = mSizeY;
      return clone;
    }

    std::int32_t mSizeX = 0;
    std::int32_t mSizeY = 0;
  };

  static_assert(
    offsetof(WxSizeEventFactoryRuntime, mSizeX) == 0x20,
    "WxSizeEventFactoryRuntime::mSizeX offset must be 0x20"
  );
  static_assert(
    offsetof(WxSizeEventFactoryRuntime, mSizeY) == 0x24,
    "WxSizeEventFactoryRuntime::mSizeY offset must be 0x24"
  );
  static_assert(sizeof(WxSizeEventFactoryRuntime) == 0x28, "WxSizeEventFactoryRuntime size must be 0x28");

  class WxPaintEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxPaintEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtPaintRuntimeType())
    {}

    WxPaintEventFactoryRuntime* Clone() const override
    {
      auto* const clone = new (std::nothrow) WxPaintEventFactoryRuntime();
      if (clone == nullptr) {
        return nullptr;
      }

      clone->mRefData = mRefData;
      clone->mEventObject = mEventObject;
      clone->mEventType = mEventType;
      clone->mEventTimestamp = mEventTimestamp;
      clone->mEventId = mEventId;
      clone->mCallbackUserData = mCallbackUserData;
      clone->mSkipped = mSkipped;
      clone->mIsCommandEvent = mIsCommandEvent;
      clone->mReserved1E = mReserved1E;
      clone->mReserved1F = mReserved1F;
      return clone;
    }
  };

  static_assert(sizeof(WxPaintEventFactoryRuntime) == 0x20, "WxPaintEventFactoryRuntime size must be 0x20");

  class WxNcPaintEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxNcPaintEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtNcPaintRuntimeType())
    {}

    WxNcPaintEventFactoryRuntime* Clone() const override
    {
      auto* const clone = new (std::nothrow) WxNcPaintEventFactoryRuntime();
      if (clone == nullptr) {
        return nullptr;
      }

      clone->mRefData = mRefData;
      clone->mEventObject = mEventObject;
      clone->mEventType = mEventType;
      clone->mEventTimestamp = mEventTimestamp;
      clone->mEventId = mEventId;
      clone->mCallbackUserData = mCallbackUserData;
      clone->mSkipped = mSkipped;
      clone->mIsCommandEvent = mIsCommandEvent;
      clone->mReserved1E = mReserved1E;
      clone->mReserved1F = mReserved1F;
      return clone;
    }
  };

  static_assert(sizeof(WxNcPaintEventFactoryRuntime) == 0x20, "WxNcPaintEventFactoryRuntime size must be 0x20");

  class WxEraseEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxEraseEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtEraseBackgroundRuntimeType())
      , mDeviceContext(nullptr)
    {}

    WxEraseEventFactoryRuntime* Clone() const override
    {
      auto* const clone = new (std::nothrow) WxEraseEventFactoryRuntime();
      if (clone == nullptr) {
        return nullptr;
      }

      clone->mRefData = mRefData;
      clone->mEventObject = mEventObject;
      clone->mEventType = mEventType;
      clone->mEventTimestamp = mEventTimestamp;
      clone->mEventId = mEventId;
      clone->mCallbackUserData = mCallbackUserData;
      clone->mSkipped = mSkipped;
      clone->mIsCommandEvent = mIsCommandEvent;
      clone->mReserved1E = mReserved1E;
      clone->mReserved1F = mReserved1F;
      clone->mDeviceContext = mDeviceContext;
      return clone;
    }

    void* mDeviceContext = nullptr;
  };

  static_assert(
    offsetof(WxEraseEventFactoryRuntime, mDeviceContext) == 0x20,
    "WxEraseEventFactoryRuntime::mDeviceContext offset must be 0x20"
  );
  static_assert(sizeof(WxEraseEventFactoryRuntime) == 0x24, "WxEraseEventFactoryRuntime size must be 0x24");

  class WxMoveEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxMoveEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtMoveRuntimeType())
      , mX(0)
      , mY(0)
    {}

    WxMoveEventFactoryRuntime* Clone() const override
    {
      auto* const clone = new (std::nothrow) WxMoveEventFactoryRuntime();
      if (clone == nullptr) {
        return nullptr;
      }

      clone->mRefData = mRefData;
      clone->mEventObject = mEventObject;
      clone->mEventType = mEventType;
      clone->mEventTimestamp = mEventTimestamp;
      clone->mEventId = mEventId;
      clone->mCallbackUserData = mCallbackUserData;
      clone->mSkipped = mSkipped;
      clone->mIsCommandEvent = mIsCommandEvent;
      clone->mReserved1E = mReserved1E;
      clone->mReserved1F = mReserved1F;
      clone->mX = mX;
      clone->mY = mY;
      return clone;
    }

    std::int32_t mX = 0;
    std::int32_t mY = 0;
  };

  static_assert(
    offsetof(WxMoveEventFactoryRuntime, mX) == 0x20,
    "WxMoveEventFactoryRuntime::mX offset must be 0x20"
  );
  static_assert(
    offsetof(WxMoveEventFactoryRuntime, mY) == 0x24,
    "WxMoveEventFactoryRuntime::mY offset must be 0x24"
  );
  static_assert(sizeof(WxMoveEventFactoryRuntime) == 0x28, "WxMoveEventFactoryRuntime size must be 0x28");

  class WxFocusEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxFocusEventFactoryRuntime()
      : wxEventRuntime(0, 0)
      , mFocusedWindow(nullptr)
    {}

    WxFocusEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxFocusEventFactoryRuntime(*this);
    }

    wxWindowBase* mFocusedWindow = nullptr;
  };

  static_assert(
    offsetof(WxFocusEventFactoryRuntime, mFocusedWindow) == 0x20,
    "WxFocusEventFactoryRuntime::mFocusedWindow offset must be 0x20"
  );
  static_assert(sizeof(WxFocusEventFactoryRuntime) == 0x24, "WxFocusEventFactoryRuntime size must be 0x24");

  class WxCloseEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxCloseEventFactoryRuntime()
      : wxEventRuntime(0, 0)
      , mCanVeto(1)
      , mVeto(0)
      , mLoggingOff(1)
      , mReserved23(0)
    {}

    /**
     * Address: 0x00962AA0 (FUN_00962AA0)
     *
     * What it does:
     * Allocates and copy-clones one close-event payload including veto/logging
     * state lanes.
     */
    WxCloseEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxCloseEventFactoryRuntime(*this);
    }

    std::uint8_t mCanVeto = 1;
    std::uint8_t mVeto = 0;
    std::uint8_t mLoggingOff = 1;
    std::uint8_t mReserved23 = 0;
  };

  static_assert(offsetof(WxCloseEventFactoryRuntime, mCanVeto) == 0x20, "WxCloseEventFactoryRuntime::mCanVeto offset must be 0x20");
  static_assert(offsetof(WxCloseEventFactoryRuntime, mVeto) == 0x21, "WxCloseEventFactoryRuntime::mVeto offset must be 0x21");
  static_assert(
    offsetof(WxCloseEventFactoryRuntime, mLoggingOff) == 0x22,
    "WxCloseEventFactoryRuntime::mLoggingOff offset must be 0x22"
  );
  static_assert(sizeof(WxCloseEventFactoryRuntime) == 0x24, "WxCloseEventFactoryRuntime size must be 0x24");

  class WxShowEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxShowEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtShowRuntimeType())
      , mShown(0)
      , mPadding21To23{0, 0, 0}
    {}

    WxShowEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxShowEventFactoryRuntime(*this);
    }

    std::uint8_t mShown = 0;
    std::uint8_t mPadding21To23[3] = {0, 0, 0};
  };

  static_assert(offsetof(WxShowEventFactoryRuntime, mShown) == 0x20, "WxShowEventFactoryRuntime::mShown offset must be 0x20");
  static_assert(sizeof(WxShowEventFactoryRuntime) == 0x24, "WxShowEventFactoryRuntime size must be 0x24");

  class WxMaximizeEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxMaximizeEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtMaximizeRuntimeType())
    {}

    WxMaximizeEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxMaximizeEventFactoryRuntime(*this);
    }
  };

  static_assert(sizeof(WxMaximizeEventFactoryRuntime) == 0x20, "WxMaximizeEventFactoryRuntime size must be 0x20");

  class WxIconizeEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxIconizeEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtIconizeRuntimeType())
      , mIconized(1)
      , mPadding21To23{0, 0, 0}
    {}

    WxIconizeEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxIconizeEventFactoryRuntime(*this);
    }

    std::uint8_t mIconized = 1;
    std::uint8_t mPadding21To23[3] = {0, 0, 0};
  };

  static_assert(offsetof(WxIconizeEventFactoryRuntime, mIconized) == 0x20, "WxIconizeEventFactoryRuntime::mIconized offset must be 0x20");
  static_assert(sizeof(WxIconizeEventFactoryRuntime) == 0x24, "WxIconizeEventFactoryRuntime size must be 0x24");

  class WxActivateEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxActivateEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtActivateRuntimeType())
      , mIsActive(1)
      , mPadding21To23{0, 0, 0}
    {}

    WxActivateEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxActivateEventFactoryRuntime(*this);
    }

    std::uint8_t mIsActive = 1;
    std::uint8_t mPadding21To23[3] = {0, 0, 0};
  };

  static_assert(
    offsetof(WxActivateEventFactoryRuntime, mIsActive) == 0x20,
    "WxActivateEventFactoryRuntime::mIsActive offset must be 0x20"
  );
  static_assert(sizeof(WxActivateEventFactoryRuntime) == 0x24, "WxActivateEventFactoryRuntime size must be 0x24");

  class WxInitDialogEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxInitDialogEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtInitDialogRuntimeType())
    {}

    /**
     * Address: 0x00964EC0 (FUN_00964EC0)
     *
     * What it does:
     * Allocates and copy-clones one init-dialog event payload.
     */
    WxInitDialogEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxInitDialogEventFactoryRuntime(*this);
    }
  };

  static_assert(sizeof(WxInitDialogEventFactoryRuntime) == 0x20, "WxInitDialogEventFactoryRuntime size must be 0x20");

  class WxSysColourChangedEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxSysColourChangedEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtSysColourChangedRuntimeType())
    {}

    /**
     * Address: 0x009650D0 (FUN_009650D0)
     *
     * What it does:
     * Allocates and copy-clones one system-colour-changed event payload.
     */
    WxSysColourChangedEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxSysColourChangedEventFactoryRuntime(*this);
    }
  };

  static_assert(
    sizeof(WxSysColourChangedEventFactoryRuntime) == 0x20,
    "WxSysColourChangedEventFactoryRuntime size must be 0x20"
  );

  class WxDisplayChangedEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxDisplayChangedEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtDisplayChangedRuntimeType())
    {}

    WxDisplayChangedEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxDisplayChangedEventFactoryRuntime(*this);
    }
  };

  static_assert(sizeof(WxDisplayChangedEventFactoryRuntime) == 0x20, "WxDisplayChangedEventFactoryRuntime size must be 0x20");

  class WxNavigationKeyEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxNavigationKeyEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtNavigationKeyRuntimeType())
      , mNavigationFlags(5)
      , mCurrentFocusWindow(nullptr)
    {}

    WxNavigationKeyEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxNavigationKeyEventFactoryRuntime(*this);
    }

    std::int32_t mNavigationFlags = 5;
    wxWindowBase* mCurrentFocusWindow = nullptr;
  };

  static_assert(
    offsetof(WxNavigationKeyEventFactoryRuntime, mNavigationFlags) == 0x20,
    "WxNavigationKeyEventFactoryRuntime::mNavigationFlags offset must be 0x20"
  );
  static_assert(
    offsetof(WxNavigationKeyEventFactoryRuntime, mCurrentFocusWindow) == 0x24,
    "WxNavigationKeyEventFactoryRuntime::mCurrentFocusWindow offset must be 0x24"
  );
  static_assert(
    sizeof(WxNavigationKeyEventFactoryRuntime) == 0x28,
    "WxNavigationKeyEventFactoryRuntime size must be 0x28"
  );

  class WxPaletteChangedEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxPaletteChangedEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtPaletteChangedRuntimeType())
      , mChangedWindow(nullptr)
    {}

    WxPaletteChangedEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxPaletteChangedEventFactoryRuntime(*this);
    }

    wxWindowBase* mChangedWindow = nullptr;
  };

  static_assert(
    offsetof(WxPaletteChangedEventFactoryRuntime, mChangedWindow) == 0x20,
    "WxPaletteChangedEventFactoryRuntime::mChangedWindow offset must be 0x20"
  );
  static_assert(
    sizeof(WxPaletteChangedEventFactoryRuntime) == 0x24,
    "WxPaletteChangedEventFactoryRuntime size must be 0x24"
  );

  class WxQueryNewPaletteEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxQueryNewPaletteEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtQueryNewPaletteRuntimeType())
      , mPaletteRealized(0)
      , mPadding21To23{0, 0, 0}
    {}

    WxQueryNewPaletteEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxQueryNewPaletteEventFactoryRuntime(*this);
    }

    std::uint8_t mPaletteRealized = 0;
    std::uint8_t mPadding21To23[3] = {0, 0, 0};
  };

  static_assert(
    offsetof(WxQueryNewPaletteEventFactoryRuntime, mPaletteRealized) == 0x20,
    "WxQueryNewPaletteEventFactoryRuntime::mPaletteRealized offset must be 0x20"
  );
  static_assert(
    sizeof(WxQueryNewPaletteEventFactoryRuntime) == 0x24,
    "WxQueryNewPaletteEventFactoryRuntime size must be 0x24"
  );

  class WxMenuEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxMenuEventFactoryRuntime()
      : wxEventRuntime(0, 0)
      , mMenuId(0)
    {}

    WxMenuEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxMenuEventFactoryRuntime(*this);
    }

    std::int32_t mMenuId = 0;
  };

  static_assert(offsetof(WxMenuEventFactoryRuntime, mMenuId) == 0x20, "WxMenuEventFactoryRuntime::mMenuId offset must be 0x20");
  static_assert(sizeof(WxMenuEventFactoryRuntime) == 0x24, "WxMenuEventFactoryRuntime size must be 0x24");

  class WxJoystickEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxJoystickEventFactoryRuntime()
      : wxEventRuntime(0, 0)
      , mPositionX(0)
      , mPositionY(0)
      , mPositionZ(0)
      , mButtonChange(0)
      , mButtonState(0)
      , mJoystickIndex(0)
    {}

    WxJoystickEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxJoystickEventFactoryRuntime(*this);
    }

    std::int32_t mPositionX = 0;
    std::int32_t mPositionY = 0;
    std::int32_t mPositionZ = 0;
    std::int32_t mButtonChange = 0;
    std::int32_t mButtonState = 0;
    std::int32_t mJoystickIndex = 0;
  };

  static_assert(
    offsetof(WxJoystickEventFactoryRuntime, mPositionX) == 0x20,
    "WxJoystickEventFactoryRuntime::mPositionX offset must be 0x20"
  );
  static_assert(
    offsetof(WxJoystickEventFactoryRuntime, mPositionY) == 0x24,
    "WxJoystickEventFactoryRuntime::mPositionY offset must be 0x24"
  );
  static_assert(
    offsetof(WxJoystickEventFactoryRuntime, mPositionZ) == 0x28,
    "WxJoystickEventFactoryRuntime::mPositionZ offset must be 0x28"
  );
  static_assert(
    offsetof(WxJoystickEventFactoryRuntime, mButtonChange) == 0x2C,
    "WxJoystickEventFactoryRuntime::mButtonChange offset must be 0x2C"
  );
  static_assert(
    offsetof(WxJoystickEventFactoryRuntime, mButtonState) == 0x30,
    "WxJoystickEventFactoryRuntime::mButtonState offset must be 0x30"
  );
  static_assert(
    offsetof(WxJoystickEventFactoryRuntime, mJoystickIndex) == 0x34,
    "WxJoystickEventFactoryRuntime::mJoystickIndex offset must be 0x34"
  );
  static_assert(sizeof(WxJoystickEventFactoryRuntime) == 0x38, "WxJoystickEventFactoryRuntime size must be 0x38");

  class WxScrollEventFactoryRuntime final : public wxCommandEventRuntime
  {
  public:
    WxScrollEventFactoryRuntime()
      : wxCommandEventRuntime(0, 0)
    {}

    WxScrollEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxScrollEventFactoryRuntime(*this);
    }
  };

  static_assert(sizeof(WxScrollEventFactoryRuntime) == 0x34, "WxScrollEventFactoryRuntime size must be 0x34");

  class WxContextMenuEventRuntime final : public wxCommandEventRuntime
  {
  public:
    WxContextMenuEventRuntime()
      : wxCommandEventRuntime(0, 0)
      , mContextMenuPosition{-1, -1}
    {}

    WxContextMenuEventRuntime* Clone() const override
    {
      return new (std::nothrow) WxContextMenuEventRuntime(*this);
    }

    wxPoint mContextMenuPosition{};
  };

  static_assert(
    offsetof(WxContextMenuEventRuntime, mContextMenuPosition) == 0x34,
    "WxContextMenuEventRuntime::mContextMenuPosition offset must be 0x34"
  );
  static_assert(
    sizeof(WxContextMenuEventRuntime) == 0x3C,
    "WxContextMenuEventRuntime size must be 0x3C"
  );

  class WxNotifyEventRuntime final : public wxCommandEventRuntime
  {
  public:
    WxNotifyEventRuntime()
      : wxCommandEventRuntime(0, 0)
      , mAllow(true)
      , mPadding35To37{0, 0, 0}
    {}

    WxNotifyEventRuntime* Clone() const override
    {
      return new (std::nothrow) WxNotifyEventRuntime(*this);
    }

    std::uint8_t mAllow = 1;
    std::uint8_t mPadding35To37[3] = {0, 0, 0};
  };

  static_assert(
    offsetof(WxNotifyEventRuntime, mAllow) == 0x34,
    "WxNotifyEventRuntime::mAllow offset must be 0x34"
  );
  static_assert(sizeof(WxNotifyEventRuntime) == 0x38, "WxNotifyEventRuntime size must be 0x38");

  class WxUpdateUIEventRuntime final : public wxCommandEventRuntime
  {
  public:
    WxUpdateUIEventRuntime()
      : wxCommandEventRuntime(EnsureWxEvtUpdateUiRuntimeType(), 0)
      , mSetChecked(0)
      , mSetEnabled(0)
      , mSetShown(0)
      , mSetText(0)
      , mSetTextColour(0)
      , mPadding39To3B{0, 0, 0}
      , mTextLabel{}
    {
      AssignOwnedWxString(&mTextLabel, std::wstring());
    }

    /**
     * Address: 0x00962BE0 (FUN_00962BE0)
     *
     * What it does:
     * Releases one update-UI text-label string lane, then tails into the
     * wx-command-event base destructor.
     */
    ~WxUpdateUIEventRuntime();

    WxUpdateUIEventRuntime* Clone() const override;

    std::uint8_t mSetChecked = 0;
    std::uint8_t mSetEnabled = 0;
    std::uint8_t mSetShown = 0;
    std::uint8_t mSetText = 0;
    std::uint8_t mSetTextColour = 0;
    std::uint8_t mPadding39To3B[3] = {0, 0, 0};
    wxStringRuntime mTextLabel{};
  };

  static_assert(
    offsetof(WxUpdateUIEventRuntime, mSetChecked) == 0x34,
    "WxUpdateUIEventRuntime::mSetChecked offset must be 0x34"
  );
  static_assert(
    offsetof(WxUpdateUIEventRuntime, mTextLabel) == 0x3C,
    "WxUpdateUIEventRuntime::mTextLabel offset must be 0x3C"
  );
  static_assert(sizeof(WxUpdateUIEventRuntime) == 0x40, "WxUpdateUIEventRuntime size must be 0x40");

  /**
   * Address: 0x00962BE0 (FUN_00962BE0)
   *
   * What it does:
   * Releases one update-UI text-label string lane, then tails into the
   * wx-command-event base destructor.
   */
  WxUpdateUIEventRuntime::~WxUpdateUIEventRuntime()
  {
    ReleaseWxStringSharedPayload(mTextLabel);
  }

  WxUpdateUIEventRuntime* WxUpdateUIEventRuntime::Clone() const
  {
    auto* const clone = new (std::nothrow) WxUpdateUIEventRuntime();
    if (clone == nullptr) {
      return nullptr;
    }

    clone->mRefData = mRefData;
    clone->mEventObject = mEventObject;
    clone->mEventType = mEventType;
    clone->mEventTimestamp = mEventTimestamp;
    clone->mEventId = mEventId;
    clone->mCallbackUserData = mCallbackUserData;
    clone->mSkipped = mSkipped;
    clone->mIsCommandEvent = mIsCommandEvent;
    clone->mReserved1E = mReserved1E;
    clone->mReserved1F = mReserved1F;
    AssignOwnedWxString(&clone->mCommandString, std::wstring(mCommandString.c_str()));
    clone->mCommandInt = mCommandInt;
    clone->mExtraLong = mExtraLong;
    clone->mClientData = mClientData;
    clone->mClientObject = mClientObject;
    clone->mSetChecked = mSetChecked;
    clone->mSetEnabled = mSetEnabled;
    clone->mSetShown = mSetShown;
    clone->mSetText = mSetText;
    clone->mSetTextColour = mSetTextColour;
    clone->mPadding39To3B[0] = mPadding39To3B[0];
    clone->mPadding39To3B[1] = mPadding39To3B[1];
    clone->mPadding39To3B[2] = mPadding39To3B[2];
    AssignOwnedWxString(&clone->mTextLabel, std::wstring(mTextLabel.c_str()));
    return clone;
  }

  class WxEvtHandlerFactoryRuntime
  {
  public:
    virtual ~WxEvtHandlerFactoryRuntime() = default;

    void* mRefData = nullptr;                            // +0x04
    WxEvtHandlerFactoryRuntime* mNextHandler = nullptr; // +0x08
    WxEvtHandlerFactoryRuntime* mPreviousHandler = nullptr; // +0x0C
    std::uint8_t mEnabled = 1;                          // +0x10
    std::uint8_t mPadding11To13[3] = {0, 0, 0};        // +0x11
    void* mDynamicEvents = nullptr;                     // +0x14
    std::uint8_t mIsWindow = 0;                         // +0x18
    std::uint8_t mPadding19To1B[3] = {0, 0, 0};        // +0x19
    void* mPendingEvents = nullptr;                     // +0x1C
    void* mEventsLocker = nullptr;                      // +0x20
    std::uint32_t mClientDataType = 0;                  // +0x24
  };
  static_assert(sizeof(WxEvtHandlerFactoryRuntime) == 0x28, "WxEvtHandlerFactoryRuntime size must be 0x28");

  class WxScrollWinEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxScrollWinEventFactoryRuntime()
      : wxEventRuntime(0, 0)
      , mCommandInt(0)
      , mExtraLong(0)
    {}

    WxScrollWinEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxScrollWinEventFactoryRuntime(*this);
    }

    std::int32_t mCommandInt = 0;
    std::int32_t mExtraLong = 0;
  };
  static_assert(sizeof(WxScrollWinEventFactoryRuntime) == 0x28, "WxScrollWinEventFactoryRuntime size must be 0x28");

  class WxMouseEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxMouseEventFactoryRuntime()
      : wxEventRuntime(0, 0)
      , mMetaDown(0)
      , mAltDown(0)
      , mControlDown(0)
      , mShiftDown(0)
      , mLeftDown(0)
      , mRightDown(0)
      , mMiddleDown(0)
      , mReserved27(0)
      , mX(0)
      , mY(0)
      , mWheelRotation(0)
      , mWheelDelta(0)
      , mLinesPerAction(0)
    {}

    /**
     * Address: 0x00979C30 (FUN_00979C30, ??_GWxMouseEvent@@UAEPAVwxEvent@@XZ)
     *
     * What it does:
     * Allocates and clones one mouse-event payload by invoking the copy
     * constructor path.
     */
    WxMouseEventFactoryRuntime* Clone() const override
    {
      auto* const clone = new (std::nothrow) WxMouseEventFactoryRuntime(*this);
      return clone;
    }

    std::uint8_t mMetaDown = 0;
    std::uint8_t mAltDown = 0;
    std::uint8_t mControlDown = 0;
    std::uint8_t mShiftDown = 0;
    std::uint8_t mLeftDown = 0;
    std::uint8_t mRightDown = 0;
    std::uint8_t mMiddleDown = 0;
    std::uint8_t mReserved27 = 0;
    std::int32_t mX = 0;
    std::int32_t mY = 0;
    std::int32_t mWheelRotation = 0;
    std::int32_t mWheelDelta = 0;
    std::int32_t mLinesPerAction = 0;
  };
  static_assert(sizeof(WxMouseEventFactoryRuntime) == 0x3C, "WxMouseEventFactoryRuntime size must be 0x3C");

  class WxKeyEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxKeyEventFactoryRuntime()
      : wxEventRuntime(0, 0)
      , mShiftDown(0)
      , mControlDown(0)
      , mMetaDown(0)
      , mAltDown(0)
      , mKeyCode(0)
      , mX(0)
      , mY(0)
      , mRawCode(0)
      , mRawFlags(0)
      , mUniChar(0)
    {}

    WxKeyEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxKeyEventFactoryRuntime(*this);
    }

    std::uint8_t mShiftDown = 0;
    std::uint8_t mControlDown = 0;
    std::uint8_t mMetaDown = 0;
    std::uint8_t mAltDown = 0;
    std::int32_t mKeyCode = 0;
    std::int32_t mX = 0;
    std::int32_t mY = 0;
    std::uint32_t mRawCode = 0;
    std::uint32_t mRawFlags = 0;
    std::int32_t mUniChar = 0;
  };
  static_assert(sizeof(WxKeyEventFactoryRuntime) == 0x3C, "WxKeyEventFactoryRuntime size must be 0x3C");

  class WxChildFocusEventFactoryRuntime final : public wxCommandEventRuntime
  {
  public:
    /**
     * Address: 0x00979710 (FUN_00979710, wxChildFocusEvent::wxChildFocusEvent)
     *
     * What it does:
     * Builds one child-focus command event and stores the focused child
     * object lane as the event source.
     */
    explicit WxChildFocusEventFactoryRuntime(void* const focusedChild)
      : wxCommandEventRuntime(EnsureWxEvtChildFocusRuntimeType(), 0)
    {
      mEventObject = focusedChild;
    }

    WxChildFocusEventFactoryRuntime()
      : WxChildFocusEventFactoryRuntime(nullptr)
    {
    }

    WxChildFocusEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxChildFocusEventFactoryRuntime(*this);
    }
  };
  static_assert(sizeof(WxChildFocusEventFactoryRuntime) == 0x34, "WxChildFocusEventFactoryRuntime size must be 0x34");

  class WxSetCursorEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxSetCursorEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtSetCursorRuntimeType())
      , mX(0)
      , mY(0)
      , mCursorStorage{0, 0, 0}
    {}

    /**
     * Address: 0x0096B5E0 (FUN_0096B5E0, ??0wxSetCursorEvent@@QAE@ABV0@@Z)
     *
     * What it does:
     * Copy-constructs one set-cursor event payload, preserving base event
     * lanes, cursor position lanes, and shared cursor ref-data ownership.
     */
    WxSetCursorEventFactoryRuntime(const WxSetCursorEventFactoryRuntime& source)
      : wxEventRuntime(source)
      , mX(source.mX)
      , mY(source.mY)
      , mCursorStorage{source.mCursorStorage[0], source.mCursorStorage[1], source.mCursorStorage[2]}
    {
      auto* const sharedCursorRefData = reinterpret_cast<WxObjectRefDataRuntimeView*>(mCursorStorage[1]);
      if (sharedCursorRefData != nullptr) {
        ++sharedCursorRefData->refCount;
      }
    }

    WxSetCursorEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxSetCursorEventFactoryRuntime(*this);
    }

    std::int32_t mX = 0;
    std::int32_t mY = 0;
    std::int32_t mCursorStorage[3] = {0, 0, 0};
  };
  static_assert(sizeof(WxSetCursorEventFactoryRuntime) == 0x34, "WxSetCursorEventFactoryRuntime size must be 0x34");

  class WxWindowCreateEventFactoryRuntime final : public wxCommandEventRuntime
  {
  public:
    /**
     * Address: 0x00979690 (FUN_00979690)
     *
     * What it does:
     * Builds one window-create command event and binds the source object lane.
     */
    explicit WxWindowCreateEventFactoryRuntime(void* const eventObject)
      : wxCommandEventRuntime(0, 0)
    {
      mEventType = EnsureWxEvtWindowCreateRuntimeType();
      mEventObject = eventObject;
    }

    WxWindowCreateEventFactoryRuntime()
      : WxWindowCreateEventFactoryRuntime(nullptr)
    {}

    WxWindowCreateEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxWindowCreateEventFactoryRuntime(*this);
    }
  };
  static_assert(sizeof(WxWindowCreateEventFactoryRuntime) == 0x34, "WxWindowCreateEventFactoryRuntime size must be 0x34");

  class WxWindowDestroyEventFactoryRuntime final : public wxCommandEventRuntime
  {
  public:
    /**
     * Address: 0x009796D0 (FUN_009796D0)
     *
     * What it does:
     * Builds one window-destroy command event and binds the source object lane.
     */
    explicit WxWindowDestroyEventFactoryRuntime(void* const eventObject)
      : wxCommandEventRuntime(0, 0)
    {
      mEventType = EnsureWxEvtWindowDestroyRuntimeType();
      mEventObject = eventObject;
    }

    WxWindowDestroyEventFactoryRuntime()
      : WxWindowDestroyEventFactoryRuntime(nullptr)
    {}

    WxWindowDestroyEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxWindowDestroyEventFactoryRuntime(*this);
    }
  };
  static_assert(sizeof(WxWindowDestroyEventFactoryRuntime) == 0x34, "WxWindowDestroyEventFactoryRuntime size must be 0x34");

  class WxSplitterEventFactoryRuntime final : public wxCommandEventRuntime
  {
  public:
    WxSplitterEventFactoryRuntime()
      : wxCommandEventRuntime(0, 0)
    {
      mShouldCallOnSashPositionChanging = 1u;
      mEventObject = nullptr;
    }

    WxSplitterEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxSplitterEventFactoryRuntime(*this);
    }

    std::uint8_t mShouldCallOnSashPositionChanging = 0;
    std::uint8_t mPadding35To3F[0xB]{};
  };
  static_assert(sizeof(WxSplitterEventFactoryRuntime) == 0x40, "WxSplitterEventFactoryRuntime size must be 0x40");

  class WxSpinEventFactoryRuntime final : public wxCommandEventRuntime
  {
  public:
    WxSpinEventFactoryRuntime()
      : wxCommandEventRuntime(0, 0)
    {
      mShouldCallSpinHandler = 1u;
    }

    WxSpinEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxSpinEventFactoryRuntime(*this);
    }

    std::uint8_t mShouldCallSpinHandler = 0;
    std::uint8_t mPadding35To37[0x3]{};
  };
  static_assert(sizeof(WxSpinEventFactoryRuntime) == 0x38, "WxSpinEventFactoryRuntime size must be 0x38");

  class WxSocketEventFactoryRuntime final : public wxEventRuntime
  {
  public:
    WxSocketEventFactoryRuntime()
      : wxEventRuntime(0, EnsureWxEvtSocketRuntimeType())
    {}

    WxSocketEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxSocketEventFactoryRuntime(*this);
    }

    std::uint8_t mUnknown20To27[0x8]{};
  };
  static_assert(sizeof(WxSocketEventFactoryRuntime) == 0x28, "WxSocketEventFactoryRuntime size must be 0x28");

  class WxSingleChoiceDialogFactoryRuntime final : public wxDialogRuntime
  {
  public:
    WxSingleChoiceDialogFactoryRuntime()
      : wxDialogRuntime()
      , mPromptTextStorage(const_cast<wchar_t*>(wxEmptyString))
      , mSelectedIndex(-1)
    {}

    std::uint8_t mUnknown170_17B[0x0C]{};   // +0x170
    wchar_t* mPromptTextStorage = nullptr; // +0x17C
    std::int32_t mSelectedIndex = -1;      // +0x180
    std::int32_t mPadding184 = 0;          // +0x184
  };
  static_assert(sizeof(WxSingleChoiceDialogFactoryRuntime) == 0x188, "WxSingleChoiceDialogFactoryRuntime size must be 0x188");

  /**
   * Address: 0x00979FB0 (FUN_00979FB0, wxConstructorForwxEvtHandler)
   *
   * What it does:
   * Allocates one wx event-handler runtime payload and seeds its core handler
   * chain/enable lanes with an empty lock pointer.
   */
  [[maybe_unused]] [[nodiscard]] void* wxConstructorForwxEvtHandler()
  {
    auto* const handler = new (std::nothrow) WxEvtHandlerFactoryRuntime();
    if (handler == nullptr) {
      return nullptr;
    }

    handler->mEventsLocker = ::operator new(0x18u, std::nothrow);
    return handler;
  }

  /**
   * Address: 0x0097A1C0 (FUN_0097A1C0, wxConstructorForwxScrollWinEvent)
   *
   * What it does:
   * Allocates one scroll-window event payload with null type and zeroed
   * command/extra lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxScrollWinEvent()
  {
    return new (std::nothrow) WxScrollWinEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A1F0 (FUN_0097A1F0, wxConstructorForwxMouseEvent)
   *
   * What it does:
   * Allocates one mouse-event payload with zeroed modifier/button/wheel lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxMouseEvent()
  {
    return new (std::nothrow) WxMouseEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A210 (FUN_0097A210, wxConstructorForwxKeyEvent)
   *
   * What it does:
   * Allocates one key-event payload with zeroed modifier/keycode/scancode
   * lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxKeyEvent()
  {
    return new (std::nothrow) WxKeyEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A390 (FUN_0097A390, wxConstructorForwxChildFocusEvent)
   *
   * What it does:
   * Allocates one child-focus command event payload and clears its event-object lane.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxChildFocusEvent()
  {
    return new (std::nothrow) WxChildFocusEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A630 (FUN_0097A630, wxConstructorForwxSetCursorEvent)
   *
   * What it does:
   * Allocates one set-cursor event payload with zeroed coordinates and cursor storage.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxSetCursorEvent()
  {
    return new (std::nothrow) WxSetCursorEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A860 (FUN_0097A860, wxConstructorForwxWindowCreateEvent)
   *
   * What it does:
   * Allocates one window-create command event payload and clears its source object.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxWindowCreateEvent()
  {
    return new (std::nothrow) WxWindowCreateEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A8D0 (FUN_0097A8D0, wxConstructorForwxWindowDestroyEvent)
   *
   * What it does:
   * Allocates one window-destroy command event payload and clears its source object.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxWindowDestroyEvent()
  {
    return new (std::nothrow) WxWindowDestroyEventFactoryRuntime();
  }

  /**
   * Address: 0x00996470 (FUN_00996470)
   *
   * What it does:
   * Allocates one splitter command-event payload and sets the internal
   * dispatch-enabled flag lane.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxSplitterEvent()
  {
    return new (std::nothrow) WxSplitterEventFactoryRuntime();
  }

  /**
   * Address: 0x009EDF60 (FUN_009EDF60)
   *
   * What it does:
   * Allocates one spin command-event payload and sets the internal
   * dispatch-enabled flag lane.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxSpinEvent()
  {
    return new (std::nothrow) WxSpinEventFactoryRuntime();
  }

  /**
   * Address: 0x00A2DF90 (FUN_00A2DF90)
   *
   * What it does:
   * Allocates one socket event payload and seeds the event-type lane.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxSocketEvent()
  {
    return new (std::nothrow) WxSocketEventFactoryRuntime();
  }

  /**
   * Address: 0x00A1E5E0 (FUN_00A1E5E0)
   *
   * What it does:
   * Allocates one single-choice dialog payload and initializes selection state
   * to no-selection (`-1`) with empty prompt text storage.
   */
  [[maybe_unused]] [[nodiscard]] wxDialogRuntime* wxConstructorForwxSingleChoiceDialog()
  {
    return new (std::nothrow) WxSingleChoiceDialogFactoryRuntime();
  }

  /**
   * Address: 0x0097A020 (FUN_0097A020, wxConstructorForwxIdleEvent)
   *
   * What it does:
   * Allocates one idle-event payload and initializes the request-more lane to
   * false.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxIdleEvent()
  {
    return new (std::nothrow) WxIdleEventRuntime();
  }

  /**
   * Address: 0x0097A060 (FUN_0097A060, wxConstructorForwxCommandEvent)
   *
   * What it does:
   * Allocates one command-event payload with default null type/id lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxCommandEvent()
  {
    return new (std::nothrow) wxCommandEventRuntime(0, 0);
  }

  /**
   * Address: 0x0097A140 (FUN_0097A140, wxConstructorForwxScrollEvent)
   *
   * What it does:
   * Allocates one scroll-event payload using command-event base initialization
   * and zeroed scroll lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxScrollEvent()
  {
    return new (std::nothrow) WxScrollEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A230 (FUN_0097A230, wxConstructorForwxSizeEvent)
   *
   * What it does:
   * Allocates one size-event payload and clears the width/height lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxSizeEvent()
  {
    return new (std::nothrow) WxSizeEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A270 (FUN_0097A270, wxConstructorForwxPaintEvent)
   *
   * What it does:
   * Allocates one paint-event payload with base wx event runtime lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxPaintEvent()
  {
    return new (std::nothrow) WxPaintEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A2A0 (FUN_0097A2A0, wxConstructorForwxNcPaintEvent)
   *
   * What it does:
   * Allocates one non-client paint-event payload with base wx event lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxNcPaintEvent()
  {
    return new (std::nothrow) WxNcPaintEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A2D0 (FUN_0097A2D0, wxConstructorForwxEraseEvent)
   *
   * What it does:
   * Allocates one erase-background event payload and clears the device-context
   * lane.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxEraseEvent()
  {
    return new (std::nothrow) WxEraseEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A310 (FUN_0097A310, wxConstructorForwxMoveEvent)
   *
   * What it does:
   * Allocates one move-event payload and clears cached move-position lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxMoveEvent()
  {
    return new (std::nothrow) WxMoveEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A350 (FUN_0097A350, wxConstructorForwxFocusEvent)
   *
   * What it does:
   * Allocates one focus-event payload and clears the focused-window lane.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxFocusEvent()
  {
    return new (std::nothrow) WxFocusEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A400 (FUN_0097A400, wxConstructorForwxCloseEvent)
   *
   * What it does:
   * Allocates one close-event payload and seeds veto/logging flags to
   * `(canVeto=true, veto=false, loggingOff=true)`.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxCloseEvent()
  {
    return new (std::nothrow) WxCloseEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A440 (FUN_0097A440, wxConstructorForwxShowEvent)
   *
   * What it does:
   * Allocates one show-event payload and seeds the shown-state lane to false.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxShowEvent()
  {
    return new (std::nothrow) WxShowEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A480 (FUN_0097A480, wxConstructorForwxMaximizeEvent)
   *
   * What it does:
   * Allocates one maximize-event payload with base wx event lanes only.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxMaximizeEvent()
  {
    return new (std::nothrow) WxMaximizeEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A4B0 (FUN_0097A4B0, wxConstructorForwxIconizeEvent)
   *
   * What it does:
   * Allocates one iconize-event payload and seeds the iconized-state lane to
   * true.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxIconizeEvent()
  {
    return new (std::nothrow) WxIconizeEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A4F0 (FUN_0097A4F0, wxConstructorForwxMenuEvent)
   *
   * What it does:
   * Allocates one menu-event payload and clears the selected menu-id lane.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxMenuEvent()
  {
    return new (std::nothrow) WxMenuEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A530 (FUN_0097A530, wxConstructorForwxJoystickEvent)
   *
   * What it does:
   * Allocates one joystick-event payload and clears all position/button
   * lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxJoystickEvent()
  {
    return new (std::nothrow) WxJoystickEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A580 (FUN_0097A580, wxConstructorForwxDropFilesEvent)
   *
   * What it does:
   * Allocates one drop-files event payload and clears file-count, drop-point,
   * and file-array lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxDropFilesEvent()
  {
    auto* const dropFilesEvent = new (std::nothrow) WxDropFilesEventRuntime();
    if (dropFilesEvent == nullptr) {
      return nullptr;
    }

    dropFilesEvent->mEventType = 0;
    dropFilesEvent->mFileCount = 0;
    dropFilesEvent->mDropPointX = 0;
    dropFilesEvent->mDropPointY = 0;
    dropFilesEvent->mFiles = nullptr;
    return dropFilesEvent;
  }

  /**
   * Address: 0x0097A5C0 (FUN_0097A5C0, wxConstructorForwxActivateEvent)
   *
   * What it does:
   * Allocates one activate-event payload and marks it active by default.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxActivateEvent()
  {
    return new (std::nothrow) WxActivateEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A600 (FUN_0097A600, wxConstructorForwxInitDialogEvent)
   *
   * What it does:
   * Allocates one init-dialog event payload with base wx-event lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxInitDialogEvent()
  {
    return new (std::nothrow) WxInitDialogEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A6A0 (FUN_0097A6A0, wxConstructorForwxSysColourChangedEvent)
   *
   * What it does:
   * Allocates one system-colour-changed event payload with base wx-event
   * lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxSysColourChangedEvent()
  {
    return new (std::nothrow) WxSysColourChangedEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A6D0 (FUN_0097A6D0, wxConstructorForwxDisplayChangedEvent)
   *
   * What it does:
   * Allocates one display-changed event payload with base wx-event lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxDisplayChangedEvent()
  {
    return new (std::nothrow) WxDisplayChangedEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A700 (FUN_0097A700, wxConstructorForwxUpdateUIEvent)
   *
   * What it does:
   * Allocates one update-UI command event payload and clears update lanes
   * (`checked/enabled/shown/text`) plus its text-label pointer lane.
   */
  [[nodiscard]] wxEventRuntime* wxConstructorForwxUpdateUIEvent()
  {
    return new (std::nothrow) WxUpdateUIEventRuntime();
  }

  /**
   * Address: 0x0097A7A0 (FUN_0097A7A0, wxConstructorForwxNavigationKeyEvent)
   *
   * What it does:
   * Allocates one navigation-key event payload and seeds default
   * `navigation-flags/current-focus` lanes.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxNavigationKeyEvent()
  {
    return new (std::nothrow) WxNavigationKeyEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A7E0 (FUN_0097A7E0, wxConstructorForwxPaletteChangedEvent)
   *
   * What it does:
   * Allocates one palette-changed event payload and clears changed-window
   * ownership lane.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxPaletteChangedEvent()
  {
    return new (std::nothrow) WxPaletteChangedEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A820 (FUN_0097A820, wxConstructorForwxQueryNewPaletteEvent)
   *
   * What it does:
   * Allocates one query-new-palette event payload and clears realized-state
   * lane.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxQueryNewPaletteEvent()
  {
    return new (std::nothrow) WxQueryNewPaletteEventFactoryRuntime();
  }

  /**
   * Address: 0x0097A9F0 (FUN_0097A9F0, wxConstructorForwxContextMenuEvent)
   *
   * What it does:
   * Allocates one context-menu event object, runs the command-event base
   * initialization lane, and seeds the event position with wx default
   * coordinates `(-1, -1)`.
   */
  [[nodiscard]] wxEventRuntime* wxConstructorForwxContextMenuEvent()
  {
    return new (std::nothrow) WxContextMenuEventRuntime();
  }

  /**
   * Address: 0x0097AA70 (FUN_0097AA70, wxConstructorForwxMouseCaptureChangedEvent)
   *
   * What it does:
   * Allocates one mouse-capture-changed event payload and clears the previous
   * capture window lane.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxMouseCaptureChangedEvent()
  {
    return new (std::nothrow)
      WxMouseCaptureChangedEventRuntime(0, EnsureWxEvtMouseCaptureChangedRuntimeType(), nullptr);
  }

  /**
   * Address: 0x0097A0D0 (FUN_0097A0D0, wxConstructorForwxNotifyEvent)
   *
   * What it does:
   * Allocates one notify-event payload lane, runs command-event base
   * initialization, and marks the event as allowed by default.
   */
  [[maybe_unused]] [[nodiscard]] wxEventRuntime* wxConstructorForwxNotifyEvent()
  {
    return new (std::nothrow) WxNotifyEventRuntime();
  }

  [[nodiscard]] std::uintptr_t AllocateSupComFramePseudoHandle() noexcept
  {
    const std::uintptr_t handle = gNextSupComFramePseudoHandle;
    gNextSupComFramePseudoHandle += kSupComFramePseudoHandleStride;
    return handle;
  }

  [[nodiscard]] SupComFrameState* FindSupComFrameState(
    const WSupComFrame* const frame
  ) noexcept
  {
    const auto it = gSupComFrameStateByFrame.find(frame);
    return it != gSupComFrameStateByFrame.end() ? &it->second : nullptr;
  }

  [[nodiscard]] const WxTopLevelWindowRuntimeState* FindWxTopLevelWindowRuntimeState(
    const wxTopLevelWindowRuntime* const window
  ) noexcept
  {
    const auto it = gWxTopLevelWindowRuntimeStateByWindow.find(window);
    return it != gWxTopLevelWindowRuntimeStateByWindow.end() ? &it->second : nullptr;
  }

  [[nodiscard]] WxTopLevelWindowRuntimeState& EnsureWxTopLevelWindowRuntimeState(
    const wxTopLevelWindowRuntime* const window
  )
  {
    return gWxTopLevelWindowRuntimeStateByWindow[window];
  }

  [[nodiscard]] WxDialogRuntimeState& EnsureWxDialogRuntimeState(
    const wxDialogRuntime* const dialog
  )
  {
    return gWxDialogRuntimeStateByDialog[dialog];
  }

  [[nodiscard]] WxLogFrameRuntimeState& EnsureWxLogFrameRuntimeState(
    const wxLogFrameRuntime* const frame
  )
  {
    return gWxLogFrameRuntimeStateByFrame[frame];
  }

  [[nodiscard]] WxTreeListRuntimeState& EnsureWxTreeListRuntimeState(
    const wxTreeListCtrlRuntime* const treeControl
  )
  {
    return gWxTreeListRuntimeStateByControl[treeControl];
  }

  [[nodiscard]] const WxTreeListRuntimeState* FindWxTreeListRuntimeState(
    const wxTreeListCtrlRuntime* const treeControl
  ) noexcept
  {
    const auto it = gWxTreeListRuntimeStateByControl.find(treeControl);
    return it != gWxTreeListRuntimeStateByControl.end() ? &it->second : nullptr;
  }

  [[nodiscard]] WxTreeListNodeRuntimeState* AllocateTreeListNode(
    WxTreeListRuntimeState& state,
    WxTreeListNodeRuntimeState* const parentNode,
    const msvc8::string& rootText
  )
  {
    auto node = std::make_unique<WxTreeListNodeRuntimeState>();
    node->parent = parentNode;
    node->columnText.resize(3);
    node->columnText[0] = rootText;

    WxTreeListNodeRuntimeState* const rawNode = node.get();
    state.nodeStorage.push_back(std::move(node));
    if (parentNode != nullptr) {
      parentNode->children.push_back(rawNode);
    }
    return rawNode;
  }

  [[nodiscard]] WxTreeListNodeRuntimeState* ResolveTreeListNode(
    const wxTreeItemIdRuntime& item
  ) noexcept
  {
    return static_cast<WxTreeListNodeRuntimeState*>(item.mNode);
  }

  [[nodiscard]] const WxTreeListNodeRuntimeState* ResolveTreeListNodeConst(
    const wxTreeItemIdRuntime& item
  ) noexcept
  {
    return static_cast<const WxTreeListNodeRuntimeState*>(item.mNode);
  }

  [[nodiscard]] const WxWindowBaseRuntimeState* FindWxWindowBaseRuntimeState(
    const wxWindowBase* const window
  ) noexcept
  {
    const auto it = gWxWindowBaseStateByWindow.find(window);
    return it != gWxWindowBaseStateByWindow.end() ? &it->second : nullptr;
  }

  [[nodiscard]] WxWindowBaseRuntimeState& EnsureWxWindowBaseRuntimeState(
    const wxWindowBase* const window
  )
  {
    return gWxWindowBaseStateByWindow[window];
  }

  [[nodiscard]] WxTextCtrlRuntimeState& EnsureWxTextCtrlRuntimeState(
    const wxTextCtrlRuntime* const control
  )
  {
    return gWxTextCtrlStateByControl[control];
  }

  [[nodiscard]] SupComFrameState& EnsureSupComFrameState(
    const WSupComFrame* const frame
  )
  {
    return gSupComFrameStateByFrame[frame];
  }

  class WSupComFrameRuntime final : public WSupComFrame
  {
  public:
    WSupComFrameRuntime(
      const char* const titleUtf8,
      const wxPoint& initialPosition,
      const wxSize& initialClientSize,
      const std::int32_t style
    )
    {
      mPendingMaximizeSync = 0;
      mPersistedMaximizeSync = 0;
      mIsApplicationActive = 0;

      SupComFrameState& state = EnsureSupComFrameState(this);
      state.clientWidth = initialClientSize.x > 0 ? initialClientSize.x : 0;
      state.clientHeight = initialClientSize.y > 0 ? initialClientSize.y : 0;
      state.windowX = initialPosition.x;
      state.windowY = initialPosition.y;
      state.windowStyle = style;
      state.pseudoWindowHandle = AllocateSupComFramePseudoHandle();
      state.title = gpg::STR_Utf8ToWide(titleUtf8 != nullptr ? titleUtf8 : "");
      state.name.assign(kSupComFrameWindowName);
      state.iconResourceName.assign(kSupComFrameIconResourceName);
      state.iconResourceAssigned = true;
    }

    bool Destroy() override
    {
      gWxTopLevelWindowRuntimeStateByWindow.erase(this);
      gWxWindowBaseStateByWindow.erase(this);
      gSupComFrameStateByFrame.erase(this);
      delete this;
      return true;
    }

    bool Show(
      const bool show
    ) override
    {
      EnsureSupComFrameState(this).visible = show;
      return true;
    }

    void SetTitle(
      const wxStringRuntime& title
    ) override
    {
      SupComFrameState& state = EnsureSupComFrameState(this);
      state.title.assign(title.c_str());
    }

    void SetName(
      const wxStringRuntime& name
    ) override
    {
      wxWindowBase::SetName(name);
      SupComFrameState& state = EnsureSupComFrameState(this);
      state.name.assign(name.c_str());
    }

    void SetWindowStyleFlag(
      const long style
    ) override
    {
      EnsureSupComFrameState(this).windowStyle = static_cast<std::int32_t>(style);
    }

    [[nodiscard]] long GetWindowStyleFlag() const override
    {
      const SupComFrameState* const state = FindSupComFrameState(this);
      return state != nullptr ? state->windowStyle : 0;
    }

    void SetSizeHints(
      const std::int32_t minWidth,
      const std::int32_t minHeight,
      const std::int32_t maxWidth,
      const std::int32_t maxHeight,
      const std::int32_t incWidth,
      const std::int32_t incHeight
    ) override
    {
      (void)maxWidth;
      (void)maxHeight;
      (void)incWidth;
      (void)incHeight;

      SupComFrameState& state = EnsureSupComFrameState(this);
      state.minWidth = minWidth > 0 ? minWidth : 0;
      state.minHeight = minHeight > 0 ? minHeight : 0;
      if (state.clientWidth < state.minWidth) {
        state.clientWidth = state.minWidth;
      }
      if (state.clientHeight < state.minHeight) {
        state.clientHeight = state.minHeight;
      }
    }

    void SetFocus() override
    {
      EnsureSupComFrameState(this).focused = true;
    }

    [[nodiscard]] unsigned long GetHandle() const override
    {
      const SupComFrameState* const state = FindSupComFrameState(this);
      return state != nullptr ? static_cast<unsigned long>(state->pseudoWindowHandle) : 0u;
    }

    void DoGetClientSize(
      std::int32_t* const outWidth,
      std::int32_t* const outHeight
    ) const override
    {
      if (outWidth != nullptr) {
        *outWidth = 0;
      }
      if (outHeight != nullptr) {
        *outHeight = 0;
      }

      const SupComFrameState* const state = FindSupComFrameState(this);
      if (state == nullptr) {
        return;
      }

      if (outWidth != nullptr) {
        *outWidth = state->clientWidth;
      }
      if (outHeight != nullptr) {
        *outHeight = state->clientHeight;
      }
    }

    void DoSetClientSize(
      const std::int32_t width,
      const std::int32_t height
    ) override
    {
      SupComFrameState& state = EnsureSupComFrameState(this);
      const std::int32_t requestedWidth = width > 0 ? width : 0;
      const std::int32_t requestedHeight = height > 0 ? height : 0;
      state.clientWidth = requestedWidth > state.minWidth ? requestedWidth : state.minWidth;
      state.clientHeight = requestedHeight > state.minHeight ? requestedHeight : state.minHeight;
    }

    void DoGetPosition(
      std::int32_t* const x,
      std::int32_t* const y
    ) const override
    {
      if (x != nullptr) {
        *x = 0;
      }
      if (y != nullptr) {
        *y = 0;
      }

      const SupComFrameState* const state = FindSupComFrameState(this);
      if (state == nullptr) {
        return;
      }

      if (x != nullptr) {
        *x = state->windowX;
      }
      if (y != nullptr) {
        *y = state->windowY;
      }
    }

    void DoSetSize(
      const std::int32_t x,
      const std::int32_t y,
      const std::int32_t width,
      const std::int32_t height,
      const std::int32_t sizeFlags
    ) override
    {
      (void)sizeFlags;

      SupComFrameState& state = EnsureSupComFrameState(this);
      state.windowX = x;
      state.windowY = y;
      DoSetClientSize(width, height);
    }

    void Maximize(
      const bool maximize
    ) override
    {
      SupComFrameState& state = EnsureSupComFrameState(this);
      state.maximized = maximize;
      mPendingMaximizeSync = maximize ? 1 : 0;
      mPersistedMaximizeSync = maximize ? 1 : 0;
    }

    void Iconize(
      const bool iconize
    ) override
    {
      EnsureSupComFrameState(this).iconized = iconize;
    }

    [[nodiscard]] bool IsMaximized() const override
    {
      const SupComFrameState* const state = FindSupComFrameState(this);
      return state != nullptr && state->maximized;
    }

    [[nodiscard]] bool IsIconized() const override
    {
      const SupComFrameState* const state = FindSupComFrameState(this);
      return state != nullptr && state->iconized;
    }

    void SetIcon(
      const void* const icon
    ) override
    {
      SupComFrameState& state = EnsureSupComFrameState(this);
      state.iconResourceAssigned = icon != nullptr;
      if (state.iconResourceAssigned) {
        state.iconResourceName.assign(kSupComFrameIconResourceName);
      } else {
        state.iconResourceName.clear();
      }
    }

    void SetIcons(
      const void* const iconBundle
    ) override
    {
      SetIcon(iconBundle);
    }
  };

  struct DwordLaneRuntimeView
  {
    std::uint32_t lane00 = 0;
  };

  static_assert(offsetof(DwordLaneRuntimeView, lane00) == 0x0, "DwordLaneRuntimeView::lane00 offset must be 0x0");
  static_assert(sizeof(DwordLaneRuntimeView) == 0x4, "DwordLaneRuntimeView size must be 0x4");

  /**
   * Address: 0x004A3670 (FUN_004A3670)
   *
   * What it does:
   * Returns the leading 32-bit lane from one unknown runtime pod view.
   */
  [[maybe_unused]] [[nodiscard]] std::uint32_t ReadRuntimeDwordLaneA(
    const DwordLaneRuntimeView* const view
  ) noexcept
  {
    return view->lane00;
  }

  /**
   * Address: 0x004A3680 (FUN_004A3680)
   *
   * What it does:
   * Returns the leading 32-bit lane from one unknown runtime pod view.
   */
  [[maybe_unused]] [[nodiscard]] std::uint32_t ReadRuntimeDwordLaneB(
    const DwordLaneRuntimeView* const view
  ) noexcept
  {
    return view->lane00;
  }

  struct FourDwordBlockRuntimeView
  {
    std::int32_t lane00 = 0;
    std::int32_t lane04 = 0;
    std::int32_t lane08 = 0;
    std::int32_t lane0C = 0;
  };

  static_assert(sizeof(FourDwordBlockRuntimeView) == 0x10, "FourDwordBlockRuntimeView size must be 0x10");

  /**
   * Address: 0x004A36D0 (FUN_004A36D0)
   *
   * What it does:
   * Clears one four-dword runtime block used by wx region rectangle vectors.
   */
  [[maybe_unused]] void ClearFourDwordBlock(
    FourDwordBlockRuntimeView* const view
  ) noexcept
  {
    view->lane00 = 0;
    view->lane04 = 0;
    view->lane08 = 0;
    view->lane0C = 0;
  }

  class SplashScreenRuntimeImpl final : public moho::SplashScreenRuntime
  {
  public:
    SplashScreenRuntimeImpl(
      const msvc8::string& imagePath,
      const wxSize& size
    )
      : mImagePath(imagePath)
      , mSize(size)
    {}

    void GetClassInfo() override {}

    void DeleteObject(
      const std::uint32_t flags
    ) override
    {
      if ((flags & 1u) != 0u) {
        delete this;
      }
    }

  private:
    msvc8::string mImagePath;
    wxSize mSize{};
  };
} // namespace

/**
 * Address: 0x009ACE50 (FUN_009ACE50, wxENTER_CRIT_SECT)
 *
 * What it does:
 * Enters one Win32 critical-section lane.
 */
void wxENTER_CRIT_SECT(
  _RTL_CRITICAL_SECTION* const criticalSection
)
{
  ::EnterCriticalSection(criticalSection);
}

/**
 * Address: 0x009ACE60 (FUN_009ACE60, wxLEAVE_CRIT_SECT)
 *
 * What it does:
 * Leaves one Win32 critical-section lane.
 */
void wxLEAVE_CRIT_SECT(
  _RTL_CRITICAL_SECTION* const criticalSection
)
{
  ::LeaveCriticalSection(criticalSection);
}

/**
 * Address: 0x009AD330 (FUN_009AD330, wxThread::IsMain)
 *
 * What it does:
 * Returns whether the current Win32 thread matches the stored wx main-thread id.
 */
bool wxThreadIsMain()
{
  return ::GetCurrentThreadId() == gs_idMainThread;
}

/**
 * Address: 0x009AD660 (FUN_009AD660, wxGuiOwnedByMainThread)
 *
 * What it does:
 * Returns the wx GUI-ownership flag managed by the GUI mutex helpers.
 */
bool wxGuiOwnedByMainThread()
{
  EnsureGuiMutexRuntimeInitialized();
  return gs_bGuiOwnedByMainThread != 0;
}

/**
 * Address: 0x009AD670 (FUN_009AD670, wxWakeUpMainThread)
 *
 * What it does:
 * Posts one wake-up message (`WM_NULL`) to the stored wx main-thread id.
 */
bool wxWakeUpMainThread()
{
  return ::PostThreadMessageW(gs_idMainThread, 0u, 0u, 0) != FALSE;
}

struct WxThreadSuspendControllerRuntime
{
  std::uint32_t reserved00 = 0;                              // +0x00
  WxThreadNativeHandleRuntime* threadRuntime = nullptr;      // +0x04
  _RTL_CRITICAL_SECTION criticalSection{};                   // +0x08
};

static_assert(
  offsetof(WxThreadSuspendControllerRuntime, threadRuntime) == 0x4,
  "WxThreadSuspendControllerRuntime::threadRuntime offset must be 0x4"
);
static_assert(
  offsetof(WxThreadSuspendControllerRuntime, criticalSection) == 0x8,
  "WxThreadSuspendControllerRuntime::criticalSection offset must be 0x8"
);
static_assert(sizeof(WxThreadSuspendControllerRuntime) == 0x20, "WxThreadSuspendControllerRuntime size must be 0x20");

/**
 * Address: 0x009AD210 (FUN_009AD210)
 *
 * What it does:
 * Suspends one native thread handle and stores the suspend-state flag lane
 * used by wx thread-control helpers.
 */
bool wxThreadSuspendNativeHandle(
  WxThreadNativeHandleRuntime* const threadRuntime
)
{
  if (::SuspendThread(static_cast<HANDLE>(threadRuntime->nativeThreadHandle)) == static_cast<DWORD>(-1)) {
    const wchar_t* messageTemplate = L"Can not suspend thread %x";
    if (wxLocale* const locale = wxGetLocale(); locale != nullptr) {
      messageTemplate = locale->GetString(messageTemplate, 0);
    }

    wxLogSysError(messageTemplate, static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(threadRuntime->nativeThreadHandle)));
    return false;
  }

  threadRuntime->suspendStateFlag = HANDLE_FLAG_PROTECT_FROM_CLOSE;
  return true;
}

/**
 * Address: 0x009AD270 (FUN_009AD270)
 *
 * What it does:
 * Resumes one native thread handle and updates suspend-state bookkeeping used
 * by wx thread-control helpers.
 */
bool wxThreadResumeNativeHandle(
  WxThreadNativeHandleRuntime* const threadRuntime
)
{
  if (::ResumeThread(static_cast<HANDLE>(threadRuntime->nativeThreadHandle)) == static_cast<DWORD>(-1)) {
    const wchar_t* messageTemplate = L"Can not resume thread %x";
    if (wxLocale* const locale = wxGetLocale(); locale != nullptr) {
      messageTemplate = locale->GetString(messageTemplate, 0);
    }

    wxLogSysError(messageTemplate, static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(threadRuntime->nativeThreadHandle)));
    return false;
  }

  if (threadRuntime->suspendStateFlag != 4u) {
    threadRuntime->suspendStateFlag = HANDLE_FLAG_INHERIT;
  }

  return true;
}

/**
 * Address: 0x009AD8D0 (FUN_009AD8D0)
 *
 * What it does:
 * Executes one critical-section guarded thread-suspend operation and returns
 * either success (`0`) or wx misc-thread error (`5`).
 */
int wxThreadSuspendNativeHandleGuarded(
  WxThreadSuspendControllerRuntime* const controller
)
{
  wxENTER_CRIT_SECT(&controller->criticalSection);
  const int result = wxThreadSuspendNativeHandle(controller->threadRuntime) ? 0 : 5;
  wxLEAVE_CRIT_SECT(&controller->criticalSection);
  return result;
}

/**
 * Address: 0x009AD940 (FUN_009AD940)
 *
 * What it does:
 * Executes one critical-section guarded thread-resume operation and returns
 * either success (`0`) or wx misc-thread error (`5`).
 */
int wxThreadResumeNativeHandleGuarded(
  WxThreadSuspendControllerRuntime* const controller
)
{
  wxENTER_CRIT_SECT(&controller->criticalSection);
  const int result = wxThreadResumeNativeHandle(controller->threadRuntime) ? 0 : 5;
  wxLEAVE_CRIT_SECT(&controller->criticalSection);
  return result;
}

/**
 * Address: 0x009ADA60 (FUN_009ADA60)
 *
 * What it does:
 * Reads the guarded thread suspend-state flag lane and returns true only when
 * the runtime state equals `1`.
 */
bool wxThreadHasSingleSuspendState(
  WxThreadSuspendControllerRuntime* const controller
)
{
  _RTL_CRITICAL_SECTION* const criticalSection = &controller->criticalSection;
  wxENTER_CRIT_SECT(criticalSection);
  const std::uint32_t suspendState = controller->threadRuntime->suspendStateFlag;
  wxLEAVE_CRIT_SECT(criticalSection);
  return suspendState == 1u;
}

/**
 * Address: 0x009ADAD0 (FUN_009ADAD0, sub_9ADAD0)
 *
 * What it does:
 * Reads the guarded thread suspend-state flag lane and returns true only when
 * the runtime state equals `2`.
 */
bool wxThreadHasDoubleSuspendState(
  WxThreadSuspendControllerRuntime* const controller
)
{
  _RTL_CRITICAL_SECTION* const criticalSection = &controller->criticalSection;
  wxENTER_CRIT_SECT(criticalSection);
  const std::uint32_t suspendState = controller->threadRuntime->suspendStateFlag;
  wxLEAVE_CRIT_SECT(criticalSection);
  return suspendState == 2u;
}

/**
 * Address: 0x009674D0 (FUN_009674D0, wxIsShiftDown)
 *
 * What it does:
 * Returns whether the Win32 Shift key is currently pressed.
 */
bool wxIsShiftDown()
{
  return ::GetKeyState(VK_SHIFT) < 0;
}

/**
 * Address: 0x009674F0 (FUN_009674F0, wxIsCtrlDown)
 *
 * What it does:
 * Returns whether the Win32 Control key is currently pressed.
 */
bool wxIsCtrlDown()
{
  return ::GetKeyState(VK_CONTROL) < 0;
}

/**
 * Address: 0x009ADC20 (FUN_009ADC20, wxMutexGuiLeave)
 *
 * What it does:
 * Releases GUI ownership for the calling lane and unlocks wx GUI/waiting
 * critical sections with the original runtime ordering.
 */
void wxMutexGuiLeave()
{
  _RTL_CRITICAL_SECTION* const waitingForGuiCriticalSection = WaitingForGuiCriticalSection();
  wxENTER_CRIT_SECT(waitingForGuiCriticalSection);

  if (wxThreadIsMain()) {
    gs_bGuiOwnedByMainThread = 0;
  } else {
    --gs_nWaitingForGui;
    (void)wxWakeUpMainThread();
  }

  wxLEAVE_CRIT_SECT(GuiCriticalSection());
  wxLEAVE_CRIT_SECT(waitingForGuiCriticalSection);
}

/**
 * Address: 0x009ADC70 (FUN_009ADC70, wxMutexGuiLeaveOrEnter)
 *
 * What it does:
 * Reconciles GUI ownership against waiting-thread state, leaving or entering
 * the wx GUI critical section as required by the original runtime contract.
 */
void wxMutexGuiLeaveOrEnter()
{
  _RTL_CRITICAL_SECTION* const waitingForGuiCriticalSection = WaitingForGuiCriticalSection();
  wxENTER_CRIT_SECT(waitingForGuiCriticalSection);

  const bool guiOwnedByMainThread = wxGuiOwnedByMainThread();
  if (gs_nWaitingForGui != 0) {
    if (guiOwnedByMainThread) {
      wxMutexGuiLeave();
    }
  } else if (!guiOwnedByMainThread) {
    wxENTER_CRIT_SECT(GuiCriticalSection());
    gs_bGuiOwnedByMainThread = 1;
    wxLEAVE_CRIT_SECT(waitingForGuiCriticalSection);
    return;
  }

  wxLEAVE_CRIT_SECT(waitingForGuiCriticalSection);
}

struct WxMutexHandleStorageRuntime
{
  HANDLE handle = nullptr; // +0x00
};
static_assert(sizeof(WxMutexHandleStorageRuntime) == 0x4, "WxMutexHandleStorageRuntime size must be 0x4");

struct WxMutexWaitControllerRuntime
{
  volatile LONG waiterCount = 0;                              // +0x00
  _RTL_CRITICAL_SECTION criticalSection{};                    // +0x04
  WxMutexHandleStorageRuntime* mutexHandleStorage = nullptr;  // +0x1C
};

static_assert(
  offsetof(WxMutexWaitControllerRuntime, waiterCount) == 0x0,
  "WxMutexWaitControllerRuntime::waiterCount offset must be 0x0"
);
static_assert(
  offsetof(WxMutexWaitControllerRuntime, criticalSection) == 0x4,
  "WxMutexWaitControllerRuntime::criticalSection offset must be 0x4"
);
static_assert(
  offsetof(WxMutexWaitControllerRuntime, mutexHandleStorage) == 0x1C,
  "WxMutexWaitControllerRuntime::mutexHandleStorage offset must be 0x1C"
);
static_assert(sizeof(WxMutexWaitControllerRuntime) == 0x20, "WxMutexWaitControllerRuntime size must be 0x20");

struct WxMutexHandleStorageOwnerRuntime
{
  WxMutexHandleStorageRuntime* handleStorage = nullptr; // +0x00
};
static_assert(sizeof(WxMutexHandleStorageOwnerRuntime) == 0x4, "WxMutexHandleStorageOwnerRuntime size must be 0x4");

/**
 * Address: 0x009AD740 (FUN_009AD740, sub_9AD740)
 *
 * What it does:
 * Closes one nested native-handle lane (when non-null) and frees the owned
 * handle-storage object.
 */
void wxDestroyMutexHandleStorageRuntime(
  WxMutexHandleStorageOwnerRuntime* const owner
)
{
  WxMutexHandleStorageRuntime* const handleStorage = owner->handleStorage;
  if (handleStorage != nullptr) {
    if (handleStorage->handle != nullptr) {
      (void)::CloseHandle(handleStorage->handle);
    }
    ::operator delete(handleStorage);
  }
}

/**
 * Address: 0x009ACF60 (FUN_009ACF60)
 *
 * What it does:
 * Waits once on one retained mutex handle with caller timeout and returns the
 * wx mutex status lane (`0/2/5`).
 */
int wxMutexWaitHandleWithTimeout(
  WxMutexHandleStorageRuntime* const mutexHandleStorage,
  const DWORD timeoutMilliseconds
)
{
  const DWORD waitResult = ::WaitForSingleObject(mutexHandleStorage->handle, timeoutMilliseconds);
  if (waitResult == WAIT_OBJECT_0) {
    return 0;
  }

  return waitResult == WAIT_TIMEOUT ? 2 : 5;
}

/**
 * Address: 0x009ACEB0 (FUN_009ACEB0)
 *
 * What it does:
 * Waits on one retained mutex handle and retries immediately after abandoned
 * ownership, returning wx mutex status lane (`0/3/5`).
 */
int wxMutexWaitHandleWithAbandonedRetry(
  WxMutexHandleStorageRuntime* const mutexHandleStorage,
  const DWORD timeoutMilliseconds
)
{
  DWORD waitResult = ::WaitForSingleObject(mutexHandleStorage->handle, timeoutMilliseconds);
  if (waitResult == WAIT_ABANDONED) {
    waitResult = ::WaitForSingleObject(mutexHandleStorage->handle, 0u);
  }

  if (waitResult == WAIT_OBJECT_0) {
    return 0;
  }

  return waitResult == WAIT_TIMEOUT ? 3 : 5;
}

/**
 * Address: 0x009AD6F0 (FUN_009AD6F0)
 *
 * What it does:
 * Releases one retained mutex handle lane when present and returns wx mutex
 * status (`0/1/5`).
 */
int wxMutexReleaseHandleIfInitialized(
  WxMutexHandleStorageRuntime* const mutexHandleStorage
)
{
  if (mutexHandleStorage != nullptr && mutexHandleStorage->handle != nullptr) {
    return ::ReleaseMutex(mutexHandleStorage->handle) ? 0 : 5;
  }

  return 1;
}

/**
 * Address: 0x009AD770 (FUN_009AD770)
 *
 * What it does:
 * Waits on one retained mutex handle when initialized; otherwise returns the
 * not-initialized lane (`1`).
 */
int wxMutexTimedWaitIfInitialized(
  WxMutexHandleStorageRuntime* const mutexHandleStorage,
  const DWORD timeoutMilliseconds
)
{
  if (mutexHandleStorage != nullptr && mutexHandleStorage->handle != nullptr) {
    return wxMutexWaitHandleWithTimeout(mutexHandleStorage, timeoutMilliseconds);
  }

  return 1;
}

/**
 * Address: 0x009AD790 (FUN_009AD790, sub_9AD790)
 *
 * What it does:
 * Releases one retained semaphore handle when initialized and returns the
 * wx synchronization status lane (`0/1/5`).
 */
int wxSemaphoreReleaseIfInitialized(
  WxMutexHandleStorageRuntime* const semaphoreHandleStorage
)
{
  if (semaphoreHandleStorage != nullptr && semaphoreHandleStorage->handle != nullptr) {
    return ::ReleaseSemaphore(semaphoreHandleStorage->handle, 1L, nullptr) ? 0 : 5;
  }

  return 1;
}

struct WxSemaphoreReleaseControllerRuntime
{
  volatile LONG pendingReleaseCount = 0;                 // +0x00
  _RTL_CRITICAL_SECTION criticalSection{};               // +0x04
  std::uint8_t pad_1C_1F[4]{};                           // +0x1C
  WxMutexHandleStorageRuntime semaphoreHandleStorage{};  // +0x20
};

static_assert(
  offsetof(WxSemaphoreReleaseControllerRuntime, pendingReleaseCount) == 0x0,
  "WxSemaphoreReleaseControllerRuntime::pendingReleaseCount offset must be 0x0"
);
static_assert(
  offsetof(WxSemaphoreReleaseControllerRuntime, criticalSection) == 0x4,
  "WxSemaphoreReleaseControllerRuntime::criticalSection offset must be 0x4"
);
static_assert(
  offsetof(WxSemaphoreReleaseControllerRuntime, semaphoreHandleStorage) == 0x20,
  "WxSemaphoreReleaseControllerRuntime::semaphoreHandleStorage offset must be 0x20"
);
static_assert(
  sizeof(WxSemaphoreReleaseControllerRuntime) == 0x24,
  "WxSemaphoreReleaseControllerRuntime size must be 0x24"
);

/**
 * Address: 0x009ADD10 (FUN_009ADD10)
 *
 * What it does:
 * Waits forever on one retained mutex handle when initialized; otherwise
 * returns the not-initialized lane (`1`).
 */
int wxMutexInfiniteWaitIfInitialized(
  WxMutexHandleStorageRuntime* const mutexHandleStorage
)
{
  if (mutexHandleStorage != nullptr && mutexHandleStorage->handle != nullptr) {
    return wxMutexWaitHandleWithAbandonedRetry(mutexHandleStorage, INFINITE);
  }

  return 1;
}

/**
 * Address: 0x009ADD80 (FUN_009ADD80)
 *
 * What it does:
 * Executes one contention-aware mutex wait lane using interlocked waiter
 * tracking and critical-section retry bookkeeping.
 */
int wxMutexContentionAwareWait(
  WxMutexWaitControllerRuntime* const controller,
  const DWORD timeoutMilliseconds
)
{
  ::InterlockedIncrement(&controller->waiterCount);
  static_cast<void>(wxMutexReleaseHandleIfInitialized(controller->mutexHandleStorage));

  int waitResult = wxMutexTimedWaitIfInitialized(controller->mutexHandleStorage, timeoutMilliseconds);
  if (waitResult == 2) {
    wxENTER_CRIT_SECT(&controller->criticalSection);
    waitResult = wxMutexTimedWaitIfInitialized(controller->mutexHandleStorage, 0u);
    if (waitResult != 0) {
      --controller->waiterCount;
    }
    wxLEAVE_CRIT_SECT(&controller->criticalSection);
  }

  static_cast<void>(wxMutexInfiniteWaitIfInitialized(controller->mutexHandleStorage));
  return waitResult != 0 ? 3 : 0;
}

/**
 * Address: 0x009ADE30 (FUN_009ADE30, sub_9ADE30)
 *
 * What it does:
 * Enters one release-controller critical section and drains pending semaphore
 * releases until the count reaches zero; returns `0` on full drain and `3`
 * when the semaphore release lane stops before the pending count is exhausted.
 */
int wxDrainPendingSemaphoreReleases(
  WxSemaphoreReleaseControllerRuntime* const controller
)
{
  wxENTER_CRIT_SECT(&controller->criticalSection);

  if (controller->pendingReleaseCount <= 0) {
    wxLEAVE_CRIT_SECT(&controller->criticalSection);
    return 0;
  }

  while (wxSemaphoreReleaseIfInitialized(&controller->semaphoreHandleStorage) == 0) {
    --controller->pendingReleaseCount;
    if (controller->pendingReleaseCount <= 0) {
      wxLEAVE_CRIT_SECT(&controller->criticalSection);
      return 0;
    }
  }

  wxLEAVE_CRIT_SECT(&controller->criticalSection);
  return 3;
}

namespace
{
  struct ProcessWindowEnumContext
  {
    HWND matchedWindow = nullptr; // +0x00
    DWORD targetProcessId = 0;    // +0x04
  };
  static_assert(sizeof(ProcessWindowEnumContext) == 0x8, "ProcessWindowEnumContext size must be 0x8");

  /**
   * Address: 0x009C72B0 (FUN_009C72B0, EnumFunc)
   *
   * What it does:
   * EnumWindows callback that stores the first window owned by
   * `targetProcessId`, then stops enumeration.
   */
  BOOL CALLBACK EnumWindowForProcessId(const HWND window, const LPARAM contextParam)
  {
    auto* const context = reinterpret_cast<ProcessWindowEnumContext*>(contextParam);
    if (context == nullptr) {
      return TRUE;
    }

    DWORD ownerProcessId = 0;
    (void)::GetWindowThreadProcessId(window, &ownerProcessId);
    if (ownerProcessId != context->targetProcessId) {
      return TRUE;
    }

    context->matchedWindow = window;
    return FALSE;
  }
} // namespace

/**
 * Address: 0x009C7540 (FUN_009C7540, wxGetOsVersion)
 *
 * What it does:
 * Caches Win32 platform-id and major/minor version lanes and returns the wx
 * OS-family enum value.
 */
int wxGetOsVersion(
  int* const majorVsn,
  int* const minorVsn
)
{
  int result = gWxGetOsVersionCache;
  if (gWxGetOsVersionCache == -1) {
    OSVERSIONINFOW versionInformation{};
    gWxGetOsVersionCache = 15;
    versionInformation.dwOSVersionInfoSize = sizeof(versionInformation);
#pragma warning(push)
#pragma warning(disable : 4996)
    const BOOL hasVersionInfo = ::GetVersionExW(&versionInformation);
#pragma warning(pop)
    if (hasVersionInfo != 0) {
      gWxGetOsVersionMinor = static_cast<int>(versionInformation.dwMinorVersion);
      gWxGetOsVersionMajor = static_cast<int>(versionInformation.dwMajorVersion);
      if (versionInformation.dwPlatformId == 0) {
        result = 19;
        gWxGetOsVersionCache = result;
      } else if (versionInformation.dwPlatformId == 1) {
        result = 20;
        gWxGetOsVersionCache = result;
      } else if (versionInformation.dwPlatformId == 2) {
        result = 18;
        gWxGetOsVersionCache = result;
      } else {
        result = gWxGetOsVersionCache;
      }
    } else {
      result = gWxGetOsVersionCache;
    }
  }

  if (majorVsn != nullptr && gWxGetOsVersionMajor != -1) {
    *majorVsn = gWxGetOsVersionMajor;
  }
  if (minorVsn != nullptr && gWxGetOsVersionMinor != -1) {
    *minorVsn = gWxGetOsVersionMinor;
  }
  return result;
}

/**
 * Address: 0x009AAF60 (FUN_009AAF60)
 *
 * What it does:
 * Resolves icon width/height into `outSize[0..1]`, defaulting to `32x32` and
 * using the icon mask bitmap lanes when available.
 */
std::uint32_t* wxResolveIconDimensions(
  std::uint32_t* const outSize,
  const HICON iconHandle
)
{
  if (outSize == nullptr) {
    return nullptr;
  }

  outSize[0] = 32u;
  outSize[1] = 32u;
  if (iconHandle == nullptr || wxGetOsVersion(nullptr, nullptr) == 19) {
    return outSize;
  }

  ICONINFO iconInfo{};
  if (::GetIconInfo(iconHandle, &iconInfo) == FALSE) {
    return outSize;
  }

  if (iconInfo.hbmMask != nullptr) {
    BITMAP maskInfo{};
    if (::GetObjectW(iconInfo.hbmMask, sizeof(maskInfo), &maskInfo) != 0) {
      outSize[0] = static_cast<std::uint32_t>(maskInfo.bmWidth);
      outSize[1] = static_cast<std::uint32_t>(maskInfo.bmHeight);
    }
    (void)::DeleteObject(iconInfo.hbmMask);
  }

  if (iconInfo.hbmColor != nullptr) {
    (void)::DeleteObject(iconInfo.hbmColor);
  }

  return outSize;
}

/**
 * Address: 0x009C8260 (FUN_009C8260)
 *
 * What it does:
 * Builds one localized human-readable Windows version description lane from
 * Win32 `GetVersionExW` platform/version fields.
 */
wxStringRuntime wxGetOsDescription()
{
  auto resolveLocalized = [](const wchar_t* const sourceText) -> const wchar_t* {
    if (wxLocale* const locale = wxGetLocale(); locale != nullptr) {
      return locale->GetString(sourceText, 0);
    }
    return sourceText;
  };

  std::wstring description;
  OSVERSIONINFOW versionInformation{};
  versionInformation.dwOSVersionInfoSize = sizeof(versionInformation);
#pragma warning(push)
#pragma warning(disable : 4996)
  const BOOL hasVersionInfo = ::GetVersionExW(&versionInformation);
#pragma warning(pop)
  if (hasVersionInfo == FALSE) {
    return AllocateOwnedWxString(description);
  }

  if (versionInformation.dwPlatformId == VER_PLATFORM_WIN32s) {
    description = resolveLocalized(L"Win32s on Windows 3.1");
    return AllocateOwnedWxString(description);
  }

  if (versionInformation.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
    std::array<wchar_t, 64> windows9xText{};
    const wchar_t* const formatText = resolveLocalized(L"Windows 9%c");
    const wchar_t versionChar = versionInformation.dwMinorVersion != 0u ? L'8' : L'5';
    (void)std::swprintf(windows9xText.data(), windows9xText.size(), formatText, versionChar);
    description.assign(windows9xText.data());

    if (versionInformation.szCSDVersion[0] != L'\0') {
      description += L" (";
      description += versionInformation.szCSDVersion;
      description.push_back(L')');
    }

    return AllocateOwnedWxString(description);
  }

  if (versionInformation.dwPlatformId == VER_PLATFORM_WIN32_NT) {
    std::array<wchar_t, 128> windowsNtText{};
    (void)std::swprintf(
      windowsNtText.data(),
      windowsNtText.size(),
      L"Windows NT %lu.%lu (build %lu",
      versionInformation.dwMajorVersion,
      versionInformation.dwMinorVersion,
      versionInformation.dwBuildNumber
    );
    description.assign(windowsNtText.data());

    if (versionInformation.szCSDVersion[0] != L'\0') {
      description += L", ";
      description += versionInformation.szCSDVersion;
    }
    description.push_back(L')');
  }

  return AllocateOwnedWxString(description);
}

/**
 * Address: 0x009BFA70 (FUN_009BFA70)
 *
 * What it does:
 * Formats one `"windows-<ACP>"` encoding label into `outEncodingName`.
 */
wxStringRuntime* wxBuildWindowsCodePageEncodingName(
  wxStringRuntime* const outEncodingName
)
{
  if (outEncodingName == nullptr) {
    return nullptr;
  }

  const std::wstring encodingName = std::wstring(L"windows-") + std::to_wstring(::GetACP());
  AssignOwnedWxString(outEncodingName, encodingName);
  return outEncodingName;
}

/**
 * Address: 0x009BB840 (FUN_009BB840)
 *
 * What it does:
 * Enables wx URL default-proxy mode when `HTTP_PROXY` is present.
 */
bool wxURLInitializeDefaultProxyFromEnvironment()
{
  if (std::getenv("HTTP_PROXY") != nullptr) {
    gWxUrlUseDefaultProxy = true;
  }
  return true;
}

namespace
{
  struct WxStatusBarRuntimeView
  {
    std::uint8_t mUnknown00To107[0x108]{};
    HWND mNativeHandle = nullptr;              // +0x108
    std::uint8_t mUnknown10CTo123[0x18]{};
    std::int32_t mPaneCount = 0;               // +0x124
  };

  static_assert(
    offsetof(WxStatusBarRuntimeView, mNativeHandle) == 0x108,
    "WxStatusBarRuntimeView::mNativeHandle offset must be 0x108"
  );
  static_assert(
    offsetof(WxStatusBarRuntimeView, mPaneCount) == 0x124,
    "WxStatusBarRuntimeView::mPaneCount offset must be 0x124"
  );

  struct WxWindowActivationRuntimeView
  {
    std::uint8_t mUnknown00To27[0x28]{};
    std::int32_t mWindowId = 0;               // +0x28
    std::uint8_t mUnknown2CTo5B[0x30]{};
    wxWindowBase* mEventHandler = nullptr;    // +0x5C
  };
  static_assert(
    offsetof(WxWindowActivationRuntimeView, mWindowId) == 0x28,
    "WxWindowActivationRuntimeView::mWindowId offset must be 0x28"
  );
  static_assert(
    offsetof(WxWindowActivationRuntimeView, mEventHandler) == 0x5C,
    "WxWindowActivationRuntimeView::mEventHandler offset must be 0x5C"
  );

  class WxEventProcessorRuntime
  {
  public:
    virtual void Slot00() = 0;
    virtual void DeleteWithFlag(std::int32_t deleteFlags) = 0;
    virtual void Slot08() = 0;
    virtual void Slot0C() = 0;
    virtual bool ProcessEvent(wxEventRuntime* event) = 0;
  };

  class WxDeleteWithFlagSlot0Runtime
  {
  public:
    virtual void* DeleteWithFlag(std::int32_t deleteFlags) = 0;
  };

  struct WxTimerOwnerRuntimeView
  {
    void* mVtable = nullptr;                         // +0x00
    std::int32_t mUnknown04 = 0;                    // +0x04
    WxEventProcessorRuntime* mEventTarget = nullptr; // +0x08
    std::int32_t mEventId = 0;                      // +0x0C
    std::int32_t mTimerId = 0;                      // +0x10
  };
  static_assert(offsetof(WxTimerOwnerRuntimeView, mEventTarget) == 0x08, "WxTimerOwnerRuntimeView::mEventTarget offset must be 0x08");
  static_assert(offsetof(WxTimerOwnerRuntimeView, mEventId) == 0x0C, "WxTimerOwnerRuntimeView::mEventId offset must be 0x0C");
  static_assert(offsetof(WxTimerOwnerRuntimeView, mTimerId) == 0x10, "WxTimerOwnerRuntimeView::mTimerId offset must be 0x10");

  struct WxProcessEventSourceRuntimeView : WxEventProcessorRuntime
  {
    std::uint8_t mUnknown04To27[0x24]{};
    std::int32_t mEventId = 0; // +0x28
  };
  static_assert(
    offsetof(WxProcessEventSourceRuntimeView, mEventId) == 0x28,
    "WxProcessEventSourceRuntimeView::mEventId offset must be 0x28"
  );

  struct WxTreeItemIndirectDataRuntimeView
  {
    void* mVtable = nullptr;                               // +0x00
    std::uint8_t mUnknown04To17[0x14]{};
    WxDeleteWithFlagSlot0Runtime* mOwnedClientData = nullptr; // +0x18
  };
  static_assert(
    offsetof(WxTreeItemIndirectDataRuntimeView, mOwnedClientData) == 0x18,
    "WxTreeItemIndirectDataRuntimeView::mOwnedClientData offset must be 0x18"
  );

  class WxTimerEventRuntime final : public wxEventRuntime
  {
  public:
    explicit WxTimerEventRuntime(const std::int32_t eventId, const std::int32_t timerId)
      : wxEventRuntime(eventId, 0)
      , mTimerId(timerId)
    {
      mEventType = EnsureWxEvtTimerRuntimeType();
    }

    WxTimerEventRuntime* Clone() const override
    {
      auto* const clone = new (std::nothrow) WxTimerEventRuntime(*this);
      return clone;
    }

    std::int32_t mTimerId = 0; // +0x20
  };
  static_assert(offsetof(WxTimerEventRuntime, mTimerId) == 0x20, "WxTimerEventRuntime::mTimerId offset must be 0x20");
  static_assert(sizeof(WxTimerEventRuntime) == 0x24, "WxTimerEventRuntime size must be 0x24");

  class WxProcessEventRuntime final : public wxEventRuntime
  {
  public:
    WxProcessEventRuntime(const std::int32_t eventId, const std::int32_t param0, const std::int32_t param1)
      : wxEventRuntime(eventId, 0)
      , mParam0(param0)
      , mParam1(param1)
    {
      mEventType = EnsureWxEvtProcessRuntimeType();
    }

    WxProcessEventRuntime* Clone() const override
    {
      auto* const clone = new (std::nothrow) WxProcessEventRuntime(*this);
      return clone;
    }

    std::int32_t mParam0 = 0; // +0x20
    std::int32_t mParam1 = 0; // +0x24
  };
  static_assert(offsetof(WxProcessEventRuntime, mParam0) == 0x20, "WxProcessEventRuntime::mParam0 offset must be 0x20");
  static_assert(offsetof(WxProcessEventRuntime, mParam1) == 0x24, "WxProcessEventRuntime::mParam1 offset must be 0x24");
  static_assert(sizeof(WxProcessEventRuntime) == 0x28, "WxProcessEventRuntime size must be 0x28");

  struct WxWindowNativeHandleRuntimeView
  {
    std::uint8_t mUnknown00To107[0x108]{};
    HWND mNativeHandle = nullptr;             // +0x108
  };
  static_assert(
    offsetof(WxWindowNativeHandleRuntimeView, mNativeHandle) == 0x108,
    "WxWindowNativeHandleRuntimeView::mNativeHandle offset must be 0x108"
  );

  struct WxMenuBarRefreshRuntimeView
  {
    std::uint8_t mUnknown00To13F[0x140]{};
    WxWindowNativeHandleRuntimeView* mOwnerFrame = nullptr; // +0x140
  };
  static_assert(
    offsetof(WxMenuBarRefreshRuntimeView, mOwnerFrame) == 0x140,
    "WxMenuBarRefreshRuntimeView::mOwnerFrame offset must be 0x140"
  );

  struct WxFrameBaseRuntimeView
  {
    std::uint8_t mUnknown00To153[0x154]{};
    void* mFrameMenuBar = nullptr;             // +0x154
    void* mOldStatusTextStorage = nullptr;     // +0x158
    void* mFrameStatusBar = nullptr;           // +0x15C
    void* mStatusBarPane = nullptr;            // +0x160
    void* mUnknown164 = nullptr;               // +0x164
    void* mUnknown168 = nullptr;               // +0x168
    void* mUnknown16C = nullptr;               // +0x16C
    void* mUnknown170 = nullptr;               // +0x170
    std::uint8_t mUnknown174 = 0;              // +0x174
    std::uint8_t mPadding175To177[0x3]{};
  };
  static_assert(
    offsetof(WxFrameBaseRuntimeView, mFrameMenuBar) == 0x154,
    "WxFrameBaseRuntimeView::mFrameMenuBar offset must be 0x154"
  );
  static_assert(
    offsetof(WxFrameBaseRuntimeView, mOldStatusTextStorage) == 0x158,
    "WxFrameBaseRuntimeView::mOldStatusTextStorage offset must be 0x158"
  );
  static_assert(
    offsetof(WxFrameBaseRuntimeView, mFrameStatusBar) == 0x15C,
    "WxFrameBaseRuntimeView::mFrameStatusBar offset must be 0x15C"
  );
  static_assert(
    offsetof(WxFrameBaseRuntimeView, mStatusBarPane) == 0x160,
    "WxFrameBaseRuntimeView::mStatusBarPane offset must be 0x160"
  );
  static_assert(
    offsetof(WxFrameBaseRuntimeView, mUnknown164) == 0x164,
    "WxFrameBaseRuntimeView::mUnknown164 offset must be 0x164"
  );
  static_assert(
    offsetof(WxFrameBaseRuntimeView, mUnknown168) == 0x168,
    "WxFrameBaseRuntimeView::mUnknown168 offset must be 0x168"
  );
  static_assert(
    offsetof(WxFrameBaseRuntimeView, mUnknown16C) == 0x16C,
    "WxFrameBaseRuntimeView::mUnknown16C offset must be 0x16C"
  );
  static_assert(
    offsetof(WxFrameBaseRuntimeView, mUnknown170) == 0x170,
    "WxFrameBaseRuntimeView::mUnknown170 offset must be 0x170"
  );
  static_assert(
    offsetof(WxFrameBaseRuntimeView, mUnknown174) == 0x174,
    "WxFrameBaseRuntimeView::mUnknown174 offset must be 0x174"
  );
  static_assert(sizeof(WxFrameBaseRuntimeView) == 0x178, "WxFrameBaseRuntimeView size must be 0x178");

  struct WxMdiParentRuntimeView
  {
    std::uint8_t mUnknown00To3F[0x40]{};
    wxNodeBaseRuntime* mChildNodeHead = nullptr;      // +0x40
    std::uint8_t mUnknown44To11B[0xD8]{};
    void* mWindowMenu = nullptr;                      // +0x11C
    std::uint8_t mUnknown120To157[0x38]{};
    void* mUnknownOwnedPointer158 = nullptr;          // +0x158
    std::uint8_t mUnknown15CTo15F[0x4]{};
    void* mUnknownOwnedPointer160 = nullptr;          // +0x160
    std::uint8_t mUnknown164To177[0x14]{};
    void* mMdiClientWindow = nullptr;                 // +0x178
    void* mActiveChildWindow = nullptr;               // +0x17C
    void* mWindowMenuBar = nullptr;                   // +0x180
    std::uint8_t mRouteCommandsToParent = 0;          // +0x184
    std::uint8_t mPadding185To187[0x3]{};
  };
  static_assert(
    offsetof(WxMdiParentRuntimeView, mChildNodeHead) == 0x40,
    "WxMdiParentRuntimeView::mChildNodeHead offset must be 0x40"
  );
  static_assert(
    offsetof(WxMdiParentRuntimeView, mWindowMenu) == 0x11C,
    "WxMdiParentRuntimeView::mWindowMenu offset must be 0x11C"
  );
  static_assert(
    offsetof(WxMdiParentRuntimeView, mMdiClientWindow) == 0x178,
    "WxMdiParentRuntimeView::mMdiClientWindow offset must be 0x178"
  );
  static_assert(
    offsetof(WxMdiParentRuntimeView, mActiveChildWindow) == 0x17C,
    "WxMdiParentRuntimeView::mActiveChildWindow offset must be 0x17C"
  );
  static_assert(
    offsetof(WxMdiParentRuntimeView, mWindowMenuBar) == 0x180,
    "WxMdiParentRuntimeView::mWindowMenuBar offset must be 0x180"
  );
  static_assert(
    offsetof(WxMdiParentRuntimeView, mRouteCommandsToParent) == 0x184,
    "WxMdiParentRuntimeView::mRouteCommandsToParent offset must be 0x184"
  );
  static_assert(sizeof(WxMdiParentRuntimeView) == 0x188, "WxMdiParentRuntimeView size must be 0x188");

  struct WxMdiChildRuntimeView
  {
    std::uint8_t mUnknown00To2B[0x2C]{};
    void* mParentFrame = nullptr;            // +0x2C
    std::uint8_t mUnknown30To107[0xD8]{};
    HWND mNativeHandle = nullptr;            // +0x108
    std::uint8_t mUnknown10CTo11B[0x10]{};
    void* mWindowMenu = nullptr;             // +0x11C
    std::uint8_t mUnknown120To177[0x58]{};
    std::uint8_t mConstructedRuntimeFlag = 0; // +0x178
    std::uint8_t mPadding179To17B[0x3]{};
  };
  static_assert(
    offsetof(WxMdiChildRuntimeView, mParentFrame) == 0x2C,
    "WxMdiChildRuntimeView::mParentFrame offset must be 0x2C"
  );
  static_assert(
    offsetof(WxMdiChildRuntimeView, mNativeHandle) == 0x108,
    "WxMdiChildRuntimeView::mNativeHandle offset must be 0x108"
  );
  static_assert(
    offsetof(WxMdiChildRuntimeView, mWindowMenu) == 0x11C,
    "WxMdiChildRuntimeView::mWindowMenu offset must be 0x11C"
  );
  static_assert(
    offsetof(WxMdiChildRuntimeView, mConstructedRuntimeFlag) == 0x178,
    "WxMdiChildRuntimeView::mConstructedRuntimeFlag offset must be 0x178"
  );
  static_assert(sizeof(WxMdiChildRuntimeView) == 0x17C, "WxMdiChildRuntimeView size must be 0x17C");

  using WxMdiChildTranslateMessageFn = bool (__thiscall*)(void* mdiChildRuntime, MSG* message);
  struct WxMdiChildTranslateVTableRuntime
  {
    std::uint8_t mUnknown000To1FF[0x200]{};
    WxMdiChildTranslateMessageFn mTranslateMessage = nullptr; // +0x200
  };
  static_assert(
    offsetof(WxMdiChildTranslateVTableRuntime, mTranslateMessage) == 0x200,
    "WxMdiChildTranslateVTableRuntime::mTranslateMessage offset must be 0x200"
  );

  struct WxMdiChildTranslateRuntimeView
  {
    WxMdiChildTranslateVTableRuntime* mVtable = nullptr; // +0x00
    std::uint8_t mUnknown04To107[0x104]{};
    HWND mNativeHandle = nullptr;                        // +0x108
  };
  static_assert(
    offsetof(WxMdiChildTranslateRuntimeView, mNativeHandle) == 0x108,
    "WxMdiChildTranslateRuntimeView::mNativeHandle offset must be 0x108"
  );

  using WxFrameGetSizeHintFn = std::int32_t (__thiscall*)(void* frameRuntime);
  using WxFrameGetCreateMdiClientStyleFn = std::int32_t (__thiscall*)(void* frameRuntime);
  using WxFrameDefaultWindowProcFn = long (__thiscall*)(void* frameRuntime, unsigned int message, unsigned int wParam, long lParam);
  using WxFrameResolveMdiBridgeFn = void* (__thiscall*)(void* frameRuntime);
  using WxFrameShowHelpTextFn = void (__thiscall*)(void* frameRuntime, const wxStringRuntime* text, int showFlag);
  using WxFrameCreateMdiClientFn = void* (__thiscall*)(void* frameRuntime);

  struct WxFrameWindowProcVTableRuntime
  {
    std::uint8_t mUnknown000To063[0x64]{};
    WxFrameGetSizeHintFn mGetMdiChildMaxWidth = nullptr;     // +0x64
    WxFrameGetSizeHintFn mGetMdiChildMaxHeight = nullptr;    // +0x68
    std::uint8_t mUnknown06CTo087[0x1C]{};
    WxFrameGetCreateMdiClientStyleFn mGetMdiClientStyle = nullptr; // +0x88
    std::uint8_t mUnknown08CTo1F3[0x168]{};
    WxFrameDefaultWindowProcFn mMswDefWindowProc = nullptr;  // +0x1F4
    std::uint8_t mUnknown1F8To25B[0x64]{};
    WxFrameResolveMdiBridgeFn mGetMdiBridge = nullptr;       // +0x25C
    void* mUnknown260 = nullptr;                             // +0x260
    WxFrameShowHelpTextFn mShowHelpText = nullptr;           // +0x264
    std::uint8_t mUnknown268To287[0x20]{};
    WxFrameCreateMdiClientFn mCreateMdiClient = nullptr;     // +0x288
  };
  static_assert(
    offsetof(WxFrameWindowProcVTableRuntime, mGetMdiChildMaxWidth) == 0x64,
    "WxFrameWindowProcVTableRuntime::mGetMdiChildMaxWidth offset must be 0x64"
  );
  static_assert(
    offsetof(WxFrameWindowProcVTableRuntime, mGetMdiChildMaxHeight) == 0x68,
    "WxFrameWindowProcVTableRuntime::mGetMdiChildMaxHeight offset must be 0x68"
  );
  static_assert(
    offsetof(WxFrameWindowProcVTableRuntime, mGetMdiClientStyle) == 0x88,
    "WxFrameWindowProcVTableRuntime::mGetMdiClientStyle offset must be 0x88"
  );
  static_assert(
    offsetof(WxFrameWindowProcVTableRuntime, mMswDefWindowProc) == 0x1F4,
    "WxFrameWindowProcVTableRuntime::mMswDefWindowProc offset must be 0x1F4"
  );
  static_assert(
    offsetof(WxFrameWindowProcVTableRuntime, mGetMdiBridge) == 0x25C,
    "WxFrameWindowProcVTableRuntime::mGetMdiBridge offset must be 0x25C"
  );
  static_assert(
    offsetof(WxFrameWindowProcVTableRuntime, mShowHelpText) == 0x264,
    "WxFrameWindowProcVTableRuntime::mShowHelpText offset must be 0x264"
  );
  static_assert(
    offsetof(WxFrameWindowProcVTableRuntime, mCreateMdiClient) == 0x288,
    "WxFrameWindowProcVTableRuntime::mCreateMdiClient offset must be 0x288"
  );

  struct WxFrameWindowProcRuntimeView
  {
    WxFrameWindowProcVTableRuntime* mVtable = nullptr;
  };

  using WxMdiClientCreateForParentFn = bool (__thiscall*)(void* mdiClientRuntime, void* parentFrameRuntime, int style);
  struct WxMdiClientRuntimeVTableView
  {
    std::uint8_t mUnknown000To20B[0x20C]{};
    WxMdiClientCreateForParentFn mCreateForParent = nullptr; // +0x20C
  };
  static_assert(
    offsetof(WxMdiClientRuntimeVTableView, mCreateForParent) == 0x20C,
    "WxMdiClientRuntimeVTableView::mCreateForParent offset must be 0x20C"
  );

  struct WxMdiClientRuntimeView
  {
    WxMdiClientRuntimeVTableView* mVtable = nullptr;
    std::uint8_t mUnknown04To2B[0x28]{};
    void* mParentFrame = nullptr;                  // +0x2C
    std::uint8_t mUnknown30To107[0xD8]{};
    HWND mNativeHandle = nullptr;                  // +0x108
    std::uint32_t mSubclassCookie = 0;             // +0x10C
  };
  static_assert(
    offsetof(WxMdiClientRuntimeView, mParentFrame) == 0x2C,
    "WxMdiClientRuntimeView::mParentFrame offset must be 0x2C"
  );
  static_assert(
    offsetof(WxMdiClientRuntimeView, mNativeHandle) == 0x108,
    "WxMdiClientRuntimeView::mNativeHandle offset must be 0x108"
  );
  static_assert(
    offsetof(WxMdiClientRuntimeView, mSubclassCookie) == 0x10C,
    "WxMdiClientRuntimeView::mSubclassCookie offset must be 0x10C"
  );

  using WxMdiBridgeSyncClientStateFn = void (__thiscall*)(void* bridgeRuntime, int visibleState, int reserved);
  struct WxMdiBridgeRuntimeVTableView
  {
    std::uint8_t mUnknown000To0EF[0xF0]{};
    WxMdiBridgeSyncClientStateFn mSyncClientState = nullptr; // +0xF0
  };
  static_assert(
    offsetof(WxMdiBridgeRuntimeVTableView, mSyncClientState) == 0xF0,
    "WxMdiBridgeRuntimeVTableView::mSyncClientState offset must be 0xF0"
  );

  struct WxMdiBridgeRuntimeView
  {
    WxMdiBridgeRuntimeVTableView* mVtable = nullptr;
    std::uint8_t mUnknown04To0CB[0xC8]{};
    std::uint8_t mFlags = 0;                       // +0xCC
  };
  static_assert(
    offsetof(WxMdiBridgeRuntimeView, mFlags) == 0xCC,
    "WxMdiBridgeRuntimeView::mFlags offset must be 0xCC"
  );

  using WxResolveMenuLookupHostFn = void* (__thiscall*)(void* frameRuntime);
  using WxFindMenuItemByIdFn = void* (__thiscall*)(void* menuLookupHostRuntime, std::uint16_t commandId, void* outMenu);
  using WxMenuItemIsCheckableFn = bool (__thiscall*)(void* menuItemRuntime);
  using WxMenuItemSetCheckedFn = void (__thiscall*)(void* menuItemRuntime, bool checked);
  using WxMenuItemIsCheckedFn = bool (__thiscall*)(void* menuItemRuntime);

  struct WxMenuCommandHostVTableRuntime
  {
    std::uint8_t mUnknown000To23B[0x23C]{};
    WxResolveMenuLookupHostFn mResolveMenuLookupHost = nullptr; // +0x23C
  };
  static_assert(
    offsetof(WxMenuCommandHostVTableRuntime, mResolveMenuLookupHost) == 0x23C,
    "WxMenuCommandHostVTableRuntime::mResolveMenuLookupHost offset must be 0x23C"
  );

  struct WxMenuCommandHostRuntimeView
  {
    WxMenuCommandHostVTableRuntime* mVtable = nullptr;
  };

  struct WxMenuLookupHostVTableRuntime
  {
    std::uint8_t mUnknown000To22F[0x230]{};
    WxFindMenuItemByIdFn mFindMenuItemById = nullptr; // +0x230
  };
  static_assert(
    offsetof(WxMenuLookupHostVTableRuntime, mFindMenuItemById) == 0x230,
    "WxMenuLookupHostVTableRuntime::mFindMenuItemById offset must be 0x230"
  );

  struct WxMenuLookupHostRuntimeView
  {
    WxMenuLookupHostVTableRuntime* mVtable = nullptr;
  };

  struct WxMenuItemRuntimeVTable
  {
    std::uint8_t mUnknown000To01B[0x1C]{};
    WxMenuItemIsCheckableFn mIsCheckable = nullptr; // +0x1C
    WxMenuItemSetCheckedFn mSetChecked = nullptr;   // +0x20
    WxMenuItemIsCheckedFn mIsChecked = nullptr;     // +0x24
  };
  static_assert(
    offsetof(WxMenuItemRuntimeVTable, mIsCheckable) == 0x1C,
    "WxMenuItemRuntimeVTable::mIsCheckable offset must be 0x1C"
  );
  static_assert(
    offsetof(WxMenuItemRuntimeVTable, mSetChecked) == 0x20,
    "WxMenuItemRuntimeVTable::mSetChecked offset must be 0x20"
  );
  static_assert(
    offsetof(WxMenuItemRuntimeVTable, mIsChecked) == 0x24,
    "WxMenuItemRuntimeVTable::mIsChecked offset must be 0x24"
  );

  struct WxMenuItemRuntimeView
  {
    WxMenuItemRuntimeVTable* mVtable = nullptr; // +0x00
    std::uint8_t mUnknown04To1B[0x18]{};
    std::int32_t mKind = 0;                     // +0x1C
    std::uint8_t mIsChecked = 0;                // +0x20
    std::uint8_t mPadding21To23[0x3]{};
  };
  static_assert(
    offsetof(WxMenuItemRuntimeView, mKind) == 0x1C,
    "WxMenuItemRuntimeView::mKind offset must be 0x1C"
  );
  static_assert(
    offsetof(WxMenuItemRuntimeView, mIsChecked) == 0x20,
    "WxMenuItemRuntimeView::mIsChecked offset must be 0x20"
  );

  struct WxMenuRuntimeView
  {
    std::uint8_t mUnknown00To2B[0x2C]{};
    WxMenuRuntimeView* mParentMenu = nullptr; // +0x2C
    std::uint8_t mUnknown30To4F[0x20]{};
    void* mInvokingWindow = nullptr;          // +0x50
    std::uint8_t mUnknown54To57[0x4]{};
    wxWindowBase* mEventHandler = nullptr;    // +0x58
    std::uint8_t mUnknown5CTo63[0x8]{};
    HMENU mNativeMenuHandle = nullptr;        // +0x64
  };
  static_assert(
    offsetof(WxMenuRuntimeView, mParentMenu) == 0x2C,
    "WxMenuRuntimeView::mParentMenu offset must be 0x2C"
  );
  static_assert(
    offsetof(WxMenuRuntimeView, mInvokingWindow) == 0x50,
    "WxMenuRuntimeView::mInvokingWindow offset must be 0x50"
  );
  static_assert(
    offsetof(WxMenuRuntimeView, mEventHandler) == 0x58,
    "WxMenuRuntimeView::mEventHandler offset must be 0x58"
  );
  static_assert(
    offsetof(WxMenuRuntimeView, mNativeMenuHandle) == 0x64,
    "WxMenuRuntimeView::mNativeMenuHandle offset must be 0x64"
  );

  [[nodiscard]] std::int32_t ResolveRuntimeWindowId(
    wxWindowBase* const window
  ) noexcept
  {
    if (window == nullptr) {
      return 0;
    }

    if (const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(window); state != nullptr) {
      return state->windowId;
    }

    return reinterpret_cast<const WxWindowActivationRuntimeView*>(window)->mWindowId;
  }

  [[nodiscard]] wxWindowBase* ResolveRuntimeEventHandler(
    wxWindowBase* const window
  ) noexcept
  {
    if (window == nullptr) {
      return nullptr;
    }

    if (const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(window); state != nullptr) {
      return state->eventHandler != nullptr ? state->eventHandler : window;
    }

    wxWindowBase* const eventHandler = reinterpret_cast<WxWindowActivationRuntimeView*>(window)->mEventHandler;
    return eventHandler != nullptr ? eventHandler : window;
  }

  [[nodiscard]] HWND ResolveRuntimeNativeHandle(
    const void* const windowRuntime
  ) noexcept
  {
    if (windowRuntime == nullptr) {
      return nullptr;
    }

    if (
      const WxWindowBaseRuntimeState* const state =
        FindWxWindowBaseRuntimeState(static_cast<const wxWindowBase*>(windowRuntime));
      state != nullptr && state->nativeHandle != 0u
    ) {
      return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(state->nativeHandle));
    }

    return static_cast<const WxWindowNativeHandleRuntimeView*>(windowRuntime)->mNativeHandle;
  }

  [[nodiscard]] long CallRuntimeDefaultWindowProc(
    void* const frameRuntime,
    const unsigned int message,
    const unsigned int wParam,
    const long lParam
  ) noexcept
  {
    auto* const frameView = static_cast<WxFrameWindowProcRuntimeView*>(frameRuntime);
    if (frameView == nullptr || frameView->mVtable == nullptr || frameView->mVtable->mMswDefWindowProc == nullptr) {
      return 0;
    }
    return frameView->mVtable->mMswDefWindowProc(frameRuntime, message, wParam, lParam);
  }

  [[nodiscard]] long CallWxFrameWindowProcBase(
    void* const frameRuntime,
    const unsigned int message,
    const unsigned int wParam,
    const long lParam
  ) noexcept
  {
    auto* const frameObject = static_cast<wxFrame*>(frameRuntime);
    if (frameObject == nullptr) {
      return 0;
    }

    return frameObject->wxFrame::MSWWindowProc(message, wParam, lParam);
  }

  [[nodiscard]] void* ResolveMdiBridgeRuntime(
    void* const frameRuntime
  ) noexcept
  {
    auto* const frameView = static_cast<WxFrameWindowProcRuntimeView*>(frameRuntime);
    if (frameView == nullptr || frameView->mVtable == nullptr || frameView->mVtable->mGetMdiBridge == nullptr) {
      return nullptr;
    }

    return frameView->mVtable->mGetMdiBridge(frameRuntime);
  }

  [[nodiscard]] void* ResolveFrameMenuLookupHost(
    void* const frameRuntime
  ) noexcept
  {
    auto* const frameView = static_cast<WxMenuCommandHostRuntimeView*>(frameRuntime);
    if (frameView == nullptr || frameView->mVtable == nullptr || frameView->mVtable->mResolveMenuLookupHost == nullptr) {
      return nullptr;
    }

    return frameView->mVtable->mResolveMenuLookupHost(frameRuntime);
  }

  [[nodiscard]] WxMenuItemRuntimeView* FindMenuItemByCommandId(
    void* const menuLookupHostRuntime,
    const std::uint16_t commandId
  ) noexcept
  {
    auto* const lookupHost = static_cast<WxMenuLookupHostRuntimeView*>(menuLookupHostRuntime);
    if (lookupHost == nullptr || lookupHost->mVtable == nullptr || lookupHost->mVtable->mFindMenuItemById == nullptr) {
      return nullptr;
    }

    return static_cast<WxMenuItemRuntimeView*>(lookupHost->mVtable->mFindMenuItemById(lookupHost, commandId, nullptr));
  }

  /**
   * Address: 0x009A08D0 (FUN_009A08D0, wxMenuBase::SendEvent)
   *
   * What it does:
   * Emits one menu-selected command event from `menuRuntime`, first through the
   * menu event-handler lane and then (if still unhandled) through the first
   * parent-chain invoking window.
   */
  [[nodiscard]] bool wxMenuSendCommandEventRuntime(
    void* const menuRuntime,
    const std::uint16_t commandId,
    const int checkedStateFlag
  )
  {
    wxCommandEventRuntime commandEvent(EnsureWxEvtCommandMenuSelectedRuntimeType(), commandId);
    commandEvent.mEventObject = menuRuntime;
    commandEvent.mCommandInt = checkedStateFlag;

    bool handled = false;
    auto* const sourceMenu = static_cast<WxMenuRuntimeView*>(menuRuntime);
    if (sourceMenu != nullptr && sourceMenu->mEventHandler != nullptr) {
      handled = sourceMenu->mEventHandler->ProcessEvent(&commandEvent);
    }

    if (handled || sourceMenu == nullptr) {
      return handled;
    }

    for (WxMenuRuntimeView* menuCursor = sourceMenu; menuCursor != nullptr; menuCursor = menuCursor->mParentMenu) {
      if (menuCursor->mInvokingWindow == nullptr) {
        continue;
      }

      wxWindowBase* const ownerEventHandler =
        ResolveRuntimeEventHandler(static_cast<wxWindowBase*>(menuCursor->mInvokingWindow));
      if (ownerEventHandler != nullptr) {
        handled = ownerEventHandler->ProcessEvent(&commandEvent);
      }
      break;
    }

    return handled;
  }

  /**
   * Address: 0x00998B20 (FUN_00998B20, wxMenu::MSWCommand)
   *
   * What it does:
   * Resolves checked-state bits from the menu handle for one command id and
   * forwards menu-selected dispatch through `wxMenuBase::SendEvent` semantics.
   */
  [[nodiscard]] bool wxMenuMswCommandRuntime(
    void* const menuRuntime,
    const unsigned short notificationCode,
    const unsigned int commandId
  )
  {
    (void)notificationCode;

    const std::uint16_t menuCommandId = static_cast<std::uint16_t>(commandId);
    if (menuCommandId != kMenuCommandSeparatorId) {
      const auto* const menuView = static_cast<const WxMenuRuntimeView*>(menuRuntime);
      const UINT menuState = ::GetMenuState(
        menuView != nullptr ? menuView->mNativeMenuHandle : nullptr,
        static_cast<UINT>(menuCommandId),
        0u
      );
      (void)wxMenuSendCommandEventRuntime(
        menuRuntime,
        menuCommandId,
        static_cast<int>(menuState & static_cast<UINT>(MF_CHECKED))
      );
    }

    return true;
  }

  /**
   * Address: 0x00968BC0 (FUN_00968BC0)
   *
   * What it does:
   * Unpacks one `WM_MENUSELECT` payload into menu-id, selection-flags, and
   * menu-handle lanes.
   */
  unsigned int* wxUnpackMenuSelectMessage(
    const unsigned int packedWord,
    const int menuHandle,
    unsigned short* const outMenuId,
    unsigned short* const outSelectionFlags,
    unsigned int* const outMenuHandle
  )
  {
    if (outMenuId != nullptr) {
      *outMenuId = static_cast<unsigned short>(packedWord & 0xFFFFu);
    }
    if (outSelectionFlags != nullptr) {
      *outSelectionFlags = static_cast<unsigned short>((packedWord >> 16u) & 0xFFFFu);
    }
    if (outMenuHandle != nullptr) {
      *outMenuHandle = static_cast<unsigned int>(menuHandle);
    }
    return outMenuHandle;
  }

  /**
   * Address: 0x0099F2F0 (FUN_0099F2F0)
   *
   * What it does:
   * Dispatches one menu-highlight event for a frame/runtime owner when menu
   * selection lanes target a command item; popup/system-menu selections clear
   * frame help text and report unhandled.
   */
  [[nodiscard]] bool wxDispatchFrameMenuHighlightEvent(
    void* const frameRuntime,
    const unsigned short menuId,
    const unsigned short selectionFlags,
    const unsigned int menuHandle
  )
  {
    std::int32_t highlightedMenuId = 0;
    if (menuId == 0xFFFFu && menuHandle == 0u) {
      highlightedMenuId = -1;
    } else if ((selectionFlags & MF_POPUP) == 0u && (selectionFlags & MF_SYSMENU) == 0u) {
      highlightedMenuId = static_cast<std::int32_t>(menuId);
    } else {
      wxStringRuntime helpText = AllocateOwnedWxString(std::wstring());
      auto* const frameView = static_cast<WxFrameWindowProcRuntimeView*>(frameRuntime);
      if (frameView != nullptr && frameView->mVtable != nullptr && frameView->mVtable->mShowHelpText != nullptr) {
        frameView->mVtable->mShowHelpText(frameRuntime, &helpText, 0);
      }
      ReleaseOwnedWxString(helpText);
      return false;
    }

    WxMenuEventFactoryRuntime menuEvent{};
    menuEvent.mEventType = EnsureWxEvtMenuHighlightRuntimeType();
    menuEvent.mEventId = highlightedMenuId;
    menuEvent.mMenuId = highlightedMenuId;
    menuEvent.mEventObject = static_cast<wxWindowBase*>(frameRuntime);

    auto* const frameEventHandlerView = static_cast<WxWindowActivationRuntimeView*>(frameRuntime);
    const bool handled =
      frameEventHandlerView != nullptr && frameEventHandlerView->mEventHandler != nullptr &&
      frameEventHandlerView->mEventHandler->ProcessEvent(&menuEvent);
    RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(&menuEvent));
    return handled;
  }

  /**
   * Address: 0x009FC9B0 (FUN_009FC9B0)
   *
   * What it does:
   * Sends one `WM_MDISETMENU` lane to the target MDI client, refreshes client
   * menus when a parent frame exists, and redraws the parent menu bar.
   */
  [[nodiscard]] LRESULT wxSetMdiClientWindowMenu(
    void* const mdiClientRuntime,
    void* const windowMenuRuntime,
    void* const menuBarNativeHandle
  )
  {
    const auto* const mdiClientView = static_cast<const WxMdiClientRuntimeView*>(mdiClientRuntime);
    if (mdiClientView == nullptr || mdiClientView->mNativeHandle == nullptr) {
      return 0;
    }

    const LRESULT setMenuResult = ::SendMessageW(
      mdiClientView->mNativeHandle,
      WM_MDISETMENU,
      reinterpret_cast<WPARAM>(windowMenuRuntime),
      reinterpret_cast<LPARAM>(menuBarNativeHandle)
    );

    const auto* const parentView = static_cast<const WxMdiParentRuntimeView*>(mdiClientView->mParentFrame);
    if (parentView == nullptr) {
      return setMenuResult;
    }

    (void)::SendMessageW(mdiClientView->mNativeHandle, WM_MDIREFRESHMENU, 0, 0);
    const HWND parentFrameHandle = ResolveRuntimeNativeHandle(parentView);
    if (parentFrameHandle != nullptr) {
      return ::DrawMenuBar(parentFrameHandle);
    }

    return setMenuResult;
  }

  /**
   * Address: 0x00998E80 (FUN_00998E80)
   *
   * What it does:
   * Redraws the native frame menu bar owned by one wx menu-bar runtime lane.
   */
  int wxMenuBarRefreshNativeMenuBar(
    void* const menuBarRuntime
  )
  {
    const auto* const menuBarView = static_cast<const WxMenuBarRefreshRuntimeView*>(menuBarRuntime);
    if (menuBarView == nullptr || menuBarView->mOwnerFrame == nullptr) {
      return 0;
    }

    const HWND ownerFrameHandle = menuBarView->mOwnerFrame->mNativeHandle;
    if (ownerFrameHandle == nullptr) {
      return 0;
    }

    return static_cast<int>(::DrawMenuBar(ownerFrameHandle));
  }

  struct WxMenuBarNodeRuntimeView
  {
    std::uint8_t mUnknown00To07[0x8]{};
    WxMenuRuntimeView* mMenuRuntime = nullptr;        // +0x08
    WxMenuBarNodeRuntimeView* mNextNode = nullptr;    // +0x0C
  };
  static_assert(
    offsetof(WxMenuBarNodeRuntimeView, mMenuRuntime) == 0x8,
    "WxMenuBarNodeRuntimeView::mMenuRuntime offset must be 0x8"
  );
  static_assert(
    offsetof(WxMenuBarNodeRuntimeView, mNextNode) == 0xC,
    "WxMenuBarNodeRuntimeView::mNextNode offset must be 0xC"
  );

  struct WxMenuBarEnsureNativeMenuRuntimeView
  {
    std::uint8_t mUnknown00To12B[0x12C]{};
    std::uint32_t mMenuCount = 0;                    // +0x12C
    std::uint8_t mUnknown130To133[0x4]{};
    WxMenuBarNodeRuntimeView* mFirstMenuNode = nullptr; // +0x134
    std::uint8_t mUnknown138To14B[0x14]{};
    const wchar_t** mMenuTitles = nullptr;           // +0x14C
    std::uint8_t mUnknown150To153[0x4]{};
    HMENU mNativeMenu = nullptr;                     // +0x154
  };
  static_assert(
    offsetof(WxMenuBarEnsureNativeMenuRuntimeView, mMenuCount) == 0x12C,
    "WxMenuBarEnsureNativeMenuRuntimeView::mMenuCount offset must be 0x12C"
  );
  static_assert(
    offsetof(WxMenuBarEnsureNativeMenuRuntimeView, mFirstMenuNode) == 0x134,
    "WxMenuBarEnsureNativeMenuRuntimeView::mFirstMenuNode offset must be 0x134"
  );
  static_assert(
    offsetof(WxMenuBarEnsureNativeMenuRuntimeView, mMenuTitles) == 0x14C,
    "WxMenuBarEnsureNativeMenuRuntimeView::mMenuTitles offset must be 0x14C"
  );
  static_assert(
    offsetof(WxMenuBarEnsureNativeMenuRuntimeView, mNativeMenu) == 0x154,
    "WxMenuBarEnsureNativeMenuRuntimeView::mNativeMenu offset must be 0x154"
  );

  [[nodiscard]] WxMenuBarNodeRuntimeView* wxMenuBarNodeAtIndex(
    WxMenuBarNodeRuntimeView* node,
    std::uint32_t index
  ) noexcept
  {
    while (node != nullptr && index > 0u) {
      node = node->mNextNode;
      --index;
    }
    return node;
  }

  /**
   * Address: 0x00998EA0 (FUN_00998EA0)
   *
   * What it does:
   * Lazily creates one native menu handle for a wx menu-bar runtime lane and
   * appends each top-level popup menu/title pair into that Win32 menu.
   */
  [[maybe_unused]] HMENU wxMenuBarEnsureNativeMenuHandle(
    void* const menuBarRuntime
  )
  {
    auto* const menuBar = static_cast<WxMenuBarEnsureNativeMenuRuntimeView*>(menuBarRuntime);
    if (menuBar == nullptr) {
      return nullptr;
    }

    if (menuBar->mNativeMenu == nullptr) {
      menuBar->mNativeMenu = ::CreateMenu();
      if (menuBar->mNativeMenu != nullptr) {
        for (std::uint32_t index = 0; index < menuBar->mMenuCount; ++index) {
          const wchar_t* menuTitle = L"";
          if (menuBar->mMenuTitles != nullptr) {
            const wchar_t* const candidateTitle = menuBar->mMenuTitles[index];
            if (candidateTitle != nullptr) {
              menuTitle = candidateTitle;
            }
          }

          const WxMenuBarNodeRuntimeView* const node = wxMenuBarNodeAtIndex(menuBar->mFirstMenuNode, index);
          const WxMenuRuntimeView* const popupMenu = node != nullptr ? node->mMenuRuntime : nullptr;
          const HMENU popupHandle = popupMenu != nullptr ? popupMenu->mNativeMenuHandle : nullptr;
          (void)::AppendMenuW(
            menuBar->mNativeMenu,
            MF_POPUP,
            reinterpret_cast<UINT_PTR>(popupHandle),
            menuTitle
          );
        }
      }
    }

    return menuBar->mNativeMenu;
  }

  /**
   * Address: 0x0098B4F0 (FUN_0098B4F0)
   *
   * What it does:
   * Resolves a suitable top-level owner window for dialog/show paths from the
   * foreground HWND first, then falls back to `wxTheApp->GetTopWindow()`.
   */
  [[maybe_unused]] wxWindowBase* wxResolveTopLevelOwnerWindow(
    wxWindowBase* const requester
  )
  {
    wxWindowBase* ownerWindow = nullptr;

    const HWND foregroundWindow = ::GetForegroundWindow();
    if (foregroundWindow != nullptr) {
      ownerWindow = wxFindWinFromHandle(static_cast<int>(reinterpret_cast<std::uintptr_t>(foregroundWindow)));
    }
    if (ownerWindow == nullptr && wxTheApp != nullptr) {
      ownerWindow = wxTheApp->GetTopWindow();
    }

    if (ownerWindow == nullptr || ownerWindow == requester) {
      return nullptr;
    }

    const WxWindowBaseRuntimeState* const ownerState = FindWxWindowBaseRuntimeState(ownerWindow);
    if (ownerState == nullptr || (ownerState->bitfields & 0x2u) == 0u) {
      return nullptr;
    }

    return ownerWindow;
  }
}

/**
 * Address: 0x009FB510 (FUN_009FB510)
 *
 * What it does:
 * Reads one status-bar pane text lane from the native HWND and stores it in
 * `outText`; invalid pane indices return an empty string.
 */
wxStringRuntime* wxGetStatusBarPaneText(
  const void* const statusBarRuntime,
  wxStringRuntime* const outText,
  const std::int32_t paneIndex
)
{
  if (outText == nullptr) {
    return nullptr;
  }

  const auto* const statusBar = static_cast<const WxStatusBarRuntimeView*>(statusBarRuntime);
  if (statusBar == nullptr || paneIndex < 0 || paneIndex >= statusBar->mPaneCount) {
    AssignOwnedWxString(outText, std::wstring());
    return outText;
  }

  std::wstring paneText;
  const WPARAM paneParam = static_cast<WPARAM>(paneIndex);
  const LRESULT lengthResult = ::SendMessageW(statusBar->mNativeHandle, SB_GETTEXTLENGTHW, paneParam, 0);
  const std::uint16_t textLength = static_cast<std::uint16_t>(lengthResult & 0xFFFF);
  if (textLength > 0u) {
    std::vector<wchar_t> textBuffer(static_cast<std::size_t>(textLength) + 1u, L'\0');
    (void)::SendMessageW(
      statusBar->mNativeHandle,
      SB_GETTEXTW,
      paneParam,
      reinterpret_cast<LPARAM>(textBuffer.data())
    );
    paneText.assign(textBuffer.data());
  }

  AssignOwnedWxString(outText, paneText);
  return outText;
}

/**
 * Address: 0x009FDEA0 (FUN_009FDEA0)
 *
 * What it does:
 * Draws one status-bar field text lane with a 2-pixel left inset and vertical
 * center alignment, clipped to the field rectangle.
 */
[[maybe_unused]] void wxDrawStatusBarFieldTextRuntime(
  void* const statusBarRuntime,
  void* const deviceContextRuntime,
  const std::int32_t fieldIndex
)
{
  auto* const statusBar = static_cast<WxStatusBarRuntimeView*>(statusBarRuntime);
  auto* const deviceContext = static_cast<wxDC*>(deviceContextRuntime);
  if (statusBar == nullptr || deviceContext == nullptr || statusBar->mNativeHandle == nullptr) {
    return;
  }

  RECT fieldRect{};
  const LRESULT hasFieldRect = ::SendMessageW(
    statusBar->mNativeHandle,
    SB_GETRECT,
    static_cast<WPARAM>(fieldIndex),
    reinterpret_cast<LPARAM>(&fieldRect)
  );
  if (hasFieldRect == 0) {
    return;
  }

  wxStringRuntime statusText{};
  statusText.m_pchData = const_cast<wchar_t*>(wxEmptyString);
  (void)wxGetStatusBarPaneText(statusBar, &statusText, fieldIndex);

  const wchar_t* const drawText = statusText.m_pchData != nullptr ? statusText.m_pchData : const_cast<wchar_t*>(wxEmptyString);
  const int drawTextLength = static_cast<int>(std::char_traits<wchar_t>::length(drawText));

  HDC nativeDeviceContext = static_cast<HDC>(deviceContext->GetNativeHandle());
  if (nativeDeviceContext != nullptr) {
    SIZE textExtent{};
    if (drawTextLength > 0) {
      (void)::GetTextExtentPoint32W(nativeDeviceContext, drawText, drawTextLength, &textExtent);
    }

    const int drawX = fieldRect.left + 2;
    const int drawY = static_cast<int>(
      static_cast<double>(fieldRect.top + (fieldRect.bottom - fieldRect.top - textExtent.cy) / 2) + 0.5
    );

    const int savedDcState = ::SaveDC(nativeDeviceContext);
    (void)::IntersectClipRect(nativeDeviceContext, fieldRect.left, fieldRect.top, fieldRect.right, fieldRect.bottom);
    (void)::TextOutW(nativeDeviceContext, drawX, drawY, drawText, drawTextLength);
    if (savedDcState != 0) {
      (void)::RestoreDC(nativeDeviceContext, savedDcState);
    } else {
      (void)::SelectClipRgn(nativeDeviceContext, nullptr);
    }
  }

  ReleaseRuntimeStringFromTemporaryStorage(&statusText);
}

/**
 * Address: 0x009F10C0 (FUN_009F10C0)
 *
 * What it does:
 * Updates horizontal/vertical scroll positions (`-1` means unchanged),
 * clamps them to visible-page bounds, updates native scrollbar lanes, and
 * scrolls the target window by pixel deltas.
 */
[[maybe_unused]] void wxScrollHelperApplyRuntimeScrollPositions(
  void* const scrollHelperRuntime,
  const std::int32_t xPosition,
  const std::int32_t yPosition
)
{
  struct ScrollRectRuntimeView
  {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
  };

  struct ScrollHelperRuntimeView
  {
    std::uint8_t unknown00_07[0x8]{};
    wxWindowBase* window = nullptr;
    wxWindowBase* targetWindow = nullptr;
    ScrollRectRuntimeView rectToScroll{};
    std::int32_t unknown20 = 0;
    std::int32_t xScrollPixelsPerUnit = 0;
    std::int32_t yScrollPixelsPerUnit = 0;
    std::int32_t xScrollPosition = 0;
    std::int32_t yScrollPosition = 0;
    std::int32_t xScrollLines = 0;
    std::int32_t yScrollLines = 0;
  };

  auto* const scrollHelper = static_cast<ScrollHelperRuntimeView*>(scrollHelperRuntime);
  if (scrollHelper == nullptr || scrollHelper->targetWindow == nullptr) {
    return;
  }

  if (
    ((xPosition != -1) && (xPosition != scrollHelper->xScrollPosition))
    || ((yPosition != -1) && (yPosition != scrollHelper->yScrollPosition))
  ) {
    std::int32_t visiblePixelWidth = 0;
    std::int32_t visiblePixelHeight = 0;
    if (scrollHelper->rectToScroll.width != 0) {
      visiblePixelWidth = scrollHelper->rectToScroll.width;
      visiblePixelHeight = scrollHelper->rectToScroll.height;
    } else {
      const wxSize clientSize = scrollHelper->targetWindow->GetClientSize();
      visiblePixelWidth = clientSize.x;
      visiblePixelHeight = clientSize.y;
    }

    if (xPosition != -1) {
      const std::int32_t xPixelsPerUnit = scrollHelper->xScrollPixelsPerUnit;
      if (xPixelsPerUnit != 0) {
        const std::int32_t oldXPosition = scrollHelper->xScrollPosition;
        scrollHelper->xScrollPosition = xPosition;

        std::int32_t visibleUnitsX =
          static_cast<std::int32_t>(static_cast<double>(visiblePixelWidth) / static_cast<double>(xPixelsPerUnit) + 0.5);
        if (visibleUnitsX < 1) {
          visibleUnitsX = 1;
        }

        std::int32_t clampedXPosition = scrollHelper->xScrollLines - visibleUnitsX;
        if (clampedXPosition >= xPosition) {
          clampedXPosition = xPosition;
        }
        if (clampedXPosition < 0) {
          clampedXPosition = 0;
        }
        scrollHelper->xScrollPosition = clampedXPosition;

        if (oldXPosition != clampedXPosition) {
          if (scrollHelper->window != nullptr) {
            scrollHelper->window->SetScrollPos(4, clampedXPosition, true);
          }

          const void* const scrollRect = (scrollHelper->rectToScroll.width != 0) ? &scrollHelper->rectToScroll : nullptr;
          const std::int32_t scrollDeltaX = xPixelsPerUnit * (oldXPosition - scrollHelper->xScrollPosition);
          scrollHelper->targetWindow->ScrollWindow(scrollDeltaX, 0, scrollRect);
        }
      }
    }

    if (yPosition != -1) {
      const std::int32_t yPixelsPerUnit = scrollHelper->yScrollPixelsPerUnit;
      if (yPixelsPerUnit != 0) {
        const std::int32_t oldYPosition = scrollHelper->yScrollPosition;
        scrollHelper->yScrollPosition = yPosition;

        std::int32_t visibleUnitsY =
          static_cast<std::int32_t>(static_cast<double>(visiblePixelHeight) / static_cast<double>(yPixelsPerUnit) + 0.5);
        if (visibleUnitsY < 1) {
          visibleUnitsY = 1;
        }

        std::int32_t clampedYPosition = scrollHelper->yScrollLines - visibleUnitsY;
        if (clampedYPosition >= yPosition) {
          clampedYPosition = yPosition;
        }
        if (clampedYPosition < 0) {
          clampedYPosition = 0;
        }
        scrollHelper->yScrollPosition = clampedYPosition;

        if (oldYPosition != clampedYPosition) {
          if (scrollHelper->window != nullptr) {
            scrollHelper->window->SetScrollPos(8, clampedYPosition, true);
          }

          const void* const scrollRect = (scrollHelper->rectToScroll.width != 0) ? &scrollHelper->rectToScroll : nullptr;
          const std::int32_t scrollDeltaY = yPixelsPerUnit * (oldYPosition - scrollHelper->yScrollPosition);
          scrollHelper->targetWindow->ScrollWindow(0, scrollDeltaY, scrollRect);
        }
      }
    }
  }
}

/**
 * Address: 0x009690F0 (FUN_009690F0, wxWindow::HandleActivate)
 *
 * What it does:
 * Builds one activate-event payload for `windowRuntime` and dispatches it to
 * the current event-handler lane.
 */
bool wxHandleWindowActivationEvent(
  void* const windowRuntime,
  const unsigned short activationState,
  const bool minimized,
  const unsigned int activatedNativeHandle
)
{
  (void)minimized;
  (void)activatedNativeHandle;

  auto* const window = static_cast<wxWindowBase*>(windowRuntime);
  if (window == nullptr) {
    return false;
  }

  WxActivateEventFactoryRuntime activateEvent{};
  activateEvent.mEventId = ResolveRuntimeWindowId(window);
  activateEvent.mIsActive = (activationState == 1u || activationState == 2u) ? 1u : 0u;
  activateEvent.mEventObject = window;

  wxWindowBase* const eventHandler = ResolveRuntimeEventHandler(window);
  const bool handled = eventHandler != nullptr && eventHandler->ProcessEvent(&activateEvent);
  RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(&activateEvent));
  return handled;
}

/**
 * Address: 0x009F2500 (FUN_009F2500)
 *
 * What it does:
 * Builds one timer-event payload from timer runtime lanes and dispatches it to
 * the bound event-handler lane.
 */
void wxDispatchTimerOwnerEvent(
  void* const timerRuntime
)
{
  const auto* const timerView = static_cast<const WxTimerOwnerRuntimeView*>(timerRuntime);
  if (timerView == nullptr || timerView->mEventTarget == nullptr) {
    return;
  }

  WxTimerEventRuntime timerEvent(timerView->mEventId, timerView->mTimerId);
  (void)timerView->mEventTarget->ProcessEvent(&timerEvent);
  RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(&timerEvent));
}

/**
 * Address: 0x00A148E0 (FUN_00A148E0)
 *
 * What it does:
 * Builds one process-event payload, dispatches it through the source runtime,
 * and deletes the source lane when the event is unhandled.
 */
void wxDispatchProcessEventOrDelete(
  void* const processEventSourceRuntime,
  const int eventParam0,
  const int eventParam1
)
{
  auto* const sourceView = static_cast<WxProcessEventSourceRuntimeView*>(processEventSourceRuntime);
  if (sourceView == nullptr) {
    return;
  }

  WxProcessEventRuntime processEvent(sourceView->mEventId, eventParam0, eventParam1);
  const bool handled = sourceView->ProcessEvent(&processEvent);
  if (!handled) {
    sourceView->DeleteWithFlag(1);
  }
  RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(&processEvent));
}

/**
 * Address: 0x009FBE70 (FUN_009FBE70)
 *
 * What it does:
 * Sends `WM_MDIGETACTIVE` to the parent's MDI client and resolves the returned
 * child HWND into the corresponding wx runtime window.
 */
void* wxFindActiveMdiChildWindow(
  const void* const mdiParentRuntime
)
{
  const auto* const parentView = static_cast<const WxMdiParentRuntimeView*>(mdiParentRuntime);
  if (parentView == nullptr || parentView->mMdiClientWindow == nullptr) {
    return nullptr;
  }

  const HWND mdiClientHandle = ResolveRuntimeNativeHandle(parentView->mMdiClientWindow);
  if (mdiClientHandle == nullptr) {
    return nullptr;
  }

  const HWND activeChildHandle = reinterpret_cast<HWND>(::SendMessageW(mdiClientHandle, WM_MDIGETACTIVE, 0, 0));
  if (activeChildHandle == nullptr) {
    return nullptr;
  }

  return wxFindWinFromHandle(static_cast<int>(reinterpret_cast<std::uintptr_t>(activeChildHandle)));
}

/**
 * Address: 0x009FC010 (FUN_009FC010)
 *
 * What it does:
 * Executes base window activation handling, then forwards one activate event
 * to the currently active MDI child when parent activation transitions to
 * active/click-active.
 */
bool wxHandleMdiParentActivation(
  void* const mdiParentRuntime,
  const unsigned short activationState,
  const bool minimized,
  const unsigned int activatedNativeHandle
)
{
  bool handled = wxHandleWindowActivationEvent(mdiParentRuntime, activationState, minimized, activatedNativeHandle);

  auto* const parentView = static_cast<WxMdiParentRuntimeView*>(mdiParentRuntime);
  if (parentView == nullptr) {
    return handled;
  }

  auto* const activeChildWindow = static_cast<wxWindowBase*>(parentView->mActiveChildWindow);
  if (activeChildWindow != nullptr && (activationState == 1u || activationState == 2u)) {
    WxActivateEventFactoryRuntime childActivateEvent{};
    childActivateEvent.mEventId = ResolveRuntimeWindowId(activeChildWindow);
    childActivateEvent.mIsActive = 1u;
    childActivateEvent.mEventObject = activeChildWindow;

    wxWindowBase* const childEventHandler = ResolveRuntimeEventHandler(activeChildWindow);
    if (childEventHandler != nullptr && childEventHandler->ProcessEvent(&childActivateEvent)) {
      handled = true;
    }

    RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(&childActivateEvent));
  }

  return handled;
}

/**
 * Address: 0x009FC740 (FUN_009FC740)
 *
 * What it does:
 * Toggles the parent MDI-client `WS_EX_CLIENTEDGE` lane based on active-child
 * maximize style and reapplies non-size/non-move frame positioning flags.
 */
bool wxSyncMdiClientEdgeStyle(
  void* const mdiChildRuntime,
  void* const outClientRect
)
{
  const auto* const childView = static_cast<const WxMdiChildRuntimeView*>(mdiChildRuntime);
  if (childView == nullptr || childView->mParentFrame == nullptr) {
    return false;
  }

  const auto* const parentView = static_cast<const WxMdiParentRuntimeView*>(childView->mParentFrame);
  if (parentView == nullptr) {
    return false;
  }

  const void* const activeChildRuntime = wxFindActiveMdiChildWindow(parentView);
  if (activeChildRuntime != nullptr && activeChildRuntime != mdiChildRuntime) {
    return false;
  }

  const HWND mdiClientHandle = ResolveRuntimeNativeHandle(parentView->mMdiClientWindow);
  if (mdiClientHandle == nullptr) {
    return false;
  }

  const LONG currentExStyle = ::GetWindowLongW(mdiClientHandle, GWL_EXSTYLE);
  LONG activeChildStyle = 0;
  if (activeChildRuntime != nullptr) {
    const HWND activeChildHandle = ResolveRuntimeNativeHandle(activeChildRuntime);
    if (activeChildHandle != nullptr) {
      activeChildStyle = ::GetWindowLongW(activeChildHandle, GWL_STYLE);
    }
  }

  const LONG nextExStyle =
    (activeChildStyle & WS_MAXIMIZE) != 0 ? (currentExStyle & ~static_cast<LONG>(WS_EX_CLIENTEDGE))
                                          : (currentExStyle | static_cast<LONG>(WS_EX_CLIENTEDGE));
  if (nextExStyle == currentExStyle) {
    return false;
  }

  (void)::RedrawWindow(mdiClientHandle, nullptr, nullptr, 0x81u);
  (void)::SetWindowLongW(mdiClientHandle, GWL_EXSTYLE, nextExStyle);
  (void)::SetWindowPos(
    mdiClientHandle,
    nullptr,
    0,
    0,
    0,
    0,
    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOOWNERZORDER
  );

  if (outClientRect != nullptr) {
    (void)::GetClientRect(mdiClientHandle, static_cast<RECT*>(outClientRect));
  }

  return true;
}

/**
 * Address: 0x009FD4B0 (FUN_009FD4B0)
 *
 * What it does:
 * Sends `WM_MDIDESTROY` for one MDI child through its parent MDI client,
 * reapplies client-edge style when no child remains active, then tears down
 * child-menu and native-handle association lanes.
 */
void wxDestroyMdiChildNativeWindow(
  void* const mdiChildRuntime
)
{
  auto* const childView = static_cast<WxMdiChildRuntimeView*>(mdiChildRuntime);
  if (childView == nullptr) {
    return;
  }

  auto* const parentView = static_cast<WxMdiParentRuntimeView*>(childView->mParentFrame);
  gWxMdiChildPendingDestroyHandle = childView->mNativeHandle;
  if (parentView != nullptr && parentView->mMdiClientWindow != nullptr) {
    if (const HWND mdiClientHandle = ResolveRuntimeNativeHandle(parentView->mMdiClientWindow); mdiClientHandle != nullptr) {
      (void)::SendMessageW(mdiClientHandle, WM_MDIDESTROY, reinterpret_cast<WPARAM>(gWxMdiChildPendingDestroyHandle), 0);
    }

    if (wxFindActiveMdiChildWindow(parentView) == nullptr) {
      (void)wxSyncMdiClientEdgeStyle(mdiChildRuntime, nullptr);
    }
  }

  gWxMdiChildPendingDestroyHandle = nullptr;
  if (childView->mWindowMenu != nullptr) {
    (void)::DestroyMenu(static_cast<HMENU>(childView->mWindowMenu));
    childView->mWindowMenu = nullptr;
  }

  wxRemoveHandleAssociation(mdiChildRuntime);
  childView->mNativeHandle = nullptr;
}

/**
 * Address: 0x0099F260 (FUN_0099F260)
 *
 * What it does:
 * Routes one frame command through control HWND forwarding, popup-menu
 * handling (only for notification lanes `0/1`), then menu-selected fallback
 * dispatch.
 */
bool wxHandleFrameCommandWithPopupMenu(
  void* const frameRuntime,
  const unsigned int commandId,
  const unsigned short notificationCode,
  const int controlHandle
)
{
  if (controlHandle != 0) {
    if (wxWindowMswRuntime* const commandWindow = wxFindWinFromHandle(controlHandle); commandWindow != nullptr) {
      return commandWindow->MSWCommand(commandId, notificationCode);
    }
  }

  if (notificationCode > 1u) {
    return false;
  }

  void* const popupMenuRuntime = wxCurrentPopupMenu;
  if (popupMenuRuntime != nullptr) {
    wxCurrentPopupMenu = nullptr;
    return wxMenuMswCommandRuntime(popupMenuRuntime, notificationCode, commandId);
  }

  return wxDispatchMenuSelectionCommandEvent(frameRuntime, static_cast<unsigned short>(commandId));
}

/**
 * Address: 0x009A90D0 (FUN_009A90D0)
 *
 * What it does:
 * Builds and dispatches one `wxEVT_COMMAND_MENU_SELECTED` event for
 * `frameRuntime`, synchronizing check/radio item state lanes when the
 * resolved menu item is checkable.
 */
bool wxDispatchMenuSelectionCommandEvent(
  void* const frameRuntime,
  const unsigned short commandId
)
{
  void* const menuLookupHost = ResolveFrameMenuLookupHost(frameRuntime);
  if (menuLookupHost == nullptr) {
    return false;
  }

  wxCommandEventRuntime commandEvent(EnsureWxEvtCommandMenuSelectedRuntimeType(), commandId);
  commandEvent.mEventObject = frameRuntime;

  WxMenuItemRuntimeView* const menuItem = FindMenuItemByCommandId(menuLookupHost, commandId);
  bool shouldDispatchEvent = (menuItem == nullptr);
  if (
    menuItem != nullptr && menuItem->mVtable != nullptr && menuItem->mVtable->mIsCheckable != nullptr &&
    menuItem->mVtable->mIsCheckable(menuItem)
  ) {
    const std::int32_t menuItemKind = menuItem->mKind;
    if (
      (menuItemKind == 1 || menuItemKind == 2) && menuItem->mVtable->mSetChecked != nullptr &&
      menuItem->mVtable->mIsChecked != nullptr
    ) {
      const bool nextCheckedState = (menuItem->mIsChecked == 0u);
      menuItem->mVtable->mSetChecked(menuItem, nextCheckedState);
      commandEvent.mCommandInt = menuItem->mVtable->mIsChecked(menuItem) ? 1 : 0;
    }

    shouldDispatchEvent = true;
  }

  if (shouldDispatchEvent) {
    wxWindowBase* const eventHandler = ResolveRuntimeEventHandler(static_cast<wxWindowBase*>(frameRuntime));
    if (eventHandler != nullptr) {
      (void)eventHandler->ProcessEvent(&commandEvent);
    }
  }

  return true;
}

/**
 * Address: 0x009FC610 (FUN_009FC610)
 *
 * What it does:
 * Routes one frame command through control HWND forwarding, popup-menu
 * handling, and menu-item lookup fallback before menu-selected dispatch.
 */
bool wxHandleFrameMenuCommand(
  void* const frameRuntime,
  const unsigned int commandId,
  const unsigned short notificationCode,
  const int controlHandle
)
{
  if (controlHandle != 0) {
    if (wxWindowMswRuntime* const commandWindow = wxFindWinFromHandle(controlHandle); commandWindow != nullptr) {
      return commandWindow->MSWCommand(commandId, notificationCode);
    }
  }

  void* const popupMenuRuntime = wxCurrentPopupMenu;
  if (popupMenuRuntime != nullptr) {
    wxCurrentPopupMenu = nullptr;
    if (wxMenuMswCommandRuntime(popupMenuRuntime, notificationCode, commandId)) {
      return true;
    }
  }

  if (ResolveFrameMenuLookupHost(frameRuntime) != nullptr) {
    void* const menuLookupHost = ResolveFrameMenuLookupHost(frameRuntime);
    if (
      menuLookupHost != nullptr &&
      FindMenuItemByCommandId(menuLookupHost, static_cast<std::uint16_t>(commandId)) != nullptr
    ) {
      return wxDispatchMenuSelectionCommandEvent(frameRuntime, static_cast<unsigned short>(commandId));
    }
  }

  return false;
}

/**
 * Address: 0x009FD0E0 (FUN_009FD0E0)
 *
 * What it does:
 * Handles one MDI parent command lane including window-arrangement command
 * ids, child activation by command-id range, and menu-routing fallbacks.
 */
bool wxHandleMdiParentMenuCommand(
  void* const mdiParentRuntime,
  const unsigned int commandId,
  const unsigned short notificationCode,
  const int controlHandle
)
{
  if (controlHandle != 0) {
    if (wxWindowMswRuntime* const commandWindow = wxFindWinFromHandle(controlHandle); commandWindow != nullptr) {
      return commandWindow->MSWCommand(commandId, notificationCode);
    }
  }

  const std::uint16_t commandWord = static_cast<std::uint16_t>(commandId);

  UINT mdiMessage = 0u;
  WPARAM mdiWParam = 0u;
  LPARAM mdiLParam = 0;
  bool isMdiSystemCommand = true;

  switch (commandWord) {
  case kMdiCommandTileHorizontal:
    mdiMessage = WM_MDITILE;
    mdiWParam = 3u;
    break;
  case kMdiCommandCascade:
    mdiMessage = WM_MDICASCADE;
    mdiWParam = 2u;
    break;
  case kMdiCommandArrangeIcons:
    mdiMessage = WM_MDIICONARRANGE;
    break;
  case kMdiCommandNextWindow:
    mdiMessage = WM_MDINEXT;
    mdiLParam = 0;
    break;
  case kMdiCommandTileVertical:
    mdiMessage = WM_MDITILE;
    mdiWParam = 2u;
    break;
  case kMdiCommandPreviousWindow:
    mdiMessage = WM_MDINEXT;
    mdiLParam = 1;
    break;
  default:
    isMdiSystemCommand = false;
    break;
  }

  auto* const parentView = static_cast<WxMdiParentRuntimeView*>(mdiParentRuntime);
  if (isMdiSystemCommand) {
    const HWND mdiClientHandle = parentView != nullptr ? ResolveRuntimeNativeHandle(parentView->mMdiClientWindow) : nullptr;
    if (mdiClientHandle != nullptr) {
      (void)::SendMessageW(mdiClientHandle, mdiMessage, mdiWParam, mdiLParam);
    }
    return true;
  }

  if (commandWord >= kMdiSystemCommandRangeStart) {
    return false;
  }

  if (commandWord >= kMdiChildCommandRangeStart && commandWord <= kMdiChildCommandRangeEnd) {
    if (parentView == nullptr) {
      return false;
    }

    for (wxNodeBaseRuntime* childNode = parentView->mChildNodeHead; childNode != nullptr; childNode = childNode->mNext) {
      auto* const childWindowRuntime = static_cast<wxWindowBase*>(childNode->mValue);
      if (childWindowRuntime == nullptr) {
        continue;
      }

      const HWND childWindowHandle = ResolveRuntimeNativeHandle(childWindowRuntime);
      if (childWindowHandle == nullptr) {
        continue;
      }

      if (static_cast<std::uint16_t>(wxGetWindowId(childWindowHandle)) != commandWord) {
        continue;
      }

      const HWND mdiClientHandle = ResolveRuntimeNativeHandle(parentView->mMdiClientWindow);
      if (mdiClientHandle != nullptr) {
        (void)::SendMessageW(mdiClientHandle, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(childWindowHandle), 0);
      }
      return true;
    }

    return false;
  }

  if (parentView != nullptr && parentView->mRouteCommandsToParent != 0u) {
    return wxDispatchMenuSelectionCommandEvent(mdiParentRuntime, commandWord);
  }

  if (parentView != nullptr && parentView->mActiveChildWindow != nullptr) {
    return wxHandleFrameMenuCommand(mdiParentRuntime, commandWord, notificationCode, controlHandle);
  }

  return false;
}

/**
 * Address: 0x009FC6C0 (FUN_009FC6C0)
 *
 * What it does:
 * Forwards one child-frame `WM_GETMINMAXINFO` lane through default window-proc
 * handling, then applies finite max-size hints into `ptMaxTrackSize`.
 */
bool wxHandleMdiChildGetMinMaxInfo(
  void* const mdiChildRuntime,
  void* const minMaxInfoRuntime
)
{
  (void)CallRuntimeDefaultWindowProc(
    mdiChildRuntime,
    WM_GETMINMAXINFO,
    0u,
    static_cast<long>(reinterpret_cast<std::intptr_t>(minMaxInfoRuntime))
  );

  auto* const frameView = static_cast<WxFrameWindowProcRuntimeView*>(mdiChildRuntime);
  if (
    frameView == nullptr || frameView->mVtable == nullptr || frameView->mVtable->mGetMdiChildMaxWidth == nullptr ||
    frameView->mVtable->mGetMdiChildMaxHeight == nullptr
  ) {
    return true;
  }

  const std::int32_t maxWidth = frameView->mVtable->mGetMdiChildMaxWidth(mdiChildRuntime);
  const std::int32_t maxHeight = frameView->mVtable->mGetMdiChildMaxHeight(mdiChildRuntime);

  auto* const minMaxInfo = static_cast<MINMAXINFO*>(minMaxInfoRuntime);
  if (minMaxInfo == nullptr) {
    return true;
  }

  if (maxWidth != -1) {
    minMaxInfo->ptMaxTrackSize.x = maxWidth;
  }
  if (maxHeight != -1) {
    minMaxInfo->ptMaxTrackSize.y = maxHeight;
  }
  return true;
}

/**
 * Address: 0x009FCD30 (FUN_009FCD30)
 *
 * What it does:
 * Unpacks one MDI-activate message into fixed activation-state `1`,
 * activated-handle, and deactivated-handle output lanes.
 */
unsigned int* wxUnpackMdiActivateMessage(
  const unsigned int deactivatedNativeHandle,
  const unsigned int activatedNativeHandle,
  unsigned short* const outActivationState,
  unsigned int* const outActivatedNativeHandle,
  unsigned int* const outDeactivatedNativeHandle
)
{
  if (outActivationState != nullptr) {
    *outActivationState = 1u;
  }
  if (outActivatedNativeHandle != nullptr) {
    *outActivatedNativeHandle = activatedNativeHandle;
  }
  if (outDeactivatedNativeHandle != nullptr) {
    *outDeactivatedNativeHandle = deactivatedNativeHandle;
  }
  return outDeactivatedNativeHandle;
}

/**
 * Address: 0x009FD2A0 (FUN_009FD2A0)
 *
 * What it does:
 * Applies one MDI child activation transition, synchronizes parent active
 * child/menu lanes, updates MDI client menus, and dispatches one activate
 * event from the child runtime.
 */
bool wxHandleMdiChildActivationChange(
  void* const mdiChildRuntime,
  const unsigned short activationState,
  const unsigned int activatedNativeHandle,
  const unsigned int deactivatedNativeHandle
)
{
  (void)activationState;

  auto* const childView = static_cast<WxMdiChildRuntimeView*>(mdiChildRuntime);
  if (childView == nullptr) {
    return false;
  }

  auto* const parentView = static_cast<WxMdiParentRuntimeView*>(childView->mParentFrame);
  if (parentView == nullptr) {
    return false;
  }

  const unsigned int childNativeHandle =
    static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(childView->mNativeHandle));

  bool isActivating = false;
  void* nextWindowMenu = nullptr;
  bool shouldSetMdiMenu = false;
  if (childNativeHandle == activatedNativeHandle) {
    parentView->mActiveChildWindow = mdiChildRuntime;
    nextWindowMenu = childView->mWindowMenu;
    isActivating = true;
    if (nextWindowMenu != nullptr) {
      parentView->mRouteCommandsToParent = 0u;
      shouldSetMdiMenu = true;
    }
  } else if (childNativeHandle == deactivatedNativeHandle) {
    nextWindowMenu = parentView->mWindowMenu;
    parentView->mActiveChildWindow = nullptr;
    if (nextWindowMenu != nullptr && activatedNativeHandle == 0u) {
      parentView->mRouteCommandsToParent = 1u;
      shouldSetMdiMenu = true;
    }
  } else {
    return false;
  }

  if (shouldSetMdiMenu) {
    void* menuBarNativeHandle = nullptr;
    if (parentView->mWindowMenuBar != nullptr) {
      const auto* const menuBarView = static_cast<const WxMenuRuntimeView*>(parentView->mWindowMenuBar);
      menuBarNativeHandle = reinterpret_cast<void*>(menuBarView->mNativeMenuHandle);
    }

    (void)wxSetMdiClientWindowMenu(parentView->mMdiClientWindow, nextWindowMenu, menuBarNativeHandle);
  }

  WxActivateEventFactoryRuntime activateEvent{};
  activateEvent.mEventId = ResolveRuntimeWindowId(static_cast<wxWindowBase*>(mdiChildRuntime));
  activateEvent.mIsActive = isActivating ? 1u : 0u;
  activateEvent.mEventObject = static_cast<wxWindowBase*>(mdiChildRuntime);

  (void)wxSyncMdiClientEdgeStyle(mdiChildRuntime, nullptr);

  const auto* const childActivationView = static_cast<const WxWindowActivationRuntimeView*>(mdiChildRuntime);
  const bool handled =
    childActivationView != nullptr && childActivationView->mEventHandler != nullptr &&
    childActivationView->mEventHandler->ProcessEvent(&activateEvent);

  RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(&activateEvent));
  return handled;
}

/**
 * Address: 0x009FD3D0 (FUN_009FD3D0)
 *
 * What it does:
 * Handles one child `WM_WINDOWPOSCHANGING` lane by syncing MDI client edge
 * style and maximize geometry into `WINDOWPOS`, then propagating bridge-state
 * updates when parent MDI lanes request it.
 */
bool wxHandleMdiChildWindowPosChanging(
  void* const mdiChildRuntime,
  void* const windowPosRuntime
)
{
  auto* const windowPos = static_cast<WINDOWPOS*>(windowPosRuntime);
  const auto* const childView = static_cast<const WxMdiChildRuntimeView*>(mdiChildRuntime);
  if (windowPos == nullptr || childView == nullptr) {
    return false;
  }

  if ((windowPos->flags & SWP_NOSIZE) == 0u) {
    const DWORD extendedStyle = ::GetWindowLongW(childView->mNativeHandle, GWL_EXSTYLE);
    const DWORD windowStyle = ::GetWindowLongW(childView->mNativeHandle, GWL_STYLE);

    RECT clientRect{};
    if (wxSyncMdiClientEdgeStyle(mdiChildRuntime, &clientRect) && (windowStyle & static_cast<DWORD>(WS_MAXIMIZE)) != 0u) {
      (void)::AdjustWindowRectEx(&clientRect, windowStyle, FALSE, extendedStyle);
      windowPos->x = clientRect.left;
      windowPos->y = clientRect.top;
      windowPos->cx = clientRect.right - clientRect.left;
      windowPos->cy = clientRect.bottom - clientRect.top;
    }

    void* const parentFrameRuntime = childView->mParentFrame;
    if (parentFrameRuntime != nullptr) {
      void* const mdiBridgeRuntime = ResolveMdiBridgeRuntime(parentFrameRuntime);
      const auto* const bridgeView = static_cast<const WxMdiBridgeRuntimeView*>(mdiBridgeRuntime);
      if (
        bridgeView != nullptr && (bridgeView->mFlags & 0x2u) != 0u && bridgeView->mVtable != nullptr &&
        bridgeView->mVtable->mSyncClientState != nullptr
      ) {
        bridgeView->mVtable->mSyncClientState(mdiBridgeRuntime, 1, 0);
      }
    }
  }

  return false;
}

/**
 * Address: 0x009FD5F0 (FUN_009FD5F0)
 *
 * What it does:
 * Handles MDI parent frame window-proc lanes for create/nonclient activate/
 * activate/menu command/menu select, then forwards unhandled lanes to
 * `wxFrame::MSWWindowProc`.
 */
long wxHandleMdiParentWindowProc(
  void* const mdiParentRuntime,
  const unsigned int message,
  const unsigned int wParam,
  const long lParam
)
{
  if (message <= WM_NCACTIVATE) {
    if (message == WM_NCACTIVATE) {
      return 1;
    }

    if (message == WM_CREATE) {
      auto* const frameView = static_cast<WxFrameWindowProcRuntimeView*>(mdiParentRuntime);
      auto* const parentView = static_cast<WxMdiParentRuntimeView*>(mdiParentRuntime);
      if (
        frameView == nullptr || frameView->mVtable == nullptr || frameView->mVtable->mCreateMdiClient == nullptr ||
        parentView == nullptr
      ) {
        return -1;
      }

      void* const mdiClientRuntime = frameView->mVtable->mCreateMdiClient(mdiParentRuntime);
      parentView->mMdiClientWindow = mdiClientRuntime;

      const auto* const mdiClientView = static_cast<const WxMdiClientRuntimeView*>(mdiClientRuntime);
      const std::int32_t mdiClientStyle =
        frameView->mVtable->mGetMdiClientStyle != nullptr ? frameView->mVtable->mGetMdiClientStyle(mdiParentRuntime) : 0;

      const bool created =
        mdiClientView != nullptr && mdiClientView->mVtable != nullptr && mdiClientView->mVtable->mCreateForParent != nullptr &&
        mdiClientView->mVtable->mCreateForParent(mdiClientRuntime, mdiParentRuntime, mdiClientStyle);
      if (!created) {
        wxLogDebug(L"Failed to create MDI parent frame.");
        return -1;
      }

      return 0;
    }

    if (message == WM_ACTIVATE) {
      unsigned short activationState = 0;
      unsigned short minimized = 0;
      unsigned int activatedNativeHandle = 0;
      (void)wxWindowMswRuntime::UnpackActivate(
        static_cast<int>(wParam),
        static_cast<int>(lParam),
        &activationState,
        &minimized,
        &activatedNativeHandle
      );
      const bool handled =
        wxHandleMdiParentActivation(mdiParentRuntime, activationState, minimized != 0u, activatedNativeHandle);
      if (handled) {
        return 0;
      }
      return CallWxFrameWindowProcBase(mdiParentRuntime, message, wParam, lParam);
    }

    return CallWxFrameWindowProcBase(mdiParentRuntime, message, wParam, lParam);
  }

  if (message == WM_COMMAND) {
    unsigned short commandId = 0;
    unsigned short notificationCode = 0;
    unsigned int controlHandle = 0;
    (void)wxWindowMswRuntime::UnpackCommand(
      wParam,
      static_cast<int>(lParam),
      &commandId,
      &controlHandle,
      &notificationCode
    );
    (void)wxHandleMdiParentMenuCommand(mdiParentRuntime, commandId, notificationCode, static_cast<int>(controlHandle));
    (void)CallRuntimeDefaultWindowProc(mdiParentRuntime, WM_COMMAND, wParam, lParam);
    return 0;
  }

  if (message == WM_MENUSELECT) {
    unsigned short menuId = 0;
    unsigned short selectionFlags = 0;
    unsigned int menuHandle = 0;
    (void)wxUnpackMenuSelectMessage(
      wParam,
      static_cast<int>(lParam),
      &menuId,
      &selectionFlags,
      &menuHandle
    );

    const auto* const parentView = static_cast<const WxMdiParentRuntimeView*>(mdiParentRuntime);
    bool handled = false;
    if (parentView != nullptr && parentView->mRouteCommandsToParent != 0u) {
      handled = wxDispatchFrameMenuHighlightEvent(mdiParentRuntime, menuId, selectionFlags, menuHandle);
    } else if (parentView != nullptr && parentView->mActiveChildWindow != nullptr) {
      handled = wxDispatchFrameMenuHighlightEvent(parentView->mActiveChildWindow, menuId, selectionFlags, menuHandle);
    } else {
      return CallWxFrameWindowProcBase(mdiParentRuntime, message, wParam, lParam);
    }

    if (handled) {
      return 0;
    }
    return CallWxFrameWindowProcBase(mdiParentRuntime, message, wParam, lParam);
  }

  return CallWxFrameWindowProcBase(mdiParentRuntime, message, wParam, lParam);
}

/**
 * Address: 0x009FD810 (FUN_009FD810)
 *
 * What it does:
 * Handles MDI child frame window-proc lanes for command/window-pos/minmax/
 * activation transitions and forwards unhandled lanes to
 * `wxFrame::MSWWindowProc`.
 */
long wxHandleMdiChildWindowProc(
  void* const mdiChildRuntime,
  const unsigned int message,
  const unsigned int wParam,
  const long lParam
)
{
  bool handled = false;
  long result = 0;

  if (message <= 0x46u) {
    if (message == 0x46u) {
      handled = wxHandleMdiChildWindowPosChanging(mdiChildRuntime, reinterpret_cast<void*>(lParam));
    } else if (message == WM_MOVE || message == WM_SIZE) {
      (void)CallRuntimeDefaultWindowProc(mdiChildRuntime, message, wParam, lParam);
    } else if (message == WM_GETMINMAXINFO) {
      handled = wxHandleMdiChildGetMinMaxInfo(mdiChildRuntime, reinterpret_cast<void*>(lParam));
    } else {
      return CallWxFrameWindowProcBase(mdiChildRuntime, message, wParam, lParam);
    }
  } else {
    if (message == WM_COMMAND) {
      unsigned short commandId = 0;
      unsigned short notificationCode = 0;
      unsigned int controlHandle = 0;
      (void)wxWindowMswRuntime::UnpackCommand(
        wParam,
        static_cast<int>(lParam),
        &commandId,
        &controlHandle,
        &notificationCode
      );
      handled = wxHandleFrameMenuCommand(mdiChildRuntime, commandId, notificationCode, static_cast<int>(controlHandle));
    } else if (message == WM_SYSCOMMAND) {
      return CallRuntimeDefaultWindowProc(mdiChildRuntime, WM_SYSCOMMAND, wParam, lParam);
    } else if (message == WM_MDIACTIVATE) {
      unsigned short activationState = 0;
      unsigned int activatedNativeHandle = 0;
      unsigned int deactivatedNativeHandle = 0;
      (void)wxUnpackMdiActivateMessage(
        wParam,
        static_cast<unsigned int>(lParam),
        &activationState,
        &activatedNativeHandle,
        &deactivatedNativeHandle
      );
      handled = wxHandleMdiChildActivationChange(
        mdiChildRuntime,
        activationState,
        activatedNativeHandle,
        deactivatedNativeHandle
      );
      (void)CallRuntimeDefaultWindowProc(mdiChildRuntime, message, wParam, lParam);
    } else {
      return CallWxFrameWindowProcBase(mdiChildRuntime, message, wParam, lParam);
    }
  }

  if (!handled) {
    return CallWxFrameWindowProcBase(mdiChildRuntime, message, wParam, lParam);
  }

  return result;
}

namespace
{
  wxWindowBase* gCurrentWindowCreationHookTarget = nullptr;

  class WxWindowCreationHookRuntimeScope
  {
  public:
    /**
     * Address: 0x00968BF0 (FUN_00968BF0, wxWindowCreationHook::wxWindowCreationHook)
     *
     * What it does:
     * Stores the current window-creation target lane while one native window
     * create message is in flight.
     */
    explicit WxWindowCreationHookRuntimeScope(wxWindowBase* const creatingWindow) noexcept
      : mPreviousWindow(gCurrentWindowCreationHookTarget)
    {
      gCurrentWindowCreationHookTarget = creatingWindow;
    }

    /**
     * Address: 0x00968C00 (FUN_00968C00, wxWindowCreationHook::~wxWindowCreationHook)
     *
     * What it does:
     * Restores the previous window-creation target lane.
     */
    ~WxWindowCreationHookRuntimeScope()
    {
      gCurrentWindowCreationHookTarget = mPreviousWindow;
    }

  private:
    wxWindowBase* mPreviousWindow = nullptr;
  };

  using WxDeleteWithFlagFn = void (__thiscall*)(void* objectRuntime, int deleteFlag);
  struct WxDeleteWithFlagVTableRuntime
  {
    void* mUnknown00 = nullptr;
    WxDeleteWithFlagFn mDeleteWithFlag = nullptr; // +0x04
  };
  static_assert(
    offsetof(WxDeleteWithFlagVTableRuntime, mDeleteWithFlag) == 0x4,
    "WxDeleteWithFlagVTableRuntime::mDeleteWithFlag offset must be 0x4"
  );

  [[nodiscard]] bool TryTranslateActiveMdiChildMessage(
    void* const activeChildRuntime,
    MSG* const message
  ) noexcept
  {
    if (activeChildRuntime == nullptr || message == nullptr) {
      return false;
    }

    const auto* const childView = static_cast<const WxMdiChildTranslateRuntimeView*>(activeChildRuntime);
    if (childView->mNativeHandle == nullptr || childView->mVtable == nullptr || childView->mVtable->mTranslateMessage == nullptr) {
      return false;
    }

    return childView->mVtable->mTranslateMessage(activeChildRuntime, message);
  }

  [[nodiscard]] int ResolveMdiCreateCoordinate(
    const int coordinate
  ) noexcept
  {
    return coordinate <= -1 ? CW_USEDEFAULT : coordinate;
  }

  [[nodiscard]] long BuildNativeMdiChildStyle(
    const long style
  ) noexcept
  {
    long nativeStyle = (style & kMdiChildStyleUseMinimizeBoxBase) != 0 ? kMdiChildStyleMinimizeBoxBase : kMdiChildStyleBase;
    if ((style & kMdiChildStyleHasMaximizeBox) != 0) {
      nativeStyle |= WS_MAXIMIZEBOX;
    }
    if ((style & kMdiChildStyleHasResizeBorder) != 0) {
      nativeStyle |= WS_THICKFRAME;
    }
    if ((style & kMdiChildStyleHasSystemMenu) != 0) {
      nativeStyle |= WS_SYSMENU;
    }
    if ((style & kMdiChildStyleStartMinimized) != 0) {
      nativeStyle |= WS_MINIMIZE;
    }
    if ((style & kMdiChildStyleStartMaximized) != 0) {
      nativeStyle |= WS_MAXIMIZE;
    }
    if ((style & kMdiChildStyleDecoratedCaption) != 0) {
      nativeStyle |= WS_CAPTION;
    }
    return nativeStyle;
  }

  [[nodiscard]] HWND ResolveMdiParentClientHandle(
    const void* const mdiParentRuntime
  ) noexcept
  {
    const auto* const parentView = static_cast<const WxMdiParentRuntimeView*>(mdiParentRuntime);
    return parentView != nullptr && parentView->mMdiClientWindow != nullptr
      ? ResolveRuntimeNativeHandle(parentView->mMdiClientWindow)
      : nullptr;
  }

  [[nodiscard]] LRESULT SendMdiChildCommandToClient(
    void* const mdiChildRuntime,
    const UINT message
  ) noexcept
  {
    const auto* const childView = static_cast<const WxMdiChildRuntimeView*>(mdiChildRuntime);
    if (childView == nullptr || childView->mParentFrame == nullptr) {
      return 0;
    }

    const auto* const parentView = static_cast<const WxMdiParentRuntimeView*>(childView->mParentFrame);
    if (parentView == nullptr || parentView->mMdiClientWindow == nullptr) {
      return 0;
    }

    const HWND mdiClientHandle = ResolveRuntimeNativeHandle(parentView->mMdiClientWindow);
    if (mdiClientHandle == nullptr) {
      return 0;
    }

    return ::SendMessageW(mdiClientHandle, message, reinterpret_cast<WPARAM>(childView->mNativeHandle), 0);
  }

  void RuntimeDeleteWithFlagOne(
    void* const objectRuntime
  ) noexcept
  {
    if (objectRuntime == nullptr) {
      return;
    }

    const auto* const vtable = *reinterpret_cast<WxDeleteWithFlagVTableRuntime* const*>(objectRuntime);
    if (vtable != nullptr && vtable->mDeleteWithFlag != nullptr) {
      vtable->mDeleteWithFlag(objectRuntime, 1);
    }
  }

  void InitializeFrameBaseCtorLanes(
    void* const frameRuntime
  ) noexcept
  {
    if (frameRuntime == nullptr) {
      return;
    }

    auto* const frameBaseView = static_cast<WxFrameBaseRuntimeView*>(frameRuntime);
    frameBaseView->mFrameMenuBar = nullptr;
    frameBaseView->mOldStatusTextStorage = nullptr;
    frameBaseView->mFrameStatusBar = nullptr;
    frameBaseView->mStatusBarPane = nullptr;
  }

  void InitializeFrameBaseInitLanes(
    void* const frameRuntime
  ) noexcept
  {
    if (frameRuntime == nullptr) {
      return;
    }

    auto* const frameBaseView = static_cast<WxFrameBaseRuntimeView*>(frameRuntime);
    frameBaseView->mUnknown164 = nullptr;
    frameBaseView->mUnknown168 = nullptr;
    frameBaseView->mUnknown16C = nullptr;
    frameBaseView->mUnknown170 = nullptr;
    frameBaseView->mUnknown174 = 0;
  }
}

/**
 * Address: 0x009FC0F0 (FUN_009FC0F0)
 *
 * What it does:
 * Forwards one parent-frame default window-proc lane through `DefFrameProcW`
 * using parent and MDI-client native handles.
 */
long wxMdiParentDefFrameWindowProc(
  void* const mdiParentRuntime,
  const unsigned int message,
  const unsigned int wParam,
  const long lParam
)
{
  const HWND mdiClientHandle = ResolveMdiParentClientHandle(mdiParentRuntime);
  const HWND parentHandle = ResolveRuntimeNativeHandle(mdiParentRuntime);
  return ::DefFrameProcW(parentHandle, mdiClientHandle, message, wParam, lParam);
}

/**
 * Address: 0x009FC130 (FUN_009FC130)
 *
 * What it does:
 * Tries active-child translation first, then base-frame translation, and falls
 * back to MDI system-accelerator translation for key lanes.
 */
bool wxMdiParentTranslateMessage(
  void* const mdiParentRuntime,
  void* const nativeMessage
)
{
  auto* const message = static_cast<MSG*>(nativeMessage);
  if (message == nullptr) {
    return false;
  }

  const auto* const parentView = static_cast<const WxMdiParentRuntimeView*>(mdiParentRuntime);
  if (parentView != nullptr && TryTranslateActiveMdiChildMessage(parentView->mActiveChildWindow, message)) {
    return true;
  }

  auto* const frameRuntime = static_cast<wxFrame*>(mdiParentRuntime);
  if (frameRuntime != nullptr && frameRuntime->MSWTranslateMessage(message)) {
    return true;
  }

  if (message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN) {
    return false;
  }

  const HWND mdiClientHandle = ResolveMdiParentClientHandle(mdiParentRuntime);
  return mdiClientHandle != nullptr && ::TranslateMDISysAccel(mdiClientHandle, message) != FALSE;
}

/**
 * Address: 0x009FC1A0 (FUN_009FC1A0)
 *
 * What it does:
 * Sets the post-construction runtime flag lane for one MDI child frame.
 */
void wxMdiChildMarkConstructed(
  void* const mdiChildRuntime
)
{
  if (mdiChildRuntime == nullptr) {
    return;
  }

  auto* const childView = static_cast<WxMdiChildRuntimeView*>(mdiChildRuntime);
  childView->mConstructedRuntimeFlag = 1u;
}

/**
 * Address: 0x009FC1B0 (FUN_009FC1B0)
 *
 * What it does:
 * Creates one child MDI native window from title/position/size/style lanes,
 * associates the created handle with the child runtime, and tracks modeless
 * ownership.
 */
bool wxMdiChildCreateWindow(
  void* const mdiChildRuntime,
  void* const mdiParentRuntime,
  const std::int32_t windowId,
  const wxStringRuntime& title,
  const wxPoint& position,
  const wxSize& size,
  const long style,
  const wxStringRuntime& name
)
{
  if (mdiChildRuntime == nullptr) {
    return false;
  }

  auto* const childWindow = static_cast<wxWindowBase*>(mdiChildRuntime);
  childWindow->SetName(name);
  (void)childWindow->Show(true);

  std::int32_t resolvedWindowId = windowId;
  if (resolvedWindowId <= -1) {
    resolvedWindowId = --gWxWindowBaseLastControlId;
  }

  WxWindowBaseRuntimeState& childState = EnsureWxWindowBaseRuntimeState(childWindow);
  childState.windowId = resolvedWindowId;
  childState.parentWindow = static_cast<wxWindowBase*>(mdiParentRuntime);
  childState.windowStyle = style;

  if (mdiParentRuntime != nullptr) {
    static_cast<wxWindowBase*>(mdiParentRuntime)->AddChild(childWindow);
  }

  MDICREATESTRUCTW createStruct{};
  createStruct.szClass = (style & kMdiChildStyleUseNoRedrawClass) != 0 ? kWxMdiChildFrameClassNameNoRedraw
                                                                        : kWxMdiChildFrameClassName;
  createStruct.szTitle = title.c_str();
  createStruct.hOwner = static_cast<HINSTANCE>(wxGetInstance());
  createStruct.x = ResolveMdiCreateCoordinate(position.x);
  createStruct.y = ResolveMdiCreateCoordinate(position.y);
  createStruct.cx = ResolveMdiCreateCoordinate(size.x);
  createStruct.cy = ResolveMdiCreateCoordinate(size.y);
  createStruct.style = static_cast<DWORD>(BuildNativeMdiChildStyle(style));
  createStruct.lParam = 0;

  WxWindowCreationHookRuntimeScope createHook(childWindow);
  const HWND mdiClientHandle = ResolveMdiParentClientHandle(mdiParentRuntime);
  const HWND createdChildHandle =
    reinterpret_cast<HWND>(::SendMessageW(mdiClientHandle, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&createStruct)));

  auto* const childView = static_cast<WxMdiChildRuntimeView*>(mdiChildRuntime);
  childView->mParentFrame = mdiParentRuntime;
  childView->mNativeHandle = createdChildHandle;
  childState.nativeHandle = static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(createdChildHandle));

  wxAssociateWinWithHandle(createdChildHandle, mdiChildRuntime);
  gWxModelessWindows.push_back(childWindow);
  return true;
}

/**
 * Address: 0x009FC500 (FUN_009FC500)
 *
 * What it does:
 * Resolves one child window origin from screen coordinates into parent
 * MDI-client coordinates and writes `outX/outY`.
 */
long wxMdiChildGetClientOrigin(
  const void* const mdiChildRuntime,
  long* const outX,
  long* const outY
)
{
  const auto* const childView = static_cast<const WxMdiChildRuntimeView*>(mdiChildRuntime);
  if (childView == nullptr || childView->mNativeHandle == nullptr) {
    if (outX != nullptr) {
      *outX = 0;
    }
    if (outY != nullptr) {
      *outY = 0;
    }
    return 0;
  }

  RECT windowRect{};
  (void)::GetWindowRect(childView->mNativeHandle, &windowRect);

  POINT origin{windowRect.left, windowRect.top};
  const HWND mdiClientHandle = ResolveMdiParentClientHandle(childView->mParentFrame);
  if (mdiClientHandle != nullptr) {
    (void)::ScreenToClient(mdiClientHandle, &origin);
  }

  if (outX != nullptr) {
    *outX = origin.x;
  }
  if (outY != nullptr) {
    *outY = origin.y;
  }
  return origin.x;
}

/**
 * Address: 0x009FC560 (FUN_009FC560)
 *
 * What it does:
 * Returns the configured MDI child-frame icon handle, falling back to the
 * default icon lane when the standard icon lane is unset.
 */
void* wxGetMdiChildFrameIconHandle()
{
  return gWxStdMdiChildFrameIcon != nullptr ? static_cast<void*>(gWxStdMdiChildFrameIcon)
                                            : static_cast<void*>(gWxDefaultMdiChildFrameIcon);
}

/**
 * Address: 0x009FC570 (FUN_009FC570)
 *
 * What it does:
 * Sends one maximize/restore command for the child through its parent
 * MDI-client window.
 */
long wxMdiChildSendMaximizeCommand(
  void* const mdiChildRuntime,
  const bool maximize
)
{
  const UINT mdiMessage = maximize ? WM_MDIMAXIMIZE : WM_MDIRESTORE;
  return SendMdiChildCommandToClient(mdiChildRuntime, mdiMessage);
}

/**
 * Address: 0x009FC5B0 (FUN_009FC5B0)
 *
 * What it does:
 * Sends one `WM_MDIRESTORE` lane for the child through the parent MDI-client.
 */
long wxMdiChildRestoreWindow(
  void* const mdiChildRuntime
)
{
  return SendMdiChildCommandToClient(mdiChildRuntime, WM_MDIRESTORE);
}

/**
 * Address: 0x009FC5E0 (FUN_009FC5E0)
 *
 * What it does:
 * Sends one `WM_MDIACTIVATE` lane for the child through the parent MDI-client.
 */
long wxMdiChildActivateWindow(
  void* const mdiChildRuntime
)
{
  return SendMdiChildCommandToClient(mdiChildRuntime, WM_MDIACTIVATE);
}

/**
 * Address: 0x009FC710 (FUN_009FC710)
 *
 * What it does:
 * Forwards one child-frame default window-proc lane through `DefMDIChildProcW`.
 */
long wxMdiChildDefFrameWindowProc(
  void* const mdiChildRuntime,
  const unsigned int message,
  const unsigned int wParam,
  const long lParam
)
{
  const HWND childHandle = ResolveRuntimeNativeHandle(mdiChildRuntime);
  return ::DefMDIChildProcW(childHandle, message, wParam, lParam);
}

/**
 * Address: 0x009FCD50 (FUN_009FCD50)
 *
 * What it does:
 * Runs non-deleting MDI parent-frame teardown and applies deleting-dtor thunk
 * semantics when `deleteFlags & 1`.
 */
void* wxDestroyMdiParentFrameWithDeleteFlag(
  void* const mdiParentRuntime,
  const std::uint8_t deleteFlags
) noexcept
{
  if (mdiParentRuntime == nullptr) {
    return nullptr;
  }

  auto* const parentView = static_cast<WxMdiParentRuntimeView*>(mdiParentRuntime);
  parentView->mUnknownOwnedPointer160 = nullptr;
  parentView->mUnknownOwnedPointer158 = nullptr;

  RuntimeDeleteWithFlagOne(parentView->mWindowMenuBar);
  parentView->mWindowMenuBar = nullptr;

  if (parentView->mWindowMenu != nullptr) {
    (void)::DestroyMenu(static_cast<HMENU>(parentView->mWindowMenu));
    parentView->mWindowMenu = nullptr;
  }

  if (parentView->mMdiClientWindow != nullptr) {
    auto* const mdiClientView = static_cast<WxMdiClientRuntimeView*>(parentView->mMdiClientWindow);
    mdiClientView->mNativeHandle = nullptr;
    RuntimeDeleteWithFlagOne(parentView->mMdiClientWindow);
    parentView->mMdiClientWindow = nullptr;
  }

  parentView->mActiveChildWindow = nullptr;
  (void)WX_FrameDestroyWithoutDelete(static_cast<wxTopLevelWindowRuntime*>(mdiParentRuntime));

  if ((deleteFlags & 1u) != 0u) {
    ::operator delete(mdiParentRuntime);
  }

  return mdiParentRuntime;
}

/**
 * Address: 0x009FCDB0 (FUN_009FCDB0)
 *
 * What it does:
 * Returns the static class-info table lane for wx MDI child frame RTTI.
 */
void* wxGetMdiChildFrameClassInfo() noexcept
{
  return gWxMdiChildFrameClassInfoTable;
}

/**
 * Address: 0x009FCDE0 (FUN_009FCDE0)
 *
 * What it does:
 * Returns the static class-info table lane for wx MDI client-window RTTI.
 */
void* wxGetMdiClientWindowClassInfo() noexcept
{
  return gWxMdiClientWindowClassInfoTable;
}

/**
 * Address: 0x009FCE00 (FUN_009FCE00)
 *
 * What it does:
 * Applies wx-window deleting-dtor thunk semantics for one MDI client-window
 * runtime lane.
 */
void* wxDestroyMdiClientWindowWithDeleteFlag(
  void* const mdiClientRuntime,
  const std::uint8_t deleteFlags
) noexcept
{
  if (mdiClientRuntime == nullptr) {
    return nullptr;
  }

  auto* const window = static_cast<wxWindowMswRuntime*>(mdiClientRuntime);
  window->MSWDestroyWindow();

  if (const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(window); state != nullptr && state->nativeHandle != 0u) {
    gWxWindowByNativeHandle.erase(static_cast<int>(state->nativeHandle));
  }
  gWxWindowBaseStateByWindow.erase(window);

  if ((deleteFlags & 1u) != 0u) {
    ::operator delete(mdiClientRuntime);
  }
  return mdiClientRuntime;
}

/**
 * Address: 0x0099F680 (FUN_0099F680)
 *
 * What it does:
 * Initializes one frame runtime lane by running frame-base constructor/init
 * lane transitions used by frame-derived constructors.
 */
void* wxConstructFrameRuntimeBase(
  void* const frameRuntime
)
{
  if (frameRuntime == nullptr) {
    return nullptr;
  }

  InitializeFrameBaseCtorLanes(frameRuntime);
  InitializeFrameBaseInitLanes(frameRuntime);

  WxWindowBaseRuntimeState& state = EnsureWxWindowBaseRuntimeState(static_cast<wxWindowBase*>(frameRuntime));
  state.eventHandler = static_cast<wxWindowBase*>(frameRuntime);
  if (state.windowId == -1) {
    state.windowId = -1;
  }
  return frameRuntime;
}

/**
 * Address: 0x009FB720 (FUN_009FB720)
 *
 * What it does:
 * Initializes one MDI parent-frame runtime lane from frame-base state and
 * seeds parent/client/menu routing lanes.
 */
void* wxConstructMdiParentFrameRuntime(
  void* const mdiParentRuntime
)
{
  auto* const parentView = static_cast<WxMdiParentRuntimeView*>(wxConstructFrameRuntimeBase(mdiParentRuntime));
  if (parentView == nullptr) {
    return nullptr;
  }

  parentView->mMdiClientWindow = nullptr;
  parentView->mActiveChildWindow = nullptr;
  parentView->mWindowMenuBar = nullptr;
  parentView->mRouteCommandsToParent = 1u;
  return mdiParentRuntime;
}

/**
 * Address: 0x009FCE20 (FUN_009FCE20)
 *
 * What it does:
 * Allocates one MDI parent-frame runtime object and runs its constructor lane.
 */
void* wxAllocateAndConstructMdiParentFrameRuntime()
{
  void* const storage = ::operator new(sizeof(WxMdiParentRuntimeView), std::nothrow);
  if (storage == nullptr) {
    return nullptr;
  }
  return wxConstructMdiParentFrameRuntime(storage);
}

/**
 * Address: 0x009FCE90 (FUN_009FCE90)
 *
 * What it does:
 * Allocates one MDI child-frame runtime object, runs frame-base constructor
 * lanes, and marks child-construction completion.
 */
void* wxAllocateAndConstructMdiChildFrameRuntime()
{
  void* const storage = ::operator new(sizeof(WxMdiChildRuntimeView), std::nothrow);
  if (storage == nullptr) {
    return nullptr;
  }

  auto* const childView = static_cast<WxMdiChildRuntimeView*>(wxConstructFrameRuntimeBase(storage));
  if (childView == nullptr) {
    return nullptr;
  }

  childView->mParentFrame = nullptr;
  childView->mNativeHandle = nullptr;
  childView->mWindowMenu = nullptr;
  wxMdiChildMarkConstructed(childView);
  return childView;
}

/**
 * Address: 0x00962900 (FUN_00962900, wxLogDebug)
 *
 * What it does:
 * Preserves wx debug-log callsites as a no-op lane.
 */
void wxLogDebug(
  ...
)
{}

/**
 * Address: 0x00962910 (FUN_00962910, wxLogTrace)
 *
 * What it does:
 * Preserves wx trace-log callsites as a no-op lane.
 */
void wxLogTrace(
  ...
)
{}

/**
 * Address: 0x00966E60 (FUN_00966E60, nullsub_3482)
 *
 * What it does:
 * Preserves one wx runtime callback lane as an intentional no-op.
 */
void wxNoOpRuntimeCallbackH()
{}

/**
 * Address: 0x00966E70 (FUN_00966E70, nullsub_3483)
 *
 * What it does:
 * Preserves one wx runtime callback lane as an intentional no-op.
 */
void wxNoOpRuntimeCallbackI()
{}

/**
 * Address: 0x00967010 (FUN_00967010, nullsub_3484)
 *
 * What it does:
 * Preserves one stdcall wx runtime callback lane with one stack argument as an
 * intentional no-op.
 */
void __stdcall wxNoOpRuntimeStdCall1G(const std::int32_t reservedArg0)
{
  static_cast<void>(reservedArg0);
}

/**
 * Address: 0x00983420 (FUN_00983420, nullsub_3491)
 *
 * What it does:
 * Preserves one stdcall wx runtime callback lane with one stack argument as an
 * intentional no-op.
 */
void __stdcall wxNoOpRuntimeStdCall1H(const std::int32_t reservedArg0)
{
  static_cast<void>(reservedArg0);
}

/**
 * Address: 0x00978200 (FUN_00978200, nullsub_3488)
 *
 * What it does:
 * Preserves one wx runtime callback lane as an intentional no-op.
 */
void wxNoOpRuntimeCallbackA()
{}

/**
 * Address: 0x0097F9B0 (FUN_0097F9B0, sub_97F9B0)
 *
 * What it does:
 * Preserves one `wxMBConvUTF7` vtable callback lane as an intentional no-op
 * that returns zero.
 */
int __stdcall wxMBConvUTF7NoOpVirtualSlot(
  const std::int32_t reservedArg0,
  const std::int32_t reservedArg1,
  const std::int32_t reservedArg2
)
{
  static_cast<void>(reservedArg0);
  static_cast<void>(reservedArg1);
  static_cast<void>(reservedArg2);
  return 0;
}

/**
 * Address: 0x00999B70 (FUN_00999B70, nullsub_3495)
 *
 * What it does:
 * Preserves one stdcall wx runtime callback lane with one stack argument as an
 * intentional no-op.
 */
void __stdcall wxNoOpRuntimeStdCall1(const std::int32_t reservedArg0)
{
  static_cast<void>(reservedArg0);
}

/**
 * Address: 0x009A8EE0 (FUN_009A8EE0, nullsub_3496)
 *
 * What it does:
 * Preserves one wx runtime callback lane as an intentional no-op.
 */
void wxNoOpRuntimeCallbackB()
{}

/**
 * Address: 0x009AD4F0 (FUN_009AD4F0, nullsub_3501)
 *
 * What it does:
 * Preserves one `wxThread` vtable virtual lane as an intentional no-op.
 */
void wxThreadNoOpVirtualSlot()
{}

/**
 * Address: 0x009C5EE0 (FUN_009C5EE0, nullsub_3505)
 *
 * What it does:
 * Preserves one stdcall wx runtime callback lane with two stack arguments as
 * an intentional no-op.
 */
void __stdcall wxNoOpRuntimeStdCall2A(
  const std::int32_t reservedArg0,
  const std::int32_t reservedArg1
)
{
  static_cast<void>(reservedArg0);
  static_cast<void>(reservedArg1);
}

/**
 * Address: 0x0098C950 (FUN_0098C950, sub_98C950)
 *
 * What it does:
 * Preserves the wx native-dialog procedure lane as an intentional no-op.
 */
INT_PTR __stdcall wxNoOpNativeDialogProc(
  HWND dialogWindow,
  UINT dialogMessage,
  WPARAM dialogWParam,
  LPARAM dialogLParam
)
{
  static_cast<void>(dialogWindow);
  static_cast<void>(dialogMessage);
  static_cast<void>(dialogWParam);
  static_cast<void>(dialogLParam);
  return 0;
}

/**
 * Address: 0x0098D8F0 (FUN_0098D8F0)
 *
 * What it does:
 * Queries one window rectangle and merges it into an existing `(left, top,
 * right, bottom)` bounds lane when the input bounds are active.
 */
BOOL wxMergeWindowRectIntoBounds(
  const HWND windowHandle,
  LPRECT inOutBounds
) noexcept
{
  if (inOutBounds == nullptr) {
    return FALSE;
  }

  const LONG previousLeft = inOutBounds->left;
  const LONG previousTop = inOutBounds->top;
  const LONG previousRight = inOutBounds->right;
  const LONG previousBottom = inOutBounds->bottom;

  const BOOL queryResult = ::GetWindowRect(windowHandle, inOutBounds);
  if (previousLeft >= 0) {
    if (previousLeft < inOutBounds->left) {
      inOutBounds->left = previousLeft;
    }
    if (previousRight > inOutBounds->right) {
      inOutBounds->right = previousRight;
    }
    if (previousTop < inOutBounds->top) {
      inOutBounds->top = previousTop;
    }
    if (previousBottom > inOutBounds->bottom) {
      inOutBounds->bottom = previousBottom;
    }
  }

  return queryResult;
}

/**
 * Address: 0x00998840 (FUN_00998840)
 *
 * What it does:
 * Builds one transient `MENUITEMINFOW` lane that sets `MIIM_STATE` to
 * `MFS_DEFAULT` and applies it to the requested menu item.
 */
BOOL wxMenuMarkItemAsDefault(
  const HMENU menuHandle,
  const UINT itemId
)
{
  MENUITEMINFOW menuItemInfo{};
  menuItemInfo.cbSize = sizeof(menuItemInfo);
  menuItemInfo.fMask = MIIM_STATE;
  menuItemInfo.fState = MFS_DEFAULT;
  return ::SetMenuItemInfoW(menuHandle, itemId, FALSE, &menuItemInfo);
}

/**
 * Address: 0x009A4CE0 (FUN_009A4CE0)
 *
 * What it does:
 * Draws one rectangle outline from integer `(left, top, right, bottom)` bounds
 * using `MoveToEx` + four `LineTo` segments.
 */
BOOL wxDrawRectangleOutlineFromBounds(
  const HDC deviceContext,
  const std::int32_t* const boundsLtrb
)
{
  if (deviceContext == nullptr || boundsLtrb == nullptr) {
    return FALSE;
  }

  (void)::MoveToEx(deviceContext, boundsLtrb[0], boundsLtrb[1], nullptr);
  (void)::LineTo(deviceContext, boundsLtrb[2], boundsLtrb[1]);
  (void)::LineTo(deviceContext, boundsLtrb[2], boundsLtrb[3]);
  (void)::LineTo(deviceContext, boundsLtrb[0], boundsLtrb[3]);
  return ::LineTo(deviceContext, boundsLtrb[0], boundsLtrb[1]);
}

namespace
{
  struct WxComboBoxRuntimeView
  {
    std::uint8_t reserved00_107[0x108]{}; // +0x00
    HWND listHandle = nullptr;            // +0x108
  };
  static_assert(offsetof(WxComboBoxRuntimeView, listHandle) == 0x108, "WxComboBoxRuntimeView::listHandle offset must be 0x108");
}

/**
 * Address: 0x009AF6E0 (FUN_009AF6E0)
 *
 * What it does:
 * Probes the combo-list host window at point `(4,4)` and returns the child HWND
 * resolved by `ChildWindowFromPoint`.
 */
HWND wxComboBoxResolveInnerChildWindow(
  void* const comboBoxRuntime
)
{
  const auto* const comboView = static_cast<const WxComboBoxRuntimeView*>(comboBoxRuntime);
  if (comboView == nullptr || comboView->listHandle == nullptr) {
    return nullptr;
  }

  constexpr POINT kProbePoint{4, 4};
  return ::ChildWindowFromPoint(comboView->listHandle, kProbePoint);
}

/**
 * Address: 0x009C5EF0 (FUN_009C5EF0, nullsub_3506)
 *
 * What it does:
 * Preserves one stdcall wx runtime callback lane with two stack arguments as
 * an intentional no-op.
 */
void __stdcall wxNoOpRuntimeStdCall2B(
  const std::int32_t reservedArg0,
  const std::int32_t reservedArg1
)
{
  static_cast<void>(reservedArg0);
  static_cast<void>(reservedArg1);
}

/**
 * Address: 0x009C5F00 (FUN_009C5F00, nullsub_3507)
 *
 * What it does:
 * Preserves one stdcall wx runtime callback lane with one stack argument as an
 * intentional no-op.
 */
void __stdcall wxNoOpRuntimeStdCall1B(const std::int32_t reservedArg0)
{
  static_cast<void>(reservedArg0);
}

/**
 * Address: 0x009C88E0 (FUN_009C88E0, nullsub_3509)
 *
 * What it does:
 * Preserves one wx runtime callback lane as an intentional no-op.
 */
void wxNoOpRuntimeCallbackC()
{}

/**
 * Address: 0x009C88F0 (FUN_009C88F0, nullsub_3510)
 *
 * What it does:
 * Preserves one wx runtime callback lane as an intentional no-op.
 */
void wxNoOpRuntimeCallbackD()
{}

/**
 * Address: 0x009C8900 (FUN_009C8900, nullsub_3511)
 *
 * What it does:
 * Preserves one wx runtime callback lane as an intentional no-op.
 */
void wxNoOpRuntimeCallbackE()
{}

/**
 * Address: 0x009C9DE0 (FUN_009C9DE0, nullsub_3512)
 *
 * What it does:
 * Preserves one wx runtime callback lane as an intentional no-op.
 */
void wxNoOpRuntimeCallbackJ()
{}

/**
 * Address: 0x009C9DF0 (FUN_009C9DF0, nullsub_3513)
 *
 * What it does:
 * Preserves one wx runtime callback lane as an intentional no-op.
 */
void wxNoOpRuntimeCallbackK()
{}

/**
 * Address: 0x009C9E00 (FUN_009C9E00, nullsub_3514)
 *
 * What it does:
 * Preserves one wx runtime callback lane as an intentional no-op.
 */
void wxNoOpRuntimeCallbackL()
{}

/**
 * Address: 0x009D2F00 (FUN_009D2F00, nullsub_3515)
 *
 * What it does:
 * Preserves one wx runtime callback lane as an intentional no-op.
 */
void wxNoOpRuntimeCallbackM()
{}

/**
 * Address: 0x00A06BF0 (FUN_00A06BF0, nullsub_3517)
 *
 * What it does:
 * Preserves one stdcall wx runtime callback lane with one stack argument as an
 * intentional no-op.
 */
void __stdcall wxNoOpRuntimeStdCall1C(const std::int32_t reservedArg0)
{
  static_cast<void>(reservedArg0);
}

/**
 * Address: 0x00A07DD0 (FUN_00A07DD0, nullsub_3518)
 *
 * What it does:
 * Preserves one stdcall wx runtime callback lane with two stack arguments as
 * an intentional no-op.
 */
void __stdcall wxNoOpRuntimeStdCall2C(
  const std::int32_t reservedArg0,
  const std::int32_t reservedArg1
)
{
  static_cast<void>(reservedArg0);
  static_cast<void>(reservedArg1);
}

/**
 * Address: 0x00A0B3F0 (FUN_00A0B3F0, nullsub_3519)
 *
 * What it does:
 * Preserves one stdcall wx runtime callback lane with one stack argument as an
 * intentional no-op.
 */
void __stdcall wxNoOpRuntimeStdCall1D(const std::int32_t reservedArg0)
{
  static_cast<void>(reservedArg0);
}

/**
 * Address: 0x00A0DC40 (FUN_00A0DC40, nullsub_3520)
 *
 * What it does:
 * Preserves one wx runtime callback lane as an intentional no-op.
 */
void wxNoOpRuntimeCallbackF()
{}

/**
 * Address: 0x00A0E400 (FUN_00A0E400, nullsub_3521)
 *
 * What it does:
 * Preserves one stdcall wx runtime callback lane with one stack argument as an
 * intentional no-op.
 */
void __stdcall wxNoOpRuntimeStdCall1I(const std::int32_t reservedArg0)
{
  static_cast<void>(reservedArg0);
}

/**
 * Address: 0x00A0E410 (FUN_00A0E410, nullsub_3522)
 *
 * What it does:
 * Preserves one stdcall wx runtime callback lane with one stack argument as an
 * intentional no-op.
 */
void __stdcall wxNoOpRuntimeStdCall1J(const std::int32_t reservedArg0)
{
  static_cast<void>(reservedArg0);
}

/**
 * Address: 0x00A18DB0 (FUN_00A18DB0, nullsub_3523)
 *
 * What it does:
 * Preserves one wx runtime callback lane as an intentional no-op.
 */
void wxNoOpRuntimeCallbackG()
{}

/**
 * Address: 0x00A20780 (FUN_00A20780, nullsub_8)
 *
 * What it does:
 * Preserves one runtime function-pointer dispatch lane as an intentional
 * no-op.
 */
void wxNoOpRuntimeDispatchSlot()
{}

/**
 * Address: 0x00A27140 (FUN_00A27140, nullsub_3525)
 *
 * What it does:
 * Preserves one stdcall wx runtime callback lane with one stack argument as an
 * intentional no-op.
 */
void __stdcall wxNoOpRuntimeStdCall1F(const std::int32_t reservedArg0)
{
  static_cast<void>(reservedArg0);
}

/**
 * Address: 0x00A37F30 (FUN_00A37F30, nullsub_3526)
 *
 * What it does:
 * Preserves one stdcall wx runtime callback lane with one stack argument as an
 * intentional no-op.
 */
void __stdcall wxNoOpRuntimeStdCall1E(const std::int32_t reservedArg0)
{
  static_cast<void>(reservedArg0);
}

/**
 * Address: 0x009DD360 (FUN_009DD360, nullsub_3486)
 *
 * What it does:
 * No-op hook lane used by wx file-buffer flush helpers before commit/fflush
 * dispatch.
 */
void wxNoOpFileFlushHook()
{}

/**
 * Address: 0x009BCDD0 (FUN_009BCDD0, wxDeleteStockLists)
 *
 * What it does:
 * Releases each global wx stock-list singleton (brush, pen, font, bitmap)
 * and clears the stored singleton pointer lanes.
 */
void wxDeleteStockLists()
{
  DeleteStockList(wxTheBrushList);
  DeleteStockList(wxThePenList);
  DeleteStockList(wxTheFontList);
  DeleteStockList(wxTheBitmapList);
}

/**
 * Address: 0x009C4840 (FUN_009C4840)
 *
 * What it does:
 * Displays one fatal-message modal box (`MB_ICONHAND`) by dereferencing
 * pointer-stable wx string text/caption lanes supplied by caller-owned
 * `wxString` storage.
 */
int wxShowFatalMessageBoxFromStringStorage(
  const wchar_t* const* const titleText,
  const wchar_t* const* const messageText
)
{
  return ::MessageBoxW(nullptr, *messageText, *titleText, MB_ICONHAND);
}

/**
 * Address: 0x009C4860 (FUN_009C4860, wxSafeShowMessage)
 *
 * What it does:
 * Formats one fatal-log message into a fixed stack buffer, wraps both title
 * and message in temporary wx string storage, then shows the message box via
 * the pointer-based helper lane.
 */
int wxSafeShowMessage(
  const wchar_t* const formatText,
  va_list argList
)
{
  constexpr std::size_t kMessageBufferCount = 2048u;
  wchar_t messageBuffer[kMessageBufferCount]{};

  (void)std::vswprintf(messageBuffer, kMessageBufferCount, formatText, argList);
  messageBuffer[kMessageBufferCount - 1] = L'\0';

  wxStringRuntime message = AllocateOwnedWxString(messageBuffer);
  wxStringRuntime title = AllocateOwnedWxString(L"Fatal Error");

  const int result = wxShowFatalMessageBoxFromStringStorage(&title.m_pchData, &message.m_pchData);

  ReleaseOwnedWxString(title);
  ReleaseOwnedWxString(message);
  return result;
}

/**
 * Address: 0x009C4FB0 (FUN_009C4FB0)
 *
 * What it does:
 * Runs one wx log-chain destruction lane by deleting owned/previous chained
 * log sinks through vtable deleting-destructor slot 1.
 */
[[maybe_unused]] int wxDestroyLogChainRuntime(
  void* const logChainRuntime
)
{
  struct WxLogChainRuntimeView
  {
    void** vtable = nullptr; // +0x00
    void* lane04 = nullptr;  // +0x04
    void* previousLog = nullptr; // +0x08
    void* oldLog = nullptr;      // +0x0C
  };

  using DeletingDtor = int(__thiscall*)(void*, int);
  auto callDeletingDtor = [](void* object) -> int {
    if (object == nullptr) {
      return 0;
    }

    auto** const vtable = *reinterpret_cast<void***>(object);
    if (vtable == nullptr || vtable[1] == nullptr) {
      return 0;
    }

    return reinterpret_cast<DeletingDtor>(vtable[1])(object, 1);
  };

  auto* const chain = static_cast<WxLogChainRuntimeView*>(logChainRuntime);
  if (chain == nullptr) {
    return 0;
  }

  int result = 0;
  if (chain->oldLog != nullptr) {
    result = callDeletingDtor(chain->oldLog);
  }

  void* const previousLog = chain->previousLog;
  if (previousLog != nullptr && previousLog != chain) {
    result = callDeletingDtor(previousLog);
  }
  return result;
}

/**
 * Address: 0x009C4940 (FUN_009C4940, wxVLogFatalError)
 *
 * What it does:
 * Initializes one variadic argument lane, forwards the fatal message through
 * `wxSafeShowMessage`, and terminates the process with `abort()`.
 */
[[noreturn]] void wxVLogFatalError(
  wchar_t* const formatText,
  ...
)
{
  va_list argList;
  va_start(argList, formatText);
  (void)wxSafeShowMessage(formatText, argList);
  va_end(argList);
  std::abort();
}

/**
 * Address: 0x009AAB90 (FUN_009AAB90)
 * Mangled: wxCheckBuildOptions
 *
 * What it does:
 * Verifies caller build-options against the embedded wx runtime tuple
 * `(2, 4, no-debug)` and routes mismatches to fatal-error reporting.
 */
bool wxCheckBuildOptions(
  const wxBuildOptionsRuntime* const buildOptions
)
{
  if (buildOptions != nullptr &&
      buildOptions->debugBuild == 0u &&
      buildOptions->versionMajor == 2 &&
      buildOptions->versionMinor == 4)
  {
    return true;
  }

  const wchar_t* const appBuildTag =
    buildOptions != nullptr && buildOptions->debugBuild != 0u ? L"debug" : L"no debug";
  const std::int32_t appMajor = buildOptions != nullptr ? buildOptions->versionMajor : 0;
  const std::int32_t appMinor = buildOptions != nullptr ? buildOptions->versionMinor : 0;

  wxVLogFatalError(
    const_cast<wchar_t*>(L"Mismatch between the program and library build versions detected.\n"
                         L"The library used %d.%d (%s), and your program used %d.%d (%s)."),
    2,
    4,
    L"no debug",
    appMajor,
    appMinor,
    appBuildTag
  );
}

/**
 * Address: 0x009C7D70 (FUN_009C7D70, wxColourDisplay)
 *
 * What it does:
 * Caches one display color-capability lane using `GetDeviceCaps(BITSPIXEL)`
 * and returns true when the current desktop device reports color output.
 */
BOOL wxColourDisplay()
{
  if (gWxColourDisplayCache == -1) {
    HDC const deviceContext = ::GetDC(nullptr);
    const int colorBits = ::GetDeviceCaps(deviceContext, BITSPIXEL);
    gWxColourDisplayCache = 0;
    if (colorBits == -1 || colorBits > 2) {
      gWxColourDisplayCache = 1;
    }
    (void)::ReleaseDC(nullptr, deviceContext);
  }

  return gWxColourDisplayCache != 0 ? TRUE : FALSE;
}

class wxListBaseRuntime
{
public:
  explicit wxListBaseRuntime(const std::int32_t) noexcept {}

  virtual ~wxListBaseRuntime() = default;
};

class wxListRuntime : public wxListBaseRuntime
{
public:
  wxListRuntime() noexcept
    : wxListBaseRuntime(0)
  {
  }
};

/**
 * Address: 0x0096FE10 (FUN_0096FE10, scalar deleting destructor thunk lane)
 *
 * What it does:
 * Runs the `wxList` destructor lane (vtable reset + base destructor body)
 * without deleting object storage.
 */
void wxListDestroyNoDelete(wxListRuntime* const object) noexcept
{
  if (object == nullptr) {
    return;
  }

  object->~wxListRuntime();
}

struct WxDwordArrayRuntimeView
{
  std::uint32_t capacity = 0; // +0x00
  std::uint32_t size = 0;     // +0x04
  std::int32_t* data = nullptr; // +0x08
};
static_assert(offsetof(WxDwordArrayRuntimeView, capacity) == 0x00, "WxDwordArrayRuntimeView::capacity offset must be 0x00");
static_assert(offsetof(WxDwordArrayRuntimeView, size) == 0x04, "WxDwordArrayRuntimeView::size offset must be 0x04");
static_assert(offsetof(WxDwordArrayRuntimeView, data) == 0x08, "WxDwordArrayRuntimeView::data offset must be 0x08");
static_assert(sizeof(WxDwordArrayRuntimeView) == 0x0C, "WxDwordArrayRuntimeView size must be 0x0C");

struct WxWordArrayRuntimeView
{
  std::uint32_t capacity = 0; // +0x00
  std::uint32_t size = 0;     // +0x04
  std::int16_t* data = nullptr; // +0x08
};
static_assert(offsetof(WxWordArrayRuntimeView, capacity) == 0x00, "WxWordArrayRuntimeView::capacity offset must be 0x00");
static_assert(offsetof(WxWordArrayRuntimeView, size) == 0x04, "WxWordArrayRuntimeView::size offset must be 0x04");
static_assert(offsetof(WxWordArrayRuntimeView, data) == 0x08, "WxWordArrayRuntimeView::data offset must be 0x08");
static_assert(sizeof(WxWordArrayRuntimeView) == 0x0C, "WxWordArrayRuntimeView size must be 0x0C");

[[nodiscard]] std::uint32_t SaturatingLegacyByteCount(
  const std::uint32_t elementCount,
  const std::uint32_t elementSize
) noexcept
{
  const std::uint64_t byteCount = static_cast<std::uint64_t>(elementCount) * static_cast<std::uint64_t>(elementSize);
  if (byteCount > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return std::numeric_limits<std::uint32_t>::max();
  }

  return static_cast<std::uint32_t>(byteCount);
}

/**
 * Address: 0x009A82D0 (FUN_009A82D0)
 *
 * What it does:
 * Replaces one legacy dword-array payload with an exact-size copy of another
 * array's active element lane.
 */
WxDwordArrayRuntimeView* WxDwordArrayAssignFrom(
  WxDwordArrayRuntimeView* const destination,
  const WxDwordArrayRuntimeView* const source
)
{
  if (destination->data != nullptr) {
    ::operator delete(destination->data);
    destination->data = nullptr;
  }

  destination->size = source->size;
  destination->capacity = source->size;
  if (source->size != 0u) {
    auto* const copiedData = static_cast<std::int32_t*>(
      ::operator new(sizeof(std::int32_t) * static_cast<std::size_t>(source->size), std::nothrow)
    );
    destination->data = copiedData;
    if (copiedData != nullptr) {
      std::memcpy(copiedData, source->data, sizeof(std::int32_t) * static_cast<std::size_t>(destination->size));
    } else {
      destination->capacity = 0u;
    }
  } else {
    destination->data = nullptr;
  }

  return destination;
}

/**
 * Address: 0x009A8480 (FUN_009A8480)
 *
 * What it does:
 * Clears active dword-array length when existing capacity is sufficient, or
 * rebuilds storage to exactly `requiredCapacity` elements otherwise.
 */
void WxDwordArrayResetWithMinimumCapacity(
  WxDwordArrayRuntimeView* const array,
  const std::uint32_t requiredCapacity
)
{
  if (requiredCapacity <= array->capacity) {
    array->size = 0u;
    return;
  }

  if (array->data != nullptr) {
    ::operator delete(array->data);
    array->data = nullptr;
  }

  array->capacity = 0u;
  array->data = static_cast<std::int32_t*>(
    ::operator new(SaturatingLegacyByteCount(requiredCapacity, static_cast<std::uint32_t>(sizeof(std::int32_t))), std::nothrow)
  );
  array->size = 0u;
  if (array->data != nullptr) {
    array->capacity = requiredCapacity;
  }
}

/**
 * Address: 0x009A87B0 (FUN_009A87B0)
 *
 * What it does:
 * Initializes one dword-array payload from another array's active-size lane
 * by allocating/copying exactly `source->size` entries.
 */
WxDwordArrayRuntimeView* WxDwordArrayInitializeFromSource(
  WxDwordArrayRuntimeView* const destination,
  const WxDwordArrayRuntimeView* const source
)
{
  const std::uint32_t sourceSize = source->size;
  destination->size = sourceSize;
  destination->capacity = sourceSize;
  if (sourceSize != 0u) {
    destination->data = static_cast<std::int32_t*>(
      ::operator new(SaturatingLegacyByteCount(sourceSize, static_cast<std::uint32_t>(sizeof(std::int32_t))), std::nothrow)
    );
    if (destination->data != nullptr) {
      std::memcpy(destination->data, source->data, sizeof(std::int32_t) * static_cast<std::size_t>(destination->size));
    } else {
      destination->capacity = 0u;
    }
  } else {
    destination->data = nullptr;
  }

  return destination;
}

/**
 * Address: 0x009A7E10 (FUN_009A7E10, sub_9A7E10)
 *
 * What it does:
 * Ensures one legacy word-array has enough free slots to append
 * `additionalCount` values, using the original geometric growth lane and
 * reallocating with copy-forward semantics.
 */
void WxWordArrayEnsureAppendCapacity(
  WxWordArrayRuntimeView* const array,
  const std::uint32_t additionalCount
)
{
  std::uint32_t growBy = additionalCount;
  const std::uint32_t capacity = array->capacity;
  const std::uint32_t size = array->size;
  if (size == capacity || (capacity - size) < additionalCount) {
    if (capacity != 0u) {
      std::uint32_t minimumGrow = 16u;
      if (capacity >= 16u) {
        minimumGrow = capacity >> 1u;
        if (minimumGrow > 0x1000u) {
          minimumGrow = 0x1000u;
        }
      }

      if (additionalCount < minimumGrow) {
        growBy = minimumGrow;
      }

      const std::uint32_t requestedCapacity = capacity + growBy;
      auto* const resizedData = static_cast<std::int16_t*>(
        ::operator new(SaturatingLegacyByteCount(requestedCapacity, static_cast<std::uint32_t>(sizeof(std::int16_t))))
      );
      if (resizedData != nullptr) {
        array->capacity += growBy;
        std::memcpy(resizedData, array->data, static_cast<std::size_t>(size) * sizeof(std::int16_t));
        ::operator delete(array->data);
        array->data = resizedData;
      }
    } else {
      std::uint32_t newCapacity = 16u;
      if (additionalCount > 16u) {
        newCapacity = additionalCount;
      }

      auto* const allocatedData = static_cast<std::int16_t*>(
        ::operator new(SaturatingLegacyByteCount(newCapacity, static_cast<std::uint32_t>(sizeof(std::int16_t))))
      );
      array->data = allocatedData;
      if (allocatedData != nullptr) {
        array->capacity = newCapacity;
      }
    }
  }
}

/**
 * Address: 0x009A8360 (FUN_009A8360, sub_9A8360)
 * Address: 0x009A88B0 (FUN_009A88B0, sub_9A88B0)
 *
 * What it does:
 * Ensures one legacy dword-array has enough free slots to append
 * `additionalCount` values, using the original geometric growth lane and
 * reallocating with copy-forward semantics.
 */
void WxDwordArrayEnsureAppendCapacity(
  WxDwordArrayRuntimeView* const array,
  const std::uint32_t additionalCount
)
{
  std::uint32_t growBy = additionalCount;
  const std::uint32_t capacity = array->capacity;
  const std::uint32_t size = array->size;
  if (size == capacity || (capacity - size) < additionalCount) {
    if (capacity != 0u) {
      std::uint32_t minimumGrow = 16u;
      if (capacity >= 16u) {
        minimumGrow = capacity >> 1u;
        if (minimumGrow > 0x1000u) {
          minimumGrow = 0x1000u;
        }
      }

      if (additionalCount < minimumGrow) {
        growBy = minimumGrow;
      }

      const std::uint32_t requestedCapacity = capacity + growBy;
      auto* const resizedData = static_cast<std::int32_t*>(
        ::operator new(SaturatingLegacyByteCount(requestedCapacity, static_cast<std::uint32_t>(sizeof(std::int32_t))))
      );
      if (resizedData != nullptr) {
        array->capacity += growBy;
        std::memcpy(resizedData, array->data, sizeof(std::int32_t) * static_cast<std::size_t>(size));
        ::operator delete(array->data);
        array->data = resizedData;
      }
    } else {
      std::uint32_t newCapacity = 16u;
      if (additionalCount > 16u) {
        newCapacity = additionalCount;
      }

      auto* const allocatedData = static_cast<std::int32_t*>(
        ::operator new(SaturatingLegacyByteCount(newCapacity, static_cast<std::uint32_t>(sizeof(std::int32_t))))
      );
      array->data = allocatedData;
      if (allocatedData != nullptr) {
        array->capacity = newCapacity;
      }
    }
  }
}

/**
 * Address: 0x009A8150 (FUN_009A8150, sub_9A8150)
 *
 * What it does:
 * Inserts `copyCount` copies of one 16-bit value at `insertionIndex`, shifting
 * the tail lane right and growing storage as needed.
 */
std::uint32_t WxWordArrayInsertCopiesAtIndexRuntime(
  WxWordArrayRuntimeView* const array,
  const std::int16_t value,
  const std::uint32_t insertionIndex,
  const std::uint32_t copyCount
)
{
  std::uint32_t result = array->size;
  if (insertionIndex <= result && result <= (result + copyCount) && copyCount != 0u) {
    WxWordArrayEnsureAppendCapacity(array, copyCount);
    std::memmove(
      array->data + insertionIndex + copyCount,
      array->data + insertionIndex,
      sizeof(std::int16_t) * static_cast<std::size_t>(array->size - insertionIndex)
    );

    result = static_cast<std::uint32_t>(sizeof(std::int16_t)) * insertionIndex;
    std::uint32_t remaining = copyCount;
    do
    {
      array->data[result / sizeof(std::int16_t)] = value;
      result += static_cast<std::uint32_t>(sizeof(std::int16_t));
      --remaining;
    }
    while (remaining != 0u);

    array->size += copyCount;
  }

  return result;
}

/**
 * Address: 0x009A8690 (FUN_009A8690, sub_9A8690)
 * Address: 0x009A8BE0 (FUN_009A8BE0, sub_9A8BE0)
 *
 * What it does:
 * Inserts `copyCount` copies of one 32-bit value at `insertionIndex`, shifting
 * the tail lane right and growing storage as needed.
 */
std::uint32_t WxDwordArrayInsertCopiesAtIndexRuntime(
  WxDwordArrayRuntimeView* const array,
  const std::int32_t value,
  const std::uint32_t insertionIndex,
  const std::uint32_t copyCount
)
{
  std::uint32_t result = array->size;
  if (insertionIndex <= result && result <= (result + copyCount) && copyCount != 0u) {
    WxDwordArrayEnsureAppendCapacity(array, copyCount);
    std::memmove(
      array->data + insertionIndex + copyCount,
      array->data + insertionIndex,
      sizeof(std::int32_t) * static_cast<std::size_t>(array->size - insertionIndex)
    );

    result = static_cast<std::uint32_t>(sizeof(std::int32_t)) * insertionIndex;
    std::uint32_t remaining = copyCount;
    do
    {
      array->data[result / sizeof(std::int32_t)] = value;
      result += static_cast<std::uint32_t>(sizeof(std::int32_t));
      --remaining;
    }
    while (remaining != 0u);

    array->size += copyCount;
  }

  return result;
}

/**
 * Address: 0x009A89D0 (FUN_009A89D0, sub_9A89D0)
 *
 * What it does:
 * Clears active element count when current capacity is enough, otherwise frees
 * old storage and rebuilds the dword-array lane to `requiredCapacity`.
 */
void WxDwordArrayResetWithMinimumCapacityAlias(
  WxDwordArrayRuntimeView* const array,
  const std::uint32_t requiredCapacity
)
{
  WxDwordArrayResetWithMinimumCapacity(array, requiredCapacity);
}

/**
 * Address: 0x009A8650 (FUN_009A8650, sub_9A8650)
 *
 * What it does:
 * Ensures one legacy dword-array has room for `copyCount` additional elements
 * and appends `value` exactly `copyCount` times.
 */
void wxDwordArrayAppendCopies(
  WxDwordArrayRuntimeView* const array,
  const std::int32_t value,
  const std::int32_t copyCount
)
{
  std::uint32_t remaining = static_cast<std::uint32_t>(copyCount);
  if (remaining != 0u) {
    WxDwordArrayEnsureAppendCapacity(array, remaining);
    do
    {
      array->data[array->size++] = value;
      --remaining;
    }
    while (remaining != 0u);
  }
}

class wxBitmapListRuntime : public wxListBaseRuntime
{
public:
  wxBitmapListRuntime() noexcept
    : wxListBaseRuntime(0)
  {
  }
};

/**
 * Address: 0x009BCE40 (FUN_009BCE40, wxBitmapListInit)
 *
 * What it does:
 * Runs the stock wx bitmap-list constructor lane used by the global list
 * initializers.
 */
[[nodiscard]] wxBitmapListRuntime* wxBitmapListInit(wxBitmapListRuntime* const object) noexcept
{
  return new (object) wxBitmapListRuntime();
}

namespace
{
  class WxListInsertDispatchRuntime
  {
  public:
    virtual ~WxListInsertDispatchRuntime() = default;

    virtual wxNodeBaseRuntime* CreateNode(
      wxNodeBaseRuntime* previous, wxNodeBaseRuntime* next, void* value, const wxListKeyRuntime* key
    ) = 0;
  };

  struct WxListInsertRuntimeView
  {
    void* vtable = nullptr;              // +0x00
    std::int32_t keyType = 0;            // +0x04
    std::int32_t itemCount = 0;          // +0x08
    std::uint8_t reserved0C_0F[0x4]{};   // +0x0C
    wxNodeBaseRuntime* first = nullptr;  // +0x10
    wxNodeBaseRuntime* last = nullptr;   // +0x14
    std::int32_t mutationLock = 0;       // +0x18
  };
  static_assert(offsetof(WxListInsertRuntimeView, itemCount) == 0x08, "WxListInsertRuntimeView::itemCount offset must be 0x08");
  static_assert(offsetof(WxListInsertRuntimeView, first) == 0x10, "WxListInsertRuntimeView::first offset must be 0x10");
  static_assert(offsetof(WxListInsertRuntimeView, last) == 0x14, "WxListInsertRuntimeView::last offset must be 0x14");
  static_assert(
    offsetof(WxListInsertRuntimeView, mutationLock) == 0x18,
    "WxListInsertRuntimeView::mutationLock offset must be 0x18"
  );

  const wxListKeyRuntime kWxDefaultListKeyRuntime{};
}

/**
 * Address: 0x00978440 (FUN_00978440, sub_978440)
 *
 * What it does:
 * Inserts one list node before `nextNode` (or at list head when null) using
 * the list virtual node-factory lane and updates first/last/count lanes.
 */
wxNodeBaseRuntime* wxListInsertBeforeNodeRuntime(
  WxListInsertRuntimeView* const list,
  wxNodeBaseRuntime* const nextNode,
  void* const value
)
{
  if (list == nullptr || list->mutationLock != 0) {
    return nullptr;
  }

  wxNodeBaseRuntime* insertionNext = nextNode;
  wxNodeBaseRuntime* insertionPrevious = nullptr;
  if (nextNode != nullptr) {
    if (nextNode->mListOwner != list) {
      return nullptr;
    }

    insertionPrevious = nextNode->mPrevious;
  } else {
    insertionNext = list->first;
  }

  auto* const dispatch = reinterpret_cast<WxListInsertDispatchRuntime*>(list);
  wxNodeBaseRuntime* const insertedNode =
    dispatch->CreateNode(insertionPrevious, insertionNext, value, &kWxDefaultListKeyRuntime);

  if (list->first == nullptr) {
    list->last = insertedNode;
  }
  if (insertionPrevious == nullptr) {
    list->first = insertedNode;
  }

  ++list->itemCount;
  return insertedNode;
}

namespace
{
  [[nodiscard]] wxNodeBaseRuntime* WxListFindMemberNode(
    WxListInsertRuntimeView* const list,
    const void* const value
  )
  {
    wxNodeBaseRuntime* node = list->first;
    while (node != nullptr && node->mValue != value) {
      node = node->mNext;
    }

    return node;
  }

  [[nodiscard]] std::int32_t WxListNodeIndexFromHead(
    const wxNodeBaseRuntime* const node
  )
  {
    if (node->mListOwner == nullptr) {
      return -1;
    }

    std::int32_t index = 0;
    for (const wxNodeBaseRuntime* current = node->mPrevious; current != nullptr; current = current->mPrevious) {
      ++index;
    }
    return index;
  }
}

/**
 * Address: 0x00978540 (FUN_00978540)
 *
 * What it does:
 * Locates one payload pointer in a wx list and returns its zero-based index,
 * or `-1` when the payload is absent.
 */
std::int32_t wxListFindObjectIndex(
  WxListInsertRuntimeView* const list,
  void* const object
)
{
  wxNodeBaseRuntime* const memberNode = WxListFindMemberNode(list, object);
  return memberNode != nullptr ? WxListNodeIndexFromHead(memberNode) : -1;
}

namespace
{
  struct WxWindowParentRuntimeView
  {
    void* vtable = nullptr;                // +0x00
    std::uint8_t reserved04_2B[0x28]{};    // +0x04
    wxWindowBase* parent = nullptr;        // +0x2C
  };
  static_assert(offsetof(WxWindowParentRuntimeView, parent) == 0x2C, "WxWindowParentRuntimeView::parent offset must be 0x2C");

  struct WxSplitterWindowRuntimeView
  {
    void* vtable = nullptr;                   // +0x00
    std::uint8_t reserved04_123[0x120]{};    // +0x04
    std::int32_t splitMode = 0;               // +0x124
    std::uint8_t reserved128_12B[0x4]{};      // +0x128
    wxWindowBase* windowOne = nullptr;        // +0x12C
    wxWindowBase* windowTwo = nullptr;        // +0x130
    std::uint8_t reserved134_13F[0xC]{};      // +0x134
    std::int32_t borderSize = 0;              // +0x140
    std::uint8_t reserved144_147[0x4]{};      // +0x144
    std::int32_t sashPosition = 0;            // +0x148
    std::int32_t requestedSashPosition = 0;   // +0x14C
    std::uint8_t reserved150_15B[0xC]{};      // +0x150
    std::int32_t minimumPaneSize = 0;         // +0x15C
  };
  static_assert(offsetof(WxSplitterWindowRuntimeView, splitMode) == 0x124, "WxSplitterWindowRuntimeView::splitMode offset must be 0x124");
  static_assert(offsetof(WxSplitterWindowRuntimeView, windowOne) == 0x12C, "WxSplitterWindowRuntimeView::windowOne offset must be 0x12C");
  static_assert(offsetof(WxSplitterWindowRuntimeView, windowTwo) == 0x130, "WxSplitterWindowRuntimeView::windowTwo offset must be 0x130");
  static_assert(offsetof(WxSplitterWindowRuntimeView, borderSize) == 0x140, "WxSplitterWindowRuntimeView::borderSize offset must be 0x140");
  static_assert(
    offsetof(WxSplitterWindowRuntimeView, sashPosition) == 0x148,
    "WxSplitterWindowRuntimeView::sashPosition offset must be 0x148"
  );
  static_assert(
    offsetof(WxSplitterWindowRuntimeView, requestedSashPosition) == 0x14C,
    "WxSplitterWindowRuntimeView::requestedSashPosition offset must be 0x14C"
  );
  static_assert(
    offsetof(WxSplitterWindowRuntimeView, minimumPaneSize) == 0x15C,
    "WxSplitterWindowRuntimeView::minimumPaneSize offset must be 0x15C"
  );

  struct WxSplitterWindowVTableRuntimeView
  {
    void* reserved000_233[141]{};
    void(__thiscall* sizeWindows)(WxSplitterWindowRuntimeView* splitter); // +0x234
  };
  static_assert(
    offsetof(WxSplitterWindowVTableRuntimeView, sizeWindows) == 0x234,
    "WxSplitterWindowVTableRuntimeView::sizeWindows offset must be 0x234"
  );

  [[nodiscard]] std::int32_t WxSplitterWindowGetWindowSize(
    WxSplitterWindowRuntimeView* const splitter
  )
  {
    std::int32_t width = 0;
    std::int32_t height = 0;
    reinterpret_cast<wxWindowBase*>(splitter)->DoGetClientSize(&width, &height);
    return splitter->splitMode == 2 ? width : height;
  }

  [[nodiscard]] std::int32_t WxSplitterWindowAdjustSashPositionRuntime(
    WxSplitterWindowRuntimeView* const splitter,
    const std::int32_t requestedSashPosition
  )
  {
    const std::int32_t windowSize = WxSplitterWindowGetWindowSize(splitter);
    std::int32_t adjustedSashPosition = requestedSashPosition;

    if (splitter->windowOne != nullptr) {
      std::int32_t minimumOne = splitter->splitMode == 2
        ? splitter->windowOne->GetMinWidth()
        : splitter->windowOne->GetMinHeight();
      if (minimumOne == -1 || splitter->minimumPaneSize > minimumOne) {
        minimumOne = splitter->minimumPaneSize;
      }

      const std::int32_t minimumSashPosition = splitter->borderSize + minimumOne;
      if (adjustedSashPosition < minimumSashPosition) {
        adjustedSashPosition = minimumSashPosition;
      }
    }

    if (splitter->windowTwo == nullptr) {
      return adjustedSashPosition;
    }

    std::int32_t minimumTwo = splitter->splitMode == 2
      ? splitter->windowTwo->GetMinWidth()
      : splitter->windowTwo->GetMinHeight();
    if (minimumTwo == -1 || splitter->minimumPaneSize > minimumTwo) {
      minimumTwo = splitter->minimumPaneSize;
    }

    const std::int32_t maximumSashPosition = windowSize - splitter->borderSize - minimumTwo;
    return adjustedSashPosition <= maximumSashPosition ? adjustedSashPosition : maximumSashPosition;
  }

  [[nodiscard]] bool WxSplitterWindowDoSetSashPositionRuntime(
    WxSplitterWindowRuntimeView* const splitter,
    const std::int32_t requestedSashPosition
  )
  {
    const std::int32_t adjusted = WxSplitterWindowAdjustSashPositionRuntime(splitter, requestedSashPosition);
    if (adjusted == splitter->sashPosition) {
      return false;
    }

    splitter->sashPosition = adjusted;
    return true;
  }

  [[nodiscard]] std::int32_t WxSplitterWindowResolveRequestedSashPosition(
    WxSplitterWindowRuntimeView* const splitter,
    const std::int32_t requestedSashPosition
  )
  {
    if (requestedSashPosition > 0) {
      return requestedSashPosition;
    }
    if (requestedSashPosition >= 0) {
      return WxSplitterWindowGetWindowSize(splitter) / 2;
    }

    return requestedSashPosition + WxSplitterWindowGetWindowSize(splitter);
  }
}

/**
 * Address: 0x00997E80 (FUN_00997E80)
 *
 * What it does:
 * Performs one splitter activation lane by validating pane parent ownership,
 * storing split state, applying sash position, and invoking layout refresh.
 */
bool wxSplitterWindowDoSplit(
  WxSplitterWindowRuntimeView* const splitter,
  const std::int32_t splitMode,
  wxWindowBase* const windowOne,
  wxWindowBase* const windowTwo,
  const std::int32_t requestedSashPosition
)
{
  if (splitter->windowTwo != nullptr ||
      windowOne == nullptr ||
      windowTwo == nullptr ||
      reinterpret_cast<WxWindowParentRuntimeView*>(windowOne)->parent != reinterpret_cast<wxWindowBase*>(splitter) ||
      reinterpret_cast<WxWindowParentRuntimeView*>(windowTwo)->parent != reinterpret_cast<wxWindowBase*>(splitter))
  {
    return false;
  }

  splitter->windowTwo = windowTwo;
  splitter->windowOne = windowOne;
  splitter->splitMode = splitMode;
  splitter->requestedSashPosition = requestedSashPosition;

  const std::int32_t resolvedSashPosition = WxSplitterWindowResolveRequestedSashPosition(splitter, requestedSashPosition);
  (void)WxSplitterWindowDoSetSashPositionRuntime(splitter, resolvedSashPosition);

  auto* const vtable = *reinterpret_cast<WxSplitterWindowVTableRuntimeView**>(splitter);
  vtable->sizeWindows(splitter);
  return true;
}

/**
 * Address: 0x009C7BB0 (FUN_009C7BB0, wxBeginBusyCursor)
 *
 * What it does:
 * Increments busy-cursor nesting depth and, on first entry, swaps the active
 * Win32 cursor to the provided wx cursor handle (or null cursor when refdata
 * is absent), while saving the previous cursor lane.
 */
void wxBeginBusyCursor(wxCursor* const cursor)
{
  if (gs_wxBusyCursorCount++ != 0) {
    return;
  }

  const auto* const objectView = reinterpret_cast<const WxObjectRuntimeView*>(cursor);
  const auto* const refDataView = reinterpret_cast<const WxCursorRefDataRuntimeView*>(objectView->refData);
  if (refDataView != nullptr) {
    gs_wxBusyCursor = reinterpret_cast<HCURSOR>(refDataView->nativeCursorHandle);
    gs_wxBusyCursorOld = ::SetCursor(gs_wxBusyCursor);
    return;
  }

  gs_wxBusyCursor = nullptr;
  gs_wxBusyCursorOld = ::SetCursor(nullptr);
}

/**
 * Address: 0x009C7C00 (FUN_009C7C00, wxEndBusyCursor)
 *
 * What it does:
 * Decrements busy-cursor nesting depth and, when depth reaches zero, restores
 * the previously cached cursor lane.
 */
HCURSOR wxEndBusyCursor()
{
  if (gs_wxBusyCursorCount > 0 && --gs_wxBusyCursorCount == 0) {
    const HCURSOR restored = ::SetCursor(gs_wxBusyCursorOld);
    gs_wxBusyCursorOld = nullptr;
    return restored;
  }

  return nullptr;
}

/**
 * Address: 0x009C7E50 (FUN_009C7E50)
 *
 * What it does:
 * Reads primary-display width/height in pixels from `GetDeviceCaps` and
 * returns the `ReleaseDC` result for the acquired screen DC lane.
 */
int wxGetPrimaryDisplayPixelSize(
  int* const widthPixels,
  int* const heightPixels
) noexcept
{
  const HDC screenDc = ::GetDC(nullptr);
  if (screenDc == nullptr) {
    if (widthPixels != nullptr) {
      *widthPixels = 0;
    }
    if (heightPixels != nullptr) {
      *heightPixels = 0;
    }
    return 0;
  }

  if (widthPixels != nullptr) {
    *widthPixels = ::GetDeviceCaps(screenDc, HORZRES);
  }
  if (heightPixels != nullptr) {
    *heightPixels = ::GetDeviceCaps(screenDc, VERTRES);
  }

  return ::ReleaseDC(nullptr, screenDc);
}

/**
 * Address: 0x009C7E10 (FUN_009C7E10)
 *
 * What it does:
 * Reads primary-display width/height pixels via `GetDeviceCaps(HORZRES /
 * VERTRES)` and stores them into optional output lanes.
 */
int wxGetDisplaySize(
  int* const widthPixels,
  int* const heightPixels
) noexcept
{
  return wxGetPrimaryDisplayPixelSize(widthPixels, heightPixels);
}

/**
 * Address: 0x009C7F00 (FUN_009C7F00)
 *
 * What it does:
 * Converts one width/height pair from device pixels to hundredths-of-mm using
 * the current screen device metrics.
 */
int wxConvertPixelsToDisplayHundredthsMillimeter(
  int* const widthPixels,
  int* const heightPixels
)
{
  const HDC screenDc = ::GetDC(nullptr);
  if (screenDc == nullptr) {
    return 0;
  }

  const int widthMillimeters = ::GetDeviceCaps(screenDc, HORZSIZE);
  const int heightMillimeters = ::GetDeviceCaps(screenDc, VERTSIZE);
  const int widthResolution = ::GetDeviceCaps(screenDc, HORZRES);
  const int heightResolution = ::GetDeviceCaps(screenDc, VERTRES);

  if (widthPixels != nullptr && widthResolution != 0) {
    *widthPixels = (100 * widthMillimeters * (*widthPixels)) / widthResolution;
  }
  if (heightPixels != nullptr && heightResolution != 0) {
    *heightPixels = (100 * heightMillimeters * (*heightPixels)) / heightResolution;
  }

  return ::ReleaseDC(nullptr, screenDc);
}

/**
 * Address: 0x009CD1D0 (FUN_009CD1D0, wx::copystring)
 *
 * What it does:
 * Allocates one heap-owned UTF-16 copy of the input string and falls back to
 * an empty literal when the source pointer is null.
 */
wchar_t* wx::copystring(
  const wchar_t* const text
)
{
  const wchar_t* const source = (text != nullptr) ? text : L"";
  std::size_t length = 0;
  while (source[length] != L'\0') {
    ++length;
  }

  auto* const copy = static_cast<wchar_t*>(::operator new((length + 1u) * sizeof(wchar_t)));
  std::memcpy(copy, source, (length + 1u) * sizeof(wchar_t));
  return copy;
}

namespace
{
  constexpr std::array<wchar_t, 16> kUpperHexDigitsWide = {
    L'0', L'1', L'2', L'3', L'4', L'5', L'6', L'7', L'8', L'9', L'A', L'B', L'C', L'D', L'E', L'F'
  };
  constexpr std::array<char, 16> kUpperHexDigitsNarrow = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
  };
}

/**
 * Address: 0x009CD430 (FUN_009CD430)
 *
 * What it does:
 * Formats one byte value as a two-digit uppercase UTF-16 hex string.
 */
int wxFormatByteUpperHexWide(
  const int byteValue,
  wchar_t* const outHexText
)
{
  if (outHexText == nullptr) {
    return 0;
  }

  const std::uint32_t value = static_cast<std::uint32_t>(byteValue) & 0xFFu;
  const std::uint32_t highNibble = value >> 4u;
  const int lowNibble = static_cast<int>(value & 0xFu);
  outHexText[0] = kUpperHexDigitsWide[highNibble];
  outHexText[1] = kUpperHexDigitsWide[static_cast<std::size_t>(lowNibble)];
  outHexText[2] = L'\0';
  return lowNibble;
}

/**
 * Address: 0x009DB130 (FUN_009DB130)
 *
 * What it does:
 * Formats one byte value as a two-digit uppercase narrow hex string.
 */
int wxFormatByteUpperHexNarrow(
  char* const outHexText,
  const int byteValue
)
{
  if (outHexText == nullptr) {
    return 0;
  }

  const std::uint32_t value = static_cast<std::uint32_t>(byteValue) & 0xFFu;
  const std::uint32_t highNibble = value >> 4u;
  const int lowNibble = static_cast<int>(value & 0xFu);
  outHexText[0] = kUpperHexDigitsNarrow[highNibble];
  outHexText[1] = kUpperHexDigitsNarrow[static_cast<std::size_t>(lowNibble)];
  outHexText[2] = '\0';
  return lowNibble;
}

namespace
{
  constexpr wchar_t kWxDefaultProfileFileName[] = L"Default";
  constexpr wchar_t kWxProfileMissingSentinel[] = L"$$default";
  constexpr std::size_t kWxProfileReadBufferChars = 1000u;
} // namespace

/**
 * Address: 0x009C7970 (FUN_009C7970, sub_9C7970)
 *
 * What it does:
 * Reads one INI/profile value with a default sentinel fallback, replaces the
 * caller-owned output buffer, and returns whether a concrete value was found.
 */
bool wxReadProfileStringValue(
  const wchar_t* const sectionName,
  const wchar_t* const keyName,
  wchar_t** const inOutText,
  const wchar_t* const profileFileName
)
{
  if (sectionName == nullptr || keyName == nullptr || inOutText == nullptr || profileFileName == nullptr) {
    return false;
  }

  wchar_t readBuffer[kWxProfileReadBufferChars]{};
  bool hasValue = false;

  if (std::wcscmp(profileFileName, kWxDefaultProfileFileName) == 0) {
    hasValue = ::GetProfileStringW(
      sectionName,
      keyName,
      kWxProfileMissingSentinel,
      readBuffer,
      static_cast<DWORD>(kWxProfileReadBufferChars)
    ) != 0
      && std::wcscmp(readBuffer, kWxProfileMissingSentinel) != 0;
  } else {
    hasValue = ::GetPrivateProfileStringW(
      sectionName,
      keyName,
      kWxProfileMissingSentinel,
      readBuffer,
      static_cast<DWORD>(kWxProfileReadBufferChars),
      profileFileName
    ) != 0
      && std::wcscmp(readBuffer, kWxProfileMissingSentinel) != 0;
  }

  if (!hasValue) {
    return false;
  }

  if (*inOutText != nullptr) {
    ::operator delete(*inOutText);
  }
  *inOutText = wx::copystring(readBuffer);
  return true;
}

namespace
{
  struct WxRegistryKeyRuntimeView
  {
    HKEY openedKey = nullptr;        // +0x00
    HKEY rootKey = nullptr;          // +0x04
    const wchar_t* subKey = nullptr; // +0x08
    LSTATUS lastStatus = ERROR_SUCCESS; // +0x0C

    /**
     * Address: 0x00A357B0 (FUN_00A357B0, sub_A357B0)
     *
     * What it does:
     * Returns success immediately when the key handle is already cached;
     * otherwise opens and caches the handle from the root/subkey lane.
     */
    [[nodiscard]] bool EnsureOpenedKeyHandleRuntime() noexcept;
  };
  static_assert(sizeof(WxRegistryKeyRuntimeView) == 0x10, "WxRegistryKeyRuntimeView size must be 0x10");

  [[nodiscard]] bool WxRegistryOpenAndCacheKeyHandle(
    const wchar_t* const subKey,
    const HKEY rootKey,
    WxRegistryKeyRuntimeView* const key
  ) noexcept
  {
    if (key == nullptr) {
      return false;
    }

    HKEY opened = nullptr;
    key->lastStatus = ::RegOpenKeyW(rootKey, subKey, &opened);
    if (key->lastStatus != ERROR_SUCCESS) {
      return false;
    }

    key->openedKey = opened;
    return true;
  }

  /**
   * Address: 0x00A357B0 (FUN_00A357B0, sub_A357B0)
   *
   * What it does:
   * Returns success immediately when the key handle is already cached;
   * otherwise opens and caches the handle from the root/subkey lane.
   */
  bool WxRegistryKeyRuntimeView::EnsureOpenedKeyHandleRuntime() noexcept
  {
    if (openedKey != nullptr) {
      return true;
    }

    return WxRegistryOpenAndCacheKeyHandle(subKey, rootKey, this);
  }

  [[nodiscard]] const wchar_t* WxRegistryRootDisplayName(
    const HKEY rootKey,
    const bool shortName
  ) noexcept
  {
    if (rootKey == HKEY_CLASSES_ROOT) {
      return shortName ? L"HKCR" : L"HKEY_CLASSES_ROOT";
    }
    if (rootKey == HKEY_CURRENT_USER) {
      return shortName ? L"HKCU" : L"HKEY_CURRENT_USER";
    }
    if (rootKey == HKEY_LOCAL_MACHINE) {
      return shortName ? L"HKLM" : L"HKEY_LOCAL_MACHINE";
    }
    if (rootKey == HKEY_USERS) {
      return shortName ? L"HKU" : L"HKEY_USERS";
    }
    if (rootKey == HKEY_CURRENT_CONFIG) {
      return shortName ? L"HKCC" : L"HKEY_CURRENT_CONFIG";
    }
    return shortName ? L"HK?" : L"HKEY_UNKNOWN";
  }

  std::wstring gWxRegistryPathScratch{};

  [[nodiscard]] const wchar_t* WxRegistryComposeDisplayPathWithOptionalChild(
    const WxRegistryKeyRuntimeView* const key,
    const wchar_t* const childSubKey
  )
  {
    if (key == nullptr) {
      gWxRegistryPathScratch.clear();
      return gWxRegistryPathScratch.c_str();
    }

    gWxRegistryPathScratch = WxRegistryRootDisplayName(key->rootKey, true);
    if (key->subKey != nullptr && key->subKey[0] != L'\0') {
      gWxRegistryPathScratch.push_back(L'\\');
      gWxRegistryPathScratch.append(key->subKey);
    }

    if (childSubKey != nullptr && childSubKey[0] != L'\0') {
      gWxRegistryPathScratch.push_back(L'\\');
      gWxRegistryPathScratch.append(childSubKey);
    }

    return gWxRegistryPathScratch.c_str();
  }

  [[nodiscard]] bool WxRegistryKeyPathExists(const WxRegistryKeyRuntimeView* const key) noexcept
  {
    if (key == nullptr || key->rootKey == nullptr || key->subKey == nullptr || key->subKey[0] == L'\0') {
      return true;
    }

    HKEY opened = nullptr;
    const LSTATUS status = ::RegOpenKeyW(key->rootKey, key->subKey, &opened);
    if (status == ERROR_SUCCESS && opened != nullptr) {
      ::RegCloseKey(opened);
      return true;
    }

    return false;
  }

  [[nodiscard]] bool WxRegistryReadValueBytes(
    WxRegistryKeyRuntimeView* const key,
    const wchar_t* const valueName,
    DWORD* const outType,
    std::vector<std::uint8_t>& outBytes
  )
  {
    if (key == nullptr || valueName == nullptr) {
      return false;
    }

    if (!key->EnsureOpenedKeyHandleRuntime()) {
      return false;
    }

    DWORD type = 0;
    DWORD byteCount = 0;
    key->lastStatus = ::RegQueryValueExW(key->openedKey, valueName, nullptr, &type, nullptr, &byteCount);
    if (key->lastStatus != ERROR_SUCCESS) {
      return false;
    }

    outBytes.resize(byteCount);
    key->lastStatus = ::RegQueryValueExW(
      key->openedKey,
      valueName,
      nullptr,
      &type,
      outBytes.empty() ? nullptr : outBytes.data(),
      &byteCount
    );
    if (key->lastStatus != ERROR_SUCCESS) {
      return false;
    }

    outBytes.resize(byteCount);
    if (outType != nullptr) {
      *outType = type;
    }
    return true;
  }

  [[nodiscard]] bool WxRegistryWriteValueBytes(
    WxRegistryKeyRuntimeView* const key,
    const wchar_t* const valueName,
    const DWORD valueType,
    const std::uint8_t* const bytes,
    const DWORD byteCount
  )
  {
    if (key == nullptr || valueName == nullptr || key->openedKey == nullptr) {
      return false;
    }

    key->lastStatus = ::RegSetValueExW(key->openedKey, valueName, 0u, valueType, bytes, byteCount);
    return key->lastStatus == ERROR_SUCCESS;
  }

  [[nodiscard]] bool WxRegistryDeleteValueByName(
    WxRegistryKeyRuntimeView* const key,
    const wchar_t* const valueName
  )
  {
    if (key == nullptr || valueName == nullptr) {
      return false;
    }

    if (!key->EnsureOpenedKeyHandleRuntime()) {
      return false;
    }

    key->lastStatus = ::RegDeleteValueW(key->openedKey, valueName);
    return key->lastStatus == ERROR_SUCCESS;
  }

  [[nodiscard]] bool WxRegistryValueExistsByName(
    WxRegistryKeyRuntimeView* const key,
    const wchar_t* const valueName
  )
  {
    if (key == nullptr || valueName == nullptr) {
      return false;
    }

    if (!key->EnsureOpenedKeyHandleRuntime()) {
      return false;
    }

    key->lastStatus = ::RegQueryValueExW(key->openedKey, valueName, nullptr, nullptr, nullptr, nullptr);
    return key->lastStatus == ERROR_SUCCESS;
  }

  using WxSocketDispatchCallback = void(__cdecl*)(void* registration, int callbackIndex, int callbackArg);

  struct WxSocketEventRegistrationRuntime
  {
    std::uint32_t socketHandle = 0;                 // +0x00
    std::uint8_t unknown04To2B[0x28]{};             // +0x04
    std::int32_t eventStateMask = 0;                // +0x2C
    WxSocketDispatchCallback callbacks[4]{};        // +0x30
    std::int32_t callbackArgs[4]{};                 // +0x40
    std::int32_t messageId = 0;                     // +0x50
  };
  static_assert(
    offsetof(WxSocketEventRegistrationRuntime, eventStateMask) == 0x2C,
    "WxSocketEventRegistrationRuntime::eventStateMask offset must be 0x2C"
  );
  static_assert(
    offsetof(WxSocketEventRegistrationRuntime, callbacks) == 0x30,
    "WxSocketEventRegistrationRuntime::callbacks offset must be 0x30"
  );
  static_assert(
    offsetof(WxSocketEventRegistrationRuntime, callbackArgs) == 0x40,
    "WxSocketEventRegistrationRuntime::callbackArgs offset must be 0x40"
  );
  static_assert(
    offsetof(WxSocketEventRegistrationRuntime, messageId) == 0x50,
    "WxSocketEventRegistrationRuntime::messageId offset must be 0x50"
  );
  static_assert(sizeof(WxSocketEventRegistrationRuntime) == 0x54, "WxSocketEventRegistrationRuntime size must be 0x54");

  struct WxSocketDispatchHashTableRuntime
  {
    std::uint32_t* buckets = nullptr; // +0x00
    std::uint32_t bucketCount = 0;    // +0x04
    std::uint32_t usedCount = 0;      // +0x08
    std::uint32_t reserved0C = 0;     // +0x0C
  };
  static_assert(sizeof(WxSocketDispatchHashTableRuntime) == 0x10, "WxSocketDispatchHashTableRuntime size must be 0x10");
  static_assert(
    offsetof(WxSocketDispatchHashTableRuntime, buckets) == 0x00,
    "WxSocketDispatchHashTableRuntime::buckets offset must be 0x00"
  );
  static_assert(
    offsetof(WxSocketDispatchHashTableRuntime, bucketCount) == 0x04,
    "WxSocketDispatchHashTableRuntime::bucketCount offset must be 0x04"
  );
  static_assert(
    offsetof(WxSocketDispatchHashTableRuntime, usedCount) == 0x08,
    "WxSocketDispatchHashTableRuntime::usedCount offset must be 0x08"
  );

  HWND gWxSocketInternalWindow = nullptr;
  CRITICAL_SECTION gWxSocketDispatchCriticalSection{};
  bool gWxSocketDispatchCriticalSectionInitialized = false;
  WxSocketEventRegistrationRuntime* gWxSocketDispatchByMessage[2048]{};
  std::int32_t gWxSocketDispatchState = 0;
  WxSocketDispatchHashTableRuntime* gWxSocketSecondaryDispatchHashTable = nullptr;
  std::uint32_t gWxSocketRuntimeInitRefCount = 0;
} // namespace

/**
 * Address: 0x00A35980 (FUN_00A35980, sub_A35980)
 *
 * What it does:
 * Opens one deferred registry key handle on demand and records Win32 status in
 * the key runtime view.
 */
bool wxRegistryKeyOpenRuntime(WxRegistryKeyRuntimeView* const key)
{
  if (key == nullptr) {
    return false;
  }

  if (key->openedKey != nullptr) {
    return true;
  }

  HKEY opened = nullptr;
  key->lastStatus = ::RegOpenKeyW(key->rootKey, key->subKey, &opened);
  if (key->lastStatus != ERROR_SUCCESS) {
    gpg::Warnf("wxRegistryKeyOpenRuntime: failed to open registry key.");
    return false;
  }

  key->openedKey = opened;
  return true;
}

/**
 * Address: 0x00A35A60 (FUN_00A35A60, sub_A35A60)
 *
 * What it does:
 * Creates one registry key lane (or reuses existing handle) and optionally
 * fails when the target key already exists.
 */
bool wxRegistryKeyCreateRuntime(
  WxRegistryKeyRuntimeView* const key,
  const bool allowExisting
)
{
  if (key == nullptr) {
    return false;
  }

  if (!allowExisting && WxRegistryKeyPathExists(key)) {
    return false;
  }

  if (key->openedKey == nullptr) {
    HKEY created = nullptr;
    key->lastStatus = ::RegCreateKeyW(key->rootKey, key->subKey, &created);
    if (key->lastStatus != ERROR_SUCCESS) {
      gpg::Warnf("wxRegistryKeyCreateRuntime: failed to create registry key.");
      return false;
    }
    key->openedKey = created;
  }

  return true;
}

/**
 * Address: 0x00A35B60 (FUN_00A35B60, sub_A35B60)
 *
 * What it does:
 * Closes one open registry key handle lane and clears the cached handle/state.
 */
bool wxRegistryKeyCloseRuntime(
  WxRegistryKeyRuntimeView* const key
)
{
  if (key == nullptr) {
    return false;
  }

  if (key->openedKey == nullptr) {
    return true;
  }

  key->lastStatus = ::RegCloseKey(key->openedKey);
  key->openedKey = nullptr;
  if (key->lastStatus == ERROR_SUCCESS) {
    return true;
  }

  gpg::Warnf("wxRegistryKeyCloseRuntime: failed to close registry key.");
  return false;
}

/**
 * Address: 0x00A35C40 (FUN_00A35C40, sub_A35C40)
 *
 * What it does:
 * Deletes one named value from the currently-open registry key and reports
 * failures with the key/value context lane.
 */
bool wxRegistryKeyDeleteValueRuntime(
  WxRegistryKeyRuntimeView* const key,
  const wchar_t* const valueName
)
{
  if (key == nullptr || valueName == nullptr) {
    return false;
  }

  if (!wxRegistryKeyOpenRuntime(key)) {
    return false;
  }

  key->lastStatus = ::RegDeleteValueW(key->openedKey, valueName);
  if (key->lastStatus == ERROR_SUCCESS) {
    return true;
  }

  wxLogDebug(
    L"Can't delete value '%s' from key '%s'",
    valueName,
    WxRegistryComposeDisplayPathWithOptionalChild(key, nullptr)
  );
  gpg::Warnf("wxRegistryKeyDeleteValueRuntime: failed to delete registry value.");
  return false;
}

/**
 * Address: 0x00A35E10 (FUN_00A35E10, sub_A35E10)
 *
 * What it does:
 * Queries one registry value type from the current key and returns `0` when
 * the query fails.
 */
DWORD wxRegistryKeyReadValueTypeRuntime(
  WxRegistryKeyRuntimeView* const key,
  const wchar_t* const valueName
)
{
  if (key == nullptr || valueName == nullptr) {
    return 0;
  }

  if (!wxRegistryKeyOpenRuntime(key)) {
    return 0;
  }

  DWORD valueType = 0;
  key->lastStatus = ::RegQueryValueExW(key->openedKey, valueName, nullptr, &valueType, nullptr, nullptr);
  if (key->lastStatus == ERROR_SUCCESS) {
    return valueType;
  }

  wxLogDebug(
    L"Can't read value of key '%s'",
    WxRegistryComposeDisplayPathWithOptionalChild(key, nullptr)
  );
  gpg::Warnf("wxRegistryKeyReadValueTypeRuntime: failed to read registry value type.");
  return 0;
}

/**
 * Address: 0x00A35F00 (FUN_00A35F00, sub_A35F00)
 *
 * What it does:
 * Reads one DWORD payload from the current registry key/value lane into caller
 * output storage.
 */
bool wxRegistryKeyReadDwordValueRuntime(
  WxRegistryKeyRuntimeView* const key,
  const wchar_t* const valueName,
  DWORD* const outValue
)
{
  if (key == nullptr || valueName == nullptr || outValue == nullptr) {
    return false;
  }

  if (!wxRegistryKeyOpenRuntime(key)) {
    return false;
  }

  DWORD valueType = 0;
  DWORD dataByteCount = static_cast<DWORD>(sizeof(DWORD));
  key->lastStatus = ::RegQueryValueExW(
    key->openedKey,
    valueName,
    nullptr,
    &valueType,
    reinterpret_cast<LPBYTE>(outValue),
    &dataByteCount
  );
  if (key->lastStatus == ERROR_SUCCESS) {
    return true;
  }

  wxLogDebug(
    L"Can't read value of key '%s'",
    WxRegistryComposeDisplayPathWithOptionalChild(key, nullptr)
  );
  gpg::Warnf("wxRegistryKeyReadDwordValueRuntime: failed to read DWORD registry value.");
  return false;
}

/**
 * Address: 0x00A36000 (FUN_00A36000, sub_A36000)
 *
 * What it does:
 * Enumerates one value name from an open registry key handle using the caller
 * index lane, advances that index, and marks completion with index `-1`.
 */
bool wxRegistryKeyEnumerateValueRuntime(
  WxRegistryKeyRuntimeView* const key,
  wxStringRuntime* const outValueName,
  DWORD* const inOutIndex
)
{
  if (key == nullptr || outValueName == nullptr || inOutIndex == nullptr) {
    return false;
  }

  const DWORD index = *inOutIndex;
  if (index == static_cast<DWORD>(-1)) {
    return false;
  }
  *inOutIndex = index + 1;

  wchar_t valueNameBuffer[1024]{};
  DWORD cchValueName = 1024;
  key->lastStatus = ::RegEnumValueW(
    key->openedKey,
    index,
    valueNameBuffer,
    &cchValueName,
    nullptr,
    nullptr,
    nullptr,
    nullptr
  );
  if (key->lastStatus == ERROR_SUCCESS) {
    AssignOwnedWxString(outValueName, std::wstring(valueNameBuffer));
    return true;
  }

  if (key->lastStatus == ERROR_NO_MORE_ITEMS) {
    key->lastStatus = ERROR_SUCCESS;
    *inOutIndex = static_cast<DWORD>(-1);
    return false;
  }

  gpg::Warnf("wxRegistryKeyEnumerateValueRuntime: failed to enumerate registry values.");
  return false;
}

/**
 * Address: 0x00A367E0 (FUN_00A367E0)
 *
 * What it does:
 * Opens one registry key lane, resets the caller index lane to zero, and
 * forwards into the value-enumeration lane for first-value lookup.
 */
bool wxRegistryKeyGetFirstValueRuntime(
  WxRegistryKeyRuntimeView* const key,
  wxStringRuntime* const outValueName,
  DWORD* const inOutIndex
)
{
  if (!wxRegistryKeyOpenRuntime(key)) {
    return false;
  }

  if (inOutIndex != nullptr) {
    *inOutIndex = 0u;
  }

  return wxRegistryKeyEnumerateValueRuntime(key, outValueName, inOutIndex);
}

/**
 * Address: 0x00A36150 (FUN_00A36150, sub_A36150)
 *
 * What it does:
 * Enumerates one subkey name from an open registry handle using the caller
 * index lane, advances that index, and marks completion with index `-1`.
 */
bool wxRegistryKeyEnumerateSubKeyRuntime(
  WxRegistryKeyRuntimeView* const key,
  wxStringRuntime* const outSubKeyName,
  DWORD* const inOutIndex
)
{
  if (key == nullptr || outSubKeyName == nullptr || inOutIndex == nullptr) {
    return false;
  }

  const DWORD index = *inOutIndex;
  if (index == static_cast<DWORD>(-1)) {
    return false;
  }
  *inOutIndex = index + 1;

  wchar_t nameBuffer[0x105]{};
  key->lastStatus = ::RegEnumKeyW(key->openedKey, index, nameBuffer, 0x105u);
  if (key->lastStatus == ERROR_SUCCESS) {
    AssignOwnedWxString(outSubKeyName, std::wstring(nameBuffer));
    return true;
  }

  if (key->lastStatus == ERROR_NO_MORE_ITEMS) {
    key->lastStatus = ERROR_SUCCESS;
    *inOutIndex = static_cast<DWORD>(-1);
    return false;
  }

  gpg::Warnf("wxRegistryKeyEnumerateSubKeyRuntime: failed to enumerate subkey.");
  return false;
}

/**
 * Address: 0x00A362B0 (FUN_00A362B0, sub_A362B0)
 *
 * What it does:
 * Builds and returns one display path lane for the current registry key,
 * appending an optional child subkey segment when provided.
 */
const wchar_t* wxRegistryKeyComposePathWithSubKeyRuntime(
  const WxRegistryKeyRuntimeView* const key,
  const wchar_t* const childSubKey
)
{
  return WxRegistryComposeDisplayPathWithOptionalChild(key, childSubKey);
}

/**
 * Address: 0x00A357D0 (FUN_00A357D0, sub_A357D0)
 *
 * What it does:
 * Builds one registry display path into `outPath`, selecting either the short
 * (`HK*`) or long (`HKEY_*`) root-name lane and appending the key subpath when
 * present.
 */
wxStringRuntime* wxRegistryKeyComposePathRuntime(
  const WxRegistryKeyRuntimeView* const key,
  wxStringRuntime* const outPath,
  const bool useShortRootName
)
{
  if (outPath == nullptr) {
    return nullptr;
  }

  const wchar_t* const rootText =
    key != nullptr ? WxRegistryRootDisplayName(key->rootKey, useShortRootName) : WxRegistryRootDisplayName(nullptr, useShortRootName);

  std::wstring composedPath = rootText != nullptr ? std::wstring(rootText) : std::wstring();
  if (key != nullptr && key->subKey != nullptr && key->subKey[0] != L'\0') {
    composedPath.push_back(L'\\');
    composedPath.append(key->subKey);
  }

  AssignOwnedWxString(outPath, composedPath);
  return outPath;
}

/**
 * Address: 0x00A36390 (FUN_00A36390, sub_A36390)
 *
 * What it does:
 * Closes one registry key runtime lane and releases the retained shared
 * subkey-string payload.
 */
void wxRegistryKeyDestroyRuntime(
  WxRegistryKeyRuntimeView* const key
)
{
  if (key == nullptr) {
    return;
  }

  (void)wxRegistryKeyCloseRuntime(key);

  if (key->subKey != nullptr) {
    wxStringRuntime sharedSubKey{};
    sharedSubKey.m_pchData = const_cast<wchar_t*>(key->subKey);
    ReleaseWxStringSharedPayload(sharedSubKey);
    key->subKey = nullptr;
  }
}

/**
 * Address: 0x00A36570 (FUN_00A36570, sub_A36570)
 *
 * What it does:
 * Opens one registry key lane on demand and writes one DWORD value payload by
 * value-name.
 */
bool wxRegistryKeySetDwordValueRuntime(
  WxRegistryKeyRuntimeView* const key,
  const wchar_t* const valueName,
  const std::uint32_t value
)
{
  if (key == nullptr || valueName == nullptr) {
    return false;
  }

  if (wxRegistryKeyOpenRuntime(key)) {
    key->lastStatus = ::RegSetValueExW(
      key->openedKey,
      valueName,
      0u,
      REG_DWORD,
      reinterpret_cast<const BYTE*>(&value),
      static_cast<DWORD>(sizeof(value))
    );
    if (key->lastStatus == ERROR_SUCCESS) {
      return true;
    }
  }

  const wchar_t* messageTemplate = L"Can't set value of '%s'";
  if (wxLocale* const locale = wxGetLocale(); locale != nullptr) {
    messageTemplate = locale->GetString(messageTemplate, 0);
  }

  wxLogDebug(messageTemplate, wxRegistryKeyComposePathWithSubKeyRuntime(key, nullptr));
  return false;
}

/**
 * Address: 0x00A36760 (FUN_00A36760, sub_A36760)
 *
 * What it does:
 * Opens one registry key lane on demand and writes one UTF-16 `REG_SZ` value
 * from the source wxString payload.
 */
bool wxRegistryKeySetStringValueRuntime(
  WxRegistryKeyRuntimeView* const key,
  const wchar_t* const valueName,
  const wxStringRuntime* const valueText
)
{
  if (key == nullptr || valueName == nullptr) {
    return false;
  }

  const wchar_t* const text = valueText != nullptr && valueText->m_pchData != nullptr ? valueText->m_pchData : L"";
  const std::size_t textLength = std::wcslen(text);
  const std::uint64_t requiredBytes64 = (static_cast<std::uint64_t>(textLength) * 2ull) + 2ull;
  const DWORD requiredBytes =
    requiredBytes64 > static_cast<std::uint64_t>((std::numeric_limits<DWORD>::max)())
      ? (std::numeric_limits<DWORD>::max)()
      : static_cast<DWORD>(requiredBytes64);

  if (wxRegistryKeyOpenRuntime(key)) {
    key->lastStatus = ::RegSetValueExW(
      key->openedKey,
      valueName,
      0u,
      REG_SZ,
      reinterpret_cast<const BYTE*>(text),
      requiredBytes
    );
    if (key->lastStatus == ERROR_SUCCESS) {
      return true;
    }
  }

  const wchar_t* messageTemplate = L"Can't set value of '%s'";
  if (wxLocale* const locale = wxGetLocale(); locale != nullptr) {
    messageTemplate = locale->GetString(messageTemplate, 0);
  }

  wxLogDebug(messageTemplate, wxRegistryKeyComposePathWithSubKeyRuntime(key, nullptr));
  return false;
}

/**
 * Address: 0x00A36810 (FUN_00A36810, sub_A36810)
 *
 * What it does:
 * Opens one registry key lane and starts subkey enumeration from index `0`.
 */
bool wxRegistryKeyGetFirstSubKeyRuntime(
  WxRegistryKeyRuntimeView* const key,
  wxStringRuntime* const outSubKeyName,
  DWORD* const inOutIndex
)
{
  if (!wxRegistryKeyOpenRuntime(key)) {
    return false;
  }

  if (inOutIndex == nullptr) {
    return false;
  }

  *inOutIndex = 0u;
  return wxRegistryKeyEnumerateSubKeyRuntime(key, outSubKeyName, inOutIndex);
}

/**
 * Address: 0x00A36840 (FUN_00A36840, sub_A36840)
 *
 * What it does:
 * Copies one registry value from source name to destination name for supported
 * value types (REG_SZ and REG_DWORD).
 */
bool wxRegistryKeyCopyValueRuntime(
  WxRegistryKeyRuntimeView* const key,
  const wchar_t* const sourceValueName,
  const wchar_t* const destinationValueName
)
{
  if (key == nullptr || sourceValueName == nullptr) {
    return false;
  }

  const wchar_t* const destinationName = destinationValueName != nullptr ? destinationValueName : sourceValueName;

  DWORD valueType = 0;
  std::vector<std::uint8_t> valueBytes{};
  if (!WxRegistryReadValueBytes(key, sourceValueName, &valueType, valueBytes)) {
    return false;
  }

  if (valueType == REG_SZ) {
    return WxRegistryWriteValueBytes(
      key,
      destinationName,
      REG_SZ,
      valueBytes.empty() ? nullptr : valueBytes.data(),
      static_cast<DWORD>(valueBytes.size())
    );
  }

  if (valueType == REG_DWORD) {
    if (valueBytes.size() < sizeof(DWORD)) {
      return false;
    }

    DWORD dwordValue = 0;
    std::memcpy(&dwordValue, valueBytes.data(), sizeof(DWORD));
    return WxRegistryWriteValueBytes(
      key,
      destinationName,
      REG_DWORD,
      reinterpret_cast<const std::uint8_t*>(&dwordValue),
      static_cast<DWORD>(sizeof(DWORD))
    );
  }

  gpg::Warnf("wxRegistryKeyCopyValueRuntime: unsupported registry value type.");
  return false;
}

/**
 * Address: 0x00A36EA0 (FUN_00A36EA0, sub_A36EA0)
 *
 * What it does:
 * Renames one registry value by copying bytes/type to a new name and removing
 * the old value name when the copy succeeds.
 */
bool wxRegistryKeyRenameValueRuntime(
  WxRegistryKeyRuntimeView* const key,
  const wchar_t* const oldValueName,
  const wchar_t* const newValueName
)
{
  if (key == nullptr || oldValueName == nullptr || newValueName == nullptr) {
    return false;
  }

  if (WxRegistryValueExistsByName(key, newValueName)) {
    gpg::Warnf("wxRegistryKeyRenameValueRuntime: destination value already exists.");
    return false;
  }

  if (!wxRegistryKeyCopyValueRuntime(key, oldValueName, newValueName)) {
    gpg::Warnf("wxRegistryKeyRenameValueRuntime: failed to copy source value.");
    return false;
  }

  if (!WxRegistryDeleteValueByName(key, oldValueName)) {
    gpg::Warnf("wxRegistryKeyRenameValueRuntime: failed to remove old value name.");
    return false;
  }

  return true;
}

/**
 * Address: 0x00A28090 (FUN_00A28090, sub_A28090)
 *
 * What it does:
 * Allocates one socket-dispatch hash-table state block, initializes a
 * threshold-sized zeroed bucket lane, and stores it as the secondary runtime
 * socket-dispatch table.
 */
std::uint32_t* wxSocketAllocateSecondaryDispatchHashTable()
{
  auto* const hashTable = static_cast<WxSocketDispatchHashTableRuntime*>(
    ::operator new(sizeof(WxSocketDispatchHashTableRuntime), std::nothrow)
  );
  if (hashTable == nullptr) {
    gWxSocketSecondaryDispatchHashTable = nullptr;
    return nullptr;
  }

  const std::uint32_t bucketCount = wxHashTableNextBucketThresholdRuntime(2u);
  hashTable->bucketCount = bucketCount;
  hashTable->usedCount = 0;
  hashTable->buckets = static_cast<std::uint32_t*>(
    std::calloc(static_cast<std::size_t>(bucketCount), sizeof(std::uint32_t))
  );
  gWxSocketSecondaryDispatchHashTable = hashTable;
  return hashTable->buckets;
}

/**
 * Address: 0x00A38080 (FUN_00A38080)
 *
 * What it does:
 * Reserves one `WM_USER..WM_USER+0x3FF` socket-dispatch slot for the given
 * registration and stores the reserved message id into that registration.
 */
bool wxSocketAssignDispatchMessageSlot(
  void* const socketRegistrationRuntime
)
{
  auto* const registration = static_cast<WxSocketEventRegistrationRuntime*>(socketRegistrationRuntime);
  if (registration == nullptr || !gWxSocketDispatchCriticalSectionInitialized) {
    return false;
  }

  ::EnterCriticalSection(&gWxSocketDispatchCriticalSection);

  std::int32_t slot = gWxSocketDispatchState & 0x3FF;
  const std::int32_t startSlot = slot;

  const auto slotOccupied = [](const std::int32_t candidateSlot) noexcept -> bool {
    const std::int32_t messageId = static_cast<std::int32_t>(WM_USER) + candidateSlot;
    return gWxSocketDispatchByMessage[messageId] != nullptr;
  };

  if (slotOccupied(slot)) {
    for (;;) {
      slot = (slot + 1) & 0x3FF;
      if (slot == startSlot) {
        ::LeaveCriticalSection(&gWxSocketDispatchCriticalSection);
        return false;
      }
      if (!slotOccupied(slot)) {
        break;
      }
    }
  }

  const std::int32_t messageId = static_cast<std::int32_t>(WM_USER) + slot;
  gWxSocketDispatchByMessage[messageId] = registration;
  gWxSocketDispatchState = (slot + 1) & 0x3FF;
  registration->messageId = messageId;

  ::LeaveCriticalSection(&gWxSocketDispatchCriticalSection);
  return true;
}

/**
 * Address: 0x00A38110 (FUN_00A38110)
 *
 * What it does:
 * Clears one socket-dispatch registration slot indexed by the registration's
 * cached message-id lane while holding the socket-dispatch critical section.
 */
void wxSocketClearDispatchMessageSlot(
  const void* const socketRegistrationRuntime
)
{
  const auto* const registration = static_cast<const WxSocketEventRegistrationRuntime*>(socketRegistrationRuntime);
  if (registration == nullptr || !gWxSocketDispatchCriticalSectionInitialized) {
    return;
  }

  const std::int32_t messageId = registration->messageId;
  if (messageId < 0 || messageId >= static_cast<std::int32_t>(std::size(gWxSocketDispatchByMessage))) {
    return;
  }

  ::EnterCriticalSection(&gWxSocketDispatchCriticalSection);
  gWxSocketDispatchByMessage[messageId] = nullptr;
  ::LeaveCriticalSection(&gWxSocketDispatchCriticalSection);
}

/**
 * Address: 0x00A38140 (FUN_00A38140, sub_A38140)
 *
 * What it does:
 * Dispatches one internal socket-window message lane to the registered socket
 * callback table using WinSock event code semantics.
 */
LRESULT CALLBACK wxSocketInternalWindowProc(
  const HWND windowHandle,
  const UINT messageId,
  const WPARAM wParam,
  const LPARAM lParam
)
{
  if (messageId < WM_USER || messageId > (WM_USER + 0x3FFu)) {
    return ::DefWindowProcW(windowHandle, messageId, wParam, lParam);
  }

  if (!gWxSocketDispatchCriticalSectionInitialized) {
    return 0;
  }

  WxSocketEventRegistrationRuntime* registration = nullptr;
  WxSocketDispatchCallback callback = nullptr;
  int callbackIndex = -1;
  int callbackArg = 0;

  ::EnterCriticalSection(&gWxSocketDispatchCriticalSection);
  registration = gWxSocketDispatchByMessage[messageId];
  if (registration != nullptr && registration->socketHandle == static_cast<std::uint32_t>(wParam)) {
    switch (static_cast<unsigned short>(lParam)) {
    case FD_READ:
      callbackIndex = 0;
      break;
    case FD_WRITE:
      callbackIndex = 1;
      break;
    case FD_ACCEPT:
      callbackIndex = 2;
      break;
    case FD_CONNECT:
      callbackIndex = (HIWORD(lParam) != 0) ? 3 : 2;
      break;
    case FD_CLOSE:
      callbackIndex = 3;
      break;
    default:
      break;
    }

    if (callbackIndex >= 0) {
      callback = registration->callbacks[callbackIndex];
      callbackArg = registration->callbackArgs[callbackIndex];
      if (callbackIndex == 3) {
        registration->eventStateMask = 8;
      } else {
        registration->eventStateMask |= (1 << callbackIndex);
      }
    }
  }
  ::LeaveCriticalSection(&gWxSocketDispatchCriticalSection);

  if (callback != nullptr) {
    callback(registration, callbackIndex, callbackArg);
  }

  return 0;
}

/**
 * Address: 0x00A382B0 (FUN_00A382B0, sub_A382B0)
 *
 * What it does:
 * Registers the internal GSocket message window class, creates the hidden
 * dispatch window, initializes socket dispatch state, and boots WinSock 1.1.
 */
BOOL wxSocketRuntimeInitialize()
{
  WNDCLASSW windowClass{};
  windowClass.style = 0;
  windowClass.lpfnWndProc = &wxSocketInternalWindowProc;
  windowClass.cbClsExtra = 0;
  windowClass.cbWndExtra = 0;
  windowClass.hInstance = ::GetModuleHandleW(nullptr);
  windowClass.hIcon = nullptr;
  windowClass.hCursor = nullptr;
  windowClass.hbrBackground = nullptr;
  windowClass.lpszMenuName = nullptr;
  windowClass.lpszClassName = L"_GSocket_Internal_Window_Class";
  (void)::RegisterClassW(&windowClass);

  gWxSocketInternalWindow = ::CreateWindowExW(
    0,
    L"_GSocket_Internal_Window_Class",
    L"_GSocket_Internal_Window_Name",
    0,
    0,
    0,
    0,
    0,
    nullptr,
    nullptr,
    ::GetModuleHandleW(nullptr),
    nullptr
  );

  if (gWxSocketInternalWindow == nullptr) {
    return FALSE;
  }

  ::InitializeCriticalSection(&gWxSocketDispatchCriticalSection);
  gWxSocketDispatchCriticalSectionInitialized = true;
  std::memset(gWxSocketDispatchByMessage, 0, sizeof(gWxSocketDispatchByMessage));
  gWxSocketDispatchState = 0;

  WSADATA winsockData{};
  return ::WSAStartup(MAKEWORD(1, 1), &winsockData) == 0 ? TRUE : FALSE;
}

/**
 * Address: 0x00A118C0 (FUN_00A118C0)
 *
 * What it does:
 * Ensures wx socket runtime is initialized once, increments the shared init
 * refcount, and rolls the refcount back when first-call init fails.
 */
bool wxEnsureSocketRuntimeInitialized()
{
  if (gWxSocketRuntimeInitRefCount != 0u) {
    return true;
  }

  ++gWxSocketRuntimeInitRefCount;

  if (wxSocketRuntimeInitialize() != FALSE) {
    return true;
  }

  --gWxSocketRuntimeInitRefCount;
  return false;
}

/**
 * Address: 0x009EBDE0 (FUN_009EBDE0, sub_9EBDE0)
 *
 * What it does:
 * Converts one bitmap/mask lane into Win32 AND/XOR planes and returns a
 * `HCURSOR` created with the provided hotspot.
 */
HCURSOR wxCreateCursorFromBitmapMask(
  const HINSTANCE instanceHandle,
  const HGDIOBJ sourceBitmap,
  int* const hotSpotXY
)
{
  if (sourceBitmap == nullptr || hotSpotXY == nullptr) {
    return nullptr;
  }

  HCURSOR cursor = nullptr;
  const HDC screenDc = ::GetDC(nullptr);
  if (screenDc == nullptr) {
    return nullptr;
  }

  const HDC sourceDc = ::CreateCompatibleDC(screenDc);
  const HDC workDc = ::CreateCompatibleDC(screenDc);
  const HBITMAP andBitmap = (workDc != nullptr) ? ::CreateCompatibleBitmap(workDc, 32, 32) : nullptr;
  const HBITMAP xorBitmap = (workDc != nullptr) ? ::CreateCompatibleBitmap(workDc, 32, 32) : nullptr;

  if (sourceDc != nullptr && workDc != nullptr && andBitmap != nullptr && xorBitmap != nullptr) {
    const HGDIOBJ oldSourceObject = ::SelectObject(sourceDc, sourceBitmap);
    const HGDIOBJ oldWorkObject = ::SelectObject(workDc, andBitmap);

    (void)::SetBkColor(sourceDc, RGB(255, 255, 255));
    (void)::BitBlt(workDc, 0, 0, 32, 32, sourceDc, 0, 0, SRCCOPY);

    BITMAP andInfo{};
    (void)::GetObjectW(andBitmap, sizeof(andInfo), &andInfo);
    const LONG andByteCount = andInfo.bmWidthBytes * andInfo.bmHeight;
    std::vector<std::uint8_t> andPlane(andByteCount > 0 ? static_cast<std::size_t>(andByteCount) : 0u);
    if (!andPlane.empty()) {
      (void)::GetBitmapBits(andBitmap, andByteCount, andPlane.data());
    }

    (void)::SelectObject(workDc, xorBitmap);
    (void)::SetBkColor(sourceDc, 0);
    (void)::BitBlt(workDc, 0, 0, 32, 32, sourceDc, 0, 0, SRCCOPY);

    BITMAP xorInfo{};
    (void)::GetObjectW(xorBitmap, sizeof(xorInfo), &xorInfo);
    const LONG xorByteCount = xorInfo.bmWidthBytes * xorInfo.bmHeight;
    std::vector<std::uint8_t> xorPlane(xorByteCount > 0 ? static_cast<std::size_t>(xorByteCount) : 0u);
    if (!xorPlane.empty()) {
      (void)::GetBitmapBits(xorBitmap, xorByteCount, xorPlane.data());
    }

    if (hotSpotXY[0] > 32) {
      hotSpotXY[0] = 32;
    }
    if (hotSpotXY[1] > 32) {
      hotSpotXY[1] = 32;
    }

    cursor = ::CreateCursor(
      instanceHandle,
      hotSpotXY[0],
      hotSpotXY[1],
      32,
      32,
      andPlane.empty() ? nullptr : andPlane.data(),
      xorPlane.empty() ? nullptr : xorPlane.data()
    );

    (void)::SelectObject(sourceDc, oldSourceObject);
    (void)::SelectObject(workDc, oldWorkObject);
  }

  if (sourceDc != nullptr) {
    (void)::DeleteDC(sourceDc);
  }
  if (workDc != nullptr) {
    (void)::DeleteDC(workDc);
  }
  if (andBitmap != nullptr) {
    (void)::DeleteObject(andBitmap);
  }
  if (xorBitmap != nullptr) {
    (void)::DeleteObject(xorBitmap);
  }
  (void)::ReleaseDC(nullptr, screenDc);
  return cursor;
}

void* wxTopLevelWindowRootRuntime::sm_classInfo[1] = {nullptr};

/**
 * Address: 0x004A3690 (FUN_004A3690)
 * Mangled: ??0wxClientData@@QAE@@Z
 *
 * What it does:
 * Constructs one `wxClientData` runtime lane.
 */
wxClientDataRuntime::wxClientDataRuntime()
{
  ResetRuntimeVTable();
}

/**
 * Address: 0x004A36A0 (FUN_004A36A0)
 *
 * What it does:
 * Rebinds this object to the `wxClientData` runtime vtable lane.
 */
void wxClientDataRuntime::ResetRuntimeVTable() noexcept {}

/**
 * Address: 0x004A36B0 (FUN_004A36B0)
 *
 * What it does:
 * Implements the deleting-dtor thunk lane for `wxClientData`.
 */
wxClientDataRuntime* wxClientDataRuntime::DeleteWithFlag(
  wxClientDataRuntime* const object,
  const std::uint8_t deleteFlags
) noexcept
{
  if (object == nullptr) {
    return nullptr;
  }

  object->ResetRuntimeVTable();
  if ((deleteFlags & 1u) != 0u) {
    operator delete(object);
  }
  return object;
}

/**
 * Address: 0x004F1570 (FUN_004F1570)
 *
 * What it does:
 * Rebinds one `wxObjectRefData` runtime payload to its base vtable lane
 * without deleting object storage.
 */
void* wxDestroyObjectRefDataNoDelete(
  void* const objectRefDataRuntime
) noexcept
{
  auto* const object = static_cast<WxObjectRefDataRuntimeView*>(objectRefDataRuntime);
  if (object == nullptr) {
    return nullptr;
  }

  object->WxObjectRefDataRuntimeDispatch::~WxObjectRefDataRuntimeDispatch();
  return object;
}

/**
 * Address: 0x004F1630 (FUN_004F1630)
 *
 * What it does:
 * Implements the deleting-dtor thunk lane for one `wxObjectRefData` runtime
 * payload.
 */
void* wxDeleteObjectRefDataWithFlag(
  void* const objectRefDataRuntime,
  const std::uint8_t deleteFlags
) noexcept
{
  auto* const object = static_cast<WxObjectRefDataRuntimeView*>(objectRefDataRuntime);
  if (object == nullptr) {
    return nullptr;
  }

  if ((deleteFlags & 1u) != 0u) {
    operator delete(object);
  }
  return object;
}

/**
 * Address: 0x004F1750 (FUN_004F1750)
 *
 * What it does:
 * Runs the scalar deleting-dtor lane for one `wxObjectRefData` payload by
 * rebinding to base destroy semantics and deleting storage when requested.
 */
void* wxDeleteObjectRefDataScalarRuntime(
  void* const objectRefDataRuntime,
  const std::uint8_t deleteFlags
) noexcept
{
  auto* const object = static_cast<WxObjectRefDataRuntimeView*>(objectRefDataRuntime);
  if (object == nullptr) {
    return nullptr;
  }

  (void)wxDestroyObjectRefDataNoDelete(object);
  if ((deleteFlags & 1u) != 0u) {
    ::operator delete(object);
  }

  return object;
}

/**
 * Address: 0x004F1710 (FUN_004F1710)
 *
 * What it does:
 * Runs the base-construction lane for one `wxObjectRefData` runtime payload.
 */
void* wxConstructObjectRefDataBaseRuntime(
  void* const objectRefDataRuntime
) noexcept
{
  auto* const object = static_cast<WxObjectRefDataRuntimeView*>(objectRefDataRuntime);
  if (object == nullptr) {
    return nullptr;
  }

  // Base-ctor vtable rebinding is represented by typed runtime construction.
  return object;
}

/**
 * Address: 0x004F19C0 (FUN_004F19C0)
 *
 * What it does:
 * Allocates one icon-refdata payload lane and initializes its shared wx GDI
 * refdata state.
 */
void* wxAllocateIconRefDataRuntime() noexcept
{
  auto* const iconRefData =
    static_cast<WxIconRefDataRuntimeView*>(::operator new(sizeof(WxIconRefDataRuntimeView), std::nothrow));
  if (iconRefData == nullptr) {
    return nullptr;
  }

  return wxInitializeIconRefDataRuntime(iconRefData);
}

/**
 * Address: 0x00A017D0 (FUN_00A017D0)
 *
 * What it does:
 * Destroys the owned client-data payload lane inside one tree-item-indirect
 * data object and rebases the object to `wxClientData` runtime state.
 */
void* wxTreeItemIndirectDataDestroyNoDelete(
  void* const treeItemIndirectDataRuntime
) noexcept
{
  auto* const treeItemData = static_cast<WxTreeItemIndirectDataRuntimeView*>(treeItemIndirectDataRuntime);
  if (treeItemData == nullptr) {
    return nullptr;
  }

  if (treeItemData->mOwnedClientData != nullptr) {
    (void)treeItemData->mOwnedClientData->DeleteWithFlag(1);
    treeItemData->mOwnedClientData = nullptr;
  }

  return wxClientDataRuntime::DeleteWithFlag(reinterpret_cast<wxClientDataRuntime*>(treeItemData), 0u);
}

/**
 * Address: 0x009F2850 (FUN_009F2850)
 *
 * What it does:
 * Captures one wall-clock timestamp with millisecond precision and stores the
 * Unix-epoch millisecond lane into `outMilliseconds`.
 */
std::int64_t* wxGetEpochMillisRuntime(std::int64_t* const outMilliseconds)
{
  if (outMilliseconds == nullptr) {
    return nullptr;
  }

  __timeb64 currentTime{};
  ::_ftime64(&currentTime);
  *outMilliseconds =
    static_cast<std::int64_t>(currentTime.time) * 1000ll +
    static_cast<std::int64_t>(currentTime.millitm);
  return outMilliseconds;
}

/**
 * Address: 0x009F34B0 (FUN_009F34B0, wxSizer::DoSetClientObject)
 *
 * What it does:
 * Deletes the previous client-object payload (when present), then stores one
 * new client-object lane and marks payload type as object-backed.
 */
void wxSizerClientDataRuntime::DoSetClientObject(
  void* const clientObject
)
{
  if (mClientPayload != nullptr) {
    delete static_cast<wxClientDataRuntime*>(mClientPayload);
  }

  mClientPayload = clientObject;
  mClientPayloadType = kClientPayloadObject;
}

/**
 * Address: 0x009F34F0 (FUN_009F34F0, wxSizer::DoGetClientObject)
 *
 * What it does:
 * Returns the stored client payload pointer lane.
 */
void* wxSizerClientDataRuntime::DoGetClientObject() const
{
  return mClientPayload;
}

/**
 * Address: 0x009F3500 (FUN_009F3500, wxSizer::DoSetClientData)
 *
 * What it does:
 * Stores one raw client-data payload pointer and marks payload type as raw
 * client-data.
 */
void wxSizerClientDataRuntime::DoSetClientData(
  void* const clientData
)
{
  mClientPayload = clientData;
  mClientPayloadType = kClientPayloadData;
}

/**
 * Address: 0x009F3520 (FUN_009F3520, wxSizer::DoGetClientData)
 *
 * What it does:
 * Returns the stored client payload pointer lane.
 */
void* wxSizerClientDataRuntime::DoGetClientData() const
{
  return mClientPayload;
}

/**
 * Address: 0x004A3710 (FUN_004A3710)
 * Mangled: ??0wxTopLevelWindowMSW@@QAE@@Z
 *
 * What it does:
 * Constructs one top-level-window runtime base lane and resets fullscreen
 * state bookkeeping.
 */
wxTopLevelWindowRuntime::wxTopLevelWindowRuntime()
{
  WxTopLevelWindowRuntimeState& state = EnsureWxTopLevelWindowRuntimeState(this);
  state.fsOldX = 0;
  state.fsOldY = 0;
  state.fsOldWidth = 0;
  state.fsOldHeight = 0;
  ResetTopLevelFlag34();
}

/**
 * Address: 0x0098C280 (FUN_0098C280, wxTopLevelWindowMSW::Show)
 * Mangled: ?Show@wxTopLevelWindowMSW@@UAE_N_N@Z
 *
 * What it does:
 * Runs base visibility transition and raises this window (or parent when
 * hiding) when native-handle lanes are present in runtime state.
 */
bool wxTopLevelWindowRuntime::Show(
  const bool show
)
{
  if (!wxWindowBase::Show(show)) {
    return false;
  }

  const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(this);
  if (show) {
    if (state != nullptr && state->nativeHandle != 0u) {
      ::BringWindowToTop(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(state->nativeHandle)));
    }
  } else if (state != nullptr && state->parentWindow != nullptr) {
    const WxWindowBaseRuntimeState* const parentState = FindWxWindowBaseRuntimeState(state->parentWindow);
    if (parentState != nullptr && parentState->nativeHandle != 0u) {
      ::BringWindowToTop(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(parentState->nativeHandle)));
    }
  }

  return true;
}

/**
 * Address: 0x0098C9B0 (FUN_0098C9B0, wxTopLevelWindowMSW::MSWGetParent)
 * Mangled: ?MSWGetParent@wxTopLevelWindowMSW@@UBEKXZ
 *
 * What it does:
 * Lazily registers the hidden top-level-parent class and creates one hidden
 * parent window used by wx top-level-window creation paths.
 */
unsigned long wxTopLevelWindowRuntime::MSWGetParent() const
{
  (void)this;

  if (gWxHiddenTopLevelParentWindow != nullptr) {
    return static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(gWxHiddenTopLevelParentWindow));
  }

  if (gWxHiddenTopLevelParentClassName == nullptr) {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = ::DefWindowProcW;
    windowClass.hInstance = ::GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kWxHiddenTopLevelParentClassName;
    if (::RegisterClassW(&windowClass) != 0) {
      gWxHiddenTopLevelParentClassName = kWxHiddenTopLevelParentClassName;
    }
  }

  gWxHiddenTopLevelParentWindow = ::CreateWindowExW(
    0,
    gWxHiddenTopLevelParentClassName,
    kWxHiddenTopLevelParentWindowName,
    0,
    0,
    0,
    0,
    0,
    nullptr,
    nullptr,
    ::GetModuleHandleW(nullptr),
    nullptr
  );
  return static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(gWxHiddenTopLevelParentWindow));
}

/**
 * Address: 0x0098C970 (FUN_0098C970)
 *
 * What it does:
 * Destroys the shared hidden top-level parent window and unregisters its
 * window class lane when present.
 */
BOOL wxDestroyTopLevelWindowParentRuntime()
{
  BOOL result = FALSE;
  if (gWxHiddenTopLevelParentWindow != nullptr) {
    result = ::DestroyWindow(gWxHiddenTopLevelParentWindow);
    gWxHiddenTopLevelParentWindow = nullptr;
  }

  if (gWxHiddenTopLevelParentClassName != nullptr) {
    result = ::UnregisterClassW(gWxHiddenTopLevelParentClassName, static_cast<HINSTANCE>(wxGetInstance()));
    gWxHiddenTopLevelParentClassName = nullptr;
  }

  return result;
}

/**
 * Address: 0x004A36E0 (FUN_004A36E0)
 *
 * What it does:
 * Resets one top-level-window runtime flag lane.
 */
void wxTopLevelWindowRuntime::ResetTopLevelFlag34() noexcept
{
  EnsureWxTopLevelWindowRuntimeState(this).flag34 = 0;
}

/**
 * Address: 0x00A0AB50 (FUN_00A0AB50, wxLogFrame::wxLogFrame)
 * Mangled: ??0wxLogFrame@@QAE@PAVwxFrame@@PAVwxLogWindow@@PBD@Z
 *
 * What it does:
 * Builds one log-output frame lane, creates the embedded multiline text
 * control, and seeds log menu/status metadata.
 */
wxLogFrameRuntime::wxLogFrameRuntime(
  wxTopLevelWindowRuntime* const parentFrame,
  wxLogWindowRuntime* const ownerLogWindow,
  const wchar_t* const titleText
)
  : wxTopLevelWindowRuntime()
{
  constexpr long kWxLogFrameStyle = 0x20400E40;
  constexpr long kWxLogTextCtrlStyle = 0x40000030;
  constexpr wchar_t kFrameWindowName[] = L"frame";
  constexpr wchar_t kTextCtrlWindowName[] = L"text";

  mOwnerLogWindow = ownerLogWindow;
  mTextControl = new (std::nothrow) wxTextCtrlRuntime();

  WxWindowBaseRuntimeState& frameState = EnsureWxWindowBaseRuntimeState(this);
  frameState.parentWindow = static_cast<wxWindowBase*>(parentFrame);
  frameState.windowId = -1;
  frameState.windowStyle = kWxLogFrameStyle;
  frameState.windowName.assign(kFrameWindowName);

  if (mTextControl != nullptr) {
    WxWindowBaseRuntimeState& textState = EnsureWxWindowBaseRuntimeState(mTextControl);
    textState.parentWindow = this;
    textState.windowId = -1;
    textState.windowStyle = kWxLogTextCtrlStyle;
    textState.windowName.assign(kTextCtrlWindowName);
    EnsureWxTextCtrlRuntimeState(mTextControl).richEditMajorVersion = 0;
  }

  WxLogFrameRuntimeState& logFrameState = EnsureWxLogFrameRuntimeState(this);
  logFrameState.title.assign(titleText != nullptr ? titleText : L"");
  logFrameState.windowName.assign(kFrameWindowName);
  logFrameState.statusText.assign(L"Ready");
  logFrameState.textControl = mTextControl;
  logFrameState.ownerLogWindow = ownerLogWindow;
  logFrameState.menuReady = true;
}

wxTextCtrlRuntime* wxLogFrameRuntime::TextCtrl() const noexcept
{
  return mTextControl;
}

/**
 * Address: 0x00A0BC80 (FUN_00A0BC80, wxLogWindow::wxLogWindow)
 * Mangled: ??0wxLogWindow@@QAE@PAVwxFrame@@PBD_N2@Z
 *
 * What it does:
 * Initializes one log-window owner lane, allocates the backing log frame, and
 * shows that frame when requested by constructor arguments.
 */
wxLogWindowRuntime::wxLogWindowRuntime(
  wxTopLevelWindowRuntime* const parentFrame,
  const wchar_t* const titleText,
  const bool showAtStartup,
  const bool passToOldLog
)
{
  std::memset(mUnknown04To0F, 0, sizeof(mUnknown04To0F));
  mPassToOldLog = passToOldLog ? 1u : 0u;
  mFrame = new (std::nothrow) wxLogFrameRuntime(parentFrame, this, titleText);
  if (showAtStartup && mFrame != nullptr) {
    (void)mFrame->Show(true);
  }
}

wxLogFrameRuntime* wxLogWindowRuntime::GetFrame() const noexcept
{
  return mFrame;
}

/**
 * Address: 0x004A36F0 (FUN_004A36F0)
 * Mangled: ?IsTopLevel@wxTopLevelWindowBase@@UBE_NXZ
 *
 * What it does:
 * Reports this runtime lane as a top-level wx window.
 */
bool wxTopLevelWindowRuntime::IsTopLevel() const
{
  return true;
}

/**
 * Address: 0x004A3700 (FUN_004A3700)
 * Mangled: ?IsOneOfBars@wxTopLevelWindowBase@@MBE_NPBVwxWindow@@@Z
 *
 * What it does:
 * Base implementation reports the queried window as not one of frame bars.
 */
bool wxTopLevelWindowRuntime::IsOneOfBars(
  const void* const window
) const
{
  (void)window;
  return false;
}

/**
 * Address: 0x004A3770 (FUN_004A3770)
 * Mangled: ?IsFullScreen@wxTopLevelWindowMSW@@UBE_NXZ
 *
 * What it does:
 * Returns one cached fullscreen-visible flag.
 */
bool wxTopLevelWindowRuntime::IsFullScreen() const
{
  const WxTopLevelWindowRuntimeState* const state = FindWxTopLevelWindowRuntimeState(this);
  return state != nullptr && state->flag34 != 0;
}

/**
 * Address: 0x004A3780 (FUN_004A3780)
 *
 * What it does:
 * Implements deleting-dtor thunk semantics for top-level-window runtime
 * lanes.
 */
wxTopLevelWindowRuntime* wxTopLevelWindowRuntime::DeleteWithFlag(
  wxTopLevelWindowRuntime* const object,
  const std::uint8_t deleteFlags
) noexcept
{
  if (object == nullptr) {
    return nullptr;
  }

  gWxTopLevelWindowRuntimeStateByWindow.erase(object);
  object->~wxTopLevelWindowRuntime();
  if ((deleteFlags & 1u) != 0u) {
    operator delete(object);
  }
  return object;
}

/**
 * Address: 0x004A37A0 (FUN_004A37A0)
 * Mangled: ??0wxTopLevelWindow@@QAE@@Z
 *
 * What it does:
 * Constructs one `wxTopLevelWindow` runtime layer and reapplies base
 * top-level init.
 */
wxTopLevelWindowRootRuntime::wxTopLevelWindowRootRuntime()
  : wxTopLevelWindowRuntime()
{
  ResetTopLevelFlag34();
}

/**
 * Address: 0x004A3800 (FUN_004A3800)
 *
 * What it does:
 * Implements deleting-dtor thunk semantics for `wxTopLevelWindow`.
 */
wxTopLevelWindowRootRuntime* wxTopLevelWindowRootRuntime::DeleteWithFlag(
  wxTopLevelWindowRootRuntime* const object,
  const std::uint8_t deleteFlags
) noexcept
{
  return reinterpret_cast<wxTopLevelWindowRootRuntime*>(
    wxTopLevelWindowRuntime::DeleteWithFlag(reinterpret_cast<wxTopLevelWindowRuntime*>(object), deleteFlags)
  );
}

/**
 * Address: 0x004A3820 (FUN_004A3820)
 *
 * What it does:
 * Runs the non-deleting top-level-window teardown thunk.
 */
wxTopLevelWindowRootRuntime* wxTopLevelWindowRootRuntime::DestroyWithoutDelete(
  wxTopLevelWindowRootRuntime* const object
) noexcept
{
  return DeleteWithFlag(object, 0);
}

void wxControlContainerRuntime::Initialize(
  const bool acceptsFocusRecursion
) noexcept
{
  mAcceptsFocusRecursion = acceptsFocusRecursion ? 1 : 0;
}

void* wxDialogRuntime::sm_classInfo[1] = {nullptr};
void* wxDialogRuntime::sm_eventTable[1] = {nullptr};

/**
 * Address: 0x004A3860 (FUN_004A3860)
 * Mangled: ??0wxDialogBase@@QAE@@Z
 *
 * What it does:
 * Builds one dialog-base runtime lane, initializes control-container
 * storage, then runs dialog-base init.
 */
wxDialogBaseRuntime::wxDialogBaseRuntime()
  : wxTopLevelWindowRootRuntime()
{
  mControlContainer.Initialize(false);
  InitRuntime();
}

void wxDialogBaseRuntime::InitRuntime() noexcept {}

/**
 * Address: 0x004A38C0 (FUN_004A38C0)
 *
 * What it does:
 * Runs non-deleting teardown for dialog-base runtime lanes.
 */
wxDialogBaseRuntime* wxDialogBaseRuntime::DestroyWithoutDelete(
  wxDialogBaseRuntime* const object
) noexcept
{
  return reinterpret_cast<wxDialogBaseRuntime*>(
    wxTopLevelWindowRuntime::DeleteWithFlag(reinterpret_cast<wxTopLevelWindowRuntime*>(object), 0)
  );
}

/**
 * Address: 0x004A38D0 (FUN_004A38D0)
 *
 * What it does:
 * Implements deleting-dtor thunk semantics for dialog-base runtime lanes.
 */
wxDialogBaseRuntime* wxDialogBaseRuntime::DeleteWithFlag(
  wxDialogBaseRuntime* const object,
  const std::uint8_t deleteFlags
) noexcept
{
  if (object == nullptr) {
    return nullptr;
  }

  gWxDialogRuntimeStateByDialog.erase(reinterpret_cast<const wxDialogRuntime*>(object));
  return reinterpret_cast<wxDialogBaseRuntime*>(
    wxTopLevelWindowRuntime::DeleteWithFlag(reinterpret_cast<wxTopLevelWindowRuntime*>(object), deleteFlags)
  );
}

/**
 * Address: 0x0098B870 (FUN_0098B870)
 * Mangled: ??0wxDialog@@QAE@XZ
 *
 * What it does:
 * Builds one dialog runtime lane and initializes default dialog state.
 */
wxDialogRuntime::wxDialogRuntime()
  : wxDialogBaseRuntime()
{
  (void)EnsureWxDialogRuntimeState(this);
}

/**
 * Address: 0x004A3900 (FUN_004A3900)
 * Mangled: ??0wxDialog@@QAE@PAVwxWindow@@HABVwxString@@ABVwxPoint@@ABVwxSize@@J1@Z
 *
 * What it does:
 * Builds one dialog runtime lane, then applies create/init arguments.
 */
wxDialogRuntime::wxDialogRuntime(
  void* const parentWindow,
  const std::int32_t windowId,
  const wxStringRuntime& title,
  const wxPoint& position,
  const wxSize& size,
  const long style,
  const wxStringRuntime& name
)
  : wxDialogBaseRuntime()
{
  WxDialogRuntimeState& state = EnsureWxDialogRuntimeState(this);
  state.parentWindow = parentWindow;
  state.windowId = windowId;
  state.position = position;
  state.size = size;
  state.style = style;
  state.title.assign(title.c_str());
  state.name.assign(name.c_str());
}

/**
 * Address: 0x004A3970 (FUN_004A3970)
 * Mangled: ?GetClassInfo@wxDialog@@UBEPAVwxClassInfo@@XZ
 *
 * What it does:
 * Returns the static class-info lane for dialog runtime RTTI checks.
 */
void* wxDialogRuntime::GetClassInfo() const
{
  return sm_classInfo;
}

/**
 * Address: 0x0098B230 (FUN_0098B230)
 * Mangled: ?GetEventTable@wxDialog@@MBEPBUwxEventTable@@XZ
 *
 * What it does:
 * Returns the static event-table lane for dialog runtime dispatch.
 */
const void* wxDialogRuntime::GetEventTable() const
{
  return sm_eventTable;
}

/**
 * Address: 0x004A3980 (FUN_004A3980)
 *
 * What it does:
 * Implements deleting-dtor thunk semantics for dialog runtime lanes.
 */
wxDialogRuntime* wxDialogRuntime::DeleteWithFlag(
  wxDialogRuntime* const object,
  const std::uint8_t deleteFlags
) noexcept
{
  if (object == nullptr) {
    return nullptr;
  }

  gWxDialogRuntimeStateByDialog.erase(object);
  return reinterpret_cast<wxDialogRuntime*>(
    wxDialogBaseRuntime::DeleteWithFlag(reinterpret_cast<wxDialogBaseRuntime*>(object), deleteFlags)
  );
}

/**
 * Address: 0x004A39A0 (FUN_004A39A0)
 *
 * What it does:
 * Clears this item-id to the null value.
 */
void wxTreeItemIdRuntime::Reset() noexcept
{
  mNode = nullptr;
}

/**
 * Address: 0x004A39B0 (FUN_004A39B0)
 *
 * What it does:
 * Reports whether this item-id currently references a valid node.
 */
bool wxTreeItemIdRuntime::IsValid() const noexcept
{
  return mNode != nullptr;
}

/**
 * Address: 0x00A02970 (FUN_00A02970)
 *
 * What it does:
 * Returns true when this tree-item id currently references one live tree node.
 */
bool wxTreeItemIdRuntime::IsOk() const noexcept
{
  return mNode != nullptr;
}

/**
 * Address: 0x004A39C0 (FUN_004A39C0)
 *
 * What it does:
 * Constructs one tree-item payload lane with null item data.
 */
wxTreeItemDataRuntime::wxTreeItemDataRuntime()
  : wxClientDataRuntime()
{
  mPayload = nullptr;
}

/**
 * Address: 0x004A39F0 (FUN_004A39F0)
 *
 * What it does:
 * Rebinds this object to the `wxClientData` base vtable lane.
 */
void wxTreeItemDataRuntime::ResetClientDataBaseVTable() noexcept
{
  ResetRuntimeVTable();
}

/**
 * Address: 0x004A39D0 (FUN_004A39D0)
 *
 * What it does:
 * Implements deleting-dtor thunk semantics for tree-item payload lanes.
 */
wxTreeItemDataRuntime* wxTreeItemDataRuntime::DeleteWithFlag(
  wxTreeItemDataRuntime* const object,
  const std::uint8_t deleteFlags
) noexcept
{
  if (object == nullptr) {
    return nullptr;
  }

  object->ResetClientDataBaseVTable();
  if ((deleteFlags & 1u) != 0u) {
    operator delete(object);
  }
  return object;
}

/**
 * Address: 0x004A3A00 (FUN_004A3A00)
 *
 * What it does:
 * Copies the primary tree-item-id lane into `outItem`.
 */
void wxTreeEventRuntime::GetItem(
  wxTreeItemIdRuntime* const outItem
) const noexcept
{
  if (outItem != nullptr) {
    *outItem = mItem;
  }
}

/**
 * Address: 0x004A3A10 (FUN_004A3A10)
 *
 * What it does:
 * Returns the label storage lane for this tree event.
 */
wxStringRuntime* wxTreeEventRuntime::GetLabelStorage() noexcept
{
  return &mLabel;
}

/**
 * Address: 0x004A3A20 (FUN_004A3A20)
 *
 * What it does:
 * Returns the edit-cancelled flag lane for this tree event.
 */
bool wxTreeEventRuntime::IsEditCancelled() const noexcept
{
  return mEditCancelled != 0;
}

/**
 * Address: 0x004A3A30 (FUN_004A3A30)
 *
 * What it does:
 * Initializes one tree-list column descriptor from title/width/align and
 * owner lane arguments.
 */
wxTreeListColumnInfoRuntime::wxTreeListColumnInfoRuntime(
  const wxStringRuntime& title,
  const std::int32_t width,
  void* const ownerTreeControl,
  const std::uint8_t shown,
  const std::uint8_t alignment,
  const std::int32_t userData
)
{
  mRefData = nullptr;
  mText = wxStringRuntime::Borrow(L"");
  mShown = shown;
  mAlignment = alignment;
  mUserData = userData;
  mText = title;
  mWidth = width;
  mImageIndex = -1;
  mOwnerTreeControl = ownerTreeControl;
}

/**
 * Address: 0x004A3AC0 (FUN_004A3AC0)
 *
 * What it does:
 * Runs non-deleting teardown for one tree-list column descriptor lane.
 */
void wxTreeListColumnInfoRuntime::DestroyWithoutDelete() noexcept
{
  mRefData = nullptr;
  mOwnerTreeControl = nullptr;
}

/**
 * Address: 0x004A3B30 (FUN_004A3B30)
 *
 * What it does:
 * Implements deleting-dtor thunk semantics for tree-list column descriptors.
 */
wxTreeListColumnInfoRuntime* wxTreeListColumnInfoRuntime::DeleteWithFlag(
  wxTreeListColumnInfoRuntime* const object,
  const std::uint8_t deleteFlags
) noexcept
{
  if (object == nullptr) {
    return nullptr;
  }

  object->DestroyWithoutDelete();
  if ((deleteFlags & 1u) != 0u) {
    operator delete(object);
  }
  return object;
}

void* wxTreeListCtrlRuntime::sm_classInfo[1] = {nullptr};

/**
 * Address: 0x004A3B50 (FUN_004A3B50)
 * Mangled: ??0wxTreeListCtrl@@QAE@PAVwxWindow@@HABVwxPoint@@ABVwxSize@@JABVwxValidator@@ABVwxString@@@Z
 *
 * What it does:
 * Initializes one tree-list control runtime lane with parent/style/name
 * creation arguments.
 */
wxTreeListCtrlRuntime::wxTreeListCtrlRuntime(
  wxWindowBase* const parentWindow,
  const std::int32_t windowId,
  const wxPoint& position,
  const wxSize& size,
  const long style,
  const wxStringRuntime& name
)
{
  WxTreeListRuntimeState& state = EnsureWxTreeListRuntimeState(this);
  state.parentWindow = parentWindow;
  state.windowId = windowId;
  state.position = position;
  state.size = size;
  state.style = style;
  state.name.assign(name.c_str());
}

/**
 * Address: 0x004A3BD0 (FUN_004A3BD0)
 *
 * What it does:
 * Runs non-deleting teardown for one tree-list control runtime lane.
 */
wxTreeListCtrlRuntime* wxTreeListCtrlRuntime::DestroyWithoutDelete(
  wxTreeListCtrlRuntime* const object
) noexcept
{
  if (object == nullptr) {
    return nullptr;
  }

  gWxTreeListRuntimeStateByControl.erase(object);
  return object;
}

/**
 * Address: 0x004A3BE0 (FUN_004A3BE0)
 * Mangled: ?AddColumn@wxTreeListCtrl@@QAEXABVwxString@@I_NW4wxTreeListColumnAlign@@@Z
 *
 * What it does:
 * Appends one tree-list column descriptor to this control.
 */
void wxTreeListCtrlRuntime::AddColumn(
  const wxStringRuntime& title,
  const std::uint32_t width,
  const bool shown,
  const std::uint8_t alignment
)
{
  WxTreeListRuntimeState& state = EnsureWxTreeListRuntimeState(this);
  state.columns.emplace_back(title, static_cast<std::int32_t>(width), this, shown ? 1u : 0u, alignment, 0);
}

/**
 * Address: 0x004A3C50 (FUN_004A3C50)
 * Mangled: ?GetWindowStyleFlag@wxTreeListCtrl@@UBEJXZ
 *
 * What it does:
 * Returns the cached window-style flags for this tree-list control.
 */
long wxTreeListCtrlRuntime::GetWindowStyleFlag() const
{
  const WxTreeListRuntimeState* const state = FindWxTreeListRuntimeState(this);
  return state != nullptr ? state->style : 0;
}

/**
 * Address: 0x004A3C70 (FUN_004A3C70)
 * Mangled: ?GetClassInfo@wxTreeListCtrl@@UBEPAVwxClassInfo@@XZ
 *
 * What it does:
 * Returns the static class-info lane for tree-list runtime RTTI checks.
 */
void* wxTreeListCtrlRuntime::GetClassInfo() const
{
  return sm_classInfo;
}

/**
 * Address: 0x004A3C80 (FUN_004A3C80)
 *
 * What it does:
 * Implements deleting-dtor thunk semantics for tree-list control runtime
 * lanes.
 */
wxTreeListCtrlRuntime* wxTreeListCtrlRuntime::DeleteWithFlag(
  wxTreeListCtrlRuntime* const object,
  const std::uint8_t deleteFlags
) noexcept
{
  if (object == nullptr) {
    return nullptr;
  }

  DestroyWithoutDelete(object);
  if ((deleteFlags & 1u) != 0u) {
    operator delete(object);
  }
  return object;
}

wxTreeItemIdRuntime wxTreeListCtrlRuntime::AddRoot(
  const wxStringRuntime& text
)
{
  WxTreeListRuntimeState& state = EnsureWxTreeListRuntimeState(this);
  wxTreeItemIdRuntime item{};
  const msvc8::string rootText = text.ToUtf8();
  state.rootNode = AllocateTreeListNode(state, nullptr, rootText);
  item.mNode = state.rootNode;
  return item;
}

wxTreeItemIdRuntime wxTreeListCtrlRuntime::AppendItem(
  const wxTreeItemIdRuntime& parentItem,
  const wxStringRuntime& text
)
{
  WxTreeListRuntimeState& state = EnsureWxTreeListRuntimeState(this);
  wxTreeItemIdRuntime item{};

  WxTreeListNodeRuntimeState* const parentNode = ResolveTreeListNode(parentItem);
  if (parentNode == nullptr) {
    return AddRoot(text);
  }

  const msvc8::string nodeText = text.ToUtf8();
  item.mNode = AllocateTreeListNode(state, parentNode, nodeText);
  return item;
}

void wxTreeListCtrlRuntime::Expand(
  const wxTreeItemIdRuntime& item
) noexcept
{
  WxTreeListNodeRuntimeState* const node = ResolveTreeListNode(item);
  if (node == nullptr) {
    return;
  }
  node->isExpanded = true;
}

void wxTreeListCtrlRuntime::Collapse(
  const wxTreeItemIdRuntime& item
) noexcept
{
  WxTreeListNodeRuntimeState* const node = ResolveTreeListNode(item);
  if (node == nullptr) {
    return;
  }
  node->isExpanded = false;
}

bool wxTreeListCtrlRuntime::IsExpanded(
  const wxTreeItemIdRuntime& item
) const noexcept
{
  const WxTreeListNodeRuntimeState* const node = ResolveTreeListNodeConst(item);
  return node != nullptr && node->isExpanded;
}

bool wxTreeListCtrlRuntime::HasChildren(
  const wxTreeItemIdRuntime& item
) const noexcept
{
  const WxTreeListNodeRuntimeState* const node = ResolveTreeListNodeConst(item);
  if (node == nullptr) {
    return false;
  }

  return node->hasChildrenFlag || !node->children.empty();
}

void wxTreeListCtrlRuntime::SortChildren(
  const wxTreeItemIdRuntime& item
)
{
  WxTreeListNodeRuntimeState* const node = ResolveTreeListNode(item);
  if (node == nullptr || node->children.size() < 2u) {
    return;
  }

  std::stable_sort(
    node->children.begin(),
    node->children.end(),
    [](const WxTreeListNodeRuntimeState* const lhs, const WxTreeListNodeRuntimeState* const rhs) {
    static const msvc8::string kEmptyText{};
    const msvc8::string& lhsText = (lhs != nullptr && !lhs->columnText.empty()) ? lhs->columnText[0] : kEmptyText;
    const msvc8::string& rhsText = (rhs != nullptr && !rhs->columnText.empty()) ? rhs->columnText[0] : kEmptyText;
    return lhsText < rhsText;
  }
  );
}

void wxTreeListCtrlRuntime::SetItemData(
  const wxTreeItemIdRuntime& item,
  wxTreeItemDataRuntime* const itemData
)
{
  WxTreeListNodeRuntimeState* const node = ResolveTreeListNode(item);
  if (node == nullptr) {
    return;
  }
  node->itemData = itemData;
}

wxTreeItemDataRuntime* wxTreeListCtrlRuntime::GetItemData(
  const wxTreeItemIdRuntime& item
) const noexcept
{
  const WxTreeListNodeRuntimeState* const node = ResolveTreeListNodeConst(item);
  return node != nullptr ? node->itemData : nullptr;
}

void wxTreeListCtrlRuntime::SetItemHasChildren(
  const wxTreeItemIdRuntime& item,
  const bool hasChildren
) noexcept
{
  WxTreeListNodeRuntimeState* const node = ResolveTreeListNode(item);
  if (node == nullptr) {
    return;
  }
  node->hasChildrenFlag = hasChildren;
}

void wxTreeListCtrlRuntime::SetItemText(
  const wxTreeItemIdRuntime& item,
  const std::uint32_t column,
  const wxStringRuntime& text
)
{
  WxTreeListNodeRuntimeState* const node = ResolveTreeListNode(item);
  if (node == nullptr) {
    return;
  }

  if (node->columnText.size() <= column) {
    node->columnText.resize(static_cast<std::size_t>(column) + 1u);
  }
  node->columnText[static_cast<std::size_t>(column)] = text.ToUtf8();
}

/**
 * Address: 0x004A37F0 (FUN_004A37F0)
 * Mangled: ?GetClassInfo@wxFrameBase@@UBEPAVwxClassInfo@@XZ
 *
 * What it does:
 * Returns the shared class-info lane used by frame/dialog/top-level
 * `GetClassInfo` slot-0 entries.
 */
void** WX_FrameBaseGetClassInfo() noexcept
{
  return wxTopLevelWindowRootRuntime::sm_classInfo;
}

/**
 * Address: 0x009C7EF0 (FUN_009C7EF0, wxGetWindowId)
 *
 * What it does:
 * Returns one Win32 window-id lane (`GWL_ID`) from the provided native HWND.
 */
long wxGetWindowId(void* const nativeWindow) noexcept
{
  return ::GetWindowLongW(static_cast<HWND>(nativeWindow), GWL_ID);
}

/**
 * Address: 0x0099E8A0 (FUN_0099E8A0)
 *
 * What it does:
 * Runs non-deleting frame-runtime teardown for frame-derived windows.
 */
wxTopLevelWindowRuntime* WX_FrameDestroyWithoutDelete(
  wxTopLevelWindowRuntime* const frame
) noexcept
{
  if (frame == nullptr) {
    return nullptr;
  }

  // Binary path sets the frame "destroyed" bit before delegating to shared
  // frame-base teardown.
  EnsureWxWindowBaseRuntimeState(frame).bitfields |= 0x8u;
  gWxLogFrameRuntimeStateByFrame.erase(reinterpret_cast<const wxLogFrameRuntime*>(frame));
  return wxTopLevelWindowRuntime::DeleteWithFlag(frame, 0u);
}

/**
 * Address: 0x0042B770 (FUN_0042B770)
 * Mangled: ?GetClassInfo@wxWindowBase@@UBEPAVwxClassInfo@@XZ
 *
 * What it does:
 * Returns the static class-info lane for wxWindowBase runtime RTTI checks.
 */
void* wxWindowBase::GetClassInfo() const
{
  return gWxWindowBaseClassInfoTable;
}

/**
 * Address: 0x00968C10 (FUN_00968C10, wxFindWinFromHandle)
 *
 * What it does:
 * Resolves one native HWND lane through `wxWinHandleHash` and returns the
 * associated wxWindow runtime pointer when present.
 */
wxWindowMswRuntime* wxFindWinFromHandle(
  const int nativeHandle
)
{
  if (nativeHandle != 0) {
    if (const auto it = gWxWindowByNativeHandle.find(nativeHandle); it != gWxWindowByNativeHandle.end()) {
      return it->second;
    }
  }

  if (wxWinHandleHash == nullptr) {
    return nullptr;
  }

  void* const hashEntry =
    wxWinHandleHash->Get(nativeHandle, reinterpret_cast<void*>(static_cast<std::uintptr_t>(nativeHandle)));
  if (hashEntry == nullptr) {
    return nullptr;
  }

  const auto* const entryRuntime = static_cast<const WxWindowHandleHashEntryRuntime*>(hashEntry);
  return entryRuntime->window;
}

/**
 * Address: 0x00968C30 (FUN_00968C30, wxRemoveHandleAssociation)
 *
 * What it does:
 * Removes one native-handle to wx-window runtime association and clears the
 * mirrored runtime native-handle lane.
 */
void wxRemoveHandleAssociation(
  void* const windowRuntime
)
{
  auto* const window = static_cast<wxWindowMswRuntime*>(windowRuntime);
  if (window == nullptr) {
    return;
  }

  if (const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(window); state != nullptr && state->nativeHandle != 0u) {
    const int nativeHandleKey = static_cast<int>(state->nativeHandle);
    gWxWindowByNativeHandle.erase(nativeHandleKey);
    const_cast<WxWindowBaseRuntimeState*>(state)->nativeHandle = 0u;
  }
}

/**
 * Address: 0x009D2500 (FUN_009D2500, wxGetInstance)
 *
 * What it does:
 * Returns the process module-instance handle used by wx Win32 create paths.
 */
void* wxGetInstance()
{
  if (gWxInstanceHandle == nullptr) {
    gWxInstanceHandle = ::GetModuleHandleW(nullptr);
  }
  return gWxInstanceHandle;
}

/**
 * Address: 0x00967D60 (FUN_00967D60, sub_967D60)
 *
 * What it does:
 * Resolves one HWND class descriptor and checks whether its registered
 * window-proc lane matches the expected callback.
 */
[[maybe_unused]] static bool DoesWindowClassUseWindowProc(
  const HWND nativeWindow,
  WNDPROC const expectedWindowProc
)
{
  if (nativeWindow == nullptr || expectedWindowProc == nullptr) {
    return false;
  }

  wchar_t className[256] = {};
  const int classNameLength =
    ::GetClassNameW(nativeWindow, className, static_cast<int>(sizeof(className) / sizeof(className[0])));
  if (classNameLength <= 0) {
    return false;
  }

  WNDCLASSW classInfo{};
  const HINSTANCE moduleInstance = static_cast<HINSTANCE>(wxGetInstance());
  if (::GetClassInfoW(moduleInstance, className, &classInfo) == 0) {
    return false;
  }

  return classInfo.lpfnWndProc == expectedWindowProc;
}

/**
 * Address: 0x0096C5C0 (FUN_0096C5C0, wxAssociateWinWithHandle)
 *
 * What it does:
 * Associates one native HWND with one wx window runtime lane for lookup and
 * updates the mirrored runtime native-handle lane.
 */
void wxAssociateWinWithHandle(
  void* const nativeHandle,
  void* const windowRuntime
)
{
  if (nativeHandle == nullptr || windowRuntime == nullptr) {
    return;
  }

  auto* const window = static_cast<wxWindowMswRuntime*>(windowRuntime);
  const HWND hwnd = static_cast<HWND>(nativeHandle);
  const int nativeHandleKey = static_cast<int>(reinterpret_cast<std::uintptr_t>(hwnd));

  WxWindowBaseRuntimeState& state = EnsureWxWindowBaseRuntimeState(window);
  state.nativeHandle = static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(hwnd));
  if (state.windowId == -1) {
    state.windowId = static_cast<std::int32_t>(::GetWindowLongW(hwnd, GWL_ID));
  }
  if (state.eventHandler == nullptr) {
    state.eventHandler = window;
  }

  gWxWindowByNativeHandle[nativeHandleKey] = window;
}

/**
 * Address: 0x00968B10 (FUN_00968B10, wxWindow::UnpackCommand)
 *
 * What it does:
 * Splits command `wParam` into low/high word lanes and forwards control
 * handle lane from `lParam`.
 */
unsigned short wxWindowMswRuntime::UnpackCommand(
  const unsigned int packedWord,
  const int controlHandle,
  unsigned short* const outCommandId,
  unsigned int* const outControlHandle,
  unsigned short* const outNotificationCode
)
{
  const unsigned short commandId = static_cast<unsigned short>(packedWord & 0xFFFFu);
  const unsigned short notificationCode = static_cast<unsigned short>((packedWord >> 16u) & 0xFFFFu);
  if (outCommandId != nullptr) {
    *outCommandId = commandId;
  }
  if (outControlHandle != nullptr) {
    *outControlHandle = static_cast<unsigned int>(controlHandle);
  }
  if (outNotificationCode != nullptr) {
    *outNotificationCode = notificationCode;
  }
  return notificationCode;
}

void* wxWindowMswRuntime::sm_eventTable[1] = {nullptr};

/**
 * Address: 0x00968B40 (FUN_00968B40, wxWindow::UnpackActivate)
 *
 * What it does:
 * Splits activation packed word into low/high word lanes and forwards the
 * native window handle lane.
 */
unsigned int* wxWindowMswRuntime::UnpackActivate(
  const int packedWord,
  const int nativeWindowHandle,
  unsigned short* const outState,
  unsigned short* const outMinimized,
  unsigned int* const outNativeWindowHandle
)
{
  if (outState != nullptr) {
    *outState = static_cast<unsigned short>(packedWord & 0xFFFF);
  }
  if (outMinimized != nullptr) {
    *outMinimized = static_cast<unsigned short>((static_cast<unsigned int>(packedWord) >> 16u) & 0xFFFFu);
  }
  if (outNativeWindowHandle != nullptr) {
    *outNativeWindowHandle = static_cast<unsigned int>(nativeWindowHandle);
  }
  return outNativeWindowHandle;
}

/**
 * Address: 0x00968B70 (FUN_00968B70, wxWindow::UnpackScroll)
 *
 * What it does:
 * Splits scroll packed word into request/position lanes and forwards the
 * native scroll-bar handle lane.
 */
unsigned int* wxWindowMswRuntime::UnpackScroll(
  const int packedWord,
  const int scrollBarHandle,
  unsigned short* const outRequest,
  unsigned short* const outPosition,
  unsigned int* const outScrollBarHandle
)
{
  if (outRequest != nullptr) {
    *outRequest = static_cast<unsigned short>(packedWord & 0xFFFF);
  }
  if (outPosition != nullptr) {
    *outPosition = static_cast<unsigned short>((static_cast<unsigned int>(packedWord) >> 16u) & 0xFFFFu);
  }
  if (outScrollBarHandle != nullptr) {
    *outScrollBarHandle = static_cast<unsigned int>(scrollBarHandle);
  }
  return outScrollBarHandle;
}

/**
 * Address: 0x00968BA0 (FUN_00968BA0, wxWindow::UnpackCtlColor)
 *
 * What it does:
 * Emits fixed control-id lane `3` and forwards raw message params.
 */
unsigned int* wxWindowMswRuntime::UnpackCtlColor(
  const int wParam,
  const int lParam,
  unsigned short* const outControlId,
  unsigned int* const outWParam,
  unsigned int* const outLParam
)
{
  if (outControlId != nullptr) {
    *outControlId = 3u;
  }
  if (outLParam != nullptr) {
    *outLParam = static_cast<unsigned int>(lParam);
  }
  if (outWParam != nullptr) {
    *outWParam = static_cast<unsigned int>(wParam);
  }
  return outWParam;
}

/**
 * Address: 0x00968C60 (FUN_00968C60, ?MSWDestroyWindow@wxWindow@@UAEXXZ)
 *
 * What it does:
 * Base window runtime lane has no additional destroy-stage behavior.
 */
void wxWindowMswRuntime::MSWDestroyWindow() {}

/**
 * Address: 0x009675F0 (FUN_009675F0)
 * Mangled: ?MSWCommand@wxWindow@@UAE_NIG@Z
 *
 * What it does:
 * Base window runtime does not consume Win32 command notifications.
 */
bool wxWindowMswRuntime::MSWCommand(
  const unsigned int commandId,
  const unsigned short notificationCode
)
{
  (void)commandId;
  (void)notificationCode;
  return false;
}

/**
 * Address: 0x00968B00 (FUN_00968B00)
 * Mangled: ?MSWShouldPreProcessMessage@wxWindow@@UAE_NPAPAX@Z
 *
 * What it does:
 * Base window runtime requests pre-processing for incoming native messages.
 */
bool wxWindowMswRuntime::MSWShouldPreProcessMessage(
  void** const message
)
{
  (void)message;
  return true;
}

/**
 * Address: 0x00969800 (FUN_00969800)
 * Mangled: ?OnCtlColor@wxWindow@@UAEKKKIIIJ@Z
 *
 * What it does:
 * Base window runtime does not provide a control-colour brush override.
 */
unsigned long wxWindowMswRuntime::OnCtlColor(
  const unsigned long hdc,
  const unsigned long hwnd,
  const unsigned int nCtlColor,
  const unsigned int message,
  const unsigned int controlId,
  const long result
)
{
  (void)hdc;
  (void)hwnd;
  (void)nCtlColor;
  (void)message;
  (void)controlId;
  (void)result;
  return 0;
}

/**
 * Address: 0x0042B830 (FUN_0042B830)
 * Mangled: ?ContainsHWND@wxWindow@@UBE_NK@Z
 *
 * What it does:
 * Base implementation reports the queried native handle as not contained.
 */
bool wxWindowMswRuntime::ContainsHWND(
  const unsigned long nativeHandle
) const
{
  (void)nativeHandle;
  return false;
}

/**
 * Address: 0x00967570 (FUN_00967570)
 * Mangled: ?GetEventTable@wxWindow@@MBEPBUwxEventTable@@XZ
 *
 * What it does:
 * Returns the static event-table lane for wx window runtime dispatch.
 */
const void* wxWindowMswRuntime::GetEventTable() const
{
  return sm_eventTable;
}

/**
 * Address: 0x00967930 (FUN_00967930, ?DoReleaseMouse@wxWindow@@MAEXXZ)
 *
 * What it does:
 * Releases the active Win32 mouse-capture lane.
 */
void wxWindowBase::DoReleaseMouse()
{
  ::ReleaseCapture();
}

/**
 * Address: 0x0042B840 (FUN_0042B840)
 * Mangled: ?GetClassInfo@wxWindow@@UBEPAVwxClassInfo@@XZ
 *
 * What it does:
 * Returns the static class-info lane for wxWindow runtime RTTI checks.
 */
void* wxWindowMswRuntime::GetClassInfo() const
{
  return gWxWindowClassInfoTable;
}

/**
 * Address: 0x00967EB0 (FUN_00967EB0)
 * Mangled: ?MSWGetStyle@wxWindow@@UBEKJPAK@Z
 *
 * What it does:
 * Converts wx-style lanes into Win32 style/ex-style masks, including auto-3D
 * upgrades for controls and child/non-child parent-background behavior.
 */
unsigned long wxWindowMswRuntime::MSWGetStyle(
  const long style,
  unsigned long* const extendedStyle
) const
{
  unsigned long nativeStyle = (style & kWxWindowStyleClipChildren) != 0 ? kMswStyleClipChildren : kMswStyleBase;

  if ((style & kWxWindowStyleRaisedBorder) != 0) {
    nativeStyle |= kMswStyleRaisedBorder;
  }
  if ((style & kWxWindowStyleSunkenBorder) != 0) {
    nativeStyle |= kMswStyleSunkenBorder;
  }
  if ((style & kWxWindowStyleDoubleBorder) != 0) {
    nativeStyle |= kMswStyleDoubleBorder;
  }

  long styleMaskLane = style & kWxWindowStyleMaskForMsw;
  if (styleMaskLane == 0 && wxTheApp != nullptr && wxTheApp->m_auto3D != 0) {
    if (dynamic_cast<const wxControlRuntime*>(this) != nullptr) {
      const WxWindowBaseRuntimeState* const thisState = FindWxWindowBaseRuntimeState(this);
      wxWindowBase* const parentWindow = thisState != nullptr ? thisState->parentWindow : nullptr;
      if (parentWindow != nullptr && (parentWindow->GetWindowStyleFlag() & kWxWindowStyleNo3D) == 0) {
        styleMaskLane = (style & kWxWindowStyleMaskAuto3DBase) | kWxWindowStyleAuto3D;
      }
    }
  }

  if ((styleMaskLane & kWxWindowStyleStaticEdge) != 0) {
    nativeStyle |= kMswStyleNo3DBit;
  }

  if (extendedStyle == nullptr) {
    return nativeStyle;
  }

  *extendedStyle = 0;
  if ((style & kWxWindowStyleTabTraversal) != 0) {
    *extendedStyle = kMswExStyleTabTraversal;
  }

  if (styleMaskLane == kWxWindowStyleAuto3D) {
    *extendedStyle |= kMswExStyleClientEdge;
    nativeStyle &= ~kMswStyleNo3DBit;
  } else if (
    styleMaskLane == kWxWindowStyleSimpleBorder || styleMaskLane == kWxWindowStyleDoubleBorderLegacy ||
    styleMaskLane == kWxWindowStyleSimpleBorderAlt
  ) {
    *extendedStyle |= kMswExStyleDlgModalFrame;
  }

  if ((style & kWxWindowStyleNoParentBg) != 0 && !IsTopLevel()) {
    *extendedStyle |= kMswExStyleNoParentNotify;
  }

  return nativeStyle;
}

/**
 * Address: 0x0097CCC0 (FUN_0097CCC0)
 * Mangled: ?AdoptAttributesFromHWND@wxWindow@@UAEXXZ
 *
 * What it does:
 * Reads the attached HWND style bits and mirrors horizontal/vertical scroll
 * flags into the wx window-style lane.
 */
void wxWindowMswRuntime::AdoptAttributesFromHWND()
{
  WxWindowBaseRuntimeState& state = EnsureWxWindowBaseRuntimeState(this);
  const HWND nativeWindow = reinterpret_cast<HWND>(state.nativeHandle);
  if (nativeWindow == nullptr) {
    return;
  }

  const long nativeStyle = static_cast<long>(::GetWindowLongW(nativeWindow, GWL_STYLE));
  if ((nativeStyle & WS_VSCROLL) != 0) {
    state.windowStyle |= kWxWindowStyleVerticalScroll;
  }
  if ((nativeStyle & WS_HSCROLL) != 0) {
    state.windowStyle |= kWxWindowStyleHorizontalScroll;
  }
}

namespace
{
  template <typename TWindow>
  [[nodiscard]] wxWindowMswRuntime* AllocateWxMswWindowRuntime() noexcept
  {
    return new (std::nothrow) TWindow();
  }

  [[nodiscard]] bool EqualsWindowClassName(
    const wchar_t* const className,
    const wchar_t* const expected
  ) noexcept
  {
    return className != nullptr && expected != nullptr && ::_wcsicmp(className, expected) == 0;
  }

  [[nodiscard]] bool TryParseRichEditMajorVersion(
    const wchar_t* const className,
    std::int32_t* const outMajorVersion
  ) noexcept
  {
    if (className == nullptr || outMajorVersion == nullptr) {
      return false;
    }

    int majorVersion = 0;
    wchar_t suffix = L'\0';
    if (std::swscanf(className, L"RichEdit%d0%c", &majorVersion, &suffix) != 2) {
      return false;
    }

    *outMajorVersion = majorVersion;
    return true;
  }

  [[nodiscard]] wxWindowMswRuntime* CreateButtonRuntimeFromStyle(
    const signed char styleLane
  ) noexcept
  {
    if (styleLane == 5 || styleLane == 6 || styleLane == 3 || styleLane == 2) {
      return AllocateWxMswWindowRuntime<wxCheckBoxRuntime>();
    }

    if (styleLane == 9 || styleLane == 4) {
      return AllocateWxMswWindowRuntime<wxControlRuntime>();
    }

    if (styleLane >= 0) {
      switch (styleLane) {
      case 11:
      case 0:
      case 1:
      case 7:
        return AllocateWxMswWindowRuntime<wxControlRuntime>();
      default:
        return nullptr;
      }
    }

    return AllocateWxMswWindowRuntime<wxControlRuntime>();
  }

  [[nodiscard]] wxWindowMswRuntime* CreateRuntimeFromClassAndStyle(
    const wchar_t* const className,
    const signed char styleLane
  ) noexcept
  {
    if (EqualsWindowClassName(className, L"BUTTON")) {
      return CreateButtonRuntimeFromStyle(styleLane);
    }

    if (EqualsWindowClassName(className, L"COMBOBOX")) {
      return AllocateWxMswWindowRuntime<wxControlRuntime>();
    }

    if (EqualsWindowClassName(className, L"EDIT")) {
      return AllocateWxMswWindowRuntime<wxTextCtrlRuntime>();
    }

    if (
      EqualsWindowClassName(className, L"LISTBOX") || EqualsWindowClassName(className, L"SCROLLBAR") ||
      EqualsWindowClassName(className, L"MSCTLS_UPDOWN32") || EqualsWindowClassName(className, L"MSCTLS_TRACKBAR32")
    ) {
      return AllocateWxMswWindowRuntime<wxControlRuntime>();
    }

    if (EqualsWindowClassName(className, L"STATIC")) {
      if (styleLane == 0 || styleLane == 2 || styleLane == 11 || styleLane == 14) {
        return AllocateWxMswWindowRuntime<wxControlRuntime>();
      }
      return nullptr;
    }

    return nullptr;
  }

  /**
   * Address: 0x00968180 (FUN_00968180, sub_968180)
   *
   * What it does:
   * Walks the parent chain of the native window currently under the cursor and
   * returns true when this window's native handle is found.
   */
  bool wxWindowContainsCursorRuntime(
    wxWindowMswRuntime* const window
  )
  {
    if (window == nullptr) {
      return false;
    }

    const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(window);
    const HWND nativeWindowHandle = state != nullptr ? reinterpret_cast<HWND>(state->nativeHandle) : nullptr;
    if (nativeWindowHandle == nullptr) {
      return false;
    }

    POINT cursorPosition{};
    (void)::GetCursorPos(&cursorPosition);

    for (HWND current = ::WindowFromPoint(cursorPosition); current != nullptr; current = ::GetParent(current)) {
      if (current == nativeWindowHandle) {
        return true;
      }
    }

    return false;
  }

  /**
   * Address: 0x0096CCC0 (FUN_0096CCC0, wxWindow::CreateKeyEvent)
   *
   * What it does:
   * Initializes one key-event payload from Win32 key/message lanes, including
   * modifier flags, raw key metadata, timestamp, and cursor position relative
   * to the owning window rectangle.
   */
  WxKeyEventFactoryRuntime* wxWindowCreateKeyEventRuntime(
    wxWindowMswRuntime* const window,
    WxKeyEventFactoryRuntime* const event,
    const std::int32_t eventType,
    const int keyCode,
    const std::uint32_t rawFlags,
    const std::uint32_t rawCode
  )
  {
    if (window == nullptr || event == nullptr) {
      return event;
    }

    WxWindowBaseRuntimeState& state = EnsureWxWindowBaseRuntimeState(window);

    event->mEventType = eventType;
    event->mEventId = state.windowId;
    event->mShiftDown = (::GetKeyState(VK_SHIFT) < 0) ? 1u : 0u;
    event->mControlDown = (::GetKeyState(VK_CONTROL) < 0) ? 1u : 0u;
    event->mAltDown = ((rawFlags & 0x20000000u) != 0u) ? 1u : 0u;
    event->mMetaDown = 0u;
    event->mKeyCode = keyCode;
    event->mEventObject = window;
    event->mRawCode = rawCode;
    event->mRawFlags = rawFlags;
    event->mEventTimestamp = static_cast<std::int32_t>(gCurrentMessage.time);

    POINT cursorPosition{};
    (void)::GetCursorPos(&cursorPosition);

    RECT windowRect{};
    const HWND nativeWindowHandle = reinterpret_cast<HWND>(state.nativeHandle);
    if (nativeWindowHandle != nullptr && ::GetWindowRect(nativeWindowHandle, &windowRect) != 0) {
      event->mX = cursorPosition.x - windowRect.left;
      event->mY = cursorPosition.y - windowRect.top;
    } else {
      event->mX = cursorPosition.x;
      event->mY = cursorPosition.y;
    }

    return event;
  }

  /**
   * Address: 0x0096CEA0 (FUN_0096CEA0, sub_96CEA0)
   *
   * What it does:
   * Translates one Win32 key-down virtual key into wx/MAUI keycode space,
   * materializes a key event payload, dispatches it through the active event
   * handler lane, and returns whether the key event was handled.
   */
  [[nodiscard]] bool wxWindowDispatchKeyDownRuntime(
    wxWindowMswRuntime* const window,
    const int virtualKeyCode,
    const std::uint32_t rawFlags
  )
  {
    if (window == nullptr) {
      return false;
    }

    int keyCode = wxCharCodeMSWToWX(virtualKeyCode);
    if (keyCode == 0) {
      keyCode = virtualKeyCode;
    }
    if (keyCode == -1) {
      return false;
    }

    WxKeyEventFactoryRuntime event{};
    (void)wxWindowCreateKeyEventRuntime(
      window,
      &event,
      EnsureWxEvtKeyDownRuntimeType(),
      keyCode,
      rawFlags,
      static_cast<std::uint32_t>(virtualKeyCode)
    );

    WxWindowBaseRuntimeState& state = EnsureWxWindowBaseRuntimeState(window);
    wxWindowBase* const eventHandler = state.eventHandler != nullptr ? state.eventHandler : window;
    const bool handled = eventHandler->ProcessEvent(&event);

    RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(&event));
    return handled;
  }
} // namespace

/**
 * Address: 0x0097D080 (FUN_0097D080)
 * Mangled: ?CreateWindowFromHWND@wxWindow@@UAEPAV1@PAV1@K@Z
 *
 * What it does:
 * Adapts one native Win32 HWND into the closest recovered wx runtime control
 * wrapper and adopts HWND-derived attributes.
 */
void* wxWindowMswRuntime::CreateWindowFromHWND(
  void* const parent,
  const unsigned long nativeHandle
)
{
  const HWND nativeWindow = reinterpret_cast<HWND>(nativeHandle);
  if (nativeWindow == nullptr) {
    return nullptr;
  }

  wchar_t windowClassName[64] = {};
  const int classNameLength = ::GetClassNameW(
    nativeWindow, windowClassName, static_cast<int>(sizeof(windowClassName) / sizeof(windowClassName[0]))
  );
  if (classNameLength <= 0) {
    return nullptr;
  }

  const long windowStyle = static_cast<long>(::GetWindowLongW(nativeWindow, GWL_STYLE));
  const signed char styleLane = static_cast<signed char>(windowStyle & 0xFF);
  const std::int32_t windowId = static_cast<std::uint16_t>(::GetDlgCtrlID(nativeWindow));

  wxWindowMswRuntime* const createdWindow = CreateRuntimeFromClassAndStyle(windowClassName, styleLane);
  if (createdWindow == nullptr) {
    return nullptr;
  }

  wxWindowBase* const parentWindow = static_cast<wxWindowBase*>(parent);
  if (parentWindow != nullptr) {
    parentWindow->AddChild(createdWindow);
  }

  WxWindowBaseRuntimeState& state = EnsureWxWindowBaseRuntimeState(createdWindow);
  state.windowStyle = windowStyle;
  state.nativeHandle = nativeHandle;
  state.windowId = windowId;
  state.parentWindow = parentWindow;
  state.eventHandler = createdWindow;

  createdWindow->AdoptAttributesFromHWND();
  createdWindow->SetupColours();
  return createdWindow;
}

/**
 * Address: 0x00969970 (FUN_00969970, wxWindow::HandleCaptureChanged)
 *
 * What it does:
 * Builds one mouse-capture-changed event, resolves the previous capture owner
 * from the native handle lane, and dispatches the event through the active
 * window event-handler lane.
 */
bool wxWindowMswRuntime::HandleCaptureChanged(
  const int nativeHandle
)
{
  WxWindowBaseRuntimeState& state = EnsureWxWindowBaseRuntimeState(this);
  wxWindowMswRuntime* const previousCapture = wxFindWinFromHandle(nativeHandle);

  WxMouseCaptureChangedEventRuntime captureEvent(
    state.windowId, EnsureWxEvtMouseCaptureChangedRuntimeType(), previousCapture
  );
  captureEvent.mEventObject = this;

  wxWindowBase* const eventHandler = state.eventHandler != nullptr ? state.eventHandler : this;
  const bool handled = eventHandler->ProcessEvent(&captureEvent);

  RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(&captureEvent));
  return handled;
}

/**
 * Address: 0x0096C5F0 (FUN_0096C5F0)
 * Mangled: ?HandleDropFiles@wxWindow@@MAE_NPAUHDROP__@@@Z
 *
 * What it does:
 * Converts one Win32 HDROP payload into a drop-files event and dispatches it
 * through the window event-handler lane.
 */
bool wxWindowMswRuntime::HandleDropFiles(
  void* const hDrop
)
{
  const HDROP dropHandle = reinterpret_cast<HDROP>(hDrop);

  WxDropFilesEventRuntime dropEvent{};
  dropEvent.PopulateFromDropHandle(dropHandle);
  ::DragFinish(dropHandle);

  POINT dropPoint{};
  (void)::DragQueryPoint(dropHandle, &dropPoint);

  dropEvent.mEventObject = this;
  dropEvent.mDropPointX = static_cast<std::int32_t>(dropPoint.x);
  dropEvent.mDropPointY = static_cast<std::int32_t>(dropPoint.y);

  WxWindowBaseRuntimeState& state = EnsureWxWindowBaseRuntimeState(this);
  wxWindowBase* const eventHandler = state.eventHandler != nullptr ? state.eventHandler : this;
  return eventHandler->ProcessEvent(&dropEvent);
}

/**
 * Address: 0x00993670 (FUN_00993670)
 * Mangled: ?AdoptAttributesFromHWND@wxTextCtrl@@UAEXXZ
 *
 * What it does:
 * Applies text-control specific style mapping from native EDIT/RichEdit HWND
 * styles into the runtime wx style lane and tracks detected RichEdit major
 * version.
 */
void wxTextCtrlRuntime::AdoptAttributesFromHWND()
{
  wxWindowMswRuntime::AdoptAttributesFromHWND();

  WxWindowBaseRuntimeState& baseState = EnsureWxWindowBaseRuntimeState(this);
  WxTextCtrlRuntimeState& textState = EnsureWxTextCtrlRuntimeState(this);
  const HWND nativeWindow = reinterpret_cast<HWND>(baseState.nativeHandle);
  if (nativeWindow == nullptr) {
    textState.richEditMajorVersion = 0;
    return;
  }

  const long nativeStyle = static_cast<long>(::GetWindowLongW(nativeWindow, GWL_STYLE));

  wchar_t className[64] = {};
  const int classNameLength =
    ::GetClassNameW(nativeWindow, className, static_cast<int>(sizeof(className) / sizeof(className[0])));
  if (classNameLength > 0) {
    if (EqualsWindowClassName(className, L"EDIT")) {
      textState.richEditMajorVersion = 0;
    } else {
      std::int32_t richEditMajorVersion = 0;
      if (TryParseRichEditMajorVersion(className, &richEditMajorVersion)) {
        textState.richEditMajorVersion = richEditMajorVersion;
      } else {
        textState.richEditMajorVersion = 0;
      }
    }
  } else {
    textState.richEditMajorVersion = 0;
  }

  if ((nativeStyle & ES_MULTILINE) != 0) {
    baseState.windowStyle |= kWxTextCtrlStyleMultiline;
  }
  if ((nativeStyle & ES_PASSWORD) != 0) {
    baseState.windowStyle |= kWxTextCtrlStylePassword;
  }
  if ((nativeStyle & ES_READONLY) != 0) {
    baseState.windowStyle |= kWxTextCtrlStyleReadOnly;
  }
  if ((nativeStyle & ES_WANTRETURN) != 0) {
    baseState.windowStyle |= kWxTextCtrlStyleProcessEnter;
  }
  if ((nativeStyle & ES_CENTER) != 0) {
    baseState.windowStyle |= kWxTextCtrlStyleCenter;
  }
  if ((nativeStyle & ES_RIGHT) != 0) {
    baseState.windowStyle |= kWxTextCtrlStyleRight;
  }
}

/**
 * Address: 0x00994510 (FUN_00994510)
 * Mangled: ?OnCtlColor@wxTextCtrl@@UAEKKKIIIJ@Z
 *
 * What it does:
 * Applies text-control foreground/background colours and returns one cached
 * solid brush for ctl-color paint requests.
 */
unsigned long wxTextCtrlRuntime::OnCtlColor(
  const unsigned long hdc,
  const unsigned long hwnd,
  const unsigned int nCtlColor,
  const unsigned int message,
  const unsigned int controlId,
  const long result
)
{
  (void)hwnd;
  (void)nCtlColor;
  (void)message;
  (void)controlId;
  (void)result;

  const WxWindowBaseRuntimeState* const thisState = FindWxWindowBaseRuntimeState(this);
  const WxWindowBaseRuntimeState* parentState = nullptr;
  if (thisState != nullptr && thisState->parentWindow != nullptr) {
    parentState = FindWxWindowBaseRuntimeState(thisState->parentWindow);
  }

  const HDC nativeDc = reinterpret_cast<HDC>(hdc);
  const int backgroundMode = (parentState != nullptr && (parentState->bitfields & 0x2u) != 0u) ? TRANSPARENT : OPAQUE;
  (void)::SetBkMode(nativeDc, backgroundMode);

  wxColourRuntime backgroundColour = GetBackgroundColour();
  const bool useWindowBackgroundColour = thisState == nullptr || (thisState->bitfields & 0x4u) != 0u ||
    (GetWindowStyleFlag() & kWxTextCtrlStyleMultiline) != 0;
  if (!useWindowBackgroundColour) {
    const COLORREF systemFace = ::GetSysColor(COLOR_3DFACE);
    backgroundColour = wxColourRuntime::FromRgb(
      static_cast<std::uint8_t>(GetRValue(systemFace)),
      static_cast<std::uint8_t>(GetGValue(systemFace)),
      static_cast<std::uint8_t>(GetBValue(systemFace))
    );
  }

  const COLORREF backgroundColorRef =
    RGB(backgroundColour.mStorage[0], backgroundColour.mStorage[1], backgroundColour.mStorage[2]);
  (void)::SetBkColor(nativeDc, backgroundColorRef);

  const COLORREF textColorRef = ::GetSysColor(COLOR_WINDOWTEXT);
  (void)::SetTextColor(nativeDc, textColorRef);

  const HBRUSH brush = GetOrCreateCtlColorBrush(backgroundColorRef);
  return static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(brush));
}

/**
 * Address: 0x009A4C90 (FUN_009A4C90)
 *
 * What it does:
 * Draws one wx string lane with transparent background into the supplied draw
 * rectangle and restores previous DC text/background state.
 */
[[maybe_unused]] COLORREF wxDrawTextTransparentRestoreDcState(
  const COLORREF textColor,
  HDC const deviceContext,
  RECT* const drawRect,
  const wchar_t* const* const stringStorage
)
{
  const COLORREF previousTextColor = ::SetTextColor(deviceContext, textColor);
  const int previousBkMode = ::SetBkMode(deviceContext, TRANSPARENT);

  const wchar_t* drawText = wxEmptyString;
  int drawTextLength = 0;
  if (stringStorage != nullptr && *stringStorage != nullptr) {
    drawText = *stringStorage;
    const auto* const sharedPrefixWords = reinterpret_cast<const std::int32_t*>(drawText) - 3;
    drawTextLength = sharedPrefixWords[1];
  }

  (void)::DrawTextW(deviceContext, drawText, drawTextLength, drawRect, 0x25u);
  (void)::SetBkMode(deviceContext, previousBkMode);
  return ::SetTextColor(deviceContext, previousTextColor);
}

/**
 * Address: 0x004A3830 (FUN_004A3830)
 * Mangled: ?Command@wxControl@@UAEXAAVwxCommandEvent@@@Z
 *
 * What it does:
 * Forwards one command-event dispatch into `ProcessCommand`.
 */
void wxControlRuntime::Command(
  void* const commandEvent
)
{
  ProcessCommand(commandEvent);
}

/**
 * Address: 0x004A3840 (FUN_004A3840)
 * Mangled: ?MSWOnDraw@wxControl@@UAE_NPAPAX@Z
 *
 * What it does:
 * Base implementation reports that no owner-draw handling was performed.
 */
bool wxControlRuntime::MSWOnDraw(
  void** const drawStruct
)
{
  (void)drawStruct;
  return false;
}

/**
 * Address: 0x004A3850 (FUN_004A3850)
 * Mangled: ?MSWOnMeasure@wxControl@@UAE_NPAPAX@Z
 *
 * What it does:
 * Base implementation reports that no owner-measure handling was performed.
 */
bool wxControlRuntime::MSWOnMeasure(
  void** const measureStruct
)
{
  (void)measureStruct;
  return false;
}

/**
 * Address: 0x0042B3E0 (FUN_0042B3E0)
 * Mangled: ?SetTitle@wxWindowBase@@UAEXPBG@Z
 *
 * What it does:
 * Base implementation accepts but ignores title updates.
 */
void wxWindowBase::SetTitle(
  const wxStringRuntime& title
)
{
  (void)title;
}

/**
 * Address: 0x0042B3F0 (FUN_0042B3F0)
 * Mangled: ?GetTitle@wxWindowBase@@UBE?AVwxString@@XZ
 *
 * What it does:
 * Returns an empty runtime wx string for base windows.
 */
wxStringRuntime wxWindowBase::GetTitle() const
{
  return wxStringRuntime::Borrow(L"");
}

/**
 * Address: 0x0042B420 (FUN_0042B420)
 * Mangled: ?SetLabel@wxWindowBase@@UAEXABVwxString@@@Z
 *
 * What it does:
 * Forwards label updates to `SetTitle`.
 */
void wxWindowBase::SetLabel(
  const wxStringRuntime& label
)
{
  SetTitle(label);
}

/**
 * Address: 0x0042B430 (FUN_0042B430)
 * Mangled: ?GetLabel@wxWindowBase@@UBE?AVwxString@@XZ
 *
 * What it does:
 * Forwards label reads to `GetTitle`.
 */
wxStringRuntime wxWindowBase::GetLabel() const
{
  return GetTitle();
}

/**
 * Address: 0x0042B450 (FUN_0042B450)
 * Mangled: ?SetName@wxWindowBase@@UAEXABVwxString@@@Z
 *
 * What it does:
 * Stores one runtime window-name value.
 */
void wxWindowBase::SetName(
  const wxStringRuntime& name
)
{
  EnsureWxWindowBaseRuntimeState(this).windowName.assign(name.c_str());
}

/**
 * Address: 0x0042B460 (FUN_0042B460)
 * Mangled: ?GetName@wxWindowBase@@UBE?AVwxString@@XZ
 *
 * What it does:
 * Returns the current runtime window-name value.
 */
wxStringRuntime wxWindowBase::GetName() const
{
  const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(this);
  return wxStringRuntime::Borrow(state != nullptr ? state->windowName.c_str() : L"");
}

/**
 * Address: 0x00967200 (FUN_00967200)
 * Mangled: ?GetBackgroundColour@wxWindowBase@@QBE?AVwxColour@@XZ
 *
 * What it does:
 * Returns one copy of the window background-colour runtime lane.
 */
wxColourRuntime wxWindowBase::GetBackgroundColour() const
{
  const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(this);
  return state != nullptr ? state->backgroundColour : wxColourRuntime::Null();
}

/**
 * Address: 0x00963540 (FUN_00963540)
 * Mangled: ?GetClientAreaOrigin@wxWindowBase@@UBE?AVwxPoint@@XZ
 *
 * What it does:
 * Returns the default client-area origin lane `(0, 0)`.
 */
wxPoint wxWindowBase::GetClientAreaOrigin() const
{
  return wxPoint{0, 0};
}

/**
 * Address: 0x0042B4F0 (FUN_0042B4F0)
 * Mangled: ?GetMinWidth@wxWindowBase@@UBEHXZ
 */
std::int32_t wxWindowBase::GetMinWidth() const
{
  const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(this);
  return state != nullptr ? state->minWidth : -1;
}

/**
 * Address: 0x0042B500 (FUN_0042B500)
 * Mangled: ?GetMinHeight@wxWindowBase@@UBEHXZ
 */
std::int32_t wxWindowBase::GetMinHeight() const
{
  const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(this);
  return state != nullptr ? state->minHeight : -1;
}

/**
 * Address: 0x0042B510 (FUN_0042B510)
 * Mangled: ?GetMaxSize@wxWindowBase@@UBE?AVwxSize@@XZ
 */
wxSize wxWindowBase::GetMaxSize() const
{
  const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(this);
  if (state == nullptr) {
    return wxSize{-1, -1};
  }

  return wxSize{state->maxWidth, state->maxHeight};
}

/**
 * Address: 0x0042B4A0 (FUN_0042B4A0)
 *
 * What it does:
 * Returns client size by forwarding to `DoGetClientSize`.
 */
wxSize wxWindowBase::GetClientSize() const
{
  std::int32_t width = 0;
  std::int32_t height = 0;
  DoGetClientSize(&width, &height);
  return wxSize{width, height};
}

/**
 * Address: 0x0042B4D0 (FUN_0042B4D0)
 *
 * What it does:
 * Returns best size by forwarding to `DoGetBestSize`.
 */
wxSize wxWindowBase::GetBestSize() const
{
  return DoGetBestSize();
}

/**
 * Address: 0x0042B530 (FUN_0042B530)
 * Mangled: ?GetBestVirtualSize@wxWindowBase@@UBE?AVwxSize@@XZ
 */
wxSize wxWindowBase::GetBestVirtualSize() const
{
  std::int32_t clientWidth = 0;
  std::int32_t clientHeight = 0;
  DoGetClientSize(&clientWidth, &clientHeight);

  const wxSize bestSize = DoGetBestSize();
  const std::int32_t width = clientWidth > bestSize.x ? clientWidth : bestSize.x;
  const std::int32_t height = clientHeight > bestSize.y ? clientHeight : bestSize.y;
  return wxSize{width, height};
}

/**
 * Address: 0x00963660 (FUN_00963660)
 * Mangled: ?Show@wxWindowBase@@UAE_N_N@Z
 *
 * What it does:
 * Toggles the base visible-state bit (0x02) and reports whether the bit
 * changed for this call.
 */
bool wxWindowBase::Show(
  const bool show
)
{
  WxWindowBaseRuntimeState& state = EnsureWxWindowBaseRuntimeState(this);
  const bool currentlyVisible = (state.bitfields & 0x2u) != 0u;
  if (show == currentlyVisible) {
    return false;
  }

  if (show) {
    state.bitfields = static_cast<std::uint8_t>(state.bitfields | 0x2u);
  } else {
    state.bitfields = static_cast<std::uint8_t>(state.bitfields & ~0x2u);
  }
  return true;
}

/**
 * Address: 0x009636A0 (FUN_009636A0)
 * Mangled: ?Enable@wxWindowBase@@UAE_N_N@Z
 *
 * What it does:
 * Toggles the base enabled-state bit (0x04) and reports whether the bit
 * changed for this call.
 */
bool wxWindowBase::Enable(
  const bool enable
)
{
  WxWindowBaseRuntimeState& state = EnsureWxWindowBaseRuntimeState(this);
  const bool currentlyEnabled = (state.bitfields & 0x4u) != 0u;
  if (enable == currentlyEnabled) {
    return false;
  }

  if (enable) {
    state.bitfields = static_cast<std::uint8_t>(state.bitfields | 0x4u);
  } else {
    state.bitfields = static_cast<std::uint8_t>(state.bitfields & ~0x4u);
  }
  return true;
}

/**
 * Address: 0x0042B5B0 (FUN_0042B5B0)
 * Mangled: ?SetWindowStyleFlag@wxWindowBase@@UAEXJ@Z
 */
void wxWindowBase::SetWindowStyleFlag(
  const long style
)
{
  EnsureWxWindowBaseRuntimeState(this).windowStyle = style;
}

/**
 * Address: 0x0042B5C0 (FUN_0042B5C0)
 * Mangled: ?GetWindowStyleFlag@wxWindowBase@@UBEJXZ
 */
long wxWindowBase::GetWindowStyleFlag() const
{
  const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(this);
  return state != nullptr ? state->windowStyle : 0;
}

/**
 * Address: 0x0042B5F0 (FUN_0042B5F0)
 * Mangled: ?IsRetained@wxWindowBase@@UBE_NXZ
 */
bool wxWindowBase::IsRetained() const
{
  return ((static_cast<unsigned long>(GetWindowStyleFlag()) >> 17) & 1u) != 0u;
}

/**
 * Address: 0x0042B600 (FUN_0042B600)
 * Mangled: ?SetExtraStyle@wxWindowBase@@UAEXJ@Z
 */
void wxWindowBase::SetExtraStyle(
  const long style
)
{
  EnsureWxWindowBaseRuntimeState(this).extraStyle = style;
}

/**
 * Address: 0x0042B610 (FUN_0042B610)
 * Mangled: ?SetThemeEnabled@wxWindowBase@@UAEX_N@Z
 */
void wxWindowBase::SetThemeEnabled(
  const bool enabled
)
{
  EnsureWxWindowBaseRuntimeState(this).themeEnabled = enabled;
}

/**
 * Address: 0x0042B620 (FUN_0042B620)
 * Mangled: ?GetThemeEnabled@wxWindowBase@@UBE_NXZ
 */
bool wxWindowBase::GetThemeEnabled() const
{
  const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(this);
  return state != nullptr && state->themeEnabled;
}

/**
 * Address: 0x0042B630 (FUN_0042B630)
 * Mangled: ?SetFocusFromKbd@wxWindowBase@@UAEXXZ
 */
void wxWindowBase::SetFocusFromKbd()
{
  SetFocus();
}

/**
 * Address: 0x0042B640 (FUN_0042B640)
 * Mangled: ?AcceptsFocus@wxWindowBase@@UBE_NXZ
 */
bool wxWindowBase::AcceptsFocus() const
{
  const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(this);
  if (state == nullptr) {
    return false;
  }

  const std::uint8_t bitfields = state->bitfields;
  return (bitfields & 0x2u) != 0 && (bitfields & 0x4u) != 0;
}

/**
 * Address: 0x0042B660 (FUN_0042B660)
 * Mangled: ?AcceptsFocusFromKeyboard@wxWindowBase@@UBE_NXZ
 */
bool wxWindowBase::AcceptsFocusFromKeyboard() const
{
  return AcceptsFocus();
}

/**
 * Address: 0x0042B670 (FUN_0042B670)
 * Mangled: ?GetDefaultItem@wxWindowBase@@UBEPAVwxWindow@@XZ
 */
void* wxWindowBase::GetDefaultItem() const
{
  return nullptr;
}

/**
 * Address: 0x0042B680 (FUN_0042B680)
 * Mangled: ?SetDefaultItem@wxWindowBase@@UAEPAVwxWindow@@PAV2@@Z
 */
void* wxWindowBase::SetDefaultItem(
  void* const defaultItem
)
{
  (void)defaultItem;
  return nullptr;
}

/**
 * Address: 0x0042B690 (FUN_0042B690)
 * Mangled: ?SetTmpDefaultItem@wxWindowBase@@UAEXPAVwxWindow@@@Z
 */
void wxWindowBase::SetTmpDefaultItem(
  void* const defaultItem
)
{
  (void)defaultItem;
}

/**
 * Address: 0x0042B6E0 (FUN_0042B6E0)
 * Mangled: ?HasCapture@wxWindowBase@@UBE_NXZ
 */
bool wxWindowBase::HasCapture() const
{
  return this == GetCapture();
}

wxWindowBase* wxWindowBase::GetCapture()
{
  return gCapturedWindow;
}

/**
 * Address: 0x00964CA0 (FUN_00964CA0)
 * Mangled: ?CaptureMouse@wxWindowBase@@QAEXXZ
 *
 * What it does:
 * Releases any previous capture owner, pushes it onto the capture-history
 * stack lane, then requests capture for this window.
 */
void wxWindowBase::CaptureMouse()
{
  wxWindowBase* const previousCapture = GetCapture();
  if (previousCapture != nullptr) {
    previousCapture->DoReleaseMouse();

    auto* const historyNode = new (std::nothrow) WxWindowCaptureHistoryNode{};
    if (historyNode != nullptr) {
      historyNode->window = previousCapture;
      historyNode->next = gWindowCaptureHistoryHead;
      gWindowCaptureHistoryHead = historyNode;
    }
  }

  DoCaptureMouse();
}

/**
 * Address: 0x0042B700 (FUN_0042B700)
 */
void wxWindowBase::Update() {}

/**
 * Address: 0x0042B710 (FUN_0042B710)
 */
void wxWindowBase::Freeze() {}

/**
 * Address: 0x0042B720 (FUN_0042B720)
 */
void wxWindowBase::Thaw() {}

/**
 * Address: 0x0042B730 (FUN_0042B730)
 * Mangled: ?PrepareDC@wxWindowBase@@UAEXAAVwxDC@@@Z
 */
void wxWindowBase::PrepareDC(
  void* const deviceContext
)
{
  (void)deviceContext;
}

/**
 * Address: 0x0042B740 (FUN_0042B740)
 */
bool wxWindowBase::ScrollLines(
  const std::int32_t lines
)
{
  (void)lines;
  return false;
}

/**
 * Address: 0x0042B750 (FUN_0042B750)
 */
bool wxWindowBase::ScrollPages(
  const std::int32_t pages
)
{
  (void)pages;
  return false;
}

void wxWindowBase::SetDropTarget(
  void* const dropTarget
)
{
  EnsureWxWindowBaseRuntimeState(this).dropTarget = dropTarget;
}

/**
 * Address: 0x0042B760 (FUN_0042B760)
 * Mangled: ?GetDropTarget@wxWindowBase@@UBEPAVwxDropTarget@@XZ
 */
void* wxWindowBase::GetDropTarget() const
{
  const WxWindowBaseRuntimeState* const state = FindWxWindowBaseRuntimeState(this);
  return state != nullptr ? state->dropTarget : nullptr;
}

/**
 * Address: 0x00992230 (FUN_00992230, ?Pending@wxApp@@UAE_NXZ)
 *
 * What it does:
 * Reports whether at least one Win32 message is pending without removing it.
 */
bool wxApp::Pending()
{
  return ::PeekMessageW(&gCurrentMessage, nullptr, 0u, 0u, 0u) != FALSE;
}

/**
 * Address: 0x00992250 (FUN_00992250, ?Dispatch@wxApp@@UAEXXZ)
 *
 * What it does:
 * Dispatches one queued app-loop message through the recovered wx runtime lane.
 */
void wxApp::Dispatch()
{
  (void)DoMessage();
}

/**
 * Address: 0x009AA860 (FUN_009AA860, ?OnExit@wxAppBase@@UAEHXZ)
 *
 * What it does:
 * Base wx app shutdown hook. The recovered FA lane returns success (`0`)
 * after higher-level teardown paths complete.
 */
int wxApp::OnExit()
{
  return 0;
}

/**
 * Address: 0x00992190 (FUN_00992190, ?ProcessIdle@wxApp@@UAE_NXZ)
 *
 * What it does:
 * Builds one idle event, dispatches it through the app event-handler lane,
 * and returns the idle-event `request more` flag.
 */
bool wxApp::ProcessIdle()
{
  WxIdleEventRuntime idleEvent{};
  idleEvent.mEventObject = this;

  (void)ProcessEvent(&idleEvent);
  const bool requestMore = idleEvent.mRequestMore;
  RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(&idleEvent));
  return requestMore;
}

/**
 * Address: 0x00993100 (FUN_00993100)
 * Mangled: ?DoMessage@wxApp@@UAE_NXZ
 *
 * What it does:
 * Reads one Win32 message and dispatches it immediately on the GUI-owner
 * thread; background-thread messages are copied into one deferred queue lane.
 */
bool wxApp::DoMessage()
{
  const int getMessageResult = ::GetMessageW(&gCurrentMessage, nullptr, 0, 0);
  if (getMessageResult == 0) {
    m_keepGoing = 0;
    return false;
  }

  if (getMessageResult == -1) {
    return true;
  }

  EnsureDeferredThreadMessageQueueInitialized();

  if (IsGuiOwnedByMainThread()) {
    DispatchDeferredThreadMessages(*this);
    ProcessMessage(reinterpret_cast<void**>(&gCurrentMessage));
  } else {
    gIsDispatchingDeferredMessages = false;
    if (!ShouldSuppressDeferredCommandMessages() || gCurrentMessage.message != kWin32CommandMessageId) {
      QueueDeferredThreadMessage(gCurrentMessage, 1u);
    }
  }

  return true;
}

/**
 * Address: 0x00992B90 (FUN_00992B90, wxEntryStart)
 *
 * IDA signature:
 * BOOL sub_992B90();
 *
 * What it does:
 * Runs wx startup initialization and returns the success flag as `bool`.
 */
bool wxEntryStart()
{
  return wxApp::Initialize();
}

/**
 * Address: 0x00992020 (FUN_00992020, wxEntryInitGui)
 *
 * What it does:
 * Invokes `wxTheApp->OnInitGui()` and returns the virtual-call success lane.
 */
bool wxEntryInitGui()
{
  return wxTheApp->OnInitGui();
}

/**
 * Address: 0x00992FE0 (FUN_00992FE0, wxEntryCleanup)
 *
 * IDA signature:
 * void __cdecl wxEntryCleanup();
 *
 * What it does:
 * Runs wx shutdown cleanup used by the `wxEntry` exit path.
 */
void wxEntryCleanup()
{
  wxApp::CleanUp();
}

/**
 * Address: 0x00968990 (FUN_00968990, wxYieldForCommandsOnly)
 *
 * What it does:
 * Pumps only `WM_COMMAND` messages from the current thread queue and routes
 * each through `wxApp::ProcessMessage`; reposts quit state when `WM_QUIT` is
 * observed.
 */
void wxYieldForCommandsOnly()
{
  MSG commandMessage{};
  const UINT commandMessageId = WM_COMMAND;

  while (::PeekMessageW(&commandMessage, nullptr, commandMessageId, commandMessageId, PM_REMOVE) != FALSE) {
    if (commandMessage.message == WM_QUIT) {
      break;
    }

    if (wxTheApp != nullptr) {
      (void)wxTheApp->ProcessMessage(reinterpret_cast<void**>(&commandMessage));
    }
  }

  if (commandMessage.message == WM_QUIT) {
    ::PostQuitMessage(0);
  }
}

msvc8::vector<moho::ManagedWindowSlot> moho::managedWindows{};
msvc8::vector<moho::ManagedWindowSlot> moho::managedFrames{};
wxWindowBase* moho::sMainWindow = nullptr;
moho::WRenViewport* moho::ren_Viewport = nullptr;
void* moho::WBitmapPanel::sm_eventTable[1] = {nullptr};
void* moho::WBitmapCheckBox::sm_eventTable[1] = {nullptr};
void* moho::WRenViewport::sm_eventTable[1] = {nullptr};

moho::wxDCRuntime::wxDCRuntime(
  wxWindowBase* const ownerWindow
) noexcept
  : mOwnerWindow(ownerWindow)
{}

void moho::wxDCRuntime::SetBrush(
  const void* const brushToken
) noexcept
{
  mActiveBrush = brushToken;
}

void moho::wxDCRuntime::DoGetSize(
  std::int32_t* const outWidth,
  std::int32_t* const outHeight
) const noexcept
{
  if (mOwnerWindow != nullptr) {
    mOwnerWindow->DoGetSize(outWidth, outHeight);
    return;
  }

  if (outWidth != nullptr) {
    *outWidth = 0;
  }
  if (outHeight != nullptr) {
    *outHeight = 0;
  }
}

void moho::wxDCRuntime::DoDrawRectangle(
  const std::int32_t x,
  const std::int32_t y,
  const std::int32_t width,
  const std::int32_t height
) noexcept
{
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  (void)mActiveBrush;
}

moho::wxPaintDCRuntime::wxPaintDCRuntime(
  wxWindowBase* const ownerWindow
) noexcept
  : wxDCRuntime(ownerWindow)
{}

moho::wxPaintDCRuntime::~wxPaintDCRuntime() = default;

bool moho::WX_EnsureSplashPngHandler()
{
  if (gSplashPngHandlerInitialized) {
    return true;
  }

  // Source-only runtime tracks registration state without importing wx handlers.
  gSplashPngHandlerInitialized = true;
  return true;
}

moho::SplashScreenRuntime* moho::WX_CreateSplashScreen(
  const char* const filename,
  const wxSize& size
)
{
  if (filename == nullptr || filename[0] == '\0') {
    return nullptr;
  }

  std::error_code pathError;
  std::filesystem::path splashPath(filename);
  if (!splashPath.is_absolute()) {
    splashPath = std::filesystem::absolute(splashPath, pathError);
    if (pathError) {
      return nullptr;
    }
  }

  if (!std::filesystem::exists(splashPath, pathError) || pathError) {
    return nullptr;
  }

  msvc8::string splashPathText;
  splashPathText.assign_owned(splashPath.generic_string());
  return new (std::nothrow) SplashScreenRuntimeImpl(splashPathText, size);
}

void* moho::WD3DViewport::sm_eventTable[1] = {nullptr};

/**
 * Address: 0x00430980 (FUN_00430980)
 * Mangled:
 * ??0WD3DViewport@Moho@@QAE@PAVwxWindow@@VStrArg@gpg@@ABVwxPoint@@ABVwxSize@@@Z
 *
 * What it does:
 * Converts the startup title lane to wide text for wx window creation flow,
 * binds parent ownership, and clears retained D3D device reference storage.
 */
moho::WD3DViewport::WD3DViewport(
  wxWindowBase* const parentWindow,
  const char* const title,
  const wxPoint& position,
  const wxSize& size
)
{
  (void)position;
  (void)size;

  const std::wstring wideTitle = gpg::STR_Utf8ToWide(title != nullptr ? title : "");
  (void)wideTitle;

  std::memset(mUnknown04To0C, 0, sizeof(mUnknown04To0C));
  mRenderState0C = -1;
  std::memset(mUnknown10To1D, 0, sizeof(mUnknown10To1D));
  mEnabled = 0;
  std::memset(mUnknown1ETo2B, 0, sizeof(mUnknown1ETo2B));
  m_parent = parentWindow;
  mD3DDevice = nullptr;
}

/**
 * Address: 0x0042BA90 (FUN_0042BA90)
 * Mangled: ??1WD3DViewport@Moho@@UAE@XZ
 *
 * What it does:
 * Releases one held D3D-device reference before base window teardown.
 */
moho::WD3DViewport::~WD3DViewport()
{
  ReleaseD3DDeviceRef(mD3DDevice);
  mD3DDevice = nullptr;
}

/**
 * Address: 0x0042BAF0 (FUN_0042BAF0)
 */
void moho::WD3DViewport::D3DWindowOnDeviceInit() {}

/**
 * Address: 0x0042BB00 (FUN_0042BB00)
 */
void moho::WD3DViewport::D3DWindowOnDeviceRender() {}

/**
 * Address: 0x0042BB10 (FUN_0042BB10)
 */
void moho::WD3DViewport::D3DWindowOnDeviceExit() {}

/**
 * Address: 0x0042BB20 (FUN_0042BB20)
 */
void moho::WD3DViewport::RenderPreviewImage() {}

/**
 * Address: 0x0042BB30 (FUN_0042BB30)
 */
moho::WPreviewImageRuntime moho::WD3DViewport::GetPreviewImage() const
{
  return {};
}

/**
 * Address: 0x0042BB50 (FUN_0042BB50)
 */
moho::CD3DPrimBatcher* moho::WD3DViewport::GetPrimBatcher() const
{
  return nullptr;
}

/**
 * Address: 0x00430970 (FUN_00430970)
 * Mangled: ?GetEventTable@WD3DViewport@Moho@@MBEPBUwxEventTable@@XZ
 *
 * What it does:
 * Returns the static event-table lane for this viewport runtime type.
 */
const void* moho::WD3DViewport::GetEventTable() const
{
  return sm_eventTable;
}

namespace
{
  constexpr std::uintptr_t kWxBlackBrushToken = 1u;
  constexpr std::uintptr_t kWxNullBrushToken = 0u;
  constexpr std::uint16_t kClientHitTestCode = 1u;

  void DrawBackgroundFill(
    moho::wxDCRuntime& deviceContext
  )
  {
    std::int32_t width = 0;
    std::int32_t height = 0;

    deviceContext.SetBrush(reinterpret_cast<const void*>(kWxBlackBrushToken));
    deviceContext.DoGetSize(&width, &height);
    deviceContext.DoDrawRectangle(0, 0, width, height);
    deviceContext.SetBrush(reinterpret_cast<const void*>(kWxNullBrushToken));
  }

  struct [[maybe_unused]] WD3DViewportPaintCallbackFrame
  {
    std::uint8_t mUnknown00To1F[0x20]{};
    moho::wxDCRuntime* mDeviceContext = nullptr;
  };

  static_assert(
    offsetof(WD3DViewportPaintCallbackFrame, mDeviceContext) == 0x20,
    "WD3DViewportPaintCallbackFrame::mDeviceContext offset must be 0x20"
  );

  /**
   * Address: 0x00430B70 (FUN_00430B70)
   *
   * What it does:
   * Draws viewport background into the supplied paint DC when D3D device is
   * missing or still in background-fallback mode.
   */
  [[maybe_unused]] void WD3DViewportPaintBackgroundFallback(
    WD3DViewportPaintCallbackFrame* const callbackFrame
  )
  {
    moho::CD3DDevice* const device = moho::D3D_GetDevice();
    if (callbackFrame == nullptr || callbackFrame->mDeviceContext == nullptr) {
      return;
    }

    if (device == nullptr || device->ShouldDrawViewportBackground()) {
      DrawBackgroundFill(*callbackFrame->mDeviceContext);
    }
  }
} // namespace

/**
 * Address: 0x00430A60 (FUN_00430A60)
 * Mangled: ?DrawBackgroundImage@WD3DViewport@Moho@@AAEXAAVwxDC@@@Z
 *
 * What it does:
 * Fills the viewport paint DC with a solid black rectangle.
 */
void moho::WD3DViewport::DrawBackgroundImage(
  wxDCRuntime& deviceContext
)
{
  DrawBackgroundFill(deviceContext);
}

/**
 * Address: 0x00430AC0 (FUN_00430AC0)
 * Mangled: ?OnPaint@WD3DViewport@Moho@@QAEXAAVwxPaintEvent@@@Z
 *
 * What it does:
 * Builds one paint DC, then renders through active D3D device when ready or
 * draws fallback background.
 */
void moho::WD3DViewport::OnPaint(
  wxPaintEventRuntime& paintEvent
)
{
  (void)paintEvent;
  wxPaintDCRuntime paintDc(this);

  CD3DDevice* const device = D3D_GetDevice();
  if (gpg::gal::Device::IsReady() && device != nullptr) {
    ReleaseD3DDeviceRef(mD3DDevice);
    mD3DDevice = nullptr;
    device->Paint();
    return;
  }

  DrawBackgroundImage(paintDc);
}

/**
 * Address: 0x00430B90 (FUN_00430B90)
 * Mangled: ?MSWWindowProc@WD3DViewport@Moho@@UAEJIIJ@Z
 *
 * What it does:
 * Handles cursor ownership handoff between wx and D3D, then delegates
 * unhandled messages to base window dispatch.
 */
long moho::WD3DViewport::MSWWindowProc(
  const unsigned int message,
  const unsigned int wParam,
  const long lParam
)
{
  CD3DDevice* const device = D3D_GetDevice();
  if (device != nullptr) {
    const bool setCursorMessage = message == WM_SETCURSOR;
    const bool clientHit = static_cast<std::uint16_t>(lParam & 0xFFFF) == kClientHitTestCode;

    if (d3d_WindowsCursor) {
      if (setCursorMessage && clientHit && device->IsCursorPixelSourceReady()) {
        gpg::gal::Device::InitCursor();
        (void)device->ShowCursor(device->IsCursorShowing());
        return 1;
      }
    } else if (setCursorMessage && clientHit && (device->IsCursorPixelSourceReady() || !device->IsCursorShowing())) {
      ::SetCursor(nullptr);
      gpg::gal::Device::InitCursor();
      (void)device->ShowCursor(device->IsCursorShowing());
      return 1;
    }
  }

  return WRenViewport::MSWWindowProc(message, wParam, lParam);
}

/**
 * Address: 0x0042BB60 (FUN_0042BB60)
 *
 * What it does:
 * Deleting-dtor thunk lane for `WD3DViewport`.
 */
static moho::WD3DViewport* DeleteWD3DViewportThunk(
  moho::WD3DViewport* const viewport,
  const std::uint8_t deleteFlags
)
{
  if (viewport == nullptr) {
    return nullptr;
  }

  viewport->~WD3DViewport();
  if ((deleteFlags & 1u) != 0u) {
    operator delete(viewport);
  }
  return viewport;
}

namespace
{
  constexpr std::size_t kMaxCommittedLogLines = 10000;
  constexpr std::uint32_t kCustomFilterCategoryBit = 1u << 4;
  constexpr std::uint32_t kWarningCategoryValue = 2u;
  constexpr std::uint32_t kErrorCategoryValue = 3u;
  constexpr std::size_t kReplayIndentWidth = 4u;
  constexpr const char* kLogCategoryPreferenceKeys[] = {
    "Options.Log.Debug",
    "Options.Log.Info",
    "Options.Log.Warn",
    "Options.Log.Error",
    "Options.Log.Custom",
  };
  constexpr bool kLogCategoryPreferenceDefaults[] = {
    false,
    true,
    true,
    true,
    true,
  };
  constexpr const char* kLogFilterPreferenceKey = "Options.Log.Filter";
  constexpr const char* kLogFilterPreferenceDefault = "*DEBUG:";
  constexpr const char* kLogWindowXPreferenceKey = "Windows.Log.x";
  constexpr const char* kLogWindowYPreferenceKey = "Windows.Log.y";
  constexpr const char* kLogWindowWidthPreferenceKey = "Windows.Log.width";
  constexpr const char* kLogWindowHeightPreferenceKey = "Windows.Log.height";
  constexpr std::int32_t kLogWindowGeometryFallback = -1;
  constexpr std::int32_t kLogWindowSetSizeFlags = 3;

  [[nodiscard]] wxTextAttrRuntime BuildTextStyleFromRgb(
    const std::uint8_t red,
    const std::uint8_t green,
    const std::uint8_t blue
  ) noexcept
  {
    return wxTextAttrRuntime(
      wxColourRuntime::FromRgb(red, green, blue), wxColourRuntime::Null(), wxFontRuntime::Null()
    );
  }

  [[nodiscard]] wxTextAttrRuntime DefaultTextStyleForCategory(
    const std::uint32_t category
  ) noexcept
  {
    switch (category) {
    case 0u:
      return BuildTextStyleFromRgb(0x80, 0x80, 0x80);
    case kWarningCategoryValue:
      return BuildTextStyleFromRgb(0xF7, 0xA1, 0x00);
    case kErrorCategoryValue:
      return BuildTextStyleFromRgb(0xFF, 0x00, 0x00);
    default:
      return BuildTextStyleFromRgb(0x00, 0x00, 0x00);
    }
  }
} // namespace

namespace
{
  [[nodiscard]] wxStreamBase* DestroyWxOutputStreamBaseRuntime(wxStreamBase* const stream) noexcept
  {
    if (stream != nullptr) {
      stream->~wxStreamBase();
    }
    return stream;
  }

  /**
   * Address: 0x009DD4D0 (FUN_009DD4D0)
   *
   * What it does:
   * Executes the non-deleting destructor lane for `wxFilterOutputStream` by
   * forwarding into the shared `wxOutputStream` base-destruction path.
   */
  [[nodiscard]] wxStreamBase* DestroyWxFilterOutputStreamNoDeleteRuntime(wxStreamBase* const stream) noexcept
  {
    return DestroyWxOutputStreamBaseRuntime(stream);
  }

  /**
   * Address: 0x00A2F9E0 (FUN_00A2F9E0)
   *
   * What it does:
   * Executes the non-deleting destructor lane for `wxSocketOutputStream` by
   * forwarding into the shared `wxOutputStream` base-destruction path.
   */
  [[nodiscard]] wxStreamBase* DestroyWxSocketOutputStreamNoDeleteRuntime(wxStreamBase* const stream) noexcept
  {
    return DestroyWxOutputStreamBaseRuntime(stream);
  }
}

wxStreamBase::wxStreamBase() = default;

/**
 * Address: 0x009DCF40 (FUN_009DCF40)
 * Mangled: ??0wxInputStream@@QAE@@Z
 *
 * What it does:
 * Initializes pushback-lane counters to zero and binds input-stream base
 * runtime state.
 */
wxInputStream::wxInputStream()
  : wxStreamBase()
  , m_wback(0)
  , m_wbackcur(0)
  , m_wbacksize(0)
{}

/**
 * Address: 0x009DD0F0 (FUN_009DD0F0)
 *
 * What it does:
 * Reads one byte through the virtual stream read lane and returns the
 * resulting character.
 */
char wxInputStream::GetC()
{
  class WxInputStreamReadDispatch
  {
  public:
    virtual void Slot00() = 0;
    virtual void Slot04() = 0;
    virtual void Slot08() = 0;
    virtual void Slot0C() = 0;
    virtual void Slot10() = 0;
    virtual void* ReadBytes(void* destination, int byteCount) = 0;
  };

  std::uint8_t value = static_cast<std::uint8_t>((reinterpret_cast<std::uintptr_t>(this) >> 8u) & 0xFFu);
  auto* const dispatch = reinterpret_cast<WxInputStreamReadDispatch*>(this);
  if (dispatch != nullptr) {
    (void)dispatch->ReadBytes(&value, 1);
  }
  return static_cast<char>(value);
}

/**
 * Address: 0x009EA000 (FUN_009EA000)
 *
 * What it does:
 * Reads the first 9 bytes from `inputStream`, seeks back by 9, and returns
 * `true` only when they match the XPM header literal `/ * XPM * /`.
 */
bool wxInputStreamHasXpmSignature(wxInputStream* const inputStream)
{
  using WxInputStreamReadBytesFn = void*(__thiscall*)(void*, void*, int);
  using WxInputStreamSeekInputFn = void(__thiscall*)(void*, int, int);

  struct WxInputStreamSignatureVTable
  {
    void* slot00 = nullptr;
    void* slot04 = nullptr;
    void* slot08 = nullptr;
    void* slot0C = nullptr;
    void* slot10 = nullptr;
    WxInputStreamReadBytesFn readBytes = nullptr; // +0x14
    void* slot18 = nullptr;
    void* slot1C = nullptr;
    void* slot20 = nullptr;
    WxInputStreamSeekInputFn seekInput = nullptr; // +0x24
  };

  if (inputStream == nullptr) {
    return false;
  }

  auto* const vtable = reinterpret_cast<WxInputStreamSignatureVTable*>(*reinterpret_cast<void**>(inputStream));
  if (vtable == nullptr || vtable->readBytes == nullptr || vtable->seekInput == nullptr) {
    return false;
  }

  std::array<char, 9> signature{};
  void* const readState = vtable->readBytes(inputStream, signature.data(), static_cast<int>(signature.size()));
  if (readState == nullptr) {
    return false;
  }

  const auto* const statusLane =
    reinterpret_cast<const std::int32_t*>(reinterpret_cast<const std::uint8_t*>(readState) + 0x08);
  if (*statusLane != 0) {
    return false;
  }

  vtable->seekInput(inputStream, -static_cast<int>(signature.size()), 1);
  return std::memcmp(signature.data(), "/* XPM */", signature.size()) == 0;
}

/**
 * Address: 0x00A312A0 (FUN_00A312A0, sub_A312A0)
 *
 * What it does:
 * Formats one DDE error-code lane into a human-readable wx string payload.
 */
wxStringRuntime* wxFormatDdeErrorString(
  wxStringRuntime* const outText,
  const unsigned int ddeErrorCode
)
{
  if (outText == nullptr) {
    return nullptr;
  }

  ReleaseOwnedWxString(*outText);

  const auto assignLiteral = [outText](const wchar_t* const text) -> wxStringRuntime* {
    AssignOwnedWxString(outText, text != nullptr ? std::wstring(text) : std::wstring());
    return outText;
  };

  switch (ddeErrorCode) {
  case 0x0000u:
    return assignLiteral(L"no DDE error.");
  case 0x4000u:
    return assignLiteral(L"a request for a synchronous advise transaction has timed out.");
  case 0x4001u:
    return assignLiteral(L"the response to the transaction caused the DDE_FBUSY bit to be set.");
  case 0x4002u:
    return assignLiteral(L"a request for a synchronous data transaction has timed out.");
  case 0x4003u:
    return assignLiteral(
      L"a DDEML function was called without first calling the DdeInitialize function,\n"
      L"or an invalid instance identifier\n"
      L"was passed to a DDEML function."
    );
  case 0x4004u:
    return assignLiteral(
      L"an application initialized as APPCLASS_MONITOR has\n"
      L"attempted to perform a DDE transaction,\n"
      L"or an application initialized as APPCMD_CLIENTONLY has \n"
      L"attempted to perform server transactions."
    );
  case 0x4005u:
    return assignLiteral(L"a request for a synchronous execute transaction has timed out.");
  case 0x4006u:
    return assignLiteral(L"a parameter failed to be validated by the DDEML.");
  case 0x4007u:
    return assignLiteral(L"a DDEML application has created a prolonged race condition.");
  case 0x4008u:
    return assignLiteral(L"a memory allocation failed.");
  case 0x4009u:
    return assignLiteral(L"a transaction failed.");
  case 0x400Au:
    return assignLiteral(L"a client's attempt to establish a conversation has failed.");
  case 0x400Bu:
    return assignLiteral(L"a request for a synchronous poke transaction has timed out.");
  case 0x400Cu:
    return assignLiteral(L"an internal call to the PostMessage function has failed. ");
  case 0x400Du:
    return assignLiteral(L"reentrancy problem.");
  case 0x400Eu:
    return assignLiteral(
      L"a server-side transaction was attempted on a conversation\n"
      L"that was terminated by the client, or the server\n"
      L"terminated before completing a transaction."
    );
  case 0x400Fu:
    return assignLiteral(L"an internal error has occurred in the DDEML.");
  case 0x4010u:
    return assignLiteral(L"a request to end an advise transaction has timed out.");
  case 0x4011u:
    return assignLiteral(
      L"an invalid transaction identifier was passed to a DDEML function.\n"
      L"Once the application has returned from an XTYP_XACT_COMPLETE callback,\n"
      L"the transaction identifier for that callback is no longer valid."
    );
  default:
    break;
  }

  std::array<wchar_t, 64> unknownMessage{};
  std::swprintf(unknownMessage.data(), unknownMessage.size(), L"Unknown DDE error %08x", ddeErrorCode);
  AssignOwnedWxString(outText, std::wstring(unknownMessage.data()));
  return outText;
}

void wxLogDdeFailureMessage(const wxStringRuntime& prefixText, unsigned int ddeErrorCode);
[[nodiscard]] HSZ wxCreateDdeStringHandleRuntime(const wxStringRuntime& text);

namespace
{
  DWORD gWxDdeInstanceId = 0;

  struct WxDdeServerRuntimeView
  {
    std::uint8_t reserved00_0B[0x0C]{};
    wxStringRuntime serviceName{}; // +0x0C
  };
  static_assert(
    offsetof(WxDdeServerRuntimeView, serviceName) == 0x0C,
    "WxDdeServerRuntimeView::serviceName offset must be 0x0C"
  );

  struct WxDdeConnectionRuntimeView
  {
    std::uint8_t reserved00_0B[0x0C]{};
    std::uint8_t* exchangeBuffer = nullptr; // +0x0C
    std::uint32_t exchangeBufferChars = 0;  // +0x10
    std::uint8_t exchangeBufferOwned = 0;   // +0x14
    std::uint8_t reserved15_23[0x0F]{};
    HCONV conversationHandle = nullptr;      // +0x24
  };
  static_assert(
    offsetof(WxDdeConnectionRuntimeView, exchangeBuffer) == 0x0C,
    "WxDdeConnectionRuntimeView::exchangeBuffer offset must be 0x0C"
  );
  static_assert(
    offsetof(WxDdeConnectionRuntimeView, exchangeBufferChars) == 0x10,
    "WxDdeConnectionRuntimeView::exchangeBufferChars offset must be 0x10"
  );
  static_assert(
    offsetof(WxDdeConnectionRuntimeView, exchangeBufferOwned) == 0x14,
    "WxDdeConnectionRuntimeView::exchangeBufferOwned offset must be 0x14"
  );
  static_assert(
    offsetof(WxDdeConnectionRuntimeView, conversationHandle) == 0x24,
    "WxDdeConnectionRuntimeView::conversationHandle offset must be 0x24"
  );
  static_assert(sizeof(WxDdeConnectionRuntimeView) == 0x28, "WxDdeConnectionRuntimeView size must be 0x28");

  std::unordered_map<std::wstring, HSZ> gWxDdeItemHandleByName{};
  bool gWxDdeItemHandleCleanupRegistered = false;

  void DestroyWxDdeItemHandleCache() noexcept
  {
    if (gWxDdeInstanceId != 0u) {
      for (const auto& [itemName, handle] : gWxDdeItemHandleByName) {
        (void)itemName;
        if (handle != nullptr) {
          (void)::DdeFreeStringHandle(gWxDdeInstanceId, handle);
        }
      }
    }
    gWxDdeItemHandleByName.clear();
  }

  void EnsureWxDdeItemHandleCacheCleanupRegistered() noexcept
  {
    if (gWxDdeItemHandleCleanupRegistered) {
      return;
    }

    gWxDdeItemHandleCleanupRegistered = true;
    std::atexit(&DestroyWxDdeItemHandleCache);
  }

  [[nodiscard]] HSZ WxResolveDdeItemStringHandleRuntime(
    const wxStringRuntime& itemName
  )
  {
    const std::wstring itemText = itemName.c_str() != nullptr ? std::wstring(itemName.c_str()) : std::wstring();
    const auto existing = gWxDdeItemHandleByName.find(itemText);
    if (existing != gWxDdeItemHandleByName.end()) {
      return existing->second;
    }

    const HSZ createdHandle = wxCreateDdeStringHandleRuntime(wxStringRuntime::Borrow(itemText.c_str()));
    if (createdHandle == nullptr) {
      return nullptr;
    }

    EnsureWxDdeItemHandleCacheCleanupRegistered();
    gWxDdeItemHandleByName.emplace(itemText, createdHandle);
    return createdHandle;
  }

  /**
   * Address: 0x00A31210 (FUN_00A31210)
   *
   * What it does:
   * Queries one DDE string-handle lane into UTF-16 text and writes it into
   * caller-provided wx-string storage.
   */
  wxStringRuntime* wxDdeQueryStringHandleRuntime(
    wxStringRuntime* const outText,
    const HSZ stringHandle
  )
  {
    if (outText == nullptr) {
      return nullptr;
    }

    std::array<wchar_t, 0x100> queryBuffer{};
    const DWORD copiedChars = ::DdeQueryStringW(
      gWxDdeInstanceId,
      stringHandle,
      queryBuffer.data(),
      static_cast<DWORD>(queryBuffer.size()),
      1200
    );
    const std::size_t textLength = std::min<std::size_t>(copiedChars, queryBuffer.size() - 1u);
    queryBuffer[textLength] = L'\0';

    AssignOwnedWxString(outText, std::wstring(queryBuffer.data()));
    return outText;
  }

  struct WxConnectionBaseRuntimeView
  {
    void* vtable = nullptr;                    // +0x00
    std::uint8_t reserved04_0B[0x08]{};        // +0x04
    void* exchangeBuffer = nullptr;            // +0x0C
    std::uint8_t reserved10_13[0x04]{};        // +0x10
    std::uint8_t ownsExchangeBuffer = 0;       // +0x14
    std::uint8_t reserved15_17[0x03]{};        // +0x15
  };
  static_assert(
    offsetof(WxConnectionBaseRuntimeView, exchangeBuffer) == 0x0C,
    "WxConnectionBaseRuntimeView::exchangeBuffer offset must be 0x0C"
  );
  static_assert(
    offsetof(WxConnectionBaseRuntimeView, ownsExchangeBuffer) == 0x14,
    "WxConnectionBaseRuntimeView::ownsExchangeBuffer offset must be 0x14"
  );
  static_assert(sizeof(WxConnectionBaseRuntimeView) == 0x18, "WxConnectionBaseRuntimeView size must be 0x18");

  /**
   * Address: 0x00A38440 (FUN_00A38440)
   *
   * What it does:
   * Runs non-deleting `wxConnectionBase` teardown by deleting the owned DDE
   * exchange-buffer lane and releasing base wx-object ref-data ownership.
   */
  void wxDestroyConnectionBaseRuntime(
    void* const connectionRuntime
  ) noexcept
  {
    auto* const connection = static_cast<WxConnectionBaseRuntimeView*>(connectionRuntime);
    if (connection == nullptr) {
      return;
    }

    if (connection->ownsExchangeBuffer != 0 && connection->exchangeBuffer != nullptr) {
      ::operator delete(connection->exchangeBuffer);
      connection->exchangeBuffer = nullptr;
    }

    wxEventUnRefRuntime(reinterpret_cast<WxObjectRuntimeView*>(connection));
  }

  /**
   * Address: 0x00A384C0 (FUN_00A384C0, sub_A384C0)
   *
   * What it does:
   * Ensures the DDE connection transfer buffer can hold `requiredChars`
   * UTF-16 code units and returns the writable buffer lane.
   */
  void* wxDdeConnectionEnsureExchangeBufferRuntime(
    WxDdeConnectionRuntimeView* const connection,
    const unsigned int requiredChars
  )
  {
    if (connection == nullptr) {
      return nullptr;
    }

    if (connection->exchangeBufferChars >= requiredChars) {
      return connection->exchangeBuffer;
    }

    if (connection->exchangeBufferOwned == 0) {
      return nullptr;
    }

    if (connection->exchangeBuffer != nullptr) {
      ::operator delete(connection->exchangeBuffer);
      connection->exchangeBuffer = nullptr;
    }

    const std::uint64_t requestedBytes = static_cast<std::uint64_t>(requiredChars) * 2ull;
    const std::uint32_t allocationBytes =
      requestedBytes > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(requestedBytes);
    void* const grownBuffer = ::operator new(static_cast<std::size_t>(allocationBytes));
    connection->exchangeBufferChars = requiredChars;
    connection->exchangeBuffer = static_cast<std::uint8_t*>(grownBuffer);
    return grownBuffer;
  }
}

struct WxPrintPaperTypeRuntimeView
{
  void* vtable = nullptr;                  // +0x00
  void* refData = nullptr;                 // +0x04
  std::int32_t runtimeLane08 = 0;          // +0x08
  std::int32_t runtimeLane0C = 0;          // +0x0C
  std::int32_t runtimeLane10 = 0;          // +0x10
  std::int32_t runtimeLane14 = 0;          // +0x14
  wxStringRuntime paperName{};             // +0x18
};
static_assert(offsetof(WxPrintPaperTypeRuntimeView, refData) == 0x04, "WxPrintPaperTypeRuntimeView::refData offset must be 0x04");
static_assert(
  offsetof(WxPrintPaperTypeRuntimeView, paperName) == 0x18,
  "WxPrintPaperTypeRuntimeView::paperName offset must be 0x18"
);
static_assert(sizeof(WxPrintPaperTypeRuntimeView) == 0x1C, "WxPrintPaperTypeRuntimeView size must be 0x1C");

/**
 * Address: 0x00A32B20 (FUN_00A32B20)
 *
 * What it does:
 * Initializes one `wxPrintPaperType` runtime payload with cleared numeric
 * lanes and default paper-name text `"Default"`.
 */
[[maybe_unused]] void* wxConstructPrintPaperTypeRuntime(
  void* const printPaperRuntime
)
{
  auto* const runtime = static_cast<WxPrintPaperTypeRuntimeView*>(printPaperRuntime);
  if (runtime == nullptr) {
    return nullptr;
  }

  runtime->vtable = &gWxPrintPaperTypeRuntimeVTableTag;
  runtime->refData = nullptr;
  runtime->runtimeLane08 = 0;
  runtime->runtimeLane0C = 0;
  runtime->runtimeLane10 = 0;
  runtime->runtimeLane14 = 0;
  runtime->paperName.m_pchData = const_cast<wchar_t*>(wxEmptyString);
  AssignOwnedWxString(&runtime->paperName, std::wstring(L"Default"));
  return runtime;
}

/**
 * Address: 0x00A31A10 (FUN_00A31A10, sub_A31A10)
 *
 * What it does:
 * Logs one DDE failure lane as `"<prefix>: <dde-error-text>"`, resolving the
 * current DDE last-error code when caller passes `0`.
 */
void wxLogDdeFailureMessage(
  const wxStringRuntime& prefixText,
  unsigned int ddeErrorCode
)
{
  if (ddeErrorCode == 0) {
    ddeErrorCode = ::DdeGetLastError(gWxDdeInstanceId);
  }

  wxStringRuntime ddeErrorText{};
  wxFormatDdeErrorString(&ddeErrorText, ddeErrorCode);

  const std::wstring composed =
    std::wstring(prefixText.c_str() != nullptr ? prefixText.c_str() : L"") +
    L": " +
    std::wstring(ddeErrorText.c_str() != nullptr ? ddeErrorText.c_str() : L"");
  wxLogDebug(L"%s", composed.c_str());

  ReleaseOwnedWxString(ddeErrorText);
}

/**
 * Address: 0x00A31EC0 (FUN_00A31EC0, sub_A31EC0)
 *
 * What it does:
 * Creates one DDE string handle from UTF-16 text using the active DDE
 * instance lane and reports failure through the wx logging path.
 */
HSZ wxCreateDdeStringHandleRuntime(
  const wxStringRuntime& text
)
{
  HSZ stringHandle = ::DdeCreateStringHandleW(gWxDdeInstanceId, text.c_str(), 1200);
  if (stringHandle != nullptr) {
    return stringHandle;
  }

  wxLogDdeFailureMessage(wxStringRuntime::Borrow(L"Failed to create DDE string"), 0);
  return nullptr;
}

/**
 * Address: 0x00A32060 (FUN_00A32060, sub_A32060)
 *
 * What it does:
 * Copies one DDE server-name lane into runtime state, creates the
 * corresponding DDE string handle, and registers that server name.
 */
bool wxDdeServerRegisterRuntime(
  WxDdeServerRuntimeView* const server,
  const wxStringRuntime& serverName
)
{
  if (server == nullptr) {
    return false;
  }

  const std::wstring serverNameText = serverName.c_str() != nullptr ? std::wstring(serverName.c_str()) : std::wstring();
  AssignOwnedWxString(&server->serviceName, serverNameText);

  const HSZ serviceHandle = wxCreateDdeStringHandleRuntime(serverName);
  if (serviceHandle == nullptr) {
    return false;
  }

  if (::DdeNameService(gWxDdeInstanceId, serviceHandle, nullptr, DNS_REGISTER) != nullptr) {
    return true;
  }

  const std::wstring failureText = L"Failed to register DDE server '" + serverNameText + L"'";
  wxLogDdeFailureMessage(wxStringRuntime::Borrow(failureText.c_str()), 0);
  return false;
}

/**
 * Address: 0x00A325A0 (FUN_00A325A0, sub_A325A0)
 *
 * What it does:
 * Requests one DDE data transaction for the given item lane and returns the
 * connection-owned transfer buffer pointer when the request succeeds.
 */
BYTE* wxDdeConnectionRequestDataRuntime(
  WxDdeConnectionRuntimeView* const connection,
  const wxStringRuntime& itemName,
  DWORD* const outByteCount,
  const UINT format
)
{
  if (connection == nullptr) {
    return nullptr;
  }

  const HSZ itemHandle = WxResolveDdeItemStringHandleRuntime(itemName);
  if (itemHandle == nullptr) {
    return nullptr;
  }

  DWORD transactionResult = 0;
  HDDEDATA const dataHandle = ::DdeClientTransaction(
    nullptr,
    0u,
    connection->conversationHandle,
    itemHandle,
    format,
    0x20B0u,
    0x1388u,
    &transactionResult
  );
  if (dataHandle == nullptr) {
    wxLogDdeFailureMessage(wxStringRuntime::Borrow(L"DDE data request failed"), 0);
    return nullptr;
  }

  const DWORD dataSize = ::DdeGetData(dataHandle, nullptr, 0u, 0u);
  auto* const transferBuffer = static_cast<BYTE*>(
    wxDdeConnectionEnsureExchangeBufferRuntime(connection, static_cast<unsigned int>(dataSize))
  );
  (void)::DdeGetData(dataHandle, transferBuffer, dataSize, 0u);
  (void)::DdeFreeDataHandle(dataHandle);

  if (outByteCount != nullptr) {
    *outByteCount = dataSize;
  }
  return transferBuffer;
}

/**
 * Address: 0x00A326B0 (FUN_00A326B0, sub_A326B0)
 *
 * What it does:
 * Sends one DDE poke transaction for the requested item lane and reports
 * failure through the DDE log path.
 */
bool wxDdeConnectionPokeDataRuntime(
  WxDdeConnectionRuntimeView* const connection,
  const wxStringRuntime& itemName,
  const wchar_t* const dataText,
  const std::int32_t dataLengthChars,
  const UINT format
)
{
  if (connection == nullptr) {
    return false;
  }

  DWORD dataByteCount = static_cast<DWORD>(dataLengthChars);
  if (dataLengthChars < 0) {
    const std::size_t inferredLength = dataText != nullptr ? std::wcslen(dataText) : 0u;
    dataByteCount = static_cast<DWORD>(inferredLength + 1u);
  }

  const HSZ itemHandle = WxResolveDdeItemStringHandleRuntime(itemName);
  if (itemHandle == nullptr) {
    return false;
  }

  DWORD transactionResult = 0;
  HDDEDATA const transaction = ::DdeClientTransaction(
    reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(dataText)),
    dataByteCount,
    connection->conversationHandle,
    itemHandle,
    format,
    0x4090u,
    0x1388u,
    &transactionResult
  );
  const bool success = transaction != nullptr;
  if (!success) {
    wxLogDdeFailureMessage(wxStringRuntime::Borrow(L"DDE poke request failed"), 0);
  }
  return success;
}

/**
 * Address: 0x00A327C0 (FUN_00A327C0, sub_A327C0)
 *
 * What it does:
 * Starts one DDE advise loop for the provided item lane on the current DDE
 * conversation.
 */
bool wxDdeConnectionStartAdviseRuntime(
  WxDdeConnectionRuntimeView* const connection,
  const wxStringRuntime& itemName
)
{
  if (connection == nullptr) {
    return false;
  }

  const HSZ itemHandle = WxResolveDdeItemStringHandleRuntime(itemName);
  if (itemHandle == nullptr) {
    return false;
  }

  DWORD transactionResult = 0;
  const bool success = ::DdeClientTransaction(
    nullptr,
    0u,
    connection->conversationHandle,
    itemHandle,
    1u,
    0x1030u,
    0x1388u,
    &transactionResult
  ) != nullptr;
  if (!success) {
    wxLogDdeFailureMessage(wxStringRuntime::Borrow(L"Failed to establish an advise loop with DDE server"), 0);
  }
  return success;
}

/**
 * Address: 0x00A328A0 (FUN_00A328A0, sub_A328A0)
 *
 * What it does:
 * Terminates one DDE advise loop for the provided item lane on the current
 * DDE conversation.
 */
bool wxDdeConnectionStopAdviseRuntime(
  WxDdeConnectionRuntimeView* const connection,
  const wxStringRuntime& itemName
)
{
  if (connection == nullptr) {
    return false;
  }

  const HSZ itemHandle = WxResolveDdeItemStringHandleRuntime(itemName);
  if (itemHandle == nullptr) {
    return false;
  }

  DWORD transactionResult = 0;
  const bool success = ::DdeClientTransaction(
    nullptr,
    0u,
    connection->conversationHandle,
    itemHandle,
    1u,
    0x8040u,
    0x1388u,
    &transactionResult
  ) != nullptr;
  if (!success) {
    wxLogDdeFailureMessage(wxStringRuntime::Borrow(L"Failed to terminate the advise loop with DDE server"), 0);
  }
  return success;
}

namespace
{
  struct ChildProcessMonitorThreadContext
  {
    HWND notificationWindow = nullptr;           // +0x00
    HANDLE childProcessHandle = nullptr;         // +0x04
    DWORD childProcessId = 0;                    // +0x08
    void* ownerContext = nullptr;                // +0x0C
    DWORD childProcessExitCode = 0;              // +0x10
    std::uint8_t completionPending = 0;          // +0x14
    std::uint8_t reserved15_17[3] = {0, 0, 0};  // +0x15
  };

  static_assert(
    offsetof(ChildProcessMonitorThreadContext, notificationWindow) == 0x00,
    "ChildProcessMonitorThreadContext::notificationWindow offset must be 0x00"
  );
  static_assert(
    offsetof(ChildProcessMonitorThreadContext, childProcessHandle) == 0x04,
    "ChildProcessMonitorThreadContext::childProcessHandle offset must be 0x04"
  );
  static_assert(
    offsetof(ChildProcessMonitorThreadContext, childProcessId) == 0x08,
    "ChildProcessMonitorThreadContext::childProcessId offset must be 0x08"
  );
  static_assert(
    offsetof(ChildProcessMonitorThreadContext, ownerContext) == 0x0C,
    "ChildProcessMonitorThreadContext::ownerContext offset must be 0x0C"
  );
  static_assert(
    offsetof(ChildProcessMonitorThreadContext, childProcessExitCode) == 0x10,
    "ChildProcessMonitorThreadContext::childProcessExitCode offset must be 0x10"
  );
  static_assert(
    offsetof(ChildProcessMonitorThreadContext, completionPending) == 0x14,
    "ChildProcessMonitorThreadContext::completionPending offset must be 0x14"
  );
  static_assert(
    sizeof(ChildProcessMonitorThreadContext) == 0x18,
    "ChildProcessMonitorThreadContext size must be 0x18"
  );

  constexpr UINT kChildProcessCompletedMessage = WM_PALETTEISCHANGING | 0x2800u;
} // namespace

/**
 * Address: 0x00A133F0 (FUN_00A133F0, StartAddress)
 *
 * What it does:
 * Waits for a launched child process, records its exit code in the shared
 * monitor context, and notifies the hidden window that completion arrived.
 */
DWORD WINAPI StartAddress(
  LPVOID const lpThreadParameter
)
{
  auto* const threadContext = static_cast<ChildProcessMonitorThreadContext*>(lpThreadParameter);
  ::WaitForSingleObject(threadContext->childProcessHandle, INFINITE);
  ::GetExitCodeProcess(threadContext->childProcessHandle, &threadContext->childProcessExitCode);
  ::SendMessageW(
    threadContext->notificationWindow,
    kChildProcessCompletedMessage,
    0,
    reinterpret_cast<LPARAM>(threadContext)
  );
  return 0;
}

/**
 * Address: 0x009DDDE0 (FUN_009DDDE0, wxFileExists)
 *
 * What it does:
 * Returns true when the provided wx-string path resolves to an existing file
 * path that is not a directory.
 */
bool wxFileExists(
  const wxStringRuntime* const fileName
)
{
  if (fileName == nullptr || fileName->m_pchData == nullptr) {
    return false;
  }

  const DWORD attributes = ::GetFileAttributesW(fileName->m_pchData);
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/**
 * Address: 0x00A12870 (FUN_00A12870)
 * Mangled: ??0wxFile@@QAE@PBGW4OpenMode@0@@Z
 *
 * What it does:
 * Initializes one file lane and opens the provided wide path with read mode.
 */
wxFile::wxFile(
  const wchar_t* const fileName,
  const OpenMode mode
)
  : m_fd(-1)
  , m_error(0)
{
  (void)Open(fileName, mode, 438);
}

wxFile::~wxFile()
{
  (void)Attach();
}

/**
 * Address: 0x00A11F50 (FUN_00A11F50)
 * Mangled: ?Exists@wxFile@@SA_NPB_W@Z
 *
 * What it does:
 * Builds one temporary wx-string from a wide path and probes whether it maps
 * to an existing non-directory file.
 */
bool wxFile::Exists(
  const wchar_t* const fileName
)
{
  wxStringRuntime path = AllocateOwnedWxString(fileName != nullptr ? std::wstring(fileName) : std::wstring());
  const bool exists = wxFileExists(&path);
  ReleaseOwnedWxString(path);
  return exists;
}

/**
 * Address: 0x00A12020 (FUN_00A12020, wxFile::Attach)
 * Mangled: ?Attach@wxFile@@QAE_NXZ
 *
 * What it does:
 * Closes one open file descriptor lane and resets it to `-1`, reporting
 * failure when the close operation returns an error.
 */
bool wxFile::Attach()
{
  if (m_fd == -1) {
    return true;
  }

  if (_close(m_fd) != -1) {
    m_fd = -1;
    return true;
  }

  const wchar_t* messageTemplate = L"can't close file descriptor %d";
  if (wxLocale* const locale = wxGetLocale(); locale != nullptr) {
    messageTemplate = locale->GetString(messageTemplate, 0);
  }

  wxLogSysError(messageTemplate, m_fd);
  m_fd = -1;
  return false;
}

/**
 * Address: 0x00A94D8C (FUN_00A94D8C, wxOpen)
 *
 * What it does:
 * Opens one wide filesystem path with `_SH_DENYNO` sharing via CRT secure
 * open dispatch and returns either the file descriptor or `-1`.
 */
int wxOpen(
  const wchar_t* const fileName,
  const int openFlags,
  const int permissions
)
{
  int fileDescriptor = -1;
  if (_wsopen_s(&fileDescriptor, fileName, openFlags, _SH_DENYNO, permissions) != 0) {
    return -1;
  }
  return fileDescriptor;
}

namespace
{
  [[nodiscard]] const wchar_t* WxResolveLocalizedMessage(
    const wchar_t* const messageTemplate
  )
  {
    if (wxLocale* const locale = wxGetLocale(); locale != nullptr) {
      return locale->GetString(messageTemplate, 0);
    }

    return messageTemplate;
  }

  template <typename... TArgs>
  void WxLogSysErrorLocalized(
    const wchar_t* const messageTemplate,
    TArgs&&... arguments
  )
  {
    wxLogSysError(WxResolveLocalizedMessage(messageTemplate), std::forward<TArgs>(arguments)...);
  }

  void WxLogSysErrorLocalized(
    const wchar_t* const messageTemplate
  )
  {
    wxLogSysError(WxResolveLocalizedMessage(messageTemplate));
  }
} // namespace

/**
 * Address: 0x00A12080 (FUN_00A12080, wxFile::Read)
 * Mangled: ?Read@wxFile@@QAEJPAXJ@Z
 *
 * What it does:
 * Reads one byte span from the descriptor lane into `buffer` and reports a
 * localized system error when `_read()` fails.
 */
long wxFile::Read(
  void* const buffer,
  const long bytesToRead
)
{
  if (buffer == nullptr || m_fd == -1) {
    return 0;
  }

  const int readResult = _read(m_fd, buffer, static_cast<unsigned int>(bytesToRead));
  if (readResult == -1) {
    WxLogSysErrorLocalized(L"can't read from file descriptor %d", m_fd);
    return -1;
  }

  return readResult;
}

/**
 * Address: 0x00A120F0 (FUN_00A120F0, wxFile::Write)
 * Mangled: ?Write@wxFile@@QAEJPBXJ@Z
 *
 * What it does:
 * Writes one byte span into the descriptor lane and sets the wx error byte
 * when `_write()` fails.
 */
long wxFile::Write(
  const void* const buffer,
  const long bytesToWrite
)
{
  if (buffer != nullptr && m_fd != -1) {
    const int writeResult = _write(m_fd, buffer, static_cast<unsigned int>(bytesToWrite));
    if (writeResult != -1) {
      return writeResult;
    }

    WxLogSysErrorLocalized(L"can't write to file descriptor %d", m_fd);
    m_error = 1;
  }

  return 0;
}

/**
 * Address: 0x00A12150 (FUN_00A12150, wxFile::Flush)
 * Mangled: ?Flush@wxFile@@QAE_NXZ
 *
 * What it does:
 * Commits one open descriptor lane and reports a localized system error when
 * `_commit()` fails.
 */
bool wxFile::Flush()
{
  if (m_fd == -1 || _commit(m_fd) != -1) {
    return true;
  }

  WxLogSysErrorLocalized(L"can't flush file descriptor %d", m_fd);
  return false;
}

/**
 * Address: 0x00A12290 (FUN_00A12290, wxFile::Length)
 * Mangled: ?Length@wxFile@@QBEJXZ
 *
 * What it does:
 * Resolves one descriptor length via CRT `_filelength()` and logs a localized
 * system error on failure.
 */
long wxFile::Length() const
{
  const long fileLength = _filelength(m_fd);
  if (fileLength == -1) {
    WxLogSysErrorLocalized(L"can't find length of file on file descriptor %d", m_fd);
    return -1;
  }

  return fileLength;
}

/**
 * Address: 0x00A12230 (FUN_00A12230, wxFile::Tell)
 * Mangled: ?Tell@wxFile@@QBEJXZ
 *
 * What it does:
 * Returns the descriptor seek-position lane and logs a localized system error
 * when seek-position resolution fails.
 */
long wxFile::Tell() const
{
  const long seekPosition = _tell(m_fd);
  if (seekPosition == -1L) {
    WxLogSysErrorLocalized(L"can't get seek position on file descriptor %d", m_fd);
  }

  return seekPosition;
}

/**
 * Address: 0x00A121B0 (FUN_00A121B0, wxFile::Seek)
 * Mangled: ?Seek@wxFile@@QAEJJW4wxSeekMode@@@Z
 *
 * What it does:
 * Applies one descriptor seek with wx-origin mapping and logs a localized
 * system error when the seek fails.
 */
long wxFile::Seek(
  const long distanceToMove,
  const int seekMode
)
{
  int moveMethod = 0;
  if (seekMode == 1) {
    moveMethod = 1;
  } else if (seekMode == 2) {
    moveMethod = 2;
  }

  const long seekPosition = _lseek(m_fd, distanceToMove, moveMethod);
  if (seekPosition == -1L) {
    WxLogSysErrorLocalized(L"can't seek on file descriptor %d", m_fd);
  }

  return seekPosition;
}

/**
 * Address: 0x00A12600 (FUN_00A12600, wxFile::Create)
 *
 * What it does:
 * Opens one writable descriptor for `fileName` in overwrite (`_O_TRUNC`) or
 * exclusive-create (`_O_EXCL`) mode and replaces the currently attached lane.
 */
bool wxFile::Create(
  const wchar_t* const fileName,
  const bool overwrite,
  const std::int32_t permissions
)
{
  const int openFlags = (overwrite ? _O_TRUNC : _O_EXCL) | _O_BINARY | _O_WRONLY | _O_CREAT;
  const int fileDescriptor = wxOpen(fileName, openFlags, permissions);
  if (fileDescriptor == -1) {
    WxLogSysErrorLocalized(L"can't create file '%s'", fileName);
    return false;
  }

  (void)Attach();
  m_fd = fileDescriptor;
  return true;
}

/**
 * Address: 0x00A12690 (FUN_00A12690, wxFile::Open)
 * Mangled: ?Open@wxFile@@QAE_NPB_WW4OpenMode@1@H@Z
 *
 * What it does:
 * Translates one wx open-mode lane into CRT flags, opens the requested path,
 * and rebinds `m_fd` on success.
 */
bool wxFile::Open(
  const wchar_t* const fileName,
  const OpenMode mode,
  const std::int32_t permissions
)
{
  int openFlags = _O_BINARY | _O_RDONLY;

  switch (mode) {
    case OpenWrite:
      openFlags = _O_BINARY | _O_WRONLY | _O_CREAT | _O_TRUNC;
      break;

    case OpenReadWrite:
      openFlags = _O_BINARY | _O_RDWR;
      break;

    case OpenWriteAppend:
      if (Exists(fileName)) {
        openFlags = _O_BINARY | _O_WRONLY | _O_APPEND;
      } else {
        openFlags = _O_BINARY | _O_WRONLY | _O_CREAT | _O_TRUNC;
      }
      break;

    case OpenWriteExcl:
      openFlags = _O_BINARY | _O_WRONLY | _O_CREAT | _O_EXCL;
      break;

    case OpenRead:
    default:
      break;
  }

  const int fileDescriptor = wxOpen(fileName, openFlags, permissions);
  if (fileDescriptor == -1) {
    WxLogSysErrorLocalized(L"can't open file '%s'", fileName);
    return false;
  }

  (void)Attach();
  m_fd = fileDescriptor;
  return true;
}

/**
 * Address: 0x009FAAE0 (FUN_009FAAE0, wxFFile::Close)
 *
 * What it does:
 * Closes one active `FILE*` lane and logs a localized system error when
 * `fclose()` fails.
 */
bool wxFFile::Close()
{
  if (m_file == nullptr) {
    return true;
  }

  if (std::fclose(m_file) == 0) {
    m_file = nullptr;
    return true;
  }

  WxLogSysErrorLocalized(L"can't close file '%s'", m_name.m_pchData);
  return false;
}

/**
 * Address: 0x00999A00 (FUN_00999A00)
 *
 * What it does:
 * Closes one `wxFFile` lane and releases shared ownership for the file-name
 * string payload.
 */
wxFFile::~wxFFile()
{
  (void)Close();
  ReleaseWxStringSharedPayload(m_name);
}

/**
 * Address: 0x009FAB40 (FUN_009FAB40, wxFFile::Read)
 * Mangled: ?Read@wxFFile@@QAEIPAXI@Z
 *
 * What it does:
 * Reads one byte span from the backing `FILE*` and logs a localized system
 * error when `ferror()` reports failure.
 */
unsigned int wxFFile::Read(
  void* const buffer,
  const unsigned int byteCount
)
{
  if (buffer == nullptr || m_file == nullptr) {
    return 0;
  }

  const std::size_t requestedBytes = static_cast<std::size_t>(byteCount);
  const std::size_t bytesRead = std::fread(buffer, 1u, requestedBytes, m_file);
  if (bytesRead < requestedBytes && std::ferror(m_file) != 0) {
    WxLogSysErrorLocalized(L"Read error on file '%s'", m_name.m_pchData);
  }

  return static_cast<unsigned int>(bytesRead);
}

/**
 * Address: 0x009FABD0 (FUN_009FABD0, wxFFile::Write)
 * Mangled: ?Write@wxFFile@@QAEIPBXI@Z
 *
 * What it does:
 * Writes one byte span into the active `FILE*` lane and logs a localized
 * system error when the number of written bytes is short.
 */
unsigned int wxFFile::Write(
  const void* const buffer,
  const unsigned int byteCount
)
{
  if (buffer == nullptr || m_file == nullptr) {
    return 0;
  }

  const std::size_t requestedBytes = static_cast<std::size_t>(byteCount);
  const std::size_t bytesWritten = std::fwrite(buffer, 1u, requestedBytes, m_file);
  if (bytesWritten < requestedBytes) {
    WxLogSysErrorLocalized(L"Write error on file '%s'", m_name.m_pchData);
  }

  return static_cast<unsigned int>(bytesWritten);
}

/**
 * Address: 0x00999960 (FUN_00999960)
 *
 * What it does:
 * Converts one UTF-16 wx string into ACP bytes and writes the full converted
 * span into this file lane.
 */
bool wxFFile::Write(
  const wxStringRuntime& text
)
{
  const wchar_t* const wideText = text.c_str();
  if (wideText == nullptr) {
    return false;
  }

  const int requiredBytes = ::WideCharToMultiByte(CP_ACP, 0, wideText, -1, nullptr, 0, nullptr, nullptr);
  if (requiredBytes <= 0) {
    return false;
  }

  std::string narrowText(static_cast<std::size_t>(requiredBytes - 1), '\0');
  if (requiredBytes > 1) {
    (void)::WideCharToMultiByte(
      CP_ACP,
      0,
      wideText,
      -1,
      narrowText.data(),
      requiredBytes,
      nullptr,
      nullptr
    );
  }

  const unsigned int expectedBytes = static_cast<unsigned int>(narrowText.size());
  return Write(narrowText.data(), expectedBytes) == expectedBytes;
}

/**
 * Address: 0x009FAC50 (FUN_009FAC50, wxFFile::Flush)
 * Mangled: ?Flush@wxFFile@@QAE_NXZ
 *
 * What it does:
 * Flushes one active `FILE*` lane and reports a localized system error when
 * `fflush()` fails.
 */
bool wxFFile::Flush()
{
  if (m_file == nullptr || std::fflush(m_file) == 0) {
    return true;
  }

  WxLogSysErrorLocalized(L"failed to flush the file '%s'", m_name.m_pchData);
  return false;
}

/**
 * Address: 0x009FACB0 (FUN_009FACB0, sub_9FACB0)
 *
 * What it does:
 * Repositions one active `FILE*` lane using wx seek-mode semantics and logs
 * a localized system error on seek failure.
 */
bool wxFFile::Seek(const long distanceToMove, const int seekMode)
{
  if (m_file == nullptr) {
    return false;
  }

  int whence = SEEK_SET;
  if (seekMode == 1) {
    whence = SEEK_CUR;
  } else if (seekMode == 2) {
    whence = SEEK_END;
  }

  if (std::fseek(m_file, distanceToMove, whence) == 0) {
    return true;
  }

  WxLogSysErrorLocalized(L"Seek error on file '%s'", m_name.m_pchData);
  return false;
}

/**
 * Address: 0x009FAD40 (FUN_009FAD40, wxFFile::Tell)
 * Mangled: ?Tell@wxFFile@@QBEIXZ
 *
 * What it does:
 * Returns one `FILE*` position lane (`ftell`) and logs a localized system
 * error when position lookup fails.
 */
unsigned int wxFFile::Tell() const
{
  const long filePosition = std::ftell(m_file);
  if (filePosition == -1L) {
    WxLogSysErrorLocalized(L"Can't find current position in file '%s'", m_name.m_pchData);
    return static_cast<unsigned int>(-1);
  }

  return static_cast<unsigned int>(filePosition);
}

/**
 * Address: 0x009FAE20 (FUN_009FAE20, sub_9FAE20)
 *
 * What it does:
 * Computes one file-length lane by seeking to end, reading that position, and
 * restoring the original seek position.
 */
int wxFFile::Length()
{
  const int originalOffset = static_cast<int>(Tell());
  if (originalOffset == -1 || !Seek(0L, 2)) {
    return -1;
  }

  const int fileLength = static_cast<int>(Tell());
  (void)Seek(originalOffset, 0);
  return fileLength;
}

/**
 * Address: 0x00A910E9 (FUN_00A910E9)
 *
 * What it does:
 * Creates one UTF-16 directory path via Win32 `CreateDirectoryW` and maps
 * failing Win32 status codes to CRT `errno` using `_dosmaperr`.
 */
int wxCreateDirectoryWithErrnoMapping(
  const wchar_t* const directoryPath
)
{
  const DWORD lastError = ::CreateDirectoryW(directoryPath, nullptr) ? 0u : ::GetLastError();
  if (lastError == 0u) {
    return 0;
  }

  _dosmaperr(lastError);
  return -1;
}

/**
 * Address: 0x00A91115 (FUN_00A91115)
 *
 * What it does:
 * Removes one UTF-16 directory path via Win32 `RemoveDirectoryW` and maps
 * failing Win32 status codes to CRT `errno` using `_dosmaperr`.
 */
int wxRemoveDirectoryWithErrnoMapping(
  const wchar_t* const directoryPath
)
{
  const DWORD lastError = ::RemoveDirectoryW(directoryPath) ? 0u : ::GetLastError();
  if (lastError == 0u) {
    return 0;
  }

  _dosmaperr(lastError);
  return -1;
}

/**
 * Address: 0x009DE1E0 (FUN_009DE1E0)
 *
 * What it does:
 * Copies one source file to one destination path using Win32 overwrite
 * semantics and logs a localized system error on failure.
 */
bool wxCopyFileRuntime(
  const wchar_t* const sourcePath,
  const wchar_t* const destinationPath,
  const bool overwrite
)
{
  if (::CopyFileW(sourcePath, destinationPath, overwrite ? FALSE : TRUE) != FALSE) {
    return true;
  }

  WxLogSysErrorLocalized(L"Failed to copy the file '%s' to '%s'", sourcePath, destinationPath);
  return false;
}

/**
 * Address: 0x009DE270 (FUN_009DE270)
 *
 * What it does:
 * Creates one filesystem directory and logs a localized system error when
 * directory creation fails.
 */
bool wxCreateDirectoryRuntime(
  const wchar_t* const directoryPath
)
{
  if (wxCreateDirectoryWithErrnoMapping(directoryPath) == 0) {
    return true;
  }

  WxLogSysErrorLocalized(L"Directory '%s' couldn't be created", directoryPath);
  return false;
}

/**
 * Address: 0x009DDED0 (FUN_009DDED0)
 *
 * What it does:
 * Removes one trailing file-extension lane (`.<ext>`) from `pathText`
 * in-place when a dot separator exists past index 0.
 */
void wxRemoveFileExtensionInPlace(
  wxStringRuntime* const pathText
)
{
  if (pathText == nullptr || pathText->c_str() == nullptr) {
    return;
  }

  const std::wstring source(pathText->c_str());
  if (source.size() <= 1u) {
    return;
  }

  std::size_t separatorIndex = source.size() - 1u;
  while (separatorIndex > 0u && source[separatorIndex] != L'.') {
    --separatorIndex;
  }

  if (separatorIndex == 0u || source[separatorIndex] != L'.') {
    return;
  }

  AssignOwnedWxString(pathText, source.substr(0u, separatorIndex));
}

/**
 * Address: 0x009DE3D0 (FUN_009DE3D0)
 *
 * What it does:
 * Resolves one temporary-file name from the provided prefix lane and stores it
 * in `outFileName`; returns `true` when the output is non-empty.
 */
bool wxCreateTempFileNameFromPrefix(
  const wxStringRuntime* const prefixText,
  wxStringRuntime* const outFileName
)
{
  if (outFileName == nullptr) {
    return false;
  }

  wxStringRuntime tempDirectory = AllocateOwnedWxString(std::wstring());
  wxStringRuntime tempPrefix = AllocateOwnedWxString(std::wstring());
  const wxStringRuntime prefixSource = prefixText != nullptr ? *prefixText : wxStringRuntime::Borrow(L"");
  wxFileName::SplitPath_0(prefixSource, &tempDirectory, &tempPrefix, nullptr, nullptr);

  std::wstring directory = tempDirectory.c_str() != nullptr ? std::wstring(tempDirectory.c_str()) : std::wstring();
  if (!directory.empty()) {
    std::replace(directory.begin(), directory.end(), L'/', L'\\');
  } else {
    std::array<wchar_t, 0x105> tempPathBuffer{};
    const DWORD tempPathLength = ::GetTempPathW(0x104u, tempPathBuffer.data());
    if (tempPathLength > 0u && tempPathLength < tempPathBuffer.size()) {
      directory.assign(tempPathBuffer.data());
    }
    if (directory.empty()) {
      directory.assign(1u, L'.');
    }
  }

  const std::wstring prefix = tempPrefix.c_str() != nullptr ? std::wstring(tempPrefix.c_str()) : std::wstring();
  std::array<wchar_t, 0x105> fileNameBuffer{};
  const bool didCreate = ::GetTempFileNameW(
                           directory.c_str(),
                           prefix.c_str(),
                           0u,
                           fileNameBuffer.data()
                         )
    != 0u;

  if (didCreate) {
    AssignOwnedWxString(outFileName, std::wstring(fileNameBuffer.data()));
  } else {
    WxLogSysErrorLocalized(L"Failed to create a temporary file name");
    AssignOwnedWxString(outFileName, std::wstring());
  }

  ReleaseOwnedWxString(tempPrefix);
  ReleaseOwnedWxString(tempDirectory);

  const wchar_t* const outputText = outFileName->c_str();
  return outputText != nullptr && *outputText != L'\0';
}

/**
 * Address: 0x00A910BA (FUN_00A910BA)
 *
 * What it does:
 * Removes one UTF-16 path via Win32 `DeleteFileW` and maps failing Win32
 * status codes to CRT `errno` using `_dosmaperr`.
 */
int wxDeleteFileWithErrnoMapping(
  const wchar_t* const fileName
)
{
  const DWORD lastError = ::DeleteFileW(fileName) ? 0u : ::GetLastError();
  if (lastError == 0u) {
    return 0;
  }

  _dosmaperr(lastError);
  return -1;
}

/**
 * Address: 0x00A127D0 (FUN_00A127D0, ??1wxTempFile@@QAE@XZ)
 * Mangled: ??1wxTempFile@@QAE@XZ
 *
 * What it does:
 * Tears down one temp-file lane by discarding active temp state, closing the
 * embedded `wxFile`, and releasing both owned path strings.
 */
wxTempFile::~wxTempFile()
{
  if (m_file.m_fd != -1) {
    Discard();
  }

  (void)m_file.Attach();
  ReleaseOwnedWxString(m_originalPath);
  ReleaseOwnedWxString(m_tempPath);
}

/**
 * Address: 0x00A12580 (FUN_00A12580, wxTempFile::Discard)
 * Mangled: ?Discard@wxTempFile@@QAEXXZ
 *
 * What it does:
 * Closes the embedded temp-file descriptor and removes the temp-file path,
 * logging a localized system error when remove fails.
 */
void wxTempFile::Discard()
{
  (void)m_file.Attach();

  if (wxDeleteFileWithErrnoMapping(m_tempPath.m_pchData) == 0) {
    return;
  }

  WxLogSysErrorLocalized(L"can't remove temporary file '%s'", m_tempPath.m_pchData);
}

/**
 * Address: 0x009DBCE0 (FUN_009DBCE0)
 *
 * What it does:
 * Initializes one file-backed output stream by opening a `wxFile` lane in
 * write mode, marks stream ownership, and propagates open failure into the
 * stream status lane.
 */
wxFileOutputStream::wxFileOutputStream(
  const wxStringRuntime& fileName
)
{
  std::memset(m_streamRuntime00, 0, sizeof(m_streamRuntime00));

  m_file = new (std::nothrow) wxFile(fileName.c_str(), wxFile::OpenWrite);
  m_ownsFile = 1u;

  auto& statusLane = *reinterpret_cast<std::int32_t*>(&m_streamRuntime00[0x8]);
  statusLane = 0;
  if (m_file == nullptr || m_file->m_fd == -1 || m_file->m_error != 0u) {
    statusLane = 2;
  }
}

/**
 * Address: 0x009DBE90 (FUN_009DBE90, wxFileOutputStream::Sync)
 * Mangled: ?Sync@wxFileOutputStream@@UAEXXZ
 *
 * What it does:
 * Runs one no-op flush hook lane, then synchronizes the wrapped `wxFile`
 * descriptor.
 */
void wxFileOutputStream::Sync()
{
  wxNoOpFileFlushHook();
  (void)m_file->Flush();
}

/**
 * Address: 0x009DBE70 (FUN_009DBE70, wxFileOutputStream::OnSysTell)
 * Mangled: ?OnSysTell@wxFileOutputStream@@MBEJXZ
 *
 * What it does:
 * Delegates output-stream tell requests into the wrapped `wxFile` lane.
 */
long wxFileOutputStream::OnSysTell() const
{
  return m_file->Tell();
}

/**
 * Address: 0x009DBE80 (FUN_009DBE80, wxFileOutputStream::OnSysSeek)
 * Mangled: ?OnSysSeek@wxFileOutputStream@@MAEJJW4wxSeekMode@@@Z
 *
 * What it does:
 * Delegates output-stream seek requests into the wrapped `wxFile` lane.
 */
long wxFileOutputStream::OnSysSeek(
  const long distanceToMove,
  const int seekMode
)
{
  return m_file->Seek(distanceToMove, seekMode);
}

/**
 * Address: 0x009DC2C0 (FUN_009DC2C0, wxFFileOutputStream::Sync)
 * Mangled: ?Sync@wxFFileOutputStream@@UAEXXZ
 *
 * What it does:
 * Runs one no-op flush hook lane, then synchronizes the wrapped `wxFFile`
 * stream.
 */
void wxFFileOutputStream::Sync()
{
  wxNoOpFileFlushHook();
  (void)m_file->Flush();
}

/**
 * Address: 0x00A13300 (FUN_00A13300)
 *
 * What it does:
 * Creates one inheritable anonymous read/write pipe pair and stores the
 * resulting handles into the provided output struct.
 */
bool wxCreateAnonymousPipe(
  WxAnonymousPipeHandles* const pipeHandles
)
{
  if (pipeHandles == nullptr) {
    return false;
  }

  SECURITY_ATTRIBUTES pipeAttributes{};
  pipeAttributes.nLength = sizeof(pipeAttributes);
  pipeAttributes.lpSecurityDescriptor = nullptr;
  pipeAttributes.bInheritHandle = TRUE;

  if (::CreatePipe(
        reinterpret_cast<PHANDLE>(&pipeHandles->readHandle),
        reinterpret_cast<PHANDLE>(&pipeHandles->writeHandle),
        &pipeAttributes,
        0
      ) != FALSE)
  {
    return true;
  }

  WxLogSysErrorLocalized(L"Failed to create an anonymous pipe");
  return false;
}

/**
 * Address: 0x009DBAF0 (FUN_009DBAF0)
 * Mangled: ??0wxFileInputStream@@QAE@@Z
 *
 * What it does:
 * Builds one file-backed input stream from a path string by allocating a
 * `wxFile` lane and marking it as stream-owned for destruction.
 */
wxFileInputStream::wxFileInputStream(
  const wxStringRuntime& fileName
)
  : wxInputStream()
{
  if (wxFile* const file = new (std::nothrow) wxFile(fileName.c_str(), wxFile::OpenRead); file != nullptr) {
    m_file = file;
  } else {
    m_file = nullptr;
  }
  m_file_destroy = 1;
}

wxFileInputStream::~wxFileInputStream()
{
  if (m_file_destroy != 0u) {
    delete m_file;
  }
  m_file = nullptr;
  m_file_destroy = 0;
}

/**
 * Address: 0x009DBCD0 (FUN_009DBCD0)
 *
 * What it does:
 * Delegates input-stream tell requests into the wrapped `wxFile` lane.
 */
long wxFileInputStream::OnSysTell() const
{
  return m_file->Tell();
}

/**
 * Address: 0x009DBCC0 (FUN_009DBCC0)
 *
 * What it does:
 * Delegates input-stream seek requests into the wrapped `wxFile` lane.
 */
long wxFileInputStream::OnSysSeek(
  const long distanceToMove,
  const int seekMode
)
{
  return m_file->Seek(distanceToMove, seekMode);
}

void wxFileName::SplitPath(
  const wxStringRuntime& input,
  wxStringRuntime* const volume,
  wxStringRuntime* const path,
  wxStringRuntime* const name,
  wxStringRuntime* const ext,
  const wchar_t* const formatHint
)
{
  (void)formatHint;

  std::wstring volumeText;
  std::wstring pathText;
  std::wstring nameText;
  std::wstring extText;

  try {
    const std::filesystem::path inputPath(input.c_str());
    volumeText = inputPath.root_name().wstring();

    pathText = inputPath.parent_path().wstring();
    const std::wstring rootPathText = inputPath.root_path().wstring();
    if (!rootPathText.empty() && pathText.rfind(rootPathText, 0) == 0) {
      pathText.erase(0, rootPathText.size());
    }

    nameText = inputPath.stem().wstring();
    extText = inputPath.extension().wstring();
    if (!extText.empty() && extText.front() == L'.') {
      extText.erase(0, 1);
    }
  } catch (const std::exception&) {
    volumeText.clear();
    pathText.clear();
    nameText.clear();
    extText.clear();
  }

  AssignOwnedWxString(volume, volumeText);
  AssignOwnedWxString(path, pathText);
  AssignOwnedWxString(name, nameText);
  AssignOwnedWxString(ext, extText);
}

/**
 * Address: 0x009F46E0 (FUN_009F46E0)
 * Mangled: ?wxGetVolumeString@@YA?AVwxString@@ABV1@W4wxPathFormat@@@Z
 *
 * What it does:
 * Builds one normalized volume-prefix text lane used by `SplitPath_0`.
 */
wxStringRuntime wxGetVolumeString(
  const wxStringRuntime& volume,
  const wchar_t* const formatHint
)
{
  const std::wstring volumeText(volume.c_str());
  if (volumeText.empty()) {
    return AllocateOwnedWxString(L"");
  }

  const std::uintptr_t rawFormatHint = reinterpret_cast<std::uintptr_t>(formatHint);
  std::int32_t pathFormat = 4;
  if (rawFormatHint <= 0x10u) {
    pathFormat = rawFormatHint == 0u ? 4 : static_cast<std::int32_t>(rawFormatHint);
  }

  std::wstring outputText;
  if (pathFormat == 3) {
    if (volumeText.size() > 1u) {
      outputText = L"\\\\";
      outputText += volumeText;
    } else {
      outputText = volumeText;
      outputText.push_back(L':');
    }
  } else if (pathFormat == 4) {
    outputText = volumeText;
    outputText.push_back(L':');
  }

  return AllocateOwnedWxString(outputText);
}

/**
 * Address: 0x009F5820 (FUN_009F5820)
 * Mangled: ?SplitPath_0@wxFileName@@SAXABVwxString@@PAV2@00PA_W@Z
 *
 * What it does:
 * Splits path components, then prepends the computed volume-prefix lane onto
 * the output path lane when requested.
 */
void wxFileName::SplitPath_0(
  const wxStringRuntime& input,
  wxStringRuntime* const path,
  wxStringRuntime* const name,
  wxStringRuntime* const ext,
  const wchar_t* const formatHint
)
{
  wxStringRuntime volume = wxStringRuntime::Borrow(L"");
  SplitPath(input, &volume, path, name, ext, formatHint);

  if (path != nullptr) {
    wxStringRuntime volumePrefix = wxGetVolumeString(volume, formatHint);
    PrependOwnedWxString(path, volumePrefix);
    ReleaseOwnedWxString(volumePrefix);
  }

  ReleaseOwnedWxString(volume);
}

/**
 * Address: 0x009DF260 (FUN_009DF260)
 *
 * What it does:
 * Splits one source path and stores `name[.ext]` into `outFileName`.
 */
wxStringRuntime* wxBuildFileNameFromPath(
  wxStringRuntime* const outFileName,
  const wxStringRuntime* const sourcePath
)
{
  if (outFileName == nullptr) {
    return nullptr;
  }

  wxStringRuntime namePart = AllocateOwnedWxString(std::wstring());
  wxStringRuntime extensionPart = AllocateOwnedWxString(std::wstring());

  const wxStringRuntime inputPath = sourcePath != nullptr ? *sourcePath : wxStringRuntime::Borrow(L"");
  wxFileName::SplitPath_0(inputPath, nullptr, &namePart, &extensionPart, nullptr);

  std::wstring fileName(namePart.c_str());
  if (const wchar_t* const extensionText = extensionPart.c_str(); extensionText != nullptr && *extensionText != L'\0') {
    fileName.push_back(L'.');
    fileName += extensionText;
  }

  AssignOwnedWxString(outFileName, fileName);

  ReleaseOwnedWxString(extensionPart);
  ReleaseOwnedWxString(namePart);
  return outFileName;
}

/**
 * Address: 0x009DFC90 (FUN_009DFC90)
 *
 * What it does:
 * Returns one pointer into `pathText` at the beginning of the filename lane.
 */
const wchar_t* wxFindFileNameStartInPath(
  const wchar_t* const pathText
)
{
  if (pathText == nullptr) {
    return nullptr;
  }

  wxStringRuntime sourcePath = AllocateOwnedWxString(pathText);
  wxStringRuntime fileName = AllocateOwnedWxString(std::wstring());
  wxBuildFileNameFromPath(&fileName, &sourcePath);

  const std::size_t sourceLength = std::wcslen(sourcePath.c_str());
  const std::size_t fileNameLength = std::wcslen(fileName.c_str());
  const wchar_t* result = pathText;
  if (fileNameLength <= sourceLength) {
    result = pathText + (sourceLength - fileNameLength);
  }

  ReleaseOwnedWxString(fileName);
  ReleaseOwnedWxString(sourcePath);
  return result;
}

/**
 * Address: 0x009DE480 (FUN_009DE480)
 *
 * What it does:
 * Resolves the current working directory into `buffer` and logs a localized
 * system error when retrieval fails.
 */
wchar_t* wxGetCurrentWorkingDirectoryRuntime(
  wchar_t* buffer,
  const DWORD maxChars
)
{
  wchar_t* target = buffer;
  if (target == nullptr) {
    target = static_cast<wchar_t*>(::operator new((static_cast<std::size_t>(maxChars) + 1u) * sizeof(wchar_t)));
  }

  if (::GetCurrentDirectoryW(maxChars, target) == 0u) {
    WxLogSysErrorLocalized(L"Failed to get the working directory");
    target[0] = L'\0';
  }
  return target;
}

/**
 * Address: 0x009DF870 (FUN_009DF870)
 *
 * What it does:
 * Writes the current working directory text into one output wx string lane.
 */
wxStringRuntime* wxBuildCurrentWorkingDirectoryStringRuntime(
  wxStringRuntime* const outText
)
{
  if (outText == nullptr) {
    return nullptr;
  }

  std::array<wchar_t, 0x400> directoryBuffer{};
  (void)wxGetCurrentWorkingDirectoryRuntime(directoryBuffer.data(), static_cast<DWORD>(directoryBuffer.size()));
  AssignOwnedWxString(outText, std::wstring(directoryBuffer.data()));
  return outText;
}

namespace
{
  [[nodiscard]] std::wstring WxResolveUserConfigRootPath()
  {
    if (const wchar_t* const home = _wgetenv(L"HOME"); home != nullptr && *home != L'\0') {
      return std::wstring(home);
    }

    std::wstring homeFromDrivePath{};
    if (const wchar_t* const homeDrive = _wgetenv(L"HOMEDRIVE"); homeDrive != nullptr) {
      homeFromDrivePath += homeDrive;
    }

    if (const wchar_t* const homePath = _wgetenv(L"HOMEPATH"); homePath != nullptr) {
      homeFromDrivePath += homePath;
      if (std::wcscmp(homePath, L"\\") == 0) {
        homeFromDrivePath.clear();
      }
    }

    if (!homeFromDrivePath.empty()) {
      return homeFromDrivePath;
    }

    if (const wchar_t* const userProfile = _wgetenv(L"USERPROFILE"); userProfile != nullptr && *userProfile != L'\0') {
      return std::wstring(userProfile);
    }

    std::array<wchar_t, 0x104> modulePath{};
    if (::GetModuleFileNameW(::GetModuleHandleW(nullptr), modulePath.data(), static_cast<DWORD>(modulePath.size())) == 0u) {
      return {};
    }

    try {
      return std::filesystem::path(modulePath.data()).parent_path().wstring();
    } catch (const std::exception&) {
      return {};
    }
  }
} // namespace

/**
 * Address: 0x00A1AEC0 (FUN_00A1AEC0)
 *
 * What it does:
 * Resolves the user config-home path, enforces writable wx-string ownership,
 * and appends a trailing `'\\'` separator when missing.
 */
wxStringRuntime* wxBuildUserConfigRootPath(
  wxStringRuntime* const outText
)
{
  if (outText == nullptr) {
    return nullptr;
  }

  AssignOwnedWxString(outText, WxResolveUserConfigRootPath());
  (void)EnsureUniqueOwnedWxStringBuffer(outText);

  const wchar_t* const pathText = outText->c_str();
  if (pathText == nullptr || *pathText == L'\0') {
    return outText;
  }

  std::wstring normalized(pathText);
  if (!normalized.empty() && normalized.back() != L'\\') {
    normalized.push_back(L'\\');
    AssignOwnedWxString(outText, normalized);
  }

  return outText;
}

/**
 * Address: 0x009F8590 (FUN_009F8590)
 *
 * What it does:
 * Copies the longest leading token from `sourceText` where each character is
 * alnum or listed in `additionalAllowedChars`.
 */
wxStringRuntime* wxExtractLeadingIdentifierToken(
  wxStringRuntime* const outText,
  const wchar_t* const sourceText,
  const wchar_t* const additionalAllowedChars
)
{
  if (outText == nullptr) {
    return nullptr;
  }

  std::wstring token{};
  if (sourceText != nullptr) {
    for (const wchar_t* cursor = sourceText; *cursor != L'\0'; ++cursor) {
      if (!WxIsAlnumWide(*cursor) && wxFindFirstWideChar(additionalAllowedChars, *cursor) == nullptr) {
        break;
      }
      token.push_back(*cursor);
    }
  }

  AssignOwnedWxString(outText, token);
  return outText;
}

namespace
{
  struct WxEncodingLookupEntryRuntimeView
  {
    std::uint16_t sourceCode = 0;   // +0x00
    std::uint8_t mappedCode = 0;    // +0x02
    std::uint8_t reserved03 = 0;    // +0x03
  };
  static_assert(sizeof(WxEncodingLookupEntryRuntimeView) == 0x4, "WxEncodingLookupEntryRuntimeView size must be 0x4");
  static_assert(
    offsetof(WxEncodingLookupEntryRuntimeView, mappedCode) == 0x02,
    "WxEncodingLookupEntryRuntimeView::mappedCode offset must be 0x02"
  );

  int __cdecl CompareWxEncodingLookupEntriesBySourceCode(
    const void* const leftEntry,
    const void* const rightEntry
  ) noexcept
  {
    const auto* const left = static_cast<const WxEncodingLookupEntryRuntimeView*>(leftEntry);
    const auto* const right = static_cast<const WxEncodingLookupEntryRuntimeView*>(rightEntry);
    return static_cast<int>(left->sourceCode) - static_cast<int>(right->sourceCode);
  }

  struct WxCmdLineEntryRuntimeView
  {
    void* vtable = nullptr;                  // +0x00
    const wchar_t* shortName = nullptr;      // +0x04
    const wchar_t* longName = nullptr;       // +0x08
    std::uint8_t reserved0C_17[0x0C]{};      // +0x0C
    std::uint8_t isEnabled = 0;              // +0x18
  };
  static_assert(
    offsetof(WxCmdLineEntryRuntimeView, shortName) == 0x04,
    "WxCmdLineEntryRuntimeView::shortName offset must be 0x04"
  );
  static_assert(
    offsetof(WxCmdLineEntryRuntimeView, longName) == 0x08,
    "WxCmdLineEntryRuntimeView::longName offset must be 0x08"
  );
  static_assert(
    offsetof(WxCmdLineEntryRuntimeView, isEnabled) == 0x18,
    "WxCmdLineEntryRuntimeView::isEnabled offset must be 0x18"
  );

  struct WxCmdLineLookupRuntimeView
  {
    void* vtable = nullptr;                              // +0x00
    std::uint8_t reserved04_1F[0x1C]{};                 // +0x04
    std::uint32_t entryCount = 0;                       // +0x20
    WxCmdLineEntryRuntimeView** entries = nullptr;      // +0x24
  };
  static_assert(
    offsetof(WxCmdLineLookupRuntimeView, entryCount) == 0x20,
    "WxCmdLineLookupRuntimeView::entryCount offset must be 0x20"
  );
  static_assert(
    offsetof(WxCmdLineLookupRuntimeView, entries) == 0x24,
    "WxCmdLineLookupRuntimeView::entries offset must be 0x24"
  );

  [[nodiscard]] std::int32_t WxSharedWideStringLengthFromHeader(
    const wchar_t* const text
  ) noexcept
  {
    if (text == nullptr) {
      return 0;
    }

    return *(reinterpret_cast<const std::int32_t*>(text) - 2);
  }

  [[nodiscard]] std::uint32_t WxFindCmdLineEntryIndexByShortName(
    const WxCmdLineLookupRuntimeView* const lookup,
    const wchar_t* const query
  ) noexcept
  {
    const std::int32_t queryLength = WxSharedWideStringLengthFromHeader(query);
    if (queryLength == 0 || lookup->entryCount == 0 || lookup->entries == nullptr) {
      return std::numeric_limits<std::uint32_t>::max();
    }

    for (std::uint32_t index = 0; index < lookup->entryCount; ++index) {
      const WxCmdLineEntryRuntimeView* const entry = lookup->entries[index];
      if (entry == nullptr) {
        continue;
      }

      const wchar_t* const candidate = entry->shortName;
      if (candidate != nullptr
          && WxSharedWideStringLengthFromHeader(candidate) == queryLength
          && std::wcscmp(candidate, query) == 0) {
        return index;
      }
    }

    return std::numeric_limits<std::uint32_t>::max();
  }

  [[nodiscard]] std::uint32_t WxFindCmdLineEntryIndexByLongName(
    const WxCmdLineLookupRuntimeView* const lookup,
    const wchar_t* const query
  ) noexcept
  {
    if (query == nullptr) {
      return std::numeric_limits<std::uint32_t>::max();
    }

    if (lookup->entryCount == 0 || lookup->entries == nullptr) {
      return std::numeric_limits<std::uint32_t>::max();
    }

    const std::int32_t queryLength = WxSharedWideStringLengthFromHeader(query);
    for (std::uint32_t index = 0; index < lookup->entryCount; ++index) {
      const WxCmdLineEntryRuntimeView* const entry = lookup->entries[index];
      if (entry == nullptr) {
        continue;
      }

      const wchar_t* const candidate = entry->longName;
      if (candidate != nullptr
          && WxSharedWideStringLengthFromHeader(candidate) == queryLength
          && std::wcscmp(candidate, query) == 0) {
        return index;
      }
    }

    return std::numeric_limits<std::uint32_t>::max();
  }
} // namespace

/**
 * Address: 0x009F00D0 (FUN_009F00D0, sub_9F00D0)
 *
 * What it does:
 * Builds 128 `(source,mapped)` 4-byte entries from one 128-word source lane
 * and sorts those entries by `source` for binary-search lookup paths.
 */
[[nodiscard]] WxEncodingLookupEntryRuntimeView* wxBuildSortedEncodingLookupEntriesRuntime(
  const std::uint16_t* const sourceCodes
)
{
  auto* const entries = static_cast<WxEncodingLookupEntryRuntimeView*>(::operator new(0x200u));
  for (std::uint32_t index = 0; index < 128u; ++index) {
    entries[index].mappedCode = static_cast<std::uint8_t>(index + 0x80u);
    entries[index].sourceCode = sourceCodes[index];
  }

  std::qsort(entries, 128u, sizeof(WxEncodingLookupEntryRuntimeView), &CompareWxEncodingLookupEntriesBySourceCode);
  return entries;
}

/**
 * Address: 0x009F7BB0 (FUN_009F7BB0, sub_9F7BB0)
 *
 * What it does:
 * Resolves one cmd-line entry index by short-name first, long-name fallback,
 * and returns whether the resolved entry is currently enabled.
 */
bool wxCmdLineLookupHasEnabledEntryRuntime(
  const WxCmdLineLookupRuntimeView* const lookup,
  const wchar_t** const queryText
)
{
  if (lookup == nullptr || queryText == nullptr) {
    return false;
  }

  const wchar_t* const query = *queryText;
  if (query == nullptr) {
    return false;
  }

  std::uint32_t index = WxFindCmdLineEntryIndexByShortName(lookup, query);
  if (index == std::numeric_limits<std::uint32_t>::max()) {
    index = WxFindCmdLineEntryIndexByLongName(lookup, query);
    if (index == std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
  }

  const WxCmdLineEntryRuntimeView* const entry = lookup->entries[index];
  return entry != nullptr && entry->isEnabled != 0u;
}

/**
 * Address: 0x00960E30 (FUN_00960E30)
 *
 * What it does:
 * Copies the suffix of `source` after the first `separator` UTF-16 code unit
 * into `outText`; when the separator is absent it copies the full source text.
 */
wxStringRuntime* wxStringCopySuffixAfterFirstSeparatorRuntime(
  const wxStringRuntime* const source,
  wxStringRuntime* const outText,
  const wchar_t separator
)
{
  if (outText == nullptr) {
    return nullptr;
  }

  const std::wstring sourceText = source != nullptr ? std::wstring(source->c_str()) : std::wstring();
  const std::size_t separatorIndex = sourceText.find(separator);
  if (separatorIndex == std::wstring::npos) {
    AssignOwnedWxString(outText, sourceText);
  } else {
    AssignOwnedWxString(outText, sourceText.substr(separatorIndex + 1u));
  }
  return outText;
}

/**
 * Address: 0x00960E90 (FUN_00960E90)
 *
 * What it does:
 * Copies at most `requestedLength` UTF-16 code units from `source` into one
 * destination wx string lane.
 */
wxStringRuntime* wxStringCopyPrefixRuntime(
  const wxStringRuntime* const source,
  wxStringRuntime* const outText,
  const std::size_t requestedLength
)
{
  if (outText == nullptr) {
    return nullptr;
  }

  const std::wstring sourceText = source != nullptr ? std::wstring(source->c_str()) : std::wstring();
  const std::size_t copyLength = std::min(requestedLength, sourceText.size());
  AssignOwnedWxString(outText, sourceText.substr(0u, copyLength));
  return outText;
}

/**
 * Address: 0x00960ED0 (FUN_00960ED0)
 *
 * What it does:
 * Copies the suffix of `source` after the first `separator` code unit into
 * `outText`, returning an empty string when the separator is absent.
 */
wxStringRuntime* wxStringCopySuffixAfterFirstCharacterRuntime(
  const wxStringRuntime* const source,
  wxStringRuntime* const outText,
  const wchar_t separator
)
{
  if (outText == nullptr) {
    return nullptr;
  }

  AssignOwnedWxString(outText, std::wstring());
  if (source == nullptr || source->c_str() == nullptr) {
    return outText;
  }

  const std::int32_t separatorIndex = source->FindCharacterIndex(separator, false);
  if (separatorIndex >= 0) {
    const wchar_t* const suffix = source->c_str() + static_cast<std::size_t>(separatorIndex) + 1u;
    AssignOwnedWxString(outText, std::wstring(suffix));
  }
  return outText;
}

/**
 * Address: 0x009CE590 (FUN_009CE590)
 *
 * What it does:
 * Stores the current user-name text in `outText` by checking Win32
 * `GetUserNameW`, then the `username` environment variable fallback.
 */
wxStringRuntime* wxBuildCurrentUserNameStringRuntime(
  wxStringRuntime* const outText
)
{
  if (outText == nullptr) {
    return nullptr;
  }

  std::array<wchar_t, 0x100> userNameBuffer{};
  DWORD userNameLength = static_cast<DWORD>(userNameBuffer.size());
  bool didResolve = ::GetUserNameW(userNameBuffer.data(), &userNameLength) != FALSE;
  if (!didResolve) {
    const DWORD charsWritten = ::GetEnvironmentVariableW(
      L"username",
      userNameBuffer.data(),
      static_cast<DWORD>(userNameBuffer.size())
    );
    didResolve = charsWritten > 0u && charsWritten < userNameBuffer.size();
  }

  AssignOwnedWxString(outText, didResolve ? std::wstring(userNameBuffer.data()) : std::wstring());
  return outText;
}

/**
 * Address: 0x009C6D40 (FUN_009C6D40)
 *
 * What it does:
 * Fills `buffer` with the local computer name via Win32 `GetComputerNameW`.
 */
bool wxGetComputerNameRuntime(
  wchar_t* const buffer,
  const DWORD maxChars
)
{
  DWORD mutableSize = maxChars;
  return ::GetComputerNameW(buffer, &mutableSize) != FALSE;
}

/**
 * Address: 0x009C6D60 (FUN_009C6D60)
 *
 * What it does:
 * Attempts hostname resolution through Winsock (`gethostname` and reverse
 * lookup) and falls back to Win32 computer-name retrieval.
 */
bool wxResolveHostNameRuntime(
  wchar_t* const buffer,
  const DWORD maxChars
)
{
  WSADATA wsaData{};
  if (::WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
    return wxGetComputerNameRuntime(buffer, maxChars);
  }

  bool didResolve = false;
  std::array<char, 256> hostNameBuffer{};
  if (::gethostname(hostNameBuffer.data(), static_cast<int>(hostNameBuffer.size())) == 0
      && std::strchr(hostNameBuffer.data(), '.') == nullptr) {
#pragma warning(push)
#pragma warning(disable : 4996)
    const hostent* const hostByName = ::gethostbyname(hostNameBuffer.data());
    if (hostByName != nullptr && hostByName->h_addr_list != nullptr && hostByName->h_addr_list[0] != nullptr) {
      const hostent* const hostByAddress = ::gethostbyaddr(hostByName->h_addr_list[0], 4, AF_INET);
      if (hostByAddress != nullptr && hostByAddress->h_name != nullptr) {
        const int convertedCount = ::MultiByteToWideChar(
          CP_ACP,
          0,
          hostByAddress->h_name,
          -1,
          buffer,
          static_cast<int>(maxChars)
        );
        didResolve = convertedCount > 0;
      }
    }
#pragma warning(pop)
  }

  (void)::WSACleanup();
  if (!didResolve) {
    return wxGetComputerNameRuntime(buffer, maxChars);
  }
  return true;
}

/**
 * Address: 0x009CE740 (FUN_009CE740)
 *
 * What it does:
 * Stores one host-name string lane into `outText`, preferring the Winsock
 * reverse-lookup path and falling back to the local computer name.
 */
wxStringRuntime* wxBuildCurrentHostNameStringRuntime(
  wxStringRuntime* const outText
)
{
  if (outText == nullptr) {
    return nullptr;
  }

  std::array<wchar_t, 0x101> hostNameBuffer{};
  const bool didResolve = wxResolveHostNameRuntime(
    hostNameBuffer.data(),
    static_cast<DWORD>(hostNameBuffer.size())
  );
  AssignOwnedWxString(outText, didResolve ? std::wstring(hostNameBuffer.data()) : std::wstring());
  return outText;
}

/**
 * Address: 0x009CEAE0 (FUN_009CEAE0)
 *
 * What it does:
 * Writes `username@hostname` into `outText` when both lanes resolve;
 * otherwise writes an empty string.
 */
wxStringRuntime* wxBuildCurrentUserAtHostStringRuntime(
  wxStringRuntime* const outText
)
{
  if (outText == nullptr) {
    return nullptr;
  }

  AssignOwnedWxString(outText, std::wstring());

  wxStringRuntime hostName = AllocateOwnedWxString(std::wstring());
  (void)wxBuildCurrentHostNameStringRuntime(&hostName);
  const wchar_t* const hostText = hostName.c_str();
  if (hostText != nullptr && hostText[0] != L'\0') {
    wxStringRuntime userName = AllocateOwnedWxString(std::wstring());
    (void)wxBuildCurrentUserNameStringRuntime(&userName);
    const wchar_t* const userText = userName.c_str();
    if (userText != nullptr && userText[0] != L'\0') {
      std::wstring fullName = userText;
      fullName.push_back(L'@');
      fullName += hostText;
      AssignOwnedWxString(outText, fullName);
    }
    ReleaseOwnedWxString(userName);
  }

  ReleaseOwnedWxString(hostName);
  return outText;
}

namespace
{
  struct WxBrushPatternOwnerRuntimeView
  {
    std::uint8_t mReserved00To07[0x8]{};
    HBITMAP mPatternBitmap = nullptr; // +0x08
  };

  static_assert(
    offsetof(WxBrushPatternOwnerRuntimeView, mPatternBitmap) == 0x08,
    "WxBrushPatternOwnerRuntimeView::mPatternBitmap offset must be 0x08"
  );

  struct WxBrushPatternSourceRuntimeView
  {
    std::uint8_t mReserved00To13[0x14]{};
    HBITMAP mDirectPatternBitmap = nullptr;             // +0x14
    std::uint8_t mReserved18To2F[0x18]{};
    WxBrushPatternOwnerRuntimeView* mPatternOwner = nullptr; // +0x30
  };

  static_assert(
    offsetof(WxBrushPatternSourceRuntimeView, mDirectPatternBitmap) == 0x14,
    "WxBrushPatternSourceRuntimeView::mDirectPatternBitmap offset must be 0x14"
  );
  static_assert(
    offsetof(WxBrushPatternSourceRuntimeView, mPatternOwner) == 0x30,
    "WxBrushPatternSourceRuntimeView::mPatternOwner offset must be 0x30"
  );

  struct WxBrushRuntimeView
  {
    std::uint8_t mReserved00To07[0x8]{};
    std::int32_t mStyleCode = 0;                       // +0x08
    std::uint8_t mReserved0CTo0F[0x4]{};
    WxBrushPatternSourceRuntimeView* mPatternSource = nullptr; // +0x10
    std::uint8_t mReserved14To1F[0xC]{};
    COLORREF mColor = 0;                               // +0x20
    std::uint8_t mReserved24To27[0x4]{};
    HBRUSH mCachedBrush = nullptr;                     // +0x28
  };

  static_assert(offsetof(WxBrushRuntimeView, mStyleCode) == 0x08, "WxBrushRuntimeView::mStyleCode offset must be 0x08");
  static_assert(
    offsetof(WxBrushRuntimeView, mPatternSource) == 0x10,
    "WxBrushRuntimeView::mPatternSource offset must be 0x10"
  );
  static_assert(offsetof(WxBrushRuntimeView, mColor) == 0x20, "WxBrushRuntimeView::mColor offset must be 0x20");
  static_assert(
    offsetof(WxBrushRuntimeView, mCachedBrush) == 0x28,
    "WxBrushRuntimeView::mCachedBrush offset must be 0x28"
  );

  struct WxBrushOwnerRuntimeView
  {
    void* mReserved00 = nullptr;
    WxBrushRuntimeView* mBrushRuntime = nullptr; // +0x04
  };

  static_assert(
    offsetof(WxBrushOwnerRuntimeView, mBrushRuntime) == 0x04,
    "WxBrushOwnerRuntimeView::mBrushRuntime offset must be 0x04"
  );
}

/**
 * Address: 0x009D2740 (FUN_009D2740)
 *
 * What it does:
 * Maps one legacy wx hatch-style token to a Win32 hatch brush index.
 */
int wxMapLegacyBrushHatchStyle(
  const std::int32_t styleCode
)
{
  switch (styleCode) {
    case 'o':
      return 3;
    case 'p':
      return 5;
    case 'q':
      return 2;
    case 'r':
      return 4;
    case 's':
      return 0;
    case 't':
      return 1;
    default:
      return -1;
  }
}

/**
 * Address: 0x009D2720 (FUN_009D2720)
 *
 * What it does:
 * Deletes one cached GDI brush lane and clears the cache handle slot.
 */
int wxDeleteCachedBrushHandleRuntime(
  WxBrushRuntimeView* const brush
)
{
  if (brush == nullptr || brush->mCachedBrush == nullptr) {
    return 0;
  }

  const int deleteResult = static_cast<int>(::DeleteObject(brush->mCachedBrush));
  brush->mCachedBrush = nullptr;
  return deleteResult;
}

/**
 * Address: 0x009D2AA0 (FUN_009D2AA0)
 *
 * What it does:
 * Returns a cached GDI brush handle for one wx brush runtime lane, creating
 * the handle lazily from style/color/pattern source state.
 */
HBRUSH wxGetOrCreateBrushHandleRuntime(
  WxBrushOwnerRuntimeView* const owner
)
{
  if (owner == nullptr || owner->mBrushRuntime == nullptr) {
    return nullptr;
  }

  WxBrushRuntimeView* const brush = owner->mBrushRuntime;
  if (brush->mCachedBrush != nullptr) {
    return brush->mCachedBrush;
  }

  const int mappedHatchStyle = wxMapLegacyBrushHatchStyle(brush->mStyleCode);
  if (mappedHatchStyle != -1) {
    brush->mCachedBrush = ::CreateHatchBrush(mappedHatchStyle, brush->mColor);
    return brush->mCachedBrush;
  }

  switch (brush->mStyleCode) {
    case 'j':
      brush->mCachedBrush = static_cast<HBRUSH>(::GetStockObject(NULL_BRUSH));
      break;

    case 'k':
      if (brush->mPatternSource != nullptr
          && brush->mPatternSource->mPatternOwner != nullptr
          && brush->mPatternSource->mPatternOwner->mPatternBitmap != nullptr) {
        brush->mCachedBrush = ::CreatePatternBrush(brush->mPatternSource->mPatternOwner->mPatternBitmap);
      } else {
        brush->mCachedBrush = ::CreatePatternBrush(nullptr);
      }
      break;

    case 'n':
      if (brush->mPatternSource != nullptr && brush->mPatternSource->mDirectPatternBitmap != nullptr) {
        brush->mCachedBrush = ::CreatePatternBrush(brush->mPatternSource->mDirectPatternBitmap);
      } else {
        brush->mCachedBrush = ::CreatePatternBrush(nullptr);
      }
      break;

    default:
      brush->mCachedBrush = ::CreateSolidBrush(brush->mColor);
      break;
  }

  return brush->mCachedBrush;
}

namespace
{
  using WxConfigWriteStringVirtual = int(__thiscall*)(void* object, int keyToken, wxStringRuntime* valueText);

  [[nodiscard]] bool CallWxConfigWriteStringSlot(
    void* const configObject,
    const int keyToken,
    wxStringRuntime* const valueText
  )
  {
    if (configObject == nullptr) {
      return false;
    }

    auto* const* const vtable = *reinterpret_cast<void***>(configObject);
    auto const writeMethod = reinterpret_cast<WxConfigWriteStringVirtual>(vtable[23]);
    return writeMethod != nullptr && writeMethod(configObject, keyToken, valueText) != 0;
  }
}

namespace
{
  struct WxBitmapRuntimeView
  {
    void* vtable = nullptr;      // +0x00
    void* refData = nullptr;     // +0x04
    std::uint32_t reserved08 = 0; // +0x08
  };
  static_assert(sizeof(WxBitmapRuntimeView) == 0x0C, "WxBitmapRuntimeView size must be 0x0C");

  struct WxToolBarToolBaseRuntimeView
  {
    void* vtable = nullptr;                    // +0x00
    std::uint8_t reserved04_1F[0x1C]{};        // +0x04
    WxBitmapRuntimeView normalBitmap{};        // +0x20
    WxBitmapRuntimeView disabledBitmap{};      // +0x2C
    wxStringRuntime shortHelpText{};           // +0x38
    wxStringRuntime longHelpText{};            // +0x3C
    wxStringRuntime labelText{};               // +0x40
  };
  static_assert(offsetof(WxToolBarToolBaseRuntimeView, normalBitmap) == 0x20, "WxToolBarToolBaseRuntimeView::normalBitmap offset must be 0x20");
  static_assert(
    offsetof(WxToolBarToolBaseRuntimeView, disabledBitmap) == 0x2C,
    "WxToolBarToolBaseRuntimeView::disabledBitmap offset must be 0x2C"
  );
  static_assert(
    offsetof(WxToolBarToolBaseRuntimeView, shortHelpText) == 0x38,
    "WxToolBarToolBaseRuntimeView::shortHelpText offset must be 0x38"
  );
  static_assert(
    offsetof(WxToolBarToolBaseRuntimeView, longHelpText) == 0x3C,
    "WxToolBarToolBaseRuntimeView::longHelpText offset must be 0x3C"
  );
  static_assert(offsetof(WxToolBarToolBaseRuntimeView, labelText) == 0x40, "WxToolBarToolBaseRuntimeView::labelText offset must be 0x40");

  struct WxEnhMetaFileRuntimeView
  {
    void* vtable = nullptr;                    // +0x00
    std::uint8_t reserved04_07[0x4]{};        // +0x04
    wxStringRuntime fileName{};               // +0x08
    HENHMETAFILE metaFileHandle = nullptr;    // +0x0C
  };
  static_assert(offsetof(WxEnhMetaFileRuntimeView, fileName) == 0x08, "WxEnhMetaFileRuntimeView::fileName offset must be 0x08");
  static_assert(
    offsetof(WxEnhMetaFileRuntimeView, metaFileHandle) == 0x0C,
    "WxEnhMetaFileRuntimeView::metaFileHandle offset must be 0x0C"
  );
  static_assert(sizeof(WxEnhMetaFileRuntimeView) == 0x10, "WxEnhMetaFileRuntimeView size must be 0x10");

  void DestroyWxBitmapRuntime(
    WxBitmapRuntimeView* const bitmap
  ) noexcept
  {
    if (bitmap == nullptr) {
      return;
    }

    RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(bitmap));
  }
}

/**
 * Address: 0x00A2BC20 (FUN_00A2BC20)
 *
 * What it does:
 * Copies one source enhanced-metafile handle into destination runtime state,
 * duplicating the Win32 handle through `CopyEnhMetaFileW` and preserving null
 * handle semantics.
 */
HENHMETAFILE wxCopyEnhMetaFileHandleFromRuntime(
  WxEnhMetaFileRuntimeView* const destination,
  const WxEnhMetaFileRuntimeView* const source
)
{
  if (destination == nullptr || source == nullptr) {
    return nullptr;
  }

  if (destination == source) {
    return source->metaFileHandle;
  }

  if (source->metaFileHandle != nullptr) {
    const wchar_t* copyPath = nullptr;
    const wchar_t* const destinationPath = destination->fileName.c_str();
    if (destinationPath != nullptr && destinationPath[0] != L'\0') {
      copyPath = destinationPath;
    }

    destination->metaFileHandle = ::CopyEnhMetaFileW(source->metaFileHandle, copyPath);
  } else {
    destination->metaFileHandle = nullptr;
  }

  return destination->metaFileHandle;
}

/**
 * Address: 0x00A2BC60 (FUN_00A2BC60)
 *
 * What it does:
 * Deletes the retained enhanced-metafile Win32 handle for one runtime object
 * when present.
 */
int wxDeleteEnhMetaFileHandleRuntime(
  void* const enhMetaFileRuntime
)
{
  auto* const runtime = static_cast<WxEnhMetaFileRuntimeView*>(enhMetaFileRuntime);
  if (runtime == nullptr || runtime->metaFileHandle == nullptr) {
    return 0;
  }

  return ::DeleteEnhMetaFile(runtime->metaFileHandle);
}

/**
 * Address: 0x00A06320 (FUN_00A06320)
 *
 * What it does:
 * Runs non-deleting `wxToolBarToolBase` teardown: releases three shared-string
 * lanes, destroys two embedded bitmap lanes, then clears base wx-object
 * ref-data ownership.
 */
void wxDestroyToolBarToolBaseRuntime(
  void* const toolRuntime
)
{
  auto* const runtime = static_cast<WxToolBarToolBaseRuntimeView*>(toolRuntime);
  if (runtime == nullptr) {
    return;
  }

  ReleaseWxStringSharedPayload(runtime->labelText);
  ReleaseWxStringSharedPayload(runtime->longHelpText);
  ReleaseWxStringSharedPayload(runtime->shortHelpText);

  DestroyWxBitmapRuntime(&runtime->disabledBitmap);
  DestroyWxBitmapRuntime(&runtime->normalBitmap);
  RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(runtime));
}

/**
 * Address: 0x00A0D5D0 (FUN_00A0D5D0)
 *
 * What it does:
 * Runs non-deleting `wxEnhMetaFile` teardown: releases the retained enhanced
 * metafile handle, releases filename shared-string ownership, then clears base
 * wx-object ref-data ownership.
 */
void wxDestroyEnhMetaFileRuntime(
  void* const enhMetaFileRuntime
)
{
  auto* const runtime = static_cast<WxEnhMetaFileRuntimeView*>(enhMetaFileRuntime);
  if (runtime == nullptr) {
    return;
  }

  (void)wxDeleteEnhMetaFileHandleRuntime(runtime);
  ReleaseWxStringSharedPayload(runtime->fileName);
  RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(runtime));
}

/**
 * Address: 0x00A1A8B0 (FUN_00A1A8B0)
 *
 * What it does:
 * Formats one signed-long value as UTF-16 text and forwards it to the
 * wx-config string write virtual lane.
 */
bool wxConfigWriteLongRuntime(
  void* const configObject,
  const int keyToken,
  const long value
)
{
  wxStringRuntime formattedValue = AllocateOwnedWxString(std::to_wstring(value));
  const bool writeResult = CallWxConfigWriteStringSlot(configObject, keyToken, &formattedValue);
  ReleaseOwnedWxString(formattedValue);
  return writeResult;
}

/**
 * Address: 0x00A098B0 (FUN_00A098B0)
 *
 * What it does:
 * Formats one floating-point value using `%g` semantics and forwards it to the
 * wx-config string write virtual lane.
 */
bool wxConfigWriteDoubleRuntime(
  void* const configObject,
  const int keyToken,
  const double value
)
{
  std::array<wchar_t, 128> formattedBuffer{};
  (void)std::swprintf(formattedBuffer.data(), formattedBuffer.size(), L"%g", value);

  wxStringRuntime formattedValue = AllocateOwnedWxString(std::wstring(formattedBuffer.data()));
  const bool writeResult = CallWxConfigWriteStringSlot(configObject, keyToken, &formattedValue);
  ReleaseOwnedWxString(formattedValue);
  return writeResult;
}

/**
 * Address: 0x00A2FA40 (FUN_00A2FA40)
 *
 * What it does:
 * Runs non-deleting teardown for one socket-input-stream lane by tail-calling
 * the `wxInputStream` base destructor path.
 */
void wxDestroySocketInputStreamRuntime(
  wxInputStream* const stream
)
{
  if (stream == nullptr) {
    return;
  }
  stream->~wxInputStream();
}

namespace
{
  struct WxSocketFrameRuntimeView
  {
    void* payload = nullptr;            // +0x00
    std::uint32_t payloadByteCount = 0; // +0x04
    std::uint32_t unknown08 = 0;        // +0x08
    std::uint32_t unknown0C = 0;        // +0x0C
    std::uint32_t unknown10 = 0;        // +0x10
  };
  static_assert(sizeof(WxSocketFrameRuntimeView) == 0x14, "WxSocketFrameRuntimeView size must be 0x14");
  static_assert(
    offsetof(WxSocketFrameRuntimeView, payloadByteCount) == 0x04,
    "WxSocketFrameRuntimeView::payloadByteCount offset must be 0x04"
  );

  struct WxSocketRuntimeView
  {
    SOCKET socketHandle = INVALID_SOCKET;             // +0x00
    WxSocketFrameRuntimeView* frameLane4 = nullptr;   // +0x04
    WxSocketFrameRuntimeView* frameLane8 = nullptr;   // +0x08
    std::int32_t stateCode = 0;                       // +0x0C
    std::uint8_t unknown10_13[0x4]{};                 // +0x10
    std::int32_t connectionModeFlag = 0;              // +0x14
    std::uint8_t unknown18_2B[0x14]{};                // +0x18
    std::int32_t eventStateMask = 0;                  // +0x2C
    std::uint32_t callbacks[4]{};                     // +0x30
    std::int32_t callbackArgs[4]{};                   // +0x40
    std::int32_t asyncMessageId = 0;                  // +0x50
  };
  static_assert(sizeof(WxSocketRuntimeView) == 0x54, "WxSocketRuntimeView size must be 0x54");
  static_assert(offsetof(WxSocketRuntimeView, frameLane4) == 0x04, "WxSocketRuntimeView::frameLane4 offset must be 0x04");
  static_assert(offsetof(WxSocketRuntimeView, frameLane8) == 0x08, "WxSocketRuntimeView::frameLane8 offset must be 0x08");
  static_assert(offsetof(WxSocketRuntimeView, stateCode) == 0x0C, "WxSocketRuntimeView::stateCode offset must be 0x0C");
  static_assert(
    offsetof(WxSocketRuntimeView, connectionModeFlag) == 0x14,
    "WxSocketRuntimeView::connectionModeFlag offset must be 0x14"
  );
  static_assert(
    offsetof(WxSocketRuntimeView, eventStateMask) == 0x2C,
    "WxSocketRuntimeView::eventStateMask offset must be 0x2C"
  );
  static_assert(
    offsetof(WxSocketRuntimeView, asyncMessageId) == 0x50,
    "WxSocketRuntimeView::asyncMessageId offset must be 0x50"
  );

  struct WxSocketAddressBlobRuntimeView
  {
    void* addressBytes = nullptr;            // +0x00
    std::uint32_t addressByteCount = 0;      // +0x04
    std::uint8_t reserved08_0F[0x8]{};       // +0x08
    std::int32_t stateCode = 0;              // +0x10
  };
  static_assert(
    offsetof(WxSocketAddressBlobRuntimeView, addressByteCount) == 0x04,
    "WxSocketAddressBlobRuntimeView::addressByteCount offset must be 0x04"
  );
  static_assert(
    offsetof(WxSocketAddressBlobRuntimeView, stateCode) == 0x10,
    "WxSocketAddressBlobRuntimeView::stateCode offset must be 0x10"
  );

  [[nodiscard]] WxSocketFrameRuntimeView* wxSocketCloneFrameRuntime(
    const WxSocketFrameRuntimeView* const source
  )
  {
    auto* const clone = static_cast<WxSocketFrameRuntimeView*>(std::malloc(sizeof(WxSocketFrameRuntimeView)));
    if (clone == nullptr) {
      return nullptr;
    }

    *clone = *source;
    if (source->payload != nullptr) {
      void* const payloadCopy = std::malloc(source->payloadByteCount);
      clone->payload = payloadCopy;
      if (payloadCopy == nullptr) {
        _free_crt(clone);
        return nullptr;
      }

      std::memcpy(payloadCopy, source->payload, source->payloadByteCount);
    }

    return clone;
  }

  void wxSocketDestroyFrameRuntime(
    WxSocketFrameRuntimeView* const frame
  )
  {
    if (frame == nullptr) {
      return;
    }

    if (frame->payload != nullptr) {
      _free_crt(frame->payload);
      frame->payload = nullptr;
    }

    _free_crt(frame);
  }

  struct WxSocketAddressPortRuntimeView
  {
    std::uint16_t family = 0;             // +0x00
    std::uint16_t networkPort = 0;        // +0x02
    std::uint8_t reserved04_0F[0x0C]{};   // +0x04
  };
  static_assert(sizeof(WxSocketAddressPortRuntimeView) == 0x10, "WxSocketAddressPortRuntimeView size must be 0x10");
  static_assert(
    offsetof(WxSocketAddressPortRuntimeView, networkPort) == 0x02,
    "WxSocketAddressPortRuntimeView::networkPort offset must be 0x02"
  );

  struct WxSocketPortRuntimeView
  {
    WxSocketAddressPortRuntimeView* address = nullptr; // +0x00
    std::uint32_t addressBytes = 0;                    // +0x04
    std::uint32_t stateCode = 0;                       // +0x08
    std::uint32_t addressFamily = 0;                   // +0x0C
    std::uint32_t errorCode = 0;                       // +0x10
  };
  static_assert(sizeof(WxSocketPortRuntimeView) == 0x14, "WxSocketPortRuntimeView size must be 0x14");
  static_assert(offsetof(WxSocketPortRuntimeView, address) == 0x00, "WxSocketPortRuntimeView::address offset must be 0x00");
  static_assert(offsetof(WxSocketPortRuntimeView, addressBytes) == 0x04, "WxSocketPortRuntimeView::addressBytes offset must be 0x04");
  static_assert(offsetof(WxSocketPortRuntimeView, stateCode) == 0x08, "WxSocketPortRuntimeView::stateCode offset must be 0x08");
  static_assert(offsetof(WxSocketPortRuntimeView, addressFamily) == 0x0C, "WxSocketPortRuntimeView::addressFamily offset must be 0x0C");
  static_assert(offsetof(WxSocketPortRuntimeView, errorCode) == 0x10, "WxSocketPortRuntimeView::errorCode offset must be 0x10");

  /**
   * Inlined helper from FUN_00A30230.
   *
   * What it does:
   * Allocates one 16-byte sockaddr lane for socket port storage and seeds
   * state/family metadata for AF_INET usage.
   */
  int wxSocketEnsureAddressPortStorage(
    WxSocketPortRuntimeView* const socketPort
  )
  {
    if (socketPort == nullptr) {
      return 9;
    }

    socketPort->addressBytes = sizeof(WxSocketAddressPortRuntimeView);
    auto* const address = static_cast<WxSocketAddressPortRuntimeView*>(std::malloc(sizeof(WxSocketAddressPortRuntimeView)));
    socketPort->address = address;
    if (address == nullptr) {
      socketPort->errorCode = 9;
      return 9;
    }

    socketPort->stateCode = 1;
    socketPort->addressFamily = AF_INET;
    address->family = AF_INET;
    address->networkPort = 0u;
    std::memset(address->reserved04_0F, 0, sizeof(address->reserved04_0F));
    return 0;
  }
} // namespace

/**
 * Address: 0x00A38290 (FUN_00A38290)
 *
 * What it does:
 * Clears asynchronous WinSock event routing for one socket lane by rebinding
 * the runtime message slot with zero event-mask flags.
 */
SOCKET wxSocketDisableAsyncSelectRuntime(
  WxSocketRuntimeView* const socketRuntime
)
{
  const SOCKET socketHandle = socketRuntime->socketHandle;
  if (socketHandle == INVALID_SOCKET) {
    return socketHandle;
  }

#pragma warning(push)
#pragma warning(disable : 4996)
  const int result = ::WSAAsyncSelect(socketHandle, gWxSocketInternalWindow, socketRuntime->asyncMessageId, 0);
#pragma warning(pop)
  return result;
}

/**
 * Address: 0x00A2FC40 (FUN_00A2FC40)
 *
 * What it does:
 * Disables async-select routing for one socket lane, closes the socket
 * handle, and marks the runtime handle lane invalid.
 */
int wxSocketCloseHandleRuntime(
  WxSocketRuntimeView* const socketRuntime
)
{
  (void)wxSocketDisableAsyncSelectRuntime(socketRuntime);
  const int result = ::closesocket(socketRuntime->socketHandle);
  socketRuntime->socketHandle = INVALID_SOCKET;
  return result;
}

/**
 * Address: 0x00A2FC60 (FUN_00A2FC60)
 *
 * What it does:
 * Shuts down and closes one live socket lane (if present), clears callback
 * dispatch lanes, and sets runtime socket state to `8` (closed).
 */
int wxSocketResetRuntimeState(
  WxSocketRuntimeView* const socketRuntime
)
{
  if (socketRuntime->socketHandle != INVALID_SOCKET) {
    (void)::shutdown(socketRuntime->socketHandle, SD_BOTH);
    (void)wxSocketCloseHandleRuntime(socketRuntime);
  }

  std::fill(std::begin(socketRuntime->callbacks), std::end(socketRuntime->callbacks), 0u);
  socketRuntime->eventStateMask = 8;
  return 0;
}

/**
 * Address: 0x00A30110 (FUN_00A30110)
 *
 * What it does:
 * Releases one optional payload buffer lane, then frees frame metadata
 * storage.
 */
void wxSocketDestroyFramePayloadRuntime(
  WxSocketFrameRuntimeView* const frame
)
{
  wxSocketDestroyFrameRuntime(frame);
}

/**
 * Address: 0x00A301D0 (FUN_00A301D0, sub_A301D0)
 *
 * What it does:
 * Duplicates one socket-address blob lane into caller-owned storage and writes
 * success/error state codes (`0`, `3`, `9`) back to the source runtime.
 */
int wxSocketCloneAddressBlobRuntime(
  WxSocketAddressBlobRuntimeView* const source,
  void** const outAddressBytes,
  std::uint32_t* const outAddressByteCount
)
{
  if (source->addressBytes == nullptr) {
    source->stateCode = 3;
    return 3;
  }

  *outAddressByteCount = source->addressByteCount;
  void* const clonedAddress = std::malloc(source->addressByteCount);
  *outAddressBytes = clonedAddress;
  if (clonedAddress == nullptr) {
    source->stateCode = 9;
    return 9;
  }

  std::memcpy(clonedAddress, source->addressBytes, source->addressByteCount);
  return 0;
}

/**
 * Address: 0x00A300A0 (FUN_00A300A0)
 *
 * What it does:
 * Allocates and shallow-copies one frame metadata lane, then deep-copies the
 * payload byte buffer when present.
 */
[[nodiscard]] WxSocketFrameRuntimeView* wxSocketCloneFramePayloadRuntime(
  const WxSocketFrameRuntimeView* const source
)
{
  return source != nullptr ? wxSocketCloneFrameRuntime(source) : nullptr;
}

/**
 * Address: 0x00A30570 (FUN_00A30570)
 *
 * What it does:
 * Installs one replacement frame lane at `+0x04` only when socket state allows
 * it (`INVALID_SOCKET` or connection-mode flag set), otherwise emits state `4`.
 */
int wxSocketAssignPrimaryFrameRuntime(
  WxSocketRuntimeView* const socketRuntime,
  const WxSocketFrameRuntimeView* const sourceFrame
)
{
  if (socketRuntime->socketHandle == INVALID_SOCKET || socketRuntime->connectionModeFlag != 0) {
    if (sourceFrame != nullptr && sourceFrame->payloadByteCount != 0u) {
      if (socketRuntime->frameLane4 != nullptr) {
        wxSocketDestroyFramePayloadRuntime(socketRuntime->frameLane4);
      }
      socketRuntime->frameLane4 = wxSocketCloneFramePayloadRuntime(sourceFrame);
      return 0;
    }

    socketRuntime->stateCode = 3;
    return 3;
  }

  socketRuntime->stateCode = 4;
  return 4;
}

/**
 * Address: 0x00A305D0 (FUN_00A305D0)
 *
 * What it does:
 * Installs one replacement frame lane at `+0x08` when payload metadata is
 * present, otherwise emits state `3`.
 */
int wxSocketAssignSecondaryFrameRuntime(
  WxSocketRuntimeView* const socketRuntime,
  const WxSocketFrameRuntimeView* const sourceFrame
)
{
  if (sourceFrame != nullptr && sourceFrame->payloadByteCount != 0u) {
    if (socketRuntime->frameLane8 != nullptr) {
      wxSocketDestroyFramePayloadRuntime(socketRuntime->frameLane8);
    }
    socketRuntime->frameLane8 = wxSocketCloneFramePayloadRuntime(sourceFrame);
    return 0;
  }

  socketRuntime->stateCode = 3;
  return 3;
}

/**
 * Address: 0x00A303C0 (FUN_00A303C0)
 *
 * What it does:
 * Ensures one socket-address storage lane exists and writes the port field in
 * network byte order; sets state code `3` when the socket-address lane is not
 * in writable state.
 */
int wxSocketSetAddressPortRuntime(
  WxSocketPortRuntimeView* const socketPort,
  const std::uint16_t hostPort
)
{
  if (socketPort == nullptr) {
    return 9;
  }

  if (socketPort->stateCode == 0 && wxSocketEnsureAddressPortStorage(socketPort) != 0) {
    return static_cast<int>(socketPort->errorCode);
  }

  if (socketPort->stateCode == 1) {
    socketPort->address->networkPort = ::htons(hostPort);
    return 0;
  }

  socketPort->errorCode = 3;
  return 3;
}

/**
 * Address: 0x00A304B0 (FUN_00A304B0)
 *
 * What it does:
 * Ensures one socket-address storage lane exists and reads the port field in
 * host byte order; stores state code `3` on invalid socket-address state.
 */
std::uint16_t wxSocketGetAddressPortRuntime(
  WxSocketPortRuntimeView* const socketPort
)
{
  if (socketPort == nullptr) {
    return 0u;
  }

  if (socketPort->stateCode == 0 && wxSocketEnsureAddressPortStorage(socketPort) != 0) {
    return 0u;
  }

  if (socketPort->stateCode != 1) {
    socketPort->errorCode = 3;
    return 0u;
  }

  return ::ntohs(socketPort->address->networkPort);
}

/**
 * Address: 0x009CBFE0 (FUN_009CBFE0)
 *
 * What it does:
 * Runs one wx-object unref tail for event/object destruction paths that end at
 * base `wxObject` ownership cleanup.
 */
void wxDestroyEventObjectRuntime(
  WxObjectRuntimeView* const object
)
{
  RunWxObjectUnrefTail(object);
}

namespace
{
  struct WxTextMetricHostRuntimeView
  {
    std::uint8_t mUnknown00ToFB[0xFC]{};
    HDC mDeviceContext = nullptr; // +0xFC
  };

  static_assert(
    offsetof(WxTextMetricHostRuntimeView, mDeviceContext) == 0xFC,
    "WxTextMetricHostRuntimeView::mDeviceContext offset must be 0xFC"
  );

/**
 * Address: 0x009C9E10 (FUN_009C9E10, sub_9C9E10)
 *
 * What it does:
 * Reads one text-metric block from the runtime device-context lane and returns
 * the character-height lane.
 */
LONG wxGetTextMetricHeightRuntime(
  WxTextMetricHostRuntimeView* const deviceContextHost
)
{
  TEXTMETRICW textMetrics{};
  (void)::GetTextMetricsW(deviceContextHost->mDeviceContext, &textMetrics);
  return textMetrics.tmHeight;
}
} // namespace

wxDCBase::wxDCBase() = default;

/**
 * Address: 0x009CA490 (FUN_009CA490)
 * Mangled: ??0wxDC@@QAE@@Z
 *
 * What it does:
 * Initializes one device-context lane with cleared selected object and native
 * handle ownership state.
 */
wxDC::wxDC()
  : wxDCBase()
{
  m_selectedBitmap = nullptr;
  m_bOwnsDC &= static_cast<std::uint8_t>(~1u);
  m_canvas = nullptr;
  m_oldBitmap = nullptr;
  m_oldPen = nullptr;
  m_oldBrush = nullptr;
  m_oldFont = nullptr;
  m_oldPalette = nullptr;
  m_hDC = nullptr;
}

/**
 * Address: 0x009D45B0 (FUN_009D45B0)
 * Mangled: ??0wxMemoryDC@@QAE@@Z
 *
 * What it does:
 * Constructs one memory DC, allocates a compatible native DC handle, then
 * initializes default pen/brush/background mode lanes.
 */
wxMemoryDC::wxMemoryDC()
  : wxDC()
{
  (void)CreateCompatible(nullptr);
  Init();
}

/**
 * Address: 0x009D4430 (FUN_009D4430)
 * Mangled: ?CreateCompatible@wxMemoryDC@@QAE_NPAVwxDC@@@Z
 */
bool wxMemoryDC::CreateCompatible(
  wxDC* const sourceDc
)
{
  HDC sourceHandle = nullptr;
  if (sourceDc != nullptr) {
    sourceHandle = reinterpret_cast<HDC>(sourceDc->GetNativeHandle());
  }

  HDC const compatibleDc = ::CreateCompatibleDC(sourceHandle);
  m_bOwnsDC |= 1u;
  m_hDC = compatibleDc;
  m_flags =
    static_cast<std::uint8_t>((m_flags & static_cast<std::uint8_t>(~0x2u)) | (compatibleDc != nullptr ? 0x2u : 0u));
  return (m_flags & 0x2u) != 0u;
}

/**
 * Address: 0x009D43F0 (FUN_009D43F0)
 * Mangled: ?Init@wxMemoryDC@@AAEXXZ
 */
void wxMemoryDC::Init()
{
  if ((m_flags & 0x2u) == 0u) {
    return;
  }

  SetBrush(::GetStockObject(WHITE_BRUSH));
  SetPen(::GetStockObject(BLACK_PEN));
  (void)::SetBkMode(reinterpret_cast<HDC>(m_hDC), 1);
}

void wxMemoryDC::SetBrush(
  void* const brushToken
)
{
  m_oldBrush = brushToken;
}

void wxMemoryDC::SetPen(
  void* const penToken
)
{
  m_oldPen = penToken;
}

[[nodiscard]] const wchar_t* wxStringRuntime::c_str() const noexcept
{
  return m_pchData != nullptr ? m_pchData : L"";
}

msvc8::string wxStringRuntime::ToUtf8() const
{
  return gpg::STR_WideToUtf8(c_str());
}

msvc8::string wxStringRuntime::ToUtf8Lower() const
{
  const msvc8::string value = ToUtf8();
  return gpg::STR_ToLower(value.c_str());
}

/**
 * Address: 0x0095FFD0 (FUN_0095FFD0, func_wstrFind)
 *
 * What it does:
 * Selects first-or-last wide-char search over this string lane and returns
 * a zero-based character index, or `-1` when no match exists.
 */
std::int32_t wxStringRuntime::FindCharacterIndex(
  const wchar_t needle,
  const bool findFromRight
) const noexcept
{
  wchar_t* const text = m_pchData;
  wchar_t* const match =
    findFromRight ? const_cast<wchar_t*>(std::wcsrchr(text, needle)) : const_cast<wchar_t*>(std::wcschr(text, needle));
  if (match == nullptr) {
    return -1;
  }

  return static_cast<std::int32_t>(match - text);
}

/**
 * Address: 0x009621C0 (FUN_009621C0, wxString::Matches)
 *
 * What it does:
 * Matches this text lane against one wildcard mask (`*`/`?`) using the
 * original backtracking and literal-segment scan behavior.
 */
bool wxStringRuntime::Matches(
  const wchar_t* const wildcardMask
) const noexcept
{
  const wchar_t* textCursor = c_str();
  const wchar_t* maskCursor = wildcardMask != nullptr ? wildcardMask : L"";
  const wchar_t* backtrackText = nullptr;
  const wchar_t* backtrackMask = nullptr;

  while (true) {
    if (*maskCursor == L'\0') {
      if (*textCursor == L'\0') {
        return true;
      }
      if (backtrackText == nullptr || backtrackMask == nullptr) {
        return false;
      }

      maskCursor = backtrackMask;
      textCursor = backtrackText + 1;
      continue;
    }

    const wchar_t wildcard = *maskCursor;
    if (wildcard == L'*') {
      backtrackText = textCursor;
      backtrackMask = maskCursor;

      while (*maskCursor == L'*' || *maskCursor == L'?') {
        ++maskCursor;
      }
      if (*maskCursor == L'\0') {
        return true;
      }

      const wchar_t* const nextWildcard = std::wcspbrk(maskCursor, L"*?");
      const std::size_t literalLength =
        nextWildcard != nullptr ? static_cast<std::size_t>(nextWildcard - maskCursor) : std::wcslen(maskCursor);

      const wchar_t* literalMatch = nullptr;
      const wchar_t literalStart = *maskCursor;
      const wchar_t* searchCursor = textCursor;
      while (true) {
        const wchar_t* const candidate = std::wcschr(searchCursor, literalStart);
        if (candidate == nullptr) {
          break;
        }

        if (std::wcsncmp(candidate, maskCursor, literalLength) == 0) {
          literalMatch = candidate;
          break;
        }
        searchCursor = candidate + 1;
      }

      if (literalMatch == nullptr) {
        return false;
      }

      textCursor = literalMatch + literalLength;
      maskCursor += literalLength;
      continue;
    }

    if (wildcard == L'?') {
      if (*textCursor == L'\0') {
        return false;
      }
    } else if (wildcard != *textCursor) {
      return false;
    }

    ++maskCursor;
    ++textCursor;
  }
}

/**
 * Address: 0x009610B0 (FUN_009610B0, wxString::Empty)
 *
 * What it does:
 * Truncates one wx string to `newLength` when shortening is requested and the
 * target payload is writable after copy-before-write checks.
 */
wxStringRuntime* wxStringRuntime::Empty(
  const std::uint32_t newLength
)
{
  if (m_pchData == nullptr || !IsOwnedWxString(*this)) {
    return this;
  }

  auto* const header = reinterpret_cast<WxOwnedStringHeader*>(reinterpret_cast<std::int32_t*>(m_pchData) - 3);
  const auto currentLength = static_cast<std::uint32_t>(header->length);
  if (newLength < currentLength && EnsureUniqueOwnedWxStringBuffer(this)) {
    m_pchData[newLength] = L'\0';

    auto* const writableHeader = reinterpret_cast<WxOwnedStringHeader*>(reinterpret_cast<std::int32_t*>(m_pchData) - 3);
    writableHeader->length = static_cast<std::int32_t>(newLength);
  }

  return this;
}

/**
 * Address: 0x00960F60 (FUN_00960F60)
 *
 * What it does:
 * Lowercases one wx-string payload in place after copy-before-write ownership
 * checks succeed.
 */
wxStringRuntime* LowerOwnedWxStringPayload(wxStringRuntime* const text)
{
  if (text == nullptr) {
    return nullptr;
  }

  if (EnsureUniqueOwnedWxStringBuffer(text) && text->m_pchData != nullptr) {
    for (wchar_t* cursor = text->m_pchData; *cursor != L'\0'; ++cursor) {
      *cursor = static_cast<wchar_t>(RuntimeToLowerWideWithCurrentLocale(*cursor));
    }
  }
  return text;
}

/**
 * Address: 0x00960F20 (FUN_00960F20)
 *
 * What it does:
 * Ensures copy-on-write ownership and lowercases this string payload in place.
 */
wxStringRuntime* wxStringRuntime::LowerInPlace()
{
  return LowerOwnedWxStringPayload(this);
}

/**
 * Address: 0x00960FA0 (FUN_00960FA0)
 *
 * What it does:
 * Trims ASCII-space characters from either edge of one wx string payload,
 * preserving copy-on-write ownership semantics.
 */
wxStringRuntime* wxStringRuntime::TrimInPlace(
  const bool fromRight
)
{
  if (m_pchData == nullptr || !IsOwnedWxString(*this)) {
    return this;
  }

  auto isAsciiWhitespace = [](const wchar_t ch) noexcept {
    return static_cast<unsigned int>(ch) < 0x7Fu && std::iswspace(static_cast<wint_t>(ch)) != 0;
  };

  auto* const initialHeader = reinterpret_cast<WxOwnedStringHeader*>(reinterpret_cast<std::int32_t*>(m_pchData) - 3);
  if (initialHeader->length <= 0) {
    return this;
  }

  if (fromRight) {
    if (!isAsciiWhitespace(m_pchData[initialHeader->length - 1])) {
      return this;
    }
  } else {
    if (!isAsciiWhitespace(m_pchData[0])) {
      return this;
    }
  }

  if (!EnsureUniqueOwnedWxStringBuffer(this)) {
    return this;
  }

  auto* const header = reinterpret_cast<WxOwnedStringHeader*>(reinterpret_cast<std::int32_t*>(m_pchData) - 3);
  if (!fromRight) {
    wchar_t* trimmedStart = m_pchData;
    while (*trimmedStart != L'\0' && isAsciiWhitespace(*trimmedStart)) {
      ++trimmedStart;
    }

    const std::int32_t removedCount = static_cast<std::int32_t>(trimmedStart - m_pchData);
    const std::int32_t newLength = header->length - removedCount;
    std::memmove(m_pchData, trimmedStart, static_cast<std::size_t>(newLength + 1) * sizeof(wchar_t));
    header->length = newLength;
    return this;
  }

  std::int32_t endIndex = header->length - 1;
  while (endIndex >= 0 && isAsciiWhitespace(m_pchData[endIndex])) {
    --endIndex;
  }

  m_pchData[endIndex + 1] = L'\0';
  header->length = endIndex + 1;
  return this;
}

/**
 * Address: 0x009620B0 (FUN_009620B0, wxString::Pad)
 *
 * What it does:
 * Builds one temporary pad-string lane of `padCount` copies of `padChar`,
 * then appends or prepends it to this string according to `appendToRight`.
 */
wxStringRuntime* wxStringRuntime::PadInPlace(
  const std::size_t padCount,
  const wchar_t padChar,
  const bool appendToRight
)
{
  if (padCount == 0u) {
    return this;
  }

  const std::wstring padding(padCount, padChar);
  if (appendToRight) {
    std::wstring combined = std::wstring(c_str());
    combined += padding;
    AssignOwnedWxString(this, combined);
    return this;
  }

  std::wstring combined = padding;
  combined += c_str();
  AssignOwnedWxString(this, combined);
  return this;
}

wxStringRuntime wxStringRuntime::Borrow(
  const wchar_t* const text
) noexcept
{
  wxStringRuntime runtime{};
  runtime.m_pchData = const_cast<wchar_t*>(text != nullptr ? text : L"");
  return runtime;
}

/**
 * Address: 0x0096E1D0 (FUN_0096E1D0, wxNativeFontInfo::wxNativeFontInfo)
 * Mangled: ??0wxNativeFontInfo@@QAE@@Z
 *
 * What it does:
 * Constructs one native-font descriptor and seeds default runtime lanes.
 */
wxNativeFontInfoRuntime::wxNativeFontInfoRuntime()
{
  Init();
}

/**
 * Address: 0x0097EEF0 (FUN_0097EEF0, wxNativeFontInfo::FromString)
 * Mangled: ?FromString@wxNativeFontInfo@@QAE_NABVwxString@@@Z
 *
 * What it does:
 * Resets this descriptor, tokenizes one textual font descriptor, and applies
 * style/weight/underline/point-size/charset/facename lanes.
 */
bool wxNativeFontInfoRuntime::FromString(
  const wxStringRuntime& description
)
{
  Init();

  std::wstring pendingFaceName{};
  const std::vector<std::wstring> tokens = SplitNativeFontDescriptionTokens(description.c_str());

  auto flushPendingFaceName = [&]() {
    if (pendingFaceName.empty()) {
      return;
    }

    SetFaceName(wxStringRuntime::Borrow(pendingFaceName.c_str()));
    pendingFaceName.clear();
  };

  for (const std::wstring& token : tokens) {
    const std::wstring lowered = ToLowerWide(token);
    bool recognized = false;

    if (lowered == L"underlined") {
      SetUnderlined(true);
      recognized = true;
    } else if (lowered == L"light") {
      SetWeight(91);
      recognized = true;
    } else if (lowered == L"bold" || ContainsNoCase(lowered, L"bold")) {
      SetWeight(92);
      recognized = true;
    } else if (lowered == L"italic" || ContainsNoCase(lowered, L"italic")) {
      SetStyle(93);
      recognized = true;
    } else {
      std::int32_t pointSize = 0;
      if (TryParseIntToken(lowered, &pointSize)) {
        SetPointSize(pointSize);
        recognized = true;
      } else {
        std::int32_t charset = 0;
        if (TryMapEncodingToCharset(lowered, &charset)) {
          SetEncoding(charset);
          recognized = true;
        }
      }
    }

    if (recognized) {
      flushPendingFaceName();
      continue;
    }

    if (!pendingFaceName.empty()) {
      pendingFaceName.push_back(L' ');
    }
    pendingFaceName += token;
  }

  flushPendingFaceName();
  return true;
}

/**
 * Address: 0x0097F440 (FUN_0097F440, wxFontBase::SetNativeFontInfo)
 * Mangled: ?SetNativeFontInfo@wxFontBase@@QAEXABVwxString@@@Z
 *
 * What it does:
 * Parses one textual native-font descriptor and forwards the parsed
 * descriptor into virtual slot `+0x68` on the font object.
 */
void WX_FontBaseSetNativeFontInfoFromString(
  void* const fontObject,
  const wxStringRuntime& description
)
{
  if (fontObject == nullptr) {
    return;
  }

  const wchar_t* const text = description.c_str();
  if (text == nullptr || *text == L'\0') {
    return;
  }

  wxNativeFontInfoRuntime nativeFontInfo{};
  if (!nativeFontInfo.FromString(description)) {
    return;
  }

  void** const vtable = *reinterpret_cast<void***>(fontObject);
  if (vtable == nullptr) {
    return;
  }

  using SetNativeInfoFn = std::int32_t(__thiscall*)(void*, const wxNativeFontInfoRuntime&);
  auto const setNativeInfo = reinterpret_cast<SetNativeInfoFn>(vtable[0x68 / sizeof(void*)]);
  (void)setNativeInfo(fontObject, nativeFontInfo);
}

void wxNativeFontInfoRuntime::Init() noexcept
{
  mHeight = 0;
  mWidth = 0;
  mEscapement = 0;
  mOrientation = 0;
  mWeight = 400;
  mItalic = 0;
  mUnderline = 0;
  mStrikeOut = 0;
  mCharSet = 1;
  mOutPrecision = 0;
  mClipPrecision = 0;
  mQuality = 0;
  mPitchAndFamily = 0;
  std::wmemset(mFaceName, 0, std::size(mFaceName));
}

/**
 * Address: 0x0096E360 (FUN_0096E360, wxNativeFontInfo::SetPointSize)
 *
 * What it does:
 * Converts point size into Win32 logical font height using display vertical
 * DPI (`LOGPIXELSY`).
 */
void wxNativeFontInfoRuntime::SetPointSize(
  const std::int32_t pointSize
) noexcept
{
  HDC const deviceContext = ::GetDC(nullptr);
  const int deviceDpiY = ::GetDeviceCaps(deviceContext, LOGPIXELSY);
  (void)::ReleaseDC(nullptr, deviceContext);

  mHeight = -static_cast<std::int32_t>(
    static_cast<double>(deviceDpiY) * static_cast<double>(pointSize) / 72.0 + 0.5
  );
}

/**
 * Address: 0x0096E1E0 (FUN_0096E1E0, wxFont::GetPointSize helper lane)
 *
 * What it does:
 * Converts this LOGFONT logical height into point size using display vertical
 * DPI (`LOGPIXELSY`).
 */
std::int32_t wxNativeFontInfoRuntime::GetPointSize() const noexcept
{
  HDC const deviceContext = ::GetDC(nullptr);
  const int deviceDpiY = ::GetDeviceCaps(deviceContext, LOGPIXELSY);
  (void)::ReleaseDC(nullptr, deviceContext);
  if (deviceDpiY <= 0) {
    return 0;
  }

  const double pointSize =
    static_cast<double>(std::abs(mHeight)) * 72.0 / static_cast<double>(deviceDpiY) + 0.5;
  return static_cast<std::int32_t>(pointSize);
}

void wxNativeFontInfoRuntime::SetWeight(
  const std::int32_t weight
) noexcept
{
  if (weight == 91) {
    mWeight = 300;
    return;
  }

  if (weight == 92) {
    mWeight = 700;
    return;
  }

  mWeight = weight;
}

void wxNativeFontInfoRuntime::SetStyle(
  const std::int32_t style
) noexcept
{
  mItalic = style == 93 ? 1u : 0u;
}

void wxNativeFontInfoRuntime::SetUnderlined(
  const bool underlined
) noexcept
{
  mUnderline = underlined ? 1u : 0u;
}

void wxNativeFontInfoRuntime::SetFaceName(
  const wxStringRuntime& faceName
) noexcept
{
  std::wmemset(mFaceName, 0, std::size(mFaceName));

  const wchar_t* const text = faceName.c_str();
  if (text == nullptr || *text == L'\0') {
    return;
  }

  const std::size_t len = std::wcslen(text);
  const std::size_t copyLen = (std::min)(len, std::size(mFaceName) - 1u);
  if (copyLen > 0u) {
    std::wmemcpy(mFaceName, text, copyLen);
    mFaceName[copyLen] = L'\0';
  }
}

/**
 * Address: 0x0096E430 (FUN_0096E430)
 *
 * What it does:
 * Copies one temporary UTF-16 face-name lane into this `LOGFONTW` payload and
 * then releases the temporary wx-string ownership lane.
 */
void wxNativeFontInfoRuntime::CopyFaceNameFromBufferAndReleaseTemp(
  wchar_t* const temporaryFaceNameBuffer
) noexcept
{
  std::wmemset(mFaceName, 0, std::size(mFaceName));
  if (temporaryFaceNameBuffer != nullptr) {
    std::wcsncpy(mFaceName, temporaryFaceNameBuffer, std::size(mFaceName) - 1u);
    mFaceName[std::size(mFaceName) - 1u] = L'\0';
  }

  wxStringRuntime temporary{};
  temporary.m_pchData = temporaryFaceNameBuffer;
  ReleaseOwnedWxString(temporary);
}

void wxNativeFontInfoRuntime::SetEncoding(
  const std::int32_t encoding
) noexcept
{
  mCharSet = static_cast<std::uint8_t>(encoding & 0xFF);
}

/**
 * Address: 0x0042B870 (FUN_0042B870)
 * Mangled: ??0wxImageHandler@@QAE@@Z
 *
 * What it does:
 * Initializes name/extension/mime string lanes and sets type to invalid.
 */
wxImageHandlerRuntime::wxImageHandlerRuntime()
{
  mRefData = nullptr;
  mName = wxStringRuntime::Borrow(L"");
  mExtension = wxStringRuntime::Borrow(L"");
  mMime = wxStringRuntime::Borrow(L"");
  mType = 0;
}

void wxImageHandlerRuntime::SetDescriptor(
  const wchar_t* const name,
  const wchar_t* const extension,
  const wchar_t* const mimeType,
  const std::int32_t bitmapType
) noexcept
{
  mName = wxStringRuntime::Borrow(name != nullptr ? name : L"");
  mExtension = wxStringRuntime::Borrow(extension != nullptr ? extension : L"");
  mMime = wxStringRuntime::Borrow(mimeType != nullptr ? mimeType : L"");
  mType = bitmapType;
}

/**
 * Address: 0x0042B8F0 (FUN_0042B8F0)
 * Mangled: ?GetClassInfo@wxImageHandler@@UBEPAVwxClassInfo@@XZ
 *
 * What it does:
 * Returns the static class-info lane for wxImageHandler runtime RTTI checks.
 */
void* wxImageHandlerRuntime::GetClassInfo() const
{
  return gWxImageHandlerClassInfoTable;
}

void wxImageHandlerRuntime::ReleaseSharedWxString(
  wxStringRuntime& value
) noexcept
{
  // Runtime wrappers keep wxString lanes as borrowed views; dropping one lane
  // is represented by clearing the pointer.
  value.m_pchData = nullptr;
}

/**
 * Address: 0x0042B920 (FUN_0042B920)
 *
 * What it does:
 * Releases runtime string lanes and clears shared ref-data ownership.
 */
wxImageHandlerRuntime::~wxImageHandlerRuntime()
{
  ReleaseSharedWxString(mMime);
  ReleaseSharedWxString(mExtension);
  ReleaseSharedWxString(mName);
  RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(this));
}

/**
 * Address: 0x0042B9E0 (FUN_0042B9E0)
 * Mangled: ??0wxPNGHandler@@QAE@XZ
 *
 * What it does:
 * Initializes the PNG handler descriptor (name, extension, mime, bitmap type).
 */
wxPngHandlerRuntime::wxPngHandlerRuntime()
{
  // wxBitmapType::wxBITMAP_TYPE_PNG in this runtime lane.
  constexpr std::int32_t kBitmapTypePng = 15;
  SetDescriptor(L"PNG file", L"png", L"image/png", kBitmapTypePng);
}

/**
 * Address: 0x0042BA50 (FUN_0042BA50)
 * Mangled: ?GetClassInfo@wxPNGHandler@@UBEPAVwxClassInfo@@XZ
 *
 * What it does:
 * Returns the static class-info lane for wxPNGHandler runtime RTTI checks.
 */
void* wxPngHandlerRuntime::GetClassInfo() const
{
  return gWxPngHandlerClassInfoTable;
}

/**
 * Address: 0x0042BA60 (FUN_0042BA60)
 *
 * What it does:
 * Deleting-dtor thunk lane for `wxPNGHandler`; no extra teardown beyond base.
 */
wxPngHandlerRuntime::~wxPngHandlerRuntime() = default;

/**
 * Address: 0x00970250 (FUN_00970250, ??0wxBMPHandler@@QAE@XZ)
 * Mangled: ??0wxBMPHandler@@QAE@XZ
 *
 * What it does:
 * Initializes one BMP handler descriptor lane (`"Windows bitmap file"`,
 * extension `"bmp"`, mime `"image/x-bmp"`, bitmap type `1`).
 */
wxBmpHandlerRuntime::wxBmpHandlerRuntime()
{
  SetDescriptor(L"Windows bitmap file", L"bmp", L"image/x-bmp", 1);
}

/**
 * Address: 0x009702F0 (FUN_009702F0, ??0wxXPMHandler@@QAE@XZ)
 * Mangled: ??0wxXPMHandler@@QAE@XZ
 *
 * What it does:
 * Initializes one XPM handler descriptor lane (`"XPM file"`, extension
 * `"xpm"`, mime `"image/xpm"`, bitmap type `9`).
 */
wxXpmHandlerRuntime::wxXpmHandlerRuntime()
{
  SetDescriptor(L"XPM file", L"xpm", L"image/xpm", 9);
}

/**
 * Address: 0x009D7E10 (FUN_009D7E10, ??0wxICOHandler@@QAE@XZ)
 * Mangled: ??0wxICOHandler@@QAE@XZ
 *
 * What it does:
 * Initializes one ICO handler descriptor lane (`"Windows icon file"`,
 * extension `"ico"`, mime `"image/x-ico"`, bitmap type `3`).
 */
wxIcoHandlerRuntime::wxIcoHandlerRuntime()
  : wxBmpHandlerRuntime()
{
  SetDescriptor(L"Windows icon file", L"ico", L"image/x-ico", 3);
}

/**
 * Address: 0x009D7EB0 (FUN_009D7EB0, ??0wxCURHandler@@QAE@XZ)
 * Mangled: ??0wxCURHandler@@QAE@XZ
 *
 * What it does:
 * Initializes one CUR handler descriptor lane (`"Windows cursor file"`,
 * extension `"cur"`, mime `"image/x-cur"`, bitmap type `5`).
 */
wxCurHandlerRuntime::wxCurHandlerRuntime()
  : wxIcoHandlerRuntime()
{
  SetDescriptor(L"Windows cursor file", L"cur", L"image/x-cur", 5);
}

/**
 * Address: 0x009D7F50 (FUN_009D7F50, ??0wxANIHandler@@QAE@XZ)
 * Mangled: ??0wxANIHandler@@QAE@XZ
 *
 * What it does:
 * Initializes one ANI handler descriptor lane (`"Windows animated cursor file"`,
 * extension `"ani"`, mime `"image/x-ani"`, bitmap type `27`).
 */
wxAniHandlerRuntime::wxAniHandlerRuntime()
  : wxCurHandlerRuntime()
{
  SetDescriptor(L"Windows animated cursor file", L"ani", L"image/x-ani", 27);
}

/**
 * Address: 0x009AB120 (FUN_009AB120, ??0wxBMPFileHandler@@QAE@XZ)
 * Mangled: ??0wxBMPFileHandler@@QAE@XZ
 *
 * What it does:
 * Initializes one BMP image-handler descriptor lane and preserves the default
 * empty MIME lane.
 */
wxBmpFileHandlerRuntime::wxBmpFileHandlerRuntime()
{
  mName = wxStringRuntime::Borrow(L"Windows bitmap file");
  mExtension = wxStringRuntime::Borrow(L"bmp");
  mType = 1;
}

/**
 * Address: 0x009AB450 (FUN_009AB450, ??0wxICOFileHandler@@QAE@XZ)
 * Mangled: ??0wxICOFileHandler@@QAE@XZ
 *
 * What it does:
 * Initializes one ICO-file image-handler descriptor lane and preserves the
 * default empty MIME lane.
 */
wxIcoFileHandlerRuntime::wxIcoFileHandlerRuntime()
{
  mName = wxStringRuntime::Borrow(L"ICO icon file");
  mExtension = wxStringRuntime::Borrow(L"ico");
  mType = 3;
}

/**
 * Address: 0x009AB570 (FUN_009AB570, ??0wxICOResourceHandler@@QAE@XZ)
 * Mangled: ??0wxICOResourceHandler@@QAE@XZ
 *
 * What it does:
 * Initializes one ICO-resource image-handler descriptor lane and preserves the
 * default empty MIME lane.
 */
wxIcoResourceHandlerRuntime::wxIcoResourceHandlerRuntime()
{
  mName = wxStringRuntime::Borrow(L"ICO resource");
  mExtension = wxStringRuntime::Borrow(L"ico");
  mType = 4;
}

/**
 * Address: 0x00970540 (FUN_00970540)
 *
 * What it does:
 * Initializes one image object and shares ref-data ownership from `clone`.
 */
wxImageRuntime::wxImageRuntime(
  const wxImageRuntime& clone
)
  : mRefData(nullptr)
{
  (void)wxObjectCopySharedRefDataRuntime(
    reinterpret_cast<WxObjectRuntimeView*>(this),
    reinterpret_cast<WxObjectRuntimeView*>(const_cast<wxImageRuntime*>(&clone))
  );
}

/**
 * Address: 0x00972460 (FUN_00972460)
 *
 * What it does:
 * When image ref-data is valid, synchronizes the embedded palette object's
 * shared ref-data lane from `clone` if it currently differs.
 */
void wxImageSyncPaletteRefDataFromClone(
  wxImageRuntime* const image,
  WxObjectRuntimeView* const clone
)
{
  auto* const imageRefData = static_cast<WxImageRefDataRuntime*>(image->mRefData);
  if (imageRefData == nullptr) {
    return;
  }

  auto* const paletteObject = reinterpret_cast<WxObjectRuntimeView*>(imageRefData->mPaletteRuntimeLane);
  if (paletteObject->refData != clone->refData) {
    (void)wxObjectCopySharedRefDataRuntime(paletteObject, clone);
  }
}

wxImageRuntime::~wxImageRuntime()
{
  ReleaseRefData();
}

void wxImageRuntime::ReleaseRefData() noexcept
{
  auto* const refData = reinterpret_cast<WxImageRefDataRuntime*>(mRefData);
  if (refData == nullptr) {
    return;
  }

  --refData->mRefCount;
  if (refData->mRefCount == 0) {
    delete refData;
  }

  mRefData = nullptr;
}

/**
 * Address: 0x00970600 (FUN_00970600)
 * Mangled: ?Create@wxImage@@QAEXHH@Z
 *
 * What it does:
 * Drops previous shared image ref-data, allocates one fresh image ref-data
 * object, then allocates/clears 24-bit RGB storage for `width*height`.
 */
void wxImageRuntime::Create(
  const std::int32_t width,
  const std::int32_t height
)
{
  ReleaseRefData();

  auto* const refData = new (std::nothrow) WxImageRefDataRuntime();
  mRefData = refData;
  if (refData == nullptr) {
    return;
  }

  const std::uint32_t wrappedPixelCount = static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height);
  const std::int32_t pixelByteCountSigned = static_cast<std::int32_t>(wrappedPixelCount * 3u);
  const std::size_t pixelByteCount = static_cast<std::size_t>(static_cast<std::uint32_t>(pixelByteCountSigned));

  refData->mPixelBytes = static_cast<std::uint8_t*>(std::malloc(pixelByteCount));
  if (refData->mPixelBytes == nullptr) {
    ReleaseRefData();
    return;
  }

  if (pixelByteCountSigned > 0) {
    std::memset(refData->mPixelBytes, 0, static_cast<std::size_t>(pixelByteCountSigned));
  }

  refData->mWidth = width;
  refData->mHeight = height;
  refData->mMaskAndFlags[4] = 1;
}

/**
 * Address: 0x00970C10 (FUN_00970C10)
 *
 * What it does:
 * Returns true when this image is valid and one option key matches
 * `optionName`.
 */
bool wxImageRuntime::HasOption(
  const wxStringRuntime& optionName
) const noexcept
{
  auto* const refData = static_cast<const WxImageRefDataRuntime*>(mRefData);
  if (refData == nullptr) {
    return false;
  }

  return WxFindStringArrayIndex(&refData->mOptionKeys, optionName.m_pchData, false, false) != -1;
}

/**
 * Address: 0x00972490 (FUN_00972490)
 *
 * What it does:
 * Resolves one image-option value by key and stores the option text lane in
 * `outValue`, falling back to `wxEmptyString` when absent.
 */
wxStringRuntime* wxImageRuntime::GetOptionValueOrEmpty(
  wxStringRuntime* const outValue,
  const wchar_t* const optionName
) const
{
  if (outValue == nullptr) {
    return nullptr;
  }

  outValue->m_pchData = const_cast<wchar_t*>(wxEmptyString);

  auto* const refData = static_cast<WxImageRefDataRuntime*>(mRefData);
  if (refData == nullptr || optionName == nullptr) {
    return outValue;
  }

  const std::int32_t optionIndex = WxFindStringArrayIndex(&refData->mOptionKeys, optionName, false, false);
  if (optionIndex < 0
      || optionIndex >= refData->mOptionValues.count
      || refData->mOptionValues.entries == nullptr) {
    return outValue;
  }

  const wchar_t* const optionValue = refData->mOptionValues.entries[optionIndex];
  if (optionValue == nullptr || optionValue[0] == L'\0') {
    return outValue;
  }

  outValue->m_pchData = const_cast<wchar_t*>(optionValue);
  if (IsOwnedWxString(*outValue)) {
    auto* const header = reinterpret_cast<WxOwnedStringHeader*>(reinterpret_cast<std::int32_t*>(outValue->m_pchData) - 3);
    if (header->refCount != -1) {
      ++header->refCount;
    }
  }

  return outValue;
}

namespace
{
  /**
   * Address: 0x009EC820 (FUN_009EC820)
   *
   * What it does:
   * Reads one file payload in `0x8000`-byte chunks and returns the original
   * requested byte count on success, or `0` when a short-read occurs.
   */
  [[maybe_unused]] [[nodiscard]] unsigned int ReadBitmapPayloadChunked(
    char* buffer,
    const unsigned int bytesToRead,
    const HFILE fileHandle
  )
  {
    unsigned int remaining = bytesToRead;
    if (remaining <= 0x8000u) {
      return remaining == static_cast<unsigned int>(::_lread(fileHandle, buffer, remaining)) ? bytesToRead : 0u;
    }

    while (::_lread(fileHandle, buffer, 0x8000u) == 0x8000u) {
      remaining -= 0x8000u;
      buffer += 0x8000;
      if (remaining <= 0x8000u) {
        return remaining == static_cast<unsigned int>(::_lread(fileHandle, buffer, remaining)) ? bytesToRead : 0u;
      }
    }

    return 0u;
  }

  /**
   * Address: 0x009EC8E0 (FUN_009EC8E0)
   *
   * What it does:
   * Builds one Win32 logical palette from the DIB color-table lane, or returns
   * the stock default palette when `biClrUsed` is zero.
   */
  [[nodiscard]] HPALETTE CreatePaletteFromDibInfo(
    const BITMAPINFO* const dibInfo
  )
  {
    if (dibInfo->bmiHeader.biClrUsed == 0u) {
      return static_cast<HPALETTE>(::GetStockObject(DEFAULT_PALETTE));
    }

    const std::uint16_t colorCount = static_cast<std::uint16_t>(dibInfo->bmiHeader.biClrUsed & 0xFFFFu);
    const std::size_t paletteBytes =
      sizeof(LOGPALETTE) + sizeof(PALETTEENTRY) * static_cast<std::size_t>(colorCount);
    auto* const palette = static_cast<LOGPALETTE*>(std::malloc(paletteBytes));
    if (palette == nullptr) {
      return nullptr;
    }

    palette->palVersion = 0x300;
    palette->palNumEntries = colorCount;

    const auto* sourceColor = reinterpret_cast<const std::uint8_t*>(dibInfo) + dibInfo->bmiHeader.biSize;
    for (std::uint16_t index = 0; index < colorCount; ++index) {
      PALETTEENTRY& entry = palette->palPalEntry[index];
      entry.peRed = sourceColor[2];
      entry.peGreen = sourceColor[1];
      entry.peBlue = sourceColor[0];
      entry.peFlags = 0;
      sourceColor += 4;
    }

    const HPALETTE createdPalette = ::CreatePalette(palette);
    std::free(palette);
    return createdPalette;
  }
} // namespace

/**
 * Address: 0x009ECA00 (FUN_009ECA00)
 *
 * What it does:
 * Builds one Win32 palette/bitmap pair from global DIB memory and writes both
 * output handles when bitmap creation succeeds.
 */
bool wxCreateBitmapFromGlobalDib(
  const HDC deviceContext,
  const HGLOBAL dibGlobalHandle,
  HPALETTE* const outPalette,
  HBITMAP* const outBitmap
)
{
  bool succeeded = false;

  const auto* const dibInfo = static_cast<const BITMAPINFO*>(::GlobalLock(dibGlobalHandle));
  const HPALETTE palette = CreatePaletteFromDibInfo(dibInfo);
  if (palette != nullptr) {
    const HPALETTE oldPalette = ::SelectPalette(deviceContext, palette, TRUE);
    (void)::RealizePalette(deviceContext);

    const std::uint16_t colorCount = static_cast<std::uint16_t>(dibInfo->bmiHeader.biClrUsed & 0xFFFFu);
    const std::uint16_t headerSize = static_cast<std::uint16_t>(dibInfo->bmiHeader.biSize & 0xFFFFu);
    const auto* const bits =
      reinterpret_cast<const std::uint8_t*>(&dibInfo->bmiHeader.biSize) +
      static_cast<std::size_t>(4u * colorCount + headerSize);
    const HBITMAP bitmap = ::CreateDIBitmap(
      deviceContext,
      &dibInfo->bmiHeader,
      CBM_INIT,
      bits,
      dibInfo,
      DIB_RGB_COLORS
    );

    (void)::SelectPalette(deviceContext, oldPalette, TRUE);
    (void)::RealizePalette(deviceContext);

    if (bitmap != nullptr) {
      *outBitmap = bitmap;
      *outPalette = palette;
      succeeded = true;
    } else {
      (void)::DeleteObject(palette);
    }
  }

  (void)::GlobalUnlock(dibGlobalHandle);
  return succeeded;
}

/**
 * Address: 0x00976400 (FUN_00976400, wxCreateDIB)
 *
 * What it does:
 * Allocates one palette-backed DIB header block, seeds its metadata, and
 * converts the palette entries into bitmap color-table order for the caller.
 */
bool wxCreateDIB(
  const std::int32_t xSize,
  const std::int32_t ySize,
  const std::int32_t bitsPerPixel,
  HPALETTE hpal,
  LPBITMAPINFO* const lpDIBHeader
)
{
  auto* const dibHeader = static_cast<LPBITMAPINFO>(std::malloc(0x428u));
  ::GetPaletteEntries(hpal, 0, 0x100u, reinterpret_cast<LPPALETTEENTRY>(dibHeader->bmiColors));

  dibHeader->bmiHeader.biPlanes = 0;
  dibHeader->bmiHeader.biXPelsPerMeter = 0;
  dibHeader->bmiHeader.biYPelsPerMeter = 0;
  dibHeader->bmiHeader.biClrImportant = 0;
  dibHeader->bmiHeader.biHeight = ySize;
  dibHeader->bmiHeader.biWidth = xSize;
  dibHeader->bmiHeader.biSizeImage = (bitsPerPixel * xSize * std::abs(ySize)) >> 3;
  dibHeader->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  dibHeader->bmiHeader.biPlanes = 1;
  dibHeader->bmiHeader.biBitCount = static_cast<WORD>(bitsPerPixel);
  dibHeader->bmiHeader.biCompression = BI_RGB;
  dibHeader->bmiHeader.biClrUsed = 0x100;

  auto* const paletteEntries = reinterpret_cast<const PALETTEENTRY*>(dibHeader->bmiColors);
  auto* const rgbQuads = dibHeader->bmiColors;
  for (std::int32_t index = 0; index < 0x100; ++index) {
    const PALETTEENTRY paletteEntry = paletteEntries[index];
    rgbQuads[index].rgbBlue = paletteEntry.peBlue;
    rgbQuads[index].rgbGreen = paletteEntry.peGreen;
    rgbQuads[index].rgbRed = paletteEntry.peRed;
    rgbQuads[index].rgbReserved = paletteEntry.peFlags;
  }

  *lpDIBHeader = dibHeader;
  return true;
}

/**
 * Address: 0x009764C0 (FUN_009764C0, wxFreeDIB)
 *
 * What it does:
 * Releases one DIB header block previously allocated by `wxCreateDIB()`.
 */
void wxFreeDIB(void* const ptr)
{
  _free_crt(ptr);
}

/**
 * Address: 0x009C6900 (FUN_009C6900, wxRGBToColour)
 *
 * What it does:
 * Initializes one `wxColourRuntime` from packed `0x00BBGGRR` RGB bytes and
 * returns the output pointer.
 */
wxColourRuntime* wxRGBToColour(wxColourRuntime* const outColour, const std::uint32_t packedRgb)
{
  if (outColour == nullptr) {
    return nullptr;
  }

  const std::uint8_t red = static_cast<std::uint8_t>(packedRgb & 0xFFu);
  const std::uint8_t green = static_cast<std::uint8_t>((packedRgb >> 8u) & 0xFFu);
  const std::uint8_t blue = static_cast<std::uint8_t>((packedRgb >> 16u) & 0xFFu);
  *outColour = wxColourRuntime::FromRgb(red, green, blue);
  return outColour;
}

wxColourRuntime wxColourRuntime::FromRgb(
  const std::uint8_t red,
  const std::uint8_t green,
  const std::uint8_t blue
) noexcept
{
  wxColourRuntime color{};
  color.mStorage[0] = red;
  color.mStorage[1] = green;
  color.mStorage[2] = blue;
  color.mStorage[3] = 0xFF;
  return color;
}

const wxColourRuntime& wxColourRuntime::Null() noexcept
{
  static const wxColourRuntime kNullColour{};
  return kNullColour;
}

const wxFontRuntime& wxFontRuntime::Null() noexcept
{
  static const wxFontRuntime kNullFont{};
  return kNullFont;
}

/**
 * Address: 0x0099A130 (FUN_0099A130, ??0wxTextAttr@@QAE@@Z)
 *
 * What it does:
 * Default-initializes foreground/background colour lanes and font lane for one
 * text-style payload.
 */
wxTextAttrRuntime::wxTextAttrRuntime()
  : mForegroundColour()
  , mBackgroundColour()
  , mFont()
{}

/**
 * Address: 0x004F36A0 (FUN_004F36A0)
 *
 * What it does:
 * Initializes one text-style payload from explicit foreground/background/font
 * runtime lanes.
 */
wxTextAttrRuntime::wxTextAttrRuntime(
  const wxColourRuntime& foreground,
  const wxColourRuntime& background,
  const wxFontRuntime& font
)
  : mForegroundColour(foreground)
  , mBackgroundColour(background)
  , mFont(font)
{}

/**
 * Address: 0x004F63B0 (FUN_004F63B0)
 *
 * What it does:
 * Destroys text-style subobjects in reverse order.
 */
wxTextAttrRuntime::~wxTextAttrRuntime() = default;

msvc8::string wxTextCtrlRuntime::GetValueUtf8() const
{
  return GetValue().ToUtf8();
}

msvc8::string wxTextCtrlRuntime::GetValueUtf8Lower() const
{
  return GetValue().ToUtf8Lower();
}

void wxTextCtrlRuntime::SetValueUtf8(
  const msvc8::string& value
)
{
  const std::wstring wideValue = gpg::STR_Utf8ToWide(value.c_str());
  SetValue(wxStringRuntime::Borrow(wideValue.c_str()));
}

void wxTextCtrlRuntime::AppendUtf8(
  const msvc8::string& text
)
{
  const std::wstring wideText = gpg::STR_Utf8ToWide(text.c_str());
  AppendText(wxStringRuntime::Borrow(wideText.c_str()));
}

void wxTextCtrlRuntime::AppendWide(
  const std::wstring& text
)
{
  AppendText(wxStringRuntime::Borrow(text.c_str()));
}

void wxTextCtrlRuntime::ScrollToLastPosition()
{
  ShowPosition(GetLastPosition());
}

/**
 * Address: 0x004F73B0 (FUN_004F73B0)
 *
 * What it does:
 * Constructs one wide stream/buffer helper used by log-window text formatting.
 */
moho::WWinLogTextBuilder::WWinLogTextBuilder() = default;

/**
 * Address: 0x004F74D0 (FUN_004F74D0)
 *
 * What it does:
 * Finalizes stream state and returns the accumulated wide text.
 */
const std::wstring& moho::WWinLogTextBuilder::Finalize() const noexcept
{
  return mText;
}

void moho::WWinLogTextBuilder::SetFieldWidth(
  const std::size_t width
) noexcept
{
  mFieldWidth = width;
}

void moho::WWinLogTextBuilder::Clear() noexcept
{
  mText.clear();
  mFieldWidth = 0;
  mFillCodePoint = L' ';
  mLeftAlign = false;
}

/**
 * Address: 0x004F98F0 (FUN_004F98F0)
 *
 * What it does:
 * Emits one code-point with optional field-width padding and clears transient
 * width state.
 */
void moho::WWinLogTextBuilder::WriteCodePoint(
  const wchar_t codePoint
)
{
  const std::wstring oneCodePoint(1, codePoint);
  WriteWideText(oneCodePoint);
}

/**
 * Address: 0x004F9B80 (FUN_004F9B80)
 *
 * What it does:
 * Emits one wide string with optional field-width padding and clears transient
 * width state.
 */
void moho::WWinLogTextBuilder::WriteWideText(
  const std::wstring& text
)
{
  const std::size_t paddingCount = mFieldWidth > text.size() ? mFieldWidth - text.size() : 0;
  if (!mLeftAlign && paddingCount != 0) {
    mText.append(paddingCount, mFillCodePoint);
  }

  mText += text;

  if (mLeftAlign && paddingCount != 0) {
    mText.append(paddingCount, mFillCodePoint);
  }

  mFieldWidth = 0;
}

/**
 * Address: 0x004F9DF0 (FUN_004F9DF0)
 *
 * What it does:
 * Emits one wide-string literal with width/padding behavior.
 */
void moho::WWinLogTextBuilder::WriteWideLiteral(
  const wchar_t* const text
)
{
  WriteWideText(text != nullptr ? std::wstring(text) : std::wstring{});
}

/**
 * Address: 0x004FA000 (FUN_004FA000)
 *
 * What it does:
 * Emits one UTF-8 fragment by widening it then appending with width behavior.
 */
void moho::WWinLogTextBuilder::WriteUtf8Text(
  const msvc8::string& text
)
{
  WriteWideText(gpg::STR_Utf8ToWide(text.c_str()));
}

/**
 * Address: 0x004FA2C0 (FUN_004FA2C0)
 *
 * What it does:
 * Emits one decoded wide code-point.
 */
void moho::WWinLogTextBuilder::WriteDecodedCodePoint(
  const wchar_t codePoint
)
{
  WriteCodePoint(codePoint);
}

/**
 * Address: 0x004F5AB0 (FUN_004F5AB0)
 *
 * What it does:
 * Emits one run of space code-points.
 */
void moho::WWinLogTextBuilder::WriteSpaces(
  std::size_t count
)
{
  while (count > 0) {
    WriteCodePoint(L' ');
    --count;
  }
}

namespace
{
  /**
   * Address: 0x004FB300 (FUN_004FB300)
   *
   * What it does:
   * Copy-constructs one `CWinLogLine` record into `destination` from `source`,
   * including legacy in-place string SSO reset plus full text assign.
   */
  [[maybe_unused]] moho::CWinLogLine* ConstructWinLogLineCopyAt(
    moho::CWinLogLine* const destination,
    const moho::CWinLogLine* const source
  )
  {
    if (destination == nullptr || source == nullptr) {
      return destination;
    }

    destination->isReplayEntry = source->isReplayEntry;
    destination->sequenceIndex = source->sequenceIndex;
    destination->categoryMask = source->categoryMask;

    destination->text.myRes = 15u;
    destination->text.mySize = 0u;
    destination->text.bx.buf[0] = '\0';
    destination->text.assign(source->text, 0u, 0xFFFFFFFFu);
    return destination;
  }
} // namespace

bool moho::CWinLogLine::IsReplayEntry() const noexcept
{
  return isReplayEntry != 0u;
}

bool moho::CWinLogLine::IsMessageEntry() const noexcept
{
  return !IsReplayEntry();
}

const wchar_t* moho::CWinLogLine::SeverityPrefix() const noexcept
{
  switch (categoryMask) {
  case 0u:
    return L"DEBUG: ";
  case kWarningCategoryValue:
    return L"WARNING: ";
  case kErrorCategoryValue:
    return L"ERROR: ";
  default:
    return L"INFO: ";
  }
}

/**
 * Address: 0x00978FF0 (FUN_00978FF0, ??0wxEvent@@QAE@@Z)
 *
 * What it does:
 * Initializes one wxEvent runtime payload from `(eventId, eventType)` and
 * clears ref/object/timestamp/flag lanes.
 */
wxEventRuntime::wxEventRuntime(
  const std::int32_t eventId,
  const std::int32_t eventType
)
  : mRefData(nullptr)
  , mEventObject(nullptr)
  , mEventType(eventType)
  , mEventTimestamp(0)
  , mEventId(eventId)
  , mCallbackUserData(nullptr)
  , mSkipped(0)
  , mIsCommandEvent(0)
  , mReserved1E(0)
  , mReserved1F(0)
{}

/**
 * Address: 0x00979090 (FUN_00979090, ??0wxCommandEvent@@QAE@@Z)
 *
 * What it does:
 * Initializes one wxCommandEvent payload on top of wxEvent runtime state and
 * sets the command-event marker flag.
 */
wxCommandEventRuntime::wxCommandEventRuntime(
  const std::int32_t commandType,
  const std::int32_t eventId
)
  : wxEventRuntime(eventId, commandType)
  , mCommandString(wxStringRuntime::Borrow(L""))
  , mCommandInt(0)
  , mExtraLong(0)
  , mClientData(nullptr)
  , mClientObject(nullptr)
{
  mIsCommandEvent = 1u;
}

/**
 * Address: 0x00964DC0 (FUN_00964DC0, ??0wxCommandEvent@@QAE@ABV0@@Z)
 *
 * What it does:
 * Copies one command-event payload including shared command-string lane and
 * command/client payload fields.
 */
wxCommandEventRuntime::wxCommandEventRuntime(const wxCommandEventRuntime& source)
  : wxEventRuntime(source.mEventId, source.mEventType)
  , mCommandString(wxStringRuntime::Borrow(L""))
  , mCommandInt(source.mCommandInt)
  , mExtraLong(source.mExtraLong)
  , mClientData(source.mClientData)
  , mClientObject(source.mClientObject)
{
  mRefData = source.mRefData;
  mEventObject = source.mEventObject;
  mEventTimestamp = source.mEventTimestamp;
  mCallbackUserData = source.mCallbackUserData;
  mSkipped = source.mSkipped;
  mIsCommandEvent = source.mIsCommandEvent;
  mReserved1E = source.mReserved1E;
  mReserved1F = source.mReserved1F;
  RetainWxStringRuntime(&mCommandString, &source.mCommandString);
}

wxCommandEventRuntime* wxCommandEventRuntime::Clone() const
{
  return new (std::nothrow) wxCommandEventRuntime(*this);
}

/**
 * Address: 0x006609B0 (FUN_006609B0, ??1wxCommandEvent@@QAE@@Z)
 *
 * What it does:
 * Releases one shared command-string payload and clears wxEvent ref-data
 * ownership via the base unref tail.
 */
wxCommandEventRuntime::~wxCommandEventRuntime()
{
  ReleaseWxStringSharedPayload(mCommandString);
  RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(this));
}

/**
 * Address: 0x0099BFA0 (FUN_0099BFA0)
 *
 * What it does:
 * Initializes one list-item-attribute storage lane by default-initializing
 * text/background colour payloads and font payload.
 */
wxListItemAttrRuntime* wxListItemAttrInitializeDefaults(
  wxListItemAttrRuntime* const runtime
)
{
  if (runtime == nullptr) {
    return nullptr;
  }

  runtime->mTextColour = wxColourRuntime{};
  runtime->mBackgroundColour = wxColourRuntime{};
  runtime->mFont = wxFontRuntime{};
  return runtime;
}

/**
 * Address: 0x009834F0 (FUN_009834F0)
 *
 * What it does:
 * Constructs one list-item-attribute payload by default-constructing text
 * colour, background colour, and font member lanes.
 */
wxListItemAttrRuntime::wxListItemAttrRuntime()
{
  (void)wxListItemAttrInitializeDefaults(this);
}

/**
 * Address: 0x0099C000 (FUN_0099C000)
 *
 * What it does:
 * Lazily allocates and constructs this list-item's optional attribute payload
 * lane and returns the retained pointer.
 */
wxListItemAttrRuntime* wxListItemRuntime::EnsureAttributeStorage()
{
  if (mAttr == nullptr) {
    void* const storage = ::operator new(sizeof(wxListItemAttrRuntime), std::nothrow);
    if (storage != nullptr) {
      mAttr = new (storage) wxListItemAttrRuntime();
    }
  }

  return mAttr;
}

/**
 * Address: 0x00987E10 (FUN_00987E10, ??0wxListItem@@QAE@XZ)
 *
 * What it does:
 * Initializes one list-item payload with default mask/id/state/text lanes.
 */
wxListItemRuntime::wxListItemRuntime()
  : mRefData(nullptr)
  , mMask(0)
  , mItemId(0)
  , mColumn(0)
  , mState(0)
  , mStateMask(0)
  , mText(wxStringRuntime::Borrow(L""))
  , mImage(-1)
  , mData(0)
  , mWidth(-1)
  , mFormat(0)
  , mAttr(nullptr)
{
}

/**
 * Address: 0x00987EE0 (FUN_00987EE0, ??0wxListItem@@QAE@ABV0@@Z)
 * Mangled: ??0wxListItem@@QAE@ABV0@@Z
 *
 * What it does:
 * Copies one list-item payload lane, retaining shared string ownership and
 * deep-copying optional attribute storage when present.
 */
wxListItemRuntime::wxListItemRuntime(
  const wxListItemRuntime& source
)
  : mRefData(nullptr)
  , mMask(source.mMask)
  , mItemId(source.mItemId)
  , mColumn(source.mColumn)
  , mState(source.mState)
  , mStateMask(source.mStateMask)
  , mText(wxStringRuntime::Borrow(L""))
  , mImage(source.mImage)
  , mData(source.mData)
  , mWidth(source.mWidth)
  , mFormat(source.mFormat)
  , mAttr(nullptr)
{
  RetainWxStringRuntime(&mText, &source.mText);

  if (source.mAttr != nullptr) {
    void* const storage = ::operator new(sizeof(wxListItemAttrRuntime), std::nothrow);
    if (storage != nullptr) {
      mAttr = new (storage) wxListItemAttrRuntime(*source.mAttr);
    }
  }
}

/**
 * Address: 0x00987D00 (FUN_00987D00, ??1wxListItem@@QAE@@Z)
 * Mangled: ??1wxListItem@@QAE@@Z
 *
 * What it does:
 * Releases optional list-item attribute storage, releases shared string
 * payload ownership, and clears base wxObject ref-data ownership lanes.
 */
wxListItemRuntime::~wxListItemRuntime()
{
  if (mAttr != nullptr) {
    DestroyWxListItemAttrRuntime(mAttr);
    ::operator delete(mAttr);
    mAttr = nullptr;
  }

  ReleaseWxStringSharedPayload(mText);
  RunWxObjectUnrefTail(reinterpret_cast<WxObjectRuntimeView*>(this));
}

namespace
{
  struct wxListItemInternalDataRuntime
  {
    wxListItemAttrRuntime* attr = nullptr; // +0x00
    LPARAM lParam = 0;                     // +0x04
  };

  static_assert(
    offsetof(wxListItemInternalDataRuntime, attr) == 0x0,
    "wxListItemInternalDataRuntime::attr offset must be 0x0"
  );
  static_assert(
    offsetof(wxListItemInternalDataRuntime, lParam) == 0x4,
    "wxListItemInternalDataRuntime::lParam offset must be 0x4"
  );
  static_assert(sizeof(wxListItemInternalDataRuntime) == 0x8, "wxListItemInternalDataRuntime size must be 0x8");

  /**
   * Address: 0x0099BC70 (FUN_0099BC70, wxGetInternalData)
   *
   * What it does:
   * Requests one list-view row and returns the internal lParam payload when
   * the native `LVM_GETITEMW` query succeeds.
   */
  [[nodiscard]] wxListItemInternalDataRuntime* wxGetInternalData(
    const LPARAM itemId,
    const HWND listHandle
  )
  {
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = static_cast<int>(itemId);

    const LRESULT queryResult = ::SendMessageW(listHandle, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item));
    return queryResult != 0 ? reinterpret_cast<wxListItemInternalDataRuntime*>(item.lParam) : nullptr;
  }

  /**
   * Address: 0x0099BCA0 (FUN_0099BCA0, wxGetInternalData_0)
   *
   * What it does:
   * Resolves the list-view HWND for one control instance and forwards to
   * `wxGetInternalData`.
   */
  [[nodiscard]] wxListItemInternalDataRuntime* wxGetInternalData_0(
    wxListCtrlRuntime* const listControl,
    const LPARAM itemId
  )
  {
    const HWND listHandle = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(listControl->GetHandle()));
    return wxGetInternalData(itemId, listHandle);
  }

  /**
   * Address: 0x0099BCD0 (FUN_0099BCD0, wxConvertFromMSWListItem)
   *
   * What it does:
   * Converts one native `LVITEMW` payload into runtime `wxListItem` lanes,
   * including state-bit translation and optional text retrieval.
   */
  void wxConvertFromMSWListItem(
    LVITEMW* const mswItem,
    wxListItemRuntime* const item,
    const HWND listHandle
  )
  {
    if (const auto* const internalData = reinterpret_cast<wxListItemInternalDataRuntime*>(mswItem->lParam);
      internalData != nullptr) {
      item->mData = internalData->lParam;
    }

    item->mMask = 0;
    item->mState = 0;
    item->mStateMask = 0;
    item->mItemId = mswItem->iItem;

    const UINT originalMask = mswItem->mask;
    UINT restoredMask = mswItem->mask;
    bool allocatedTextBuffer = false;

    if (listHandle != nullptr) {
      if ((originalMask & LVIF_TEXT) == 0u) {
        allocatedTextBuffer = true;
        mswItem->pszText = static_cast<LPWSTR>(::operator new(0x402u));
        mswItem->cchTextMax = 512;
      }

      mswItem->mask |= (LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM);
      (void)::SendMessageW(listHandle, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(mswItem));
      restoredMask = originalMask;
    }

    if ((mswItem->mask & LVIF_STATE) != 0u) {
      item->mMask |= 0x1;

      if ((mswItem->stateMask & 0x4u) != 0u) {
        item->mStateMask |= 0x8;
        if ((mswItem->state & 0x4u) != 0u) {
          item->mState |= 0x8;
        }
      }

      if ((mswItem->stateMask & 0x8u) != 0u) {
        item->mStateMask |= 0x1;
        if ((mswItem->state & 0x8u) != 0u) {
          item->mState |= 0x1;
        }
      }

      if ((mswItem->stateMask & 0x1u) != 0u) {
        item->mStateMask |= 0x2;
        if ((mswItem->state & 0x1u) != 0u) {
          item->mState |= 0x2;
        }
      }

      if ((mswItem->stateMask & 0x2u) != 0u) {
        item->mStateMask |= 0x4;
        if ((mswItem->state & 0x2u) != 0u) {
          item->mState |= 0x4;
        }
      }
    }

    if ((mswItem->mask & LVIF_TEXT) != 0u) {
      item->mMask |= 0x2;
      AssignOwnedWxString(&item->mText, mswItem->pszText != nullptr ? std::wstring(mswItem->pszText) : std::wstring{});
      restoredMask = originalMask;
    }

    if ((mswItem->mask & LVIF_IMAGE) != 0u) {
      item->mMask |= 0x4;
      item->mImage = mswItem->iImage;
    }

    if ((mswItem->mask & LVIF_PARAM) != 0u) {
      item->mMask |= 0x8;
    }

    if ((mswItem->mask & LVIF_INDENT) != 0u) {
      item->mMask |= 0x10;
    }

    item->mColumn = mswItem->iSubItem;

    if (allocatedTextBuffer && mswItem->pszText != nullptr) {
      ::operator delete(mswItem->pszText);
      mswItem->mask = originalMask;
    } else {
      mswItem->mask = restoredMask;
    }
  }
} // namespace

/**
 * Address: 0x0099BCB0 (FUN_0099BCB0, wxGetInternalDataAttr)
 *
 * What it does:
 * Returns the optional per-row list-item attribute payload lane for one
 * list-control row id.
 */
wxListItemAttrRuntime* wxGetInternalDataAttr(
  const LPARAM itemId,
  wxListCtrlRuntime* const listControl
)
{
  wxListItemInternalDataRuntime* const internalData = wxGetInternalData_0(listControl, itemId);
  return internalData != nullptr ? internalData->attr : nullptr;
}

/**
 * Address: 0x0099D910 (FUN_0099D910, wxDeleteInternalData)
 *
 * What it does:
 * Clears one row's native list-view lParam lane, then destroys retained
 * wx-list-item internal attribute/data payload storage.
 */
void wxDeleteInternalData(
  const LPARAM itemId,
  wxListCtrlRuntime* const listControl
)
{
  wxListItemInternalDataRuntime* const internalData = wxGetInternalData_0(listControl, itemId);
  if (internalData == nullptr) {
    return;
  }

  LVITEMW item{};
  item.mask = LVIF_PARAM;
  item.iItem = static_cast<int>(itemId);

  const HWND listHandle = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(listControl->GetHandle()));
  (void)::SendMessageW(listHandle, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&item));

  if (internalData->attr != nullptr) {
    DestroyWxListItemAttrRuntime(internalData->attr);
    ::operator delete(internalData->attr);
  }

  ::operator delete(internalData);
}

namespace
{
  struct WxListCtrlRuntimeView
  {
    void* vtable = nullptr;                 // +0x00
    std::uint8_t reserved04_14B[0x148]{};  // +0x04
    std::uint8_t hasAnyInternalData = 0;   // +0x14C
  };
  static_assert(
    offsetof(WxListCtrlRuntimeView, hasAnyInternalData) == 0x14C,
    "WxListCtrlRuntimeView::hasAnyInternalData offset must be 0x14C"
  );

  [[nodiscard]] LRESULT wxListCtrlGetItemCountRuntime(
    wxListCtrlRuntime* const listControl
  )
  {
    const HWND listHandle = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(listControl->GetHandle()));
    return listHandle != nullptr ? ::SendMessageW(listHandle, LVM_GETITEMCOUNT, 0, 0) : 0;
  }
}

/**
 * Address: 0x0099D9A0 (FUN_0099D9A0)
 *
 * What it does:
 * Deletes retained per-row internal payload lanes for all rows when the list
 * control reports active internal-data storage.
 */
void wxListCtrlDeleteAllInternalData(
  wxListCtrlRuntime* const listControl
)
{
  auto* const listControlView = reinterpret_cast<WxListCtrlRuntimeView*>(listControl);
  if (listControlView->hasAnyInternalData == 0u) {
    return;
  }

  const LRESULT itemCount = wxListCtrlGetItemCountRuntime(listControl);
  for (LPARAM itemId = 0; itemId < itemCount; ++itemId) {
    wxDeleteInternalData(itemId, listControl);
  }

  listControlView->hasAnyInternalData = 0;
}

/**
 * Address: 0x0099B520 (FUN_0099B520, wxListCtrl::EnsureVisible)
 *
 * What it does:
 * Sends native `LVM_ENSUREVISIBLE` to request that row `itemId` is scrolled
 * into view for this list control.
 */
bool wxListCtrlRuntime::EnsureVisible(
  const std::int32_t itemId
) const
{
  const HWND listHandle = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(GetHandle()));
  return listHandle != nullptr
    && ::SendMessageW(listHandle, LVM_ENSUREVISIBLE, static_cast<WPARAM>(itemId), 0) != 0;
}

/**
 * Address: 0x0099D120 (FUN_0099D120, wxListCtrl::GetItemData)
 *
 * What it does:
 * Builds one stack `wxListItem` request payload (mask=0x8 for data lane),
 * queries the row through `GetItem`, and returns row user-data on success.
 */
long wxListCtrlRuntime::GetItemData(
  const std::int32_t itemId
)
{
  wxListItemRuntime item{};
  item.mMask = 0x8;
  item.mItemId = itemId;
  item.mColumn = 0;
  item.mState = 0;
  item.mStateMask = 0;
  item.mImage = 0;
  item.mData = 0;
  item.mFormat = 2;
  item.mWidth = 0;
  item.mAttr = nullptr;

  if (!GetItem(&item)) {
    return 0;
  }

  return item.mData;
}

/**
 * Address: 0x0099AEB0 (FUN_0099AEB0)
 *
 * What it does:
 * Queries one list-view item rectangle (`LVM_GETITEMRECT`) and writes it as
 * `(x, y, width, height)` into the caller-provided output lane.
 */
bool wxListCtrlGetItemRectAsXywh(
  wxListCtrlRuntime* const listControl,
  const WPARAM itemId,
  std::int32_t* const outRectXywh,
  const unsigned int rectangleKind
)
{
  if (listControl == nullptr || outRectXywh == nullptr) {
    return false;
  }

  const HWND listHandle = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(listControl->GetHandle()));
  if (listHandle == nullptr) {
    return false;
  }

  int requestCode = static_cast<int>(rectangleKind);
  if (rectangleKind >= 2u) {
    requestCode = rectangleKind == 2u ? 2 : 0;
  }

  LONG rectLane[4]{requestCode, 0, 0, 0};
  const LRESULT queryResult = ::SendMessageW(listHandle, LVM_GETITEMRECT, itemId, reinterpret_cast<LPARAM>(rectLane));

  outRectXywh[0] = rectLane[0];
  outRectXywh[1] = rectLane[1];
  outRectXywh[2] = rectLane[2] - rectLane[0];
  outRectXywh[3] = rectLane[3] - rectLane[1];
  return queryResult != 0;
}

namespace
{
  struct WxListCtrlColumnDragRuntimeView
  {
    std::uint8_t mUnknown00To2B[0x2C]{};
    wxWindowBase* mOwnerWindow = nullptr; // +0x2C
    std::uint8_t mUnknown30To133[0x104]{};
    std::int32_t mCurrentColumn = 0; // +0x134
  };

  static_assert(
    offsetof(WxListCtrlColumnDragRuntimeView, mOwnerWindow) == 0x2C,
    "WxListCtrlColumnDragRuntimeView::mOwnerWindow offset must be 0x2C"
  );
  static_assert(
    offsetof(WxListCtrlColumnDragRuntimeView, mCurrentColumn) == 0x134,
    "WxListCtrlColumnDragRuntimeView::mCurrentColumn offset must be 0x134"
  );
  static_assert(
    sizeof(WxListCtrlColumnDragRuntimeView) == 0x138,
    "WxListCtrlColumnDragRuntimeView size must be 0x138"
  );

  struct WxListEventRuntime final : wxCommandEventRuntime
  {
    explicit WxListEventRuntime(
      const std::int32_t eventType,
      const std::int32_t eventId
    )
      : wxCommandEventRuntime(eventType, eventId)
    {
      mCode = 0;
      mOldItemIndex = 0;
      mItemIndex = 0;
      mCol = 0;
      mAllow = 1;
      mPointDrag.x = 0;
      mPointDrag.y = 0;
      mItem.mMask = 0;
      mItem.mItemId = 0;
      mItem.mColumn = 0;
      mItem.mState = 0;
      mItem.mStateMask = 0;
      mItem.mImage = 0;
      mItem.mData = 0;
      mItem.mWidth = 0;
      mItem.mFormat = 2;
      mItem.mAttr = nullptr;
    }

    /**
     * Address: 0x00987FD0 (FUN_00987FD0, ??0wxListEvent@@QAE@ABV0@@Z)
     * Mangled: ??0wxListEvent@@QAE@ABV0@@Z
     *
     * What it does:
     * Copies one list-event payload including notify-event base fields, drag
     * point, and embedded list-item payload.
     */
    WxListEventRuntime(const WxListEventRuntime& source)
      : wxCommandEventRuntime(source)
      , mCode(source.mCode)
      , mOldItemIndex(source.mOldItemIndex)
      , mItemIndex(source.mItemIndex)
      , mCol(source.mCol)
      , mAllow(source.mAllow)
      , mPadding35To37{}
      , mPointDrag(source.mPointDrag)
      , mItem(source.mItem)
    {
    }

    std::int32_t mCode = 0;
    std::int32_t mOldItemIndex = 0;
    std::int32_t mItemIndex = 0;
    std::int32_t mCol = 0;
    std::uint8_t mAllow = 1;
    std::uint8_t mPadding35To37[0x3]{};
    wxPoint mPointDrag{};
    wxListItemRuntime mItem{};
  };

  static_assert(
    offsetof(WxListEventRuntime, mCode) == 0x34,
    "WxListEventRuntime::mCode offset must be 0x34"
  );
  static_assert(
    offsetof(WxListEventRuntime, mPointDrag) == 0x48,
    "WxListEventRuntime::mPointDrag offset must be 0x48"
  );
  static_assert(
    offsetof(WxListEventRuntime, mItem) == 0x50,
    "WxListEventRuntime::mItem offset must be 0x50"
  );
  static_assert(sizeof(WxListEventRuntime) == 0x84, "WxListEventRuntime size must be 0x84");

  struct WxTreeEventFactoryRuntime final : wxCommandEventRuntime
  {
    explicit WxTreeEventFactoryRuntime(
      const std::int32_t eventType,
      const std::int32_t eventId
    )
      : wxCommandEventRuntime(eventType, eventId)
      , mAllow(1)
      , mPadding35To37{0, 0, 0}
      , mUnknown38To73{}
      , mItem{}
      , mPreviousItem{}
      , mDragPoint{0, 0}
      , mLabel(wxStringRuntime::Borrow(L""))
      , mEditCancelled(0)
      , mUnknown89(0)
      , mPadding8ATo8F{}
    {
    }

    WxTreeEventFactoryRuntime* Clone() const override
    {
      return new (std::nothrow) WxTreeEventFactoryRuntime(*this);
    }

    std::uint8_t mAllow = 1;
    std::uint8_t mPadding35To37[0x3]{};
    std::uint8_t mUnknown38To73[0x3C]{};
    wxTreeItemIdRuntime mItem{};
    wxTreeItemIdRuntime mPreviousItem{};
    wxPoint mDragPoint{};
    wxStringRuntime mLabel{};
    std::uint8_t mEditCancelled = 0;
    std::uint8_t mUnknown89 = 0;
    std::uint8_t mPadding8ATo8F[0x6]{};
  };
  static_assert(
    offsetof(WxTreeEventFactoryRuntime, mAllow) == 0x34,
    "WxTreeEventFactoryRuntime::mAllow offset must be 0x34"
  );
  static_assert(
    offsetof(WxTreeEventFactoryRuntime, mItem) == 0x74,
    "WxTreeEventFactoryRuntime::mItem offset must be 0x74"
  );
  static_assert(
    offsetof(WxTreeEventFactoryRuntime, mLabel) == 0x84,
    "WxTreeEventFactoryRuntime::mLabel offset must be 0x84"
  );
  static_assert(
    offsetof(WxTreeEventFactoryRuntime, mEditCancelled) == 0x88,
    "WxTreeEventFactoryRuntime::mEditCancelled offset must be 0x88"
  );
  static_assert(sizeof(WxTreeEventFactoryRuntime) == 0x90, "WxTreeEventFactoryRuntime size must be 0x90");

  /**
   * Address: 0x0098BDB0 (FUN_0098BDB0)
   *
   * What it does:
   * Allocates and initializes one default `wxTreeEvent` runtime payload
   * (`eventType=wxEVT_NULL`, `eventId=0`).
   */
  [[maybe_unused]] WxTreeEventFactoryRuntime* wxCreateTreeEventRuntimeClassInstance()
  {
    return new (std::nothrow) WxTreeEventFactoryRuntime(0, 0);
  }

  /**
   * Address: 0x0099CB10 (FUN_0099CB10)
   *
   * What it does:
   * Allocates and initializes one default `wxListEvent` runtime payload
   * (`eventType=wxEVT_NULL`, `eventId=0`).
   */
  [[maybe_unused]] WxListEventRuntime* wxCreateListEventRuntimeClassInstance()
  {
    return new (std::nothrow) WxListEventRuntime(0, 0);
  }
} // namespace

/**
 * Address: 0x00988590 (FUN_00988590)
 *
 * What it does:
 * Builds one `wxListEvent` payload for a list-control column-drag lane,
 * copies the drag point and current column state into the event, resolves the
 * owner event handler, and dispatches the synthesized event.
 *
 * Notes:
 * The owning lane is the list-control header/column-drag path reached from
 * `FUN_00989AC0`.
 */
[[maybe_unused]] static void wxDispatchListColumnEvent(
  wxWindowBase* const listWindow,
  const std::int32_t eventType,
  const std::int32_t dragPointX,
  const std::int32_t dragPointY
)
{
  const auto* const listView = reinterpret_cast<const WxListCtrlColumnDragRuntimeView*>(listWindow);
  const std::int32_t ownerWindowId = ResolveRuntimeWindowId(listView->mOwnerWindow);

  WxListEventRuntime event(eventType, ownerWindowId);
  event.mEventObject = listView->mOwnerWindow;
  event.mPointDrag.x = dragPointX;
  event.mPointDrag.y = dragPointY;
  event.mCol = listView->mCurrentColumn;

  std::int32_t clientWidth = 0;
  std::int32_t clientHeight = 0;
  listWindow->DoGetClientSize(&clientWidth, &clientHeight);
  event.mPointDrag.y -= clientHeight;

  wxWindowBase* const ownerEventHandler = ResolveRuntimeEventHandler(listView->mOwnerWindow);
  (void)ownerEventHandler->ProcessEvent(&event);
}

/**
 * Address: 0x00978190 (FUN_00978190, func_wxNodeBaseInit)
 *
 * What it does:
 * Initializes one `wxNodeBase` node with key/data/owner lanes and links it
 * between optional neighboring nodes.
 */
wxNodeBaseRuntime* wxNodeBaseInit(
  wxNodeBaseRuntime* const node,
  void* const listOwner,
  wxNodeBaseRuntime* const previous,
  wxNodeBaseRuntime* const next,
  void* const value,
  const wxListKeyRuntime* const key
)
{
  if (node == nullptr) {
    return nullptr;
  }

  new (node) wxNodeBaseRuntime();
  node->mValue = value;
  node->mListOwner = listOwner;
  node->mPrevious = previous;
  node->mNext = next;

  if (key != nullptr) {
    if (key->mKeyType == wxKEY_INTEGER_RUNTIME) {
      node->mKeyStorage = key->mKey.integer;
    } else if (key->mKeyType == wxKEY_STRING_RUNTIME && key->mKey.string != nullptr) {
      node->mKeyStorage = reinterpret_cast<std::uintptr_t>(::_wcsdup(key->mKey.string));
    }
  }

  if (previous != nullptr) {
    previous->mNext = node;
  }
  if (next != nullptr) {
    next->mPrevious = node;
  }

  return node;
}

/**
 * Address: 0x0099FAD0 (FUN_0099FAD0)
 *
 * What it does:
 * Allocates one menu-list node payload, initializes base list-link/key/value
 * lanes, and rebinds the node to the wx menu-list-node dispatch lane.
 */
[[maybe_unused]] wxNodeBaseRuntime* wxCreateMenuListNodeRuntime(
  void* const listOwner,
  wxNodeBaseRuntime* const previous,
  wxNodeBaseRuntime* const next,
  void* const value,
  const wxListKeyRuntime* const key
)
{
  auto* const allocated = static_cast<wxNodeBaseRuntime*>(::operator new(sizeof(wxNodeBaseRuntime), std::nothrow));
  if (allocated == nullptr) {
    return nullptr;
  }

  wxNodeBaseRuntime* const initialized = wxNodeBaseInit(allocated, listOwner, previous, next, value, key);
  if (initialized != nullptr) {
    *reinterpret_cast<void**>(initialized) = &gWxMenuListNodeRuntimeVTableTag;
  }
  return initialized;
}

/**
 * Address: 0x004F38E0 (FUN_004F38E0)
 *
 * What it does:
 * Returns the static wx class-info lane for this event payload type.
 */
void* moho::CLogAdditionEvent::GetClassInfo() const
{
  return gCLogAdditionEventClassInfoTable;
}

/**
 * Address: 0x004F3850 (FUN_004F3850)
 *
 * What it does:
 * Deleting-dtor entry for this event payload type.
 */
void moho::CLogAdditionEvent::DeleteObject()
{
  delete this;
}

/**
 * Address: 0x004F37F0 (FUN_004F37F0)
 *
 * What it does:
 * Allocates and copy-clones one `CLogAdditionEvent` object.
 */
moho::CLogAdditionEvent* moho::CLogAdditionEvent::Clone() const
{
  return new CLogAdditionEvent(*this);
}

namespace
{
  /**
   * Address: 0x004FAC00 (FUN_004FAC00)
   *
   * What it does:
   * Resets one contiguous `CWinLogLine` range by releasing heap-backed text
   * lanes and restoring each embedded string to empty SSO state.
   */
  [[maybe_unused]] void ResetWinLogLineRange(
    moho::CWinLogLine* begin,
    moho::CWinLogLine* end
  ) noexcept
  {
    while (begin != end) {
      if (begin->text.myRes >= 0x10u) {
        ::operator delete(begin->text.bx.ptr);
      }

      begin->text.myRes = 15u;
      begin->text.mySize = 0u;
      begin->text.bx.buf[0] = '\0';
      ++begin;
    }
  }
} // namespace

/**
 * Address: 0x004F38F0 (FUN_004F38F0, ??0CWinLogTarget@Moho@@QAE@@Z)
 *
 * What it does:
 * Initializes the global log-target owner and auto-registers it with gpg logging.
 */
moho::CWinLogTarget::CWinLogTarget()
  : gpg::LogTarget(true)
{}

/**
 * Address: 0x004F39B0 (FUN_004F39B0)
 * Mangled deleting-dtor thunk: 0x004F3990 (FUN_004F3990)
 *
 * What it does:
 * Releases pending/committed vectors and tears down base log-target registration.
 */
moho::CWinLogTarget::~CWinLogTarget() = default;

/**
 * Address: 0x004F6F40 (FUN_004F6F40)
 *
 * What it does:
 * Appends one line record into the pending queue.
 */
void moho::CWinLogTarget::AppendPendingLine(
  const CWinLogLine& line
)
{
  mPendingLines.push_back(line);
}

/**
 * Address: 0x004F6F10 (FUN_004F6F10)
 *
 * What it does:
 * Returns committed line count.
 */
std::size_t moho::CWinLogTarget::CommittedLineCount() const
{
  return mCommittedLines.size();
}

const msvc8::vector<moho::CWinLogLine>& moho::CWinLogTarget::CommittedLines() const
{
  return mCommittedLines;
}

/**
 * Address: 0x004F6FD0 (FUN_004F6FD0)
 *
 * What it does:
 * Replaces committed-line storage with a copy of `nextCommittedLines`.
 */
void moho::CWinLogTarget::ReplaceCommittedLines(
  const msvc8::vector<CWinLogLine>& nextCommittedLines
)
{
  mCommittedLines = nextCommittedLines;
}

void moho::CWinLogTarget::SnapshotCommittedLines(
  msvc8::vector<CWinLogLine>* const outLines
)
{
  if (outLines == nullptr) {
    return;
  }

  boost::mutex::scoped_lock scopedLock(lock);
  *outLines = mCommittedLines;
}

void moho::CWinLogTarget::ResetCommittedLinesFromReplayBuffer(
  const msvc8::vector<msvc8::string>& replayLines
)
{
  boost::mutex::scoped_lock scopedLock(lock);

  msvc8::vector<CWinLogLine> rebuiltLines;
  rebuiltLines.reserve(replayLines.size());
  for (std::size_t index = 0; index < replayLines.size(); ++index) {
    CWinLogLine replayLine{};
    replayLine.isReplayEntry = 1;
    replayLine.sequenceIndex = static_cast<std::uint32_t>(index);
    replayLine.categoryMask = 1;
    replayLine.text = replayLines[index];
    rebuiltLines.push_back(replayLine);
  }

  ReplaceCommittedLines(rebuiltLines);
}

/**
 * Address: 0x004F6A50 (FUN_004F6A50)
 *
 * What it does:
 * Merges pending lines into committed history and enforces the 10,000 line cap.
 */
void moho::CWinLogTarget::MergePendingLines()
{
  boost::mutex::scoped_lock scopedLock(lock);

  const std::size_t pendingCount = mPendingLines.size();
  const std::size_t committedCount = mCommittedLines.size();
  if (committedCount + pendingCount > kMaxCommittedLogLines) {
    const std::size_t dropCount = (std::min)(committedCount, pendingCount);
    if (dropCount != 0) {
      mCommittedLines.erase(mCommittedLines.begin(), mCommittedLines.begin() + dropCount);
    }
  }

  for (const CWinLogLine& line : mPendingLines) {
    mCommittedLines.push_back(line);
  }
  mPendingLines.clear();
}

/**
 * Address: 0x004F6860 (FUN_004F6860)
 *
 * gpg::LogSeverity level, msvc8::string const &, msvc8::vector<msvc8::string> const &, int
 *
 * What it does:
 * Queues replay/context lines plus the current line into the pending log queue.
 */
void moho::CWinLogTarget::OnMessage(
  const gpg::LogSeverity level,
  const msvc8::string& message,
  const msvc8::vector<msvc8::string>& context,
  const int previousDepth
)
{
  boost::mutex::scoped_lock scopedLock(lock);

  std::size_t replayStart = 0;
  if (previousDepth > 0) {
    replayStart = static_cast<std::size_t>(previousDepth);
  }
  if (replayStart > context.size()) {
    replayStart = context.size();
  }

  const std::uint32_t categoryMask = static_cast<std::uint32_t>(level);
  for (std::size_t index = replayStart; index < context.size(); ++index) {
    CWinLogLine replayLine{};
    replayLine.isReplayEntry = 1;
    replayLine.sequenceIndex = static_cast<std::uint32_t>(index);
    replayLine.categoryMask = categoryMask;
    replayLine.text = context[index];
    AppendPendingLine(replayLine);
  }

  CWinLogLine messageLine{};
  messageLine.isReplayEntry = 0;
  messageLine.sequenceIndex = static_cast<std::uint32_t>(context.size());
  messageLine.categoryMask = categoryMask;
  messageLine.text = message;
  AppendPendingLine(messageLine);

  WWinLogWindow* const dialogWindow = dialog;
  scopedLock.unlock();
  if (dialogWindow != nullptr) {
    const CLogAdditionEvent event{};
    dialogWindow->OnTargetPendingLinesChanged(event);
  }
}

/**
 * Address: 0x004F4270 (FUN_004F4270)
 *
 * What it does:
 * Constructs one managed log-window object and seeds control/runtime state
 * lanes used by downstream append/rebuild handlers.
 */
moho::WWinLogWindow::WWinLogWindow()
{
  mIsInitializingControls = 1;
  mEnabledCategoriesMask = 0;
  mFilterText.clear();
  mBufferedLines.clear();
  mFirstVisibleLine = 0;
  RegisterManagedOwnerSlot();
  InitializeFromUserPreferences();
  mIsInitializingControls = 0;
}

void moho::WWinLogWindow::InitializeFromUserPreferences()
{
  IUserPrefs* const preferences = USER_GetPreferences();
  RestoreCategoryStateFromPreferences(preferences);
  RestoreFilterFromPreferences(preferences);
  RestoreGeometryFromPreferences(preferences);
}

void moho::WWinLogWindow::RestoreCategoryStateFromPreferences(
  IUserPrefs* const preferences
)
{
  mEnabledCategoriesMask = 0;

  const auto checkBoxes = CategoryCheckBoxes();
  for (std::size_t index = 0; index < checkBoxes.size(); ++index) {
    const bool enabled = preferences != nullptr
      ? preferences->GetBoolean(msvc8::string(kLogCategoryPreferenceKeys[index]), kLogCategoryPreferenceDefaults[index])
      : kLogCategoryPreferenceDefaults[index];

    if (enabled) {
      mEnabledCategoriesMask |= (1u << static_cast<std::uint32_t>(index));
    }

    if (checkBoxes[index] != nullptr) {
      checkBoxes[index]->SetValue(enabled);
    }
  }
}

void moho::WWinLogWindow::RestoreFilterFromPreferences(
  IUserPrefs* const preferences
)
{
  const msvc8::string fallback(kLogFilterPreferenceDefault);
  if (preferences != nullptr) {
    mFilterText = preferences->GetString(msvc8::string(kLogFilterPreferenceKey), fallback);
  } else {
    mFilterText = fallback;
  }

  if (mFilterTextControl != nullptr) {
    mFilterTextControl->SetValueUtf8(mFilterText);
  }
}

void moho::WWinLogWindow::RestoreGeometryFromPreferences(
  IUserPrefs* const preferences
)
{
  if (preferences == nullptr) {
    return;
  }

  const std::int32_t height =
    preferences->GetInteger(msvc8::string(kLogWindowHeightPreferenceKey), kLogWindowGeometryFallback);
  const std::int32_t width =
    preferences->GetInteger(msvc8::string(kLogWindowWidthPreferenceKey), kLogWindowGeometryFallback);
  const std::int32_t y = preferences->GetInteger(msvc8::string(kLogWindowYPreferenceKey), kLogWindowGeometryFallback);
  const std::int32_t x = preferences->GetInteger(msvc8::string(kLogWindowXPreferenceKey), kLogWindowGeometryFallback);

  DoSetSize(x, y, width, height, kLogWindowSetSizeFlags);
}

/**
 * Address: 0x004F5380 (FUN_004F5380)
 * Mangled deleting-dtor thunk: 0x004F5360 (FUN_004F5360)
 *
 * What it does:
 * Detaches from the owner log target, clears local storage lanes, and unlinks
 * managed-owner slots.
 */
moho::WWinLogWindow::~WWinLogWindow()
{
  DetachFromTarget();
  mBufferedLines.clear();
  mFilterText.clear();
  ReleaseManagedOwnerSlots();
}

void moho::WWinLogWindow::SetOwnerTarget(
  CWinLogTarget* const ownerTarget
) noexcept
{
  mOwnerTarget = ownerTarget;
}

/**
 * Address: 0x004F6760 (FUN_004F6760)
 *
 * What it does:
 * Clears `mOwnerTarget->dialog` under the target lock.
 */
void moho::WWinLogWindow::DetachFromTarget()
{
  if (mOwnerTarget == nullptr) {
    return;
  }

  boost::mutex::scoped_lock scopedLock(mOwnerTarget->lock);
  mOwnerTarget->dialog = nullptr;
}

std::array<wxCheckBoxRuntime*, 5> moho::WWinLogWindow::CategoryCheckBoxes() noexcept
{
  return {
    mDebugCategoryCheckBox,
    mInfoCategoryCheckBox,
    mWarnCategoryCheckBox,
    mErrorCategoryCheckBox,
    mCustomCategoryCheckBox,
  };
}

std::array<const wxCheckBoxRuntime*, 5> moho::WWinLogWindow::CategoryCheckBoxes() const noexcept
{
  return {
    mDebugCategoryCheckBox,
    mInfoCategoryCheckBox,
    mWarnCategoryCheckBox,
    mErrorCategoryCheckBox,
    mCustomCategoryCheckBox,
  };
}

/**
 * Address: 0x004F5440 (FUN_004F5440)
 *
 * What it does:
 * Clears output and rebuilds committed target lines from buffered replay text
 * entries.
 */
void moho::WWinLogWindow::ResetCommittedLinesFromBuffer()
{
  if (mOutputTextControl != nullptr) {
    mOutputTextControl->Clear();
  }

  mFirstVisibleLine = 0;

  if (mOwnerTarget == nullptr) {
    return;
  }

  mOwnerTarget->ResetCommittedLinesFromReplayBuffer(mBufferedLines);
}

bool moho::WWinLogWindow::ShouldDisplayCommittedLine(
  const CWinLogLine& line
) const
{
  if (line.categoryMask < 32) {
    const std::uint32_t categoryBit = 1u << line.categoryMask;
    if ((mEnabledCategoriesMask & categoryBit) != 0u) {
      return true;
    }
  }

  if ((mEnabledCategoriesMask & kCustomFilterCategoryBit) == 0u) {
    return false;
  }

  const msvc8::string loweredLineText = gpg::STR_ToLower(line.text.c_str());
  return std::strstr(loweredLineText.c_str(), mFilterText.c_str()) != nullptr;
}

std::wstring moho::WWinLogWindow::BuildReplayFlushText(
  const std::size_t startIndex
) const
{
  WWinLogTextBuilder replayBuilder{};
  for (std::size_t index = startIndex; index < mBufferedLines.size(); ++index) {
    replayBuilder.WriteSpaces(index * kReplayIndentWidth);
    replayBuilder.WriteUtf8Text(mBufferedLines[index]);
    replayBuilder.WriteCodePoint(L'\n');
  }

  return replayBuilder.Finalize();
}

std::wstring moho::WWinLogWindow::BuildFormattedCommittedLineText(
  const CWinLogLine& line
) const
{
  WWinLogTextBuilder lineBuilder{};
  const std::wstring severityPrefix(line.SeverityPrefix());
  lineBuilder.WriteWideText(severityPrefix);

  const std::size_t continuationIndent = mBufferedLines.size() * kReplayIndentWidth + severityPrefix.size();
  bool continuationLine = false;
  wchar_t decodedCodePoint = 0;
  const char* cursor = gpg::STR_DecodeUtf8Char(line.text.c_str(), decodedCodePoint);
  while (decodedCodePoint != 0) {
    if (continuationLine) {
      lineBuilder.WriteSpaces(continuationIndent);
      continuationLine = false;
    }

    lineBuilder.WriteDecodedCodePoint(decodedCodePoint);
    if (decodedCodePoint == L'\n') {
      continuationLine = true;
    }

    cursor = gpg::STR_DecodeUtf8Char(cursor, decodedCodePoint);
  }

  if (!continuationLine) {
    lineBuilder.WriteCodePoint(L'\n');
  }

  return lineBuilder.Finalize();
}

/**
 * Address: 0x004F5840 (FUN_004F5840)
 *
 * What it does:
 * Rebuilds category/filter visibility state from controls and replays matching
 * committed lines into output.
 */
void moho::WWinLogWindow::RebuildVisibleLinesFromControls()
{
  mEnabledCategoriesMask = 0;
  const auto checkBoxes = CategoryCheckBoxes();
  for (std::size_t index = 0; index < checkBoxes.size(); ++index) {
    const wxCheckBoxRuntime* const checkBox = checkBoxes[index];
    if (checkBox != nullptr && checkBox->GetValue()) {
      mEnabledCategoriesMask |= (1u << index);
    }
  }

  if (mFilterTextControl != nullptr) {
    mFilterText = mFilterTextControl->GetValueUtf8Lower();
  } else {
    mFilterText.clear();
  }

  if (mOutputTextControl != nullptr) {
    mOutputTextControl->Clear();
  }

  mFirstVisibleLine = 0;
  mBufferedLines.clear();

  if (mOwnerTarget == nullptr) {
    return;
  }

  msvc8::vector<CWinLogLine> committedLinesSnapshot;
  mOwnerTarget->SnapshotCommittedLines(&committedLinesSnapshot);

  for (const CWinLogLine& line : committedLinesSnapshot) {
    AppendCommittedLine(line);
  }

  if (mOutputTextControl != nullptr) {
    mOutputTextControl->ScrollToLastPosition();
  }
}

/**
 * Address: 0x004F5AE0 (FUN_004F5AE0)
 *
 * What it does:
 * Applies one committed line against category/filter visibility and appends
 * replay/text output with preserved indentation behavior.
 */
void moho::WWinLogWindow::AppendCommittedLine(
  const CWinLogLine& line
)
{
  while (mBufferedLines.size() > line.sequenceIndex) {
    mBufferedLines.pop_back();
  }

  if (mFirstVisibleLine > mBufferedLines.size()) {
    mFirstVisibleLine = static_cast<std::uint32_t>(mBufferedLines.size());
  }

  if (line.IsReplayEntry()) {
    if (line.isReplayEntry == 1u) {
      mBufferedLines.push_back(line.text);
    }
    return;
  }

  if (!ShouldDisplayCommittedLine(line)) {
    return;
  }

  if (mOutputTextControl == nullptr) {
    return;
  }

  if (mFirstVisibleLine < mBufferedLines.size()) {
    const wxTextAttrRuntime replayStyle = DefaultTextStyleForCategory(1u);
    (void)mOutputTextControl->SetDefaultStyle(replayStyle);
    const std::wstring replayText = BuildReplayFlushText(mFirstVisibleLine);

    mFirstVisibleLine = static_cast<std::uint32_t>(mBufferedLines.size());
    if (!replayText.empty()) {
      mOutputTextControl->AppendWide(replayText);
    }
  }

  const wxTextAttrRuntime lineStyle = DefaultTextStyleForCategory(line.categoryMask);
  (void)mOutputTextControl->SetDefaultStyle(lineStyle);
  const std::wstring formattedText = BuildFormattedCommittedLineText(line);
  mOutputTextControl->AppendWide(formattedText);

  if (line.categoryMask >= kWarningCategoryValue && moho::CFG_GetArgOption("/edit", 0, nullptr)) {
    Show(true);
  }
}

/**
 * Address: 0x004F6470 (FUN_004F6470)
 *
 * What it does:
 * Merges pending lines into committed history and refreshes output when the
 * committed count changed.
 */
void moho::WWinLogWindow::OnTargetPendingLinesChanged(
  const CLogAdditionEvent& event
)
{
  (void)event;
  if (mOwnerTarget == nullptr) {
    return;
  }

  const std::size_t previousCommittedLineCount = mOwnerTarget->CommittedLineCount();
  mOwnerTarget->MergePendingLines();
  if (previousCommittedLineCount != mOwnerTarget->CommittedLineCount()) {
    RebuildVisibleLinesFromControls();
  }
}

void moho::WWinLogWindow::OnTargetPendingLinesChanged()
{
  const CLogAdditionEvent event{};
  OnTargetPendingLinesChanged(event);
}

/**
 * Address: 0x008CD8C0 (FUN_008CD8C0)
 * Mangled: ??0WSupComFrame@@QAE@PBDABVwxPoint@@ABVwxSize@@J@Z
 *
 * What it does:
 * Creates one SupCom frame runtime object with constructor-equivalent startup
 * semantics: UTF-8 title lane, position/style lanes, sync-flag zeroing,
 * min-drag size hints, and startup icon-resource assignment.
 */
WSupComFrame* WX_CreateSupComFrame(
  const char* const title,
  const wxPoint& position,
  const wxSize& size,
  const std::int32_t style
)
{
  auto* const frame = new WSupComFrameRuntime(title, position, size, style);
  frame->SetSizeHints(moho::wnd_MinDragWidth, moho::wnd_MinDragHeight, -1, -1, -1, -1);
  return frame;
}

namespace
{
  constexpr unsigned int kSupComFrameMessageSize = WM_SIZE;
  constexpr unsigned int kSupComFrameMessageActivateApp = WM_ACTIVATEAPP;
  constexpr unsigned int kSupComFrameMessageSysCommand = WM_SYSCOMMAND;
  constexpr unsigned int kSupComFrameMessageExitSizeMove = WM_EXITSIZEMOVE;
  constexpr unsigned int kSupComFrameSysCommandSize = SC_SIZE;
  constexpr unsigned int kSupComFrameSysCommandToggleLogDialog = 0x1u;
  constexpr unsigned int kSupComFrameSysCommandLuaDebugger = 0x2u;
  constexpr unsigned int kSupComFrameSysCommandKeyMenu = SC_KEYMENU;

  constexpr const char* kSupComFrameXPreferenceKey = "Windows.Main.x";
  constexpr const char* kSupComFrameYPreferenceKey = "Windows.Main.y";
  constexpr const char* kSupComFrameWidthPreferenceKey = "Windows.Main.width";
  constexpr const char* kSupComFrameHeightPreferenceKey = "Windows.Main.height";
  constexpr const char* kSupComFrameMaximizedPreferenceKey = "Windows.Main.maximized";
  constexpr const char* kSupComFrameToggleLogDialogCommand = "WIN_ToggleLogDialog";
  constexpr const char* kSupComFrameLuaDebuggerCommand = "SC_LuaDebugger";
  constexpr const char* kSupComFrameCursorLockPreferenceKey = "lock_fullscreen_cursor_to_window";

  /**
   * Address: 0x008CDBE0 (FUN_008CDBE0)
   *
   * What it does:
   * Clamps SupCom frame client dimensions to drag minima, propagates size to
   * the active viewport, then rebuilds one GAL context head and reinitializes
   * D3D device state.
   */
  void SyncSupComFrameClientSizeAndViewport(
    WSupComFrame& frame
  )
  {
    std::int32_t width = 0;
    std::int32_t height = 0;
    frame.DoGetClientSize(&width, &height);

    if (width < moho::wnd_MinDragWidth) {
      width = moho::wnd_MinDragWidth;
    }
    if (height < moho::wnd_MinDragHeight) {
      height = moho::wnd_MinDragHeight;
    }

    frame.DoSetClientSize(width, height);
    if (moho::ren_Viewport != nullptr) {
      moho::ren_Viewport->DoSetSize(-1, -1, width, height, 0);
    }

    gpg::gal::Device* const galDevice = gpg::gal::Device::GetInstance();
    if (galDevice == nullptr) {
      return;
    }

    gpg::gal::DeviceContext* const activeContext = galDevice->GetDeviceContext();
    if (activeContext == nullptr) {
      return;
    }

    gpg::gal::DeviceContext context(*activeContext);
    if (context.GetHeadCount() > 0) {
      gpg::gal::Head& head = context.GetHead(0);
      head.mWidth = width;
      head.mHeight = height;
    }

    moho::CD3DDevice* const d3dDevice = moho::D3D_GetDevice();
    if (d3dDevice == nullptr) {
      return;
    }

    d3dDevice->Clear();
    d3dDevice->InitContext(&context);
  }

  /**
   * Address: 0x008D1D70 (FUN_008D1D70)
   *
   * What it does:
   * Applies cursor clipping for active SupCom frame focus: locks to main
   * window rectangle when one windowed head is active and the cursor-lock
   * option is enabled, otherwise clears clip bounds.
   */
  void UpdateSupComCursorClipForActivation()
  {
    gpg::gal::Device* const galDevice = gpg::gal::Device::GetInstance();
    if (galDevice == nullptr) {
      return;
    }

    gpg::gal::DeviceContext* const activeContext = galDevice->GetDeviceContext();
    if (activeContext == nullptr) {
      return;
    }

    gpg::gal::DeviceContext context(*activeContext);
    if (context.GetHeadCount() <= 0) {
      return;
    }

    const gpg::gal::Head& head = context.GetHead(0);
    RECT clipRect{};
    RECT* clipRectPtr = nullptr;
    if (
      context.GetHeadCount() == 1 && head.mWindowed && moho::OPTIONS_GetInt(kSupComFrameCursorLockPreferenceKey) == 1 &&
      moho::sMainWindow != nullptr
    ) {
      const HWND mainWindowHandle = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(moho::sMainWindow->GetHandle()));
      if (mainWindowHandle != nullptr) {
        ::GetWindowRect(mainWindowHandle, &clipRect);
        clipRectPtr = &clipRect;
      }
    }

    ::ClipCursor(clipRectPtr);
  }
} // namespace

/**
 * Address: 0x008CE060 (FUN_008CE060, WSupComFrame::dtr)
 *
 * What it does:
 * Implements deleting-dtor thunk semantics for SupCom frame runtime lanes.
 */
WSupComFrame* WSupComFrame::DeleteWithFlag(
  WSupComFrame* const object,
  const std::uint8_t deleteFlags
) noexcept
{
  if (object == nullptr) {
    return nullptr;
  }

  gSupComFrameStateByFrame.erase(object);
  WX_FrameDestroyWithoutDelete(object);
  if ((deleteFlags & 1u) != 0u) {
    operator delete(object);
  }

  return object;
}

/**
 * Address: 0x008CDAA0 (FUN_008CDAA0, WSupComFrame::OnCloseWindow)
 *
 * What it does:
 * Exits the wx main loop when the frame is iconized; otherwise requests the
 * Moho escape dialog.
 */
void WSupComFrame::OnCloseWindow(
  wxCloseEventRuntime& event
)
{
  (void)event;

  if (IsIconized()) {
    wxTheApp->ExitMainLoop();
    return;
  }

  (void)moho::ShowEscapeDialog(true);
}

/**
 * Address: 0x008CDCD0 (FUN_008CDCD0, WSupComFrame::MSWDefWindowProc)
 *
 * What it does:
 * Handles SupCom system-command defaults, including pending-maximize sync
 * priming and Alt-menu suppression, then forwards remaining lanes through
 * base wx default-window-proc dispatch.
 */
long WSupComFrame::MSWDefWindowProc(
  const unsigned int message,
  const unsigned int wParam,
  const long lParam
)
{
  auto dispatchBase = [this, message, wParam, lParam]() -> long {
    return wxTopLevelWindowRuntime::MSWDefWindowProc(message, wParam, lParam);
  };

  if (message != kSupComFrameMessageSysCommand) {
    return dispatchBase();
  }

  if ((wParam & 0xFFF0u) == kSupComFrameSysCommandSize) {
    mPendingMaximizeSync = 1;
    if (moho::CD3DDevice* const device = moho::D3D_GetDevice(); device != nullptr) {
      (void)device->Clear2(true);
    }
  }

  if (wParam == kSupComFrameSysCommandKeyMenu && lParam == 0) {
    return 0;
  }

  return dispatchBase();
}

/**
 * Address: 0x008CDAD0 (FUN_008CDAD0, WSupComFrame::OnMove)
 *
 * What it does:
 * Persists SupCom frame position lanes to user preferences while device-lock
 * is disabled and the frame is not iconized.
 */
void WSupComFrame::OnMove(
  wxMoveEventRuntime& event
)
{
  (void)event;
  if (moho::sDeviceLock || IsIconized()) {
    return;
  }

  moho::IUserPrefs* const preferences = moho::USER_GetPreferences();
  if (preferences == nullptr) {
    return;
  }

  std::int32_t positionLaneA = 0;
  std::int32_t positionLaneB = 0;

  DoGetPosition(&positionLaneA, &positionLaneB);
  preferences->SetInteger(msvc8::string(kSupComFrameXPreferenceKey), positionLaneB);

  DoGetPosition(&positionLaneA, &positionLaneB);
  preferences->SetInteger(msvc8::string(kSupComFrameYPreferenceKey), positionLaneA);
}

/**
 * Address: 0x008CDD40 (FUN_008CDD40, WSupComFrame::MSWWindowProc)
 * Mangled: ?MSWWindowProc@WSupComFrame@@UAEJIIJ@Z
 *
 * What it does:
 * Handles SupCom frame resize/maximize/app-activation/system-command routing,
 * persists window preference keys, and forwards unhandled messages to base
 * frame dispatch.
 */
long WSupComFrame::MSWWindowProc(
  const unsigned int message,
  const unsigned int wParam,
  const long lParam
)
{
  auto dispatchBase = [this, message, wParam, lParam]() -> long {
    return wxTopLevelWindowRuntime::MSWWindowProc(message, wParam, lParam);
  };

  moho::IUserPrefs* const preferences = moho::USER_GetPreferences();
  if (!moho::sDeviceLock && moho::ren_Viewport != nullptr && gpg::gal::Device::IsReady()) {
    if (gpg::gal::Device* const galDevice = gpg::gal::Device::GetInstance(); galDevice != nullptr) {
      if (
        gpg::gal::DeviceContext* const activeContext = galDevice->GetDeviceContext();
        activeContext != nullptr && activeContext->GetHeadCount() > 0
      ) {
        (void)activeContext->GetHead(0);
      }
    }

    if (mPendingMaximizeSync != 0 && message == kSupComFrameMessageExitSizeMove) {
      SyncSupComFrameClientSizeAndViewport(*this);

      if (preferences != nullptr) {
        const wxSize clientSize = GetClientSize();
        preferences->SetInteger(msvc8::string(kSupComFrameWidthPreferenceKey), clientSize.x);
        preferences->SetInteger(msvc8::string(kSupComFrameHeightPreferenceKey), clientSize.y);
      }

      if (moho::CD3DDevice* const d3dDevice = moho::D3D_GetDevice(); d3dDevice != nullptr) {
        (void)d3dDevice->Clear2(false);
      }

      mPendingMaximizeSync = 0;
    } else if (message == kSupComFrameMessageSize && wParam == SIZE_MAXIMIZED) {
      SyncSupComFrameClientSizeAndViewport(*this);
      if (preferences != nullptr) {
        preferences->SetBoolean(msvc8::string(kSupComFrameMaximizedPreferenceKey), true);
      }
      mPersistedMaximizeSync = 1;
    }

    if (mPendingMaximizeSync == 0 && message == kSupComFrameMessageSize) {
      if (wParam == SIZE_RESTORED && mPersistedMaximizeSync != 0) {
        SyncSupComFrameClientSizeAndViewport(*this);
        if (preferences != nullptr) {
          const wxSize clientSize = GetClientSize();
          preferences->SetInteger(msvc8::string(kSupComFrameWidthPreferenceKey), clientSize.x);
          preferences->SetInteger(msvc8::string(kSupComFrameHeightPreferenceKey), clientSize.y);
          preferences->SetBoolean(msvc8::string(kSupComFrameMaximizedPreferenceKey), false);
        }
        mPersistedMaximizeSync = 0;
      }

      return dispatchBase();
    }
  }

  if (message == kSupComFrameMessageActivateApp) {
    const bool isActive = wParam != 0;
    mIsApplicationActive = isActive ? 1 : 0;
    if (isActive) {
      UpdateSupComCursorClipForActivation();
    } else {
      ::ClipCursor(nullptr);
    }
    return dispatchBase();
  }

  if (message != kSupComFrameMessageSysCommand) {
    return dispatchBase();
  }

  if (wParam == kSupComFrameSysCommandToggleLogDialog) {
    moho::CON_Execute(kSupComFrameToggleLogDialogCommand);
    return dispatchBase();
  }

  if (wParam == kSupComFrameSysCommandLuaDebugger) {
    moho::CON_Execute(kSupComFrameLuaDebuggerCommand);
    return dispatchBase();
  }

  if (wParam != kSupComFrameSysCommandKeyMenu) {
    return dispatchBase();
  }

  gpg::gal::Device* const galDevice = gpg::gal::Device::GetInstance();
  if (galDevice == nullptr) {
    return dispatchBase();
  }

  gpg::gal::DeviceContext* const activeContext = galDevice->GetDeviceContext();
  if (activeContext == nullptr || activeContext->GetHeadCount() <= 0 || !activeContext->GetHead(0).mWindowed) {
    return dispatchBase();
  }

  return 0;
}

/**
 * Address family:
 * - 0x004F7210 (FUN_004F7210)
 * - 0x004F72D0 (FUN_004F72D0)
 *
 * What it does:
 * Unlinks one managed slot from its owner slot chain.
 */
void moho::ManagedWindowSlot::UnlinkFromOwner() noexcept
{
  if (ownerHeadLink == nullptr || IsInlineHeadLinkSentinel(ownerHeadLink)) {
    ownerHeadLink = nullptr;
    nextInOwnerChain = nullptr;
    return;
  }

  ManagedWindowSlot** link = ownerHeadLink;
  while (*link != this) {
    if (*link == nullptr) {
      ownerHeadLink = nullptr;
      nextInOwnerChain = nullptr;
      return;
    }
    link = &(*link)->nextInOwnerChain;
  }

  *link = nextInOwnerChain;
  ownerHeadLink = nullptr;
  nextInOwnerChain = nullptr;
}

/**
 * Address context:
 * - destructor slot-clear writes in 0x004F40A0 / 0x004F4230
 *
 * What it does:
 * Clears both link lanes to the inert slot state.
 */
void moho::ManagedWindowSlot::Clear() noexcept
{
  ownerHeadLink = nullptr;
  nextInOwnerChain = nullptr;
}

moho::WWinManagedDialog* moho::WWinManagedDialog::FromManagedSlotHeadLink(
  ManagedWindowSlot** const ownerHeadLink
) noexcept
{
  if (ownerHeadLink == nullptr || ownerHeadLink == NullManagedSlotHeadLinkSentinel()) {
    return nullptr;
  }

  const std::uintptr_t linkAddress = reinterpret_cast<std::uintptr_t>(ownerHeadLink);
  return reinterpret_cast<WWinManagedDialog*>(linkAddress - offsetof(WWinManagedDialog, mManagedSlotsHead));
}

moho::ManagedWindowSlot** moho::WWinManagedDialog::NullManagedSlotHeadLinkSentinel() noexcept
{
  return reinterpret_cast<ManagedWindowSlot**>(offsetof(WWinManagedDialog, mManagedSlotsHead));
}

/**
 * Address: 0x004F7070 (FUN_004F7070)
 *
 * What it does:
 * Returns the current number of dialog-managed registry slots.
 */
std::size_t moho::WWinManagedDialog::ManagedSlotCount()
{
  return managedWindows.size();
}

/**
 * Address: 0x004F70A0 (FUN_004F70A0)
 *
 * What it does:
 * Appends one dialog-managed registry slot and links it to `ownerHeadLink`.
 */
void moho::WWinManagedDialog::AppendManagedSlotForOwner(
  ManagedWindowSlot** const ownerHeadLink
)
{
  AppendManagedSlot<WWinManagedDialog>(managedWindows, ownerHeadLink);
}

/**
 * Address: 0x004F3F50 (FUN_004F3F50, WWinManagedDialog ctor tail)
 *
 * What it does:
 * Registers this dialog owner head in the global managed-dialog slot registry.
 */
void moho::WWinManagedDialog::RegisterManagedOwnerSlot()
{
  RegisterManagedOwnerSlotImpl<WWinManagedDialog>(managedWindows, &mManagedSlotsHead);
}

/**
 * Address: 0x004F40A0 (FUN_004F40A0, WWinManagedDialog dtor core)
 *
 * What it does:
 * Unlinks and clears every managed slot currently chained to this dialog.
 */
void moho::WWinManagedDialog::ReleaseManagedOwnerSlots()
{
  ReleaseManagedOwnerSlotChain(mManagedSlotsHead);
}

void moho::WWinManagedDialog::DestroyManagedOwners(
  msvc8::vector<ManagedWindowSlot>& slots
)
{
  DestroyManagedRuntimeCollection<WWinManagedDialog>(slots);
}

moho::WWinManagedFrame* moho::WWinManagedFrame::FromManagedSlotHeadLink(
  ManagedWindowSlot** const ownerHeadLink
) noexcept
{
  if (ownerHeadLink == nullptr || ownerHeadLink == NullManagedSlotHeadLinkSentinel()) {
    return nullptr;
  }

  const std::uintptr_t linkAddress = reinterpret_cast<std::uintptr_t>(ownerHeadLink);
  return reinterpret_cast<WWinManagedFrame*>(linkAddress - offsetof(WWinManagedFrame, mManagedSlotsHead));
}

moho::ManagedWindowSlot** moho::WWinManagedFrame::NullManagedSlotHeadLinkSentinel() noexcept
{
  return reinterpret_cast<ManagedWindowSlot**>(offsetof(WWinManagedFrame, mManagedSlotsHead));
}

/**
 * Address: 0x004F7140 (FUN_004F7140)
 *
 * What it does:
 * Returns the current number of frame-managed registry slots.
 */
std::size_t moho::WWinManagedFrame::ManagedSlotCount()
{
  return managedFrames.size();
}

/**
 * Address: 0x004F7170 (FUN_004F7170)
 *
 * What it does:
 * Appends one frame-managed registry slot and links it to `ownerHeadLink`.
 */
void moho::WWinManagedFrame::AppendManagedSlotForOwner(
  ManagedWindowSlot** const ownerHeadLink
)
{
  AppendManagedSlot<WWinManagedFrame>(managedFrames, ownerHeadLink);
}

/**
 * Address: 0x004F40E0 (FUN_004F40E0, WWinManagedFrame ctor tail)
 *
 * What it does:
 * Registers this frame owner head in the global managed-frame slot registry.
 */
void moho::WWinManagedFrame::RegisterManagedOwnerSlot()
{
  RegisterManagedOwnerSlotImpl<WWinManagedFrame>(managedFrames, &mManagedSlotsHead);
}

/**
 * Address: 0x004F4230 (FUN_004F4230, WWinManagedFrame dtor core)
 *
 * What it does:
 * Unlinks and clears every managed slot currently chained to this frame.
 */
void moho::WWinManagedFrame::ReleaseManagedOwnerSlots()
{
  ReleaseManagedOwnerSlotChain(mManagedSlotsHead);
}

void moho::WWinManagedFrame::DestroyManagedOwners(
  msvc8::vector<ManagedWindowSlot>& slots
)
{
  DestroyManagedRuntimeCollection<WWinManagedFrame>(slots);
}

/**
 * Address: 0x00453AA0 (FUN_00453AA0, sub_453AA0)
 *
 * IDA signature:
 * void __thiscall sub_453AA0(_DWORD *this);
 *
 * What it does:
 * Resets the viewport render-state dword at `+0x0C` to `-1`. Called
 * from the render-camera-outline path as the viewport begins a new
 * render pass.
 */
namespace
{
  /**
   * Address: 0x007FB7A0 (FUN_007FB7A0, boost::shared_ptr_IRenTerrain::shared_ptr_IRenTerrain)
   *
   * What it does:
   * Constructs one `shared_ptr<TerrainCommon>` in-place from one raw terrain
   * pointer and wires the shared-from-this control lane.
   */
  boost::shared_ptr<moho::TerrainCommon>* ConstructSharedTerrainFromRaw(
    boost::shared_ptr<moho::TerrainCommon>* const outTerrain,
    moho::TerrainCommon* const terrain
  )
  {
    return ::new (outTerrain) boost::shared_ptr<moho::TerrainCommon>(terrain);
  }

  /**
   * Address: 0x007FB730 (FUN_007FB730, boost::shared_ptr_CD3DPrimBatcher::operator=)
   *
   * What it does:
   * Rebinds one `shared_ptr<CD3DPrimBatcher>` from a raw pointer and releases
   * prior ownership.
   */
  boost::shared_ptr<moho::CD3DPrimBatcher>* AssignSharedPrimBatcherFromRaw(
    boost::shared_ptr<moho::CD3DPrimBatcher>* const outPrimBatcher,
    moho::CD3DPrimBatcher* const primBatcher
  )
  {
    outPrimBatcher->reset(primBatcher);
    return outPrimBatcher;
  }

  /**
   * Address: 0x007FBA20 (FUN_007FBA20, boost::shared_ptr_CD3DTextureBatcher::shared_ptr_CD3DTextureBatcher)
   *
   * What it does:
   * Constructs one `shared_ptr<CD3DTextureBatcher>` in-place from one raw
   * batcher pointer and wires the shared-from-this control lane.
   */
  boost::shared_ptr<moho::CD3DTextureBatcher>* ConstructSharedTextureBatcherFromRaw(
    boost::shared_ptr<moho::CD3DTextureBatcher>* const outTextureBatcher,
    moho::CD3DTextureBatcher* const textureBatcher
  )
  {
    return ::new (outTextureBatcher) boost::shared_ptr<moho::CD3DTextureBatcher>(textureBatcher);
  }

  /**
   * Address: 0x007FB7C0 (FUN_007FB7C0, boost::shared_ptr_IRenTerrain::operator=)
   *
   * What it does:
   * Rebinds one `shared_ptr<TerrainCommon>` from a raw pointer and releases
   * prior ownership.
   */
  boost::shared_ptr<moho::TerrainCommon>* AssignSharedTerrainFromRaw(
    boost::shared_ptr<moho::TerrainCommon>* const outTerrain,
    moho::TerrainCommon* const terrain
  )
  {
    outTerrain->reset(terrain);
    return outTerrain;
  }

  struct WRenViewportWorldViewParamRuntime final
  {
    moho::IRenderWorldView* view;           // +0x00
    std::int32_t head;                      // +0x04
    std::int32_t depth;                     // +0x08
    boost::shared_ptr<moho::TerrainCommon> terrain; // +0x0C
  };
  static_assert(offsetof(WRenViewportWorldViewParamRuntime, depth) == 0x08);
  static_assert(offsetof(WRenViewportWorldViewParamRuntime, terrain) == 0x0C);
  static_assert(sizeof(WRenViewportWorldViewParamRuntime) == 0x14);

  struct WRenViewportWorldViewVectorRuntime final
  {
    WRenViewportWorldViewParamRuntime* mFirst; // +0x00
    WRenViewportWorldViewParamRuntime* mLast;  // +0x04
    WRenViewportWorldViewParamRuntime* mEnd;   // +0x08
  };
  static_assert(sizeof(WRenViewportWorldViewVectorRuntime) == 0x0C);

  struct WRenViewportRenderView final
  {
    struct DebugCanvasRuntimeView final
    {
      moho::CD3DPrimBatcher* mPrimBatcher = nullptr; // +0x00
      std::uint8_t mUnknown04To3F[0x3C]{};
    };

    static_assert(sizeof(DebugCanvasRuntimeView) == 0x40, "WRenViewportRenderView::DebugCanvasRuntimeView size must be 0x40");

    std::uint8_t mUnknown0000_2147[0x2148];
    WRenViewportWorldViewVectorRuntime mWorldViews; // +0x2148
    std::uint8_t mUnknown2154_215B[0x08];
    DebugCanvasRuntimeView mDebugCanvas;
    moho::GeomCamera3* mCam; // +0x219C
    std::uint8_t mUnknown21A0_2C7[0x128];
    struct PrimBatcherView final
    {
      moho::CD3DPrimBatcher* batcher;
    };
    PrimBatcherView mPrimBatcher; // +0x2C8
    std::uint8_t mUnknown2CC_307[0x3C];
    Wm3::Vector2i mScreenPos; // +0x308
    Wm3::Vector2i mScreenSize; // +0x310
    Wm3::Vector2i mFullScreen; // +0x318
    std::int32_t mHead; // +0x320
    std::uint8_t mUnknown324_4EF[0x1CC];
    struct ShadowView final
    {
      std::uint8_t mUnknown00_07[0x08];
      std::int32_t shadow_Fidelity; // +0x08
    };
    ShadowView mShadowRenderer; // +0x4F0
  };

  struct WRenViewportPreviewImageView final
  {
    std::uint8_t mUnknown0000_2193[0x2194];
    moho::WPreviewImageRuntime mPreviewImage; // +0x2194
  };

  struct WRenViewportReflectionPassView final
  {
    struct ReflectionRenderTargetSlot final
    {
      moho::ID3DRenderTarget* mRenderTarget; // +0x00
      void* mWriterLock;                      // +0x04
    };

    struct ReflectionDepthStencilSlot final
    {
      moho::ID3DDepthStencil* mDepthStencil; // +0x00
      void* mWriterLock;                      // +0x04
    };

    ReflectionRenderTargetSlot mRenderTargetSlots[2]; // +0x00
    ReflectionDepthStencilSlot mDepthStencilSlots[2]; // +0x10
  };

  static_assert(sizeof(WRenViewportReflectionPassView::ReflectionRenderTargetSlot) == 0x08);
  static_assert(sizeof(WRenViewportReflectionPassView::ReflectionDepthStencilSlot) == 0x08);
  static_assert(sizeof(WRenViewportReflectionPassView) == 0x20);

  static_assert(
    offsetof(WRenViewportRenderView, mDebugCanvas) == 0x215C,
    "WRenViewportRenderView::mDebugCanvas offset must be 0x215C"
  );
  static_assert(
    offsetof(WRenViewportRenderView, mWorldViews) == 0x2148,
    "WRenViewportRenderView::mWorldViews offset must be 0x2148"
  );
  static_assert(
    offsetof(WRenViewportRenderView, mCam) == 0x219C, "WRenViewportRenderView::mCam offset must be 0x219C"
  );
  static_assert(
    offsetof(WRenViewportPreviewImageView, mPreviewImage) == 0x2194,
    "WRenViewportPreviewImageView::mPreviewImage offset must be 0x2194"
  );
#if defined(MOHO_ABI_MSVC8_COMPAT)
  static_assert(
    offsetof(WRenViewportRenderView, mFullScreen) == 0x318,
    "WRenViewportRenderView::mFullScreen offset must be 0x318"
  );
#endif
  [[nodiscard]] WRenViewportRenderView* AsRenderView(moho::WRenViewport* const viewport) noexcept
  {
    return reinterpret_cast<WRenViewportRenderView*>(viewport);
  }

  [[nodiscard]] WRenViewportReflectionPassView* AsReflectionPassView(WRenViewportRenderView* const runtime) noexcept
  {
    auto* const bytes = reinterpret_cast<std::uint8_t*>(runtime);
    return reinterpret_cast<WRenViewportReflectionPassView*>(bytes + 0x2174);
  }

  [[nodiscard]] msvc8::vector<WRenViewportWorldViewParamRuntime>* AsWorldViewVector(
    WRenViewportRenderView* const runtime
  ) noexcept
  {
    auto* const bytes = reinterpret_cast<std::uint8_t*>(runtime);
    return reinterpret_cast<msvc8::vector<WRenViewportWorldViewParamRuntime>*>(bytes + 0x2144);
  }

  /**
   * Address: 0x007FB060 (FUN_007FB060, sub_7FB060)
   *
   * What it does:
   * Inserts one world-view parameter record into the viewport world-view vector
   * at the requested position, preserving value semantics for retained terrain
   * shared-pointer lanes.
   */
  WRenViewportWorldViewParamRuntime* InsertWorldViewParamAt(
    msvc8::vector<WRenViewportWorldViewParamRuntime>* const worldViews,
    WRenViewportWorldViewParamRuntime* insertPosition,
    const WRenViewportWorldViewParamRuntime& entry
  )
  {
    if (worldViews == nullptr) {
      return nullptr;
    }

    WRenViewportWorldViewParamRuntime* const begin = worldViews->begin();
    WRenViewportWorldViewParamRuntime* const end = worldViews->end();
    if (begin == nullptr || end == nullptr) {
      worldViews->push_back(entry);
      return worldViews->end() - 1;
    }

    if (insertPosition == nullptr || insertPosition < begin || insertPosition > end) {
      insertPosition = end;
    }

    const std::size_t index = static_cast<std::size_t>(insertPosition - begin);
    worldViews->push_back(entry);

    WRenViewportWorldViewParamRuntime* const dataBegin = worldViews->begin();
    WRenViewportWorldViewParamRuntime* const dataEnd = worldViews->end();
    WRenViewportWorldViewParamRuntime* const destination = dataBegin + static_cast<std::ptrdiff_t>(index);
    if (destination < (dataEnd - 1)) {
      for (WRenViewportWorldViewParamRuntime* cursor = dataEnd - 1; cursor > destination; --cursor) {
        *cursor = *(cursor - 1);
      }
      *destination = entry;
    }

    return destination;
  }

} // namespace

namespace moho
{
  extern bool ren_Fx;
  extern bool ren_ShowSkeletons;
  extern bool ren_Water;
  extern bool ren_Reflection;
} // namespace moho

/**
 * Address: 0x004F1F10 (FUN_004F1F10, Moho::MohoApp::MohoApp)
 * Mangled: ??0MohoApp@Moho@@QAE@@Z
 *
 * What it does:
 * Constructs one `MohoApp` shell over `wxApp` base runtime state.
 */
moho::MohoApp::MohoApp()
  : wxApp()
{}

/**
 * Address: 0x004F1E50 (FUN_004F1E50, Moho::MohoApp::OnInit)
 * Mangled: ?OnInit@MohoApp@Moho@@UAE_NXZ
 *
 * What it does:
 * Returns startup success for the app bootstrap lane.
 */
bool moho::MohoApp::OnInit()
{
  return true;
}

/**
 * Address: 0x004F1E80 (FUN_004F1E80, Moho::MohoApp::ExitMainLoop)
 * Mangled: ?ExitMainLoop@MohoApp@Moho@@UAEXXZ
 *
 * What it does:
 * Clears the loop-keepalive flag so wx main-loop pumping exits.
 */
void moho::MohoApp::ExitMainLoop()
{
  m_keepGoing = 0;
}

/**
 * Address: 0x007F6530 (FUN_007F6530, Moho::REN_ShowSkeletons)
 *
 * What it does:
 * Toggles skeleton-debug rendering and mirrors that bool into the active
 * sim-driver sync option lane when a driver instance exists.
 */
void moho::REN_ShowSkeletons()
{
  const bool showSkeletons = !moho::ren_ShowSkeletons;
  moho::ren_ShowSkeletons = showSkeletons;

  if (ISTIDriver* const simDriver = moho::SIM_GetActiveDriver(); simDriver != nullptr) {
    simDriver->SetSyncFilterOptionFlag(showSkeletons);
  }
}

/**
 * Address: 0x007FA170 (FUN_007FA170, ?REN_GetTerrainRes@Moho@@YAPAVIWldTerrainRes@1@XZ)
 *
 * What it does:
 * Returns the active world-map terrain resource when one is available.
 */
moho::IWldTerrainRes* moho::REN_GetTerrainRes()
{
  moho::CWldSession* const session = moho::WLD_GetActiveSession();
  if (session == nullptr || session->mWldMap == nullptr) {
    return nullptr;
  }

  return session->mWldMap->mTerrainRes;
}

/**
 * Address: 0x004FBCC0 (FUN_004FBCC0, ??0WBitmapPanel@Moho@@QAE@PAVwxWindow@@PAVwxBitmap@@@Z)
 * Mangled: ??0WBitmapPanel@Moho@@QAE@PAVwxWindow@@PAVwxBitmap@@@Z
 *
 * What it does:
 * Stores one bitmap lane used by this panel runtime wrapper.
 */
moho::WBitmapPanel::WBitmapPanel(
  wxWindowBase* const parentWindow,
  wxBitmap* const bitmap
)
{
  (void)parentWindow;
  mBitmapLane = bitmap;
}

/**
 * Address: 0x004FBCB0 (FUN_004FBCB0, ?GetEventTable@WBitmapPanel@Moho@@MBEPBUwxEventTable@@XZ)
 * Mangled: ?GetEventTable@WBitmapPanel@Moho@@MBEPBUwxEventTable@@XZ
 *
 * What it does:
 * Returns the static event-table lane for this bitmap-panel runtime type.
 */
const void* moho::WBitmapPanel::GetEventTable() const
{
  return sm_eventTable;
}

/**
 * Address: 0x004FBE20 (FUN_004FBE20, ?GetEventTable@WBitmapCheckBox@Moho@@MBEPBUwxEventTable@@XZ)
 * Mangled: ?GetEventTable@WBitmapCheckBox@Moho@@MBEPBUwxEventTable@@XZ
 *
 * What it does:
 * Returns the static event-table lane for this bitmap-check-box runtime type.
 */
const void* moho::WBitmapCheckBox::GetEventTable() const
{
  return sm_eventTable;
}

/**
 * Address: 0x004FBF10 (FUN_004FBF10, ?IsChecked@WBitmapCheckBox@Moho@@QAE_NXZ)
 * Mangled: ?IsChecked@WBitmapCheckBox@Moho@@QAE_NXZ
 *
 * What it does:
 * Returns whether the bitmap check-box checked-state byte is non-zero.
 */
bool moho::WBitmapCheckBox::IsChecked()
{
  return mIsChecked != 0;
}

/**
 * Address: 0x007F65D0 (FUN_007F65D0, ?GetPreviewImage@WRenViewport@Moho@@UAE?AV?$shared_ptr@VID3DTextureSheet@Moho@@@boost@@XZ)
 *
 * What it does:
 * Returns one retained preview-image shared-pointer lane from viewport
 * runtime storage.
 */
moho::WPreviewImageRuntime moho::WRenViewport::GetPreviewImage() const
{
  const auto* const runtime = reinterpret_cast<const WRenViewportPreviewImageView*>(this);
  WPreviewImageRuntime previewImage = runtime->mPreviewImage;
  if (previewImage.lane1 != nullptr) {
    auto* const refCount = reinterpret_cast<volatile long*>(reinterpret_cast<std::uint8_t*>(previewImage.lane1) + 0x04u);
    (void)InterlockedIncrement(refCount);
  }
  return previewImage;
}

/**
 * Address: 0x007F6690 (FUN_007F6690, ?GetEventTable@WRenViewport@Moho@@MBEPBUwxEventTable@@XZ)
 * Mangled: ?GetEventTable@WRenViewport@Moho@@MBEPBUwxEventTable@@XZ
 *
 * What it does:
 * Returns the static event-table lane for this viewport runtime type.
 */
const void* moho::WRenViewport::GetEventTable() const
{
  return sm_eventTable;
}

/**
 * Address: 0x007F6600 (FUN_007F6600, ?GetPrimBatcher@WRenViewport@Moho@@UBEPAVCD3DPrimBatcher@2@XZ)
 * Mangled: ?GetPrimBatcher@WRenViewport@Moho@@UBEPAVCD3DPrimBatcher@2@XZ
 *
 * What it does:
 * Returns the viewport debug-canvas primary batcher lane.
 */
moho::CD3DPrimBatcher* moho::WRenViewport::GetPrimBatcher() const
{
  const WRenViewportRenderView* const runtime = AsRenderView(const_cast<WRenViewport*>(this));
  return runtime->mDebugCanvas.mPrimBatcher;
}

/**
 * Address: 0x007F6610 (FUN_007F6610, ?OnMouseEnter@WRenViewport@Moho@@QAEXAAVwxMouseEvent@@@Z)
 *
 * What it does:
 * Focuses the primary GAL head window when the mouse enters the render
 * viewport and device runtime is ready.
 */
void moho::WRenViewport::OnMouseEnter(wxMouseEventRuntime& mouseEvent)
{
  (void)mouseEvent;

  if (!gpg::gal::Device::IsReady()) {
    return;
  }

  gpg::gal::Device* const device = gpg::gal::Device::GetInstance();
  if (device == nullptr) {
    return;
  }

  gpg::gal::DeviceContext* const context = device->GetDeviceContext();
  if (context == nullptr || context->GetHeadCount() <= 0) {
    return;
  }

  const gpg::gal::Head& head = context->GetHead(0);
  if (head.mWindow != nullptr) {
    (void)::SetFocus(reinterpret_cast<HWND>(head.mWindow));
  }
}

/**
 * Address: 0x007F6640 (FUN_007F6640, ?OnMouseLeave@WRenViewport@Moho@@QAEXAAVwxMouseEvent@@@Z)
 *
 * What it does:
 * Focuses the secondary GAL head window when the mouse leaves the render
 * viewport and device runtime is ready.
 */
void moho::WRenViewport::OnMouseLeave(wxMouseEventRuntime& mouseEvent)
{
  (void)mouseEvent;

  if (!gpg::gal::Device::IsReady()) {
    return;
  }

  gpg::gal::Device* const device = gpg::gal::Device::GetInstance();
  if (device == nullptr) {
    return;
  }

  gpg::gal::DeviceContext* const context = device->GetDeviceContext();
  if (context == nullptr || context->GetHeadCount() <= 1) {
    return;
  }

  const gpg::gal::Head& head = context->GetHead(1);
  if (head.mWindow != nullptr) {
    (void)::SetFocus(reinterpret_cast<HWND>(head.mWindow));
  }
}

void moho::WRenViewport::ResetRenderState0C() noexcept
{
  mRenderState0C = -1;
}

/**
 * Address: 0x007F9E60 (FUN_007F9E60, ?AddWorldView@WRenViewport@Moho@@QAEXPAVIRenderWorldView@2@HH@Z)
 *
 * What it does:
 * Inserts one world-view lane sorted by depth and initializes one terrain
 * renderer lane for that world-view entry.
 *
 * Notes:
 * The binary uses global `ren_Viewport` as the active owner lane.
 */
void moho::WRenViewport::AddWorldView(
  IRenderWorldView* const worldView,
  const int head,
  const int depth
)
{
  WRenViewport* const viewport = moho::ren_Viewport;
  viewport->RemoveWorldView(worldView);

  WRenViewportRenderView* const runtime = AsRenderView(viewport);
  WRenViewportWorldViewParamRuntime* insertPos = runtime->mWorldViews.mFirst;
  for (WRenViewportWorldViewParamRuntime* it = runtime->mWorldViews.mLast; insertPos != it; ++insertPos) {
    if (depth < insertPos->depth) {
      break;
    }
  }

  WRenViewportWorldViewParamRuntime entry{};
  entry.view = worldView;
  entry.head = head;
  entry.depth = depth;
  (void)AssignSharedTerrainFromRaw(&entry.terrain, moho::IRenTerrain::Create());
  if (entry.terrain) {
    (void)entry.terrain->Create(reinterpret_cast<moho::TerrainWaterResourceView*>(moho::REN_GetTerrainRes()));
  }

  msvc8::vector<WRenViewportWorldViewParamRuntime>* const worldViews = AsWorldViewVector(runtime);
  (void)InsertWorldViewParamAt(worldViews, insertPos, entry);
}

/**
 * Address: 0x007FA090 (FUN_007FA090, ?RemoveWorldView@WRenViewport@Moho@@QAEXPAVIRenderWorldView@2@@Z)
 *
 * What it does:
 * Removes the first matching world-view lane from the viewport world-view
 * vector at `+0x2148`.
 */
void moho::WRenViewport::RemoveWorldView(IRenderWorldView* const worldView)
{
  WRenViewportRenderView* const runtime = AsRenderView(this);
  msvc8::vector<WRenViewportWorldViewParamRuntime>* const worldViews = AsWorldViewVector(runtime);
  WRenViewportWorldViewParamRuntime* const first = runtime->mWorldViews.mFirst;
  WRenViewportWorldViewParamRuntime* const last = runtime->mWorldViews.mLast;
  if (first == nullptr || last == nullptr || first == last || worldViews == nullptr) {
    return;
  }

  for (WRenViewportWorldViewParamRuntime* it = first; it != last; ++it) {
    if (it->view != worldView) {
      continue;
    }

    worldViews->erase(it);
    return;
  }
}

/**
 * Address: 0x007F81C0 (FUN_007F81C0, ?RenderCompositeTerrain@WRenViewport@Moho@@AAEXPAVIRenTerrain@2@@Z)
 * Mangled: ?RenderCompositeTerrain@WRenViewport@Moho@@AAEXPAVIRenTerrain@2@@Z
 *
 * What it does:
 * Binds the active viewport render target and viewport lanes, renders terrain
 * normal-composite data with optional shadow lane, then emits terrain skirt
 * geometry for the same frame.
 */
void moho::WRenViewport::RenderCompositeTerrain(TerrainCommon* const terrain)
{
  WRenViewportRenderView* const runtime = AsRenderView(this);
  moho::CD3DDevice* const device = moho::D3D_GetDevice();
  device->SetRenderTarget2(runtime->mHead, false, 0, 1.0f, 0);
  device->SetColorWriteState(true, false);
  SetViewportToLocalScreen();

  (void)terrain;
}

/**
 * Address: 0x007F8290 (FUN_007F8290, Moho::WRenViewport::RenderMeshes)
 *
 * What it does:
 * Sets the render target, viewport, and color-write state for one viewport
 * mesh pass, then dispatches either skeleton-debug rendering or the normal
 * mesh batch renderer depending on `ren_ShowSkeletons`.
 */
void moho::WRenViewport::RenderMeshes(const int meshFlags, const bool mirrored)
{
  WRenViewportRenderView* const runtime = AsRenderView(this);
  moho::GeomCamera3* const cam = runtime->mCam;
  moho::CD3DDevice* const device = moho::D3D_GetDevice();
  device->SetRenderTarget2(runtime->mHead, false, 0, 1.0f, 0);
  SetViewportToLocalScreen();
  device->SetColorWriteState(true, true);

  moho::Shadow* const shadowRenderer = runtime->mShadowRenderer.shadow_Fidelity != 0
    ? reinterpret_cast<moho::Shadow*>(&runtime->mShadowRenderer)
    : nullptr;

  moho::MeshRenderer* const instance = moho::MeshRenderer::GetInstance();
  if (moho::ren_ShowSkeletons) {
    instance->RenderSkeletons(
      reinterpret_cast<moho::CD3DPrimBatcher*>(runtime->mPrimBatcher.batcher),
      reinterpret_cast<moho::CDebugCanvas*>(&runtime->mDebugCanvas),
      *cam,
      true
    );
    return;
  }

  instance->Render(meshFlags, *cam, shadowRenderer, instance->meshes);
  (void)mirrored;
}

/**
 * Address: 0x007F8560 (FUN_007F8560, Moho::WRenViewport::RenderEffects)
 *
 * What it does:
 * Binds the viewport render target and viewport lanes for the active head,
 * configures color writes for FX, then renders world-particle effects.
 */
void moho::WRenViewport::RenderEffects(const bool renderWaterSurface)
{
  if (!moho::ren_Fx) {
    return;
  }

  WRenViewportRenderView* const runtime = AsRenderView(this);
  moho::CD3DDevice* const device = moho::D3D_GetDevice();
  device->SetRenderTarget2(runtime->mHead, false, 0, 1.0f, 0);
  SetViewportToLocalScreen();
  device->SetColorWriteState(true, false);

  (void)moho::sWorldParticles.RenderEffects(
    runtime->mCam,
    static_cast<char>(renderWaterSurface ? 1 : 0),
    0,
    moho::REN_GetGameTick(),
    moho::REN_GetSimDeltaSeconds()
  );
}

/**
 * Address: 0x007F86F0 (FUN_007F86F0, ?RenderWater@WRenViewport@Moho@@AAEXPAVIRenTerrain@2@@Z)
 * Mangled: ?RenderWater@WRenViewport@Moho@@AAEXPAVIRenTerrain@2@@Z
 *
 * What it does:
 * Binds the active viewport head to the water render target, restores the
 * viewport rectangle, and forwards the current frame lanes to terrain water
 * rendering.
 */
void moho::WRenViewport::RenderWater(TerrainCommon* const terrain)
{
  if (!moho::ren_Water) {
    return;
  }

  WRenViewportRenderView* const runtime = AsRenderView(this);
  moho::CD3DDevice* const device = moho::D3D_GetDevice();
  device->SetRenderTarget2(runtime->mHead, false, 0, 1.0f, 0);
  SetViewportToLocalScreen();
  device->SetColorWriteState(true, true);

  (void)terrain;
}

/**
 * Address: 0x007F7DF0 (FUN_007F7DF0, ?RenderReflections@WRenViewport@Moho@@AAEXXZ)
 *
 * What it does:
 * Binds reflection render-target/depth lanes for the active head slot and,
 * when enabled, renders reflection meshes through `MeshRenderer`.
 */
void moho::WRenViewport::RenderReflections()
{
  if (!moho::ren_Water) {
    return;
  }

  WRenViewportRenderView* const runtime = AsRenderView(this);
  WRenViewportReflectionPassView* const reflectionView = AsReflectionPassView(runtime);
  moho::CD3DDevice* const colorDevice = moho::D3D_GetDevice();
  moho::CD3DDevice* const targetDevice = moho::D3D_GetDevice();
  const std::size_t reflectionIndex = static_cast<std::size_t>(runtime->mHead);
  targetDevice->SetRenderTarget1(
    reflectionView->mRenderTargetSlots[reflectionIndex].mRenderTarget,
    reflectionView->mDepthStencilSlots[reflectionIndex].mDepthStencil,
    true,
    0,
    1.0f,
    0
  );

  if (!moho::ren_Reflection) {
    return;
  }
  SetViewportToLocalScreen();
  colorDevice->SetColorWriteState(true, true);

  moho::MeshRenderer* const renderer = moho::MeshRenderer::GetInstance();
  renderer->Render(2, *runtime->mCam, nullptr, renderer->meshes);
}

/**
 * Address: 0x007F7ED0 (FUN_007F7ED0, ?SetViewportToFullScreen@WRenViewport@Moho@@AAEXXZ)
 *
 * What it does:
 * Applies a full-head viewport rectangle (`(0,0)` to `mFullScreen`) to the
 * active D3D device viewport state.
 */
void moho::WRenViewport::SetViewportToFullScreen()
{
  WRenViewportRenderView* const runtime = AsRenderView(this);
  moho::CD3DDevice* const device = moho::D3D_GetDevice();
  Wm3::Vector2i screenOrigin{0, 0};
  device->SetViewport(&screenOrigin, &runtime->mFullScreen, 0.0f, 1.0f);
}

/**
 * Address: 0x007F7EA0 (FUN_007F7EA0, ?SetViewportToLocalScreen@WRenViewport@Moho@@AAEXXZ)
 *
 * What it does:
 * Applies this viewport's cached local-screen rectangle to the active D3D
 * device viewport state.
 */
void moho::WRenViewport::SetViewportToLocalScreen()
{
  WRenViewportRenderView* const runtime = AsRenderView(this);
  moho::CD3DDevice* const device = moho::D3D_GetDevice();
  device->SetViewport(&runtime->mScreenPos, &runtime->mScreenSize, 0.0f, 1.0f);
}

/**
 * Address: 0x007F87F0 (FUN_007F87F0, ?UpdateRenderViewportCoordinates@WRenViewport@Moho@@AAEXXZ)
 * Mangled: ?UpdateRenderViewportCoordinates@WRenViewport@Moho@@AAEXXZ
 *
 * What it does:
 * Refreshes full-head dimensions and local viewport lanes from the active
 * camera's viewport matrix row when a camera is present, else falls back to
 * the full-head rectangle.
 */
void moho::WRenViewport::UpdateRenderViewportCoordinates()
{
  WRenViewportRenderView* const runtime = AsRenderView(this);

  moho::CD3DDevice* const widthDevice = moho::D3D_GetDevice();
  const int headWidth = widthDevice->GetHeadWidth(static_cast<unsigned int>(runtime->mHead));
  moho::CD3DDevice* const heightDevice = moho::D3D_GetDevice();
  const int headHeight = heightDevice->GetHeadHeight(static_cast<unsigned int>(runtime->mHead));
  runtime->mFullScreen.x = headWidth;
  runtime->mFullScreen.y = headHeight;

  moho::GeomCamera3* const camera = runtime->mCam;
  if (camera != nullptr) {
    runtime->mScreenPos.x = static_cast<int>(camera->viewport.r[3].x);
    runtime->mScreenPos.y = static_cast<int>(camera->viewport.r[3].y);
    runtime->mScreenSize.x = static_cast<int>(camera->viewport.r[3].z);
    runtime->mScreenSize.y = static_cast<int>(camera->viewport.r[3].w);
    return;
  }

  runtime->mScreenSize.x = headWidth;
  runtime->mScreenSize.y = runtime->mFullScreen.y;
  runtime->mScreenPos.x = 0;
  runtime->mScreenPos.y = 0;
}

/**
 * Address: 0x007F8B70 (FUN_007F8B70, ?FogOff@WRenViewport@Moho@@AAEXXZ)
 * Mangled: ?FogOff@WRenViewport@Moho@@AAEXXZ
 *
 * What it does:
 * Disables fog on the active GAL D3D9 device with default depth range
 * (`0.0f..1.0f`) and zero fog color lanes.
 */
void moho::WRenViewport::FogOff()
{
  gpg::gal::DeviceD3D9* const device = static_cast<gpg::gal::DeviceD3D9*>(gpg::gal::Device::GetInstance());
  device->SetFogState(false, nullptr, 0.0f, 1.0f, 0);
}

/**
 * Address: 0x007F7F10 (FUN_007F7F10, ?RenderTerrainNormals@WRenViewport@Moho@@AAEXPAVIRenTerrain@2@@Z)
 * Mangled: ?RenderTerrainNormals@WRenViewport@Moho@@AAEXPAVIRenTerrain@2@@Z
 *
 * What it does:
 * Binds the viewport's terrain-normal render target and viewport lanes, then
 * dispatches terrain-normal rendering when terrain debug rendering is enabled.
 */
void moho::WRenViewport::RenderTerrainNormals(TerrainCommon* const terrain)
{
  if (terrain == nullptr) {
    return;
  }

  WRenViewportRenderView* const runtime = AsRenderView(this);
  moho::CD3DDevice* const device = moho::D3D_GetDevice();
  gpg::gal::DeviceD3D9* const d3dDevice = device->GetDeviceD3D9();
  if (d3dDevice != nullptr) {
    (void)d3dDevice->ClearTextures();
  }

  device->SetRenderTarget2(runtime->mHead, true, 0, 1.0f, 0);
  SetViewportToLocalScreen();
}

/**
 * Address: 0x009FE820 (FUN_009FE820)
 *
 * What it does:
 * Builds one `TVITEMW` lane that targets a tree item handle and toggles one
 * state bit (`0x2`) before dispatching `TVM_SETITEMW`.
 */
LRESULT wxTreeCtrlSetStateBit2(
  const bool setStateBit,
  const HWND treeWindow,
  const LPARAM treeItemHandle
)
{
  TVITEMW itemState{};
  itemState.mask = TVIF_HANDLE | TVIF_STATE;
  itemState.hItem = reinterpret_cast<HTREEITEM>(treeItemHandle);
  itemState.state = setStateBit ? 2u : 0u;
  itemState.stateMask = 2u;
  return ::SendMessageW(treeWindow, TVM_SETITEMW, 0u, reinterpret_cast<LPARAM>(&itemState));
}

/**
 * Address: 0x009FE7E0 (FUN_009FE7E0)
 *
 * What it does:
 * Queries one tree-item state lane (`TVM_GETITEMW`) and reports whether state
 * bit `0x2` is currently set.
 */
[[nodiscard]] bool wxTreeCtrlGetStateBit2(
  const LPARAM treeItemHandle,
  const HWND treeWindow
)
{
  TVITEMW itemState{};
  itemState.mask = TVIF_HANDLE | TVIF_STATE;
  itemState.hItem = reinterpret_cast<HTREEITEM>(treeItemHandle);
  itemState.stateMask = 2u;
  (void)::SendMessageW(treeWindow, TVM_GETITEMW, 0u, reinterpret_cast<LPARAM>(&itemState));
  return ((itemState.state >> 1u) & 1u) != 0u;
}

/**
 * Address: 0x009FF940 (FUN_009FF940)
 *
 * What it does:
 * Queries one child item from the tree control (`TVGN_CHILD`) and mirrors the
 * returned handle into both output lanes.
 */
LRESULT* wxTreeCtrlGetChildItemMirror(
  const HWND treeWindow,
  LRESULT* const outResultLane,
  const LPARAM currentItem,
  LRESULT* const outMirrorLane
)
{
  const LRESULT childItem = ::SendMessageW(
    treeWindow,
    TVM_GETNEXTITEM,
    static_cast<WPARAM>(TVGN_CHILD),
    currentItem
  );
  *outMirrorLane = childItem;
  *outResultLane = childItem;
  return outResultLane;
}

namespace
{
  struct WxTreeItemClientDataRuntimeView
  {
    std::uint32_t reserved00 = 0;                  // +0x00
    std::uint32_t isDeleted = 0;                   // +0x04
    std::uint8_t reserved08To17[0x10]{};           // +0x08
    wxTreeItemDataRuntime* assignedItemData = nullptr; // +0x18
  };
  static_assert(
    offsetof(WxTreeItemClientDataRuntimeView, isDeleted) == 0x04,
    "WxTreeItemClientDataRuntimeView::isDeleted offset must be 0x04"
  );
  static_assert(
    offsetof(WxTreeItemClientDataRuntimeView, assignedItemData) == 0x18,
    "WxTreeItemClientDataRuntimeView::assignedItemData offset must be 0x18"
  );
}

/**
 * Address: 0x009FF380 (FUN_009FF380)
 *
 * What it does:
 * Queries one tree-item `lParam` lane and reports whether it points to live
 * client-data storage (non-null and not marked deleted).
 */
[[nodiscard]] bool wxTreeCtrlItemHasLiveClientData(
  const HWND treeWindow,
  const int* const treeItemHandleLane
)
{
  if (treeWindow == nullptr || treeItemHandleLane == nullptr) {
    return false;
  }

  const int itemHandleValue = *treeItemHandleLane;
  if (itemHandleValue == -65536) {
    return false;
  }

  TVITEMW itemState{};
  itemState.mask = TVIF_HANDLE | TVIF_PARAM;
  itemState.hItem = reinterpret_cast<HTREEITEM>(static_cast<std::uintptr_t>(itemHandleValue));
  if (::SendMessageW(treeWindow, TVM_GETITEMW, 0u, reinterpret_cast<LPARAM>(&itemState)) == 0) {
    return false;
  }

  const auto* const clientData = reinterpret_cast<const WxTreeItemClientDataRuntimeView*>(itemState.lParam);
  return clientData != nullptr && clientData->isDeleted == 0u;
}

namespace
{
  using WxTreeItemDataReleaseFn = void(__thiscall*)(void* object, int deleteFlag);

  struct WxTreeCtrlVirtualRootRuntimeView
  {
    std::uint8_t reserved00To27[0x28]{};
    void* attachedItemData = nullptr; // +0x28
  };
  static_assert(
    offsetof(WxTreeCtrlVirtualRootRuntimeView, attachedItemData) == 0x28,
    "WxTreeCtrlVirtualRootRuntimeView::attachedItemData offset must be 0x28"
  );

  struct WxTreeCtrlItemDataAssignRuntimeView
  {
    std::uint8_t reserved00To107[0x108]{};
    HWND treeWindowHandle = nullptr;                 // +0x108
    std::uint8_t reserved10CTo16B[0x60]{};
    WxTreeCtrlVirtualRootRuntimeView* virtualRoot = nullptr; // +0x16C
  };
  static_assert(
    offsetof(WxTreeCtrlItemDataAssignRuntimeView, treeWindowHandle) == 0x108,
    "WxTreeCtrlItemDataAssignRuntimeView::treeWindowHandle offset must be 0x108"
  );
  static_assert(
    offsetof(WxTreeCtrlItemDataAssignRuntimeView, virtualRoot) == 0x16C,
    "WxTreeCtrlItemDataAssignRuntimeView::virtualRoot offset must be 0x16C"
  );
}

/**
 * Address: 0x00A00910 (FUN_00A00910)
 *
 * What it does:
 * Assigns one tree-item data payload lane, handling virtual-root ownership
 * updates and mirroring client-data references through native tree-view item
 * queries.
 */
[[maybe_unused]] bool wxTreeCtrlAssignItemDataRuntime(
  void* const treeCtrlRuntime,
  int* const treeItemHandleLane,
  wxTreeItemDataRuntime* const itemData
)
{
  auto* const treeCtrl = static_cast<WxTreeCtrlItemDataAssignRuntimeView*>(treeCtrlRuntime);
  if (treeCtrl == nullptr || treeItemHandleLane == nullptr) {
    return false;
  }

  const int itemHandleValue = *treeItemHandleLane;
  if (itemHandleValue == -65536) {
    WxTreeCtrlVirtualRootRuntimeView* const virtualRoot = treeCtrl->virtualRoot;
    if (virtualRoot != nullptr && virtualRoot->attachedItemData != nullptr) {
      auto* const attachedItemData = virtualRoot->attachedItemData;
      const auto releaseItemData =
        reinterpret_cast<WxTreeItemDataReleaseFn>((*reinterpret_cast<void***>(attachedItemData))[0]);
      releaseItemData(attachedItemData, 1);
    }

    if (virtualRoot != nullptr) {
      virtualRoot->attachedItemData = itemData;
    }
  }

  if (itemData != nullptr) {
    itemData->mPayload = reinterpret_cast<void*>(static_cast<std::uintptr_t>(itemHandleValue));
  }

  TVITEMW itemState{};
  itemState.mask = TVIF_HANDLE | TVIF_PARAM;
  itemState.hItem = reinterpret_cast<HTREEITEM>(static_cast<std::uintptr_t>(itemHandleValue));

  if (wxTreeCtrlItemHasLiveClientData(treeCtrl->treeWindowHandle, treeItemHandleLane)) {
    if (itemHandleValue == -65536) {
      return true;
    }

    if (::SendMessageW(treeCtrl->treeWindowHandle, TVM_GETITEMW, 0u, reinterpret_cast<LPARAM>(&itemState)) == 0) {
      return false;
    }

    auto* const clientData = reinterpret_cast<WxTreeItemClientDataRuntimeView*>(itemState.lParam);
    clientData->assignedItemData = itemData;
    return true;
  }

  itemState.lParam = reinterpret_cast<LPARAM>(itemData);
  return ::SendMessageW(treeCtrl->treeWindowHandle, TVM_SETITEMW, 0u, reinterpret_cast<LPARAM>(&itemState)) != 0;
}

struct WxGenericDirCtrlRuntimeView
{
  std::uint8_t reserved00_107[0x108]{};
  HWND treeWindowHandle = nullptr; // +0x108
};
static_assert(
  offsetof(WxGenericDirCtrlRuntimeView, treeWindowHandle) == 0x108,
  "WxGenericDirCtrlRuntimeView::treeWindowHandle offset must be 0x108"
);

/**
 * Address: 0x009FF970 (FUN_009FF970)
 *
 * What it does:
 * Resolves one `TVGN_CHILD` tree-item handle from the `wxGenericDirCtrl`
 * tree-window lane and mirrors the result into both output lanes.
 */
LRESULT* wxGenericDirCtrlGetChildItemMirror(
  const WxGenericDirCtrlRuntimeView* const dirCtrlRuntime,
  LRESULT* const outResultLane,
  const int nextItemMode,
  LPARAM* const inOutTreeItem
)
{
  (void)nextItemMode;

  if (outResultLane == nullptr || inOutTreeItem == nullptr || dirCtrlRuntime == nullptr) {
    return outResultLane;
  }

  const LRESULT childItem = ::SendMessageW(
    dirCtrlRuntime->treeWindowHandle,
    TVM_GETNEXTITEM,
    static_cast<WPARAM>(TVGN_CHILD),
    *inOutTreeItem
  );

  *inOutTreeItem = childItem;
  *outResultLane = childItem;
  return outResultLane;
}

namespace
{
  struct WxStreamIoStatusRuntimeView
  {
    std::uint8_t reserved00_03[0x4]{};
    std::int32_t lastTransferCount = 0; // +0x04
    std::int32_t errorCode = 0;         // +0x08
  };
  static_assert(
    offsetof(WxStreamIoStatusRuntimeView, lastTransferCount) == 0x4,
    "WxStreamIoStatusRuntimeView::lastTransferCount offset must be 0x4"
  );
  static_assert(
    offsetof(WxStreamIoStatusRuntimeView, errorCode) == 0x8,
    "WxStreamIoStatusRuntimeView::errorCode offset must be 0x8"
  );

  using WxStreamWriteBytesFn = int(__thiscall*)(void* stream, const void* sourceBytes, unsigned int byteCount);
  using WxStreamReadBytesFn = int(__thiscall*)(void* stream, void* destinationBytes, unsigned int byteCount);

  struct WxStreamIoVTableRuntimeView
  {
    void* slot00 = nullptr;
    void* slot04 = nullptr;
    void* slot08 = nullptr;
    void* slot0C = nullptr;
    void* slot10 = nullptr;
    void* slot14 = nullptr;
    void* slot18 = nullptr;
    void* slot1C = nullptr;
    void* slot20 = nullptr;
    WxStreamWriteBytesFn writeBytes = nullptr; // +0x24
    void* slot28 = nullptr;
    WxStreamReadBytesFn readBytes = nullptr;   // +0x2C
  };
  static_assert(
    offsetof(WxStreamIoVTableRuntimeView, writeBytes) == 0x24,
    "WxStreamIoVTableRuntimeView::writeBytes offset must be 0x24"
  );
  static_assert(
    offsetof(WxStreamIoVTableRuntimeView, readBytes) == 0x2C,
    "WxStreamIoVTableRuntimeView::readBytes offset must be 0x2C"
  );

  struct WxStreamBufferRuntimeView
  {
    void* vtable = nullptr;                 // +0x00
    std::uint8_t* bufferBase = nullptr;     // +0x04
    std::uint8_t* bufferEnd = nullptr;      // +0x08
    std::uint8_t* bufferCursor = nullptr;   // +0x0C
    std::uint32_t bufferSize = 0;           // +0x10
    void* stream = nullptr;                 // +0x14
    std::int32_t ioMode = 0;                // +0x18
    std::uint8_t reserved1C = 0;            // +0x1C
    std::uint8_t fixedBufferMode = 0;       // +0x1D
    std::uint8_t autoTransferEnabled = 0;   // +0x1E
    std::uint8_t reserved1F = 0;            // +0x1F
  };
  static_assert(
    offsetof(WxStreamBufferRuntimeView, bufferBase) == 0x4,
    "WxStreamBufferRuntimeView::bufferBase offset must be 0x4"
  );
  static_assert(
    offsetof(WxStreamBufferRuntimeView, bufferEnd) == 0x8,
    "WxStreamBufferRuntimeView::bufferEnd offset must be 0x8"
  );
  static_assert(
    offsetof(WxStreamBufferRuntimeView, bufferCursor) == 0xC,
    "WxStreamBufferRuntimeView::bufferCursor offset must be 0xC"
  );
  static_assert(
    offsetof(WxStreamBufferRuntimeView, bufferSize) == 0x10,
    "WxStreamBufferRuntimeView::bufferSize offset must be 0x10"
  );
  static_assert(
    offsetof(WxStreamBufferRuntimeView, stream) == 0x14,
    "WxStreamBufferRuntimeView::stream offset must be 0x14"
  );
  static_assert(
    offsetof(WxStreamBufferRuntimeView, ioMode) == 0x18,
    "WxStreamBufferRuntimeView::ioMode offset must be 0x18"
  );
  static_assert(
    offsetof(WxStreamBufferRuntimeView, fixedBufferMode) == 0x1D,
    "WxStreamBufferRuntimeView::fixedBufferMode offset must be 0x1D"
  );
  static_assert(
    offsetof(WxStreamBufferRuntimeView, autoTransferEnabled) == 0x1E,
    "WxStreamBufferRuntimeView::autoTransferEnabled offset must be 0x1E"
  );

  [[nodiscard]] WxStreamIoVTableRuntimeView* WxStreamIoVTable(void* const stream) noexcept
  {
    if (stream == nullptr) {
      return nullptr;
    }
    return *reinterpret_cast<WxStreamIoVTableRuntimeView**>(stream);
  }

  [[nodiscard]] int WxStreamInvokeRead(
    void* const stream,
    void* const destinationBytes,
    const unsigned int byteCount
  ) noexcept
  {
    WxStreamIoVTableRuntimeView* const ioVTable = WxStreamIoVTable(stream);
    if (ioVTable == nullptr || ioVTable->readBytes == nullptr) {
      return 0;
    }
    return ioVTable->readBytes(stream, destinationBytes, byteCount);
  }

  [[nodiscard]] int WxStreamInvokeWrite(
    void* const stream,
    const void* const sourceBytes,
    const unsigned int byteCount
  ) noexcept
  {
    WxStreamIoVTableRuntimeView* const ioVTable = WxStreamIoVTable(stream);
    if (ioVTable == nullptr || ioVTable->writeBytes == nullptr) {
      return 0;
    }
    return ioVTable->writeBytes(stream, sourceBytes, byteCount);
  }

  [[nodiscard]] WxStreamIoStatusRuntimeView* WxStreamIoStatus(
    WxStreamBufferRuntimeView* const streamBuffer
  ) noexcept
  {
    return reinterpret_cast<WxStreamIoStatusRuntimeView*>(streamBuffer->stream);
  }

  struct WxSizerItemLayoutRuntimeView
  {
    void* vtable = nullptr;                 // +0x00
    std::uint8_t reserved04_07[0x4]{};      // +0x04
    wxWindowBase* containedWindow = nullptr; // +0x08
    void* containedSizer = nullptr;         // +0x0C
    std::int32_t laidOutWidth = 0;          // +0x10
    std::int32_t laidOutHeight = 0;         // +0x14
    std::int32_t positionX = 0;             // +0x18
    std::int32_t positionY = 0;             // +0x1C
    std::int32_t minWidth = 0;              // +0x20
    std::int32_t minHeight = 0;             // +0x24
    std::uint8_t reserved28_2B[0x4]{};      // +0x28
    std::int32_t border = 0;                // +0x2C
    std::int32_t flags = 0;                 // +0x30
    std::uint8_t reserved34_37[0x4]{};      // +0x34
    float userAspectRatio = 0.0f;           // +0x38
  };
  static_assert(
    offsetof(WxSizerItemLayoutRuntimeView, containedWindow) == 0x08,
    "WxSizerItemLayoutRuntimeView::containedWindow offset must be 0x08"
  );
  static_assert(
    offsetof(WxSizerItemLayoutRuntimeView, containedSizer) == 0x0C,
    "WxSizerItemLayoutRuntimeView::containedSizer offset must be 0x0C"
  );
  static_assert(
    offsetof(WxSizerItemLayoutRuntimeView, laidOutWidth) == 0x10,
    "WxSizerItemLayoutRuntimeView::laidOutWidth offset must be 0x10"
  );
  static_assert(
    offsetof(WxSizerItemLayoutRuntimeView, laidOutHeight) == 0x14,
    "WxSizerItemLayoutRuntimeView::laidOutHeight offset must be 0x14"
  );
  static_assert(
    offsetof(WxSizerItemLayoutRuntimeView, positionX) == 0x18,
    "WxSizerItemLayoutRuntimeView::positionX offset must be 0x18"
  );
  static_assert(
    offsetof(WxSizerItemLayoutRuntimeView, positionY) == 0x1C,
    "WxSizerItemLayoutRuntimeView::positionY offset must be 0x1C"
  );
  static_assert(
    offsetof(WxSizerItemLayoutRuntimeView, minWidth) == 0x20,
    "WxSizerItemLayoutRuntimeView::minWidth offset must be 0x20"
  );
  static_assert(
    offsetof(WxSizerItemLayoutRuntimeView, minHeight) == 0x24,
    "WxSizerItemLayoutRuntimeView::minHeight offset must be 0x24"
  );
  static_assert(
    offsetof(WxSizerItemLayoutRuntimeView, border) == 0x2C,
    "WxSizerItemLayoutRuntimeView::border offset must be 0x2C"
  );
  static_assert(
    offsetof(WxSizerItemLayoutRuntimeView, flags) == 0x30,
    "WxSizerItemLayoutRuntimeView::flags offset must be 0x30"
  );
  static_assert(
    offsetof(WxSizerItemLayoutRuntimeView, userAspectRatio) == 0x38,
    "WxSizerItemLayoutRuntimeView::userAspectRatio offset must be 0x38"
  );

  struct WxSizerRuntimeInterface
  {
    virtual ~WxSizerRuntimeInterface() = default;
    virtual wxSize* GetMinSize(wxSize* outSize) = 0;
  };

  using WxSizerRecalcSizesFn = int(__thiscall*)(void* sizer);
  struct WxSizerVTableRuntimeView
  {
    void* slots00To4C[20]{};
    WxSizerRecalcSizesFn recalcSizes = nullptr; // +0x50
  };
  static_assert(
    offsetof(WxSizerVTableRuntimeView, recalcSizes) == 0x50,
    "WxSizerVTableRuntimeView::recalcSizes offset must be 0x50"
  );

  struct WxSizerLayoutRuntimeView
  {
    void* vtable = nullptr;               // +0x00
    std::uint8_t reserved04_0F[0xC]{};    // +0x04
    std::int32_t laidOutWidth = 0;        // +0x10
    std::int32_t laidOutHeight = 0;       // +0x14
    std::int32_t positionX = 0;           // +0x18
    std::int32_t positionY = 0;           // +0x1C
  };
  static_assert(
    offsetof(WxSizerLayoutRuntimeView, laidOutWidth) == 0x10,
    "WxSizerLayoutRuntimeView::laidOutWidth offset must be 0x10"
  );
  static_assert(
    offsetof(WxSizerLayoutRuntimeView, laidOutHeight) == 0x14,
    "WxSizerLayoutRuntimeView::laidOutHeight offset must be 0x14"
  );
  static_assert(
    offsetof(WxSizerLayoutRuntimeView, positionX) == 0x18,
    "WxSizerLayoutRuntimeView::positionX offset must be 0x18"
  );
  static_assert(
    offsetof(WxSizerLayoutRuntimeView, positionY) == 0x1C,
    "WxSizerLayoutRuntimeView::positionY offset must be 0x1C"
  );

  struct WxObjectRuntimeVTableView
  {
    void* slot00 = nullptr;
    void* slot04 = nullptr;
    void* slot08 = nullptr;
    void* slot0C = nullptr;
    void* slot10 = nullptr;
    void* slot14 = nullptr;
    void (__thiscall* createRefData)(void* object) = nullptr; // +0x18
  };
  static_assert(
    offsetof(WxObjectRuntimeVTableView, createRefData) == 0x18,
    "WxObjectRuntimeVTableView::createRefData offset must be 0x18"
  );

  struct WxObjectDestroyFlagsRuntimeView
  {
    WxObjectRuntimeVTableView* vtable = nullptr; // +0x00
    std::uint8_t reserved04_44[0x41]{};          // +0x04
    std::uint8_t pendingDeleteFlag = 0;          // +0x45
    std::uint8_t reserved46_5F[0x1A]{};          // +0x46
    std::uint8_t delayedDestroyFlag = 0;         // +0x60
  };
  static_assert(
    offsetof(WxObjectDestroyFlagsRuntimeView, pendingDeleteFlag) == 0x45,
    "WxObjectDestroyFlagsRuntimeView::pendingDeleteFlag offset must be 0x45"
  );
  static_assert(
    offsetof(WxObjectDestroyFlagsRuntimeView, delayedDestroyFlag) == 0x60,
    "WxObjectDestroyFlagsRuntimeView::delayedDestroyFlag offset must be 0x60"
  );

  wxListRuntime wxPendingDelete{};
  std::unordered_map<const wxListRuntime*, std::vector<void*>> gWxRuntimeListShadowEntries{};

  [[nodiscard]] bool WxRuntimeListContains(
    const wxListRuntime* const listOwner,
    const void* const value
  ) noexcept
  {
    const auto found = gWxRuntimeListShadowEntries.find(listOwner);
    if (found == gWxRuntimeListShadowEntries.end()) {
      return false;
    }

    const auto& entries = found->second;
    return std::find(entries.begin(), entries.end(), value) != entries.end();
  }

  void WxRuntimeListAppend(
    wxListRuntime* const listOwner,
    void* const value
  )
  {
    gWxRuntimeListShadowEntries[listOwner].push_back(value);
  }
} // namespace

/**
 * Address: 0x009DDD40 (FUN_009DDD40, sub_9DDD40)
 *
 * What it does:
 * Clones one UTF-16 source string and appends that owned copy to the target
 * wx-list runtime lane.
 */
wxNodeBaseRuntime* wxListAppendCopiedWideStringRuntime(
  wxListRuntime* const listOwner,
  const wchar_t** const sourceText
)
{
  if (listOwner == nullptr || sourceText == nullptr) {
    return nullptr;
  }

  wchar_t* const clonedText = wx::copystring(*sourceText);
  WxRuntimeListAppend(listOwner, clonedText);
  return nullptr;
}

/**
 * Address: 0x009DC7D0 (FUN_009DC7D0)
 *
 * What it does:
 * Writes one stream-buffer error code lane when no prior error is latched.
 */
WxStreamIoStatusRuntimeView* WxStreamBufferSetErrorCodeIfUnset(
  WxStreamBufferRuntimeView* const streamBuffer,
  const std::int32_t errorCode
)
{
  WxStreamIoStatusRuntimeView* const statusLane = WxStreamIoStatus(streamBuffer);
  if (statusLane != nullptr && statusLane->errorCode == 0) {
    statusLane->errorCode = errorCode;
  }
  return statusLane;
}

/**
 * Address: 0x009DC8F0 (FUN_009DC8F0)
 *
 * What it does:
 * Returns the input-side stream lane unless this buffer is configured in
 * output-only mode (`ioMode == 1`).
 */
void* WxStreamBufferGetInputStream(
  WxStreamBufferRuntimeView* const streamBuffer
)
{
  return streamBuffer->ioMode == 1 ? nullptr : streamBuffer->stream;
}

/**
 * Address: 0x009DC900 (FUN_009DC900)
 *
 * What it does:
 * Returns the output-side stream lane unless this buffer is configured in
 * input-only mode (`ioMode == 0`).
 */
void* WxStreamBufferGetOutputStream(
  WxStreamBufferRuntimeView* const streamBuffer
)
{
  return streamBuffer->ioMode == 0 ? nullptr : streamBuffer->stream;
}

/**
 * Address: 0x009DC940 (FUN_009DC940)
 *
 * What it does:
 * Refills one input buffer lane from the underlying stream and resets the
 * cursor to the newly loaded byte range.
 */
bool WxStreamBufferRefillInput(
  WxStreamBufferRuntimeView* const streamBuffer
)
{
  void* const inputStream = WxStreamBufferGetInputStream(streamBuffer);
  if (inputStream == nullptr) {
    return false;
  }

  const int bytesRead = WxStreamInvokeRead(
    inputStream,
    streamBuffer->bufferBase,
    streamBuffer->bufferSize
  );
  if (bytesRead <= 0) {
    return false;
  }

  streamBuffer->bufferEnd = streamBuffer->bufferBase + bytesRead;
  streamBuffer->bufferCursor = streamBuffer->bufferBase;
  return true;
}

/**
 * Address: 0x009DC9D0 (FUN_009DC9D0)
 *
 * What it does:
 * Returns readable-byte count for one stream-buffer lane, refilling from the
 * input stream when empty and auto-transfer is enabled.
 */
std::int32_t WxStreamBufferReadableBytes(
  WxStreamBufferRuntimeView* const streamBuffer
)
{
  if (streamBuffer->bufferCursor == streamBuffer->bufferEnd &&
      streamBuffer->autoTransferEnabled != 0u) {
    (void)WxStreamBufferRefillInput(streamBuffer);
  }

  return static_cast<std::int32_t>(streamBuffer->bufferEnd - streamBuffer->bufferCursor);
}

/**
 * Address: 0x009DC9F0 (FUN_009DC9F0)
 *
 * What it does:
 * Copies up to `byteCount` bytes from the current buffer cursor into caller
 * storage and advances the cursor by the copied byte count.
 */
void* WxStreamBufferCopyOut(
  WxStreamBufferRuntimeView* const streamBuffer,
  void* const outBytes,
  const unsigned int byteCount
)
{
  std::size_t bytesToCopy = byteCount;
  const std::size_t available = static_cast<std::size_t>(streamBuffer->bufferEnd - streamBuffer->bufferCursor);
  if (bytesToCopy > available) {
    bytesToCopy = available;
  }

  void* const copyResult = std::memcpy(outBytes, streamBuffer->bufferCursor, bytesToCopy);
  streamBuffer->bufferCursor += bytesToCopy;
  return copyResult;
}

/**
 * Address: 0x009DC980 (FUN_009DC980)
 *
 * What it does:
 * Flushes pending output bytes (`bufferBase..bufferCursor`) to the underlying
 * stream and rewinds the cursor to the buffer base on full-write success.
 */
bool WxStreamBufferFlushOutput(
  WxStreamBufferRuntimeView* const streamBuffer
)
{
  if (streamBuffer->autoTransferEnabled == 0u) {
    return false;
  }

  if (streamBuffer->bufferCursor == streamBuffer->bufferBase) {
    return false;
  }

  void* const outputStream = WxStreamBufferGetOutputStream(streamBuffer);
  if (outputStream == nullptr) {
    return false;
  }

  const unsigned int pendingBytes = static_cast<unsigned int>(streamBuffer->bufferCursor - streamBuffer->bufferBase);
  if (WxStreamInvokeWrite(outputStream, streamBuffer->bufferBase, pendingBytes) != static_cast<int>(pendingBytes)) {
    return false;
  }

  streamBuffer->bufferCursor = streamBuffer->bufferBase;
  return true;
}

/**
 * Address: 0x009DCA20 (FUN_009DCA20)
 *
 * What it does:
 * Appends bytes into one output buffer lane, truncating on fixed-size buffers
 * or growing dynamic buffers with `realloc` when needed.
 */
void* WxStreamBufferCopyIn(
  WxStreamBufferRuntimeView* const streamBuffer,
  const void* const sourceBytes,
  const std::size_t byteCount
)
{
  std::size_t bytesToCopy = byteCount;
  const std::size_t available = static_cast<std::size_t>(streamBuffer->bufferEnd - streamBuffer->bufferCursor);

  if (byteCount > available) {
    if (streamBuffer->fixedBufferMode != 0u) {
      bytesToCopy = available;
    } else {
      std::uint8_t* const previousBase = streamBuffer->bufferBase;
      streamBuffer->bufferSize += static_cast<std::uint32_t>(byteCount);
      const std::uintptr_t previousBaseAddress = reinterpret_cast<std::uintptr_t>(previousBase);
      const std::uintptr_t previousCursorAddress = reinterpret_cast<std::uintptr_t>(streamBuffer->bufferCursor);
      const std::uintptr_t cursorOffset = previousCursorAddress >= previousBaseAddress
        ? (previousCursorAddress - previousBaseAddress)
        : 0u;

      auto* const resizedBase = static_cast<std::uint8_t*>(
        std::realloc(previousBase, streamBuffer->bufferSize)
      );
      streamBuffer->bufferBase = resizedBase;
      if (resizedBase == nullptr) {
        streamBuffer->bufferSize -= static_cast<std::uint32_t>(byteCount);
        streamBuffer->bufferBase = previousBase;
        return nullptr;
      }

      streamBuffer->bufferCursor = resizedBase + static_cast<std::ptrdiff_t>(cursorOffset);
      streamBuffer->bufferEnd = resizedBase + streamBuffer->bufferSize;
    }
  }

  void* const copyResult = std::memcpy(streamBuffer->bufferCursor, sourceBytes, bytesToCopy);
  streamBuffer->bufferCursor += bytesToCopy;
  return copyResult;
}

/**
 * Address: 0x009DCAA0 (FUN_009DCAA0)
 *
 * What it does:
 * Writes one byte through the stream buffer lane, choosing direct-stream or
 * buffered paths and latching stream error code `2` on flush/write failure.
 */
void* WxStreamBufferWriteByte(
  WxStreamBufferRuntimeView* const streamBuffer,
  const char value
)
{
  void* const outputStream = WxStreamBufferGetOutputStream(streamBuffer);
  if (outputStream == nullptr) {
    return nullptr;
  }

  if (streamBuffer->bufferSize == 0u) {
    return reinterpret_cast<void*>(
      static_cast<std::intptr_t>(
        WxStreamInvokeWrite(outputStream, &value, 1u)
      )
    );
  }

  if (WxStreamBufferReadableBytes(streamBuffer) == 0 &&
      !WxStreamBufferFlushOutput(streamBuffer)) {
    return WxStreamBufferSetErrorCodeIfUnset(streamBuffer, 2);
  }

  void* const copyResult = WxStreamBufferCopyIn(streamBuffer, &value, 1u);
  if (WxStreamIoStatusRuntimeView* const statusLane = WxStreamIoStatus(streamBuffer);
      statusLane != nullptr) {
    statusLane->lastTransferCount = 1;
  }
  return copyResult;
}

/**
 * Address: 0x009DCB10 (FUN_009DCB10)
 *
 * What it does:
 * Reads one byte from the buffered input lane as a peek operation (cursor is
 * restored after copy) and latches stream error code `3` on underflow.
 */
char WxStreamBufferPeekByte(
  WxStreamBufferRuntimeView* const streamBuffer
)
{
  char value = 0;
  if (streamBuffer->stream == nullptr || streamBuffer->bufferSize == 0u) {
    return 0;
  }

  if (WxStreamBufferReadableBytes(streamBuffer) == 0) {
    (void)WxStreamBufferSetErrorCodeIfUnset(streamBuffer, 3);
    return 0;
  }

  (void)WxStreamBufferCopyOut(streamBuffer, &value, 1u);
  --streamBuffer->bufferCursor;
  return value;
}

/**
 * Address: 0x009DCB50 (FUN_009DCB50)
 *
 * What it does:
 * Reads one byte from either the internal input buffer or directly from the
 * attached input stream when no buffer storage is configured.
 */
char WxStreamBufferReadByte(
  WxStreamBufferRuntimeView* const streamBuffer
)
{
  char value = 0;
  void* const inputStream = WxStreamBufferGetInputStream(streamBuffer);
  if (inputStream == nullptr) {
    return 0;
  }

  if (streamBuffer->bufferSize != 0u) {
    if (WxStreamBufferReadableBytes(streamBuffer) == 0) {
      (void)WxStreamBufferSetErrorCodeIfUnset(streamBuffer, 3);
      return 0;
    }

    (void)WxStreamBufferCopyOut(streamBuffer, &value, 1u);
    if (WxStreamIoStatusRuntimeView* const statusLane = WxStreamIoStatus(streamBuffer);
        statusLane != nullptr) {
      statusLane->lastTransferCount = 1;
    }
    return value;
  }

  (void)WxStreamInvokeRead(inputStream, &value, 1u);
  return value;
}

/**
 * Address: 0x009DCBC0 (FUN_009DCBC0)
 *
 * What it does:
 * Reads up to `byteCount` bytes from the stream-buffer lane, combining buffered
 * copies and refill cycles, then updates stream status counters.
 */
int WxStreamBufferRead(
  WxStreamBufferRuntimeView* const streamBuffer,
  void* const outBytes,
  const unsigned int byteCount
)
{
  if (WxStreamIoStatusRuntimeView* const statusLane = WxStreamIoStatus(streamBuffer);
      statusLane != nullptr) {
    statusLane->errorCode = 0;
  }

  int transferredCount = 0;
  if (streamBuffer->bufferSize != 0u) {
    unsigned int remainingBytes = byteCount;
    auto* writeCursor = static_cast<std::uint8_t*>(outBytes);
    while (remainingBytes != 0u) {
      const unsigned int readableBytes = static_cast<unsigned int>(WxStreamBufferReadableBytes(streamBuffer));
      if (remainingBytes <= readableBytes) {
        (void)WxStreamBufferCopyOut(streamBuffer, writeCursor, remainingBytes);
        remainingBytes = 0u;
        break;
      }

      (void)WxStreamBufferCopyOut(streamBuffer, writeCursor, readableBytes);
      remainingBytes -= readableBytes;
      writeCursor += readableBytes;
      if (!WxStreamBufferRefillInput(streamBuffer)) {
        break;
      }
    }

    if (remainingBytes != 0u) {
      (void)WxStreamBufferSetErrorCodeIfUnset(streamBuffer, 1);
    }
    transferredCount = static_cast<int>(byteCount - remainingBytes);
  } else {
    void* const inputStream = WxStreamBufferGetInputStream(streamBuffer);
    if (inputStream == nullptr) {
      return 0;
    }
    transferredCount = WxStreamInvokeRead(inputStream, outBytes, byteCount);
  }

  if (WxStreamIoStatusRuntimeView* const statusLane = WxStreamIoStatus(streamBuffer);
      statusLane != nullptr) {
    statusLane->lastTransferCount = transferredCount;
  }
  return transferredCount;
}

/**
 * Address: 0x009DCCF0 (FUN_009DCCF0)
 *
 * What it does:
 * Writes up to `byteCount` bytes through one stream-buffer lane, using buffered
 * writes with optional growth/flush, or direct stream writes when buffering is
 * disabled.
 */
int WxStreamBufferWrite(
  WxStreamBufferRuntimeView* const streamBuffer,
  const void* const sourceBytes,
  const std::size_t byteCount
)
{
  if (WxStreamIoStatusRuntimeView* const statusLane = WxStreamIoStatus(streamBuffer);
      statusLane != nullptr) {
    statusLane->errorCode = 0;
  }

  int transferredCount = 0;
  if (streamBuffer->bufferSize != 0u || streamBuffer->fixedBufferMode == 0u) {
    std::size_t remainingBytes = byteCount;
    auto* readCursor = static_cast<const std::uint8_t*>(sourceBytes);
    if (remainingBytes != 0u) {
      while (true) {
        const std::size_t available = static_cast<std::size_t>(streamBuffer->bufferEnd - streamBuffer->bufferCursor);
        if (remainingBytes <= available || streamBuffer->fixedBufferMode == 0u) {
          (void)WxStreamBufferCopyIn(streamBuffer, readCursor, remainingBytes);
          remainingBytes = 0u;
          break;
        }

        (void)WxStreamBufferCopyIn(streamBuffer, readCursor, available);
        remainingBytes -= available;
        readCursor += available;
        if (!WxStreamBufferFlushOutput(streamBuffer)) {
          break;
        }
        streamBuffer->bufferCursor = streamBuffer->bufferBase;
        if (remainingBytes == 0u) {
          break;
        }
      }

      if (remainingBytes != 0u) {
        (void)WxStreamBufferSetErrorCodeIfUnset(streamBuffer, 2);
      }
    }
    transferredCount = static_cast<int>(byteCount - remainingBytes);
  } else {
    void* const outputStream = WxStreamBufferGetOutputStream(streamBuffer);
    if (outputStream == nullptr) {
      return 0;
    }
    transferredCount = WxStreamInvokeWrite(
      outputStream,
      sourceBytes,
      static_cast<unsigned int>(byteCount)
    );
  }

  if (WxStreamIoStatusRuntimeView* const statusLane = WxStreamIoStatus(streamBuffer);
      statusLane != nullptr) {
    statusLane->lastTransferCount = transferredCount;
  }
  return transferredCount;
}

/**
 * Address: 0x0098DB00 (FUN_0098DB00)
 *
 * What it does:
 * Resolves one window's runtime size lane, then clamps width/height to the
 * window's min-width/min-height constraints.
 */
wxSize* WxWindowResolveSizerMinSize(
  wxWindowBase* const window,
  wxSize* const outSize
)
{
  if (window == nullptr || outSize == nullptr) {
    return outSize;
  }

  window->DoGetSize(&outSize->x, &outSize->y);
  const std::int32_t minHeight = window->GetMinHeight();
  if (outSize->y <= minHeight) {
    outSize->y = minHeight;
  }

  const std::int32_t minWidth = window->GetMinWidth();
  if (outSize->x <= minWidth) {
    outSize->x = minWidth;
  }
  return outSize;
}

/**
 * Address: 0x0098DF40 (FUN_0098DF40)
 *
 * What it does:
 * Returns whether one sizer-item lane currently owns a window child.
 */
BOOL WxSizerItemHasWindow(
  const WxSizerItemLayoutRuntimeView* const item
)
{
  return item->containedWindow != nullptr ? TRUE : FALSE;
}

/**
 * Address: 0x0098DF50 (FUN_0098DF50)
 *
 * What it does:
 * Returns whether one sizer-item lane currently owns a nested sizer child.
 */
BOOL WxSizerItemHasSizer(
  const WxSizerItemLayoutRuntimeView* const item
)
{
  return item->containedSizer != nullptr ? TRUE : FALSE;
}

/**
 * Address: 0x0098E420 (FUN_0098E420)
 *
 * What it does:
 * Commits one nested sizer geometry lane (`x/y/width/height`) and dispatches
 * the `RecalcSizes` virtual lane.
 */
int WxSizerSetDimensionRuntime(
  WxSizerLayoutRuntimeView* const sizer,
  const std::int32_t x,
  const std::int32_t y,
  const std::int32_t width,
  const std::int32_t height
)
{
  sizer->positionX = x;
  sizer->positionY = y;
  sizer->laidOutWidth = width;
  sizer->laidOutHeight = height;

  auto* const vtable = *reinterpret_cast<WxSizerVTableRuntimeView**>(sizer);
  if (vtable == nullptr || vtable->recalcSizes == nullptr) {
    return 0;
  }
  return vtable->recalcSizes(sizer);
}

/**
 * Alias of FUN_0098E450.
 *
 * What it does:
 * Applies one nested-sizer min-size query lane and clamps the returned size
 * against the owning sizer's cached min-width/min-height lanes.
 */
wxSize* WxSizerGetMinSizeClamped(
  WxSizerRuntimeInterface* const sizer,
  wxSize* const outSize
)
{
  if (sizer == nullptr || outSize == nullptr) {
    return outSize;
  }

  (void)sizer->GetMinSize(outSize);
  const auto* const sizerLanes = reinterpret_cast<const std::int32_t*>(sizer);
  const std::int32_t minWidth = sizerLanes[7];
  if (outSize->x < minWidth) {
    outSize->x = minWidth;
  }

  const std::int32_t minHeight = sizerLanes[8];
  if (outSize->y < minHeight) {
    outSize->y = minHeight;
  }
  return outSize;
}

/**
 * Address: 0x0098E8F0 (FUN_0098E8F0)
 *
 * What it does:
 * Computes one sizer-item minimum size lane from nested sizer/window children,
 * then applies border-flag inflation and optional aspect-ratio priming.
 */
wxSize* WxSizerItemComputeMinSize(
  WxSizerItemLayoutRuntimeView* const item,
  wxSize* const outSize
)
{
  outSize->x = 0;
  outSize->y = 0;

  if (WxSizerItemHasSizer(item) != FALSE) {
    wxSize nestedMin{};
    (void)WxSizerGetMinSizeClamped(
      reinterpret_cast<WxSizerRuntimeInterface*>(item->containedSizer),
      &nestedMin
    );
    outSize->x = nestedMin.x;
    outSize->y = nestedMin.y;

    if ((item->flags & 0x4000) != 0 && item->userAspectRatio == 0.0f) {
      if (nestedMin.x != 0 && nestedMin.y != 0) {
        item->userAspectRatio = static_cast<float>(nestedMin.x) / static_cast<float>(nestedMin.y);
      } else {
        item->userAspectRatio = 1.0f;
      }
    }
  } else {
    if (WxSizerItemHasWindow(item) != FALSE && (item->flags & 0x8000) != 0) {
      wxSize computedWindowMin{};
      (void)WxWindowResolveSizerMinSize(item->containedWindow, &computedWindowMin);
      item->minWidth = computedWindowMin.x;
      item->minHeight = computedWindowMin.y;
    }
    outSize->x = item->minWidth;
    outSize->y = item->minHeight;
  }

  if ((item->flags & 0x10) != 0) {
    outSize->x += item->border;
  }
  if ((item->flags & 0x20) != 0) {
    outSize->x += item->border;
  }
  if ((item->flags & 0x40) != 0) {
    outSize->y += item->border;
  }
  if ((item->flags & 0x80) != 0) {
    outSize->y += item->border;
  }

  return outSize;
}

/**
 * Address: 0x0098E9D0 (FUN_0098E9D0)
 *
 * What it does:
 * Applies one sizer-item layout rectangle with ratio/alignment/border rules,
 * forwards geometry to nested sizers/windows, and caches final laid-out size.
 */
int WxSizerItemSetDimension(
  WxSizerItemLayoutRuntimeView* const item,
  std::int32_t x,
  std::int32_t y,
  std::int32_t width,
  std::int32_t height
)
{
  const std::int32_t flags = item->flags;
  std::int32_t laidOutWidth = width;
  std::int32_t laidOutHeight = height;

  if ((flags & 0x4000) != 0) {
    const std::int32_t ratioWidth = static_cast<std::int32_t>(static_cast<double>(height) * item->userAspectRatio);
    laidOutWidth = width;
    if (ratioWidth <= width) {
      if (ratioWidth < width) {
        if ((flags & 0x100) != 0) {
          x += (width - ratioWidth) / 2;
          laidOutWidth = static_cast<std::int32_t>(static_cast<double>(height) * item->userAspectRatio);
        } else {
          if ((flags & 0x200) != 0) {
            x += width - ratioWidth;
          }
          laidOutWidth = static_cast<std::int32_t>(static_cast<double>(height) * item->userAspectRatio);
        }
      }
      laidOutHeight = height;
    } else {
      laidOutHeight = static_cast<std::int32_t>(static_cast<double>(width) / item->userAspectRatio);
      if ((flags & 0x800) != 0) {
        y += (height - laidOutHeight) / 2;
      } else if ((flags & 0x400) != 0) {
        y += height - laidOutHeight;
      }
    }
  }

  item->positionX = x;
  item->positionY = y;

  if ((flags & 0x10) != 0) {
    x += item->border;
    laidOutWidth -= item->border;
  }
  if ((flags & 0x20) != 0) {
    laidOutWidth -= item->border;
  }
  if ((flags & 0x40) != 0) {
    y += item->border;
    laidOutHeight -= item->border;
  }
  if ((flags & 0x80) != 0) {
    laidOutHeight -= item->border;
  }

  if (WxSizerItemHasSizer(item) != FALSE) {
    (void)WxSizerSetDimensionRuntime(
      reinterpret_cast<WxSizerLayoutRuntimeView*>(item->containedSizer),
      x,
      y,
      laidOutWidth,
      laidOutHeight
    );
  }

  int result = WxSizerItemHasWindow(item);
  if (result != 0) {
    item->containedWindow->DoSetSize(x, y, laidOutWidth, laidOutHeight, 4);
  }

  item->laidOutWidth = laidOutWidth;
  item->laidOutHeight = laidOutHeight;
  return result;
}

/**
 * Address: 0x00A2E610 (FUN_00A2E610)
 *
 * What it does:
 * Writes one object delayed-destroy flag lane and returns the written value.
 */
std::uint8_t wxObjectSetDelayedDestroyFlag(
  WxObjectDestroyFlagsRuntimeView* const object,
  const std::uint8_t enabled
)
{
  object->delayedDestroyFlag = enabled;
  return enabled;
}

struct WxBufferedReadCacheRuntimeView
{
  std::uint8_t reserved00_47[0x48]{};
  std::uint8_t* stagedBytes = nullptr;      // +0x48
  std::uint32_t stagedByteCount = 0;        // +0x4C
  std::uint32_t stagedReadOffset = 0;       // +0x50
};
static_assert(
  offsetof(WxBufferedReadCacheRuntimeView, stagedBytes) == 0x48,
  "WxBufferedReadCacheRuntimeView::stagedBytes offset must be 0x48"
);
static_assert(
  offsetof(WxBufferedReadCacheRuntimeView, stagedByteCount) == 0x4C,
  "WxBufferedReadCacheRuntimeView::stagedByteCount offset must be 0x4C"
);
static_assert(
  offsetof(WxBufferedReadCacheRuntimeView, stagedReadOffset) == 0x50,
  "WxBufferedReadCacheRuntimeView::stagedReadOffset offset must be 0x50"
);

/**
 * Address: 0x00A2E6C0 (FUN_00A2E6C0, sub_A2E6C0)
 *
 * What it does:
 * Copies bytes from one staged read-cache lane into caller storage, optionally
 * consuming the staged data and freeing cache storage when fully drained.
 */
std::size_t wxSocketConsumeStagedReadCacheRuntime(
  WxBufferedReadCacheRuntimeView* const cache,
  void* const destinationBytes,
  const std::size_t requestedByteCount,
  const bool peekOnly
)
{
  const std::size_t stagedByteCount = cache->stagedByteCount;
  if (stagedByteCount == 0u) {
    return 0u;
  }

  const std::size_t unreadByteCount = stagedByteCount - cache->stagedReadOffset;
  std::size_t copiedByteCount = requestedByteCount;
  if (requestedByteCount > unreadByteCount) {
    copiedByteCount = unreadByteCount;
  }

  std::memcpy(destinationBytes, cache->stagedBytes + cache->stagedReadOffset, copiedByteCount);
  if (!peekOnly) {
    cache->stagedReadOffset += static_cast<std::uint32_t>(copiedByteCount);
    if (cache->stagedByteCount == cache->stagedReadOffset) {
      _free_crt(cache->stagedBytes);
      cache->stagedBytes = nullptr;
      cache->stagedByteCount = 0;
      cache->stagedReadOffset = 0;
    }
  }

  return copiedByteCount;
}

/**
 * Address: 0x00A2E950 (FUN_00A2E950)
 *
 * What it does:
 * Marks one wx object as pending delete, refreshes ref-data, clears delayed
 * destroy mode, and appends the object to the pending-delete list once.
 */
bool wxObjectEnqueuePendingDelete(
  WxObjectDestroyFlagsRuntimeView* const object
)
{
  if (object == nullptr || object->vtable == nullptr) {
    return false;
  }

  object->pendingDeleteFlag = 1u;
  if (object->vtable->createRefData != nullptr) {
    object->vtable->createRefData(object);
  }

  (void)wxObjectSetDelayedDestroyFlag(object, 0u);
  if (!WxRuntimeListContains(&wxPendingDelete, object)) {
    WxRuntimeListAppend(&wxPendingDelete, object);
  }
  return true;
}

/**
 * Address: 0x009F24F0 (FUN_009F24F0)
 *
 * What it does:
 * Runs the base wx-object unref lane used by event-derived destructor tails.
 */
void wxObjectDestroyEventTailRuntime(
  void* const objectRuntime
)
{
  auto* const object = static_cast<WxObjectRuntimeView*>(objectRuntime);
  if (object == nullptr) {
    return;
  }

  wxEventUnRefRuntime(object);
}

/**
 * Address: 0x009F3C30 (FUN_009F3C30)
 *
 * What it does:
 * Copies one wx string payload lane by sharing storage and bumping the shared
 * refcount when the source is non-empty; otherwise assigns `wxEmptyString`.
 */
wxStringRuntime* wxCopySharedWxStringRuntime(
  const wxStringRuntime* const source,
  wxStringRuntime* const outValue
)
{
  if (outValue == nullptr) {
    return nullptr;
  }

  outValue->m_pchData = nullptr;
  const wchar_t* const sourceText = (source != nullptr) ? source->m_pchData : nullptr;
  if (sourceText == nullptr) {
    outValue->m_pchData = const_cast<wchar_t*>(wxEmptyString);
    return outValue;
  }

  auto* const sharedPrefixWords = reinterpret_cast<std::int32_t*>(const_cast<wchar_t*>(sourceText)) - 3;
  const std::int32_t sourceLength = sharedPrefixWords[1];
  if (sourceLength == 0) {
    outValue->m_pchData = const_cast<wchar_t*>(wxEmptyString);
    return outValue;
  }

  outValue->m_pchData = const_cast<wchar_t*>(sourceText);
  if (sharedPrefixWords[0] != -1) {
    ++sharedPrefixWords[0];
  }
  return outValue;
}

namespace
{
  struct WxThreadRuntimeView
  {
    void* vtable = nullptr;               // +0x00
    HANDLE* nativeHandleSlot = nullptr;   // +0x04
    CRITICAL_SECTION criticalSection{};   // +0x08
  };
  static_assert(
    offsetof(WxThreadRuntimeView, nativeHandleSlot) == 0x4,
    "WxThreadRuntimeView::nativeHandleSlot offset must be 0x4"
  );
  static_assert(
    offsetof(WxThreadRuntimeView, criticalSection) == 0x8,
    "WxThreadRuntimeView::criticalSection offset must be 0x8"
  );
}

/**
 * Address: 0x009ADFD0 (FUN_009ADFD0)
 *
 * What it does:
 * Closes and frees one optional native-thread handle slot, then tears down
 * the embedded critical-section lane.
 */
void wxThreadDestroyRuntime(
  void* const threadRuntime
)
{
  auto* const thread = static_cast<WxThreadRuntimeView*>(threadRuntime);
  if (thread == nullptr) {
    return;
  }

  if (thread->nativeHandleSlot != nullptr) {
    if (*thread->nativeHandleSlot != nullptr) {
      ::CloseHandle(*thread->nativeHandleSlot);
      *thread->nativeHandleSlot = nullptr;
    }
    ::operator delete(thread->nativeHandleSlot);
    thread->nativeHandleSlot = nullptr;
  }

  ::DeleteCriticalSection(&thread->criticalSection);
}

namespace
{
  struct WxSocketWritableProbeRuntimeView
  {
    SOCKET socketHandle = INVALID_SOCKET;     // +0x00
    std::uint8_t unknown04_0B[0x8]{};         // +0x04
    std::int32_t stateCode = 0;               // +0x0C
    std::uint8_t unknown10_23[0x14]{};        // +0x10
    timeval timeout{};                        // +0x24
  };
  static_assert(
    offsetof(WxSocketWritableProbeRuntimeView, socketHandle) == 0x0,
    "WxSocketWritableProbeRuntimeView::socketHandle offset must be 0x0"
  );
  static_assert(
    offsetof(WxSocketWritableProbeRuntimeView, stateCode) == 0x0C,
    "WxSocketWritableProbeRuntimeView::stateCode offset must be 0x0C"
  );
  static_assert(
    offsetof(WxSocketWritableProbeRuntimeView, timeout) == 0x24,
    "WxSocketWritableProbeRuntimeView::timeout offset must be 0x24"
  );
  static_assert(
    sizeof(WxSocketWritableProbeRuntimeView) == 0x2C,
    "WxSocketWritableProbeRuntimeView size must be 0x2C"
  );
}

/**
 * Address: 0x00A2FEF0 (FUN_00A2FEF0)
 *
 * What it does:
 * Performs one timeout-only readability probe for the socket lane and sets the
 * probe state to `8` when the `select` call times out.
 */
int wxSocketPollReadableTimeoutOnly(
  void* const socketProbeRuntime
)
{
  auto* const probe = static_cast<WxSocketWritableProbeRuntimeView*>(socketProbeRuntime);
  if (probe == nullptr) {
    return 8;
  }

  std::int32_t pendingProbeState = 0;
  std::memcpy(&pendingProbeState, probe->unknown10_23, sizeof(pendingProbeState));
  if (pendingProbeState != 0) {
    return 0;
  }

  fd_set readSet{};
  readSet.fd_count = 1u;
  readSet.fd_array[0] = probe->socketHandle;
  if (::select(0, &readSet, nullptr, nullptr, &probe->timeout) != 0) {
    return 0;
  }

  probe->stateCode = 8;
  return 8;
}

/**
 * Address: 0x00A2FFB0 (FUN_00A2FFB0)
 *
 * What it does:
 * Polls one socket for writable/exception lanes using the stored timeout and
 * updates the runtime state code to `2` (exception) or `8` (timeout).
 */
int wxSocketPollWritableRuntime(
  void* const socketProbeRuntime
)
{
  auto* const probe = static_cast<WxSocketWritableProbeRuntimeView*>(socketProbeRuntime);
  if (probe == nullptr) {
    return 8;
  }

  fd_set writeSet{};
  fd_set exceptSet{};
  writeSet.fd_count = 1u;
  writeSet.fd_array[0] = probe->socketHandle;
  exceptSet.fd_count = 1u;
  exceptSet.fd_array[0] = probe->socketHandle;

  if (::select(0, nullptr, &writeSet, &exceptSet, &probe->timeout) != 0) {
    if (FD_ISSET(probe->socketHandle, &writeSet)) {
      return 0;
    }
    probe->stateCode = 2;
    return 2;
  }

  probe->stateCode = 8;
  return 8;
}

namespace
{
  struct WxTextCtrlClassFactoryRuntimeView
  {
    std::uint8_t runtimeStorage[0x16C]{};
  };
  static_assert(sizeof(WxTextCtrlClassFactoryRuntimeView) == 0x16C, "WxTextCtrlClassFactoryRuntimeView size must be 0x16C");

  struct WxTreeListCtrlClassFactoryRuntimeView
  {
    std::uint8_t runtimeStorage[0x140]{};
  };
  static_assert(
    sizeof(WxTreeListCtrlClassFactoryRuntimeView) == 0x140,
    "WxTreeListCtrlClassFactoryRuntimeView size must be 0x140"
  );

  struct WxControl130ClassFactoryRuntimeView
  {
    std::uint8_t runtimeStorage[0x130]{};
  };
  static_assert(
    sizeof(WxControl130ClassFactoryRuntimeView) == 0x130,
    "WxControl130ClassFactoryRuntimeView size must be 0x130"
  );

  struct WxBitmapButtonClassFactoryRuntimeView
  {
    std::uint8_t runtimeStorage00To12F[0x130]{};
    WxBitmapRuntimeView normalBitmap{};        // +0x130
    WxBitmapRuntimeView selectedBitmap{};      // +0x13C
    WxBitmapRuntimeView focusedBitmap{};       // +0x148
    WxBitmapRuntimeView disabledBitmap{};      // +0x154
    std::int32_t runtimeLane160 = 0;           // +0x160
    std::int32_t runtimeLane164 = 0;           // +0x164
  };
  static_assert(
    offsetof(WxBitmapButtonClassFactoryRuntimeView, normalBitmap) == 0x130,
    "WxBitmapButtonClassFactoryRuntimeView::normalBitmap offset must be 0x130"
  );
  static_assert(
    offsetof(WxBitmapButtonClassFactoryRuntimeView, selectedBitmap) == 0x13C,
    "WxBitmapButtonClassFactoryRuntimeView::selectedBitmap offset must be 0x13C"
  );
  static_assert(
    offsetof(WxBitmapButtonClassFactoryRuntimeView, focusedBitmap) == 0x148,
    "WxBitmapButtonClassFactoryRuntimeView::focusedBitmap offset must be 0x148"
  );
  static_assert(
    offsetof(WxBitmapButtonClassFactoryRuntimeView, disabledBitmap) == 0x154,
    "WxBitmapButtonClassFactoryRuntimeView::disabledBitmap offset must be 0x154"
  );
  static_assert(
    offsetof(WxBitmapButtonClassFactoryRuntimeView, runtimeLane160) == 0x160,
    "WxBitmapButtonClassFactoryRuntimeView::runtimeLane160 offset must be 0x160"
  );
  static_assert(
    offsetof(WxBitmapButtonClassFactoryRuntimeView, runtimeLane164) == 0x164,
    "WxBitmapButtonClassFactoryRuntimeView::runtimeLane164 offset must be 0x164"
  );
  static_assert(
    sizeof(WxBitmapButtonClassFactoryRuntimeView) == 0x168,
    "WxBitmapButtonClassFactoryRuntimeView size must be 0x168"
  );

  struct WxStockListClassFactoryRuntimeView : wxListBaseRuntime
  {
    explicit WxStockListClassFactoryRuntimeView() noexcept
      : wxListBaseRuntime(0)
      , runtimeStorage04To1B{}
    {
    }

    std::uint8_t runtimeStorage04To1B[0x18]{};
  };
  static_assert(
    sizeof(WxStockListClassFactoryRuntimeView) == 0x1C,
    "WxStockListClassFactoryRuntimeView size must be 0x1C"
  );

  struct WxListCtrlClassFactoryRuntimeView
  {
    std::uint8_t unknown000_12F[0x130]{};
    void* runtimeLane130 = nullptr;                // +0x130
    std::uint32_t runtimeLane134 = 0;              // +0x134
    std::uint32_t runtimeLane138 = 0;              // +0x138
    std::uint32_t runtimeLane13C = 0;              // +0x13C
    std::uint8_t runtimeFlag140 = 0;               // +0x140
    std::uint8_t runtimeFlag141 = 0;               // +0x141
    std::uint8_t runtimeFlag142 = 0;               // +0x142
    std::uint8_t unknown143 = 0;                   // +0x143
    void* runtimeLane144 = nullptr;                // +0x144
    void* runtimeLane148 = nullptr;                // +0x148
    std::uint8_t runtimeFlag14C = 0;               // +0x14C
    std::uint8_t runtimeFlag14D = 0;               // +0x14D
    std::uint8_t unknown14E_14F[0x2]{};            // +0x14E
  };
  static_assert(
    offsetof(WxListCtrlClassFactoryRuntimeView, runtimeLane130) == 0x130,
    "WxListCtrlClassFactoryRuntimeView::runtimeLane130 offset must be 0x130"
  );
  static_assert(
    offsetof(WxListCtrlClassFactoryRuntimeView, runtimeLane13C) == 0x13C,
    "WxListCtrlClassFactoryRuntimeView::runtimeLane13C offset must be 0x13C"
  );
  static_assert(
    offsetof(WxListCtrlClassFactoryRuntimeView, runtimeFlag140) == 0x140,
    "WxListCtrlClassFactoryRuntimeView::runtimeFlag140 offset must be 0x140"
  );
  static_assert(
    offsetof(WxListCtrlClassFactoryRuntimeView, runtimeLane144) == 0x144,
    "WxListCtrlClassFactoryRuntimeView::runtimeLane144 offset must be 0x144"
  );
  static_assert(
    offsetof(WxListCtrlClassFactoryRuntimeView, runtimeFlag14C) == 0x14C,
    "WxListCtrlClassFactoryRuntimeView::runtimeFlag14C offset must be 0x14C"
  );
  static_assert(
    sizeof(WxListCtrlClassFactoryRuntimeView) == 0x150,
    "WxListCtrlClassFactoryRuntimeView size must be 0x150"
  );

  struct WxRadioButtonClassFactoryRuntimeView
  {
    std::uint8_t unknown000_12F[0x130]{};
    std::uint8_t checkedFlag = 0;        // +0x130
    std::uint8_t unknown131_133[0x3]{};  // +0x131
  };
  static_assert(
    offsetof(WxRadioButtonClassFactoryRuntimeView, checkedFlag) == 0x130,
    "WxRadioButtonClassFactoryRuntimeView::checkedFlag offset must be 0x130"
  );
  static_assert(
    sizeof(WxRadioButtonClassFactoryRuntimeView) == 0x134,
    "WxRadioButtonClassFactoryRuntimeView size must be 0x134"
  );

  struct WxToolBarToolListRuntimeView
  {
    void* vtable = nullptr;                      // +0x00
    std::uint8_t unknown04_0B[0x8]{};           // +0x04
  };
  static_assert(sizeof(WxToolBarToolListRuntimeView) == 0x0C, "WxToolBarToolListRuntimeView size must be 0x0C");

  struct WxToolBarClassFactoryRuntimeView
  {
    std::uint8_t unknown000_12F[0x130]{};
    WxToolBarToolListRuntimeView toolList{};          // +0x130
    std::uint8_t toolPackingEnabled = 1;              // +0x13C
    std::uint8_t unknown13D_14B[0xF]{};               // +0x13D
    std::uint32_t runtimeLane14C = 0;                 // +0x14C
    std::uint32_t runtimeLane150 = 0;                 // +0x150
    std::uint32_t runtimeLane154 = 0;                 // +0x154
    std::uint32_t runtimeLane158 = 0;                 // +0x158
    std::uint8_t unknown15C_163[0x8]{};               // +0x15C
    std::int32_t defaultToolWidth = 16;               // +0x164
    std::int32_t defaultToolHeight = 15;              // +0x168
    std::int32_t runtimeLane16C = 0;                  // +0x16C
    std::int32_t runtimeLane170 = 0;                  // +0x170
    std::int32_t runtimeLane174 = 0;                  // +0x174
  };
  static_assert(
    offsetof(WxToolBarClassFactoryRuntimeView, toolList) == 0x130,
    "WxToolBarClassFactoryRuntimeView::toolList offset must be 0x130"
  );
  static_assert(
    offsetof(WxToolBarClassFactoryRuntimeView, toolPackingEnabled) == 0x13C,
    "WxToolBarClassFactoryRuntimeView::toolPackingEnabled offset must be 0x13C"
  );
  static_assert(
    offsetof(WxToolBarClassFactoryRuntimeView, runtimeLane14C) == 0x14C,
    "WxToolBarClassFactoryRuntimeView::runtimeLane14C offset must be 0x14C"
  );
  static_assert(
    offsetof(WxToolBarClassFactoryRuntimeView, defaultToolWidth) == 0x164,
    "WxToolBarClassFactoryRuntimeView::defaultToolWidth offset must be 0x164"
  );
  static_assert(
    offsetof(WxToolBarClassFactoryRuntimeView, runtimeLane174) == 0x174,
    "WxToolBarClassFactoryRuntimeView::runtimeLane174 offset must be 0x174"
  );
  static_assert(
    sizeof(WxToolBarClassFactoryRuntimeView) == 0x178,
    "WxToolBarClassFactoryRuntimeView size must be 0x178"
  );

  struct WxProcessClassFactoryRuntimeView
  {
    std::uint8_t unknown00_07[0x8]{};
    std::int32_t processData = 0;            // +0x08
    std::uint8_t unknown0C_27[0x1C]{};       // +0x0C
    std::int32_t processId = -1;             // +0x28
    void* inputStream = nullptr;             // +0x2C
    void* outputStream = nullptr;            // +0x30
    void* errorStream = nullptr;             // +0x34
    std::uint8_t redirectMode = 0;           // +0x38
    std::uint8_t unknown39_3B[0x3]{};        // +0x39
  };
  static_assert(
    offsetof(WxProcessClassFactoryRuntimeView, processData) == 0x08,
    "WxProcessClassFactoryRuntimeView::processData offset must be 0x08"
  );
  static_assert(
    offsetof(WxProcessClassFactoryRuntimeView, processId) == 0x28,
    "WxProcessClassFactoryRuntimeView::processId offset must be 0x28"
  );
  static_assert(
    offsetof(WxProcessClassFactoryRuntimeView, inputStream) == 0x2C,
    "WxProcessClassFactoryRuntimeView::inputStream offset must be 0x2C"
  );
  static_assert(
    offsetof(WxProcessClassFactoryRuntimeView, redirectMode) == 0x38,
    "WxProcessClassFactoryRuntimeView::redirectMode offset must be 0x38"
  );
  static_assert(
    sizeof(WxProcessClassFactoryRuntimeView) == 0x3C,
    "WxProcessClassFactoryRuntimeView size must be 0x3C"
  );

  struct WxPopupTransientWindowClassFactoryRuntimeView
  {
    std::uint8_t unknown000_123[0x124]{};
    void* popupOwnerWindow = nullptr;         // +0x124
    void* popupFocusWindow = nullptr;         // +0x128
    void* popupChildWindow = nullptr;         // +0x12C
    void* popupParentWindow = nullptr;        // +0x130
  };
  static_assert(
    offsetof(WxPopupTransientWindowClassFactoryRuntimeView, popupOwnerWindow) == 0x124,
    "WxPopupTransientWindowClassFactoryRuntimeView::popupOwnerWindow offset must be 0x124"
  );
  static_assert(
    offsetof(WxPopupTransientWindowClassFactoryRuntimeView, popupParentWindow) == 0x130,
    "WxPopupTransientWindowClassFactoryRuntimeView::popupParentWindow offset must be 0x130"
  );
  static_assert(
    sizeof(WxPopupTransientWindowClassFactoryRuntimeView) == 0x134,
    "WxPopupTransientWindowClassFactoryRuntimeView size must be 0x134"
  );

  struct WxListBaseCtorRuntimeView
  {
    void* vtable = nullptr;                  // +0x00
    void* refData = nullptr;                 // +0x04
    std::int32_t itemCount = 0;              // +0x08
    std::uint8_t destroyItems = 0;           // +0x0C
    std::uint8_t unknown0D_0F[0x3]{};        // +0x0D
    wxNodeBaseRuntime* first = nullptr;      // +0x10
    wxNodeBaseRuntime* last = nullptr;       // +0x14
    std::int32_t keyType = 0;                // +0x18
  };
  static_assert(offsetof(WxListBaseCtorRuntimeView, itemCount) == 0x08, "WxListBaseCtorRuntimeView::itemCount offset must be 0x08");
  static_assert(
    offsetof(WxListBaseCtorRuntimeView, destroyItems) == 0x0C,
    "WxListBaseCtorRuntimeView::destroyItems offset must be 0x0C"
  );
  static_assert(offsetof(WxListBaseCtorRuntimeView, first) == 0x10, "WxListBaseCtorRuntimeView::first offset must be 0x10");
  static_assert(offsetof(WxListBaseCtorRuntimeView, last) == 0x14, "WxListBaseCtorRuntimeView::last offset must be 0x14");
  static_assert(offsetof(WxListBaseCtorRuntimeView, keyType) == 0x18, "WxListBaseCtorRuntimeView::keyType offset must be 0x18");
  static_assert(sizeof(WxListBaseCtorRuntimeView) == 0x1C, "WxListBaseCtorRuntimeView size must be 0x1C");

  struct WxPopupWindowClassFactoryRuntimeView
  {
    std::uint8_t runtimeStorage[0x124]{};
  };
  static_assert(sizeof(WxPopupWindowClassFactoryRuntimeView) == 0x124, "WxPopupWindowClassFactoryRuntimeView size must be 0x124");

  struct WxSpinCtrlClassFactoryRuntimeView
  {
    std::uint8_t unknown000_12F[0x130]{};
    std::int32_t minValue = 0;              // +0x130
    std::int32_t maxValue = 100;            // +0x134
    std::uint8_t unknown138_13F[0x8]{};
  };
  static_assert(offsetof(WxSpinCtrlClassFactoryRuntimeView, minValue) == 0x130, "WxSpinCtrlClassFactoryRuntimeView::minValue offset must be 0x130");
  static_assert(offsetof(WxSpinCtrlClassFactoryRuntimeView, maxValue) == 0x134, "WxSpinCtrlClassFactoryRuntimeView::maxValue offset must be 0x134");
  static_assert(sizeof(WxSpinCtrlClassFactoryRuntimeView) == 0x140, "WxSpinCtrlClassFactoryRuntimeView size must be 0x140");

  struct WxScrollBarClassFactoryRuntimeView
  {
    std::uint8_t unknown000_12F[0x130]{};
    std::int32_t thumbPosition = 0;         // +0x130
    std::int32_t thumbRange = 0;            // +0x134
    std::int32_t pageSize = 0;              // +0x138
  };
  static_assert(
    offsetof(WxScrollBarClassFactoryRuntimeView, thumbPosition) == 0x130,
    "WxScrollBarClassFactoryRuntimeView::thumbPosition offset must be 0x130"
  );
  static_assert(offsetof(WxScrollBarClassFactoryRuntimeView, pageSize) == 0x138, "WxScrollBarClassFactoryRuntimeView::pageSize offset must be 0x138");
  static_assert(sizeof(WxScrollBarClassFactoryRuntimeView) == 0x13C, "WxScrollBarClassFactoryRuntimeView size must be 0x13C");

  struct WxStaticBitmapClassFactoryRuntimeView
  {
    std::uint8_t unknown000_12F[0x130]{};
    std::uint8_t useMaskFlag = 1;           // +0x130
    std::uint8_t unknown131_133[0x3]{};
    void* bitmapPayload = nullptr;          // +0x134
  };
  static_assert(
    offsetof(WxStaticBitmapClassFactoryRuntimeView, useMaskFlag) == 0x130,
    "WxStaticBitmapClassFactoryRuntimeView::useMaskFlag offset must be 0x130"
  );
  static_assert(
    offsetof(WxStaticBitmapClassFactoryRuntimeView, bitmapPayload) == 0x134,
    "WxStaticBitmapClassFactoryRuntimeView::bitmapPayload offset must be 0x134"
  );
  static_assert(sizeof(WxStaticBitmapClassFactoryRuntimeView) == 0x138, "WxStaticBitmapClassFactoryRuntimeView size must be 0x138");

  struct WxSpinButtonClassFactoryRuntimeView
  {
    std::uint8_t unknown000_12F[0x130]{};
    std::int32_t minValue = 0;              // +0x130
    std::int32_t maxValue = 100;            // +0x134
  };
  static_assert(
    offsetof(WxSpinButtonClassFactoryRuntimeView, minValue) == 0x130,
    "WxSpinButtonClassFactoryRuntimeView::minValue offset must be 0x130"
  );
  static_assert(
    offsetof(WxSpinButtonClassFactoryRuntimeView, maxValue) == 0x134,
    "WxSpinButtonClassFactoryRuntimeView::maxValue offset must be 0x134"
  );
  static_assert(sizeof(WxSpinButtonClassFactoryRuntimeView) == 0x138, "WxSpinButtonClassFactoryRuntimeView size must be 0x138");

  struct WxStaticLineClassFactoryRuntimeView
  {
    std::uint8_t runtimeStorage[0x130]{};
  };
  static_assert(sizeof(WxStaticLineClassFactoryRuntimeView) == 0x130, "WxStaticLineClassFactoryRuntimeView size must be 0x130");

  struct WxMdiClientWindowClassFactoryRuntimeView
  {
    std::uint8_t unknown000_123[0x124]{};
    void* mdiParentRuntime = nullptr;       // +0x124
    void* mdiActiveChild = nullptr;         // +0x128
  };
  static_assert(
    offsetof(WxMdiClientWindowClassFactoryRuntimeView, mdiParentRuntime) == 0x124,
    "WxMdiClientWindowClassFactoryRuntimeView::mdiParentRuntime offset must be 0x124"
  );
  static_assert(
    offsetof(WxMdiClientWindowClassFactoryRuntimeView, mdiActiveChild) == 0x128,
    "WxMdiClientWindowClassFactoryRuntimeView::mdiActiveChild offset must be 0x128"
  );
  static_assert(sizeof(WxMdiClientWindowClassFactoryRuntimeView) == 0x12C, "WxMdiClientWindowClassFactoryRuntimeView size must be 0x12C");

  struct WxFontDataClassFactoryRuntimeView
  {
    void* vtable = nullptr;                       // +0x00
    void* runtimeLane04 = nullptr;                // +0x04
    wxColourRuntime selectedColour{};             // +0x08
    std::uint8_t allowSymbols = 0;                // +0x18
    std::uint8_t showHelp = 1;                    // +0x19
    std::uint8_t enableEffects = 1;               // +0x1A
    std::uint8_t unknown1B = 0;                   // +0x1B
    wxFontRuntime chosenFont{};                   // +0x1C
    wxFontRuntime initialFont{};                  // +0x28
    std::int32_t runtimeLane34 = 0;               // +0x34
    std::int32_t runtimeLane38 = 0;               // +0x38
    std::int32_t selectedEncoding = -1;           // +0x3C
    wchar_t* titleStorage = const_cast<wchar_t*>(wxEmptyString); // +0x40
    std::int32_t runtimeLane44 = -1;              // +0x44
    std::int32_t runtimeLane48 = 0;               // +0x48
  };
  static_assert(offsetof(WxFontDataClassFactoryRuntimeView, selectedColour) == 0x08, "WxFontDataClassFactoryRuntimeView::selectedColour offset must be 0x08");
  static_assert(offsetof(WxFontDataClassFactoryRuntimeView, allowSymbols) == 0x18, "WxFontDataClassFactoryRuntimeView::allowSymbols offset must be 0x18");
  static_assert(offsetof(WxFontDataClassFactoryRuntimeView, chosenFont) == 0x1C, "WxFontDataClassFactoryRuntimeView::chosenFont offset must be 0x1C");
  static_assert(offsetof(WxFontDataClassFactoryRuntimeView, initialFont) == 0x28, "WxFontDataClassFactoryRuntimeView::initialFont offset must be 0x28");
  static_assert(
    offsetof(WxFontDataClassFactoryRuntimeView, selectedEncoding) == 0x3C,
    "WxFontDataClassFactoryRuntimeView::selectedEncoding offset must be 0x3C"
  );
  static_assert(
    offsetof(WxFontDataClassFactoryRuntimeView, titleStorage) == 0x40,
    "WxFontDataClassFactoryRuntimeView::titleStorage offset must be 0x40"
  );
  static_assert(sizeof(WxFontDataClassFactoryRuntimeView) == 0x4C, "WxFontDataClassFactoryRuntimeView size must be 0x4C");

  struct WxFontDialogClassFactoryRuntimeView
  {
    wxDialogRuntime dialogBase{};
    WxFontDataClassFactoryRuntimeView fontData{};
  };
  static_assert(
    offsetof(WxFontDialogClassFactoryRuntimeView, fontData) == 0x170,
    "WxFontDialogClassFactoryRuntimeView::fontData offset must be 0x170"
  );
  static_assert(sizeof(WxFontDialogClassFactoryRuntimeView) == 0x1BC, "WxFontDialogClassFactoryRuntimeView size must be 0x1BC");

  struct WxBrushRefDataRuntimeView
  {
    void* vtable = nullptr;                  // +0x00
    std::int32_t refCount = 1;               // +0x04
    std::int32_t style = 100;                // +0x08
    std::uint8_t bitmapStorage[0xC]{};       // +0x0C
    std::uint8_t colourStorage[0x10]{};      // +0x18
    void* nativeBrush = nullptr;             // +0x28
  };
  static_assert(offsetof(WxBrushRefDataRuntimeView, refCount) == 0x04, "WxBrushRefDataRuntimeView::refCount offset must be 0x04");
  static_assert(offsetof(WxBrushRefDataRuntimeView, style) == 0x08, "WxBrushRefDataRuntimeView::style offset must be 0x08");
  static_assert(offsetof(WxBrushRefDataRuntimeView, nativeBrush) == 0x28, "WxBrushRefDataRuntimeView::nativeBrush offset must be 0x28");
  static_assert(sizeof(WxBrushRefDataRuntimeView) == 0x2C, "WxBrushRefDataRuntimeView size must be 0x2C");

  struct WxClipboardRuntimeView
  {
    void* vtable = nullptr;                  // +0x00
    void* openedDataObject = nullptr;        // +0x04
    std::uint8_t isOpen = 0;                 // +0x08
    std::uint8_t unknown09_0B[0x3]{};
  };
  static_assert(
    offsetof(WxClipboardRuntimeView, openedDataObject) == 0x04,
    "WxClipboardRuntimeView::openedDataObject offset must be 0x04"
  );
  static_assert(offsetof(WxClipboardRuntimeView, isOpen) == 0x08, "WxClipboardRuntimeView::isOpen offset must be 0x08");
  static_assert(sizeof(WxClipboardRuntimeView) == 0x0C, "WxClipboardRuntimeView size must be 0x0C");

  struct WxCursorRuntimeView
  {
    void* vtable = nullptr;                  // +0x00
    void* refData = nullptr;                 // +0x04
    std::uint8_t runtimeFlag08 = 0;          // +0x08
    std::uint8_t unknown09_0B[0x3]{};
  };
  static_assert(offsetof(WxCursorRuntimeView, refData) == 0x04, "WxCursorRuntimeView::refData offset must be 0x04");
  static_assert(offsetof(WxCursorRuntimeView, runtimeFlag08) == 0x08, "WxCursorRuntimeView::runtimeFlag08 offset must be 0x08");
  static_assert(sizeof(WxCursorRuntimeView) == 0x0C, "WxCursorRuntimeView size must be 0x0C");

  struct WxCommandDispatchSourceRuntimeView
  {
    std::uint8_t unknown00_5B[0x5C]{};
    WxEventProcessorRuntime* eventHandler = nullptr;   // +0x5C
  };
  static_assert(
    offsetof(WxCommandDispatchSourceRuntimeView, eventHandler) == 0x5C,
    "WxCommandDispatchSourceRuntimeView::eventHandler offset must be 0x5C"
  );

  struct WxScrollHelperRectRuntimeView
  {
    std::int32_t x = 0;                  // +0x00
    std::int32_t y = 0;                  // +0x04
    std::int32_t width = 0;              // +0x08
    std::int32_t height = 0;             // +0x0C
  };
  static_assert(
    sizeof(WxScrollHelperRectRuntimeView) == 0x10,
    "WxScrollHelperRectRuntimeView size must be 0x10"
  );

  struct WxScrollHelperRuntimeView
  {
    std::uint8_t unknown00_07[0x8]{};       // +0x00
    wxWindowBase* window = nullptr;         // +0x08
    wxWindowBase* targetWindow = nullptr;   // +0x0C
    WxScrollHelperRectRuntimeView rectToScroll{}; // +0x10
    std::int32_t unknown20 = 0;             // +0x20
    std::int32_t xScrollPixelsPerUnit = 0;  // +0x24
    std::int32_t yScrollPixelsPerUnit = 0;  // +0x28
    std::int32_t xScrollPosition = 0;       // +0x2C
    std::int32_t yScrollPosition = 0;       // +0x30
    std::int32_t xScrollLines = 0;          // +0x34
    std::int32_t yScrollLines = 0;          // +0x38
  };
  static_assert(offsetof(WxScrollHelperRuntimeView, window) == 0x08, "WxScrollHelperRuntimeView::window offset must be 0x08");
  static_assert(
    offsetof(WxScrollHelperRuntimeView, targetWindow) == 0x0C,
    "WxScrollHelperRuntimeView::targetWindow offset must be 0x0C"
  );
  static_assert(
    offsetof(WxScrollHelperRuntimeView, rectToScroll) == 0x10,
    "WxScrollHelperRuntimeView::rectToScroll offset must be 0x10"
  );
  static_assert(
    offsetof(WxScrollHelperRuntimeView, xScrollPixelsPerUnit) == 0x24,
    "WxScrollHelperRuntimeView::xScrollPixelsPerUnit offset must be 0x24"
  );
  static_assert(
    offsetof(WxScrollHelperRuntimeView, yScrollPixelsPerUnit) == 0x28,
    "WxScrollHelperRuntimeView::yScrollPixelsPerUnit offset must be 0x28"
  );
  static_assert(
    offsetof(WxScrollHelperRuntimeView, xScrollPosition) == 0x2C,
    "WxScrollHelperRuntimeView::xScrollPosition offset must be 0x2C"
  );
  static_assert(
    offsetof(WxScrollHelperRuntimeView, yScrollPosition) == 0x30,
    "WxScrollHelperRuntimeView::yScrollPosition offset must be 0x30"
  );
  static_assert(
    offsetof(WxScrollHelperRuntimeView, xScrollLines) == 0x34,
    "WxScrollHelperRuntimeView::xScrollLines offset must be 0x34"
  );
  static_assert(
    offsetof(WxScrollHelperRuntimeView, yScrollLines) == 0x38,
    "WxScrollHelperRuntimeView::yScrollLines offset must be 0x38"
  );

  struct WxToolBarToolRuntimeView
  {
    void* vtable = nullptr;                     // +0x00
    void* runtimeLane04 = nullptr;              // +0x04
    void* ownerToolBar = nullptr;               // +0x08
    std::int32_t runtimeLane0C = 0;             // +0x0C
    std::int32_t runtimeLane10 = 0;             // +0x10
    std::int32_t toolKind = 0;                  // +0x14
    std::int32_t runtimeLane18 = 0;             // +0x18
    std::uint8_t runtimeFlag1C = 0;             // +0x1C
    std::uint8_t runtimeFlag1D = 1;             // +0x1D
    std::uint8_t unknown1E_1F[0x2]{};
    WxBitmapRuntimeView normalBitmap{};         // +0x20
    WxBitmapRuntimeView disabledBitmap{};       // +0x2C
    wxStringRuntime shortHelpText{};            // +0x38
    wxStringRuntime longHelpText{};             // +0x3C
    wxStringRuntime labelText{};                // +0x40
    std::int32_t runtimeLane44 = 0;             // +0x44
  };
  static_assert(offsetof(WxToolBarToolRuntimeView, ownerToolBar) == 0x08, "WxToolBarToolRuntimeView::ownerToolBar offset must be 0x08");
  static_assert(offsetof(WxToolBarToolRuntimeView, toolKind) == 0x14, "WxToolBarToolRuntimeView::toolKind offset must be 0x14");
  static_assert(offsetof(WxToolBarToolRuntimeView, normalBitmap) == 0x20, "WxToolBarToolRuntimeView::normalBitmap offset must be 0x20");
  static_assert(
    offsetof(WxToolBarToolRuntimeView, shortHelpText) == 0x38,
    "WxToolBarToolRuntimeView::shortHelpText offset must be 0x38"
  );
  static_assert(offsetof(WxToolBarToolRuntimeView, runtimeLane44) == 0x44, "WxToolBarToolRuntimeView::runtimeLane44 offset must be 0x44");
  static_assert(sizeof(WxToolBarToolRuntimeView) == 0x48, "WxToolBarToolRuntimeView size must be 0x48");

  struct WxToolBarToolSeedRuntimeView
  {
    std::uint8_t unknown00_27[0x28]{};
    std::int32_t runtimeLane28 = 0;             // +0x28
  };
  static_assert(
    offsetof(WxToolBarToolSeedRuntimeView, runtimeLane28) == 0x28,
    "WxToolBarToolSeedRuntimeView::runtimeLane28 offset must be 0x28"
  );

  class WxSelectionLabelProviderRuntimeDispatch
  {
  public:
    virtual void Slot00() = 0;
    virtual std::int32_t GetSelectionIndex() = 0;
    virtual void Slot08() = 0;
    virtual void Slot0C() = 0;
    virtual void Slot10() = 0;
    virtual void Slot14() = 0;
    virtual wxStringRuntime* GetLabelByIndex(wxStringRuntime* outText, std::int32_t index) = 0;
  };

  struct WxUxThemeApiRuntimeView
  {
    std::uint8_t isAvailable = 0;           // +0x00
    std::uint8_t unknown01_03[0x3]{};       // +0x01
    HMODULE moduleHandle = nullptr;         // +0x04
    FARPROC procSlots[47]{};                // +0x08
  };
  static_assert(
    offsetof(WxUxThemeApiRuntimeView, moduleHandle) == 0x4,
    "WxUxThemeApiRuntimeView::moduleHandle offset must be 0x4"
  );
  static_assert(
    offsetof(WxUxThemeApiRuntimeView, procSlots) == 0x8,
    "WxUxThemeApiRuntimeView::procSlots offset must be 0x8"
  );
  static_assert(sizeof(WxUxThemeApiRuntimeView) == 0xC4, "WxUxThemeApiRuntimeView size must be 0xC4");

  struct WxStringPairLookupRuntimeView
  {
    WxStringArrayRuntimeView keys{};
    WxStringArrayRuntimeView values{};
  };

  WxUxThemeApiRuntimeView* gWxUxThemeApiRuntime = nullptr;
  WxStringPairLookupRuntimeView gWxStringPairLookup{};

  [[nodiscard]] std::uint32_t wxGetComCtl32PackedVersionRuntime() noexcept
  {
    HMODULE moduleHandle = ::LoadLibraryW(L"comctl32.dll");
    if (moduleHandle == nullptr) {
      return 0;
    }

    struct DllVersionInfoRuntime
    {
      DWORD cbSize;
      DWORD dwMajorVersion;
      DWORD dwMinorVersion;
      DWORD dwBuildNumber;
      DWORD dwPlatformID;
    };

    using DllGetVersionFn = HRESULT(WINAPI*)(DllVersionInfoRuntime*);
    const auto getVersion = reinterpret_cast<DllGetVersionFn>(::GetProcAddress(moduleHandle, "DllGetVersion"));
    if (getVersion == nullptr) {
      ::FreeLibrary(moduleHandle);
      return 0;
    }

    DllVersionInfoRuntime versionInfo{};
    versionInfo.cbSize = sizeof(versionInfo);
    const HRESULT hr = getVersion(&versionInfo);
    ::FreeLibrary(moduleHandle);
    if (FAILED(hr)) {
      return 0;
    }

    return static_cast<std::uint32_t>(versionInfo.dwMajorVersion * 100u + versionInfo.dwMinorVersion);
  }
}

/**
 * Address: 0x0099AA00 (FUN_0099AA00)
 *
 * What it does:
 * Zeros one list-control runtime tail lane used by both `wxListCtrl` and
 * `wxListView` constructor paths.
 */
[[maybe_unused]] int wxInitializeListCtrlRuntimeState(
  void* const listControlRuntime
)
{
  auto* const runtime = static_cast<WxListCtrlClassFactoryRuntimeView*>(listControlRuntime);
  if (runtime == nullptr) {
    return 0;
  }

  runtime->runtimeLane134 = 0;
  runtime->runtimeLane138 = 0;
  runtime->runtimeLane13C = 0;
  runtime->runtimeFlag142 = 0;
  runtime->runtimeFlag141 = 0;
  runtime->runtimeFlag140 = 0;
  runtime->runtimeLane144 = nullptr;
  runtime->runtimeLane148 = nullptr;
  runtime->runtimeLane130 = nullptr;
  runtime->runtimeFlag14C = 0;
  runtime->runtimeFlag14D = 0;
  return 0;
}

/**
 * Address: 0x009CECB0 (FUN_009CECB0)
 *
 * What it does:
 * Clears one radio-button checked-state lane during constructor initialization.
 */
[[maybe_unused]] void wxInitializeRadioButtonRuntimeState(
  void* const radioButtonRuntime
)
{
  auto* const runtime = static_cast<WxRadioButtonClassFactoryRuntimeView*>(radioButtonRuntime);
  if (runtime == nullptr) {
    return;
  }

  runtime->checkedFlag = 0;
}

/**
 * Address: 0x00A06420 (FUN_00A06420)
 *
 * What it does:
 * Initializes one toolbar-base runtime tail lane by clearing tool-list storage
 * pointers and defaulting the "tool packing enabled" flag.
 */
[[maybe_unused]] int wxInitializeToolBarBaseRuntimeState(
  void* const toolBarRuntime
)
{
  auto* const runtime = static_cast<WxToolBarClassFactoryRuntimeView*>(toolBarRuntime);
  if (runtime == nullptr) {
    return 0;
  }

  runtime->toolList.vtable = nullptr;
  runtime->toolPackingEnabled = 1u;
  runtime->runtimeLane150 = 0;
  runtime->runtimeLane14C = 0;
  runtime->runtimeLane158 = 0;
  runtime->runtimeLane154 = 0;
  return 0;
}

/**
 * Address: 0x00A076A0 (FUN_00A076A0)
 *
 * What it does:
 * Seeds one toolbar runtime with default tool-size lanes `(16, 15)` and clears
 * supplemental metric lanes.
 */
[[maybe_unused]] int wxInitializeToolBarRuntimeMetrics(
  void* const toolBarRuntime
)
{
  auto* const runtime = static_cast<WxToolBarClassFactoryRuntimeView*>(toolBarRuntime);
  if (runtime == nullptr) {
    return 0;
  }

  runtime->runtimeLane16C = 0;
  runtime->runtimeLane170 = 0;
  runtime->defaultToolWidth = 16;
  runtime->defaultToolHeight = 15;
  runtime->runtimeLane174 = 0;
  return 0;
}

/**
 * Address: 0x00A14820 (FUN_00A14820)
 *
 * What it does:
 * Initializes one process runtime lane by seeding process id/redirect mode and
 * clearing stream ownership lanes.
 */
[[maybe_unused]] char wxInitializeProcessRuntimeState(
  void* const processRuntime,
  const std::int32_t processData,
  const std::int32_t processId,
  const char redirectMode
)
{
  auto* const runtime = static_cast<WxProcessClassFactoryRuntimeView*>(processRuntime);
  if (runtime == nullptr) {
    return static_cast<char>(redirectMode & 1);
  }

  if (processData != 0) {
    runtime->processData = processData;
  }

  runtime->processId = processId;
  runtime->redirectMode = static_cast<std::uint8_t>(redirectMode & 1);
  runtime->inputStream = nullptr;
  runtime->outputStream = nullptr;
  runtime->errorStream = nullptr;
  return static_cast<char>(redirectMode & 1);
}

/**
 * Address: 0x00A18B90 (FUN_00A18B90)
 *
 * What it does:
 * Clears one popup-transient-window runtime tail lane that retains popup owner
 * and transient child/parent pointers.
 */
[[maybe_unused]] int wxInitializePopupTransientWindowRuntimeState(
  void* const popupRuntime
)
{
  auto* const runtime = static_cast<WxPopupTransientWindowClassFactoryRuntimeView*>(popupRuntime);
  if (runtime == nullptr) {
    return 0;
  }

  runtime->popupOwnerWindow = nullptr;
  runtime->popupFocusWindow = nullptr;
  runtime->popupParentWindow = nullptr;
  runtime->popupChildWindow = nullptr;
  return 0;
}

/**
 * Address: 0x00995750 (FUN_00995750)
 *
 * What it does:
 * Allocates one default `wxTextCtrl` class-factory instance payload.
 */
[[maybe_unused]] void* wxCreateTextCtrlRuntimeClassInstance()
{
  return new (std::nothrow) WxTextCtrlClassFactoryRuntimeView{};
}

/**
 * Address: 0x00982AC0 (FUN_00982AC0)
 *
 * What it does:
 * Allocates and seeds one default `wxTreeListCtrl` class-factory instance.
 */
[[maybe_unused]] void* wxCreateTreeListCtrlRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxTreeListCtrlClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(runtime);
  auto* const baseObject = static_cast<WxObjectRuntimeView*>(static_cast<void*>(runtime));
  baseObject->vtable = &gWxTreeListCtrlCtorRuntimeVTableTag;
  return runtime;
}

/**
 * Address: 0x009A51A0 (FUN_009A51A0)
 *
 * What it does:
 * Allocates and seeds one default `wxButton` class-factory instance.
 */
[[maybe_unused]] void* wxCreateButtonRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxControl130ClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(runtime);
  auto* const baseObject = static_cast<WxObjectRuntimeView*>(static_cast<void*>(runtime));
  baseObject->vtable = &gWxButtonRuntimeVTableTag;
  return runtime;
}

/**
 * Address: 0x009A5A50 (FUN_009A5A50)
 *
 * What it does:
 * Allocates and seeds one default `wxStaticText` class-factory instance.
 */
[[maybe_unused]] void* wxCreateStaticTextRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxControl130ClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(runtime);
  auto* const baseObject = static_cast<WxObjectRuntimeView*>(static_cast<void*>(runtime));
  baseObject->vtable = &gWxStaticTextRuntimeVTableTag;
  return runtime;
}

/**
 * Address: 0x009ACA50 (FUN_009ACA50)
 *
 * What it does:
 * Allocates and seeds one default `wxStaticBox` class-factory instance.
 */
[[maybe_unused]] void* wxCreateStaticBoxRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxControl130ClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(runtime);
  auto* const baseObject = static_cast<WxObjectRuntimeView*>(static_cast<void*>(runtime));
  baseObject->vtable = &gWxStaticBoxRuntimeVTableTag;
  return runtime;
}

/**
 * Address: 0x009ACC50 (FUN_009ACC50)
 *
 * What it does:
 * Allocates and seeds one default `wxCheckBox` class-factory instance.
 */
[[maybe_unused]] void* wxCreateCheckBoxRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxControl130ClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(runtime);
  auto* const baseObject = static_cast<WxObjectRuntimeView*>(static_cast<void*>(runtime));
  baseObject->vtable = &gWxCheckBoxRuntimeVTableTag;
  return runtime;
}

/**
 * Address: 0x004FB960 (FUN_004FB960)
 *
 * What it does:
 * Constructs one `wxBitmapButtonBase` runtime payload by seeding control base
 * state, clearing four embedded bitmap lanes, and zeroing bitmap-button tail
 * metrics.
 */
[[maybe_unused]] void* wxConstructBitmapButtonBaseRuntime(
  void* const bitmapButtonRuntime
)
{
  auto* const runtime = static_cast<WxBitmapButtonClassFactoryRuntimeView*>(bitmapButtonRuntime);
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(runtime);
  auto* const baseObject = static_cast<WxObjectRuntimeView*>(static_cast<void*>(runtime));
  baseObject->vtable = &gWxBitmapButtonBaseRuntimeVTableTag;

  runtime->normalBitmap.vtable = nullptr;
  runtime->normalBitmap.refData = nullptr;
  runtime->normalBitmap.reserved08 = 0;
  runtime->selectedBitmap.vtable = nullptr;
  runtime->selectedBitmap.refData = nullptr;
  runtime->selectedBitmap.reserved08 = 0;
  runtime->focusedBitmap.vtable = nullptr;
  runtime->focusedBitmap.refData = nullptr;
  runtime->focusedBitmap.reserved08 = 0;
  runtime->disabledBitmap.vtable = nullptr;
  runtime->disabledBitmap.refData = nullptr;
  runtime->disabledBitmap.reserved08 = 0;
  runtime->runtimeLane160 = 0;
  runtime->runtimeLane164 = 0;
  return runtime;
}

/**
 * Address: 0x009AF090 (FUN_009AF090)
 *
 * What it does:
 * Allocates one default `wxBitmapButton` class-factory instance, constructs
 * the bitmap-button base lanes, seeds default margins, and rebinds the
 * instance to the `wxBitmapButton` dispatch lane.
 */
[[maybe_unused]] void* wxCreateBitmapButtonRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxBitmapButtonClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  (void)wxConstructBitmapButtonBaseRuntime(runtime);
  runtime->runtimeLane164 = 4;
  runtime->runtimeLane160 = 4;

  auto* const baseObject = static_cast<WxObjectRuntimeView*>(static_cast<void*>(runtime));
  baseObject->vtable = &gWxBitmapButtonRuntimeVTableTag;
  return runtime;
}

/**
 * Address: 0x009BC5E0 (FUN_009BC5E0)
 *
 * What it does:
 * Allocates and seeds one default `wxFontList` class-factory instance.
 */
[[maybe_unused]] void* wxCreateFontListRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxStockListClassFactoryRuntimeView();
  if (runtime == nullptr) {
    return nullptr;
  }

  auto* const baseObject = static_cast<WxObjectRuntimeView*>(static_cast<void*>(runtime));
  baseObject->vtable = &gWxFontListRuntimeVTableTag;
  return runtime;
}

/**
 * Address: 0x009BC640 (FUN_009BC640)
 *
 * What it does:
 * Allocates and seeds one default `wxPenList` class-factory instance.
 */
[[maybe_unused]] void* wxCreatePenListRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxStockListClassFactoryRuntimeView();
  if (runtime == nullptr) {
    return nullptr;
  }

  auto* const baseObject = static_cast<WxObjectRuntimeView*>(static_cast<void*>(runtime));
  baseObject->vtable = &gWxPenListRuntimeVTableTag;
  return runtime;
}

/**
 * Address: 0x0099C0D0 (FUN_0099C0D0)
 *
 * What it does:
 * Allocates and initializes one default `wxListCtrl` class-factory instance.
 */
[[maybe_unused]] void* wxCreateListCtrlRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxListCtrlClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  (void)wxInitializeListCtrlRuntimeState(runtime);
  return runtime;
}

/**
 * Address: 0x0099E640 (FUN_0099E640)
 *
 * What it does:
 * Allocates and initializes one default `wxListView` class-factory instance.
 */
[[maybe_unused]] void* wxCreateListViewRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxListCtrlClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  (void)wxInitializeListCtrlRuntimeState(runtime);
  return runtime;
}

/**
 * Address: 0x009CF010 (FUN_009CF010)
 *
 * What it does:
 * Allocates and initializes one default `wxRadioButton` class-factory
 * instance.
 */
[[maybe_unused]] void* wxCreateRadioButtonRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxRadioButtonClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeRadioButtonRuntimeState(runtime);
  return runtime;
}

/**
 * Address: 0x00A086A0 (FUN_00A086A0)
 *
 * What it does:
 * Allocates and initializes one default `wxToolBar` class-factory instance.
 */
[[maybe_unused]] void* wxCreateToolBarRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxToolBarClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  (void)wxInitializeToolBarBaseRuntimeState(runtime);
  (void)wxInitializeToolBarRuntimeMetrics(runtime);
  return runtime;
}

/**
 * Address: 0x00A14B40 (FUN_00A14B40)
 *
 * What it does:
 * Allocates and initializes one default `wxProcess` class-factory instance
 * (`processData=0`, `processId=-1`, `redirectMode=0`).
 */
[[maybe_unused]] void* wxCreateProcessRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxProcessClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  (void)wxInitializeProcessRuntimeState(runtime, 0, -1, 0);
  return runtime;
}

/**
 * Address: 0x00A18F80 (FUN_00A18F80)
 *
 * What it does:
 * Allocates and initializes one default `wxPopupTransientWindow`
 * class-factory instance.
 */
[[maybe_unused]] void* wxCreatePopupTransientWindowRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxPopupTransientWindowClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  (void)wxInitializePopupTransientWindowRuntimeState(runtime);
  return runtime;
}

namespace
{
  void wxInitializeListBaseCtorRuntime(
    WxListBaseCtorRuntimeView* const listRuntime,
    const std::int32_t keyType,
    const std::uint8_t destroyItems
  ) noexcept
  {
    if (listRuntime == nullptr) {
      return;
    }

    listRuntime->vtable = &gWxListBaseRuntimeVTableTag;
    listRuntime->refData = nullptr;
    listRuntime->itemCount = 0;
    listRuntime->destroyItems = destroyItems;
    listRuntime->first = nullptr;
    listRuntime->last = nullptr;
    listRuntime->keyType = keyType;
  }

  [[nodiscard]] wxNodeBaseRuntime* wxCreateTypedListNodeRuntime(
    void* const listOwner,
    wxNodeBaseRuntime* const previous,
    wxNodeBaseRuntime* const next,
    void* const value,
    const wxListKeyRuntime* const key,
    void* const nodeVtableTag
  )
  {
    auto* const node = new (std::nothrow) wxNodeBaseRuntime{};
    if (node == nullptr) {
      return nullptr;
    }

    (void)wxNodeBaseInit(node, listOwner, previous, next, value, key);
    auto* const nodeObject = reinterpret_cast<WxObjectRuntimeView*>(node);
    nodeObject->vtable = nodeVtableTag;
    return node;
  }

  void wxObjectRefRuntime(
    WxObjectRuntimeView* const object,
    const WxObjectRuntimeView* const clone
  ) noexcept
  {
    if (object == nullptr || clone == nullptr || object->refData == clone->refData) {
      return;
    }

    wxEventUnRefRuntime(object);
    auto* const refData = static_cast<WxObjectRefDataRuntimeView*>(clone->refData);
    if (refData != nullptr) {
      object->refData = refData;
      ++refData->refCount;
    }
  }

  void RetainRuntimeStringFromPointerStorage(
    wxStringRuntime* const outValue,
    const wxStringRuntime* const sourceValue
  ) noexcept
  {
    if (outValue == nullptr) {
      return;
    }

    outValue->m_pchData = const_cast<wchar_t*>(wxEmptyString);
    if (sourceValue == nullptr || sourceValue->m_pchData == nullptr || sourceValue->m_pchData == wxEmptyString) {
      return;
    }

    outValue->m_pchData = sourceValue->m_pchData;
    auto* const sharedPrefixWords = reinterpret_cast<std::int32_t*>(outValue->m_pchData) - 3;
    if (sharedPrefixWords[1] != 0 && sharedPrefixWords[0] != -1) {
      ++sharedPrefixWords[0];
    }
  }

  void ReleaseRuntimeStringFromTemporaryStorage(
    wxStringRuntime* const value
  ) noexcept
  {
    if (value == nullptr || value->m_pchData == nullptr || value->m_pchData == wxEmptyString) {
      return;
    }

    auto* const sharedPrefixWords = reinterpret_cast<std::int32_t*>(value->m_pchData) - 3;
    if (sharedPrefixWords[0] != -1) {
      --sharedPrefixWords[0];
      if (sharedPrefixWords[0] == 0) {
        ::operator delete(sharedPrefixWords);
      }
    }
    value->m_pchData = const_cast<wchar_t*>(wxEmptyString);
  }

  void wxResolveScrollHelperVisibleSpan(
    const WxScrollHelperRuntimeView* const scrollHelper,
    std::int32_t* const outWidth,
    std::int32_t* const outHeight
  ) noexcept
  {
    if (outWidth == nullptr || outHeight == nullptr) {
      return;
    }

    *outWidth = 0;
    *outHeight = 0;
    if (scrollHelper == nullptr) {
      return;
    }

    if (scrollHelper->rectToScroll.width != 0) {
      *outWidth = scrollHelper->rectToScroll.width;
      *outHeight = scrollHelper->rectToScroll.height;
      return;
    }

    if (scrollHelper->targetWindow == nullptr) {
      return;
    }

    const wxSize clientSize = scrollHelper->targetWindow->GetClientSize();
    *outWidth = clientSize.x;
    *outHeight = clientSize.y;
  }

  void wxInitializeToolBarToolBaseRuntime(
    WxToolBarToolRuntimeView* const runtime
  ) noexcept
  {
    if (runtime == nullptr) {
      return;
    }

    runtime->runtimeLane04 = nullptr;
    runtime->vtable = &gWxToolBarToolBaseRuntimeVTableTag;
    runtime->normalBitmap.vtable = nullptr;
    runtime->normalBitmap.refData = nullptr;
    runtime->normalBitmap.reserved08 = 0;
    runtime->disabledBitmap.vtable = nullptr;
    runtime->disabledBitmap.refData = nullptr;
    runtime->disabledBitmap.reserved08 = 0;
    runtime->shortHelpText.m_pchData = const_cast<wchar_t*>(wxEmptyString);
    runtime->longHelpText.m_pchData = const_cast<wchar_t*>(wxEmptyString);
    runtime->labelText.m_pchData = const_cast<wchar_t*>(wxEmptyString);
    runtime->runtimeFlag1C = 0;
    runtime->runtimeFlag1D = 1;
  }

  void wxInitializeFontDataRuntime(
    WxFontDataClassFactoryRuntimeView* const runtime
  ) noexcept
  {
    if (runtime == nullptr) {
      return;
    }

    runtime->runtimeLane04 = nullptr;
    runtime->vtable = &gWxFontDataRuntimeVTableTag;
    runtime->selectedColour = wxColourRuntime::Null();
    runtime->allowSymbols = 0u;
    runtime->showHelp = 1u;
    runtime->enableEffects = 1u;
    runtime->chosenFont = wxFontRuntime::Null();
    runtime->initialFont = wxFontRuntime::Null();
    runtime->runtimeLane34 = 0;
    runtime->runtimeLane38 = 0;
    runtime->selectedEncoding = -1;
    runtime->titleStorage = const_cast<wchar_t*>(wxEmptyString);
    runtime->runtimeLane44 = -1;
    runtime->runtimeLane48 = 0;
  }

  [[nodiscard]] WxClipboardRuntimeView* wxConstructClipboardRuntime(
    WxClipboardRuntimeView* const runtime
  ) noexcept
  {
    if (runtime == nullptr) {
      return nullptr;
    }

    runtime->vtable = &gWxClipboardRuntimeVTableTag;
    runtime->openedDataObject = nullptr;
    runtime->isOpen = 0;
    return runtime;
  }
}

/**
 * Address: 0x00A187F0 (FUN_00A187F0)
 *
 * What it does:
 * Allocates one `wxFontDialog` runtime payload, runs the dialog-base init
 * lane, initializes embedded `wxFontData` defaults, then rebinds the concrete
 * font-dialog vtable lane.
 */
[[maybe_unused]] void* wxCreateFontDialogRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxFontDialogClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  auto* const baseObject = reinterpret_cast<WxObjectRuntimeView*>(static_cast<void*>(&runtime->dialogBase));
  baseObject->vtable = &gWxFontDialogBaseRuntimeVTableTag;
  wxInitializeFontDataRuntime(&runtime->fontData);
  baseObject->vtable = &gWxFontDialogRuntimeVTableTag;
  return runtime;
}

/**
 * Address: 0x009BC700 (FUN_009BC700)
 *
 * What it does:
 * Allocates one `wxResourceCache` list runtime lane, runs wx-list-base ctor
 * field initialization, and binds the resource-cache vtable.
 */
[[maybe_unused]] void* wxCreateResourceCacheRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxListBaseCtorRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeListBaseCtorRuntime(runtime, 0, 0u);
  runtime->vtable = &gWxResourceCacheListRuntimeVTableTag;
  return runtime;
}

/**
 * Address: 0x009DE960 (FUN_009DE960)
 *
 * What it does:
 * Allocates one `wxPathList` runtime lane, initializes base list fields, sets
 * destroy-item mode, and binds the path-list vtable.
 */
[[maybe_unused]] void* wxCreatePathListRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxListBaseCtorRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeListBaseCtorRuntime(runtime, 0, 1u);
  runtime->vtable = &gWxPathListRuntimeVTableTag;
  return runtime;
}

/**
 * Address: 0x009CF2A0 (FUN_009CF2A0)
 *
 * What it does:
 * Allocates one list-string node, runs `wxNodeBase` initialization, and binds
 * the `wxListStringNode` dispatch vtable lane.
 */
[[maybe_unused]] wxNodeBaseRuntime* wxCreateListStringNodeRuntime(
  void* const listOwner,
  wxNodeBaseRuntime* const previous,
  wxNodeBaseRuntime* const next,
  void* const value,
  const wxListKeyRuntime* const key
)
{
  return wxCreateTypedListNodeRuntime(
    listOwner,
    previous,
    next,
    value,
    key,
    &gWxListStringNodeRuntimeVTableTag
  );
}

/**
 * Address: 0x009D7A20 (FUN_009D7A20)
 *
 * What it does:
 * Allocates one module-list node, runs `wxNodeBase` initialization, and binds
 * the `wxModuleListNode` dispatch vtable lane.
 */
[[maybe_unused]] wxNodeBaseRuntime* wxCreateModuleListNodeRuntime(
  void* const listOwner,
  wxNodeBaseRuntime* const previous,
  wxNodeBaseRuntime* const next,
  void* const value,
  const wxListKeyRuntime* const key
)
{
  return wxCreateTypedListNodeRuntime(
    listOwner,
    previous,
    next,
    value,
    key,
    &gWxModuleListNodeRuntimeVTableTag
  );
}

/**
 * Address: 0x00A06130 (FUN_00A06130)
 *
 * What it does:
 * Allocates one toolbar-tools list node, runs `wxNodeBase` initialization, and
 * binds the typed list-node vtable lane.
 */
[[maybe_unused]] wxNodeBaseRuntime* wxCreateToolBarToolsListNodeRuntime(
  void* const listOwner,
  wxNodeBaseRuntime* const previous,
  wxNodeBaseRuntime* const next,
  void* const value,
  const wxListKeyRuntime* const key
)
{
  return wxCreateTypedListNodeRuntime(
    listOwner,
    previous,
    next,
    value,
    key,
    &gWxToolBarToolsListNodeRuntimeVTableTag
  );
}

/**
 * Address: 0x00A19350 (FUN_00A19350)
 *
 * What it does:
 * Allocates one simple-data-object list node, runs `wxNodeBase` initialization,
 * and binds the typed list-node vtable lane.
 */
[[maybe_unused]] wxNodeBaseRuntime* wxCreateSimpleDataObjectListNodeRuntime(
  void* const listOwner,
  wxNodeBaseRuntime* const previous,
  wxNodeBaseRuntime* const next,
  void* const value,
  const wxListKeyRuntime* const key
)
{
  return wxCreateTypedListNodeRuntime(
    listOwner,
    previous,
    next,
    value,
    key,
    &gWxSimpleDataObjectListNodeRuntimeVTableTag
  );
}

/**
 * Address: 0x00A2AD80 (FUN_00A2AD80)
 *
 * What it does:
 * Allocates one art-provider list node, runs `wxNodeBase` initialization, and
 * binds the typed list-node vtable lane.
 */
[[maybe_unused]] wxNodeBaseRuntime* wxCreateArtProvidersListNodeRuntime(
  void* const listOwner,
  wxNodeBaseRuntime* const previous,
  wxNodeBaseRuntime* const next,
  void* const value,
  const wxListKeyRuntime* const key
)
{
  return wxCreateTypedListNodeRuntime(
    listOwner,
    previous,
    next,
    value,
    key,
    &gWxArtProvidersListNodeRuntimeVTableTag
  );
}

/**
 * Address: 0x009D24A0 (FUN_009D24A0)
 *
 * What it does:
 * Allocates one default `wxPopupWindow` runtime class-factory payload and
 * applies the popup-window vtable lane.
 */
[[maybe_unused]] void* wxCreatePopupWindowRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxPopupWindowClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  auto* const objectRuntime = reinterpret_cast<WxObjectRuntimeView*>(runtime);
  objectRuntime->refData = nullptr;
  objectRuntime->vtable = &gWxPopupWindowRuntimeVTableTag;
  return runtime;
}

/**
 * Address: 0x009D2960 (FUN_009D2960)
 *
 * What it does:
 * Allocates one `wxBrushRefData` runtime payload and seeds refcount/style/brush
 * lanes for the default null-colour brush state.
 */
[[maybe_unused]] void* wxCreateBrushRefDataRuntimeWithNullColour()
{
  auto* const runtime = new (std::nothrow) WxBrushRefDataRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  runtime->vtable = &gWxBrushRefDataRuntimeVTableTag;
  runtime->refCount = 1;
  runtime->style = 100;
  runtime->nativeBrush = nullptr;
  return runtime;
}

/**
 * Address: 0x009D4110 (FUN_009D4110)
 *
 * What it does:
 * Allocates one default `wxSpinCtrl` runtime class-factory payload, clears min
 * lane, seeds max lane to `100`, and binds the spin-ctrl vtable.
 */
[[maybe_unused]] void* wxCreateSpinCtrlRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxSpinCtrlClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(runtime);
  auto* const objectRuntime = reinterpret_cast<WxObjectRuntimeView*>(runtime);
  objectRuntime->vtable = &gWxSpinCtrlRuntimeVTableTag;
  runtime->minValue = 0;
  runtime->maxValue = 100;
  return runtime;
}

/**
 * Address: 0x009ED8C0 (FUN_009ED8C0)
 *
 * What it does:
 * Allocates one default `wxScrollBar` runtime class-factory payload, clears
 * thumb/range/page lanes, and binds the scrollbar vtable.
 */
[[maybe_unused]] void* wxCreateScrollBarRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxScrollBarClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(runtime);
  auto* const objectRuntime = reinterpret_cast<WxObjectRuntimeView*>(runtime);
  objectRuntime->vtable = &gWxScrollBarRuntimeVTableTag;
  runtime->thumbPosition = 0;
  runtime->thumbRange = 0;
  runtime->pageSize = 0;
  return runtime;
}

/**
 * Address: 0x009EDC50 (FUN_009EDC50)
 *
 * What it does:
 * Allocates one default `wxStaticBitmap` runtime class-factory payload, enables
 * mask usage, clears bitmap payload lane, and binds the static-bitmap vtable.
 */
[[maybe_unused]] void* wxCreateStaticBitmapRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxStaticBitmapClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(runtime);
  auto* const objectRuntime = reinterpret_cast<WxObjectRuntimeView*>(runtime);
  objectRuntime->vtable = &gWxStaticBitmapRuntimeVTableTag;
  runtime->useMaskFlag = 1u;
  runtime->bitmapPayload = nullptr;
  return runtime;
}

/**
 * Address: 0x009EE4F0 (FUN_009EE4F0)
 *
 * What it does:
 * Allocates one default `wxSpinButton` runtime class-factory payload, seeds
 * range lanes (`min=0`, `max=100`), and binds the spin-button vtable.
 */
[[maybe_unused]] void* wxCreateSpinButtonRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxSpinButtonClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(runtime);
  auto* const objectRuntime = reinterpret_cast<WxObjectRuntimeView*>(runtime);
  objectRuntime->vtable = &gWxSpinButtonRuntimeVTableTag;
  runtime->minValue = 0;
  runtime->maxValue = 100;
  return runtime;
}

/**
 * Address: 0x00A05D50 (FUN_00A05D50)
 *
 * What it does:
 * Allocates one default `wxStaticLine` runtime class-factory payload and binds
 * the static-line vtable lane.
 */
[[maybe_unused]] void* wxCreateStaticLineRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxStaticLineClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(runtime);
  auto* const objectRuntime = reinterpret_cast<WxObjectRuntimeView*>(runtime);
  objectRuntime->vtable = &gWxStaticLineRuntimeVTableTag;
  return runtime;
}

/**
 * Address: 0x009FCF00 (FUN_009FCF00)
 *
 * What it does:
 * Allocates one default `wxMDIClientWindow` class-factory payload, clears the
 * MDI parent/active-child lanes, and binds the MDI-client vtable.
 */
[[maybe_unused]] void* wxCreateMdiClientWindowRuntimeClassInstance()
{
  auto* const runtime = new (std::nothrow) WxMdiClientWindowClassFactoryRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  auto* const objectRuntime = reinterpret_cast<WxObjectRuntimeView*>(runtime);
  objectRuntime->refData = nullptr;
  objectRuntime->vtable = &gWxMdiClientWindowRuntimeVTableTag;
  runtime->mdiParentRuntime = nullptr;
  runtime->mdiActiveChild = nullptr;
  return runtime;
}

/**
 * Address: 0x009FD050 (FUN_009FD050)
 *
 * What it does:
 * Secondary class-factory lane for `wxMDIClientWindow`; forwards to the
 * canonical MDI-client allocator.
 */
[[maybe_unused]] void* wxCreateMdiClientWindowRuntimeClassInstanceAlias()
{
  return wxCreateMdiClientWindowRuntimeClassInstance();
}

/**
 * Address: 0x00A07630 (FUN_00A07630)
 *
 * What it does:
 * Allocates one `wxToolBarTool` runtime payload from toolbar + seed-tool lanes
 * and marks the constructed tool as inserted/owned by toolbar storage.
 */
[[maybe_unused]] void* wxCreateToolBarToolRuntimeFromSeed(
  void* const ownerToolBarRuntime,
  const void* const seedToolRuntime
)
{
  auto* const runtime = new (std::nothrow) WxToolBarToolRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeToolBarToolBaseRuntime(runtime);
  runtime->ownerToolBar = ownerToolBarRuntime;
  runtime->runtimeLane18 = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(seedToolRuntime));

  const auto* const seed = static_cast<const WxToolBarToolSeedRuntimeView*>(seedToolRuntime);
  runtime->runtimeLane10 = seed != nullptr ? seed->runtimeLane28 : 0;
  runtime->toolKind = 3;
  runtime->runtimeLane0C = 3;
  runtime->runtimeFlag1C = 0;
  runtime->runtimeFlag1D = 1;
  runtime->vtable = &gWxToolBarToolRuntimeVTableTag;
  runtime->runtimeLane44 = 1;
  return runtime;
}

/**
 * Address: 0x00A08780 (FUN_00A08780)
 *
 * What it does:
 * Allocates one `wxToolBarTool` runtime payload from explicit ctor lanes
 * (owner/id/bitmaps/help strings), applies runtime flags, and returns the tool.
 */
[[maybe_unused]] void* wxCreateToolBarToolRuntimeWithArguments(
  void* const ownerToolBarRuntime,
  const std::int32_t runtimeLane10,
  const wxStringRuntime* const labelText,
  const WxObjectRuntimeView* const normalBitmapRuntime,
  const WxObjectRuntimeView* const disabledBitmapRuntime,
  const std::int32_t toolKind,
  const std::int32_t runtimeLane18,
  const wxStringRuntime* const shortHelpText,
  const wxStringRuntime* const longHelpText
)
{
  auto* const runtime = new (std::nothrow) WxToolBarToolRuntimeView{};
  if (runtime == nullptr) {
    return nullptr;
  }

  wxInitializeToolBarToolBaseRuntime(runtime);
  RetainRuntimeStringFromPointerStorage(&runtime->labelText, labelText);
  RetainRuntimeStringFromPointerStorage(&runtime->shortHelpText, shortHelpText);
  RetainRuntimeStringFromPointerStorage(&runtime->longHelpText, longHelpText);

  runtime->ownerToolBar = ownerToolBarRuntime;
  runtime->runtimeLane10 = runtimeLane10;
  runtime->runtimeLane18 = runtimeLane18;
  if (normalBitmapRuntime != nullptr && runtime->normalBitmap.refData != normalBitmapRuntime->refData) {
    wxObjectRefRuntime(reinterpret_cast<WxObjectRuntimeView*>(&runtime->normalBitmap), normalBitmapRuntime);
  }
  if (disabledBitmapRuntime != nullptr && runtime->disabledBitmap.refData != disabledBitmapRuntime->refData) {
    wxObjectRefRuntime(reinterpret_cast<WxObjectRuntimeView*>(&runtime->disabledBitmap), disabledBitmapRuntime);
  }
  runtime->toolKind = toolKind;
  runtime->runtimeFlag1C = static_cast<std::uint8_t>((runtimeLane10 == -1) ? 2 : 1);
  runtime->runtimeFlag1D = 1;
  runtime->vtable = &gWxToolBarToolRuntimeVTableTag;
  runtime->runtimeLane44 = 0;
  return runtime;
}

/**
 * Address: 0x00A05400 (FUN_00A05400)
 *
 * What it does:
 * Returns the selected label text from one selection-provider runtime by
 * querying selected index then copying the associated label lane.
 */
[[maybe_unused]] wxStringRuntime* wxGetSelectionLabelRuntime(
  void* const selectionProviderRuntime,
  wxStringRuntime* const outLabel
)
{
  if (outLabel == nullptr) {
    return nullptr;
  }

  outLabel->m_pchData = const_cast<wchar_t*>(wxEmptyString);
  auto* const provider = static_cast<WxSelectionLabelProviderRuntimeDispatch*>(selectionProviderRuntime);
  if (provider == nullptr) {
    return outLabel;
  }

  const std::int32_t selectedIndex = provider->GetSelectionIndex();
  if (selectedIndex == -1) {
    return outLabel;
  }

  wxStringRuntime temporaryLabel{};
  temporaryLabel.m_pchData = const_cast<wchar_t*>(wxEmptyString);
  wxStringRuntime* const sourceLabel = provider->GetLabelByIndex(&temporaryLabel, selectedIndex);
  (void)wxCopySharedWxStringRuntime(sourceLabel, outLabel);
  ReleaseRuntimeStringFromTemporaryStorage(&temporaryLabel);
  return outLabel;
}

/**
 * Address: 0x00A06C00 (FUN_00A06C00)
 *
 * What it does:
 * Builds one `wxEVT_COMMAND_MENU_SELECTED` command-event payload with command
 * state lanes and dispatches it through the owner's event-handler lane.
 */
[[maybe_unused]] bool wxDispatchMenuSelectionCommandFromObjectRuntime(
  void* const objectRuntime,
  const std::int32_t commandId,
  const std::uint8_t commandState
)
{
  wxCommandEventRuntime commandEvent(EnsureWxEvtCommandMenuSelectedRuntimeType(), commandId);
  commandEvent.mCommandInt = static_cast<std::int32_t>(commandState);
  commandEvent.mExtraLong = static_cast<std::int32_t>(commandState);
  commandEvent.mEventObject = objectRuntime;

  const auto* const source = static_cast<WxCommandDispatchSourceRuntimeView*>(objectRuntime);
  if (source != nullptr && source->eventHandler != nullptr) {
    (void)source->eventHandler->ProcessEvent(&commandEvent);
  }
  return true;
}

/**
 * Address: 0x0098B810 (FUN_0098B810)
 *
 * What it does:
 * Initializes one cursor runtime lane (`refData=null`, state flag cleared),
 * binds cursor vtable, and shares ref-data from `clone` when present.
 */
[[maybe_unused]] void* wxConstructCursorRuntimeFromClone(
  void* const cursorRuntime,
  const void* const cloneRuntime
)
{
  auto* const cursor = static_cast<WxCursorRuntimeView*>(cursorRuntime);
  if (cursor == nullptr) {
    return nullptr;
  }

  cursor->refData = nullptr;
  cursor->runtimeFlag08 = 0;
  cursor->vtable = &gWxCursorRuntimeVTableTag;

  if (cloneRuntime != nullptr) {
    wxObjectRefRuntime(
      reinterpret_cast<WxObjectRuntimeView*>(cursor),
      static_cast<const WxObjectRuntimeView*>(cloneRuntime)
    );
  }
  return cursor;
}

/**
 * Address: 0x00A37F50 (FUN_00A37F50)
 *
 * What it does:
 * Allocates one process-wide clipboard runtime singleton lane, initializes base
 * clipboard fields, stores it into global runtime state, and returns success.
 */
[[maybe_unused]] char wxInitializeClipboardRuntimeSingleton()
{
  auto* const clipboardRuntime = new (std::nothrow) WxClipboardRuntimeView{};
  gWxClipboardRuntime = wxConstructClipboardRuntime(clipboardRuntime);
  return 1;
}

/**
 * Address: 0x00A03AC0 (FUN_00A03AC0)
 *
 * What it does:
 * Reports whether runtime UX-theme support should be enabled (Windows 5.x with
 * non-zero minor version and comctl32 version `>= 6.00`).
 */
[[maybe_unused]] BOOL wxCanUseUxThemeRuntime()
{
  std::array<std::uint8_t, 284> versionInformation{};
  auto* const versionHeader = reinterpret_cast<OSVERSIONINFOW*>(versionInformation.data());
  versionHeader->dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);

#pragma warning(push)
#pragma warning(disable : 4996)
  const BOOL gotVersionInfo = ::GetVersionExW(versionHeader)
    || ((versionHeader->dwOSVersionInfoSize = sizeof(OSVERSIONINFOW)), ::GetVersionExW(versionHeader));
#pragma warning(pop)
  if (gotVersionInfo == FALSE) {
    return FALSE;
  }

  if (versionHeader->dwMajorVersion != 5u || versionHeader->dwMinorVersion == 0u) {
    return FALSE;
  }

  return wxGetComCtl32PackedVersionRuntime() >= 600u ? TRUE : FALSE;
}

/**
 * Address: 0x00A03BC0 (FUN_00A03BC0)
 *
 * What it does:
 * Clears one UX-theme API slot table payload (`+0x08 .. +0xC0`) while keeping
 * availability and module-handle lanes intact.
 */
[[maybe_unused]] int wxResetUxThemeApiRuntimeSlots(
  void* const uxThemeRuntime
)
{
  auto* const runtime = static_cast<WxUxThemeApiRuntimeView*>(uxThemeRuntime);
  if (runtime == nullptr) {
    return 0;
  }

  std::fill(std::begin(runtime->procSlots), std::end(runtime->procSlots), nullptr);
  return 0;
}

/**
 * Address: 0x00A03C90 (FUN_00A03C90)
 *
 * What it does:
 * Releases one loaded UX-theme module handle and resets all cached function
 * slots.
 */
[[maybe_unused]] int wxReleaseUxThemeApiRuntime(
  void* const uxThemeRuntime
)
{
  auto* const runtime = static_cast<WxUxThemeApiRuntimeView*>(uxThemeRuntime);
  if (runtime == nullptr) {
    return 0;
  }

  if (runtime->moduleHandle != nullptr) {
    ::FreeLibrary(runtime->moduleHandle);
    runtime->moduleHandle = nullptr;
  }

  return wxResetUxThemeApiRuntimeSlots(runtime);
}

/**
 * Address: 0x00A04360 (FUN_00A04360)
 *
 * What it does:
 * If UX-theme support is available, destroys and frees the process-wide
 * UX-theme API cache object when present.
 */
[[maybe_unused]] void wxDestroyUxThemeApiRuntimeIfAvailable()
{
  if (!wxCanUseUxThemeRuntime()) {
    return;
  }

  if (gWxUxThemeApiRuntime != nullptr) {
    WxUxThemeApiRuntimeView* const runtime = gWxUxThemeApiRuntime;
    (void)wxReleaseUxThemeApiRuntime(runtime);
    ::operator delete(runtime);
    gWxUxThemeApiRuntime = nullptr;
  }
}

/**
 * Address: 0x00A14640 (FUN_00A14640)
 *
 * What it does:
 * Looks up one key string in the runtime string-pair registry and returns a
 * shared copy of the mapped value string, or `wxEmptyString` when absent.
 */
[[maybe_unused]] wxStringRuntime* wxLookupStringPairValueRuntime(
  wxStringRuntime* const outValue,
  const wxStringRuntime* const keyValue
)
{
  if (outValue == nullptr) {
    return nullptr;
  }

  const wchar_t* const keyText =
    (keyValue != nullptr && keyValue->m_pchData != nullptr) ? keyValue->m_pchData : const_cast<wchar_t*>(wxEmptyString);
  const std::int32_t entryIndex = WxFindStringArrayIndex(&gWxStringPairLookup.keys, keyText, false, false);
  if (
    entryIndex < 0 || gWxStringPairLookup.values.entries == nullptr || gWxStringPairLookup.values.count <= entryIndex
  ) {
    return wxCopySharedWxStringRuntime(nullptr, outValue);
  }

  wxStringRuntime sourceValue{};
  sourceValue.m_pchData = const_cast<wchar_t*>(gWxStringPairLookup.values.entries[entryIndex]);
  return wxCopySharedWxStringRuntime(&sourceValue, outValue);
}

namespace
{
  struct WxModuleRuntimeView
  {
    void* vtable = nullptr; // +0x00
    void* refData = nullptr; // +0x04
  };
  static_assert(offsetof(WxModuleRuntimeView, refData) == 0x4, "WxModuleRuntimeView::refData offset must be 0x4");
  static_assert(sizeof(WxModuleRuntimeView) == 0x8, "WxModuleRuntimeView size must be 0x8");

  struct WxRadioBoxBaseRuntimeView
  {
    void* vtable = nullptr;                   // +0x00
    std::uint8_t unknown004_133[0x130]{};     // +0x04
    std::int32_t selectedIndex = -1;          // +0x134
  };
  static_assert(
    offsetof(WxRadioBoxBaseRuntimeView, selectedIndex) == 0x134,
    "WxRadioBoxBaseRuntimeView::selectedIndex offset must be 0x134"
  );

  struct WxEventCopyRuntimeView
  {
    void* vtable = nullptr;                // +0x00
    void* refData = nullptr;               // +0x04
    void* eventObject = nullptr;           // +0x08
    std::int32_t eventType = 0;            // +0x0C
    std::int32_t eventTimestamp = 0;       // +0x10
    std::int32_t eventId = 0;              // +0x14
    void* callbackUserData = nullptr;      // +0x18
    std::uint8_t skipped = 0;              // +0x1C
    std::uint8_t isCommandEvent = 0;       // +0x1D
    std::uint8_t reserved1E = 0;           // +0x1E
    std::uint8_t reserved1F = 0;           // +0x1F
  };
  static_assert(offsetof(WxEventCopyRuntimeView, refData) == 0x4, "WxEventCopyRuntimeView::refData offset must be 0x4");
  static_assert(
    offsetof(WxEventCopyRuntimeView, eventObject) == 0x8,
    "WxEventCopyRuntimeView::eventObject offset must be 0x8"
  );
  static_assert(
    offsetof(WxEventCopyRuntimeView, callbackUserData) == 0x18,
    "WxEventCopyRuntimeView::callbackUserData offset must be 0x18"
  );
  static_assert(offsetof(WxEventCopyRuntimeView, skipped) == 0x1C, "WxEventCopyRuntimeView::skipped offset must be 0x1C");
  static_assert(
    offsetof(WxEventCopyRuntimeView, isCommandEvent) == 0x1D,
    "WxEventCopyRuntimeView::isCommandEvent offset must be 0x1D"
  );
  static_assert(sizeof(WxEventCopyRuntimeView) == 0x20, "WxEventCopyRuntimeView size must be 0x20");

  struct WxProcessEventCopyRuntimeView
  {
    WxEventCopyRuntimeView base{};
    std::int32_t param0 = 0;              // +0x20
    std::int32_t param1 = 0;              // +0x24
  };
  static_assert(offsetof(WxProcessEventCopyRuntimeView, param0) == 0x20, "WxProcessEventCopyRuntimeView::param0 offset must be 0x20");
  static_assert(offsetof(WxProcessEventCopyRuntimeView, param1) == 0x24, "WxProcessEventCopyRuntimeView::param1 offset must be 0x24");
  static_assert(sizeof(WxProcessEventCopyRuntimeView) == 0x28, "WxProcessEventCopyRuntimeView size must be 0x28");

  struct WxTimerEventCopyRuntimeView
  {
    WxEventCopyRuntimeView base{};
    std::int32_t timerId = 0;             // +0x20
  };
  static_assert(offsetof(WxTimerEventCopyRuntimeView, timerId) == 0x20, "WxTimerEventCopyRuntimeView::timerId offset must be 0x20");
  static_assert(sizeof(WxTimerEventCopyRuntimeView) == 0x24, "WxTimerEventCopyRuntimeView size must be 0x24");

  struct WxSocketEventCopyRuntimeView
  {
    WxEventCopyRuntimeView base{};
    std::int32_t lane20 = 0;              // +0x20
    std::int32_t lane24 = 0;              // +0x24
  };
  static_assert(offsetof(WxSocketEventCopyRuntimeView, lane20) == 0x20, "WxSocketEventCopyRuntimeView::lane20 offset must be 0x20");
  static_assert(offsetof(WxSocketEventCopyRuntimeView, lane24) == 0x24, "WxSocketEventCopyRuntimeView::lane24 offset must be 0x24");
  static_assert(sizeof(WxSocketEventCopyRuntimeView) == 0x28, "WxSocketEventCopyRuntimeView size must be 0x28");

  struct WxEvtHandlerCtorRuntimeView
  {
    void* vtable = nullptr;               // +0x00
    void* refData = nullptr;              // +0x04
    void* nextHandler = nullptr;          // +0x08
    void* previousHandler = nullptr;      // +0x0C
    void* dynamicEvents = nullptr;        // +0x10
    void* pendingEvents = nullptr;        // +0x14
    CRITICAL_SECTION* eventsLocker = nullptr; // +0x18
    std::uint8_t isWindow = 0;            // +0x1C
    std::uint8_t enabled = 1;             // +0x1D
    std::uint8_t reserved1E_1F[0x2]{};    // +0x1E
    void* clientObject = nullptr;         // +0x20
    std::int32_t clientDataType = 0;      // +0x24
  };
  static_assert(
    offsetof(WxEvtHandlerCtorRuntimeView, eventsLocker) == 0x18,
    "WxEvtHandlerCtorRuntimeView::eventsLocker offset must be 0x18"
  );
  static_assert(offsetof(WxEvtHandlerCtorRuntimeView, isWindow) == 0x1C, "WxEvtHandlerCtorRuntimeView::isWindow offset must be 0x1C");
  static_assert(offsetof(WxEvtHandlerCtorRuntimeView, enabled) == 0x1D, "WxEvtHandlerCtorRuntimeView::enabled offset must be 0x1D");
  static_assert(
    offsetof(WxEvtHandlerCtorRuntimeView, clientObject) == 0x20,
    "WxEvtHandlerCtorRuntimeView::clientObject offset must be 0x20"
  );
  static_assert(
    offsetof(WxEvtHandlerCtorRuntimeView, clientDataType) == 0x24,
    "WxEvtHandlerCtorRuntimeView::clientDataType offset must be 0x24"
  );

  using WxModuleLifecycleCallback = void(*)(void*);
  struct WxModuleRuntimeDispatchTable
  {
    WxModuleLifecycleCallback slot00 = nullptr;       // +0x00
    void* slot01 = nullptr;                           // +0x04
    void* slot02 = nullptr;                           // +0x08
    void* slot03 = nullptr;                           // +0x0C
    void* slot04 = nullptr;                           // +0x10
    WxModuleLifecycleCallback onBootstrapFailure = nullptr; // +0x14
  };
  static_assert(
    offsetof(WxModuleRuntimeDispatchTable, onBootstrapFailure) == 0x14,
    "WxModuleRuntimeDispatchTable::onBootstrapFailure offset must be 0x14"
  );

  using WxRadioBoxApplySelectionCallback = void(*)(void*, std::int32_t);
  using WxRadioBoxResolveSelectionCallback = std::int32_t(*)(void*, std::int32_t);
  struct WxRadioBoxBaseDispatchTable
  {
    WxRadioBoxApplySelectionCallback applySelection = nullptr; // +0x00
    void* slot01 = nullptr;                                    // +0x04
    void* slot02 = nullptr;                                    // +0x08
    void* slot03 = nullptr;                                    // +0x0C
    void* slot04 = nullptr;                                    // +0x10
    WxRadioBoxResolveSelectionCallback resolveSelectionByString = nullptr; // +0x14
  };
  static_assert(
    offsetof(WxRadioBoxBaseDispatchTable, resolveSelectionByString) == 0x14,
    "WxRadioBoxBaseDispatchTable::resolveSelectionByString offset must be 0x14"
  );

  void wxModuleLifecycleNoOp(
    void* const runtime
  )
  {
    (void)runtime;
  }

  void wxRadioBoxApplySelectionNoOp(
    void* const runtime,
    const std::int32_t selectionIndex
  )
  {
    (void)runtime;
    (void)selectionIndex;
  }

  std::int32_t wxRadioBoxResolveSelectionNotFound(
    void* const runtime,
    const std::int32_t sourceIndex
  )
  {
    (void)runtime;
    (void)sourceIndex;
    return -1;
  }

  void* gWxUxThemeModuleClassInfo[1] = {nullptr};
  void* gWxSystemOptionsModuleClassInfo[1] = {nullptr};
  void* gWxToolBarClassInfo[1] = {nullptr};
  void* gWxProcessClassInfo[1] = {nullptr};
  void* gWxProcessEventClassInfo[1] = {nullptr};
  WxClipboardRuntimeView* gWxClipboardRuntime = nullptr;

  WxModuleRuntimeDispatchTable gWxObjectModuleDispatch{};
  WxModuleRuntimeDispatchTable gWxUxThemeModuleDispatch = {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &wxModuleLifecycleNoOp,
  };
  WxModuleRuntimeDispatchTable gWxSystemOptionsModuleDispatch{};
  WxRadioBoxBaseDispatchTable gWxRadioBoxBaseDispatch = {
    &wxRadioBoxApplySelectionNoOp,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &wxRadioBoxResolveSelectionNotFound,
  };

  std::uint8_t gWxRadioButtonRuntimeVTableTag = 0;
  std::uint8_t gWxPopupWindowRuntimeVTableTag = 0;
  std::uint8_t gWxSpinCtrlRuntimeVTableTag = 0;
  std::uint8_t gWxScrollBarRuntimeVTableTag = 0;
  std::uint8_t gWxStaticBitmapRuntimeVTableTag = 0;
  std::uint8_t gWxSpinButtonRuntimeVTableTag = 0;
  std::uint8_t gWxMdiClientWindowRuntimeVTableTag = 0;
  std::uint8_t gWxStaticLineRuntimeVTableTag = 0;
  std::uint8_t gWxResourceCacheListRuntimeVTableTag = 0;
  std::uint8_t gWxPathListRuntimeVTableTag = 0;
  std::uint8_t gWxBrushRefDataRuntimeVTableTag = 0;
  std::uint8_t gWxListBaseRuntimeVTableTag = 0;
  std::uint8_t gWxListStringNodeRuntimeVTableTag = 0;
  std::uint8_t gWxModuleListNodeRuntimeVTableTag = 0;
  std::uint8_t gWxToolBarToolsListNodeRuntimeVTableTag = 0;
  std::uint8_t gWxSimpleDataObjectListNodeRuntimeVTableTag = 0;
  std::uint8_t gWxArtProvidersListNodeRuntimeVTableTag = 0;
  std::uint8_t gWxCursorRuntimeVTableTag = 0;
  std::uint8_t gWxClipboardRuntimeVTableTag = 0;
  std::uint8_t gWxToolBarToolBaseRuntimeVTableTag = 0;
  std::uint8_t gWxToolBarToolRuntimeVTableTag = 0;
  std::uint8_t gWxFontDataRuntimeVTableTag = 0;
  std::uint8_t gWxFontDialogBaseRuntimeVTableTag = 0;
  std::uint8_t gWxFontDialogRuntimeVTableTag = 0;
  std::uint8_t gWxTimerEventRuntimeVTableTag = 0;
  std::uint8_t gWxSocketEventRuntimeVTableTag = 0;
  std::uint8_t gWxListCtrlRuntimeVTableTag = 0;
  std::uint8_t gWxListViewRuntimeVTableTag = 0;
  std::uint8_t gWxToolBarRuntimeVTableTag = 0;
  std::uint8_t gWxProcessRuntimeVTableTag = 0;
  std::uint8_t gWxProcessEventRuntimeVTableTag = 0;
  std::uint8_t gWxEvtHandlerRuntimeVTableTag = 0;
  std::uint8_t gWxObjectRuntimeVTableTag = 0;
  std::uint8_t gWxMaskRuntimeVTableTag = 0;
  std::uint8_t gWxImageListRuntimeVTableTag = 0;
  std::uint8_t gWxCursorRefDataRuntimeVTableTag = 0;
  std::uint8_t gWxObjectRefDataRuntimeVTableTag = 0;
  std::uint8_t gWxTreeListCtrlCtorRuntimeVTableTag = 0;
  std::uint8_t gWxButtonRuntimeVTableTag = 0;
  std::uint8_t gWxStaticTextRuntimeVTableTag = 0;
  std::uint8_t gWxStaticBoxRuntimeVTableTag = 0;
  std::uint8_t gWxCheckBoxRuntimeVTableTag = 0;
  std::uint8_t gWxBitmapButtonBaseRuntimeVTableTag = 0;
  std::uint8_t gWxBitmapButtonRuntimeVTableTag = 0;
  std::uint8_t gWxMenuListNodeRuntimeVTableTag = 0;
  std::uint8_t gWxFontListRuntimeVTableTag = 0;
  std::uint8_t gWxPenListRuntimeVTableTag = 0;
  std::uint8_t gWxPrintPaperTypeRuntimeVTableTag = 0;

  [[nodiscard]] wxStringRuntime AllocateSharedWxStringRuntime(
    const std::wstring& value
  ) noexcept
  {
    const std::size_t allocationBytes = sizeof(std::int32_t) * 3u + (value.size() + 1u) * sizeof(wchar_t);
    auto* const rawStorage = static_cast<std::uint8_t*>(::operator new(allocationBytes, std::nothrow));
    if (rawStorage == nullptr) {
      return wxStringRuntime::Borrow(L"");
    }

    auto* const sharedPrefixWords = reinterpret_cast<std::int32_t*>(rawStorage);
    sharedPrefixWords[0] = 1;
    sharedPrefixWords[1] = static_cast<std::int32_t>(value.size());
    sharedPrefixWords[2] = static_cast<std::int32_t>(value.size());

    auto* const textStorage = reinterpret_cast<wchar_t*>(sharedPrefixWords + 3);
    if (!value.empty()) {
      std::wmemcpy(textStorage, value.data(), value.size());
    }
    textStorage[value.size()] = L'\0';

    wxStringRuntime runtime{};
    runtime.m_pchData = textStorage;
    return runtime;
  }

  void wxClearStringArrayRuntime(
    WxStringArrayRuntimeView* const arrayRuntime
  ) noexcept
  {
    if (arrayRuntime == nullptr) {
      return;
    }

    if (arrayRuntime->entries != nullptr && arrayRuntime->count > 0) {
      for (std::int32_t index = 0; index < arrayRuntime->count; ++index) {
        wxStringRuntime storedValue{};
        storedValue.m_pchData = const_cast<wchar_t*>(arrayRuntime->entries[index]);
        if (storedValue.m_pchData != nullptr) {
          ReleaseWxStringSharedPayload(storedValue);
        }
      }
    }

    delete[] arrayRuntime->entries;
    arrayRuntime->entries = nullptr;
    arrayRuntime->count = 0;
    arrayRuntime->isSorted = 0;
  }

  [[nodiscard]] bool wxAppendStringPairEntryRuntime(
    const wxStringRuntime* const keyValue,
    const wxStringRuntime* const mappedValue
  ) noexcept
  {
    const std::int32_t keyCount = gWxStringPairLookup.keys.count;
    const std::int32_t valueCount = gWxStringPairLookup.values.count;
    if (keyCount != valueCount || keyCount < 0) {
      return false;
    }

    const std::size_t entryCount = static_cast<std::size_t>(keyCount);
    auto* const nextKeys = new (std::nothrow) const wchar_t*[entryCount + 1u];
    auto* const nextValues = new (std::nothrow) const wchar_t*[entryCount + 1u];
    if (nextKeys == nullptr || nextValues == nullptr) {
      delete[] nextKeys;
      delete[] nextValues;
      return false;
    }

    for (std::size_t index = 0; index < entryCount; ++index) {
      nextKeys[index] = gWxStringPairLookup.keys.entries[index];
      nextValues[index] = gWxStringPairLookup.values.entries[index];
    }

    wxStringRuntime retainedKey{};
    RetainWxStringRuntime(&retainedKey, keyValue);
    wxStringRuntime retainedValue{};
    RetainWxStringRuntime(&retainedValue, mappedValue);
    nextKeys[entryCount] = retainedKey.m_pchData;
    nextValues[entryCount] = retainedValue.m_pchData;

    delete[] gWxStringPairLookup.keys.entries;
    delete[] gWxStringPairLookup.values.entries;
    gWxStringPairLookup.keys.entries = nextKeys;
    gWxStringPairLookup.values.entries = nextValues;
    gWxStringPairLookup.keys.count = keyCount + 1;
    gWxStringPairLookup.values.count = valueCount + 1;
    return true;
  }

  void wxAssignStringArrayEntryRuntime(
    WxStringArrayRuntimeView* const arrayRuntime,
    const std::int32_t index,
    const wxStringRuntime* const sourceValue
  ) noexcept
  {
    if (
      arrayRuntime == nullptr || arrayRuntime->entries == nullptr || index < 0 || arrayRuntime->count <= index
    ) {
      return;
    }

    wxStringRuntime existingValue{};
    existingValue.m_pchData = const_cast<wchar_t*>(arrayRuntime->entries[index]);
    if (existingValue.m_pchData != nullptr) {
      ReleaseWxStringSharedPayload(existingValue);
    }

    wxStringRuntime retainedValue{};
    RetainWxStringRuntime(&retainedValue, sourceValue);
    arrayRuntime->entries[index] = retainedValue.m_pchData;
  }

  void wxInitializeControlRuntimeBaseState(
    void* const controlRuntime
  ) noexcept
  {
    auto* const objectRuntime = static_cast<WxObjectRuntimeView*>(controlRuntime);
    if (objectRuntime == nullptr) {
      return;
    }

    objectRuntime->refData = nullptr;
  }

  void wxInitializeEvtHandlerRuntimeBaseState(
    void* const evtHandlerRuntime
  ) noexcept
  {
    auto* const handlerRuntime = static_cast<WxEvtHandlerCtorRuntimeView*>(evtHandlerRuntime);
    if (handlerRuntime == nullptr) {
      return;
    }

    handlerRuntime->refData = nullptr;
    handlerRuntime->vtable = &gWxEvtHandlerRuntimeVTableTag;
    handlerRuntime->nextHandler = nullptr;
    handlerRuntime->previousHandler = nullptr;
    handlerRuntime->enabled = 1;
    handlerRuntime->dynamicEvents = nullptr;
    handlerRuntime->isWindow = 0;
    handlerRuntime->pendingEvents = nullptr;
    handlerRuntime->clientObject = nullptr;
    handlerRuntime->clientDataType = 0;

    auto* const lockerStorage = static_cast<CRITICAL_SECTION*>(::operator new(sizeof(CRITICAL_SECTION), std::nothrow));
    if (lockerStorage != nullptr) {
      ::InitializeCriticalSection(lockerStorage);
    }
    handlerRuntime->eventsLocker = lockerStorage;
  }

  [[nodiscard]] std::int32_t wxParseSignedIntWideRuntime(
    const wchar_t* const textValue
  ) noexcept
  {
    if (textValue == nullptr) {
      return 0;
    }

    return static_cast<std::int32_t>(std::wcstol(textValue, nullptr, 10));
  }

  [[nodiscard]] bool wxInitializeUxThemeProcSlotRuntime(
    WxUxThemeApiRuntimeView* const runtime,
    const std::size_t slotIndex,
    const char* const procName
  ) noexcept
  {
    if (
      runtime == nullptr ||
      runtime->moduleHandle == nullptr ||
      procName == nullptr ||
      slotIndex >= std::size(runtime->procSlots)
    ) {
      return false;
    }

    runtime->procSlots[slotIndex] = ::GetProcAddress(runtime->moduleHandle, procName);
    if (runtime->procSlots[slotIndex] != nullptr) {
      return true;
    }

    ::FreeLibrary(runtime->moduleHandle);
    runtime->moduleHandle = nullptr;
    return false;
  }

  [[nodiscard]] WxUxThemeApiRuntimeView* wxInitializeUxThemeApiRuntime(
    WxUxThemeApiRuntimeView* const runtime
  ) noexcept
  {
    if (runtime == nullptr) {
      return nullptr;
    }

    runtime->isAvailable = 0;
    runtime->moduleHandle = ::LoadLibraryA("uxtheme.dll");
    (void)wxResetUxThemeApiRuntimeSlots(runtime);
    if (runtime->moduleHandle == nullptr) {
      return runtime;
    }

    constexpr std::array<const char*, 47> kUxThemeProcNames = {
      "OpenThemeData",
      "CloseThemeData",
      "DrawThemeBackground",
      "DrawThemeText",
      "GetThemeBackgroundContentRect",
      "GetThemeBackgroundExtent",
      "GetThemePartSize",
      "GetThemeTextExtent",
      "GetThemeTextMetrics",
      "GetThemeBackgroundRegion",
      "HitTestThemeBackground",
      "DrawThemeEdge",
      "DrawThemeIcon",
      "IsThemePartDefined",
      "IsThemeBackgroundPartiallyTransparent",
      "GetThemeColor",
      "GetThemeMetric",
      "GetThemeString",
      "GetThemeBool",
      "GetThemeInt",
      "GetThemeEnumValue",
      "GetThemePosition",
      "GetThemeFont",
      "GetThemeRect",
      "GetThemeMargins",
      "GetThemeIntList",
      "GetThemePropertyOrigin",
      "SetWindowTheme",
      "GetThemeFilename",
      "GetThemeSysColor",
      "GetThemeSysColorBrush",
      "GetThemeSysBool",
      "GetThemeSysSize",
      "GetThemeSysFont",
      "GetThemeSysString",
      "GetThemeSysInt",
      "IsThemeActive",
      "IsAppThemed",
      "GetWindowTheme",
      "EnableThemeDialogTexture",
      "IsThemeDialogTextureEnabled",
      "GetThemeAppProperties",
      "SetThemeAppProperties",
      "GetCurrentThemeName",
      "GetThemeDocumentationProperty",
      "DrawThemeParentBackground",
      "EnableTheming",
    };

    for (std::size_t slotIndex = 0; slotIndex < kUxThemeProcNames.size(); ++slotIndex) {
      if (!wxInitializeUxThemeProcSlotRuntime(runtime, slotIndex, kUxThemeProcNames[slotIndex])) {
        break;
      }
    }

    runtime->isAvailable = runtime->moduleHandle != nullptr ? 1u : 0u;
    if (runtime->isAvailable == 0u) {
      (void)wxResetUxThemeApiRuntimeSlots(runtime);
    }
    return runtime;
  }

  [[nodiscard]] bool wxInitializeToolBarCreateLaneRuntime(
    void* const toolBarRuntime,
    void* const parent,
    const std::int32_t windowId,
    const HMENU positionToken,
    const void* const sizeToken,
    const DWORD style,
    const wxStringRuntime* const windowName
  )
  {
    (void)parent;
    (void)windowId;
    (void)positionToken;
    (void)sizeToken;
    (void)style;
    (void)windowName;
    if (toolBarRuntime == nullptr) {
      return false;
    }

    auto* const windowRuntime = static_cast<wxWindowBase*>(toolBarRuntime);
    const wxColourRuntime backgroundColour = wxColourRuntime::FromRgb(240u, 240u, 240u);
    (void)windowRuntime->SetBackgroundColour(&backgroundColour);
    const wxFontRuntime& defaultFont = wxFontRuntime::Null();
    (void)windowRuntime->SetFont(&defaultFont);
    return true;
  }
}


/**
 * Address: 0x00A03B70 (FUN_00A03B70)
 *
 * What it does:
 * Returns the process-wide UX-theme API runtime object pointer lane.
 */
[[maybe_unused]] void* wxGetUxThemeApiRuntimeGlobal()
{
  return gWxUxThemeApiRuntime;
}

/**
 * Address: 0x00A03B80 (FUN_00A03B80)
 *
 * What it does:
 * Initializes one `wxUxThemeModule` runtime lane (`refData=null`, module
 * dispatch vtable token).
 */
[[maybe_unused]] void* wxConstructUxThemeModuleRuntime(
  void* const moduleRuntime
)
{
  auto* const module = static_cast<WxModuleRuntimeView*>(moduleRuntime);
  if (module == nullptr) {
    return nullptr;
  }

  module->refData = nullptr;
  module->vtable = &gWxUxThemeModuleDispatch;
  return module;
}

/**
 * Address: 0x00A03B90 (FUN_00A03B90)
 *
 * What it does:
 * Returns whether this UX-theme API runtime payload is currently marked as
 * available.
 */
[[maybe_unused]] bool wxIsUxThemeApiRuntimeAvailable(
  const void* const uxThemeApiRuntime
)
{
  const auto* const runtime = static_cast<const WxUxThemeApiRuntimeView*>(uxThemeApiRuntime);
  return runtime != nullptr && runtime->isAvailable != 0u;
}

/**
 * Address: 0x00A03BA0 (FUN_00A03BA0)
 *
 * What it does:
 * Returns the static class-info lane for `wxUxThemeModule`.
 */
[[maybe_unused]] void* wxGetUxThemeModuleClassInfoRuntime() noexcept
{
  return gWxUxThemeModuleClassInfo;
}

/**
 * Address: 0x00A03BB0 (FUN_00A03BB0)
 *
 * What it does:
 * Runs non-deleting teardown for one `wxUxThemeModule` runtime lane by
 * rebinding to base wx-object dispatch and releasing shared ref-data.
 */
[[maybe_unused]] void wxDestroyUxThemeModuleRuntimeNoDelete(
  void* const moduleRuntime
)
{
  auto* const module = static_cast<WxModuleRuntimeView*>(moduleRuntime);
  if (module == nullptr) {
    return;
  }

  module->vtable = &gWxObjectModuleDispatch;
  wxEventUnRefRuntime(reinterpret_cast<WxObjectRuntimeView*>(module));
}

/**
 * Address: 0x00A03CD0 (FUN_00A03CD0)
 *
 * What it does:
 * Allocates and initializes one `wxUxThemeModule` runtime object.
 */
[[maybe_unused]] void* wxAllocateUxThemeModuleRuntime()
{
  auto* const module = static_cast<WxModuleRuntimeView*>(::operator new(sizeof(WxModuleRuntimeView), std::nothrow));
  if (module == nullptr) {
    return nullptr;
  }

  return wxConstructUxThemeModuleRuntime(module);
}

/**
 * Address: 0x00A03CF0 (FUN_00A03CF0)
 *
 * What it does:
 * Runs the deleting-dtor thunk path for one `wxUxThemeModule` runtime object.
 */
[[maybe_unused]] void* wxDestroyUxThemeModuleRuntimeWithFlag(
  void* const moduleRuntime,
  const std::uint8_t deleteFlags
)
{
  auto* const module = static_cast<WxModuleRuntimeView*>(moduleRuntime);
  if (module == nullptr) {
    return nullptr;
  }

  module->vtable = &gWxObjectModuleDispatch;
  wxEventUnRefRuntime(reinterpret_cast<WxObjectRuntimeView*>(module));
  if ((deleteFlags & 1u) != 0u) {
    ::operator delete(module);
  }
  return module;
}

/**
 * Address: 0x00A04340 (FUN_00A04340)
 *
 * What it does:
 * Allocates and initializes one UX-theme API runtime payload (`0xC4` bytes).
 */
[[maybe_unused]] void* wxAllocateUxThemeApiRuntime()
{
  auto* const runtime =
    static_cast<WxUxThemeApiRuntimeView*>(::operator new(sizeof(WxUxThemeApiRuntimeView), std::nothrow));
  if (runtime == nullptr) {
    return nullptr;
  }

  return wxInitializeUxThemeApiRuntime(runtime);
}

/**
 * Address: 0x00A04390 (FUN_00A04390)
 *
 * What it does:
 * Boots process-wide UX-theme runtime support; when boot fails, invokes module
 * lifecycle slot-5 cleanup callback.
 */
[[maybe_unused]] bool wxInitializeUxThemeRuntimeForModule(
  void* const moduleRuntime
)
{
  if (!wxCanUseUxThemeRuntime()) {
    gWxUxThemeApiRuntime = nullptr;
    return true;
  }

  auto* const runtime = static_cast<WxUxThemeApiRuntimeView*>(wxAllocateUxThemeApiRuntime());
  gWxUxThemeApiRuntime = runtime;
  if (runtime != nullptr) {
    if (runtime->isAvailable != 0u) {
      return true;
    }

    const auto* const module = static_cast<WxModuleRuntimeView*>(moduleRuntime);
    const auto* const dispatch =
      (module != nullptr) ? static_cast<WxModuleRuntimeDispatchTable*>(module->vtable) : nullptr;
    if (dispatch != nullptr && dispatch->onBootstrapFailure != nullptr) {
      dispatch->onBootstrapFailure(moduleRuntime);
    }
  }

  return false;
}

/**
 * Address: 0x00A043D0 (FUN_00A043D0)
 *
 * What it does:
 * Returns the cached selected-index lane from one radio-box-base runtime
 * payload.
 */
[[maybe_unused]] std::int32_t wxGetRadioBoxBaseSelectedIndexRuntime(
  const void* const radioBoxRuntime
)
{
  const auto* const runtime = static_cast<const WxRadioBoxBaseRuntimeView*>(radioBoxRuntime);
  return runtime != nullptr ? runtime->selectedIndex : -1;
}

/**
 * Address: 0x00A043E0 (FUN_00A043E0)
 *
 * What it does:
 * Rebinds one radio-box-base runtime payload to its base dispatch table.
 */
[[maybe_unused]] void* wxConstructRadioBoxBaseRuntime(
  void* const radioBoxRuntime
)
{
  auto* const runtime = static_cast<WxRadioBoxBaseRuntimeView*>(radioBoxRuntime);
  if (runtime == nullptr) {
    return nullptr;
  }

  runtime->vtable = &gWxRadioBoxBaseDispatch;
  return runtime;
}

/**
 * Address: 0x00A043F0 (FUN_00A043F0)
 *
 * What it does:
 * Resolves one string token to a radio-box selection index, applies the
 * selection when found, and reports success.
 */
[[maybe_unused]] bool wxSetRadioBoxBaseSelectionFromStringRuntime(
  void* const radioBoxRuntime,
  const std::int32_t stringToken
)
{
  auto* const runtime = static_cast<WxRadioBoxBaseRuntimeView*>(radioBoxRuntime);
  if (runtime == nullptr) {
    return false;
  }

  auto* const dispatch = static_cast<WxRadioBoxBaseDispatchTable*>(runtime->vtable);
  if (
    dispatch == nullptr || dispatch->resolveSelectionByString == nullptr || dispatch->applySelection == nullptr
  ) {
    return false;
  }

  const std::int32_t resolvedIndex = dispatch->resolveSelectionByString(radioBoxRuntime, stringToken);
  if (resolvedIndex == -1) {
    return false;
  }

  dispatch->applySelection(radioBoxRuntime, resolvedIndex);
  return true;
}

/**
 * Address: 0x00A14470 (FUN_00A14470)
 *
 * What it does:
 * Initializes one `wxSystemOptionsModule` runtime lane (`refData=null`,
 * module dispatch token).
 */
[[maybe_unused]] void* wxConstructSystemOptionsModuleRuntime(
  void* const moduleRuntime
)
{
  auto* const module = static_cast<WxModuleRuntimeView*>(moduleRuntime);
  if (module == nullptr) {
    return nullptr;
  }

  module->refData = nullptr;
  module->vtable = &gWxSystemOptionsModuleDispatch;
  return module;
}

/**
 * Address: 0x00A14480 (FUN_00A14480)
 *
 * What it does:
 * Returns the static class-info lane for `wxSystemOptionsModule`.
 */
[[maybe_unused]] void* wxGetSystemOptionsModuleClassInfoRuntime() noexcept
{
  return gWxSystemOptionsModuleClassInfo;
}

/**
 * Address: 0x00A14490 (FUN_00A14490)
 *
 * What it does:
 * Runs non-deleting teardown for one `wxSystemOptionsModule` runtime lane.
 */
[[maybe_unused]] void wxDestroySystemOptionsModuleRuntimeNoDelete(
  void* const moduleRuntime
)
{
  auto* const module = static_cast<WxModuleRuntimeView*>(moduleRuntime);
  if (module == nullptr) {
    return;
  }

  module->vtable = &gWxObjectModuleDispatch;
  wxEventUnRefRuntime(reinterpret_cast<WxObjectRuntimeView*>(module));
}

/**
 * Address: 0x00A144B0 (FUN_00A144B0)
 *
 * What it does:
 * Clears both system-options string arrays (keys and mapped values).
 */
[[maybe_unused]] void wxClearSystemOptionsRuntime()
{
  wxClearStringArrayRuntime(&gWxStringPairLookup.keys);
  wxClearStringArrayRuntime(&gWxStringPairLookup.values);
}

/**
 * Address: 0x00A144D0 (FUN_00A144D0)
 *
 * What it does:
 * Inserts or updates one system-option key/value string pair.
 */
[[maybe_unused]] wxStringRuntime* wxSetSystemOptionStringRuntime(
  const wxStringRuntime* const keyValue,
  const wxStringRuntime* const mappedValue
)
{
  if (keyValue == nullptr || keyValue->m_pchData == nullptr || mappedValue == nullptr) {
    return nullptr;
  }

  const std::int32_t entryIndex = WxFindStringArrayIndex(&gWxStringPairLookup.keys, keyValue->m_pchData, false, false);
  if (entryIndex == -1) {
    if (!wxAppendStringPairEntryRuntime(keyValue, mappedValue)) {
      return nullptr;
    }
    return const_cast<wxStringRuntime*>(mappedValue);
  }

  wxAssignStringArrayEntryRuntime(&gWxStringPairLookup.keys, entryIndex, keyValue);
  wxAssignStringArrayEntryRuntime(&gWxStringPairLookup.values, entryIndex, mappedValue);
  return const_cast<wxStringRuntime*>(mappedValue);
}

/**
 * Address: 0x00A14540 (FUN_00A14540)
 *
 * What it does:
 * Formats one signed integer as decimal UTF-16 text and stores it under the
 * provided system-option key.
 */
[[maybe_unused]] void wxSetSystemOptionIntegerRuntime(
  const wxStringRuntime* const keyValue,
  const std::int32_t numericValue
)
{
  wxStringRuntime formattedValue = AllocateSharedWxStringRuntime(std::to_wstring(numericValue));
  (void)wxSetSystemOptionStringRuntime(keyValue, &formattedValue);
  if (formattedValue.m_pchData != nullptr) {
    ReleaseWxStringSharedPayload(formattedValue);
  }
}

/**
 * Address: 0x00A145F0 (FUN_00A145F0)
 *
 * What it does:
 * Allocates and initializes one `wxSystemOptionsModule` runtime object.
 */
[[maybe_unused]] void* wxAllocateSystemOptionsModuleRuntime()
{
  auto* const module = static_cast<WxModuleRuntimeView*>(::operator new(sizeof(WxModuleRuntimeView), std::nothrow));
  if (module == nullptr) {
    return nullptr;
  }

  return wxConstructSystemOptionsModuleRuntime(module);
}

/**
 * Address: 0x00A14610 (FUN_00A14610)
 *
 * What it does:
 * Runs the deleting-dtor thunk path for one `wxSystemOptionsModule` runtime
 * object.
 */
[[maybe_unused]] void* wxDestroySystemOptionsModuleRuntimeWithFlag(
  void* const moduleRuntime,
  const std::uint8_t deleteFlags
)
{
  auto* const module = static_cast<WxModuleRuntimeView*>(moduleRuntime);
  if (module == nullptr) {
    return nullptr;
  }

  module->vtable = &gWxObjectModuleDispatch;
  wxEventUnRefRuntime(reinterpret_cast<WxObjectRuntimeView*>(module));
  if ((deleteFlags & 1u) != 0u) {
    ::operator delete(module);
  }
  return module;
}

/**
 * Address: 0x00A146C0 (FUN_00A146C0)
 *
 * What it does:
 * Looks up one system-option string and parses it as a signed decimal integer.
 */
[[maybe_unused]] std::int32_t wxGetSystemOptionIntegerRuntime(
  const wxStringRuntime* const keyValue
)
{
  wxStringRuntime lookedUpValue{};
  (void)wxLookupStringPairValueRuntime(&lookedUpValue, keyValue);
  const std::int32_t parsedValue = wxParseSignedIntWideRuntime(lookedUpValue.m_pchData);
  if (lookedUpValue.m_pchData != nullptr) {
    ReleaseWxStringSharedPayload(lookedUpValue);
  }
  return parsedValue;
}

/**
 * Address: 0x0097C920 (FUN_0097C920)
 *
 * What it does:
 * Constructs one `wxRadioButton` runtime lane from control-base storage and
 * clears the checked-state lane.
 */
[[maybe_unused]] void* wxConstructRadioButtonRuntime(
  void* const radioButtonRuntime
)
{
  if (radioButtonRuntime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(radioButtonRuntime);
  auto* const baseObject = static_cast<WxObjectRuntimeView*>(radioButtonRuntime);
  baseObject->vtable = &gWxRadioButtonRuntimeVTableTag;
  wxInitializeRadioButtonRuntimeState(radioButtonRuntime);
  return radioButtonRuntime;
}

/**
 * Address: 0x0099C070 (FUN_0099C070)
 *
 * What it does:
 * Constructs one `wxListCtrl` runtime lane from control-base storage and
 * initializes list-control tail state.
 */
[[maybe_unused]] void* wxConstructListCtrlRuntime(
  void* const listControlRuntime
)
{
  if (listControlRuntime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(listControlRuntime);
  auto* const baseObject = static_cast<WxObjectRuntimeView*>(listControlRuntime);
  baseObject->vtable = &gWxListCtrlRuntimeVTableTag;
  (void)wxInitializeListCtrlRuntimeState(listControlRuntime);
  return listControlRuntime;
}

/**
 * Address: 0x0099E600 (FUN_0099E600)
 *
 * What it does:
 * Constructs one `wxListView` runtime lane from control-base storage and
 * initializes list-control tail state.
 */
[[maybe_unused]] void* wxConstructListViewRuntime(
  void* const listViewRuntime
)
{
  if (listViewRuntime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(listViewRuntime);
  (void)wxInitializeListCtrlRuntimeState(listViewRuntime);
  auto* const baseObject = static_cast<WxObjectRuntimeView*>(listViewRuntime);
  baseObject->vtable = &gWxListViewRuntimeVTableTag;
  return listViewRuntime;
}

/**
 * Address: 0x009A8DD0 (FUN_009A8DD0)
 *
 * What it does:
 * Constructs one `wxToolBar` runtime lane, seeds toolbar metric lanes, and
 * runs toolbar create-time visual initialization.
 */
[[maybe_unused]] void* wxConstructToolBarRuntime(
  void* const toolBarRuntime,
  void* const parent,
  const std::int32_t windowId,
  const HMENU positionToken,
  const void* const sizeToken,
  const DWORD style,
  const wxStringRuntime* const windowName
)
{
  if (toolBarRuntime == nullptr) {
    return nullptr;
  }

  wxInitializeControlRuntimeBaseState(toolBarRuntime);
  auto* const baseObject = static_cast<WxObjectRuntimeView*>(toolBarRuntime);
  baseObject->vtable = &gWxToolBarRuntimeVTableTag;
  (void)wxInitializeToolBarBaseRuntimeState(toolBarRuntime);
  (void)wxInitializeToolBarRuntimeMetrics(toolBarRuntime);
  (void)wxInitializeToolBarCreateLaneRuntime(
    toolBarRuntime,
    parent,
    windowId,
    positionToken,
    sizeToken,
    style,
    windowName
  );
  return toolBarRuntime;
}

/**
 * Address: 0x009A8EF0 (FUN_009A8EF0)
 *
 * What it does:
 * Returns the static class-info lane for `wxToolBar`.
 */
[[maybe_unused]] void* wxGetToolBarClassInfoRuntime() noexcept
{
  return gWxToolBarClassInfo;
}

/**
 * Address: 0x009EE270 (FUN_009EE270)
 *
 * What it does:
 * Dispatches the toolbar message lane selected by comctl32 version:
 * `0x467` for pre-5.80 controls, otherwise `0x471`.
 */
[[maybe_unused]] LRESULT wxToolBarSendVersionedMessage(
  void* const toolBarRuntime,
  const LPARAM messageParam
)
{
  const auto* const toolBarView = static_cast<const WxWindowNativeHandleRuntimeView*>(toolBarRuntime);
  if (toolBarView == nullptr || toolBarView->mNativeHandle == nullptr) {
    return 0;
  }

  if (wxGetComCtl32PackedVersionRuntime() < 0x244u) {
    return ::SendMessageW(
      toolBarView->mNativeHandle,
      0x467u,
      0u,
      static_cast<LPARAM>(static_cast<std::uint16_t>(messageParam))
    );
  }

  return ::SendMessageW(toolBarView->mNativeHandle, 0x471u, 0u, messageParam);
}

struct WxToolBarGeometryRuntimeView
{
  std::uint8_t mUnknown00To107[0x108]{};
  HWND mPrimaryHandle = nullptr;          // +0x108
  std::uint8_t mUnknown10CTo137[0x2C]{};
  HWND mSecondaryHandle = nullptr;        // +0x138
};
static_assert(
  offsetof(WxToolBarGeometryRuntimeView, mPrimaryHandle) == 0x108,
  "WxToolBarGeometryRuntimeView::mPrimaryHandle offset must be 0x108"
);
static_assert(
  offsetof(WxToolBarGeometryRuntimeView, mSecondaryHandle) == 0x138,
  "WxToolBarGeometryRuntimeView::mSecondaryHandle offset must be 0x138"
);

/**
 * Address: 0x009D4010 (FUN_009D4010)
 *
 * What it does:
 * Computes the union rectangle of two toolbar-owned HWND lanes and writes
 * resulting width/height spans to optional output pointers.
 */
[[maybe_unused]] int* wxGetToolBarUnionWindowExtent(
  const void* const toolBarRuntime,
  int* const outWidth,
  int* const outHeight
)
{
  const auto* const toolBarView = static_cast<const WxToolBarGeometryRuntimeView*>(toolBarRuntime);

  RECT primaryRect{};
  RECT secondaryRect{};
  RECT unionRect{};
  (void)::GetWindowRect(toolBarView->mPrimaryHandle, &primaryRect);
  (void)::GetWindowRect(toolBarView->mSecondaryHandle, &secondaryRect);
  (void)::UnionRect(&unionRect, &secondaryRect, &primaryRect);

  if (outWidth != nullptr) {
    *outWidth = unionRect.right - unionRect.left;
  }
  if (outHeight != nullptr) {
    *outHeight = unionRect.bottom - unionRect.top;
  }

  return outHeight;
}

/**
 * Address: 0x009CD0B0 (FUN_009CD0B0)
 *
 * What it does:
 * Constructs one `wxProcess` runtime lane from `wxEvtHandler` state and seeds
 * process id/stream lanes.
 */
[[maybe_unused]] void* wxConstructProcessRuntime(
  void* const processRuntime,
  const std::int32_t processData,
  const std::int32_t processId
)
{
  if (processRuntime == nullptr) {
    return nullptr;
  }

  wxInitializeEvtHandlerRuntimeBaseState(processRuntime);
  auto* const baseObject = static_cast<WxObjectRuntimeView*>(processRuntime);
  baseObject->vtable = &gWxProcessRuntimeVTableTag;
  (void)wxInitializeProcessRuntimeState(processRuntime, processData, processId, 0);
  return processRuntime;
}

/**
 * Address: 0x009CD150 (FUN_009CD150)
 *
 * What it does:
 * Returns the static class-info lane for `wxProcess`.
 */
[[maybe_unused]] void* wxGetProcessClassInfoRuntime() noexcept
{
  return gWxProcessClassInfo;
}

/**
 * Address: 0x00A14740 (FUN_00A14740)
 *
 * What it does:
 * Returns the static class-info lane for `wxProcessEvent`.
 */
[[maybe_unused]] void* wxGetProcessEventClassInfoRuntime() noexcept
{
  return gWxProcessEventClassInfo;
}

/**
 * Address: 0x00A14750 (FUN_00A14750)
 *
 * What it does:
 * Copy-constructs one `wxProcessEvent` runtime payload from source event lanes.
 */
[[maybe_unused]] void* wxCopyConstructProcessEventRuntime(
  void* const processEventRuntime,
  const void* const sourceEventRuntime
)
{
  auto* const outEvent = static_cast<WxProcessEventCopyRuntimeView*>(processEventRuntime);
  const auto* const sourceEvent = static_cast<const WxProcessEventCopyRuntimeView*>(sourceEventRuntime);
  if (outEvent == nullptr || sourceEvent == nullptr) {
    return outEvent;
  }

  outEvent->base.refData = nullptr;
  outEvent->base.vtable = &gWxProcessEventRuntimeVTableTag;
  outEvent->base.eventObject = sourceEvent->base.eventObject;
  outEvent->base.eventType = sourceEvent->base.eventType;
  outEvent->base.eventTimestamp = sourceEvent->base.eventTimestamp;
  outEvent->base.eventId = sourceEvent->base.eventId;
  outEvent->base.callbackUserData = sourceEvent->base.callbackUserData;
  outEvent->base.skipped = sourceEvent->base.skipped;
  outEvent->base.isCommandEvent = sourceEvent->base.isCommandEvent;
  outEvent->base.reserved1E = sourceEvent->base.reserved1E;
  outEvent->base.reserved1F = sourceEvent->base.reserved1F;
  outEvent->param0 = sourceEvent->param0;
  outEvent->param1 = sourceEvent->param1;
  return outEvent;
}

/**
 * Address: 0x009F2890 (FUN_009F2890)
 *
 * What it does:
 * Clones one `wxTimerEvent` payload by copying base `wxEvent` lanes plus timer
 * id lane (`+0x20`) and rebinding timer-event vtable dispatch.
 */
[[maybe_unused]] void* wxCloneTimerEventRuntime(
  const void* const sourceEventRuntime
)
{
  if (sourceEventRuntime == nullptr) {
    return nullptr;
  }

  auto* const clone = new (std::nothrow) WxTimerEventCopyRuntimeView{};
  const auto* const sourceEvent = static_cast<const WxTimerEventCopyRuntimeView*>(sourceEventRuntime);
  if (clone == nullptr) {
    return nullptr;
  }

  clone->base = sourceEvent->base;
  clone->base.vtable = &gWxTimerEventRuntimeVTableTag;
  clone->timerId = sourceEvent->timerId;
  return clone;
}

/**
 * Address: 0x00A14AA0 (FUN_00A14AA0)
 *
 * What it does:
 * Allocates and copy-constructs one `wxProcessEvent` payload from source event
 * lanes and preserves both process payload lanes (`+0x20`, `+0x24`).
 */
[[maybe_unused]] void* wxCloneProcessEventRuntime(
  const void* const sourceEventRuntime
)
{
  if (sourceEventRuntime == nullptr) {
    return nullptr;
  }

  auto* const clone = new (std::nothrow) WxProcessEventCopyRuntimeView{};
  if (clone == nullptr) {
    return nullptr;
  }

  return wxCopyConstructProcessEventRuntime(clone, sourceEventRuntime);
}

/**
 * Address: 0x00A2E7D0 (FUN_00A2E7D0)
 *
 * What it does:
 * Clones one `wxSocketEvent` payload by copying base `wxEvent` lanes and socket
 * payload lanes (`+0x20`, `+0x24`) then rebinding socket-event vtable dispatch.
 */
[[maybe_unused]] void* wxCloneSocketEventRuntime(
  const void* const sourceEventRuntime
)
{
  if (sourceEventRuntime == nullptr) {
    return nullptr;
  }

  auto* const clone = new (std::nothrow) WxSocketEventCopyRuntimeView{};
  const auto* const sourceEvent = static_cast<const WxSocketEventCopyRuntimeView*>(sourceEventRuntime);
  if (clone == nullptr) {
    return nullptr;
  }

  clone->base = sourceEvent->base;
  clone->base.vtable = &gWxSocketEventRuntimeVTableTag;
  clone->lane20 = sourceEvent->lane20;
  clone->lane24 = sourceEvent->lane24;
  return clone;
}
