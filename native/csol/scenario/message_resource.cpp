// SPDX-License-Identifier: GPL-3.0-or-later
#include "message_table.hpp"
#include "../config_codec.hpp"

namespace csol::scenario
{
bool ReadMessageTableResource(std::span<const std::byte> encoded,
    MessageTable& output, ConfigError& error)
{
    error = {};
    if (encoded.empty() || encoded.size() > 8 * 1024 * 1024 || encoded.size() % 8)
    {
        error.message = "invalid encrypted scenario message table size";
        return false;
    }
    std::string plain(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    DecodeConfiguration(std::as_writable_bytes(std::span(plain.data(), plain.size())));
    while (!plain.empty() && plain.back() == '\0') plain.pop_back();
    return ReadMessageTable(plain, output, error);
}
}
