#pragma once
// The same rounded-disc intersections and optical material run on Metal and
// the CPU fallback. All coordinates here are world axes relative to the tower.
#ifdef __METAL_VERSION__
#define TOWER_CONSTANT constant
#define TOWER_OUT thread
namespace tide { namespace tower {
using Vec=float3;
using Parameters=TowerParameters;
inline float lengthOf(Vec v){return length(v);}
inline float dotOf(Vec a,Vec b){return dot(a,b);}
#else
#include "GpuEffects.h"
#include "SphericalWater.h"
#define TOWER_CONSTANT const
#define TOWER_OUT
namespace tide { namespace tower {
using Vec=SphericalWater::Vec;
using Parameters=tide::gpu::TowerParameters;
using std::abs;using std::max;using std::min;using std::clamp;using std::sqrt;using std::sin;using std::cos;using std::exp;using std::pow;
inline float lengthOf(Vec v){return v.length();}
inline float dotOf(Vec a,Vec b){return a.dot(b);}
#endif
inline Vec unitOf(Vec v){return v*(1.f/max(1.e-9f,lengthOf(v)));}

inline float smooth(float a,float b,float value){float t=clamp((value-a)/(b-a),0.f,1.f);return t*t*(3-2*t);}
inline Vec reflection(Vec ray,Vec n){return ray-n*(2*dotOf(ray,n));}
inline bool refraction(Vec ray,Vec n,float eta,TOWER_OUT Vec& result){float c=clamp(-dotOf(ray,n),0.f,1.f),d=1-eta*eta*(1-c*c);if(d<0)return false;result=ray*eta+n*(eta*c-sqrt(d));return true;}
inline Vec tint(int identity){return identity==0?Vec{1,.88f,.67f}:identity==1?Vec{.57f,.90f,1}:Vec{.88f,.73f,1};}
inline Vec world(TOWER_CONSTANT Parameters& p,Vec v){return Vec{p.right.x,p.right.y,p.right.z}*v.x+Vec{p.up.x,p.up.y,p.up.z}*v.y+Vec{p.back.x,p.back.y,p.back.z}*v.z;}
inline Vec camera(TOWER_CONSTANT Parameters& p,Vec v){return {dotOf(v,{p.right.x,p.right.y,p.right.z}),dotOf(v,{p.up.x,p.up.y,p.up.z}),dotOf(v,{p.back.x,p.back.y,p.back.z})};}
inline Vec studioLight(Vec d){
    float z=max(.05f,d.z),u=d.x/z,v=d.y/z;
    float key=(1-smooth(.32f,.43f,abs(u+.75f)))*(1-smooth(.60f,.72f,abs(v-.95f)));
    float strip=(1-smooth(.035f,.065f,abs(u-.83f)))*(1-smooth(.8f,1.1f,abs(v-.2f)));
    float horizon=1-smooth(.025f,.10f,abs(d.y+.18f));
    return Vec{.20f,.27f,.30f}+Vec{5.8f,5.6f,5.1f}*key+Vec{2.4f,3.2f,3.3f}*strip+Vec{.8f,1.3f,1.5f}*horizon;
}
inline Vec environmentLight(TOWER_CONSTANT Parameters& p,Vec origin,Vec ray){
    Vec location=Vec{p.originRadius.x,p.originRadius.y,p.originRadius.z}+origin*p.originRadius.w;
    if(ray.z<-.0001f){float t=-location.z/ray.z;if(t>0){Vec hit=location+ray*t;
        if(abs(hit.x)<p.room.x*.5f&&hit.y>0&&hit.y<p.room.y){float u=hit.x/p.room.x+.5f,v=hit.y/p.room.y;
            float key=exp(-((u-.30f)*(u-.30f)*7+(v-.62f)*(v-.62f)*3.2f));
            float fill=exp(-((u-.84f)*(u-.84f)*18+(v-.75f)*(v-.75f)*7));
            return Vec{.049f,.058f,.063f}+Vec{.088f,.081f,.068f}*key+Vec{.017f,.020f,.023f}*fill;}}}
    return Vec{.043f,.063f,.086f};
}
// One continuous liquid volume encloses all plates. The inner plates are dry
// geometry with a glass/water interface, not separate water-coated cylinders.
TOWER_CONSTANT constexpr float imageSpan=4.4f;
TOWER_CONSTANT constexpr float colourSplit=.0425f; // half the previous note separation
inline Vec multiply(Vec a,Vec b){return {a.x*b.x,a.y*b.y,a.z*b.z};}
inline float largest(Vec a){return max(a.x,max(a.y,a.z));}
inline float distance(TOWER_CONSTANT Parameters& p,int plate,Vec q,bool){
    auto disc=p.plates[plate];q.z-=disc.y;
    float halfHeight=disc.z,bevel=min(.035f,halfHeight*.45f);
    float radial=sqrt(q.x*q.x+q.y*q.y)-disc.x*.89f+bevel,vertical=abs(q.z)-halfHeight+bevel;
    float a=max(radial,0.f),b=max(vertical,0.f);return min(max(radial,vertical),0.f)+sqrt(a*a+b*b)-bevel;
}
inline Vec normal(TOWER_CONSTANT Parameters& p,int plate,Vec point,bool wet){
    constexpr float e=.0008f;
    return unitOf({distance(p,plate,point+Vec{e,0,0},wet)-distance(p,plate,point-Vec{e,0,0},wet),
                   distance(p,plate,point+Vec{0,e,0},wet)-distance(p,plate,point-Vec{0,e,0},wet),
                   distance(p,plate,point+Vec{0,0,e},wet)-distance(p,plate,point-Vec{0,0,e},wet)});
}
inline bool intersection(TOWER_CONSTANT Parameters& p,int plate,Vec origin,Vec ray,bool wet,float limit,TOWER_OUT float& found){
    auto disc=p.plates[plate];float radius=disc.x*.89f;
    Vec lo={-radius-.001f,-radius-.001f,disc.y-disc.z-.001f},hi={radius+.001f,radius+.001f,disc.y+disc.z+.001f};
    float entry=.0003f,exit=limit;
    for(int axis=0;axis<3;++axis){float o=axis==0?origin.x:axis==1?origin.y:origin.z,d=axis==0?ray.x:axis==1?ray.y:ray.z;
        float a=axis==0?lo.x:axis==1?lo.y:lo.z,b=axis==0?hi.x:axis==1?hi.y:hi.z;
        if(abs(d)<1.e-7f){if(o<a||o>b)return false;}else{float t0=(a-o)/d,t1=(b-o)/d;entry=max(entry,min(t0,t1));exit=min(exit,max(t0,t1));if(entry>exit)return false;}}
    float t=entry;
    for(int step=0;step<40&&t<exit;++step){float d=distance(p,plate,origin+ray*t,wet);if(abs(d)<.0005f){found=t;return true;}t+=max(.00025f,abs(d)*.90f);}
    return false;
}
inline float waterDistance(TOWER_CONSTANT Parameters& p,Vec q){
    int last=max(0,(int)p.settings.z-1);float bottom=p.plates[0].y-p.plates[0].z,top=p.plates[last].y+p.plates[last].z;
    float middle=(bottom+top)*.5f,halfHeight=(top-bottom)*.5f,t=clamp((q.z-bottom)/max(.01f,top-bottom),0.f,1.f),time=p.settings.w;
    // Broad, delayed displacement is visible in the silhouette. Travelling
    // ripples ride on that volume instead of appearing as a plate texture.
    float belly=sin(t*3.14159265f),phase=time*1.7f+p.settings.y;
    q.x-=p.flow.x*(.08f+.045f*belly)+.018f*sin(q.z*3.4f-phase);
    q.y-=p.flow.y*(.08f+.045f*belly)+.015f*sin(q.z*4.1f-phase*.83f);
    float radius=1.f-.62f*(t*t*(3-2*t))+.11f+.035f*belly;
    float wave=.017f*sin(q.x*7+q.y*5-time*2.1f)+.009f*sin(q.y*13-q.x*3+time*2.9f);
    float radial=sqrt(q.x*q.x+q.y*q.y)-radius-wave,vertical=abs(q.z-middle)-halfHeight-.015f-wave*.6f;
    float a=max(radial,0.f),b=max(vertical,0.f);return min(max(radial,vertical),0.f)+sqrt(a*a+b*b)-.11f;
}
inline Vec waterNormal(TOWER_CONSTANT Parameters& p,Vec point){
    constexpr float e=.001f;
    return unitOf({waterDistance(p,point+Vec{e,0,0})-waterDistance(p,point-Vec{e,0,0}),waterDistance(p,point+Vec{0,e,0})-waterDistance(p,point-Vec{0,e,0}),waterDistance(p,point+Vec{0,0,e})-waterDistance(p,point-Vec{0,0,e})});
}
inline bool waterIntersection(TOWER_CONSTANT Parameters& p,Vec origin,Vec ray,TOWER_OUT float& hit){
    float t=.003f;
    for(int step=0;step<88&&t<8.f;++step){float d=waterDistance(p,origin+ray*t);if(abs(d)<.0007f){hit=t;return true;}t+=max(.0004f,abs(d)*.65f);}
    return false;
}
inline float schlick(float cosine,float from,float to){float f=(from-to)/(from+to);f*=f;return f+(1-f)*pow(1-clamp(cosine,0.f,1.f),5.f);}
inline float display(float value){return clamp(value<.8f?value:.8f+.2f*(1-exp(-(value-.8f)*5)),0.f,1.f);}
struct Pixel {Vec colour{0,0,0},emission{0,0,0};float alpha=0,light=0;int plate=-1;};
inline Pixel sample(TOWER_CONSTANT Parameters& p,float u,float v){
    Pixel result;Vec origin=world(p,{u,v,3.4f}),ray=world(p,{0,0,-1});float entry;
    if(!waterIntersection(p,origin,ray,entry))return result;
    Vec point=origin+ray*entry,n=waterNormal(p,point),inside;
    float f=schlick(-dotOf(ray,n),1.f,1.333f);
    Vec colour=(studioLight(camera(p,reflection(ray,n)))+Vec{.40f,.48f,.52f})*f,emission{0,0,0},throughput{1-f,1-f,1-f};
    if(!refraction(ray,n,1.f/1.333f,inside)){result.colour=colour;result.alpha=1;return result;}
    origin=point+inside*.004f;ray=inside;
    // Follow glass entry and exit separately, retaining the plates and the room
    // seen behind each one. Beer-Lambert absorption creates smoked glass.
    for(int layer=0;layer<8&&largest(throughput)>.018f;++layer){
        float nearest=8;int plate=-1;
        for(int i=0;i<(int)p.settings.z;++i){float hit;if(intersection(p,i,origin,ray,false,nearest,hit)){nearest=hit;plate=i;}}
        if(plate<0)break;if(result.plate<0)result.plate=plate;
        point=origin+ray*nearest;n=normal(p,plate,point,false);
        const float activity=p.plates[plate].w;result.light=max(result.light,activity);
        float glassF=schlick(-dotOf(ray,n),1.333f,1.52f);
        colour=colour+multiply(throughput,studioLight(camera(p,reflection(ray,n))))*glassF;
        emission=emission+multiply(throughput,tint((int)p.settings.y))*(activity*1.65f);
        throughput=throughput*((1-glassF)*(1-activity*.78f));
        Vec glassRay;if(!refraction(ray,n,1.333f/1.52f,glassRay))break;
        float exitDistance;Vec glassOrigin=point+glassRay*.004f;
        if(!intersection(p,plate,glassOrigin,glassRay,false,4.f,exitDistance)){origin=point+ray*.02f;continue;}
        Vec exit=glassOrigin+glassRay*exitDistance;float thickness=exitDistance*p.originRadius.w;
        throughput=multiply(throughput,{exp(-thickness*2.6f),exp(-thickness*1.9f),exp(-thickness*1.6f)});
        Vec outgoing;Vec exitNormal=normal(p,plate,exit,false);
        if(!refraction(glassRay,exitNormal*-1.f,1.52f/1.333f,outgoing)){colour=colour+multiply(throughput,studioLight(camera(p,reflection(glassRay,exitNormal))))*.5f;throughput={0,0,0};break;}
        origin=exit+outgoing*.004f;ray=outgoing;
    }
    float leave;Vec finalRay=ray;
    if(waterIntersection(p,origin,ray,leave)){
        point=origin+ray*leave;Vec exitNormal=waterNormal(p,point),outgoing;
        float exitF=schlick(abs(dotOf(ray,exitNormal)),1.333f,1.f);
        colour=colour+multiply(throughput,studioLight(camera(p,reflection(ray,exitNormal)))+Vec{.40f,.48f,.52f})*(exitF*.65f);
        throughput=throughput*(1-exitF);
        if(refraction(ray,exitNormal*-1.f,1.333f,outgoing))finalRay=outgoing;
        throughput=multiply(throughput,{exp(-leave*.045f),exp(-leave*.018f),exp(-leave*.011f)});
        origin=point;
    }
    colour=colour+multiply(throughput,environmentLight(p,origin,finalRay));
    Vec lit=colour+emission;
    result.colour={display(lit.x),display(lit.y),display(lit.z)};
    result.emission={max(0.f,result.colour.x-display(colour.x)),max(0.f,result.colour.y-display(colour.y)),max(0.f,result.colour.z-display(colour.z))};
    result.alpha=1;return result;
}
}}
#undef TOWER_CONSTANT
#undef TOWER_OUT
