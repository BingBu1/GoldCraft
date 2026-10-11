#include "goldcraft/shared_bulk.hpp"
#include <windows.h>
#include <sddl.h>
#include <algorithm>
#include <bit>
#include <cstring>

namespace goldcraft {
static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(LONG) == 4);
static_assert(bulk_capacity < 0x80000000u && (bulk_capacity & (bulk_capacity - 1)) == 0);
namespace {
// Interlocked 32-bit operations are aligned, atomic and full barriers on x86/x64.
// They provide at least acquire/release publication across mapped process views.
std::uint32_t acquire32(const std::uint8_t* address) {
    return static_cast<std::uint32_t>(InterlockedCompareExchange(
        reinterpret_cast<volatile LONG*>(const_cast<std::uint8_t*>(address)), 0, 0));
}
void release32(std::uint8_t* address, std::uint32_t value) {
    InterlockedExchange(reinterpret_cast<volatile LONG*>(address), std::bit_cast<LONG>(value));
}
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
};
struct LocalMemory {
    void* value = nullptr;
    ~LocalMemory() { if (value) LocalFree(value); }
};
}
bool bulk_eligible(Type t) {
    switch (t) {
    case Type::atlas: case Type::section_mesh: case Type::entity_texture:
    case Type::atlas_patches: case Type::hud_frame: case Type::particle_texture:
    case Type::particle_mesh: case Type::post_effect_plan: case Type::particle_texture_chunk:
    case Type::entity_frame: return true;
    default: return false;
    }
}
bool bulk_internal(Type t) { return t == Type::bulk_offer || t == Type::bulk_ack || t == Type::bulk_ref; }

std::unique_ptr<SharedBulk> SharedBulk::create(std::uint64_t epoch) {
    if (!epoch) throw ProtocolError("invalid shared bulk epoch");
    auto result = std::unique_ptr<SharedBulk>(new SharedBulk);
    result->epoch_ = epoch;
    auto random = random_key();
    constexpr char hex[] = "0123456789abcdef";
    result->name_ = "Local\\GoldCraftBulk-";
    for (auto b : random) { result->name_ += hex[b >> 4]; result->name_ += hex[b & 15]; }

    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return nullptr;
    DWORD bytes = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);
    if (!bytes || bytes > 65536) return nullptr;
    Bytes user(bytes);
    if (!GetTokenInformation(token.value, TokenUser, user.data(), bytes, &bytes)) return nullptr;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid)) return nullptr;
    LocalMemory sid_memory{sid};
    // Protected DACL, no inheritance: only this process user's SID and LocalSystem.
    std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;";
    sddl += sid; sddl += L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return nullptr;
    LocalMemory descriptor_memory{descriptor};
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    std::wstring wide_name(result->name_.begin(), result->name_.end());
    result->handle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
        bulk_header_bytes + bulk_capacity, wide_name.c_str());
    const auto mapping_error = GetLastError();
    if (!result->handle_ || mapping_error == ERROR_ALREADY_EXISTS) return nullptr;
    result->view_ = static_cast<std::uint8_t*>(MapViewOfFile(result->handle_, FILE_MAP_READ | FILE_MAP_WRITE,
        0, 0, bulk_header_bytes + bulk_capacity));
    if (!result->view_) return nullptr;
    // Fresh pagefile mappings start zeroed. Initialize the immutable header before the offer.
    Writer header; header.u32(bulk_magic); header.u32(bulk_layout); header.u32(bulk_capacity);
    header.u32(0); header.u64(epoch);
    std::memcpy(result->view_, header.data.data(), header.data.size());
    release32(result->view_ + 64, 0);
    release32(result->view_ + 128, 0);
    return result;
}
SharedBulk::~SharedBulk() {
    if (view_) UnmapViewOfFile(view_);
    if (handle_) CloseHandle(handle_);
}
Bytes SharedBulk::offer() const {
    Writer w; w.u32(bulk_layout); w.u32(bulk_capacity); w.u64(epoch_); w.string(name_); return w.data;
}
void SharedBulk::acknowledge(std::span<const std::uint8_t> payload) {
    Reader r(payload); const auto layout = r.u32(), accepted = r.u32(); r.finish();
    if (acknowledged_ || layout != bulk_layout || accepted > 1) throw ProtocolError("invalid shared bulk acknowledgement");
    validate_header();
    acknowledged_ = true; enabled_ = accepted == 1;
}
void SharedBulk::validate_header() const {
    Reader r({view_, 64});
    if (r.u32() != bulk_magic || r.u32() != bulk_layout || r.u32() != bulk_capacity || r.u32() != 0 || r.u64() != epoch_)
        throw ProtocolError("shared bulk header changed");
    for (auto b : r.bytes(r.remaining())) if (b) throw ProtocolError("shared bulk reserved header changed");
    for (std::size_t i = 68; i < 128; ++i) if (view_[i]) throw ProtocolError("shared bulk reserved header changed");
    for (std::size_t i = 132; i < bulk_header_bytes; ++i) if (view_[i]) throw ProtocolError("shared bulk reserved header changed");
    if (acquire32(view_ + 64) != read_cursor_) throw ProtocolError("shared bulk reader cursor changed");
}
Message SharedBulk::consume(std::span<const std::uint8_t> reference) {
    Reader r(reference); const auto type_value = r.u32(), length = r.u32(), start = r.u32(), reserved = r.u32(); r.finish();
    if (!enabled_ || type_value > UINT16_MAX || !bulk_eligible(static_cast<Type>(type_value)) ||
        length < bulk_min_payload || length > max_payload || reserved || start != read_cursor_)
        throw ProtocolError("invalid shared bulk reference");
    validate_header();
    const auto reservation = (length + 7u) & ~7u;
    const auto available = acquire32(view_ + 128) - read_cursor_;
    if (available > bulk_capacity || available < reservation) throw ProtocolError("unpublished shared bulk reference");
    high_water_ = std::max(high_water_, available);
    Message message{static_cast<Type>(type_value), Bytes(length)};
    const auto position = read_cursor_ & (bulk_capacity - 1);
    const auto first = std::min(length, bulk_capacity - position);
    std::memcpy(message.payload.data(), view_ + bulk_header_bytes + position, first);
    if (first < length) std::memcpy(message.payload.data() + first, view_ + bulk_header_bytes, length - first);
    read_cursor_ += reservation;
    // Reclaim only after the entire owned payload copy succeeds.
    release32(view_ + 64, read_cursor_);
    return message;
}
}
