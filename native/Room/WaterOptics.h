#pragma once
#include "SphericalWater.h"

namespace tide::optics {
using Vec=SphericalWater::Vec;
// Weak absorption in the clear shell: the cubic differs from exp(-x) by less
// than 0.000043 for x <= 0.18, well below one 8-bit colour level. Denser media
// take the exact path. Avoids three transcendental calls for each core pixel.
inline float absorption(float opticalDepth) {
    if(opticalDepth>.18f)return std::exp(-opticalDepth);
    const float x=std::max(0.f,opticalDepth);return 1-x*(1-x*(.5f-x/6));
}
inline Vec reflect(Vec incident,Vec normal){return incident-normal*(2*incident.dot(normal));}
// normal faces the incident medium; eta = incident IOR / transmitted IOR.
inline bool refract(Vec incident,Vec normal,float eta,Vec& transmitted) {
    const float cosine=std::clamp(-incident.dot(normal),0.f,1.f);
    const float discriminant=1-eta*eta*(1-cosine*cosine);if(discriminant<0)return false;
    transmitted=incident*eta+normal*(eta*cosine-std::sqrt(discriminant));return true;
}
inline float fresnel(float cosine,float incidentIOR,float transmittedIOR) {
    cosine=std::clamp(cosine,0.f,1.f);const float sine2=(incidentIOR*incidentIOR)/(transmittedIOR*transmittedIOR)*(1-cosine*cosine);
    if(sine2>=1)return 1;const float transmittedCosine=std::sqrt(1-sine2);
    const float parallel=(transmittedIOR*cosine-incidentIOR*transmittedCosine)/(transmittedIOR*cosine+incidentIOR*transmittedCosine);
    const float perpendicular=(incidentIOR*cosine-transmittedIOR*transmittedCosine)/(incidentIOR*cosine+transmittedIOR*transmittedCosine);
    return .5f*(parallel*parallel+perpendicular*perpendicular);
}
inline float sphere(Vec origin,Vec direction,float radius) {
    const float b=origin.dot(direction),c=origin.dot(origin)-radius*radius,d=b*b-c;if(d<0)return -1;
    const float root=std::sqrt(d),front=-b-root;return front>.0001f?front:((-b+root)>.0001f?-b+root:-1);
}
}
