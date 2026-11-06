#include "display/frame_history.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
int main() {
    using sims2::display::FrameHistory;
    try {
        const auto require=[](bool ok,const char *why){if(!ok)throw std::runtime_error(why);};
        FrameHistory history;
        require(history.summary().fps==0,"Empty history reports FPS");
        history.observe(0,true);history.observe(16667,true);
        require(history.count==1 && std::abs(history.summary().fps-60)<.01f,"Normal 60 Hz cadence");
        history.observe(516667,true);
        require(history.count==2 && history.summary().maximum==500,"Half-second stutter was hidden");
        require(history.summary().fps<4,"FPS ignored the stall");
        history.observe(600000,false);history.observe(9000000,true);history.observe(9016667,true);
        require(history.count==3 && history.summary().maximum==500,"Settings pause contaminated timings");
        history.reset_anchor();history.observe(30000000,true);
        require(history.count==3,"Minimized interval entered history");
        history.observe(30000000,true);history.observe(29999999,true);
        require(history.count==3,"Duplicate or nonmonotonic timestamp entered history");
        FrameHistory capped;
        for(unsigned i=0;i<=240;++i)capped.observe(std::uint64_t(i)*33333,true);
        require(capped.count==120 && capped.sample==240,"Frame history ring capacity");
        require(std::abs(capped.summary().fps-30)<.01f,"Presentation cap is misreported");
        capped.observe(240ull*33333+300000,true);
        require(capped.summary().maximum==300,"Wrapped ring dropped a stall");
        std::cout<<"Presentation history: cadence, stalls, pauses, clock discontinuities and ring passed\n";
        return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
