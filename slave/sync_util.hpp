#pragma once

#include <functional>
#include <memory>
#include <time.h>

#include <glog/logging.h>

namespace stats {
  class sync_util {
   public:
    /**
     * Executes the provided function on the provided dispatcher (which must implement
     * "dispatch()"), and returns the value returned by that function or an empty pointer if the
     * result timed out. Timeouts may be disabled with timeout_secs=0.
     *
     * This is useful for avoiding race conditions against code that's being run by the dispatcher,
     * by ensuring that requested work occurs within the dispatcher thread.
     */
    template <typename Dispatcher, typename Result>
    static std::shared_ptr<Result> dispatch_get(
        const std::string& desc, Dispatcher& dispatcher, std::function<Result()> func,
        size_t timeout_secs = 5) {
      DLOG(INFO) << "dispatch_get(): " << desc;
      std::shared_ptr<Result> out;
      dispatcher.dispatch(
          std::bind(&sync_util::_exec_and_pass_result_cb<Result>, func, &out));

      struct timespec wait_5ms;
      wait_5ms.tv_sec = 0;
      wait_5ms.tv_nsec = 5000000;

      // 5ms * 200 = 1 second (approximately)
      for (size_t i = 0; timeout_secs == 0 || i < (timeout_secs * 200); ++i) {
        DLOG(INFO) << "dispatch_get(): wait " << desc << ": " << i;
        if (out) {
          DLOG(INFO) << "Dispatch result obtained after " << (i + 1) << " 5ms waits";
          return out;
        }
        nanosleep(&wait_5ms, NULL);
      }

      // Note that if this ever occurs, we will likely segfault: The 'out' pointer we passed into
      // _get_and_insert_result() will be out of scope.
      LOG(ERROR) << "Timed out after " << timeout_secs << "s waiting for async response: " << desc;
      return out;
    }

    /**
     * Executes the provided function on the provided dispatcher (which must implement
     * "dispatch()"), and returns whether the function successfully completed or if it timed out.
     * Timeouts may be disabled with timeout_secs=0.
     *
     * This is useful for avoiding race conditions against code that's being run by the dispatcher,
     * by ensuring that requested work occurs within the dispatcher thread.
     */
    template <typename Dispatcher>
    static bool dispatch_run(
        const std::string& desc, Dispatcher& dispatcher, std::function<void()> func,
        size_t timeout_secs = 5) {
      std::shared_ptr<bool> out = dispatch_get<Dispatcher, bool>(
          desc, dispatcher, std::bind(&sync_util::_exec_and_return_true_cb, func));
      // Just return whether 'out' was populated with something:
      return (bool) out;
    }

   private:
    /**
     * No instantiation allowed.
     */
    sync_util() { };
    sync_util(const sync_util&) { }

    /**
     * Internal utility function to execute the provided function and pass its result to the
     * provided output.
     */
    template <typename Result>
    static void _exec_and_pass_result_cb(std::function<Result()> func, std::shared_ptr<Result>* out) {
      out->reset(new Result(func()));
    }

    /**
     * Internal utility function to execute the provided function and return true.
     */
    static bool _exec_and_return_true_cb(std::function<void()> func) {
      func();
      return true;
    }
  };
}
