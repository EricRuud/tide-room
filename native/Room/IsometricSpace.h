#pragma once
#include "SphericalWater.h"
#include <juce_graphics/juce_graphics.h>

// Orthographic isometric basis. Projection and dragging share the same inverse.
struct IsometricSpace {
    using Vec=SphericalWater::Vec;
    static constexpr float a=.7071067812f,b=.4082482905f,c=.8164965809f;
    juce::Point<float> origin{596,495};
    float scale=39,width=10,depth=8,height=4;
    static Vec camera(Vec p){return {a*(p.x+p.y),b*(-p.x+p.y)+c*p.z,.5773502692f*(p.x-p.y+p.z)};}
    static Vec world(Vec p){return {a*p.x-b*p.y+.5773502692f*p.z,a*p.x+b*p.y-.5773502692f*p.z,c*p.y+.5773502692f*p.z};}
    juce::Point<float> project(Vec p)const {const auto v=camera(p);return {origin.x+v.x*scale,origin.y-v.y*scale};}
    Vec position(float lateral,float distance,float elevation)const{return {lateral*width*.5f,(distance-.5f)*depth,elevation};}
    juce::Point<float> unproject(juce::Point<float> p,float elevation)const {
        const float x=(p.x-origin.x)/scale,y=(origin.y-p.y)/scale-c*elevation;
        const float wx=(x/a-y/b)*.5f,wy=(x/a+y/b)*.5f;
        return {wx*2/width,wy/depth+.5f};
    }
    float cameraDepth(Vec p)const{return camera(p).z;}
    void back(juce::Graphics& g)const {
        const auto q=corners();juce::Path base;base.startNewSubPath(q[0]);for(int i:{1,2,3})base.lineTo(q[(size_t)i]);base.closeSubPath();
        g.setColour(juce::Colour(0xff111b23));g.fillPath(base);
        g.setColour(juce::Colour(0xff748f9b).withAlpha(.08f));
        for(float x=-width*.5f+1;x<width*.5f;x+=1)g.drawLine({project({x,-depth*.5f,0}),project({x,depth*.5f,0})},.65f);
        for(float y=-depth*.5f+1;y<depth*.5f;y+=1)g.drawLine({project({-width*.5f,y,0}),project({width*.5f,y,0})},.65f);
        for(int i:{2,3}){const int j=(i+1)%4;juce::Path wall;wall.startNewSubPath(q[(size_t)i]);wall.lineTo(q[(size_t)j]);wall.lineTo(q[(size_t)j+4]);wall.lineTo(q[(size_t)i+4]);wall.closeSubPath();
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xffafc9d7).withAlpha(.022f),q[(size_t)i],juce::Colour(0xff7c9dab).withAlpha(.055f),q[(size_t)i+4],false));g.fillPath(wall);}
        g.setColour(juce::Colour(0xffa9c3ce).withAlpha(.17f));
        for(int i=0;i<4;++i){const int j=(i+1)%4;g.drawLine({q[(size_t)i],q[(size_t)j]},.8f);g.drawLine({q[(size_t)i+4],q[(size_t)j+4]},.8f);g.drawLine({q[(size_t)i],q[(size_t)i+4]},.8f);}
    }
    void front(juce::Graphics& g)const {
        const auto q=corners();
        // The two near panes have a barely visible coating and polished edges.
        for(int i:{0,1}){const int j=(i+1)%4;juce::Path wall;wall.startNewSubPath(q[(size_t)i]);wall.lineTo(q[(size_t)j]);wall.lineTo(q[(size_t)j+4]);wall.lineTo(q[(size_t)i+4]);wall.closeSubPath();
            g.setColour(juce::Colour(0xffc1d7e5).withAlpha(.015f));g.fillPath(wall);}
        g.setColour(juce::Colour(0xffcee5ed).withAlpha(.37f));
        for(int i:{0,1,2})g.drawLine({q[(size_t)i],q[(size_t)i+4]},.8f);
        for(int i:{0,1}){const int j=(i+1)%4;g.drawLine({q[(size_t)i+4],q[(size_t)j+4]},1.1f);g.drawLine({q[(size_t)i],q[(size_t)j]},.85f);}
        // Reflections from long overhead softboxes on the top rim.
        const auto start=q[4],end=q[5];g.setColour(juce::Colour(0xffe8f5f8).withAlpha(.38f));g.drawLine({start+(end-start)*.12f,start+(end-start)*.55f},1.25f);
    }
    std::array<juce::Point<float>,8> corners()const {
        std::array<juce::Point<float>,8> q;const float x=width*.5f,y=depth*.5f;
        const std::array<Vec,4> p{{{-x,-y,0},{x,-y,0},{x,y,0},{-x,y,0}}};
        for(size_t i=0;i<4;++i){q[i]=project(p[i]);q[i+4]=project(p[i]+Vec{0,0,height});}return q;
    }
};
