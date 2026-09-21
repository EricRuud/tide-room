#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// A closed, irregular height field with inertial flow between adjacent cells.
// This is a visual shallow-water approximation, not the audio CV generator.
// Each edge transfers one volume in opposite directions; donor limiting keeps
// the water nonnegative without creating water when a cell empties.
class SurfaceWater {
public:
    static constexpr int side=41,cells=side*side;
    static constexpr float spacing=2.f/(side-1),stepSeconds=1.f/120;
    struct Sample {float bed=0,depth=0,vx=0,vy=0,coverage=0;};
    void prepare(int identity) {
        depth={};fluxX={};fluxY={};velocityX={};velocityY={};accumulator=0;
        const float phase=(float)identity*1.7f;
        for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
            const int at=y*side+x;const float px=x*spacing-1,py=y*spacing-1;
            const float angle=std::atan2(py,px),r=std::hypot(px,py);
            const float boundary=.92f+.035f*std::sin(angle*3+phase)+.025f*std::cos(angle*5-phase);
            mask[(size_t)at]=r<boundary;signedEdge[(size_t)at]=boundary-r;
            const float ridge=.10f*std::sin(px*5+phase+.7f*std::sin(py*4))+.09f*std::cos(py*5.5f-phase+px*2);
            const float islands=.055f*std::sin(px*9-py*5+phase)+.035f*std::cos(py*12+px*6);
            const float bank=.22f*std::pow(std::min(1.f,r/boundary),12.f);
            bed[(size_t)at]=.46f+ridge+islands+bank;
            if(mask[(size_t)at])depth[(size_t)at]=std::max(0.f,.505f-bed[(size_t)at]);
        }
    }
    int advance(double seconds,float tideX,float tideY) {
        accumulator+=std::clamp(seconds,0.,.25);int count=0;
        while(accumulator+1.e-10>=stepSeconds){step(tideX,tideY);accumulator-=stepSeconds;++count;}
        return count;
    }
    void step(float tideX,float tideY) {
        std::array<float,cells> outgoing{},limit{};
        constexpr float dt=stepSeconds,area=spacing*spacing;
        // The driving potential tilts the effective water level, not the rock.
        const float forceX=.18f*std::clamp(tideX,-1.f,1.f),forceY=.18f*std::clamp(tideY,-1.f,1.f);
        const auto update=[&](int a,int b,float& flow,float force) {
            if(!mask[(size_t)a]||!mask[(size_t)b]){flow=0;return;}
            const float ea=bed[(size_t)a]+depth[(size_t)a],eb=bed[(size_t)b]+depth[(size_t)b];
            const float crest=std::max(bed[(size_t)a],bed[(size_t)b]);
            const float da=std::max(0.f,ea-crest),db=std::max(0.f,eb-crest);
            const float wet=std::max(da,db);
            if(wet<1.e-7f){flow=0;return;}
            const float head=da-db+force*spacing;
            flow=.987f*flow+dt*5.f*wet*head;
            outgoing[(size_t)(flow>0?a:b)]+=std::abs(flow);
        };
        for(int y=0;y<side;++y)for(int x=0;x<side;++x){const int a=y*side+x;
            if(x+1<side)update(a,a+1,fluxX[(size_t)a],forceX);
            if(y+1<side)update(a,a+side,fluxY[(size_t)a],forceY);
        }
        for(size_t i=0;i<cells;++i)limit[i]=outgoing[i]>0?std::min(1.f,std::max(0.f,depth[i])*area/(dt*outgoing[i])):1.f;
        const auto transfer=[&](int a,int b,float& flow) {
            flow*=limit[(size_t)(flow>0?a:b)];const float moved=dt*flow/area;
            depth[(size_t)a]-=moved;depth[(size_t)b]+=moved;
        };
        for(int y=0;y<side;++y)for(int x=0;x<side;++x){const int a=y*side+x;
            if(x+1<side)transfer(a,a+1,fluxX[(size_t)a]);
            if(y+1<side)transfer(a,a+side,fluxY[(size_t)a]);
        }
        for(int y=0;y<side;++y)for(int x=0;x<side;++x){const size_t a=(size_t)(y*side+x);
            const float crossSection=spacing*std::max(.006f,depth[a]);
            velocityX[a]=std::clamp(.5f*(fluxX[a]+(x>0?fluxX[a-1]:0))/crossSection,-1.2f,1.2f);
            velocityY[a]=std::clamp(.5f*(fluxY[a]+(y>0?fluxY[a-side]:0))/crossSection,-1.2f,1.2f);
        }
    }
    Sample sample(float x,float y)const {
        if(x<-1||x>1||y<-1||y>1)return {};
        const float gx=(x+1)/spacing,gy=(y+1)/spacing;
        const int ix=std::min(side-2,(int)gx),iy=std::min(side-2,(int)gy);
        const float fx=gx-ix,fy=gy-iy;Sample value;
        for(int j=0;j<2;++j)for(int i=0;i<2;++i){const size_t at=(size_t)((iy+j)*side+ix+i);const float w=(i?fx:1-fx)*(j?fy:1-fy);
            value.bed+=w*bed[at];value.depth+=w*depth[at];value.vx+=w*velocityX[at];value.vy+=w*velocityY[at];value.coverage+=w*signedEdge[at];
        }
        value.coverage=std::clamp(value.coverage/(spacing*.65f)+.5f,0.f,1.f);
        return value;
    }
    double volume()const {double sum=0;for(float d:depth)sum+=d;return sum*spacing*spacing;}
    float minimumDepth()const {return *std::min_element(depth.begin(),depth.end());}
    float maximumSpeed()const {float value=0;for(size_t i=0;i<cells;++i)value=std::max(value,std::hypot(velocityX[i],velocityY[i]));return value;}
    std::array<float,2> centroid()const {double mass=0,x=0,y=0;for(int j=0;j<side;++j)for(int i=0;i<side;++i){const float d=depth[(size_t)(j*side+i)];mass+=d;x+=d*(i*spacing-1);y+=d*(j*spacing-1);}return {(float)(x/std::max(1.e-12,mass)),(float)(y/std::max(1.e-12,mass))};}
private:
    double accumulator=0;
    std::array<bool,cells> mask{};
    std::array<float,cells> bed{},depth{},signedEdge{},fluxX{},fluxY{},velocityX{},velocityY{};
};
