// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "synaxis/player.hpp"

#include "player_backend.hpp"

#include <condition_variable>
#include <mutex>

namespace synaxis {

// Everything here is backend-agnostic: the backends report status, and this
// layer caches it, wakes waiters, and forwards to the user. Keeping it out of
// the backends means a new one only has to drive its library.
class Player::Impl {
public:
    explicit Impl(Backend backend) : backend_kind_(backend) {}

    void SetStatusCallback(StatusCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        user_callback_ = std::move(callback);
    }

    bool Open(const std::filesystem::path& path) {
        if (!EnsureBackend()) return false;

        // Reset before handing off: the backend may report Playing from
        // another thread before Open() has even returned.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_ = Status{};
            status_.state = State::Loading;
        }

        if (!backend_->Open(path)) {
            Publish([](Status& s) { s.state = State::Error; });
            return false;
        }
        return true;
    }

    void Pause() {
        if (backend_) backend_->Pause();
    }

    void Resume() {
        if (backend_) backend_->Resume();
    }

    void Stop() {
        if (backend_) backend_->Stop();
    }

    void Seek(double seconds) {
        if (backend_) backend_->Seek(seconds);
    }

    Status GetStatus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

    bool WaitUntilFinished() {
        std::unique_lock<std::mutex> lock(mutex_);
        finished_.wait(lock, [this] {
            return status_.state == State::Ended || status_.state == State::Error;
        });
        return status_.state != State::Error;
    }

private:
    // Deferred so that constructing a Player never fails or opens a window;
    // the library only gets initialized once there's something to play.
    bool EnsureBackend() {
        if (backend_) return true;

        auto handler = [this](const Status& status) { OnBackendStatus(status); };
        backend_ = backend_kind_ == Backend::Vlc ? detail::MakeVlcBackend(handler)
                                                  : detail::MakeMpvBackend(handler);
        return backend_ != nullptr;
    }

    void OnBackendStatus(const Status& status) {
        Publish([&status](Status& s) { s = status; });
    }

    // Applies `mutate` to the cached status, wakes any waiter, then invokes
    // the user callback with the lock released — the callback is arbitrary
    // code and must never run while holding our mutex.
    template <typename Mutate>
    void Publish(Mutate&& mutate) {
        StatusCallback callback;
        Status snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            mutate(status_);
            snapshot = status_;
            callback = user_callback_;
        }
        finished_.notify_all();
        if (callback) callback(snapshot);
    }

    mutable std::mutex mutex_;
    std::condition_variable finished_;
    Status status_;
    StatusCallback user_callback_;

    Backend backend_kind_;
    std::unique_ptr<detail::PlayerBackend> backend_;
};

Player::Player(Backend backend) : impl_(std::make_unique<Impl>(backend)) {}

Player::~Player() = default;

void Player::SetStatusCallback(StatusCallback callback) {
    impl_->SetStatusCallback(std::move(callback));
}

bool Player::Open(const std::filesystem::path& path) { return impl_->Open(path); }

void Player::Pause() { impl_->Pause(); }

void Player::Resume() { impl_->Resume(); }

void Player::Stop() { impl_->Stop(); }

void Player::Seek(double seconds) { impl_->Seek(seconds); }

Player::Status Player::GetStatus() const { return impl_->GetStatus(); }

bool Player::WaitUntilFinished() { return impl_->WaitUntilFinished(); }

} // namespace synaxis
