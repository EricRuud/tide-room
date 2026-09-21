#pragma once
#include "SphericalWater.h"
#include "RoomSurface.h"
#include "RoomGeometry.h"
#include <juce_graphics/juce_graphics.h>

// A 34-degree overview of the case, or a first-person acoustic listener view.
// Camera changes are visual only; sources retain their acoustic coordinates.
struct ListenerSpace {
    using Vec=SphericalWater::Vec;
    juce::Rectangle<float> bounds{238,232,724,394};
    juce::Point<float> centre{600,429};
    float width=10,depth=8,height=4;
    bool fromListener=false;
    // The overview reveals floor motion; immersive uses the acoustic listener.
    Vec right()const{return fromListener?Vec{1,0,0}:Vec{0.961261696f,0.275637356f,0};}
    Vec up()const{return fromListener?Vec{0,0,1}:Vec{-0.154134453f,0.537530719f,0.829037573f};}
    Vec forward()const{return fromListener?Vec{0,1,0}:Vec{-0.228513724f,0.796922063f,-0.559192903f};}
    Vec listener()const{return {0,0,tide::room::listenerHeight(height)};}
    Vec target()const{return fromListener?listener()+forward():Vec{0,depth*.40f,height*.43f};}
    Vec eye()const{return fromListener?listener():target()-forward()*(std::max({width,depth,height})*1.2f);}
    float nearPlane()const{return fromListener?.12f:0.f;}
    float cameraDepth(Vec p)const{return (p-eye()).dot(forward());}
    float focal()const {
        if(fromListener)return std::min(bounds.getWidth()*.23f,bounds.getHeight()*.46f);
        float horizontal=.001f,vertical=.001f;
        for(float x:{-width*.5f,width*.5f})for(float y:{0.f,depth})for(float z:{0.f,height}){
            const auto v=Vec{x,y,z}-eye();const float d=std::max(.05f,v.dot(forward()));
            horizontal=std::max(horizontal,std::abs(v.dot(right())/d));vertical=std::max(vertical,std::abs(v.dot(up())/d));}
        return std::min((bounds.getWidth()*.5f-18)/horizontal,(bounds.getHeight()*.5f-18)/vertical);
    }
    Vec position(float lateral,float distance,float elevation)const{return {lateral*width*.5f,distance*depth,elevation};}
    juce::Point<float> project(Vec p)const {const auto v=p-eye();const float scale=focal()/std::max(.05f,v.dot(forward()));return {centre.x+scale*v.dot(right()),centre.y-scale*v.dot(up())};}
    float radius(float metres,Vec p)const{return focal()*metres/std::max(.05f,cameraDepth(p));}
    Vec unproject(juce::Point<float> p,float distance)const {
        const auto ray=forward()+right()*((p.x-centre.x)/focal())+up()*((centre.y-p.y)/focal());
        const auto origin=eye();return origin+ray*((distance-origin.y)/ray.y);
    }
    Vec unprojectAtHeight(juce::Point<float> p,float elevation)const {
        const auto ray=forward()+right()*((p.x-centre.x)/focal())+up()*((centre.y-p.y)/focal());
        const auto origin=eye();if(std::abs(ray.z)<1.e-6f)return {0,depth*.4f,elevation};
        return origin+ray*((elevation-origin.z)/ray.z);
    }
    void basis(Vec p,Vec& r,Vec& u,Vec& back)const{back=(eye()-p).unit();r=up().cross(back).unit();u=back.cross(r).unit();}
    Vec cameraVector(Vec v,Vec position)const{Vec r,u,back;basis(position,r,u,back);return {v.dot(r),v.dot(u),v.dot(back)};}
    juce::Path floorPath()const {
        juce::Path path;const float x=width*.5f;path.startNewSubPath(project({-x,nearPlane(),0}));
        for(const auto p:std::array<Vec,3>{{{x,nearPlane(),0},{x,depth,0},{-x,depth,0}}})path.lineTo(project(p));path.closeSubPath();return path;
    }
    void draw(juce::Graphics& g)const {
        const float x=width*.5f,near=nearPlane();
        const auto quad=[&](Vec a,Vec b,Vec c,Vec d){juce::Path p;p.startNewSubPath(project(a));for(auto v:{b,c,d})p.lineTo(project(v));p.closeSubPath();return p;};
        // A shallow satin base gives the enclosure weight without a wire grid.
        if(!fromListener){
            auto base=quad({-x,0,0},{x,0,0},{x,0,-.13f},{-x,0,-.13f});
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff191e20),project({0,0,0}),juce::Colour(0xff080b0e),project({0,0,-.13f}),false));g.fillPath(base);
        }
        // Sample the same material used by refracted water rays. A small cached
        // raster avoids banding and gives perspective-correct soft illumination.
        const int w=std::max(2,juce::roundToInt(bounds.getWidth())),h=std::max(2,juce::roundToInt(bounds.getHeight()));
        juce::Image floorImage(juce::Image::ARGB,w,h,true);const auto eyePosition=eye();const float focalLength=focal();
        {juce::Image::BitmapData pixels(floorImage,juce::Image::BitmapData::writeOnly);
            for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){const float sx=bounds.getX()+(float)xx+.5f,sy=bounds.getY()+(float)yy+.5f;
                const auto ray=forward()+right()*((sx-centre.x)/focalLength)+up()*((centre.y-sy)/focalLength);
                if(ray.z>=-.00001f)continue;const float t=-eyePosition.z/ray.z;if(t<=0)continue;
                const auto hit=eyePosition+ray*t;if(std::abs(hit.x)>x||hit.y<near||hit.y>depth)continue;
                const auto colour=tide::surface::floor(hit.x,hit.y,width,depth);pixels.setPixelColour(xx,yy,juce::Colour::fromFloatRGBA(colour.x,colour.y,colour.z,1));
            }
        }
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(floorImage,bounds);
        // Large, barely visible reflections on the clear panes are closer to a
        // photographed vitrine than a luminous frame. The centre stays open.
        const auto back=quad({-x,depth,0},{x,depth,0},{x,depth,height},{-x,depth,height});
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffdfded4).withAlpha(.033f),project({-x,depth,height*.75f}),juce::Colours::transparentWhite,project({x*.5f,depth,height*.2f}),false));g.fillPath(back);
        const auto side=quad({-x,near,0},{-x,depth,0},{-x,depth,height},{-x,near,height});
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffc5d0cc).withAlpha(.028f),project({-x,depth*.48f,height*.8f}),juce::Colours::transparentWhite,project({-x,near,0}),false));g.fillPath(side);
        // A soft photographic reflection across one panel, rather than stripes
        // repeated on every edge. Real 3D corners still supply depth and parallax.
        const auto card=quad({-x,depth*.18f,height*.08f},{-x,depth*.34f,height*.08f},{-x,depth*.55f,height*.90f},{-x,depth*.40f,height*.90f});
        g.setGradientFill(juce::ColourGradient(juce::Colours::transparentWhite,project({-x,depth*.25f,0}),juce::Colour(0xffecebe1).withAlpha(.048f),project({-x,depth*.48f,height}),false));g.fillPath(card);
    }
};
