#include "pixie_data.h"
#include <cstdlib>


PixieData::PixieData()
    : frames(0), w(0), h(0), data(NULL)
{}

PixieData::PixieData(unsigned char frames, unsigned char w, unsigned char h, unsigned char* data)
    : frames(frames), w(w), h(h), data(data)
{}

bool PixieData::valid() const
{
    return (data != NULL && frames != 0 && w != 0 && h != 0);
}

void PixieData::clear()
{
    frames = 0;
    w = 0;
    h = 0;
    data = NULL;
}

void PixieData::free()
{
    frames = 0;
    w = 0;
    h = 0;
    delete[] data;
    data = NULL;
}
