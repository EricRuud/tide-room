#pragma once
#include <algorithm>
#if JUCE_MAC
#include <dispatch/dispatch.h>
#elif JUCE_WINDOWS
#include <ppl.h>
#endif

// Synchronous, independent raster bands. These workers never access the audio
// engine or JUCE components, only disjoint rows of already locked image data.
namespace tide::raster {
template<class Function> void rows(int height,const Function& function) {
#if JUCE_MAC
    struct Work {int height;const Function& function;} work{height,function};
    dispatch_apply_f(32,dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW,0),&work,[](void* context,size_t band){
        auto& w=*static_cast<Work*>(context);w.function(w.height*(int)band/32,w.height*((int)band+1)/32);
    });
#elif JUCE_WINDOWS
    concurrency::parallel_for(0,16,[&](int band){function(height*band/16,height*(band+1)/16);});
#else
    function(0,height);
#endif
}
}
