#pragma once
#include <juce_graphics/juce_graphics.h>
#include "SphericalWater.h"
#include "ListenerSpace.h"
#include "WaterOptics.h"
#include "RasterWork.h"
#include "PlanetColour.h"
#include "GpuEffects.h"
#include <array>
#include <cmath>

// Metal ray tracing with a CPU fallback. Refraction is evaluated at both water
// interfaces; the liquid solver and all rendering work remain UI-only.
class PlanetRenderer {
    using Vec=SphericalWater::Vec;
public:
    static constexpr int resolution=768;
    static Vec noteTint(int planet){return lightTint(planet);}
    void setContext(int planet,Vec position,float radius,float width,float depth,float height=4) {
        ListenerSpace view;view.width=width;view.depth=depth;view.height=height;setContext(planet,position,radius,view);
    }
    void setContext(int planet,Vec position,float radius,const ListenerSpace& view) {
        auto& f=frames[(size_t)planet];
        f.context.origin=position;f.context.radius=radius;f.context.width=view.width;f.context.depth=view.depth;
        view.basis(position,f.context.right,f.context.up,f.context.back);
    }
    void draw(juce::Graphics& g,juce::Point<float> centre,float radius,int planet,float tideX,float tideY,double time,float noteLight=0,bool muted=false,float tideZ=0,juce::Point<float> smear={}) {
        auto& f=frames[(size_t)planet];initialise(f,planet);
        const float previousLight=f.light;f.light=muted?0.f:std::clamp(noteLight,0.f,1.f);
        const double elapsed=f.previous<0?0:std::clamp(time-f.previous,0.,.15);f.previous=time;
        f.water.advance(elapsed,tideX,tideY,tideZ);f.liquidTime+=elapsed;
        if(f.image.isNull()||f.renderedAt<0||time-f.renderedAt>=.038||std::abs(f.light-previousLight)>.0001f){render(f,planet,time);f.renderedAt=time;}
        if(f.light>.005f){const float halo=radius*1.8f;const auto tint=lightTint(planet);
            g.setGradientFill(juce::ColourGradient(juce::Colour::fromFloatRGBA(tint.x,tint.y,tint.z,.16f*f.light),centre,juce::Colours::transparentBlack,centre+juce::Point<float>{halo,0},true));
            g.fillEllipse(centre.x-halo,centre.y-halo,halo*2,halo*2);}
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.setOpacity(muted?.4f:1.f);
        const auto& texture=f.light>.0001f?f.chromatic:f.image;
        const float extent=radius*1.68f*(float)texture.getWidth()/(float)resolution;const juce::Rectangle<float> target{centre.x-extent,centre.y-extent,extent*2,extent*2};
        const float opacity=muted?.4f:1.f;
        if(smear.getDistanceFromOrigin()>.35f){
            for(int tap=4;tap>=1;--tap){const float amount=(float)tap*.25f;g.setOpacity(opacity*(.10f-.016f*(float)tap));g.drawImage(texture,target.translated(-smear.x*amount,-smear.y*amount));}
            g.setOpacity(opacity*.97f);
        }
        g.drawImage(texture,target);g.setOpacity(1);
    }
    void drawFloorReflection(juce::Graphics& g,const ListenerSpace& view,Vec position,float physicalRadius,int identity)const {
        const auto& f=frames[(size_t)identity];if(f.reflection.isNull())return;
        const auto mirror=Vec{position.x,position.y,-position.z};const auto centre=view.project(mirror);
        const float r=view.radius(physicalRadius,mirror)*1.68f;
        if(r<1)return;juce::Graphics::ScopedSaveState save(g);g.reduceClipRegion(view.floorPath());
        // A prefiltered image suggests a rough planar reflection while the
        // water itself retains its full-resolution optical detail.
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.setOpacity(.14f/(1+position.z*.35f));
        g.drawImageTransformed(f.reflection,juce::AffineTransform::scale(2*r/(float)f.reflection.getWidth(),-2*r/(float)f.reflection.getHeight()).translated(centre.x-r,centre.y+r),false);
        g.setOpacity(1);
    }
    static Vec viewTide(float x,float y,Vec physical,Vec kick={},const ListenerSpace& view={}) {
        // Display gain makes weak, distant attraction readable without changing CV.
        return view.cameraVector(Vec{x,y,0}*1.8f+kick*.48f,physical);
    }
    static void drawBridge(juce::Graphics& g,juce::Point<float> a,juce::Point<float> b,float ra,float rb,float strength) {
        const auto delta=b-a;const float distance=delta.getDistanceFromOrigin();if(strength<.025f||distance<std::min(ra,rb)*.6f||distance>(ra+rb)*1.6f)return;
        const auto direction=delta/distance,side=juce::Point<float>{-direction.y,direction.x};
        // Draw behind the shells. Only the liquid neck outside their silhouettes
        // remains visible, so contact never puts an outline across a solid core.
        const auto middle=(a+b)*.5f;const float r=std::min(ra,rb);
        const float stretch=std::clamp((distance/(ra+rb)-.65f)/.80f,0.f,1.f);
        const float neck=r*(.91f-.62f*stretch)*std::sqrt(strength);
        const auto upperA=a+side*(ra*.88f),upperB=b+side*(rb*.88f),lowerA=a-side*(ra*.88f),lowerB=b-side*(rb*.88f);
        juce::Path bridge;bridge.startNewSubPath(upperA);bridge.cubicTo(middle+side*neck,middle+side*neck,upperB);
        bridge.lineTo(lowerB);bridge.cubicTo(middle-side*neck,middle-side*neck,lowerA);bridge.closeSubPath();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff8bf0f4).withAlpha(.22f*strength),middle-side*r,juce::Colour(0xff668cdd).withAlpha(.12f*strength),middle+side*r,false));g.fillPath(bridge);
        juce::Path edges;edges.startNewSubPath(upperA);edges.cubicTo(middle+side*neck,middle+side*neck,upperB);
        edges.startNewSubPath(lowerA);edges.cubicTo(middle-side*neck,middle-side*neck,lowerB);
        g.setColour(juce::Colour(0xffd5fbff).withAlpha(.54f*strength));g.strokePath(edges,juce::PathStrokeType(1.f));
    }
    void preview(int planet,float tideX,float tideY,double seconds){frames[(size_t)planet]=Frame{};advancePreview(planet,tideX,tideY,seconds);}
    void advancePreview(int planet,float tideX,float tideY,double seconds,float tideZ=0) {
        auto& f=frames[(size_t)planet];initialise(f,planet);f.liquidTime+=seconds;while(seconds>1.e-9){const double dt=std::min(seconds,.04);f.water.advance(dt,tideX,tideY,tideZ);seconds-=dt;}f.previous=-1;f.renderedAt=-1;
    }
