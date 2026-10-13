#include "goldcraft/local_qpc_proof.hpp"
#include <windows.h>
#include <sddl.h>
#include <algorithm>
#include <cstring>
#include <limits>

namespace goldcraft {
namespace {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
};
struct LocalMemory {
    void* value = nullptr;
    ~LocalMemory() { if (value) LocalFree(value); }
};
bool read_clock(std::uint64_t& counter, std::uint64_t& frequency) noexcept {
    LARGE_INTEGER qpc{}, rate{};
    if (!QueryPerformanceFrequency(&rate) || !QueryPerformanceCounter(&qpc) ||
        rate.QuadPart <= 0 || qpc.QuadPart < 0) return false;
    counter = static_cast<std::uint64_t>(qpc.QuadPart);
    frequency = static_cast<std::uint64_t>(rate.QuadPart);
    return true;
}
}

std::unique_ptr<LocalQpcProof> LocalQpcProof::create(std::uint64_t epoch) {
    if (!epoch) throw ProtocolError("invalid local clock epoch");
    auto proof = std::unique_ptr<LocalQpcProof>(new LocalQpcProof);
    proof->epoch_ = epoch;
    if (!read_clock(proof->issued_, proof->frequency_)) return nullptr;
    constexpr auto maximum = static_cast<std::uint64_t>(INT64_MAX);
    if (proof->frequency_ > (maximum - proof->issued_) / 3) return nullptr;
    proof->deadline_ = proof->issued_ + proof->frequency_ * 3;
    proof->challenge_ = random_key();
    if (proof->challenge_ == Key{}) return nullptr;
    const auto nameKey = random_key();
    constexpr char hex[] = "0123456789abcdef";
    proof->name_ = "Local\\GoldCraftClock-";
    for (auto byte : nameKey) { proof->name_ += hex[byte >> 4]; proof->name_ += hex[byte & 15]; }

    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return nullptr;
    DWORD bytes = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);
    if (!bytes || bytes > 65536) return nullptr;
    Bytes user(bytes);
    if (!GetTokenInformation(token.value, TokenUser, user.data(), bytes, &bytes)) return nullptr;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid)) return nullptr;
    LocalMemory sidMemory{sid};
    std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;";
    sddl += sid; sddl += L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
        return nullptr;
    LocalMemory descriptorMemory{descriptor};
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    std::wstring wideName(proof->name_.begin(), proof->name_.end());
    proof->handle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0, mapping_bytes, wideName.c_str());
    const auto mappingError = GetLastError();
    if (!proof->handle_ || mappingError == ERROR_ALREADY_EXISTS) return nullptr;
    proof->view_ = MapViewOfFile(proof->handle_, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, mapping_bytes);
    if (!proof->view_) return nullptr;
    Writer header;
    header.u32(magic); header.u32(layout); header.u32(mapping_bytes); header.u32(0);
    header.u64(epoch); header.u64(proof->frequency_); header.u64(proof->issued_); header.u64(proof->deadline_);
    header.bytes(proof->challenge_); header.bytes(Key{});
    std::copy(header.data.begin(), header.data.end(), proof->header_.begin());
    std::memcpy(proof->view_, proof->header_.data(), mapping_bytes);
    // Publish the immutable bytes before the caller enqueues the TCP offer.
    MemoryBarrier();
    return proof;
}

LocalQpcProof::~LocalQpcProof() {
    if (view_) UnmapViewOfFile(view_);
    if (handle_) CloseHandle(handle_);
    SecureZeroMemory(challenge_.data(), challenge_.size());
}

Bytes LocalQpcProof::offer() const {
    Writer w;
    w.u32(layout); w.u32(mapping_bytes); w.u64(epoch_); w.u64(frequency_);
    w.u64(issued_); w.u64(deadline_); w.string(name_);
    // The random challenge is deliberately absent from this TCP message.
    return w.data;
}

bool LocalQpcProof::acknowledge(std::span<const std::uint8_t> payload) {
    if (answered_) throw ProtocolError("repeated local clock response");
    Reader r(payload);
    const auto version = r.u32(), accepted = r.u32();
    const auto epoch = r.u64(), frequency = r.u64();
    const auto challenge = r.key(); r.finish();
    if (version != layout || accepted > 1 || epoch != epoch_)
        throw ProtocolError("local clock response context");
    answered_ = true;
    if (!accepted) {
        if (frequency || challenge != Key{}) throw ProtocolError("noncanonical local clock decline");
        return false;
    }
    unsigned different = 0;
    for (std::size_t i = 0; i < challenge.size(); ++i) different |= challenge[i] ^ challenge_[i];
    if (frequency != frequency_ || different) throw ProtocolError("local clock proof mismatch");
    if (std::memcmp(view_, header_.data(), mapping_bytes)) throw ProtocolError("local clock mapping changed");
    std::uint64_t counter = 0, rate = 0;
    return read_clock(counter, rate) && rate == frequency_ && counter >= issued_ && counter < deadline_;
}
}
