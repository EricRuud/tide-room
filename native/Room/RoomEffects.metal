#include <metal_stdlib>
using namespace metal;

struct PlanetParameters {
    float4 shape, dipole, axis, inverseAxes, waves[3], phases, amplitudes;
    float4 originRadius, right, up, back, room, settings;
};
struct TowerParameters {
    float4 originRadius, right, up, back, room, settings, flow, plates[16];
};
#include "TowerOptics.h"
struct GlassParameters {
    float4 sizeScale, bounds, centreFocal, eye, forward, right, up, room;
};
struct Lights {float4 geometry[3], tint[3];};
struct GlassMap {float4 redGreen, blueTransmissionSoftness, light;};

float3 unit(float3 v) {return v / max(1.e-9f, length(v));}
uint byteValue(float v) {return uint(clamp(v + .5f, 0.f, 255.f));}
uint rgba(float4 c) {return byteValue(c.z) | (byteValue(c.y)<<8) | (byteValue(c.x)<<16) | (byteValue(c.w)<<24);}
float4 unpack(uint c) {return float4((c>>16)&255, (c>>8)&255, c&255, c>>24);}
float4 samplePixels(device const uint* image, int2 size, float2 p) {
    p=clamp(p,float2(0),float2(size)-1.001f);
    int2 a=int2(p);float2 f=p-float2(a);
    return mix(mix(unpack(image[a.y*size.x+a.x]),unpack(image[a.y*size.x+a.x+1]),f.x),
               mix(unpack(image[(a.y+1)*size.x+a.x]),unpack(image[(a.y+1)*size.x+a.x+1]),f.x),f.y);
}
float4 borderPixel(device const uint* image, int size, int2 p) {
    return any(p<0)||any(p>=size)?float4(0):unpack(image[p.y*size+p.x]);
}
float4 sampleBorder(device const uint* image, int size, float2 p) {
    int2 a=int2(floor(p));float2 f=p-float2(a);
    return mix(mix(borderPixel(image,size,a),borderPixel(image,size,a+int2(1,0)),f.x),
               mix(borderPixel(image,size,a+int2(0,1)),borderPixel(image,size,a+int2(1,1)),f.x),f.y);
}
float3 floorMaterial(float x,float y,float width,float depth) {
    float u=x/max(1.f,width)+.5f,v=y/max(1.f,depth);
    float key=exp(-((u-.30f)*(u-.30f)*7.f+(v-.62f)*(v-.62f)*3.2f));
    float fill=exp(-((u-.84f)*(u-.84f)*18.f+(v-.75f)*(v-.75f)*7.f));
    return float3(.049f,.058f,.063f)+float3(.088f,.081f,.068f)*key+float3(.017f,.020f,.023f)*fill;
}
bool bend(float3 incident,float3 normal,float eta,thread float3& transmitted) {
    float cosine=clamp(-dot(incident,normal),0.f,1.f),d=1-eta*eta*(1-cosine*cosine);
    if(d<0)return false;transmitted=incident*eta+normal*(eta*cosine-sqrt(d));return true;
}
float fresnel(float cosine,float a,float b) {
    cosine=clamp(cosine,0.f,1.f);float sine2=(a*a)/(b*b)*(1-cosine*cosine);
    if(sine2>=1)return 1;float tc=sqrt(1-sine2);
    float parallel=(b*cosine-a*tc)/(b*cosine+a*tc),perpendicular=(a*cosine-b*tc)/(a*cosine+b*tc);
    return .5f*(parallel*parallel+perpendicular*perpendicular);
}
float sphere(float3 origin,float3 direction,float radius) {
    float b=dot(origin,direction),c=dot(origin,origin)-radius*radius,d=b*b-c;
    if(d<0)return -1;float root=sqrt(d),front=-b-root;
    return front>.0001f?front:((-b+root)>.0001f?-b+root:-1);
}
float3 axial(constant PlanetParameters& s,float3 v) {return v*s.shape.z+s.axis.xyz*(dot(v,s.axis.xyz)*(s.shape.y-s.shape.z));}
float3 toSphere(constant PlanetParameters& s,float3 v) {return axial(s,v)*s.inverseAxes.xyz;}
float3 waterNormal(constant PlanetParameters& s,float3 point) {
    float3 p=point-s.dipole.xyz,v=toSphere(s,p);
    float3 n=unit(axial(s,v*s.inverseAxes.xyz)),direction=unit(p),grad=0;
    for(int i=0;i<3;++i)grad+=s.waves[i].xyz*(cos(dot(s.waves[i].xyz,direction)+s.phases[i])*s.amplitudes[i]);
    grad-=n*dot(grad,n);return unit(n-grad);
}
bool surface(constant PlanetParameters& s,float3 origin,float3 direction,thread float3& point) {
    float3 o=toSphere(s,origin-s.dipole.xyz),d=toSphere(s,direction);
    float a=dot(d,d),b=dot(o,d),c=dot(o,o)-s.shape.x*s.shape.x,discriminant=b*b-a*c;
    if(discriminant<0)return false;float root=sqrt(discriminant),front=(-b-root)/a,back=(-b+root)/a;
    float t=front>.0001f?front:back;if(t<=.0001f)return false;point=origin+direction*t;return true;
}
float3 studio(float3 direction) {
    float z=max(.05f,direction.z),u=direction.x/z,v=direction.y/z;
    float key=(1-smoothstep(.32f,.43f,abs(u+.75f)))*(1-smoothstep(.60f,.72f,abs(v-.95f)));
    float strip=(1-smoothstep(.035f,.065f,abs(u-.83f)))*(1-smoothstep(.8f,1.1f,abs(v-.2f)));
    float wash=max(0.f,direction.y)*.09f;
    float violet=(1-smoothstep(.045f,.11f,abs(u+1.65f)))*(1-smoothstep(.7f,1.4f,abs(v+.25f)));
    float horizon=1-smoothstep(.025f,.10f,abs(direction.y+.18f));
    return float3(.20f+wash,.32f+wash,.39f+wash)+float3(6.4f,6.6f,6.8f)*key
        +float3(2.f,3.6f,4.1f)*strip+float3(2.4f,.85f,3.2f)*violet+float3(.8f,1.7f,1.9f)*horizon;
}
float3 worldVector(constant PlanetParameters& p,float3 v) {return p.right.xyz*v.x+p.up.xyz*v.y+p.back.xyz*v.z;}
float3 environment(constant PlanetParameters& c,float3 origin,float3 direction) {
    float3 p=worldVector(c,origin)*c.originRadius.w+c.originRadius.xyz,d=worldVector(c,direction);
    if(d.z<-.0001f){float t=-p.z/d.z;if(t>0){float3 hit=p+d*t;
        if(abs(hit.x)<c.room.x*.5f&&hit.y>0&&hit.y<c.room.y)return floorMaterial(hit.x,hit.y,c.room.x,c.room.y);}}
    return float3(.043f,.063f,.086f);
}
float3 lightTint(int identity) {
    return identity==0?float3(1,.88f,.67f):identity==1?float3(.57f,.90f,1):float3(.88f,.73f,1);
}
float3 core(float3 p,float3 view,int identity,float activity) {
    float3 n=unit(p),light=unit(float3(-.6f,.9f,1.3f));float diffuse=max(0.f,dot(n,light));
    float3 tone=identity==0?float3(.76f,.56f,.32f):identity==1?float3(.25f,.61f,.64f):float3(.58f,.37f,.66f);
    float sheen=max(0.f,dot(n,unit(light+view)));for(int i=0;i<5;++i)sheen*=sheen;sheen*=.48f;
    float3 reflection=studio(reflect(-view,n));float edge=1-clamp(dot(n,view),0.f,1.f);float f=.10f+.24f*edge*edge*edge;
    return tone*(.12f+.74f*diffuse)+reflection*f+sheen+lightTint(identity)*(activity*.80f);
}
float absorption(float x) {return x>.18f?exp(-x):1-max(0.f,x)*(1-max(0.f,x)*(.5f-max(0.f,x)/6));}
float3 transmission(constant PlanetParameters& s,float3 entry,float3 direction,int identity,float activity) {
    float hit=sphere(entry,direction,.60f);
    if(hit>0)return core(entry+direction*hit,-direction,identity,activity)*float3(absorption(.065f*hit),absorption(.024f*hit),absorption(.014f*hit));
    float3 exit;if(!surface(s,entry+direction*.001f,direction,exit))return environment(s,entry,direction);
    float3 n=waterNormal(s,exit),outgoing;
    if(!bend(direction,-n,1.333f,outgoing))return studio(reflect(direction,n))*.65f;
    float f=fresnel(abs(dot(direction,n)),1.333f,1.f);
    return environment(s,exit,outgoing)*(1-f)+studio(reflect(direction,n))*f;
}
float display(float value) {return clamp(value<.8f?value:.8f+.2f*(1-exp(-(value-.8f)*5)),0.f,1.f);}
kernel void planet(device uint* output [[buffer(0)]],constant PlanetParameters& p [[buffer(1)]],uint2 xy [[thread_position_in_grid]]) {
    int size=int(p.settings.x);if(any(xy>=uint(size)))return;
    float u=(float(xy.x)+.5f-size*.5f)*(3.36f/size),v=(size*.5f-float(xy.y)-.5f)*(3.36f/size);
    float3 point,incident=float3(0,0,-1);uint index=xy.y*size+xy.x;
    if(u*u+v*v>1.67f*1.67f||!surface(p,float3(u,v,3),incident,point)){output[index]=0;return;}
    float3 n=waterNormal(p,point),ray;bool transmitted=bend(incident,n,1.f/1.333f,ray);
    float f=fresnel(max(0.f,n.z),1,1.333f);
    float3 colour=(transmitted?transmission(p,point,ray,int(p.settings.y),p.shape.w):float3(0))*(1-f)+studio(reflect(incident,n))*f;
    output[index]=rgba(float4(display(colour.x)*255,display(colour.y)*255,display(colour.z)*255,255));
}
kernel void planetColour(device const uint* source [[buffer(0)]],device uint* output [[buffer(1)]],constant PlanetParameters& p [[buffer(2)]],uint2 xy [[thread_position_in_grid]]) {
    int size=int(p.settings.x),pad=(size+11)/12,extent=size+pad*2;if(any(xy>=uint(extent)))return;
    float light=clamp(p.shape.w,0.f,1.f),angle=p.settings.y*2.1f+.3f;
    float2 pos=float2(xy)-pad,offset=float2(cos(angle),sin(angle))*float(size)/3.36f*.10f*light;
    float2 shift=(pos-(float(size)-1)*.5f)*(.065f*light)+offset;
    float r=sampleBorder(source,size,pos+shift).x,b=sampleBorder(source,size,pos-shift).z;
    float4 base=borderPixel(source,size,int2(pos));
    output[xy.y*extent+xy.x]=rgba(float4(r,base.y,b,max(base.w,max(r,b))));
}

