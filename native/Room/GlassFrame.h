#pragma once
#include "ListenerSpace.h"
#include <vector>

// Twelve solid, chamfered glass rails. Every bevel is a world-space quad;
// perspective and facet lighting are evaluated from the current camera.
class GlassFrame {
public:
    using Vec=SphericalWater::Vec;
    static constexpr float halfWidth=.10f;
    struct Facet {std::vector<Vec> vertices;Vec normal;float depth=0;bool foreground=false;};
    static std::vector<Facet> mesh(const ListenerSpace& view) {
        const float x=view.width*.5f,y=view.fromListener?view.nearPlane()+halfWidth:0;
        const std::array<Vec,8> corners{{{-x,y,0},{x,y,0},{x,view.depth,0},{-x,view.depth,0},{-x,y,view.height},{x,y,view.height},{x,view.depth,view.height},{-x,view.depth,view.height}}};
        const std::array<std::array<int,2>,12> edges{{{{0,1}},{{1,2}},{{2,3}},{{3,0}},{{4,5}},{{5,6}},{{6,7}},{{7,4}},{{0,4}},{{1,5}},{{2,6}},{{3,7}}}};
        constexpr float cut=.64f;
        const std::array<juce::Point<float>,8> cross{{{-cut,-1},{cut,-1},{1,-cut},{1,cut},{cut,1},{-cut,1},{-1,cut},{-1,-cut}}};
        std::vector<Facet> faces;faces.reserve(120);
        for(const auto edge:edges){
            if(view.fromListener&&(edge[0]%4)<2&&(edge[1]%4)<2)continue;
            const auto a=corners[(size_t)edge[0]],b=corners[(size_t)edge[1]],axis=(b-a).unit();
            const auto eye=view.eye();
            // Rails bordering an entering pane sit in front of the contents;
            // exit-side rails sit behind them. Inside the case all rails recede.
            const bool front=!view.fromListener&&(
                (std::abs(a.x-b.x)<.001f&&((a.x<0&&eye.x<-x)||(a.x>0&&eye.x>x)))||
                (std::abs(a.y-b.y)<.001f&&((a.y<.001f&&eye.y<0)||(a.y>.001f&&eye.y>view.depth)))||
                (std::abs(a.z-b.z)<.001f&&((a.z<.001f&&eye.z<0)||(a.z>.001f&&eye.z>view.height))));
            const auto u=axis.cross(std::abs(axis.z)<.5f?Vec{0,0,1}:Vec{0,1,0}).unit(),v=axis.cross(u);
            std::array<Vec,16> points;
            for(size_t i=0;i<8;++i){const auto offset=(u*cross[i].x+v*cross[i].y)*halfWidth;points[i]=a+offset;points[i+8]=b+offset;}
            const auto add=[&](std::vector<Vec> vertices,Vec normal){float depth=0;for(auto p:vertices)depth+=view.cameraDepth(p);depth/=(float)vertices.size();faces.push_back({std::move(vertices),normal,depth,front});};
            for(size_t i=0;i<8;++i){const auto j=(i+1)%8;const auto n=(u*(cross[i].x+cross[j].x)+v*(cross[i].y+cross[j].y)).unit();add({points[i],points[j],points[j+8],points[i+8]},n);}
            add(std::vector<Vec>(points.begin(),points.begin()+8),axis*-1);add(std::vector<Vec>(points.begin()+8,points.end()),axis);
        }
        std::sort(faces.begin(),faces.end(),[](const Facet& a,const Facet& b){return a.depth>b.depth;});return faces;
    }
    static void draw(juce::Graphics& g,const ListenerSpace& view,bool foreground) {
        const auto key=Vec{-.42f,-.35f,.84f}.unit(),fill=Vec{.82f,.2f,.52f}.unit();
        for(const auto& face:mesh(view)){
            if(face.foreground!=foreground)continue;
            Vec centre;for(const auto p:face.vertices)centre=centre+p;centre=centre*(1.f/(float)face.vertices.size());
            const auto eye=(view.eye()-centre).unit();
            const float cosine=std::abs(face.normal.dot(eye));
            const float fresnel=.04f+.96f*std::pow(1-cosine,5.f);
            // Broad softboxes and dark flags define the thickness. There are
            // no stroked facet boundaries or repeated spectral edge bands.
            const float softbox=std::pow(std::abs(face.normal.dot((key+eye).unit())),20.f);
            const float rim=std::pow(std::abs(face.normal.dot((fill+eye).unit())),36.f);
            const float faceLight=.008f+.040f*fresnel+.16f*softbox+.07f*rim;
            juce::Path path;const auto first=view.project(face.vertices[0]);path.startNewSubPath(first);
            for(size_t i=1;i<face.vertices.size();++i)path.lineTo(view.project(face.vertices[i]));path.closeSubPath();
            auto end=view.project(face.vertices[face.vertices.size()>4?4:3]);if(end.getDistanceFrom(first)<1)end=first+juce::Point<float>{4,4};
            juce::ColourGradient reflection(juce::Colour(0xffc8d5d2).withAlpha(faceLight*.35f),first,juce::Colour(0xffb9c8c8).withAlpha(faceLight*.18f),end,false);
            reflection.addColour(.18,juce::Colour(0xffeff0e6).withAlpha(faceLight*.85f));
            reflection.addColour(.43,juce::Colour(0xfff8f3e7).withAlpha(std::min(.72f,faceLight*2.1f)));
            reflection.addColour(.60,juce::Colour(0xff8ea4a0).withAlpha(faceLight*.3f));
            reflection.addColour(.82,juce::Colour(0xffe8eeed).withAlpha(faceLight*.7f));
            g.setGradientFill(reflection);g.fillPath(path);

        }
        // Discrete softbox catches along polished edges provide a few sharp,
        // bright accents. Their positions are world-space and camera-correct.
        const float x=view.width*.5f,z=view.height;
        struct Catch {Vec a,b;float alpha;bool front;};
        const bool above=!view.fromListener&&view.eye().z>z;
        const bool inFront=!view.fromListener&&view.eye().y<0;
        const std::array<Catch,4> catches{{
            {{-x*.93f,0,z+halfWidth},{x*.40f,0,z+halfWidth},.85f,above||inFront},
            {{-x*.35f,view.depth,z+halfWidth},{x*.94f,view.depth,z+halfWidth},.92f,above},
            {{-x,view.depth,z*.22f},{-x,view.depth,z*.95f},.27f,false},
            {{-x*.84f,0,-halfWidth},{x*.38f,0,-halfWidth},.30f,inFront}
        }};
        for(const auto& c:catches){if(c.front!=foreground||view.cameraDepth(c.a)<.12f||view.cameraDepth(c.b)<.12f)continue;
            const auto a=view.project(c.a),b=view.project(c.b);
            juce::ColourGradient highlight(juce::Colours::transparentWhite,a,juce::Colours::transparentWhite,b,false);
            highlight.addColour(.32,juce::Colour(0xfff3f1e8).withAlpha(c.alpha));
            highlight.addColour(.54,juce::Colour(0xffeef1eb).withAlpha(c.alpha*.48f));
            g.setGradientFill(highlight);g.drawLine({a,b},.9f);
        }
    }
};
