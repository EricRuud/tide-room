#pragma once
#include "ListenerSpace.h"
#include "WaterOptics.h"
#include "RasterWork.h"
#include "NoteLens.h"
#include "GlassFrame.h"
#include "GpuEffects.h"
#include <vector>

// UI-only, cached screen-space glass. Rays traverse a finite slab using Snell's
// law; the exit-ray displacement samples the already rendered room. Bevels,
// dispersion, studio reflections and a small lens kernel are art directed.
// This does not trace hidden geometry, recursive reflections or caustic paths.
class GlassCaseRenderer {
public:
    using Vec=SphericalWater::Vec;
    static constexpr float thickness=.24f;
    static constexpr float maxPixels=4000000.f;
    float scale=2.f;
    struct Transfer {juce::Point<float> sample;float reflection=0,edge=0;};
    juce::Image& begin(const ListenerSpace& view) {
        // Cap fullscreen post-processing cost, including Retina/4K displays.
        scale=std::min(2.f,std::sqrt(maxPixels/std::max(1.f,view.bounds.getWidth()*view.bounds.getHeight())));
        const int w=std::max(2,juce::roundToInt(view.bounds.getWidth()*scale)),h=std::max(2,juce::roundToInt(view.bounds.getHeight()*scale));
        if(scene.getWidth()!=w||scene.getHeight()!=h){scene=juce::Image(juce::Image::ARGB,w,h,true);refracted=scene.createCopy();finished=scene.createCopy();mapsReady=false;const int sw=std::max(2,juce::roundToInt(view.bounds.getWidth()/3)),sh=std::max(2,juce::roundToInt(view.bounds.getHeight()/3));soft=juce::Image(juce::Image::ARGB,sw,sh,true);scratch=soft.createCopy();}
        if(!mapsReady||view.fromListener!=cached.fromListener||view.bounds!=cached.bounds||view.centre!=cached.centre||std::abs(view.width-cached.width)>.0001f||std::abs(view.depth-cached.depth)>.0001f||std::abs(view.height-cached.height)>.0001f){cached=view;buildMap();}
        return scene;
    }
    juce::AffineTransform transform(const ListenerSpace& v)const{return {scale,0,-v.bounds.getX()*scale,0,scale,-v.bounds.getY()*scale};}
    static Transfer trace(const ListenerSpace& view,juce::Point<float> screen,float ior=1.517f,float plateThickness=thickness) {
        return traceRay(view,screen,view.eye(),view.focal(),ior,plateThickness);
    }
    void drawBackEdges(juce::Graphics& g)const{g.drawImageAt(rearEdges,0,0);}
    void draw(juce::Graphics& g,const ListenerSpace& view,const NoteLens::Lights& lights={},const tide::gpu::Effects& effects={}) {
        const auto origin=view.bounds.getPosition()+juce::Point<float>{.5f/scale,.5f/scale};
        // Pane geometry and refraction stay fixed when a note plays.
        {juce::Graphics surface(scene);surface.drawImageAt(edges,0,0);}
        tide::gpu::Lights gpuLights{};
        for(size_t i=0;i<lights.size();++i){const auto& l=lights[i];const auto centre=(l.centre-origin)*scale;
            gpuLights.geometry[i]={centre.x,centre.y,l.radius>0?std::max(26.f,l.radius*4.3f)*scale:0.f,std::clamp(l.strength,0.f,1.f)};
            gpuLights.tint[i]={l.tint.x*.65f+.35f,l.tint.y*.65f+.35f,l.tint.z*.65f+.35f,0};}
        if(gpu.render(scene,finished,gpuLights,effects)){
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(finished,view.bounds);return;
        }
        if(!cpuMapsReady)buildCpuMap();
        const NoteLens lens(lights,origin,scale);
        {
            juce::Image::BitmapData src(scene,juce::Image::BitmapData::readOnly),dst(refracted,juce::Image::BitmapData::writeOnly);
            const int w=scene.getWidth(),h=scene.getHeight();
            if(view.fromListener){
                // The inside view needs no pane map. Notes only add local glare.
                tide::raster::rows(h,[&](int first,int last){
                    for(int y=first;y<last;++y){auto* row=reinterpret_cast<juce::PixelARGB*>(dst.getLinePointer(y));
                        std::memcpy(row,src.getLinePointer(y),(size_t)w*4);const auto rowLens=lens.inRows((float)y,(float)y);if(rowLens.empty())continue;
                        for(int x=0;x<w;++x){const auto glare=rowLens.at({(float)x,(float)y});
                            if(glare.x<=0)continue;const auto original=row[x];
                            row[x].setARGB(255,byte((float)original.getRed()+glare.x),byte((float)original.getGreen()+glare.y),byte((float)original.getBlue()+glare.z));
                        }
                    }
                });
            }else{
            const auto resting=[&](const Map& m){juce::PixelARGB pixel;pixel.setARGB(255,
                byte(sample(src,m.red,juce::PixelARGB::indexR)*m.transmission+m.light.x),
                byte(sample(src,m.green,juce::PixelARGB::indexG)*m.transmission+m.light.y),
                byte(sample(src,m.blue,juce::PixelARGB::indexB)*m.transmission+m.light.z));return pixel;};
            tide::raster::rows(h,[&](int first,int last){
                for(int y=first;y<last;++y){auto* row=reinterpret_cast<juce::PixelARGB*>(dst.getLinePointer(y));
                    const auto span=sourceRows[(size_t)y];const auto rowLens=lens.inRows(span.x,span.y);
                    if(rowLens.empty()){for(int x=0;x<w;++x)row[x]=resting(map[(size_t)(y*w+x)]);continue;}
                    for(int x=0;x<w;++x){const auto& m=map[(size_t)(y*w+x)];const auto glare=rowLens.at(m.green);const auto original=resting(m);
                        row[x].setARGB(255,byte((float)original.getRed()+glare.x),byte((float)original.getGreen()+glare.y),byte((float)original.getBlue()+glare.z));
                    }
                }
            });
            }
        }
        // Keep the resting image clear. Broad note glare is supplied by the
        // light field above; only a small amount of lens softness stays on.
        {juce::Graphics blurred(soft);blurred.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);blurred.drawImage(refracted,soft.getBounds().toFloat());}
        boxBlur(soft,scratch,true);boxBlur(scratch,soft,false);
        {
            juce::Image::BitmapData src(refracted,juce::Image::BitmapData::readOnly),blurred(soft,juce::Image::BitmapData::readOnly),dst(finished,juce::Image::BitmapData::writeOnly);
            const int w=scene.getWidth(),h=scene.getHeight();const float sx=(float)(soft.getWidth()-1.001f)/(float)w,sy=(float)(soft.getHeight()-1.001f)/(float)h;
            tide::raster::rows(h,[&](int first,int last){
                for(int y=first;y<last;++y){const auto* source=reinterpret_cast<const juce::PixelARGB*>(src.getLinePointer(y));auto* out=reinterpret_cast<juce::PixelARGB*>(dst.getLinePointer(y));
                    for(int x=0;x<w;++x){const float blur=softness[(size_t)(y*w+x)];const auto glow=sampleColour(blurred,{(float)x*sx,(float)y*sy});
                        const auto blend=[&](int sharp,float softValue){return byte((float)sharp+(softValue-(float)sharp)*blur+std::max(0.f,softValue-95.f)*.065f);};
                        out[x].setARGB(255,blend(source[x].getRed(),glow.x),blend(source[x].getGreen(),glow.y),blend(source[x].getBlue(),glow.z));
                    }
                }
            });
        }
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(finished,view.bounds);
    }
    static void caustic(juce::Graphics& g,const ListenerSpace& view,Vec p,float radius,float light) {
        // A restrained projected light pool, rather than a full caustics solver.
        const auto centre=view.project({p.x,p.y,0});const float r=view.radius(radius*1.9f,{p.x,p.y,0});
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe2e0cf).withAlpha(.035f+.045f*light),centre,juce::Colours::transparentBlack,centre+juce::Point<float>{r*1.8f,0},true));
        g.fillEllipse(centre.x-r*1.8f,centre.y-r*.42f,r*3.6f,r*.84f);

    }
