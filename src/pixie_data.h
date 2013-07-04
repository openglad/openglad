#ifndef _PIXIE_DATA_H__
#define _PIXIE_DATA_H__


class PixieData
{
    public:
    
    unsigned char frames;
    unsigned char w, h;
    unsigned char* data;
    
    PixieData();
    PixieData(unsigned char frames, unsigned char w, unsigned char h, unsigned char* data);
    
    bool valid() const;
    
    void clear();
    void free();
};


#endif
