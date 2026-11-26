// libFuzzer harness for tinynet::Buffer (issue #7).
//
// The fuzz input is decoded into a sequence of Buffer operations. Every operation is mirrored on a
// std::string reference model, and after each one the harness checks that the Buffer still agrees
// with the model and with its own space-accounting rules. Any disagreement calls __builtin_trap(),
// which libFuzzer reports as a crash and saves as a reproducer.
//
// Sanitizers (ASan/UBSan) catch memory errors; the model and the invariants catch wrong results
// that do not crash, such as data reordered by compaction or growing when compaction would do.
//
// Preconditions guarded by assert() in Buffer (retrieve <= readable, prepend <= prependable) are
// respected here, so an assert firing is a real bug, not a harness misuse.
#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "tinynet/Buffer.h"

using tinynet::Buffer;

namespace {

constexpr std::size_t kMaxChunk = 4096;

#define FUZZ_CHECK(cond)          \
    do {                          \
        if (!(cond)) __builtin_trap(); \
    } while (0)

void check_invariants(const Buffer& buf, const std::string& model) {
    FUZZ_CHECK(buf.readable_bytes() == model.size());
    FUZZ_CHECK(buf.view() == model);
    FUZZ_CHECK(buf.prependable_bytes() + buf.readable_bytes() + buf.writable_bytes() == buf.capacity());
}

// append() must follow the space rules derived in the test strategy (section 5):
//   len <= W               -> fits, nothing moves
//   len <= W + P - 8       -> compacts: capacity unchanged, prependable reset to kCheapPrepend
//   otherwise              -> grows
// The middle rule is written as `len + 8 <= W + P`: after a prepend, P can be smaller than 8, and
// `W + P - 8` would wrap around as an unsigned value. (The first version of this harness made
// exactly that mistake; the fuzzer reported it as a crash within 1 228 runs.)
void append_and_check_space_rules(Buffer& buf, std::string& model, const std::string& data) {
    const std::size_t len = data.size();
    const std::size_t w = buf.writable_bytes();
    const std::size_t p = buf.prependable_bytes();
    const std::size_t capacity = buf.capacity();

    buf.append(data);
    model += data;

    if (len <= w) {
        FUZZ_CHECK(buf.capacity() == capacity);
        FUZZ_CHECK(buf.prependable_bytes() == p);
    } else if (len + Buffer::kCheapPrepend <= w + p) {
        FUZZ_CHECK(buf.capacity() == capacity);
        FUZZ_CHECK(buf.prependable_bytes() == Buffer::kCheapPrepend);
    } else {
        FUZZ_CHECK(buf.capacity() > capacity);
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FuzzedDataProvider fdp(data, size);

    Buffer buf(fdp.ConsumeIntegralInRange<std::size_t>(0, 2048));
    std::string model;
    check_invariants(buf, model);

    while (fdp.remaining_bytes() > 0) {
        switch (fdp.ConsumeIntegralInRange<int>(0, 7)) {
            case 0: {  // append
                const std::string chunk = fdp.ConsumeRandomLengthString(kMaxChunk);
                append_and_check_space_rules(buf, model, chunk);
                break;
            }
            case 1: {  // retrieve(n), 0 <= n <= readable
                const std::size_t n = fdp.ConsumeIntegralInRange<std::size_t>(0, model.size());
                buf.retrieve(n);
                model.erase(0, n);
                break;
            }
            case 2: {  // retrieve_as_string(n)
                const std::size_t n = fdp.ConsumeIntegralInRange<std::size_t>(0, model.size());
                const std::string got = buf.retrieve_as_string(n);
                FUZZ_CHECK(got == model.substr(0, n));
                model.erase(0, n);
                break;
            }
            case 3: {  // retrieve_all
                buf.retrieve_all();
                model.clear();
                FUZZ_CHECK(buf.prependable_bytes() == Buffer::kCheapPrepend);
                break;
            }
            case 4: {  // prepend(n), 0 <= n <= prependable
                const std::size_t n = fdp.ConsumeIntegralInRange<std::size_t>(0, buf.prependable_bytes());
                const std::string header = fdp.ConsumeBytesAsString(n);
                buf.prepend(header.data(), header.size());
                model.insert(0, header);
                break;
            }
            case 5: {  // ensure_writable + write in place + has_written
                const std::string chunk = fdp.ConsumeRandomLengthString(kMaxChunk);
                buf.ensure_writable(chunk.size());
                FUZZ_CHECK(buf.writable_bytes() >= chunk.size());
                std::copy(chunk.begin(), chunk.end(), buf.begin_write());
                buf.has_written(chunk.size());
                model += chunk;
                break;
            }
            case 6: {  // find_crlf
                const char* crlf = buf.find_crlf();
                const std::size_t pos = model.find("\r\n");
                if (pos == std::string::npos) {
                    FUZZ_CHECK(crlf == nullptr);
                } else {
                    FUZZ_CHECK(crlf == buf.peek() + pos);
                }
                break;
            }
            case 7: {  // retrieve_until the first CRLF (as an HTTP parser would)
                const char* crlf = buf.find_crlf();
                if (crlf != nullptr) {
                    const std::size_t n = static_cast<std::size_t>(crlf - buf.peek()) + 2;
                    buf.retrieve_until(crlf + 2);
                    model.erase(0, n);
                }
                break;
            }
        }
        check_invariants(buf, model);
    }
    return 0;
}
