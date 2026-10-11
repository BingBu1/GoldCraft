#pragma once
#include "goldcraft/endpoint.hpp"
#include <ostream>

namespace goldcraft {
inline void write_transport_stats(std::ostream& out,const TransportStats& s) {
    out<<"\"transport\":{\"sharedActive\":"<<(s.shared_active?"true":"false")
       <<",\"offered\":"<<s.mapping_offered<<",\"enabled\":"<<s.mapping_enabled<<",\"fallback\":"<<s.mapping_fallback
       <<",\"rejections\":"<<s.rejections<<",\"disconnects\":"<<s.disconnects<<",\"ringHighWater\":"<<s.ring_high_water
       <<",\"queuedBytes\":"<<s.queued_bytes<<",\"queueHighWater\":"<<s.queued_high_water
       <<",\"incomingBytes\":"<<s.incoming_bytes<<",\"incomingHighWater\":"<<s.incoming_high_water
       <<",\"queueAgeNs\":"<<s.queue_age_ns<<",\"peakQueueAgeNs\":"<<s.peak_queue_age_ns
       <<",\"pendingAgeNs\":"<<s.pending_age_ns<<",\"peakPendingAgeNs\":"<<s.peak_pending_age_ns
       <<",\"encodeNs\":"<<s.encode_ns<<",\"copyNs\":"<<s.copy_ns<<",\"socketNs\":"<<s.socket_ns<<",\"drainNs\":"<<s.drain_ns
       <<",\"socketBytesSent\":"<<s.socket_bytes_sent<<",\"socketBytesReceived\":"<<s.socket_bytes_received;
    const auto counters=[&](const char* name,const auto& counts){
        out<<",\""<<name<<"\":{";bool first=true;
        for(std::size_t i=0;i<counts.size();++i){const auto& c=counts[i];if(!c.frames&&!c.bytes)continue;
            if(!first)out<<',';first=false;out<<'"'<<i<<"\":{\"frames\":"<<c.frames<<",\"bytes\":"<<c.bytes<<'}';}
        out<<'}';
    };
    counters("tcpSent",s.tcp_sent);counters("tcpReceived",s.tcp_received);
    counters("sharedSent",s.shared_sent);counters("sharedReceived",s.shared_received);out<<'}';
}
}
