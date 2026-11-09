// Uses the real present clock, without a game, GPU or sleeping.
// c++ -std=c++20 -Iportablekit/host scripts/analyze-presentation-delay.cpp
//     portablekit/host/gpu/frame_pacing.cpp -o out/analyze-presentation-delay
#include "gpu/frame_pacing.hpp"

#include <cassert>
#include <iomanip>
#include <iostream>

int main() {
    namespace pacing = portablekit::gpu::pacing;
    pacing::set_game_frame_vblanks(2);
    std::cout << "source_fps,present_fps,buffer_ms,work_allowance_ms,total_ms\n";
    for (const int vblanks : {2, 3}) {
        const auto frame = std::int64_t(16683) * vblanks;
        for (const double rate : {30., 45., 60., 90., 120., 144.}) {
            pacing::PresentClock clock;
            clock.set_presents_per_frame(pacing::presents_per_frame(rate));
            for (int i = 1; i <= 120; ++i) clock.flip(i * frame, i * frame + 3000);
            const bool enabled = clock.next_due().has_value();
            const auto total = enabled ? clock.delay_us() : 0;
            const auto work = enabled ? clock.work_us() : 0;
            assert(clock.frame_us() == frame);
            if (rate == 30) assert(!enabled);
            if (rate == 60) assert(total - work == frame - 16683);
            if (rate == 144) assert(total - work > frame - 16683);
            std::cout << std::fixed << std::setprecision(3) << 60.0 / vblanks << ',' << rate << ','
                      << (total - work) / 1000.0 << ',' << work / 1000.0 << ',' << total / 1000.0 << '\n';
        }
    }
}