private:
    struct Context {Vec origin{0,3,1.5f},right{1,0,0},up{0,0,1},back{0,-1,0};float radius=.28f,width=10,depth=8;};
    struct Frame {SphericalWater water;juce::Image image,chromatic,reflection;Context context;std::unique_ptr<tide::gpu::PlanetPass> gpu;double previous=-1,renderedAt=-1,liquidTime=0;float light=0;bool ready=false;};
    struct Shape {float base=1.015f,axialInverse=1,transverseInverse=1;Vec dipole,axis,inverseAxes{1,1,1};std::array<Vec,3> waves;std::array<float,3> phase,amplitude;};
    std::array<Frame,3> frames;
    static void initialise(Frame& f,int identity){if(!f.ready){f.water.prepare(identity,true);f.ready=true;}}
    static float smooth(float a,float b,float v){const float t=std::clamp((v-a)/(b-a),0.f,1.f);return t*t*(3-2*t);}
    static Shape shape(const SphericalWater& water,float time,int identity) {
        Shape s;const auto& m=SphericalWater::mesh();float average=0;Vec first;
        for(size_t i=0;i<m.points.size();++i){const float h=water.bed[i]+water.depth[i],w=m.area[i]/12.5663706144f;
            average+=h*w;first=first+m.points[i]*(h*w*3);}
        s.base=1+average;s.dipole=first*2.6f;const float length=s.dipole.length();if(length>.24f)s.dipole=s.dipole*(.24f/length);
        // The solver's first mass moment supplies both the lagging water centre
        // and the stretching axis. A reversed gravitational pull travels through
        // the liquid before the silhouette turns; the solid core stays put.
        s.axis=first.unit();const float stretch=1+.30f*std::tanh(first.length()*9);
        s.axialInverse=1/stretch;s.transverseInverse=std::sqrt(stretch);
        const float energy=std::min(1.f,water.maximumSpeed()*4),phase=(float)identity*2.1f;
        // Slow volume-preserving swell beneath several independently travelling
        // wave bands. Tide/current energy strengthens the rolling surface.
        const float a=1+(.035f+.035f*energy)*std::sin(time*1.7f+phase);
        const float b=1+(.028f+.025f*energy)*std::sin(time*1.31f-phase);
        s.inverseAxes={1/a,1/b,a*b};
        const float turn=time*.14f+phase,cs=std::cos(turn),sn=std::sin(turn);
        s.waves={Vec{5*cs,4,5*sn},Vec{-11*sn,9,11*cs},Vec{23,17*sn,17*cs}};
        s.phase={-time*1.8f+phase,-time*2.7f-phase,time*3.5f+phase*.7f};
        s.amplitude={.024f+.024f*energy,.011f+.009f*energy,.0038f+.003f*energy};return s;
    }
    static Vec axial(const Shape& s,Vec v) {
        return v*s.transverseInverse+s.axis*(v.dot(s.axis)*(s.axialInverse-s.transverseInverse));
    }
    static Vec toSphere(const Shape& s,Vec p) {
        const auto v=axial(s,p);return {v.x*s.inverseAxes.x,v.y*s.inverseAxes.y,v.z*s.inverseAxes.z};
    }
    static Vec normal(const Shape& s,Vec point) {
        const auto p=point-s.dipole,v=toSphere(s,p);
        const auto n=axial(s,{v.x*s.inverseAxes.x,v.y*s.inverseAxes.y,v.z*s.inverseAxes.z}).unit();
        const auto direction=p.unit();Vec grad;
        for(size_t i=0;i<s.waves.size();++i)grad=grad+s.waves[i]*(std::cos(s.waves[i].dot(direction)+s.phase[i])*s.amplitude[i]);
        grad=grad-n*grad.dot(n);return (n-grad).unit();
    }
    static bool surface(const Shape& s,Vec origin,Vec direction,Vec& point) {
        const auto o=toSphere(s,origin-s.dipole),d=toSphere(s,direction);
        const float a=d.dot(d),b=o.dot(d),c=o.dot(o)-s.base*s.base,discriminant=b*b-a*c;
        if(discriminant<0)return false;const float root=std::sqrt(discriminant),front=(-b-root)/a,back=(-b+root)/a;
        const float t=front>.0001f?front:back;if(t<=.0001f)return false;point=origin+direction*t;return true;
    }
    static Vec studio(Vec direction) {
        const float z=std::max(.05f,direction.z),u=direction.x/z,v=direction.y/z;
        const float key=(1-smooth(.32f,.43f,std::abs(u+.75f)))*(1-smooth(.60f,.72f,std::abs(v-.95f)));
        const float strip=(1-smooth(.035f,.065f,std::abs(u-.83f)))*(1-smooth(.8f,1.1f,std::abs(v-.2f)));
        const float wash=std::max(0.f,direction.y)*.09f;
        const float violet=(1-smooth(.045f,.11f,std::abs(u+1.65f)))*(1-smooth(.7f,1.4f,std::abs(v+.25f)));
        const float horizon=1-smooth(.025f,.10f,std::abs(direction.y+.18f));
        return Vec{.20f+wash,.32f+wash,.39f+wash}+Vec{6.4f,6.6f,6.8f}*key+Vec{2.0f,3.6f,4.1f}*strip+Vec{2.4f,.85f,3.2f}*violet+Vec{.8f,1.7f,1.9f}*horizon;
    }
    static Vec environment(Vec origin,Vec direction,const Context& context) {
        const auto toWorld=[&](Vec v){return context.right*v.x+context.up*v.y+context.back*v.z;};
        const auto p=toWorld(origin)*context.radius+context.origin,d=toWorld(direction);
        if(d.z<-.0001f){const float t=-p.z/d.z;if(t>0){const auto hit=p+d*t;if(std::abs(hit.x)<context.width*.5f&&hit.y>0&&hit.y<context.depth){
            return tide::surface::floor(hit.x,hit.y,context.width,context.depth);}}}
        return { .043f,.063f,.086f };
    }
    static Vec lightTint(int identity){const std::array<Vec,3> tint{{{1.f,.88f,.67f},{.57f,.90f,1.f},{.88f,.73f,1.f}}};return tint[(size_t)identity];}
    static Vec core(Vec p,Vec view,int identity,float activity) {
        const auto n=p.unit();const auto light=Vec{-.6f,.9f,1.3f}.unit();const float diffuse=std::max(0.f,n.dot(light));
        const std::array<Vec,3> tones{{{.76f,.56f,.32f},{.25f,.61f,.64f},{.58f,.37f,.66f}}};
        float sheen=std::max(0.f,n.dot((light+view).unit()));for(int i=0;i<5;++i)sheen*=sheen;sheen*=.48f;
        const auto reflection=studio(tide::optics::reflect(view*-1,n));
        const float fresnel=.10f+.24f*std::pow(1-std::max(0.f,n.dot(view)),3.f);
        return tones[(size_t)identity]*(.12f+.74f*diffuse)+reflection*fresnel+Vec{sheen,sheen,sheen}+lightTint(identity)*(activity*.80f);
    }
    static Vec transmission(const Shape& s,const Context& context,Vec entry,Vec direction,int identity,float activity) {
        const float hit=tide::optics::sphere(entry,direction,.60f);
        if(hit>0){auto value=core(entry+direction*hit,direction*-1,identity,activity);return {value.x*tide::optics::absorption(.065f*hit),value.y*tide::optics::absorption(.024f*hit),value.z*tide::optics::absorption(.014f*hit)};}
        // Find the far interface and refract water -> air. Internal reflection
        // uses the studio environment rather than an unbounded ray bounce loop.
        Vec origin=entry+direction*.001f;
        Vec exit;if(!surface(s,origin,direction,exit))return environment(entry,direction,context);
        const auto n=normal(s,exit);Vec outgoing;
        if(!tide::optics::refract(direction,n*-1,1.333f,outgoing))return studio(tide::optics::reflect(direction,n))*.65f;
        const float f=tide::optics::fresnel(std::abs(direction.dot(n)),1.333f,1.f);
        return environment(exit,outgoing,context)*(1-f)+studio(tide::optics::reflect(direction,n))*f;
    }
    static void floorReflection(Frame& f) {
        constexpr int size=48;juce::Image reduced(juce::Image::ARGB,size,size,true);
        {juce::Graphics g(reduced);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(f.image,reduced.getBounds().toFloat());}
        if(f.reflection.isNull())f.reflection=juce::Image(juce::Image::ARGB,size,size,true);
        juce::Image::BitmapData source(reduced,juce::Image::BitmapData::readOnly),target(f.reflection,juce::Image::BitmapData::writeOnly);
        for(int y=0;y<size;++y){auto* dst=reinterpret_cast<juce::PixelARGB*>(target.getLinePointer(y));for(int x=0;x<size;++x){int a=0,r=0,g=0,b=0;
            for(int j=-2;j<=2;++j){const auto* row=reinterpret_cast<const juce::PixelARGB*>(source.getLinePointer(std::clamp(y+j,0,size-1)));
                for(int i=-2;i<=2;++i){const auto pixel=row[std::clamp(x+i,0,size-1)];a+=pixel.getAlpha();r+=pixel.getRed();g+=pixel.getGreen();b+=pixel.getBlue();}}
            dst[x].setARGB((juce::uint8)(a/25),(juce::uint8)(r/25),(juce::uint8)(g/25),(juce::uint8)(b/25));}}
    }
    static void render(Frame& f,int identity,double) {
        if(f.image.isNull())f.image=juce::Image(juce::Image::ARGB,resolution,resolution,true);
        const auto s=shape(f.water,(float)f.liquidTime,identity);
        if(tide::gpu::available()){
            const auto vector=[](Vec v,float w=0.f){return tide::gpu::Float4{v.x,v.y,v.z,w};};
            tide::gpu::PlanetParameters p{};p.shape={s.base,s.axialInverse,s.transverseInverse,f.light};
            p.dipole=vector(s.dipole);p.axis=vector(s.axis);p.inverseAxes=vector(s.inverseAxes);
            for(size_t i=0;i<3;++i)p.waves[i]=vector(s.waves[i]);
            p.phases={s.phase[0],s.phase[1],s.phase[2],0};p.amplitudes={s.amplitude[0],s.amplitude[1],s.amplitude[2],0};
            p.originRadius=vector(f.context.origin,f.context.radius);p.right=vector(f.context.right);p.up=vector(f.context.up);p.back=vector(f.context.back);
            p.room={f.context.width,f.context.depth,0,0};p.settings={(float)resolution,(float)identity,0,0};
            if(!f.gpu)f.gpu=std::make_unique<tide::gpu::PlanetPass>();
            if(f.gpu->render(p,f.image,f.chromatic)){floorReflection(f);return;}
        }
        {juce::Image::BitmapData pixels(f.image,juce::Image::BitmapData::writeOnly);const Vec incident{0,0,-1};
        tide::raster::rows(resolution,[&](int first,int last){
        for(int y=first;y<last;++y)for(int x=0;x<resolution;++x){const float u=((float)x+.5f-resolution*.5f)*(3.36f/resolution),v=(resolution*.5f-(float)y-.5f)*(3.36f/resolution);
            Vec point;if(u*u+v*v>1.67f*1.67f||!surface(s,{u,v,3},incident,point)){pixels.setPixelColour(x,y,juce::Colours::transparentBlack);continue;}
            const auto n=normal(s,point);Vec ray;const bool transmitted=tide::optics::refract(incident,n,1.f/1.333f,ray);
            const float fresnel=tide::optics::fresnel(std::max(0.f,n.z),1,1.333f);
            const auto refracted=transmitted?transmission(s,f.context,point,ray,identity,f.light):Vec{};
            const auto reflected=studio(tide::optics::reflect(incident,n));auto colour=refracted*(1-fresnel)+reflected*fresnel;
            // Preserve broad highlights while rolling off the softbox HDR peak.
            const auto display=[](float value){return std::clamp(value<.8f?value:.8f+.2f*(1-std::exp(-(value-.8f)*5)),0.f,1.f);};
            pixels.setPixelColour(x,y,juce::Colour::fromFloatRGBA(display(colour.x),display(colour.y),display(colour.z),1));
        }
        });}
        if(f.light>.0001f)PlanetColour::render(f.image,f.chromatic,f.light,identity);
        floorReflection(f);
    }
};
