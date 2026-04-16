#include "BoostWrappers.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <new>

#include <boost/ptr_container/exception.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/tss.hpp>

/**
 * Address: 0x00AC6070 (FUN_00AC6070, tss_cleanup_implemented)
 *
 * What it does:
 * Placeholder TSS cleanup hook used by the boost thread-local bootstrap lane;
 * this binary variant is a no-op.
 */
void tss_cleanup_implemented()
{
}

namespace boost
{
  namespace detail
  {
#if defined(BOOST_HAS_WINTHREADS)
    /**
     * Address: 0x00AC2190 (FUN_00AC2190, boost::detail::condition_impl::condition_impl)
     *
     * What it does:
     * Initializes Win32 semaphore/mutex primitives for one condition
     * implementation lane and throws `boost::thread_resource_error` when any
     * primitive allocation fails.
     */
    condition_impl::condition_impl() : m_gate(nullptr), m_queue(nullptr), m_mutex(nullptr), m_gone(0), m_blocked(0), m_waiting(0)
    {
      m_gate = ::CreateSemaphoreA(nullptr, 1, 1, nullptr);
      m_queue = ::CreateSemaphoreA(nullptr, 0, 0x7FFFFFFF, nullptr);
      m_mutex = ::CreateMutexA(nullptr, FALSE, nullptr);

      if (m_gate != nullptr && m_queue != nullptr && m_mutex != nullptr) {
        return;
      }

      if (m_gate != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(m_gate));
        m_gate = nullptr;
      }
      if (m_queue != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(m_queue));
        m_queue = nullptr;
      }
      if (m_mutex != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(m_mutex));
        m_mutex = nullptr;
      }

      throw boost::thread_resource_error();
    }
#endif
  } // namespace detail

#if defined(BOOST_HAS_WINTHREADS)
  /**
   * Address: 0x00AC2760 (FUN_00AC2760, boost::thread::~thread)
   *
   * What it does:
   * Closes one native thread handle when this thread lane is still marked
   * joinable.
   */
  thread::~thread()
  {
    if (m_joinable) {
      ::CloseHandle(static_cast<HANDLE>(m_thread));
    }
  }