kernel void tower(device uint* output [[buffer(0)]],device uint* lit [[buffer(1)]],constant TowerParameters& p [[buffer(2)]],uint2 xy [[thread_position_in_grid]]) {
    int size=int(p.settings.x);if(any(xy>=uint(size)))return;
    float u=(float(xy.x)+.5f-size*.5f)*(tide::tower::imageSpan/size),v=(size*.5f-float(xy.y)-.5f)*(tide::tower::imageSpan/size);
    auto pixel=tide::tower::sample(p,u,v);uint at=xy.y*size+xy.x;
    float4 colour=float4(pixel.colour,pixel.alpha)*255;output[at]=rgba(colour);lit[at]=rgba(float4(pixel.emission,tide::tower::largest(pixel.emission))*255);
}
kernel void towerColour(device const uint* source [[buffer(0)]],device const uint* lit [[buffer(1)]],device uint* output [[buffer(2)]],constant TowerParameters& p [[buffer(3)]],uint2 xy [[thread_position_in_grid]]) {
    int size=int(p.settings.x);if(any(xy>=uint(size)))return;int index=int(xy.y)*size+int(xy.x);
    float angle=p.settings.y*2.1f+.3f;float2 offset=float2(cos(angle),sin(angle))*float(size)/tide::tower::imageSpan*tide::tower::colourSplit;
    float4 base=unpack(source[index]),glow=unpack(lit[index]);
    float4 red=sampleBorder(lit,size,float2(xy)+offset),blue=sampleBorder(lit,size,float2(xy)-offset);
    float3 colour=max(float3(0),base.xyz-glow.xyz)+float3(red.x,glow.y,blue.z);
    output[index]=rgba(float4(colour,max(base.w,max(red.w,blue.w))));
}