private:
    struct Map {juce::Point<float> red,green,blue;Vec light;float transmission=1;};
    tide::gpu::GlassPass gpu;
    juce::Image scene,refracted,finished,soft,scratch,edges,rearEdges;ListenerSpace cached;std::vector<Map> map;std::vector<juce::Point<float>> sourceRows;std::vector<float> softness;bool mapsReady=false,cpuMapsReady=false;
    static juce::uint8 byte(float v){return (juce::uint8)std::clamp(v+.5f,0.f,255.f);}
    static float sample(const juce::Image::BitmapData& src,juce::Point<float> p,int channel){
        const int x=(int)p.x,y=(int)p.y;const float fx=p.x-(float)x,fy=p.y-(float)y;
        const auto* a=src.getPixelPointer(x,y)+channel;const auto* b=a+src.lineStride;
        return ((float)a[0]+((float)a[4]-a[0])*fx)*(1-fy)+((float)b[0]+((float)b[4]-b[0])*fx)*fy;
    }
    static Vec sampleColour(const juce::Image::BitmapData& src,juce::Point<float> p){
        const int x=(int)p.x,y=(int)p.y;const float fx=p.x-(float)x,fy=p.y-(float)y;
        const auto* a=reinterpret_cast<const juce::PixelARGB*>(src.getLinePointer(y))+x;
        const auto* b=reinterpret_cast<const juce::PixelARGB*>(src.getLinePointer(y+1))+x;
        const auto channel=[&](int aa,int ab,int ba,int bb){return ((float)aa+((float)ab-aa)*fx)*(1-fy)+((float)ba+((float)bb-ba)*fx)*fy;};
        return {channel(a[0].getRed(),a[1].getRed(),b[0].getRed(),b[1].getRed()),channel(a[0].getGreen(),a[1].getGreen(),b[0].getGreen(),b[1].getGreen()),channel(a[0].getBlue(),a[1].getBlue(),b[0].getBlue(),b[1].getBlue())};
    }
    static void boxBlur(const juce::Image& source,juce::Image& target,bool horizontal){
        juce::Image::BitmapData src(source,juce::Image::BitmapData::readOnly),dst(target,juce::Image::BitmapData::writeOnly);
        const int w=source.getWidth(),h=source.getHeight();
        tide::raster::rows(h,[&](int first,int last){
            for(int y=first;y<last;++y){auto* out=reinterpret_cast<juce::PixelARGB*>(dst.getLinePointer(y));
                for(int x=0;x<w;++x){int r=0,g=0,b=0;
                    for(int tap=-2;tap<=2;++tap){const int xx=horizontal?std::clamp(x+tap,0,w-1):x,yy=horizontal?y:std::clamp(y+tap,0,h-1);
                        const auto* pixel=reinterpret_cast<const juce::PixelARGB*>(src.getLinePointer(yy))+xx;r+=pixel->getRed();g+=pixel->getGreen();b+=pixel->getBlue();}
                    out[x].setARGB(255,(juce::uint8)(r/5),(juce::uint8)(g/5),(juce::uint8)(b/5));
                }
            }
        });
    }
    static Transfer traceRay(const ListenerSpace& v,juce::Point<float> screen,Vec eye,float focal,float ior,float plateThickness) {
        Transfer out{screen};
        const auto ray=(v.forward()+v.right()*((screen.x-v.centre.x)/focal)+v.up()*((v.centre.y-screen.y)/focal)).unit();
        const std::array<float,3> origin{{eye.x,eye.y,eye.z}},dir{{ray.x,ray.y,ray.z}},lo{{-v.width*.5f,0,0}},hi{{v.width*.5f,v.depth,v.height}};
        float enter=0,leave=1.e6f;int face=-1;float sign=0;
        for(int axis=0;axis<3;++axis){const size_t i=(size_t)axis;
            if(std::abs(dir[i])<1.e-7f){if(origin[i]<lo[i]||origin[i]>hi[i])return out;continue;}
            float a=(lo[i]-origin[i])/dir[i],b=(hi[i]-origin[i])/dir[i];float s=-1;
            if(a>b){std::swap(a,b);s=1;}if(a>enter){enter=a;face=axis;sign=s;}leave=std::min(leave,b);
            if(enter>=leave)return out;
        }
        if(face<0)return out;
        const auto p=eye+ray*enter;const std::array<float,3> hit{{p.x,p.y,p.z}};
        std::array<float,3> normal{};normal[(size_t)face]=sign;float edge=0;
        for(int axis=0;axis<3;++axis)if(axis!=face){const size_t i=(size_t)axis;const float a=hit[i]-lo[i],b=hi[i]-hit[i],distance=std::min(a,b);
            const float bevel=std::clamp(1-distance/.30f,0.f,1.f);edge=std::max(edge,bevel);normal[i]=(a<b?-1.f:1.f)*bevel*.48f;}
        Vec n=Vec{normal[0],normal[1],normal[2]}.unit();
        if(-ray.dot(n)<.08f){normal={};normal[(size_t)face]=sign;n={normal[0],normal[1],normal[2]};}
        const float cosine=std::max(.08f,-ray.dot(n));Vec internal;
        if(!tide::optics::refract(ray,n,1/ior,internal))return out;
        // At the parallel second interface the exiting ray is parallel to the
        // incoming ray. The finite slab leaves a lateral displacement.
        const auto shift=internal*(plateThickness/std::max(.08f,-internal.dot(n)))-ray*(plateThickness/cosine);
        const auto probe=p+ray*std::max(1.f,(leave-enter)*.5f)+shift,relative=probe-eye;
        const float projection=focal/std::max(.05f,relative.dot(v.forward()));
        out.sample={v.centre.x+relative.dot(v.right())*projection,v.centre.y-relative.dot(v.up())*projection};
        out.reflection=tide::optics::fresnel(cosine,1,ior);out.edge=edge;return out;
    }
    void buildMap(){
        cpuMapsReady=false;
        edges=juce::Image(juce::Image::ARGB,scene.getWidth(),scene.getHeight(),true);
        {juce::Graphics edgeGraphics(edges);edgeGraphics.addTransform(transform(cached));GlassFrame::draw(edgeGraphics,cached,true);}
        rearEdges=juce::Image(juce::Image::ARGB,scene.getWidth(),scene.getHeight(),true);
        {juce::Graphics edgeGraphics(rearEdges);edgeGraphics.addTransform(transform(cached));GlassFrame::draw(edgeGraphics,cached,false);}
        const auto vector=[](Vec v){return tide::gpu::Float4{v.x,v.y,v.z,0};};
        tide::gpu::GlassParameters p{};
        p.sizeScale={(float)scene.getWidth(),(float)scene.getHeight(),scale,cached.fromListener?1.f:0.f};
        p.bounds={cached.bounds.getX(),cached.bounds.getY(),cached.bounds.getWidth(),cached.bounds.getHeight()};
        p.centreFocal={cached.centre.x,cached.centre.y,cached.focal(),0};p.eye=vector(cached.eye());
        p.forward=vector(cached.forward());p.right=vector(cached.right());p.up=vector(cached.up());p.room={cached.width,cached.depth,cached.height,0};
        if(gpu.prepare(p)){map.clear();map.shrink_to_fit();softness.clear();sourceRows.clear();mapsReady=true;return;}
        buildCpuMap();
    }
    void buildCpuMap(){
        cpuMapsReady=true;
        const auto eye=cached.eye();const float focal=cached.focal();const int w=scene.getWidth(),h=scene.getHeight();map.resize(cached.fromListener?0:(size_t)(w*h));sourceRows.resize((size_t)h);softness.resize((size_t)(w*h));
        if(cached.fromListener){
            tide::raster::rows(h,[&](int first,int last){for(int y=first;y<last;++y)for(int x=0;x<w;++x){
                const float nx=(((float)x+.5f)/scale-cached.bounds.getWidth()*.5f)/(cached.bounds.getWidth()*.5f);
                const float ny=(((float)y+.5f)/scale-cached.bounds.getHeight()*.5f)/(cached.bounds.getHeight()*.5f);
                softness[(size_t)(y*w+x)]=std::clamp(.07f+(nx*nx+ny*ny)*.16f,.07f,.28f);
            }});mapsReady=true;return;
        }
        tide::raster::rows(h,[&](int first,int last){
        for(int y=first;y<last;++y){auto& span=sourceRows[(size_t)y];span={1.e9f,-1.e9f};
        for(int x=0;x<w;++x){const juce::Point<float> screen{cached.bounds.getX()+((float)x+.5f)/scale,cached.bounds.getY()+((float)y+.5f)/scale};
            const auto green=cached.fromListener?Transfer{screen}:traceRay(cached,screen,eye,focal,1.517f,thickness);
            const auto red=cached.fromListener?Transfer{screen}:traceRay(cached,screen,eye,focal,1.509f,thickness),blue=cached.fromListener?Transfer{screen}:traceRay(cached,screen,eye,focal,1.526f,thickness);
            const float nx=(screen.x-cached.centre.x)/(cached.bounds.getWidth()*.5f),ny=(screen.y-cached.centre.y)/(cached.bounds.getHeight()*.5f),radial=nx*nx+ny*ny;
            const auto coordinate=[&](juce::Point<float> point){point=(point-cached.bounds.getPosition())*scale-juce::Point<float>{.5f,.5f};return juce::Point<float>{std::clamp(point.x,0.f,(float)w-1.001f),std::clamp(point.y,0.f,(float)h-1.001f)};};
            auto& m=map[(size_t)(y*w+x)];m.red=coordinate(red.sample);m.green=coordinate(green.sample);m.blue=coordinate(blue.sample);
            span.x=std::min(span.x,m.green.y);span.y=std::max(span.y,m.green.y);
            const float sheen=std::exp(-std::pow((nx+ny*.28f+.27f)*4.5f,2.f))*.55f+std::exp(-std::pow((nx-ny*.2f-.65f)*10,2.f))*.35f;
            const float reflected=green.reflection*(.30f+sheen*1.75f);m.light=Vec{30,31,29}*reflected+Vec{13,17,16}*(green.edge*green.edge);
            m.transmission=1-green.reflection*.34f;softness[(size_t)(y*w+x)]=std::clamp(.07f+radial*.16f+green.edge*.04f,.07f,.28f);
        }}
        });mapsReady=true;
    }
};
