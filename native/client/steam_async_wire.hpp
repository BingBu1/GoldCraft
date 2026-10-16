#pragma once
#include <cstdint>
namespace goldcraft::steam_async::wire {
inline constexpr std::uint32_t magic=0x53414347,version=2;
enum class Op:std::uint32_t { avatar=1,start,stop,capture,quit,status };
#pragma pack(push,4)
struct Request {std::uint32_t signature=magic,protocol=version;Op op{};
    std::uint64_t id{};std::uint32_t size{},pcm{},rate{};};
struct Reply {std::uint32_t signature=magic,protocol=version;std::int32_t result{};
    std::uint32_t width{},height{};std::int32_t relationship{},handle{};
    std::uint32_t compressed{},pcm{},app_id{};std::int32_t logged_on=-1;};
#pragma pack(pop)
static_assert(sizeof(Request)==32&&sizeof(Reply)==44);
}