// Finite glass slab: exactly the same camera and Snell geometry as CPU picking.
float4 trace(constant GlassParameters& v,float2 screen,float ior) {
    float4 out=float4(screen,0,0);float3 origin=v.eye.xyz;
    float3 ray=unit(v.forward.xyz+v.right.xyz*((screen.x-v.centreFocal.x)/v.centreFocal.z)+v.up.xyz*((v.centreFocal.y-screen.y)/v.centreFocal.z));
    float3 lo=float3(-v.room.x*.5f,0,0),hi=float3(v.room.x*.5f,v.room.y,v.room.z);
    float enter=0,leave=1.e6f,sign=0;int face=-1;
    for(int i=0;i<3;++i){if(abs(ray[i])<1.e-7f){if(origin[i]<lo[i]||origin[i]>hi[i])return out;continue;}
        float a=(lo[i]-origin[i])/ray[i],b=(hi[i]-origin[i])/ray[i],s=-1;
        if(a>b){float swap=a;a=b;b=swap;s=1;}if(a>enter){enter=a;face=i;sign=s;}leave=min(leave,b);if(enter>=leave)return out;}
    if(face<0)return out;float3 p=origin+ray*enter,normal=0;normal[face]=sign;float edge=0;
    for(int i=0;i<3;++i)if(i!=face){float a=p[i]-lo[i],b=hi[i]-p[i],distance=min(a,b);
        float bevel=clamp(1-distance/.30f,0.f,1.f);edge=max(edge,bevel);normal[i]=(a<b?-1.f:1.f)*bevel*.48f;}
    float3 n=unit(normal);if(-dot(ray,n)<.08f){normal=0;normal[face]=sign;n=normal;}
    float cosine=max(.08f,-dot(ray,n));float3 internal;if(!bend(ray,n,1/ior,internal))return out;
    float3 shift=internal*(.24f/max(.08f,-dot(internal,n)))-ray*(.24f/cosine);
    float3 relative=p+ray*max(1.f,(leave-enter)*.5f)+shift-origin;
    float projection=v.centreFocal.z/max(.05f,dot(relative,v.forward.xyz));
    return float4(v.centreFocal.x+dot(relative,v.right.xyz)*projection,v.centreFocal.y-dot(relative,v.up.xyz)*projection,fresnel(cosine,1,ior),edge);
}
kernel void glassMap(device GlassMap* map [[buffer(0)]],constant GlassParameters& p [[buffer(1)]],uint2 xy [[thread_position_in_grid]]) {
    int2 size=int2(p.sizeScale.xy);if(any(xy>=uint2(size)))return;
    float2 screen=p.bounds.xy+(float2(xy)+.5f)/p.sizeScale.z;
    float4 g=trace(p,screen,1.517f),r=trace(p,screen,1.509f),b=trace(p,screen,1.526f);
    float2 n=(screen-p.centreFocal.xy)/(p.bounds.zw*.5f);
    float key=(n.x+n.y*.28f+.27f)*4.5f,fill=(n.x-n.y*.2f-.65f)*10;
    float sheen=exp(-key*key)*.55f+exp(-fill*fill)*.35f;
    float reflected=g.z*(.30f+sheen*1.75f);
    GlassMap m;
    m.redGreen=float4(clamp((r.xy-p.bounds.xy)*p.sizeScale.z-.5f,float2(0),float2(size)-1.001f),clamp((g.xy-p.bounds.xy)*p.sizeScale.z-.5f,float2(0),float2(size)-1.001f));
    m.blueTransmissionSoftness=float4(clamp((b.xy-p.bounds.xy)*p.sizeScale.z-.5f,float2(0),float2(size)-1.001f),1-g.z*.34f,clamp(.07f+dot(n,n)*.16f+g.w*.04f,.07f,.28f));
    m.light=float4(float3(30,31,29)*reflected+float3(13,17,16)*(g.w*g.w),0);map[xy.y*size.x+xy.x]=m;
}
float3 glare(float2 point,constant Lights& lights) {
    float3 value=0;
    for(int i=0;i<3;++i){float4 p=lights.geometry[i];if(p.w<=.0001f||p.z<=0)continue;
        float2 delta=point-p.xy;float u=dot(delta,delta)/(p.z*p.z);if(u>=1)continue;
        float edge=1-u,falloff=edge*edge*edge,core=1/(1+u*24);
        value+=lights.tint[i].xyz*(p.w*(12*falloff+18*core*core*falloff));}
    return value;
}
kernel void glassRefract(device const uint* source [[buffer(0)]],device uint* output [[buffer(1)]],device const GlassMap* map [[buffer(2)]],constant GlassParameters& p [[buffer(3)]],constant Lights& lights [[buffer(4)]],uint2 xy [[thread_position_in_grid]]) {
    int2 size=int2(p.sizeScale.xy);if(any(xy>=uint2(size)))return;uint index=xy.y*size.x+xy.x;
    float3 colour;float2 sample=float2(xy);
    if(p.sizeScale.w>.5f)colour=unpack(source[index]).xyz;
    else {GlassMap m=map[index];sample=m.redGreen.zw;
        colour=float3(samplePixels(source,size,m.redGreen.xy).x,samplePixels(source,size,sample).y,samplePixels(source,size,m.blueTransmissionSoftness.xy).z)*m.blueTransmissionSoftness.z+m.light.xyz;
        // Match the CPU's intermediate 8-bit quantisation before adding glare.
        colour=floor(clamp(colour+.5f,0.f,255.f));}
    output[index]=rgba(float4(colour+glare(sample,lights),255));
}
kernel void downsample(device const uint* source [[buffer(0)]],device uint* output [[buffer(1)]],constant float4& dimensions [[buffer(2)]],uint2 xy [[thread_position_in_grid]]) {
    int2 small=int2(dimensions.zw),big=int2(dimensions.xy);if(any(xy>=uint2(small)))return;
    // Area sampling prevents tiny bright glints aliasing in the soft lens image.
    float2 step=float2(big)/float2(small);float4 sum=0;
    for(int y=0;y<4;++y)for(int x=0;x<4;++x)sum+=samplePixels(source,big,(float2(xy)+(float2(x,y)+.5f)/4)*step-.5f);
    output[xy.y*small.x+xy.x]=rgba(sum/16);
}
kernel void blur(device const uint* source [[buffer(0)]],device uint* output [[buffer(1)]],constant float4& dimensions [[buffer(2)]],uint2 xy [[thread_position_in_grid]]) {
    int2 size=int2(dimensions.xy);if(any(xy>=uint2(size)))return;float4 sum=0;
    for(int tap=-2;tap<=2;++tap){int2 at=clamp(int2(xy)+(dimensions.z>.5f?int2(tap,0):int2(0,tap)),int2(0),size-1);sum+=unpack(source[at.y*size.x+at.x]);}
    output[xy.y*size.x+xy.x]=rgba(floor(sum/5));
}
kernel void lensFinish(device const uint* source [[buffer(0)]],device const uint* soft [[buffer(1)]],device uint* output [[buffer(2)]],device const GlassMap* map [[buffer(3)]],constant GlassParameters& p [[buffer(4)]],constant float4& dimensions [[buffer(5)]],uint2 xy [[thread_position_in_grid]]) {
    int2 size=int2(p.sizeScale.xy);if(any(xy>=uint2(size)))return;uint index=xy.y*size.x+xy.x;
    float amount;
    if(p.sizeScale.w>.5f){float2 n=((float2(xy)+.5f)/p.sizeScale.z-p.bounds.zw*.5f)/(p.bounds.zw*.5f);amount=clamp(.07f+dot(n,n)*.16f,.07f,.28f);}
    else amount=map[index].blueTransmissionSoftness.w;
    float3 sharp=unpack(source[index]).xyz;
    float3 glow=samplePixels(soft,int2(dimensions.xy),float2(xy)*(dimensions.xy-1.001f)/float2(size)).xyz;
    output[index]=rgba(float4(mix(sharp,glow,amount)+max(float3(0),glow-95)*.065f,255));
}

