#ifndef UTIL_CACHE_H_
#define UTIL_CACHE_H_

#include <QDateTime>

#include <utility>

namespace util {

template <class T>
class CachedValue {
public:
    bool valid() const { return valid_; }

    const T& value() const { return value_; }
    T& value() { return value_; }

    QDateTime updatedAtUtc() const { return updatedAtUtc_; }

    void set(T value, QDateTime updatedAtUtc = QDateTime::currentDateTimeUtc()) {
        value_ = std::move(value);
        updatedAtUtc_ = updatedAtUtc;
        valid_ = true;
    }

    void clear() {
        value_ = T{};
        updatedAtUtc_ = {};
        valid_ = false;
    }

private:
    T value_{};
    QDateTime updatedAtUtc_;
    bool valid_ = false;
};

} // namespace util

#endif  // UTIL_CACHE_H_
