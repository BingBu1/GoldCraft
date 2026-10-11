#include "goldcraft/authoritative_dimensions.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <sstream>

using namespace goldcraft;
int main() {
    unsigned checks = 0;
    const auto check = [&](bool value) { ++checks; if (!value) throw std::runtime_error("dimension check failed"); };
    const auto good = [&](float eye, float width, float height) {
        for (std::uint32_t flags = 0; flags <= 7; ++flags) check(validate_authoritative_dimensions(flags,eye,width,height) == DimensionRejection::none);
    };
    good(1.62f,.6f,1.8f); // vanilla standing
    good(1.27f,.6f,1.5f); // vanilla crouching
    good(.4f,.6f,.6f); // vanilla swimming/crawling
    good(1.62f,.6f,.8f); // real Epic roll leaves eye height at standing value
    good(1.62f,.8f,.6f); // real Epic knockdown wakeup body
    good(1.62f,.6f,1.65f); // Epic step
    good(0,.1f,.1f); good(authoritative_eye_max,4,4);
    check(1.62f > .8f + .1f); // former relative guard deterministically rejects roll
    check(validate_authoritative_dimensions(8,1.62f,.6f,1.8f) == DimensionRejection::flags);
    for (float width : {0.f,.099f,4.001f,-1.f}) check(validate_authoritative_dimensions(1,1.62f,width,1.8f) == DimensionRejection::width);
    for (float height : {0.f,.099f,4.001f,-1.f}) check(validate_authoritative_dimensions(1,1.62f,.6f,height) == DimensionRejection::height);
    for (float eye : {-1.f,-.001f,4.101f,100.f}) check(validate_authoritative_dimensions(1,eye,.6f,1.8f) == DimensionRejection::eye);
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
        check(validate_authoritative_dimensions(1,invalid,.6f,1.8f) == DimensionRejection::non_finite);
        check(validate_authoritative_dimensions(1,1.62f,invalid,1.8f) == DimensionRejection::non_finite);
        check(validate_authoritative_dimensions(1,1.62f,.6f,invalid) == DimensionRejection::non_finite);
    }
    AuthoritativeDimensionTrace trace;
    trace.observe(DimensionRejection::none,22,3,3,1.62f,.6f,.8f);
    for (int i=0;i<100000;++i) trace.observe(DimensionRejection::eye,22,3,3,5,.6f,.8f);
    std::ostringstream output; trace.write(output);
    check(output.str().find("\"accepted\":1,\"eyeAboveBody\":1,\"rejected\":100000") != std::string::npos);
    check(output.str().size()<256);
    trace.observe(DimensionRejection::non_finite,22,3,3,std::numeric_limits<float>::quiet_NaN(),.6f,.8f);
    std::ostringstream finite; trace.write(finite); check(finite.str().find("\"eye\":null") != std::string::npos);
    static_assert(sizeof(AuthoritativeDimensionTrace)<=64);
    std::cout << "PASS " << checks << " authoritative dimension checks\n";
}