#endif

  /**
   * Address: 0x00935E30 (FUN_00935E30)
   *
   * What it does:
   * Clears one current-thread TSS payload lane and then destroys one
   * `boost::detail::tss` descriptor, preserving destructor-unwind semantics.
   */
  void ResetCurrentThreadValueAndDestroyTss(detail::tss* const tssSlot)
  {
    struct ScopedTssDestroy
    {
      detail::tss* slot;
      ~ScopedTssDestroy()
      {
        slot->~tss();
      }
    } destroyGuard{ tssSlot };

    void* const currentValue = tssSlot->get();
    if (currentValue != nullptr) {
      tssSlot->set(nullptr);
      tssSlot->cleanup(currentValue);
    }
  }

  namespace
  {
    struct SpCountedBaseRuntimeView
    {
      void* vftable;
      volatile LONG useCount;
      volatile LONG weakCount;
    };

    static_assert(sizeof(SpCountedBaseRuntimeView) == 0x0C, "SpCountedBaseRuntimeView size must be 0x0C");

    [[nodiscard]] inline SpCountedBaseRuntimeView* AsRuntimeView(detail::sp_counted_base* const control) noexcept
    {
      return reinterpret_cast<SpCountedBaseRuntimeView*>(control);
    }

    template <typename OutPairT, typename SourcePairT>
    SharedCountPair* AssignWeakPairFromSharedCore(OutPairT* const outPair, const SourcePairT* const sourcePair) noexcept
    {
      outPair->px = sourcePair->px;

      detail::sp_counted_base* const sourceControl = sourcePair->pi;
      if (sourceControl != outPair->pi) {
        if (sourceControl != nullptr) {
          sourceControl->weak_add_ref();
        }
        if (outPair->pi != nullptr) {
          outPair->pi->weak_release();
        }
        outPair->pi = sourceControl;
      }

      return outPair;
    }

    template <typename OutPairT, typename SourcePairT>
    SharedCountPair* AssignSharedPairRetainCore(OutPairT* const outPair, const SourcePairT* const sourcePair) noexcept
    {
      outPair->px = sourcePair->px;
      outPair->pi = sourcePair->pi;
      if (outPair->pi != nullptr) {
        outPair->pi->add_ref_copy();
      }
      return outPair;
    }
  } // namespace

  /**
   * Address: 0x0043D940 (FUN_0043D940)
   * Address: 0x0043EED0 (FUN_0043EED0)
   * Address: 0x0043F2E0 (FUN_0043F2E0)
   * Address: 0x004438C0 (FUN_004438C0)
   *
   * What it does:
   * Copies one `(px,pi)` pair and rebinds control ownership by retaining the
   * incoming `pi` then weak-releasing the previous `pi`.
   */
  SharedCountPair* AssignWeakPairFromShared(
    SharedCountPair* const outPair,
    const SharedCountPair* const sourcePair
  ) noexcept
  {
    return AssignWeakPairFromSharedCore(outPair, sourcePair);
  }

  /**
   * Address: 0x004414F0 (FUN_004414F0)
   * Address: 0x0043F7E0 (FUN_0043F7E0)
   * Address: 0x0043FCF0 (FUN_0043FCF0)
   * Address: 0x0089AE50 (FUN_0089AE50, Moho::WeakPtr_UICommandGraph::cpy)
   *
   * What it does:
   * Executes the same weak-owner pair rebind as `AssignWeakPairFromShared`,
   * but receives arguments in `(source, destination)` order.
   */
  SharedCountPair* AssignWeakPairFromSharedReversed(
    const SharedCountPair* const sourcePair,
    SharedCountPair* const outPair
  ) noexcept
  {
    return AssignWeakPairFromSharedCore(outPair, sourcePair);
  }

  /**
   * Address: 0x0043DCF0 (FUN_0043DCF0)
 * Address: 0x0043F500 (FUN_0043F500)
 * Address: 0x0043F8E0 (FUN_0043F8E0)
 * Address: 0x0043FD90 (FUN_0043FD90)
 * Address: 0x00446A80 (FUN_00446A80)
 * Address: 0x004456E0 (FUN_004456E0)
 * Address: 0x00445860 (FUN_00445860)
 * Address: 0x004459A0 (FUN_004459A0)
 * Address: 0x004459C0 (FUN_004459C0)
 * Address: 0x004459E0 (FUN_004459E0)
 * Address: 0x00446150 (FUN_00446150)
 * Address: 0x004462F0 (FUN_004462F0)
 *
 * What it does:
 * Copies one `(px,pi)` pair and retains one shared control-block reference.
 */
  SharedCountPair* AssignSharedPairRetain(
    SharedCountPair* const outPair,
    const SharedCountPair* const sourcePair
  ) noexcept
  {
    return AssignSharedPairRetainCore(outPair, sourcePair);
  }

  /**
   * Address: 0x0043E3B0 (FUN_0043E3B0)
   *
   * What it does:
   * Duplicate codegen lane of `AssignSharedPairRetain`.
   */
  SharedCountPair* AssignSharedPairRetainAlias(
    SharedCountPair* const outPair,
    const SharedCountPair* const sourcePair
  ) noexcept
  {
    return AssignSharedPairRetainCore(outPair, sourcePair);
  }

  /**
   * Address: 0x00740270 (FUN_00740270)
   *
   * What it does:
   * Releases one shared control block and disposes/destroys the control block
   * on the final strong and weak transitions.
   */
  void ReleaseSharedCount(detail::sp_counted_base* const control) noexcept
  {
    if (control != nullptr) {
      control->release();
    }
  }

  /**
   * Address: 0x00857630 (FUN_00857630)
   *
   * What it does:
   * Releases one half-open range of shared-pair slots by releasing each
   * control block referenced from the pair lanes.
   */
  SharedCountPair* ReleaseSharedCountRange(
    SharedCountPair* const begin,
    SharedCountPair* const end
  ) noexcept
  {
    SharedCountPair* cursor = begin;
    while (cursor != end) {
      ReleaseSharedCount(cursor->pi);
      ++cursor;
    }

    return cursor;
  }

  /**
   * Address: 0x004DE370 (FUN_004DE370)
   *
   * What it does:
   * Copy-assigns one shared-pair half-open range `[sourceBegin, sourceEnd)`
   * into already-constructed destination lanes, retaining incoming control
   * blocks and releasing previously bound controls per slot.
   */
  SharedCountPair* CopyAssignSharedPairRangeRetain(
    SharedCountPair* destination,
    const SharedCountPair* sourceBegin,
    const SharedCountPair* const sourceEnd
  ) noexcept
  {
    while (sourceBegin != sourceEnd) {
      destination->px = sourceBegin->px;

      detail::sp_counted_base* const incomingControl = sourceBegin->pi;
      if (incomingControl != destination->pi) {
        if (incomingControl != nullptr) {
          incomingControl->add_ref_copy();
        }
        if (destination->pi != nullptr) {
          destination->pi->release();
        }
        destination->pi = incomingControl;
      }

      ++sourceBegin;
      ++destination;
    }

    return destination;
  }

  /**
   * Address: 0x004DE570 (FUN_004DE570)
   *
   * What it does:
   * Fill-assigns one shared-pair value over one already-constructed
   * destination range `[destinationBegin, destinationEnd)`, retaining incoming
   * controls and releasing previously bound controls per slot.
   */
  SharedCountPair* FillAssignSharedPairRangeRetain(
    SharedCountPair* destinationBegin,
    SharedCountPair* const destinationEnd,
    const SharedCountPair& value
  ) noexcept
  {
    SharedCountPair* cursor = destinationBegin;
    while (cursor != destinationEnd) {
      cursor->px = value.px;

      detail::sp_counted_base* const incomingControl = value.pi;
      if (incomingControl != cursor->pi) {
        if (incomingControl != nullptr) {
          incomingControl->add_ref_copy();
        }
        if (cursor->pi != nullptr) {
          cursor->pi->release();
        }
        cursor->pi = incomingControl;
      }

      ++cursor;
    }

    return cursor;
  }

  /**
   * Address: 0x004DE830 (FUN_004DE830)
   *
   * What it does:
   * Copy-assigns one shared-pair range backward from `[sourceBegin, sourceEnd)`
   * into destination lanes ending at `destinationEnd`, preserving overlap-safe
   * copy-backward semantics while retaining/releasing control blocks.
   */
  SharedCountPair* CopyAssignSharedPairRangeBackwardRetain(
    SharedCountPair* destinationEnd,
    const SharedCountPair* const sourceBegin,
    const SharedCountPair* sourceEnd
  ) noexcept
  {
    while (sourceEnd != sourceBegin) {
      --sourceEnd;
      --destinationEnd;

      destinationEnd->px = sourceEnd->px;

      detail::sp_counted_base* const incomingControl = sourceEnd->pi;
      if (incomingControl != destinationEnd->pi) {
        if (incomingControl != nullptr) {
          incomingControl->add_ref_copy();
        }
        if (destinationEnd->pi != nullptr) {
          destinationEnd->pi->release();
        }
        destinationEnd->pi = incomingControl;
      }
    }

    return destinationEnd;
  }

  /**
   * Address: 0x004DEA20 (FUN_004DEA20)
   * Address: 0x007846C0 (FUN_007846C0)
   *
   * What it does:
   * Uninitialized-copies one shared-pair range `[sourceBegin, sourceEnd)` into
   * destination lanes starting at `destinationBegin`, retaining the copied
   * control blocks and returning one-past the final destination slot.
   */
  SharedCountPair* UninitializedCopySharedPairRangeRetain(
    SharedCountPair* destinationBegin,
    const SharedCountPair* sourceBegin,
    const SharedCountPair* const sourceEnd
  ) noexcept
  {
    SharedCountPair* destination = destinationBegin;
    while (sourceBegin != sourceEnd) {
      if (destination != nullptr) {
        destination->px = sourceBegin->px;
        destination->pi = sourceBegin->pi;
        if (destination->pi != nullptr) {
          destination->pi->add_ref_copy();
        }
      }

      ++sourceBegin;
      ++destination;
    }

    return destination;
  }

  /**
   * Address: 0x007568D0 (FUN_007568D0)
   *
   * What it does:
   * Uninitialized-copies one 12-byte `(lane0,lane1,pi)` range
   * `[sourceBegin, sourceEnd)` into destination lanes and retains each copied
   * shared control lane.
   */
  SharedControlTriplet* UninitializedCopySharedControlTripletRangeRetain(
    SharedControlTriplet* destinationBegin,
    const SharedControlTriplet* sourceBegin,
    const SharedControlTriplet* const sourceEnd
  ) noexcept
  {
    SharedControlTriplet* destination = destinationBegin;
    while (sourceBegin != sourceEnd) {
      if (destination != nullptr) {
        destination->lane0 = sourceBegin->lane0;
        destination->lane1 = sourceBegin->lane1;
        destination->pi = sourceBegin->pi;
        if (destination->pi != nullptr) {
          destination->pi->add_ref_copy();
        }
      }

      ++sourceBegin;
      ++destination;
    }

    return destination;
  }

  /**
   * Address: 0x004DDDC0 (FUN_004DDDC0)
   *
   * What it does:
   * Alias lane of `CopyAssignSharedPairRangeRetain`.
   */
  SharedCountPair* CopyAssignSharedPairRangeRetainAlias(
    SharedCountPair* destination,
    const SharedCountPair* sourceBegin,
    const SharedCountPair* const sourceEnd
  ) noexcept
  {
    return CopyAssignSharedPairRangeRetain(destination, sourceBegin, sourceEnd);
  }

  /**
   * Address: 0x004DDF70 (FUN_004DDF70)
   *
   * What it does:
   * Wrapper lane of `UninitializedCopySharedPairRangeRetain` used by vector
   * insertion paths to shift one shared-pair tail into uninitialized storage.
   */
  SharedCountPair* UninitializedCopySharedPairRangeRetainAlias(
    SharedCountPair* destinationBegin,
    const SharedCountPair* sourceBegin,
    const SharedCountPair* const sourceEnd
  ) noexcept
  {
    return UninitializedCopySharedPairRangeRetain(destinationBegin, sourceBegin, sourceEnd);
  }

  /**
   * Address: 0x004DDFA0 (FUN_004DDFA0)
   *
   * What it does:
   * Alias lane of `FillAssignSharedPairRangeRetain`.
   */
  SharedCountPair* FillAssignSharedPairRangeRetainAlias(
    SharedCountPair* destinationBegin,
    SharedCountPair* const destinationEnd,
    const SharedCountPair& value
  ) noexcept
  {
    return FillAssignSharedPairRangeRetain(destinationBegin, destinationEnd, value);
  }

  /**
   * Address: 0x004DDFB0 (FUN_004DDFB0)
   *
   * What it does:
   * Alias lane of `CopyAssignSharedPairRangeBackwardRetain`.
   */
  SharedCountPair* CopyAssignSharedPairRangeBackwardRetainAlias(
    SharedCountPair* destinationEnd,
    const SharedCountPair* const sourceBegin,
    const SharedCountPair* sourceEnd
  ) noexcept
  {
    return CopyAssignSharedPairRangeBackwardRetain(destinationEnd, sourceBegin, sourceEnd);
  }

  /**
   * Address: 0x00446F30 (FUN_00446F30)
   *
   * What it does:
   * Attempts to acquire one shared-owner reference only when the current
   * use-count is non-zero.
   */
  bool SpCountedBaseAddRefLock(detail::sp_counted_base* const control) noexcept
  {
    return control != nullptr && control->add_ref_lock();
  }

  /**
   * Address: 0x00446F70 (FUN_00446F70)
   *
   * What it does:
   * Atomically increments one weak-count lane and returns the previous value.
   */
  std::int32_t SpCountedBaseWeakAddRef(detail::sp_counted_base* const control) noexcept
  {
    if (control == nullptr) {
      return 0;
    }

    SpCountedBaseRuntimeView* const runtime = AsRuntimeView(control);
    return static_cast<std::int32_t>(InterlockedExchangeAdd(&runtime->weakCount, 1));
  }

  /**
   * Address: 0x00446F80 (FUN_00446F80)
   *
   * What it does:
   * Returns one shared-owner use-count lane.
   */
  std::int32_t SpCountedBaseUseCount(const detail::sp_counted_base* const control) noexcept
  {
    if (control == nullptr) {
      return 0;
    }
    return static_cast<std::int32_t>(control->use_count());
  }

  /**
   * Address: 0x00446FB0 (FUN_00446FB0)
   *
   * What it does:
   * Increments one weak-count lane and returns the same control pointer.
   */
  detail::sp_counted_base* SpCountedBaseWeakAddRefReturn(detail::sp_counted_base* const control) noexcept
  {
    SpCountedBaseWeakAddRef(control);
    return control;
  }

  /**
   * Address: 0x00446FC0 (FUN_00446FC0)
   *
   * What it does:
   * Releases one weak-owner reference from one control-pointer slot.
   */
  detail::sp_counted_base* SpCountedBaseWeakReleaseFromSlot(detail::sp_counted_base** const controlSlot) noexcept
  {
    if (controlSlot == nullptr) {
      return nullptr;
    }

    detail::sp_counted_base* const control = *controlSlot;
    if (control != nullptr) {
      control->weak_release();
    }
    return control;
  }

  /**
   * Address: 0x00446FE0 (FUN_00446FE0)
   *
   * What it does:
   * Rebinds one weak control-pointer slot by weak-retaining the incoming
   * source control and weak-releasing the previously bound control.
   */
  detail::sp_counted_base** SpCountedBaseWeakAssignSlot(
    detail::sp_counted_base** const targetControlSlot,
    detail::sp_counted_base* const* const sourceControlSlot
  ) noexcept
  {
    if (targetControlSlot == nullptr) {
      return nullptr;
    }

    detail::sp_counted_base* const incomingControl =
      sourceControlSlot != nullptr ? *sourceControlSlot : nullptr;
    if (incomingControl != nullptr) {
      SpCountedBaseWeakAddRef(incomingControl);
    }

    if (*targetControlSlot != nullptr) {
      (*targetControlSlot)->weak_release();
    }

    *targetControlSlot = incomingControl;
    return targetControlSlot;
  }

  /**
   * Address: 0x00447020 (FUN_00447020)
   *
   * What it does:
   * Returns shared-owner use-count from one control-pointer slot, or zero when
   * no control block is present.
   */
  std::int32_t SpCountedBaseUseCountFromSlotOrZero(detail::sp_counted_base* const* const controlSlot) noexcept
  {
    if (controlSlot == nullptr || *controlSlot == nullptr) {
      return 0;
    }
    return SpCountedBaseUseCount(*controlSlot);
  }

  /**
   * Address: 0x00447030 (FUN_00447030)
   *
   * What it does:
   * Constructs/rebinds one weak control-pointer slot from one shared slot and
   * throws `boost::bad_weak_ptr` when the shared owner is absent or lock fails.
   */
  detail::sp_counted_base** SpCountedBaseWeakConstructFromSharedOrThrow(
    detail::sp_counted_base** const outWeakControlSlot,
    detail::sp_counted_base* const* const sourceSharedControlSlot
  )
  {
    if (outWeakControlSlot == nullptr) {
      throw boost::bad_weak_ptr();
    }

    detail::sp_counted_base* const sourceControl =
      sourceSharedControlSlot != nullptr ? *sourceSharedControlSlot : nullptr;
    *outWeakControlSlot = sourceControl;

    if (sourceControl == nullptr || !SpCountedBaseAddRefLock(sourceControl)) {
      throw boost::bad_weak_ptr();
    }

    return outWeakControlSlot;
  }

  /**
   * Address: 0x004470A0 (FUN_004470A0)
   *
   * What it does:
   * Constructs one `boost::bad_weak_ptr` exception object in caller-provided
   * storage.
   */
  boost::bad_weak_ptr* ConstructBadWeakPtr(boost::bad_weak_ptr* const outException)
  {
    return ::new (static_cast<void*>(outException)) boost::bad_weak_ptr();
  }

  /**
   * Address: 0x004470D0 (FUN_004470D0)
   *
   * What it does:
   * Runs one `boost::bad_weak_ptr` deleting-destructor lane controlled by
   * the low bit of `deleteFlag`.
   */
  boost::bad_weak_ptr* DestructBadWeakPtr(
    boost::bad_weak_ptr* const exceptionObject,
    const unsigned char deleteFlag
  ) noexcept
  {
    if (exceptionObject == nullptr) {
      return nullptr;
    }

    exceptionObject->~bad_weak_ptr();
    if ((deleteFlag & 1u) != 0u) {
      ::operator delete(static_cast<void*>(exceptionObject));
    }
    return exceptionObject;
  }

  /**
   * Address: 0x0049C140 (FUN_0049C140)
   *
   * What it does:
   * Copy-constructs one `boost::bad_pointer` exception into caller-provided
   * storage, preserving the legacy pointer-container exception chain.
   */
  boost::bad_pointer* ConstructBadPointerFromCopy(
    boost::bad_pointer* const outException,
    const boost::bad_pointer& sourceException
  )
  {
    return ::new (static_cast<void*>(outException)) boost::bad_pointer(sourceException);
  }

  /**
   * Address: 0x0049C170 (FUN_0049C170)
   *
   * What it does:
   * Copy-constructs one `boost::bad_ptr_container_operation` exception into
   * caller-provided storage.
   */
  boost::bad_ptr_container_operation* ConstructBadPtrContainerOperationFromCopy(
    boost::bad_ptr_container_operation* const outException,
    const boost::bad_ptr_container_operation& sourceException
  )
  {
    return ::new (static_cast<void*>(outException)) boost::bad_ptr_container_operation(sourceException);
  }

  namespace
  {
    struct BadPtrContainerOperationRuntimeView
    {
      void* vftable;
      std::uint32_t stdExceptionWhat;
      std::uint32_t stdExceptionDoFree;
      const char* message;
    };

    static_assert(
      offsetof(BadPtrContainerOperationRuntimeView, message) == 0x0C,
      "BadPtrContainerOperationRuntimeView::message offset must be 0x0C"
    );
  } // namespace

  /**
   * Address: 0x00491350 (FUN_00491350)
   *
   * What it does:
   * Returns the pointer-container exception message lane used by both
   * `boost::bad_ptr_container_operation` and `boost::bad_pointer`.
   */
  const char* GetBadPtrContainerMessage(const boost::bad_ptr_container_operation* const exceptionObject) noexcept
  {
    if (exceptionObject == nullptr) {
      return nullptr;
    }

    const auto* const view = reinterpret_cast<const BadPtrContainerOperationRuntimeView*>(exceptionObject);
    return view->message;
  }

  /**
   * Address: 0x00491360 (FUN_00491360)
   *
   * What it does:
   * Runs one deleting-destructor thunk for `boost::bad_ptr_container_operation`,
   * forwarding through `std::exception` teardown and optional operator delete.
   */
  boost::bad_ptr_container_operation* DestructBadPtrContainerOperation(
    boost::bad_ptr_container_operation* const exceptionObject,
    const unsigned char deleteFlag
  ) noexcept
  {
    if (exceptionObject == nullptr) {
      return nullptr;
    }

    static_cast<std::exception*>(exceptionObject)->~exception();
    if ((deleteFlag & 1u) != 0u) {
      ::operator delete(static_cast<void*>(exceptionObject));
    }
    return exceptionObject;
  }

  /**
   * Address: 0x004913B0 (FUN_004913B0)
   *
   * What it does:
   * Runs one deleting-destructor thunk for `boost::bad_pointer`,
   * forwarding through `std::exception` teardown and optional operator delete.
   */
  boost::bad_pointer* DestructBadPointer(
    boost::bad_pointer* const exceptionObject,
    const unsigned char deleteFlag
  ) noexcept
  {
    if (exceptionObject == nullptr) {
      return nullptr;
    }

    static_cast<std::exception*>(exceptionObject)->~exception();
    if ((deleteFlag & 1u) != 0u) {
      ::operator delete(static_cast<void*>(exceptionObject));
    }
    return exceptionObject;
  }
  namespace
  {
    [[nodiscard]] void* SpCountedImplGetDeleterNullResult(boost::detail::sp_typeinfo const&) noexcept
    {
      return nullptr;
    }
  } // namespace
  /**
   * Address: 0x004DE780 (FUN_004DE780, boost::detail::sp_counted_impl_p<Moho::AudioEngine>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullAudioEngine(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x0053A260 (FUN_0053A260, boost::detail::sp_counted_impl_p<Moho::RScmResource>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullRScmResource(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x0053B3F0 (FUN_0053B3F0, boost::detail::sp_counted_impl_p<Moho::RScaResource>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullRScaResource(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x00545370 (FUN_00545370, boost::detail::sp_counted_impl_p<Moho::LaunchInfoNew>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullLaunchInfoNew(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x005791E0 (FUN_005791E0, boost::detail::sp_counted_impl_p<Moho::CHeightField>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullCHeightField(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x005CD5B0 (FUN_005CD5B0, boost::detail::sp_counted_impl_p<Moho::CIntelGrid>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullCIntelGrid(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x0063E7A0 (FUN_0063E7A0, boost::detail::sp_counted_impl_p<Moho::CAniPose>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullCAniPose(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x00755FD0 (FUN_00755FD0, boost::detail::sp_counted_impl_p<Moho::ISimResources>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullISimResources(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x00756020 (FUN_00756020, boost::detail::sp_counted_impl_p<Moho::CDebugCanvas>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullCDebugCanvas(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x00756070 (FUN_00756070, boost::detail::sp_counted_impl_p<Moho::SParticleBuffer>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullSParticleBuffer(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x00765760 (FUN_00765760, boost::detail::sp_counted_impl_p<Moho::PathPreviewFinder>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullPathPreviewFinder(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x007BDC50 (FUN_007BDC50, boost::detail::sp_counted_impl_p<Moho::CGpgNetInterface>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullCGpgNetInterface(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x007E6580 (FUN_007E6580, boost::detail::sp_counted_impl_p<Moho::MeshMaterial>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullMeshMaterial(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x007E65D0 (FUN_007E65D0, boost::detail::sp_counted_impl_p<Moho::Mesh>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullMesh(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x007E69A0 (FUN_007E69A0, boost::detail::sp_counted_impl_p<Moho::MeshBatch>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullMeshBatch(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x007FBE70 (FUN_007FBE70, boost::detail::sp_counted_impl_p<Moho::IRenTerrain>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullIRenTerrain(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x007FC190 (FUN_007FC190, boost::detail::sp_counted_impl_p<Moho::CD3DTextureBatcher>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullCD3DTextureBatcher(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x007FC1E0 (FUN_007FC1E0, boost::detail::sp_counted_impl_p<Moho::CD3DPrimBatcher>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullCD3DPrimBatcher(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x007FF6E0 (FUN_007FF6E0, boost::detail::sp_counted_impl_p<Moho::ID3DVertexSheet>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullID3DVertexSheet(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x008142D0 (FUN_008142D0, boost::detail::sp_counted_impl_p<Moho::ShoreCell>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullShoreCell(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x00832A30 (FUN_00832A30, boost::detail::sp_counted_impl_p<Moho::MeshInstance>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullMeshInstance(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x00884F20 (FUN_00884F20, boost::detail::sp_counted_impl_p<Moho::LaunchInfoLoad>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullLaunchInfoLoad(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x0089B8A0 (FUN_0089B8A0, boost::detail::sp_counted_impl_p<Moho::SSessionSaveData>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullSSessionSaveData(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x0089BCB0 (FUN_0089BCB0, boost::detail::sp_counted_impl_p<Moho::UICommandGraph>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullUICommandGraph(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x008E89B0 (FUN_008E89B0, boost::detail::sp_counted_impl_p<gpg::gal::TextureD3D9>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullTextureD3D9(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x008E89E0 (FUN_008E89E0, boost::detail::sp_counted_impl_p<gpg::gal::RenderTargetD3D9>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullRenderTargetD3D9(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x008E8A40 (FUN_008E8A40, boost::detail::sp_counted_impl_p<gpg::gal::DepthStencilTargetD3D9>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullDepthStencilTargetD3D9(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x008E8A70 (FUN_008E8A70, boost::detail::sp_counted_impl_p<gpg::gal::VertexFormatD3D9>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullVertexFormatD3D9(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x008E8AA0 (FUN_008E8AA0, boost::detail::sp_counted_impl_p<gpg::gal::VertexBufferD3D9>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullVertexBufferD3D9(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x008E8AD0 (FUN_008E8AD0, boost::detail::sp_counted_impl_p<gpg::gal::IndexBufferD3D9>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullIndexBufferD3D9(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x008E8B00 (FUN_008E8B00, boost::detail::sp_counted_impl_p<gpg::gal::EffectD3D9>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullEffectD3D9(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x008E8DA0 (FUN_008E8DA0, boost::detail::sp_counted_impl_p<gpg::gal::PipelineStateD3D9>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullPipelineStateD3D9(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x008F8FE0 (FUN_008F8FE0, boost::detail::sp_counted_impl_p<gpg::gal::EffectD3D10>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullEffectD3D10(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x008F9040 (FUN_008F9040, boost::detail::sp_counted_impl_p<gpg::gal::RenderTargetD3D10>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullRenderTargetD3D10(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x008F93A0 (FUN_008F93A0, boost::detail::sp_counted_impl_p<gpg::gal::PipelineStateD3D10>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullPipelineStateD3D10(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x00923720 (FUN_00923720, boost::detail::sp_counted_impl_p<std::basic_stringstream<char, std::char_traits<char>, std::allocator<char>>>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullStdStringstreamChar(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x00931ED0 (FUN_00931ED0, boost::detail::sp_counted_impl_p<gpg::HaStar::ClusterCache::Impl>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullClusterCacheImpl(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x00941680 (FUN_00941680, boost::detail::sp_counted_impl_p<gpg::gal::EffectTechniqueD3D9>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullEffectTechniqueD3D9(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }

  /**
   * Address: 0x009416B0 (FUN_009416B0, boost::detail::sp_counted_impl_p<gpg::gal::EffectVariableD3D9>::get_deleter)
   *
   * What it does:
   * Returns the null deleter-query lane for this `sp_counted_impl_p<T>` specialization.
   */
  void* SpCountedImplPGetDeleterNullEffectVariableD3D9(
    detail::sp_typeinfo const& requestedType
  ) noexcept
  {
    return SpCountedImplGetDeleterNullResult(requestedType);
  }
} // namespace boost
