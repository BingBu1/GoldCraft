#include "../../native/client/map_edit_history.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace goldcraft;
int main() {
    edits::Snapshot first{7, 42, {{99, {}, {{-1, -2, -3}, {4, 5, 6}}}}};
    edits::Snapshot same = first;
    const auto a = client_map::edit_work_identity(first, 3);
    const auto b = client_map::edit_work_identity(same, 3);
    assert(a.attempt == 3 && a.epoch == 7 && a.revision == 42 && a.cuts == 1 && a.cuts_hash == b.cuts_hash);
    same.cuts[0].box.max[2] = 7;
    assert(a.cuts_hash != client_map::edit_work_identity(same, 3).cuts_hash);

    client_map::EditWorkHistory history;
    history.record(client_map::EditStage::renderer_begin, a, 0.01, 1.0);
    history.record(client_map::EditStage::renderer_poll, a, 4.0, 2.0);
    history.record(client_map::EditStage::renderer_poll, a, 0.02, 3.0, true);
    assert(history.counters[static_cast<std::size_t>(client_map::EditStage::renderer_poll)].failed == 1);
    std::ostringstream output;
    history.write(output);
    const auto text = output.str();
    assert(text.find("\"revision\":\"42\"") != std::string::npos);
    assert(text.find("\"cuts\":1") != std::string::npos);
    assert(text.find("\"stage\":\"rendererPoll\"") != std::string::npos);
    // Idle frames must retain the operation that actually blocked. Ring wrap
    // is chronological, and 64-bit session identity survives JSON consumers.
    for (unsigned i = 0; i < 200000; ++i)
        history.record(client_map::EditStage::renderer_poll, b, .01, 4.0 + i);
    assert(history.event_count == 3);
    client_map::EditWorkIdentity wide{77, 18446744073709551614ull, 9007199254740993ull, 999, 123, 4};
    for (unsigned i = 0; i < 80; ++i) {
        wide.attempt = i;
        history.record(client_map::EditStage::collision_commit, wide, .01, 300000.0 + i);
    }
    assert(history.event_count == 83);
    assert(history.events[(history.event_count - history.capacity) % history.capacity].identity.attempt == 16);
    std::ostringstream wrapped; history.write(wrapped);
    assert(wrapped.str().find("\"epoch\":\"18446744073709551614\"") != std::string::npos);
    assert(wrapped.str().find("\"revision\":\"9007199254740993\"") != std::string::npos);
    assert(wrapped.str().find("\"overwritten\":19") != std::string::npos);
    // Busy mining can wrap the chronology too. The worst pause must retain
    // its original world/revision even after newer operations replace the ring.
    assert(wrapped.str().find("\"slowest\":{\"stage\":\"rendererPoll\",\"attempt\":\"3\",\"epoch\":\"7\",\"revision\":\"42\"") != std::string::npos);
    assert(wrapped.str().find("\"maxMs\":0,\"slowest\":null") != std::string::npos);
    history.record(client_map::EditStage::renderer_poll, wide, 5.0, 400000.0, true);
    const auto poll = static_cast<std::size_t>(client_map::EditStage::renderer_poll);
    assert(history.slowest[poll].identity.epoch == wide.epoch && history.slowest[poll].failed);
    history.record(client_map::EditStage::renderer_poll, a, 5.0, 400001.0);
    assert(history.slowest[poll].identity.epoch == wide.epoch);
    const auto stage = static_cast<std::size_t>(client_map::EditStage::physics_prepare);
    try {
        client_map::EditWorkScope scope(history, client_map::EditStage::physics_prepare, wide);
        throw std::runtime_error("fixture preparation failure");
    } catch (const std::runtime_error&) {}
    assert(history.counters[stage].count == 1 && history.counters[stage].failed == 1);
    {
        client_map::EditWorkScope scope(history, client_map::EditStage::physics_prepare, wide);
        scope.fail();
    }
    assert(history.counters[stage].count == 2 && history.counters[stage].failed == 2);
    constexpr double uptime = 1000000000.1234567;
    history.record(client_map::EditStage::collision_commit, wide, .125, uptime);
    std::ostringstream precise;
    precise.precision(4);
    history.write(precise);
    assert(precise.precision() == 4);
    const std::string timestamp = "\"completedSteadySeconds\":";
    const auto last = precise.str().rfind(timestamp);
    assert(last != std::string::npos);
    assert(std::stod(precise.str().substr(last + timestamp.size())) == uptime);
    std::cout << "Map edit stage history identity, failure and bounded JSON passed\n";
}
