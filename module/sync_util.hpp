#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <unordered_set>

#include <glog/logging.h>

namespace metrics {
  class sync_util {
   public:
    /**
     * Executes the provided function on the provided dispatcher (which must implement
     * "dispatch()"), and returns the value returned by that function or an empty pointer if the
     * result timed out. Timeouts may be disabled (indefinite wait) with timeout_secs=0.
     *
     * This is useful for avoiding race conditions against code that's being run by the dispatcher,
     * by ensuring that requested work occurs within the dispatcher thread.
     */
    template <typename Dispatcher, typename Result>
    static std::shared_ptr<Result> dispatch_get(
        const std::string& desc, Dispatcher& dispatcher, std::function<Result()> func,
        size_t timeout_secs = 5) {
      DLOG(INFO) << "dispatch_get(): " << desc;

      // These go out of scope when we exit, hence the reason for tracking status via 'tickets':
      std::shared_ptr<Result> out;
      std::condition_variable cv;

      size_t ticket;
      {
        std::unique_lock<std::mutex> lock(tickets->mutex);
        ticket = tickets->locked_get_next_ticket();
      }

      // Unlock in case this 'dispatch' call is actually running synchronously
      // (mainly an issue in certain tests, but doesn't hurt in general)
      LOG(INFO) << "Dispatching and waiting <=" << timeout_secs << "s for ticket " << ticket << ": " << desc;
      dispatcher.dispatch(
          std::bind(&sync_util::_exec_and_pass_result_cb<Result>, func, ticket, &cv, &out));

      {
        std::unique_lock<std::mutex> lock(tickets->mutex);
        if (out) {
          // Dispatch already completed, clear ticket and skip wait
          DLOG(INFO) << "Skipping wait for ticket " << ticket << ": " << desc;
        } else if (timeout_secs == 0) {
          // wait indefinitely
          for (;;) {
            cv.wait(lock);
            if (out) {
              DLOG(INFO) << "Result acquired for ticket " << ticket << ": " << desc;
              break;
            } else {
              DLOG(INFO) << "Flaked for ticket " << ticket << ", resuming indefinite wait: " << desc;
            }
          }
        } else {
          for (;;) {
            std::cv_status status = cv.wait_for(lock, std::chrono::milliseconds(timeout_secs * 1000));
            if (out) {
              // 'out' has been populated by _exec_and_pass_result_cb, we're done here.
              DLOG(INFO) << "Result acquired for ticket " << ticket << ": " << desc;
              break;
            } else if (status == std::cv_status::timeout) {
              // timeout has passed or 'out' has arrived. we're done here. clear the ticket in either case.
              DLOG(INFO) << "Timed out after " << timeout_secs << "s for ticket " << ticket << ": " << desc;
              break;
            } else {
              // not a timeout and 'out' isnt set yet: just a flake. retry wait.
              DLOG(INFO) << "Flaked for ticket " << ticket << ","
                         << " resetting wait for " << timeout_secs << "s: " << desc;
            }
          }
        }

        // regardless of outcome, always clear the ticket we created before we exit
        tickets->locked_clear_ticket(ticket);
      }

      if (out) {
        LOG(INFO) << "Dispatch result obtained for ticket " << ticket
                  << " after waiting <=" << timeout_secs << "s: " << desc;
        return std::shared_ptr<Result>(out);
      } else {
        LOG(ERROR) << "Timed out for ticket " << ticket
                   << " after waiting " << timeout_secs << "s (abandoning result): " << desc;
        return std::shared_ptr<Result>();
      }
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
          desc, dispatcher, std::bind(&sync_util::_exec_and_return_true_cb, func), timeout_secs);
      // Just return whether 'out' was populated with something:
      return (bool) out;
    }

   private:
    /**
     * No instantiation allowed.
     */
    sync_util() { }
    sync_util(const sync_util&) { }

    /**
     * Internal utility function to execute the provided function and pass its result to the
     * provided output.
     */
    template <typename Result>
    static void _exec_and_pass_result_cb(
        std::function<Result()> func,
        size_t ticket,
        std::condition_variable* cv_caller_scoped,
        std::shared_ptr<Result>* out_caller_scoped) {
      // Run func and get result.
      Result* result = new Result(func());

      {
        // BEFORE we touch 'out' or 'cv', check that the caller which owns them is still waiting for us.
        std::unique_lock<std::mutex> lock(tickets->mutex);
        if (tickets->locked_find_ticket(ticket)) {
          // Ticket was found, so caller is waiting for us. Pass result and notify.
          LOG(INFO) << "Result for ticket " << ticket << " complete, returning value.";
          out_caller_scoped->reset(result);
          cv_caller_scoped->notify_all();
        } else {
          LOG(INFO) << "Result for ticket " << ticket << " complete, abandoning value.";
          // Ticket was missing, so just toss what we got and exit.
          // DO NOT touch 'out' or 'cv', they are likely out of scope!
          delete result;
        }
      }
    }

    /**
     * Internal utility function to execute the provided function and return true.
     */
    static bool _exec_and_return_true_cb(std::function<void()> func) {
      func();
      return true;
    }

    // Shared resource for tracking which synchronous requests are still outstanding.
    // Allows work thread to safely check whether it should return a result.
    class Tickets {
     public:
      Tickets() : next_ticket(0) { }

      /**
       * Creates and returns a new registered ticket.
       * This must ONLY be called while 'mutex' is locked.
       */
      size_t locked_get_next_ticket();

      /**
       * Finds and clears a ticket. Returns whether the ticket was found.
       * This must ONLY be called while 'mutex' is locked.
       */
      bool locked_find_ticket(size_t ticket);

      /**
       * Clears a ticket.
       * This must ONLY be called while 'mutex' is locked.
       */
      void locked_clear_ticket(size_t ticket);

      /**
       * Above funcs should only be called WHILE this is locked.
       */
      std::mutex mutex;

     private:
      size_t next_ticket;
      std::unordered_set<size_t> tickets;
    };

    static std::shared_ptr<Tickets> tickets;
  };
}
