#pragma once
#include "RoomProcessor.h"
#include "PlanetRenderer.h"
#include "TowerRenderer.h"
#include "GlassCaseRenderer.h"
#include "GlassLook.h"
#include "VisualMapping.h"

// Independent visual caches for each camera; both read the same audio scene.
class RoomSceneRenderer {
public:
    TowerRenderer towers;
    juce::Point<float> hitPosition(const ListenerSpace& view,juce::Point<float> point)const{
        if(tide::gpu::available())point=tide::visual::sourcePoint(point,view.bounds,lastEffects);
        return GlassCaseRenderer::trace(view,point).sample;
    }
    void draw(juce::Graphics& g,RoomProcessor& scene,const ListenerSpace& view,int dragged=-1,juce::Point<float> mouse={-1000,-1000}) {
        juce::Graphics::ScopedSaveState saved(g);g.reduceClipRegion(view.bounds.toNearestInt());
        auto& canvas=glassCase.begin(view);
        if(backdrop.isNull()||backdrop.getBounds()!=canvas.getBounds()||view.fromListener!=backdropView.fromListener||view.bounds!=backdropView.bounds||view.centre!=backdropView.centre
           ||std::abs(view.width-backdropView.width)>.0001f||std::abs(view.depth-backdropView.depth)>.0001f||std::abs(view.height-backdropView.height)>.0001f){
            backdrop=canvas.createCopy();backdropView=view;
            {juce::Graphics background(backdrop);background.addTransform(glassCase.transform(view));
            background.fillAll(tide::glass::background);background.setGradientFill(juce::ColourGradient(juce::Colour(0xff222522).withAlpha(.35f),view.centre,juce::Colour(0xff0b1016).withAlpha(0.f),view.bounds.getBottomRight(),true));background.fillAll();view.draw(background);}
            juce::Graphics frame(backdrop);glassCase.drawBackEdges(frame);
        }
        // Keep the high-resolution case and floor cached; only moving light,
        // towers and water need rebuilding on each frame.
        {juce::Image::BitmapData src(backdrop,juce::Image::BitmapData::readOnly),dst(canvas,juce::Image::BitmapData::writeOnly);
            tide::raster::rows(canvas.getHeight(),[&](int first,int last){for(int y=first;y<last;++y)std::memcpy(dst.getLinePointer(y),src.getLinePointer(y),(size_t)canvas.getWidth()*4);});}

        const double now=juce::Time::getMillisecondCounterHiRes()*.001;
        const bool preview=previewFrameSeconds>0;const double frameTime=preview?previewFrameSeconds:now-previousSceneTime;previewFrameSeconds=0;
        std::array<SphericalWater::Vec,3> physical;std::array<juce::Point<float>,3> centres;std::array<float,3> radii;std::array<int,3> order{{0,1,2}};
        {
            juce::Graphics surface(canvas);surface.addTransform(glassCase.transform(view));

            for(int i=0;i<3;++i){const auto moving=scene.movingPosition(i);physical[(size_t)i]=view.position(moving.lateral,moving.depth,moving.height);centres[(size_t)i]=view.project(physical[(size_t)i]);
                radii[(size_t)i]=view.radius(tide::room::PlanetMotion::waterRadius,physical[(size_t)i]);
                const float light=scene.get(RoomProcessor::partId(i,"mute"))>.5f?0.f:std::clamp(scene.noteLights[(size_t)i].load(),0.f,1.f);
                lensLights[(size_t)i]={centres[(size_t)i],radii[(size_t)i],0,PlanetRenderer::noteTint(i)};
                auto base=physical[(size_t)i];base.z=0;const auto shadow=view.project(base);const float r=radii[(size_t)i];
                surface.setGradientFill(juce::ColourGradient(juce::Colour(0xff010509).withAlpha(.30f),shadow,juce::Colours::transparentBlack,shadow+juce::Point<float>{r*1.4f,0},true));
                surface.fillEllipse(shadow.x-r*1.4f,shadow.y-r*.22f,r*2.8f,r*.44f);
                towers.drawFloorReflection(surface,view,physical[(size_t)i],i);
                GlassCaseRenderer::caustic(surface,view,physical[(size_t)i],tide::room::PlanetMotion::waterRadius,light);
            }
            for(size_t pair=0;pair<3;++pair){
                const float strength=scene.waterConnections[pair].load();if(strength<.025f)continue;
                const auto ids=tide::room::PlanetMotion::pairs[pair];auto a=physical[(size_t)ids[0]],b=physical[(size_t)ids[1]];const auto delta=b-a;
                if(delta.length()>tide::room::PlanetMotion::waterRadius*2.9f)continue;
                const auto direction=delta.unit();a=a+direction*.60f;b=b-direction*.60f;
                a.z=scene.towerNotes(ids[0]).worldElevation(0,view.height);b.z=scene.towerNotes(ids[1]).worldElevation(0,view.height);
                auto from=view.project(a),to=view.project(b),middle=(from+to)*.5f;middle.y+=2*strength;
                juce::Path neck;neck.startNewSubPath(from);neck.quadraticTo(middle,to);
                surface.setColour(juce::Colour(0xffb8d7d9).withAlpha(strength*.28f));surface.strokePath(neck,juce::PathStrokeType(2+strength*3,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
                surface.setColour(juce::Colour(0xffeff3e9).withAlpha(strength*.45f));surface.strokePath(neck,juce::PathStrokeType(.7f));
            }
            std::sort(order.begin(),order.end(),[&](int a,int b){return view.cameraDepth(physical[(size_t)a])>view.cameraDepth(physical[(size_t)b]);});
            bool listenerDrawn=view.fromListener;
            for(int i:order){
                if(!listenerDrawn&&view.cameraDepth(physical[(size_t)i])<view.cameraDepth(view.listener())){drawListener(surface,view);listenerDrawn=true;}
                const auto centre=centres[(size_t)i];const float r=radii[(size_t)i];
                towers.draw(surface,scene,i,physical[(size_t)i],view,centre,r,preview||previousSceneTime<0?0:std::clamp(frameTime,0.,.15));
                if(!view.fromListener&&(dragged==i||TowerRenderer::bounds(scene,i,physical[(size_t)i],view).contains(hitPosition(view,mouse)))){surface.setColour(tide::glass::text.withAlpha(.16f));surface.drawRoundedRectangle(TowerRenderer::bounds(scene,i,physical[(size_t)i],view).expanded(3),6,.6f);}
            }
            if(!listenerDrawn)drawListener(surface,view);
        }
        const float dt=(float)std::clamp(frameTime,0.,.1);
        visualTime+=dt*scene.get("visualSpeed");
        float activity=0;
        for(size_t i=0;i<3;++i)if(scene.get(RoomProcessor::partId((int)i,"mute"))<.5f)
            activity=std::max(activity,std::max(scene.noteLights[i].load(),std::clamp(scene.meters[i].load()*3.f,0.f,1.f)));
        visualEnvelope+=(activity-visualEnvelope)*(1-std::exp(-dt/(activity>visualEnvelope?.045f:.24f)));
        tide::gpu::Effects effects;
        // The app now uses only the retained audio-reactive prism. Legacy effect
        // parameters remain loadable, but cannot bring back removed layers.
        effects.layers={0,0,0,scene.get("visualPrism")*.5f};
        effects.controls={0,scene.get("visualEnabled")>.5f?scene.get("visualMix"):0.f,1,6};
        effects.animation={(float)visualTime,0,scene.get("visualReact"),visualEnvelope};
        previousSceneTime=now;glassCase.draw(g,view,lensLights,effects);lastEffects=effects;
        if(!view.fromListener){
            g.setFont(juce::FontOptions(10.f));g.setColour(tide::glass::text.withAlpha(.65f));
            for(int i:order){const auto centre=centres[(size_t)i];g.drawText("0"+juce::String(i+1),(int)centre.x-8,(int)(centre.y+(TowerRenderer::bounds(scene,i,physical[(size_t)i],view).getBottom()-centre.y+3)),25,17,juce::Justification::centredLeft);}
            const auto listener=view.project(view.listener());
            g.setColour(tide::glass::text.withAlpha(.8f));g.drawText("LISTENER",(int)listener.x-40,(int)listener.y+15,80,17,juce::Justification::centred);
        }
    }
    void advancePreview(RoomProcessor& scene,const ListenerSpace&,double seconds){
        previewFrameSeconds=seconds;
        for(int i=0;i<3;++i)towers.advancePreview(scene,i,seconds);
    }
private:
    GlassCaseRenderer glassCase;
    NoteLens::Lights lensLights{};
    juce::Image backdrop;
    ListenerSpace backdropView;

    double previousSceneTime=-1,previewFrameSeconds=0;
    double visualTime=0;
    float visualEnvelope=0;
    tide::gpu::Effects lastEffects{};
    static void drawListener(juce::Graphics& g,const ListenerSpace& view){
        const auto centre=view.project(view.listener()),floor=view.project({0,0,0});
        const auto aim=view.project({0,.65f,view.listener().z});
        g.setColour(juce::Colour(0xffd9dbd1).withAlpha(.06f));g.fillEllipse(floor.x-8,floor.y-2,16,4);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe5e5db).withAlpha(.06f),centre,juce::Colours::transparentBlack,centre+juce::Point<float>{21,0},true));
        g.fillEllipse(centre.x-21,centre.y-21,42,42);
        g.setColour(juce::Colour(0xffd6d9d2).withAlpha(.45f));g.drawLine({centre,aim},1.f);
        const auto direction=(aim-centre)/std::max(1.f,aim.getDistanceFrom(centre)),side=juce::Point<float>{-direction.y,direction.x};
        juce::Path pointer;pointer.startNewSubPath(aim);pointer.lineTo(aim-direction*5+side*2.5f);pointer.lineTo(aim-direction*5-side*2.5f);pointer.closeSubPath();g.fillPath(pointer);
        g.setColour(juce::Colour(0xff15242d));g.fillEllipse(centre.x-8,centre.y-8,16,16);
        g.setColour(juce::Colour(0xffe2e3da));g.drawEllipse(centre.x-6,centre.y-6,12,12,1.f);
        g.fillRoundedRectangle(centre.x-10,centre.y-3,3,7,1.3f);g.fillRoundedRectangle(centre.x+7,centre.y-3,3,7,1.3f);
    }
};
