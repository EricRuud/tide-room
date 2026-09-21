#pragma once
#include "RasterWork.h"
#include <juce_graphics/juce_graphics.h>

// Split only the planet's own premultiplied texture. The green image anchors
// its position; red and blue spread in opposite directions on a note hit.
// Padding holds the spectral fringes outside the original water silhouette.
class PlanetColour {
public:
    static int padding(int width){return (width+11)/12;}
    static void render(const juce::Image& source,juce::Image& target,float strength,int identity) {
        const int size=source.getWidth(),pad=padding(size),extent=size+pad*2;
        if(target.getWidth()!=extent||target.getHeight()!=extent)target=juce::Image(juce::Image::ARGB,extent,extent,true);
        const float light=std::clamp(strength,0.f,1.f),radial=.065f*light;
        const float angle=(float)identity*2.1f+.3f;
        const float offsetX=std::cos(angle)*(float)size/3.36f*.10f*light,offsetY=std::sin(angle)*(float)size/3.36f*.10f*light;
        juce::Image::BitmapData src(source,juce::Image::BitmapData::readOnly),dst(target,juce::Image::BitmapData::writeOnly);
        const auto channel=[&](int x,int y,int index)->float {
            return x>=0&&y>=0&&x<size&&y<size?(float)src.getPixelPointer(x,y)[index]:0.f;
        };
        const auto sample=[&](float x,float y,int index){
            const int ix=(int)std::floor(x),iy=(int)std::floor(y);const float dx=x-(float)ix,dy=y-(float)iy;
            return (channel(ix,iy,index)*(1-dx)+channel(ix+1,iy,index)*dx)*(1-dy)
                +(channel(ix,iy+1,index)*(1-dx)+channel(ix+1,iy+1,index)*dx)*dy;
        };
        const auto byte=[](float v){return (juce::uint8)std::clamp(v+.5f,0.f,255.f);};
        tide::raster::rows(extent,[&](int first,int last){
            for(int y=first;y<last;++y){auto* out=reinterpret_cast<juce::PixelARGB*>(dst.getLinePointer(y));
                const int sy=y-pad;const float shiftY=((float)sy-((float)size-1)*.5f)*radial+offsetY;
                for(int x=0;x<extent;++x){const int sx=x-pad;const float shiftX=((float)sx-((float)size-1)*.5f)*radial+offsetX;
                    const auto r=byte(sample((float)sx+shiftX,(float)sy+shiftY,juce::PixelARGB::indexR));
                    const auto b=byte(sample((float)sx-shiftX,(float)sy-shiftY,juce::PixelARGB::indexB));
                    const auto g=byte(channel(sx,sy,juce::PixelARGB::indexG)),a=byte(channel(sx,sy,juce::PixelARGB::indexA));
                    // Keep the original coverage. Extra coloured glints stay
                    // translucent instead of leaving opaque black fringes.
                    out[x].setARGB(std::max({a,r,b}),r,g,b);
                }
            }
        });
    }
};
