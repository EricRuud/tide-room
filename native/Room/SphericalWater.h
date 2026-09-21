#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

// UI-only radial shallow-water approximation. Equal and opposite edge fluxes
// conserve area-weighted water volume on a closed spherical mesh. Tide X/Y is
// an artistic lateral potential, not a full astronomical differential tide.
class SphericalWater {
public:
    struct Vec {
        float x=0,y=0,z=0;
        Vec operator+(Vec b)const{return {x+b.x,y+b.y,z+b.z};}
        Vec operator-(Vec b)const{return {x-b.x,y-b.y,z-b.z};}
        Vec operator*(float a)const{return {x*a,y*a,z*a};}
        float dot(Vec b)const{return x*b.x+y*b.y+z*b.z;}
        Vec cross(Vec b)const{return {y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x};}
        float length()const{return std::sqrt(dot(*this));}
        Vec unit()const{return *this*(1.f/std::max(1.e-9f,length()));}
    };
    struct Face {int a,b,c;};
    struct Edge {int a,b;float conductance,length;};
    struct Mesh {
        std::vector<Vec> points;
        std::vector<Face> faces;
        std::vector<Edge> edges;
        std::vector<float> area;
        Mesh() {
            const float t=(1+std::sqrt(5.f))*.5f;
            points={{-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},{0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},{t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1}};
            for(auto& p:points)p=p.unit();
            faces={{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
            for(int level=0;level<3;++level) {
                std::map<std::pair<int,int>,int> mids;std::vector<Face> next;
                const auto midpoint=[&](int a,int b){auto key=std::minmax(a,b);const auto found=mids.find(key);if(found!=mids.end())return found->second;
                    const int id=(int)points.size();points.push_back((points[(size_t)a]+points[(size_t)b]).unit());mids[key]=id;return id;};
                for(auto f:faces){const int a=midpoint(f.a,f.b),b=midpoint(f.b,f.c),c=midpoint(f.c,f.a);next.insert(next.end(),{{f.a,a,c},{f.b,b,a},{f.c,c,b},{a,b,c}});}faces=std::move(next);
            }
            area.resize(points.size());std::map<std::pair<int,int>,bool> unique;
            for(auto f:faces){const auto a=points[(size_t)f.a],b=points[(size_t)f.b],c=points[(size_t)f.c];
                const float third=2*std::atan2(std::abs(a.dot(b.cross(c))),1+a.dot(b)+b.dot(c)+c.dot(a))/3;
                for(int i:{f.a,f.b,f.c})area[(size_t)i]+=third;
                unique[std::minmax(f.a,f.b)]=true;unique[std::minmax(f.b,f.c)]=true;unique[std::minmax(f.c,f.a)]=true;
            }
            for(auto pair:unique){const int a=pair.first.first,b=pair.first.second;const float length=(points[(size_t)b]-points[(size_t)a]).length();
                edges.push_back({a,b,(area[(size_t)a]+area[(size_t)b])/(3*length*length),length});}
        }
    };
    static constexpr float stepSeconds=1.f/120;
    static const Mesh& mesh(){static const Mesh value;return value;}
    void prepare(int identity,bool smoothShell=false) {
        const auto& m=mesh();const auto count=m.points.size();bed.resize(count);depth.resize(count);velocity.assign(count,{});
        flux.assign(m.edges.size(),0);outgoing.resize(count);limit.resize(count);potential.resize(count);accumulator=0;
        const float phase=(float)identity*1.9f;
        for(size_t i=0;i<count;++i){const auto n=m.points[i];
            bed[i]=-.026f+.059f*std::sin(n.x*4.1f+n.z*2+phase)*std::cos(n.y*3.6f-.6f)
                +.037f*std::sin(n.y*6+n.z*3-phase)+.019f*std::cos(n.x*10-n.y*4+n.z*5+phase);
            if(smoothShell)bed[i]=-.40f+.12f*bed[i];
            depth[i]=std::max(0.f,.015f-bed[i]);}
    }
    int advance(double seconds,float tideX,float tideY,float tideZ=0) {
        accumulator+=std::clamp(seconds,0.,.2);int count=0;
        while(accumulator+1.e-10>=stepSeconds){step(tideX,tideY,tideZ);accumulator-=stepSeconds;++count;}return count;
    }
    void step(float tideX,float tideY,float tideZ=0) {
        const auto& m=mesh();std::fill(outgoing.begin(),outgoing.end(),0.f);
        const float strength=std::sqrt(tideX*tideX+tideY*tideY+tideZ*tideZ),scale=strength>1.e-7f?.15f*std::tanh(3.2f*strength)/strength:0;
        for(size_t i=0;i<depth.size();++i)potential[i]=scale*(m.points[i].x*tideX+m.points[i].y*tideY+m.points[i].z*tideZ);
        for(size_t e=0;e<m.edges.size();++e){const auto edge=m.edges[e];const auto a=(size_t)edge.a,b=(size_t)edge.b;
            const float crest=std::max(bed[a],bed[b]);const float da=std::max(0.f,bed[a]+depth[a]-crest),db=std::max(0.f,bed[b]+depth[b]-crest);
            const float wet=std::max(da,db);auto& flow=flux[e];
            if(wet<1.e-7f){flow=0;continue;}
            flow=.993f*flow+stepSeconds*4.f*wet*edge.conductance*(da-db+potential[b]-potential[a]);
            outgoing[flow>0?a:b]+=std::abs(flow);
        }
        for(size_t i=0;i<depth.size();++i)limit[i]=outgoing[i]>0?std::min(1.f,std::max(0.f,depth[i])*m.area[i]/(stepSeconds*outgoing[i])):1;
        std::fill(velocity.begin(),velocity.end(),Vec{});
        for(size_t e=0;e<m.edges.size();++e){const auto edge=m.edges[e];const auto a=(size_t)edge.a,b=(size_t)edge.b;auto& flow=flux[e];
            flow*=limit[flow>0?a:b];const float moved=stepSeconds*flow;depth[a]-=moved/m.area[a];depth[b]+=moved/m.area[b];
            const auto direction=(m.points[b]-m.points[a])*(flow*edge.length);
            velocity[a]=velocity[a]+direction;velocity[b]=velocity[b]+direction;
        }
        for(size_t i=0;i<depth.size();++i){const auto n=m.points[i];auto v=velocity[i]*(.5f/(m.area[i]*std::max(.008f,depth[i])));v=v-n*v.dot(n);
            velocity[i]=v*(std::min(1.f,1.f/std::max(1.e-7f,v.length())));}
    }
    double volume()const {double result=0;const auto& m=mesh();for(size_t i=0;i<depth.size();++i)result+=depth[i]*m.area[i];return result;}
    float minimumDepth()const{return *std::min_element(depth.begin(),depth.end());}
    float maximumSpeed()const {float result=0;for(auto v:velocity)result=std::max(result,v.length());return result;}
    std::array<float,3> centroid()const {Vec result;const auto& m=mesh();for(size_t i=0;i<depth.size();++i)result=result+m.points[i]*(depth[i]*m.area[i]);result=result*(1.f/(float)std::max(1.e-12,volume()));return {result.x,result.y,result.z};}
    std::vector<float> bed,depth;
    std::vector<Vec> velocity;
private:
    double accumulator=0;
    std::vector<float> flux,outgoing,limit,potential;
};
