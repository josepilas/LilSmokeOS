#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace lilsmoke::media {
// Geometry is validated by NativeVideo before mapping. Keep division and
// allocation out of the per-frame hcfb expansion (often 856x480 -> 1280x720).
class OverlayBlit {
public:
    void reset(unsigned width,unsigned height,unsigned outputWidth,unsigned outputHeight) {
        width_=width;outputWidth_=outputWidth;
        columns_.resize(outputWidth);rows_.resize(outputHeight);line_.resize(outputWidth);
        for(unsigned x=0;x<outputWidth;++x)columns_[x]=size_t(x)*width/outputWidth;
        for(unsigned y=0;y<outputHeight;++y)rows_[y]=size_t(y)*height/outputHeight;
    }
    void copy(const uint32_t *source,uint8_t *destination,size_t pitch) {
        unsigned previous=~0u;
        for(unsigned y=0;y<rows_.size();++y) {
            const auto *row=source+size_t(rows_[y])*width_;
            if(width_==outputWidth_)std::memcpy(destination+y*pitch,row,outputWidth_*4);
            else {
                if(rows_[y]!=previous) {
                    for(unsigned x=0;x<outputWidth_;++x)line_[x]=row[columns_[x]];
                    previous=rows_[y];
                }
                // Reuse RAM, not reads from the mapped display, for duplicate rows.
                std::memcpy(destination+y*pitch,line_.data(),outputWidth_*4);
            }
        }
    }
private:
    unsigned width_=0,outputWidth_=0;
    std::vector<unsigned> columns_,rows_;
    std::vector<uint32_t> line_;
};
}
