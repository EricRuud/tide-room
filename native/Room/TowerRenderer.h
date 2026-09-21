#pragma once
#include "RoomProcessor.h"
#include "ListenerSpace.h"
#include "TowerOptics.h"
#include "RasterWork.h"

class TowerRenderer {
    using Vec=SphericalWater::Vec;
public:
    static constexpr int resolution=768;
    static tide::gpu::TowerParameters parameters(RoomProcessor& scene,int identity,Vec position,const ListenerSpace& view,float time,Vec flow){
        const auto vector=[](Vec v,float w=0.f){return tide::gpu::Float4{v.x,v.y,v.z,w};};
        tide::gpu::TowerParameters p{};Vec right,up,back;view.basis(position,right,up,back);
        p.originRadius=vector(position,tide::room::PlanetMotion::waterRadius);p.right=vector(right);p.up=vector(up);p.back=vector(back);
        p.room={view.width,view.depth,view.height,0};p.flow=vector(flow);
        const auto notes=scene.towerNotes(identity);const float heightScale=notes.roomScale(view.height);p.settings={(float)resolution,(float)identity,(float)notes.count,time};
        const bool muted=scene.get(RoomProcessor::partId(identity,"mute"))>.5f;
        for(int i=0;i<notes.count;++i)p.plates[i]={notes.radius(i),notes.elevation(i)*heightScale,notes.halfThickness()*heightScale,muted?0.f:std::clamp(scene.pitchLights[(size_t)identity][(size_t)notes.pitches[(size_t)i]].load(),0.f,1.f)};
        return p;
    }
    static juce::Rectangle<float> bounds(RoomProcessor& scene,int identity,Vec position,const ListenerSpace& view){
        const auto notes=scene.towerNotes(identity);const float half=(notes.elevation(std::max(0,notes.count-1))+notes.halfThickness())*notes.roomScale(view.height)*tide::room::towerRadius+.14f;
        juce::Rectangle<float> box;bool first=true;
        for(float x:{-tide::room::towerEnvelopeRadius,tide::room::towerEnvelopeRadius})for(float y:{-tide::room::towerEnvelopeRadius,tide::room::towerEnvelopeRadius})for(float z:{-half,half}){
            auto at=view.project(position+Vec{x,y,z});if(first){box={at.x,at.y,.001f,.001f};first=false;}else box=box.getUnion({at.x,at.y,.001f,.001f});}
        return box.expanded(2);
    }
    void draw(juce::Graphics& g,RoomProcessor& scene,int identity,Vec position,const ListenerSpace& view,juce::Point<float> centre,float radius,double seconds){
        auto& f=frames[(size_t)identity];advance(f,scene,identity,seconds);
        auto p=parameters(scene,identity,position,view,(float)f.time,f.flow);
        p.settings.x=(float)std::clamp(((int)std::ceil(radius*tide::tower::imageSpan*2)+31)/32*32,384,1024);
#if TIDE_CPU_PREVIEW
        p.settings.x=(float)std::clamp(((int)std::ceil(radius*tide::tower::imageSpan)+15)/16*16,128,256);
#endif
        if(!f.gpu)f.gpu=std::make_unique<tide::gpu::TowerPass>();
        if(!f.gpu->render(p,f.image))renderCPU(p,f.image);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.setOpacity(scene.get(RoomProcessor::partId(identity,"mute"))>.5f?.40f:1.f);
        g.drawImage(f.image,{centre.x-radius*(tide::tower::imageSpan*.5f),centre.y-radius*(tide::tower::imageSpan*.5f),radius*tide::tower::imageSpan,radius*tide::tower::imageSpan});g.setOpacity(1);
    }
    void advancePreview(RoomProcessor& scene,int identity,double seconds){advance(frames[(size_t)identity],scene,identity,seconds);}
    void drawFloorReflection(juce::Graphics& g,const ListenerSpace& view,Vec position,int identity)const {
        const auto& image=frames[(size_t)identity].image;if(image.isNull())return;
        Vec mirror{position.x,position.y,-position.z};auto centre=view.project(mirror);float r=view.radius(tide::room::PlanetMotion::waterRadius,mirror)*(tide::tower::imageSpan*.5f);
        juce::Graphics::ScopedSaveState save(g);g.reduceClipRegion(view.floorPath());g.setOpacity(.10f/(1+position.z*.35f));g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImageTransformed(image,juce::AffineTransform::scale(2*r/image.getWidth(),-2*r/image.getHeight()).translated(centre.x-r,centre.y+r),false);
    }
    static void renderCPU(const tide::gpu::TowerParameters& p,juce::Image& image){
        const int size=(int)p.settings.x;juce::Image base(juce::Image::ARGB,size,size,true),lit(juce::Image::ARGB,size,size,true);
        {juce::Image::BitmapData plain(base,juce::Image::BitmapData::writeOnly),lights(lit,juce::Image::BitmapData::writeOnly);
        tide::raster::rows(size,[&](int first,int last){for(int y=first;y<last;++y)for(int x=0;x<size;++x){
            const auto pixel=tide::tower::sample(p,((float)x+.5f-size*.5f)*(tide::tower::imageSpan/size),(size*.5f-(float)y-.5f)*(tide::tower::imageSpan/size));
            plain.setPixelColour(x,y,juce::Colour::fromFloatRGBA(pixel.colour.x,pixel.colour.y,pixel.colour.z,pixel.alpha));
            auto* row=reinterpret_cast<juce::PixelARGB*>(lights.getLinePointer(y));row[x].setARGB(byte(std::max({pixel.emission.x,pixel.emission.y,pixel.emission.z})*255),byte(pixel.emission.x*255),byte(pixel.emission.y*255),byte(pixel.emission.z*255));
        }});}
        if(image.getWidth()!=size||image.getHeight()!=size)image=juce::Image(juce::Image::ARGB,size,size,true);
        juce::Image::BitmapData plain(base,juce::Image::BitmapData::readOnly),lights(lit,juce::Image::BitmapData::readOnly),out(image,juce::Image::BitmapData::writeOnly);
        const float angle=p.settings.y*2.1f+.3f,dx=std::cos(angle)*size/tide::tower::imageSpan*tide::tower::colourSplit,dy=std::sin(angle)*size/tide::tower::imageSpan*tide::tower::colourSplit;
        const auto at=[&](int x,int y){if(x<0||y<0||x>=size||y>=size)return tide::gpu::Float4{};const auto pixel=reinterpret_cast<const juce::PixelARGB*>(lights.getLinePointer(y))[x];return tide::gpu::Float4{(float)pixel.getRed(),(float)pixel.getGreen(),(float)pixel.getBlue(),(float)pixel.getAlpha()};};
        const auto sample=[&](float x,float y){int ix=(int)std::floor(x),iy=(int)std::floor(y);float fx=x-ix,fy=y-iy;auto a=at(ix,iy),b=at(ix+1,iy),c=at(ix,iy+1),d=at(ix+1,iy+1);tide::gpu::Float4 value{};
            value.x=(a.x*(1-fx)+b.x*fx)*(1-fy)+(c.x*(1-fx)+d.x*fx)*fy;value.z=(a.z*(1-fx)+b.z*fx)*(1-fy)+(c.z*(1-fx)+d.z*fx)*fy;value.w=(a.w*(1-fx)+b.w*fx)*(1-fy)+(c.w*(1-fx)+d.w*fx)*fy;return value;};
        tide::raster::rows(size,[&](int first,int last){for(int y=first;y<last;++y){auto* dst=reinterpret_cast<juce::PixelARGB*>(out.getLinePointer(y));auto* src=reinterpret_cast<const juce::PixelARGB*>(plain.getLinePointer(y));for(int x=0;x<size;++x){
            auto red=sample((float)x+dx,(float)y+dy),blue=sample((float)x-dx,(float)y-dy),glow=at(x,y);dst[x].setARGB(byte(std::max({(float)src[x].getAlpha(),red.w,blue.w})),byte(src[x].getRed()-glow.x+red.x),src[x].getGreen(),byte(src[x].getBlue()-glow.z+blue.z));
        }}});
    }
private:
    struct Frame {juce::Image image;std::unique_ptr<tide::gpu::TowerPass> gpu;Vec flow{},velocity{};double time=0;};
    std::array<Frame,3> frames;
    static juce::uint8 byte(float v){return (juce::uint8)std::clamp((int)(v+.5f),0,255);}
    static void advance(Frame& f,RoomProcessor& scene,int identity,double seconds){
        const auto i=(size_t)identity;
        Vec target{scene.oceanSignals[i*2].load()*2.5f+scene.waterKicks[i][0].load()*.8f,scene.oceanSignals[i*2+1].load()*2.5f+scene.waterKicks[i][1].load()*.8f,0};
        target.x=std::clamp(target.x,-1.f,1.f);target.y=std::clamp(target.y,-1.f,1.f);
        seconds=std::clamp(seconds,0.,1.);f.time+=seconds;
        while(seconds>1.e-9){float dt=(float)std::min(seconds,1./120.);seconds-=dt;f.velocity=f.velocity+((target-f.flow)*45.f-f.velocity*6.f)*dt;f.flow=f.flow+f.velocity*dt;}
    }
};
