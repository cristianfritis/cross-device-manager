#pragma once
#include <atomic>
#include <memory>

namespace devmgr::runtime {

class CancellationToken {
   public:
    CancellationToken() : flag_(std::make_shared<std::atomic_bool>(false)) {}
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> flag) : flag_(std::move(flag)) {}
    bool isCancellationRequested() const { return flag_ && flag_->load(); }

   private:
    std::shared_ptr<std::atomic_bool> flag_;
};

class CancellationSource {
   public:
    CancellationSource() : flag_(std::make_shared<std::atomic_bool>(false)) {}
    CancellationToken token() const { return CancellationToken(flag_); }
    void cancel() { flag_->store(true); }
    bool isCancelled() const { return flag_->load(); }

   private:
    std::shared_ptr<std::atomic_bool> flag_;
};

}  // namespace devmgr::runtime