struct Effects {float4 layers, controls, animation;};
float3 palette(float t) {return .5f+.5f*cos(6.2831853f*(float3(0,.33f,.67f)+t));}
kernel void visualEffects(device const uint* source [[buffer(0)]],device uint* output [[buffer(1)]],constant float4& dimensions [[buffer(2)]],constant Effects& fx [[buffer(3)]],uint2 xy [[thread_position_in_grid]]) {
    int2 size=int2(dimensions.xy);if(any(xy>=uint2(size)))return;uint index=xy.y*size.x+xy.x;
    float3 original=unpack(source[index]).xyz/255;
    float2 centre=(float2(size)-1)*.5f;
    float unitSize=min(float(size.x),float(size.y))*.5f;
    float2 uv=(float2(xy)-centre)/unitSize;
    float time=fx.animation.x,hue=fx.animation.y,react=fx.animation.z*fx.animation.w;
    float zoom=max(.25f,fx.controls.z),detail=fx.controls.w;
    float2 at=uv;
    if(fx.layers.z>0){
        float sectors=round(3+detail),sector=6.2831853f/sectors;
        float angle=atan2(uv.y,uv.x)+time*.08f+react*.12f;
        angle=abs(fmod(angle+12.5663706f,sector)-sector*.5f);
        float2 folded=float2(cos(angle),sin(angle))*length(uv)/(1+.06f*react);
        at=mix(at,folded,fx.layers.z);
    }
    if(fx.controls.x>0){
        float2 curl=float2(sin(at.y*5/zoom+time*.7f),cos(at.x*4/zoom-time*.6f));
        at+=curl*(fx.controls.x*(.055f+.07f*react));
    }
    float2 sample=at*unitSize+centre;
    float3 colour=samplePixels(source,size,sample).xyz/255;
    if(fx.layers.w>0){
        float2 split=(at*.8f+float2(cos(time*.13f),sin(time*.17f))*.2f)*unitSize*fx.layers.w*(.012f+.023f*react);
        colour.x=samplePixels(source,size,sample+split).x/255;
        colour.z=samplePixels(source,size,sample-split).z/255;
    }
    float2 field=at/zoom;
    if(fx.layers.x>0){
        float2 z=field*1.65f;
        float2 c=float2(-.745f+.07f*sin(time*.19f),.186f+.035f*cos(time*.23f)+react*.012f);
        float trap=1000,orbit=0;int count=int(12+detail*4);
        for(int i=0;i<count;++i){
            z=float2(z.x*z.x-z.y*z.y,2*z.x*z.y)+c;
            float d=abs(length(z)-.65f);trap=min(trap,d);orbit=float(i);
            if(dot(z,z)>128)break;
        }
        float line=exp(-trap*(24+detail*5));
        float3 ink=palette(hue+orbit*.035f+time*.012f)*.75f+.16f;
        float amount=fx.layers.x*clamp(line*(.6f+.55f*react),0.f,.9f);
        colour=1-(1-colour)*(1-ink*amount);
    }
    if(fx.layers.y>0){
        float2 mesh=field*(4+detail*1.5f);
        mesh+=float2(sin(field.y*3+time*.5f),cos(field.x*3-time*.45f))*(.7f+.7f*react);
        float3 phases=float3(mesh.x,mesh.y*.8660254f+mesh.x*.5f,mesh.y*.8660254f-mesh.x*.5f);
        float3 grid=abs(sin(phases*3.14159265f));float distance=min(grid.x,min(grid.y,grid.z));
        float width=max(.025f,(4+detail*1.5f)*3.14159265f/unitSize*1.2f/zoom);
        float line=1-smoothstep(width,width*2.6f,distance);
        float depth=.55f+.45f*sin(length(field)*4-time*.3f+react);
        float3 ink=palette(hue+length(field)*.13f+time*.018f)*.70f+.20f;
        float amount=fx.layers.y*line*depth*(.5f+.3f*react);
        colour=1-(1-colour)*(1-ink*clamp(amount,0.f,.85f));
    }
    output[index]=rgba(float4(clamp(mix(original,colour,fx.controls.y),0.f,1.f)*255,255));
}
