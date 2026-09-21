#include "GpuEffects.h"
#include "RoomShaderAssets.h"
#import <Metal/Metal.h>
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace tide::gpu {
static_assert(sizeof(TowerParameters)==368 && sizeof(Float4)==16 && sizeof(PlanetParameters)==240 && sizeof(GlassParameters)==128 && sizeof(Effects)==48);
namespace {
std::atomic<uint64_t> planetCount{0},glassCount{0},effectCount{0},failureCount{0};
bool disabled(){const auto* value=std::getenv("TIDE_DISABLE_METAL");return value&&value[0]=='1';}
struct Context {
    id<MTLDevice> device=nil;
    id<MTLCommandQueue> queue=nil;
    id<MTLComputePipelineState> pipelines[10]{};
    std::atomic<bool> ready{false};
    juce::String name;
    Context(){@autoreleasepool {
        device=MTLCreateSystemDefaultDevice();if(!device){name="CPU renderer (no Metal device)";return;}
        NSError* error=nil;
        auto data=dispatch_data_create(RoomShaderAssets::RoomEffects_metallib,RoomShaderAssets::RoomEffects_metallibSize,dispatch_get_main_queue(),DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        id<MTLLibrary> library=[device newLibraryWithData:data error:&error];
        if(!library){name="CPU renderer (Metal library: "+juce::String([[error localizedDescription] UTF8String])+")";return;}
        NSString* names[]={@"planet",@"planetColour",@"glassMap",@"glassRefract",@"downsample",@"blur",@"lensFinish",@"visualEffects",@"tower",@"towerColour"};
        for(int i=0;i<10;++i){id<MTLFunction> function=[library newFunctionWithName:names[i]];
            if(!function){name="CPU renderer (missing Metal kernel)";return;}
            pipelines[i]=[device newComputePipelineStateWithFunction:function error:&error];
            if(!pipelines[i]){name="CPU renderer (Metal pipeline: "+juce::String([[error localizedDescription] UTF8String])+")";return;}}
        queue=[device newCommandQueue];if(!queue){name="CPU renderer (no Metal command queue)";return;}
        name="Metal / "+juce::String([[device name] UTF8String]);ready=true;
    }}
};
Context& context(){static Context value;return value;}
bool fail(){context().ready=false;++failureCount;juce::Logger::writeToLog("Tide Room: Metal pass failed; continuing with CPU rendering.");return false;}
struct Buffer {
    id<MTLBuffer> value=nil;
    bool ensure(size_t bytes){if(value&&[value length]>=bytes)return true;
        value=[context().device newBufferWithLength:std::max<size_t>(bytes,16) options:MTLResourceStorageModeShared];return value!=nil;}
};
void dispatch(id<MTLComputeCommandEncoder> encoder,int kernel,int w,int h){
    [encoder setComputePipelineState:context().pipelines[kernel]];
    [encoder dispatchThreads:MTLSizeMake((NSUInteger)w,(NSUInteger)h,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
}
bool complete(id<MTLCommandBuffer> command){if(!command)return fail();[command commit];[command waitUntilCompleted];return [command status]==MTLCommandBufferStatusCompleted?true:fail();}
void readImage(Buffer& buffer,juce::Image& image,int w,int h){
    if(image.getWidth()!=w||image.getHeight()!=h)image=juce::Image(juce::Image::ARGB,w,h,true);
    juce::Image::BitmapData dst(image,juce::Image::BitmapData::writeOnly);const auto* bytes=(const unsigned char*)[buffer.value contents];
    for(int y=0;y<h;++y)std::memcpy(dst.getLinePointer(y),bytes+(size_t)y*(size_t)w*4,(size_t)w*4);
}
void writeImage(Buffer& buffer,const juce::Image& image){
    juce::Image::BitmapData src(image,juce::Image::BitmapData::readOnly);auto* bytes=(unsigned char*)[buffer.value contents];
    for(int y=0;y<image.getHeight();++y)std::memcpy(bytes+(size_t)y*(size_t)image.getWidth()*4,src.getLinePointer(y),(size_t)image.getWidth()*4);
}
}
bool available(){return !disabled()&&context().ready;}
juce::String description(){if(disabled())return "CPU renderer (Metal disabled)";auto& c=context();return c.ready?c.name:c.name+" / CPU fallback";}
Statistics statistics(){return {planetCount.load(),glassCount.load(),effectCount.load(),failureCount.load()};}
struct PlanetPass::Impl {Buffer plain,split;};
PlanetPass::PlanetPass()=default;
PlanetPass::~PlanetPass()=default;
bool PlanetPass::render(const PlanetParameters& p,juce::Image& plain,juce::Image& split){@autoreleasepool {
    if(!available())return false;if(!impl)impl=std::make_unique<Impl>();auto& b=*impl;
    const int size=(int)p.settings.x,pad=(size+11)/12,extent=size+pad*2;
    if(!b.plain.ensure((size_t)size*(size_t)size*4)||!b.split.ensure((size_t)extent*(size_t)extent*4))return fail();
    id<MTLCommandBuffer> command=[context().queue commandBuffer];id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];if(!encoder)return fail();
    [encoder setBuffer:b.plain.value offset:0 atIndex:0];[encoder setBytes:&p length:sizeof(p) atIndex:1];dispatch(encoder,0,size,size);
    const bool lit=p.shape.w>.0001f;
    if(lit){[encoder setBuffer:b.plain.value offset:0 atIndex:0];[encoder setBuffer:b.split.value offset:0 atIndex:1];[encoder setBytes:&p length:sizeof(p) atIndex:2];dispatch(encoder,1,extent,extent);}
    [encoder endEncoding];if(!complete(command))return false;
    readImage(b.plain,plain,size,size);if(lit)readImage(b.split,split,extent,extent);++planetCount;return true;
}}
struct TowerPass::Impl {Buffer plain,lit,finished;};
TowerPass::TowerPass()=default;
TowerPass::~TowerPass()=default;
bool TowerPass::render(const TowerParameters& p,juce::Image& image){@autoreleasepool {
    if(!available())return false;if(!impl)impl=std::make_unique<Impl>();auto& b=*impl;
    const int size=(int)p.settings.x;const size_t bytes=(size_t)size*(size_t)size*4;
    if(!b.plain.ensure(bytes)||!b.lit.ensure(bytes)||!b.finished.ensure(bytes))return fail();
    id<MTLCommandBuffer> command=[context().queue commandBuffer];id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];if(!encoder)return fail();
    [encoder setBuffer:b.plain.value offset:0 atIndex:0];[encoder setBuffer:b.lit.value offset:0 atIndex:1];[encoder setBytes:&p length:sizeof(p) atIndex:2];dispatch(encoder,8,size,size);
    [encoder setBuffer:b.plain.value offset:0 atIndex:0];[encoder setBuffer:b.lit.value offset:0 atIndex:1];[encoder setBuffer:b.finished.value offset:0 atIndex:2];[encoder setBytes:&p length:sizeof(p) atIndex:3];dispatch(encoder,9,size,size);
    [encoder endEncoding];if(!complete(command))return false;readImage(b.finished,image,size,size);++planetCount;return true;
}}
struct GlassPass::Impl {Buffer source,map,refracted,soft,scratch,finished,effects;GlassParameters parameters{};int w=0,h=0,sw=0,sh=0;bool prepared=false;};
GlassPass::GlassPass()=default;
GlassPass::~GlassPass()=default;
bool GlassPass::prepare(const GlassParameters& p){@autoreleasepool {
    if(!available())return false;if(!impl)impl=std::make_unique<Impl>();auto& b=*impl;
    if(b.prepared&&std::memcmp(&p,&b.parameters,sizeof(p))==0)return true;
    b.prepared=false;b.parameters=p;b.w=(int)p.sizeScale.x;b.h=(int)p.sizeScale.y;
    b.sw=std::max(2,juce::roundToInt(p.bounds.z/3));b.sh=std::max(2,juce::roundToInt(p.bounds.w/3));
    const size_t bytes=(size_t)b.w*(size_t)b.h*4,softBytes=(size_t)b.sw*(size_t)b.sh*4;
    if(!b.source.ensure(bytes)||!b.refracted.ensure(bytes)||!b.finished.ensure(bytes)||!b.soft.ensure(softBytes)||!b.scratch.ensure(softBytes)
        ||!b.map.ensure(p.sizeScale.w>.5f?48:(size_t)b.w*(size_t)b.h*48))return fail();
    if(p.sizeScale.w<.5f){id<MTLCommandBuffer> command=[context().queue commandBuffer];id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];if(!encoder)return fail();
        [encoder setBuffer:b.map.value offset:0 atIndex:0];[encoder setBytes:&p length:sizeof(p) atIndex:1];dispatch(encoder,2,b.w,b.h);[encoder endEncoding];if(!complete(command))return false;}
    b.prepared=true;return true;
}}
bool GlassPass::render(const juce::Image& source,juce::Image& target,const Lights& lights,const Effects& fx){@autoreleasepool {
    if(!available()||!impl||!impl->prepared)return false;auto& b=*impl;const auto& p=b.parameters;
    if(source.getWidth()!=b.w||source.getHeight()!=b.h)return false;writeImage(b.source,source);
    const bool effects=fx.controls.y>0&&(fx.layers.x>0||fx.layers.y>0||fx.layers.z>0||fx.layers.w>0||fx.controls.x>0);
    if(effects&&!b.effects.ensure((size_t)b.w*(size_t)b.h*4))return fail();
    id<MTLCommandBuffer> command=[context().queue commandBuffer];id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];if(!encoder)return fail();
    [encoder setBuffer:b.source.value offset:0 atIndex:0];[encoder setBuffer:b.refracted.value offset:0 atIndex:1];[encoder setBuffer:b.map.value offset:0 atIndex:2];
    [encoder setBytes:&p length:sizeof(p) atIndex:3];[encoder setBytes:&lights length:sizeof(lights) atIndex:4];dispatch(encoder,3,b.w,b.h);
    Float4 dimensions{(float)b.w,(float)b.h,(float)b.sw,(float)b.sh};
    [encoder setBuffer:b.refracted.value offset:0 atIndex:0];[encoder setBuffer:b.soft.value offset:0 atIndex:1];[encoder setBytes:&dimensions length:sizeof(dimensions) atIndex:2];dispatch(encoder,4,b.sw,b.sh);
    dimensions={(float)b.sw,(float)b.sh,1,0};
    [encoder setBuffer:b.soft.value offset:0 atIndex:0];[encoder setBuffer:b.scratch.value offset:0 atIndex:1];[encoder setBytes:&dimensions length:sizeof(dimensions) atIndex:2];dispatch(encoder,5,b.sw,b.sh);
    dimensions.z=0;
    [encoder setBuffer:b.scratch.value offset:0 atIndex:0];[encoder setBuffer:b.soft.value offset:0 atIndex:1];[encoder setBytes:&dimensions length:sizeof(dimensions) atIndex:2];dispatch(encoder,5,b.sw,b.sh);
    [encoder setBuffer:b.refracted.value offset:0 atIndex:0];[encoder setBuffer:b.soft.value offset:0 atIndex:1];[encoder setBuffer:b.finished.value offset:0 atIndex:2];[encoder setBuffer:b.map.value offset:0 atIndex:3];
    [encoder setBytes:&p length:sizeof(p) atIndex:4];[encoder setBytes:&dimensions length:sizeof(dimensions) atIndex:5];dispatch(encoder,6,b.w,b.h);
    if(effects){dimensions={(float)b.w,(float)b.h,0,0};[encoder setBuffer:b.finished.value offset:0 atIndex:0];[encoder setBuffer:b.effects.value offset:0 atIndex:1];
        [encoder setBytes:&dimensions length:sizeof(dimensions) atIndex:2];[encoder setBytes:&fx length:sizeof(fx) atIndex:3];dispatch(encoder,7,b.w,b.h);}
    [encoder endEncoding];if(!complete(command))return false;
    readImage(effects?b.effects:b.finished,target,b.w,b.h);++glassCount;if(effects)++effectCount;return true;
}}
}
