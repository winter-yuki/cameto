#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <utility>

const size_t KB = 1024;
const size_t MB = 1024 * KB;

void *operator new(std::size_t size) {
    return new (std::align_val_t(4 * KB)) char[size];
}

std::vector<void *> mkTestBuffer(const size_t size, const size_t step) {
    assert(size > 0 && step > 0);
    std::vector<void *> buffer(size);
    for (long long i = size - 1, j = size - 1 - step; j >= 0; i = j, j -= step) {
        buffer[i] = &buffer[j];
        buffer[j] = &buffer[size - 1];
    }
    return buffer;
}

size_t *touchTestBuffer(std::vector<void *> const & buffer, const size_t nTouches) {
    size_t **pos = (size_t **) buffer[buffer.size() - 1];
    for (size_t i = 0; i < nTouches; ++i) {
        pos = (size_t **) *pos;
    }
    return *pos;
}

std::pair<std::vector<long long>, size_t *> touchTestBufferTimed(
        std::vector<void *> const & buffer, const size_t nTouches) {
    size_t **pos = (size_t **) buffer[buffer.size() - 1];
    std::vector<long long> times;
    times.reserve(nTouches);
    for (size_t i = 0; i < nTouches; ++i) {
        const auto start = std::chrono::high_resolution_clock::now();
        pos = (size_t **) *pos;
        const auto end = std::chrono::high_resolution_clock::now();
        times.push_back((end - start).count());
    }
    return std::make_pair(std::move(times), *pos);
}

struct RawLevelInfo {
    const size_t arraySizeBytes;
    const long long timeNanos;
};

std::vector<RawLevelInfo> tryCacheLevelSizes(size_t minSizeBytes, size_t maxSizeBytes) {
    std::vector<RawLevelInfo> infos;
    const auto minSize = minSizeBytes / sizeof(void*);
    const auto maxSize = maxSizeBytes / sizeof(void*);
    const size_t nTouches = 1000000;
    const size_t sizeStep = minSize;
    for (size_t size = minSize; size <= maxSize; size += sizeStep) {
        const size_t step = size / nTouches;
        std::cout << "Couning caches: size = " << size << ", step = " << step << std::endl;
        const auto buffer = mkTestBuffer(size, std::max(size_t(1), step));
        // Warm up instruction and data caches
        const auto dummyPtr1 = touchTestBuffer(buffer, nTouches);
        const auto start = std::chrono::high_resolution_clock::now();
        const auto dummyPtr2 = touchTestBuffer(buffer, nTouches);
        const auto end = std::chrono::high_resolution_clock::now();
        // Prevent removing computation calls by -O3
        std::cout << dummyPtr1 << " " << dummyPtr2 << "\n";
        const auto info = RawLevelInfo {
                .arraySizeBytes = size * sizeof(void*),
                .timeNanos = static_cast<long long>((end - start).count())
        };
        infos.push_back(info);
    }
    return infos;
}

size_t selectCacheSize(std::vector<RawLevelInfo> const & infos, const size_t windowSize = 3) {
    assert(windowSize >= 2);
    std::vector<RawLevelInfo> diffs;
    diffs.reserve(infos.size());
    for (size_t i = 1; i < infos.size(); ++i) {
        const auto prev = infos[i - 1];
        const auto curr = infos[i];
        const auto info = RawLevelInfo {
                .arraySizeBytes = curr.arraySizeBytes,
                .timeNanos = curr.timeNanos - prev.timeNanos
        };
        diffs.push_back(info);
    }
    std::vector<RawLevelInfo> windowed;
    windowed.reserve(diffs.size() / 3);
    for (size_t i = windowSize - 1; i < diffs.size() - 2 /* tail outliers */; ++i) {
        auto sum = diffs[i].timeNanos;
        for (size_t j = i + 1 - windowSize; j < i; ++j) {
            sum += diffs[j].timeNanos;
        }
        const auto info = RawLevelInfo {
                .arraySizeBytes = diffs[i].arraySizeBytes,
                .timeNanos = sum
        };
        windowed.push_back(info);
    }
    return std::max_element(
                windowed.begin(), windowed.end(),
                [](auto x, auto y) { return x.timeNanos < y.timeNanos; }
    )->arraySizeBytes;
}

size_t calcCacheLineSize(const size_t cacheSizeBytes) {
    const auto size = cacheSizeBytes / sizeof(void*);
    long long lastMaxJump = std::numeric_limits<long long>::max();
    size_t step{};
    for (step = 1; step < size; step *= 2) {
        const auto buffer = mkTestBuffer(size, step);
        const auto [times, _] = touchTestBufferTimed(buffer, size / step);

        std::vector<long long> diffs;
        diffs.reserve(times.size());
        for (auto it = times.begin() + 1; it != times.end(); ++it) {
            diffs.push_back(*it - *(it - 1));
        }

        const long long max = *std::max_element(diffs.begin(), diffs.end());
        if (lastMaxJump != std::numeric_limits<long long>::max() && max < lastMaxJump / 10) {
            return step * sizeof(void*);
        }
        lastMaxJump = max;
    }
    return step * sizeof(void*);
}

int main() {
    const auto rawLevels = tryCacheLevelSizes(8 * KB, 256 * KB);
    for (auto it = rawLevels.begin(); it != rawLevels.end(); ++it) {
        std::cout << (it->arraySizeBytes / (double) KB) << "\t" << it->timeNanos << std::endl;
    }
    const auto cacheSize = selectCacheSize(rawLevels, 3);
    std::cout << "L1 cache size \t\t-- " << cacheSize << " KB" << std::endl;
    const auto cacheLineSize = calcCacheLineSize(cacheSize);
    std::cout << "L1 cache line size \t-- " << cacheLineSize << " bytes" << std::endl;
    // TODO cache line size & assiciativity
    return 0;
}
