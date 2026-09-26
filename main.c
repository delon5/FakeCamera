/*
 * FakeCamera
 *
 * taiHEN user plugin that fakes the SceCamera API on devices without a camera
 * (PlayStation TV, or a PS Vita with a broken camera) so that titles which
 * expect one keep working, and that can feed a BMP image as the camera picture.
 *
 * Copyright (c) OperationNT414C - MIT license, see LICENSE.md
 */

#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/appmgr.h>
#include <psp2/camera.h>
#include <psp2/motion.h>
#include <taihen.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAKECAMERA_VERSION  "1.3"
#define FAKECAMERA_DIR      "ux0:data/FakeCamera"
#define CONFIG_PATH         FAKECAMERA_DIR "/config.txt"
#define LOG_PATH            FAKECAMERA_DIR "/log.txt"

#define NB_CAM              2       // front and back cameras
#define MAX_IMAGE_SIZE      2048    // largest accepted BMP width or height (pixels)

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static inline float absf(float v) { return (v < 0.f) ? -v : v; }
static inline int absi(int v) { return (v < 0) ? -v : v; }
static inline float clampf(float v, float lo, float hi) { return (v > hi) ? hi : ((v < lo) ? lo : v); }
static inline unsigned int minu(unsigned int a, unsigned int b) { return (a < b) ? a : b; }

#define M_PI_F 3.14159265359f

// Fast atan2 approximation (about 0.005 rad of error), enough for tilt control
static float atan2_approx(float y, float x)
{
    const float ONEQTR_PI = M_PI_F / 4.0f;
    const float THRQTR_PI = 3.0f * M_PI_F / 4.0f;
    float r, angle;
    float abs_y = absf(y) + 1e-10f;
    if (x < 0.0f)
    {
        r = (x + abs_y) / (abs_y - x);
        angle = THRQTR_PI;
    }
    else
    {
        r = (x - abs_y) / (x + abs_y);
        angle = ONEQTR_PI;
    }
    angle += (0.1963f * r * r - 0.9817f) * r;
    return (y < 0.0f) ? -angle : angle;
}

// ---------------------------------------------------------------------------
// Configuration (optional file: ux0:data/FakeCamera/config.txt)
//
//   motion=on          tilt the device (or the DualShock on PS TV) to scroll
//                      an image larger than the camera resolution
//   invert_x=off       invert the horizontal scrolling direction
//   invert_y=off       invert the vertical scrolling direction
//   sensitivity=100    tilt sensitivity in percent (10 to 1000)
//   log=off            append diagnostics to ux0:data/FakeCamera/log.txt
//   image=name.bmp     picture for both cameras (in ux0:data/FakeCamera, or a
//                      full path), tried before the TITLEID/ALL file names
//   front=name.bmp     picture for the front camera only (overrides "image")
//   back=name.bmp      picture for the back camera only (overrides "image")
//   cycle=0            several pictures can be listed (a.bmp,b.bmp,c.bmp): the
//                      next one is used at each camera opening, or every
//                      "cycle" seconds while the camera stays open
//
// The keys before any "*TITLEID" line (or after "*ALL") apply to every title;
// a "*TITLEID" line starts a section applying to that title only, on top of
// the global values.
// ---------------------------------------------------------------------------

#define CONFIG_MAX_SIZE 16384
#define IMAGE_LIST_MAX  256     // "image", "front" and "back" values: names separated by commas
#define IMAGE_NAME_MAX  128

typedef struct {
    int motion;
    int invertX;
    int invertY;
    int sensitivity;
    int log;
    int cycle;
    char image[IMAGE_LIST_MAX];
    char front[IMAGE_LIST_MAX];
    char back[IMAGE_LIST_MAX];
} Config;

static Config config = { 1, 0, 0, 100, 0, 0, "", "", "" };
static char configText[CONFIG_MAX_SIZE];
static char titleid[16] = "";

static void Log(const char* fmt, ...)
{
    if (!config.log)
        return;

    char line[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (len <= 0)
        return;
    if (len >= (int)sizeof(line))
        len = sizeof(line) - 1;

    SceUID fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd < 0)
        return;
    sceIoWrite(fd, line, len);
    sceIoClose(fd);
}

static char* TrimSpaces(char* s)
{
    while (' ' == *s || '\t' == *s)
        s++;
    char* end = s + strlen(s);
    while (end > s && (' ' == end[-1] || '\t' == end[-1]))
        *--end = '\0';
    return s;
}

// Case insensitive string equality
static int KeyEquals(const char* a, const char* b)
{
    for (; '\0' != *a && '\0' != *b; a++, b++)
    {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb)
            return 0;
    }
    return *a == *b;
}

static int ParseBool(const char* v)
{
    return KeyEquals(v, "1") || KeyEquals(v, "on") || KeyEquals(v, "yes") || KeyEquals(v, "true");
}

static void CopyName(char* oName, const char* iValue)
{
    strncpy(oName, iValue, IMAGE_LIST_MAX - 1);
    oName[IMAGE_LIST_MAX - 1] = '\0';
}

// Number of names in a comma separated list
static int ImageListCount(const char* iList)
{
    int count = 0;
    const char* p = iList;
    while ('\0' != *p)
    {
        const char* end = strchr(p, ',');
        size_t len = (NULL != end) ? (size_t)(end - p) : strlen(p);
        while (len > 0 && (' ' == *p || '\t' == *p)) { p++; len--; }
        while (len > 0 && (' ' == p[len - 1] || '\t' == p[len - 1])) len--;
        if (len > 0)
            count++;
        p += (NULL != end) ? (size_t)(end - p) + 1 : strlen(p);
    }
    return count;
}

// Copies the iIndex-th name (modulo the count) of a comma separated list
static int ImageListGet(const char* iList, int iIndex, char* oName, size_t iNameSize)
{
    int count = ImageListCount(iList);
    if (count <= 0)
        return 0;
    int wanted = ((iIndex % count) + count) % count;
    const char* p = iList;
    while ('\0' != *p)
    {
        const char* end = strchr(p, ',');
        size_t len = (NULL != end) ? (size_t)(end - p) : strlen(p);
        const char* start = p;
        while (len > 0 && (' ' == *start || '\t' == *start)) { start++; len--; }
        while (len > 0 && (' ' == start[len - 1] || '\t' == start[len - 1])) len--;
        if (len > 0)
        {
            if (0 == wanted)
            {
                if (len >= iNameSize)
                    len = iNameSize - 1;
                memcpy(oName, start, len);
                oName[len] = '\0';
                return 1;
            }
            wanted--;
        }
        p += (NULL != end) ? (size_t)(end - p) + 1 : strlen(p);
    }
    return 0;
}

static void ApplyConfigValue(const char* key, const char* val)
{
    if (KeyEquals(key, "motion"))
        config.motion = ParseBool(val);
    else if (KeyEquals(key, "invert_x"))
        config.invertX = ParseBool(val);
    else if (KeyEquals(key, "invert_y"))
        config.invertY = ParseBool(val);
    else if (KeyEquals(key, "sensitivity"))
    {
        int s = atoi(val);
        if (s >= 10 && s <= 1000)
            config.sensitivity = s;
    }
    else if (KeyEquals(key, "log"))
        config.log = ParseBool(val);
    else if (KeyEquals(key, "cycle"))
    {
        int c = atoi(val);
        if (c >= 0 && c <= 3600)
            config.cycle = c;
    }
    else if (KeyEquals(key, "image"))
    {
        // both cameras: a later "front" or "back" line still overrides one of them
        CopyName(config.image, val);
        CopyName(config.front, val);
        CopyName(config.back, val);
    }
    else if (KeyEquals(key, "front"))
        CopyName(config.front, val);
    else if (KeyEquals(key, "back"))
        CopyName(config.back, val);
}

// Applies one section of the configuration text: the global one when
// iSection is NULL, otherwise the "*iSection" one
static void ApplyConfigSection(const char* iText, const char* iSection)
{
    int inSection = (NULL == iSection);   // the text starts in the global section
    const char* p = iText;
    while ('\0' != *p)
    {
        const char* end = strchr(p, '\n');
        size_t len = (NULL != end) ? (size_t)(end - p) : strlen(p);
        const char* next = p + len + ((NULL != end) ? 1 : 0);

        char line[IMAGE_LIST_MAX + 64];
        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        p = next;

        char* cut = strchr(line, '#');
        if (NULL != cut)
            *cut = '\0';
        cut = strchr(line, '\r');
        if (NULL != cut)
            *cut = '\0';

        char* content = TrimSpaces(line);
        if ('*' == content[0])
        {
            const char* name = TrimSpaces(content + 1);
            inSection = (NULL == iSection) ? KeyEquals(name, "ALL") : KeyEquals(name, iSection);
            continue;
        }
        if (!inSection)
            continue;

        char* eq = strchr(content, '=');
        if (NULL != eq)
        {
            *eq = '\0';
            ApplyConfigValue(TrimSpaces(content), TrimSpaces(eq + 1));
        }
    }
}

static void LoadConfig(void)
{
    SceUID fd = sceIoOpen(CONFIG_PATH, SCE_O_RDONLY, 0);
    if (fd < 0)
        return;
    int len = sceIoRead(fd, configText, sizeof(configText) - 1);
    sceIoClose(fd);
    if (len <= 0)
        return;
    configText[len] = '\0';

    ApplyConfigSection(configText, NULL);      // global values first
    ApplyConfigSection(configText, titleid);   // then the title's own section
}

// ---------------------------------------------------------------------------
// Motion sensors (optional)
//
// The image scrolls with the tilt reported by the system SceMotion library.
// On a PS Vita those are the internal sensors; on a PS TV they are the ones of
// a DualShock when a motion emulator is running (PSVshell+, ds34motion or
// DSMotion all feed SceMotion). The library is resolved at run time so that
// the plugin still loads, without scrolling, when it is not available.
// ---------------------------------------------------------------------------

#define SCEMOTION_MODULE        "SceDriverUser"
#define SCEMOTION_LIBRARY_NID   0xDC571B3F
#define SCEMOTION_START_NID     0x28034AC9  // sceMotionStartSampling
#define SCEMOTION_STOP_NID      0xAF32CB1D  // sceMotionStopSampling
#define SCEMOTION_GETSTATE_NID  0xBDB32767  // sceMotionGetState

typedef int (*MotionStartFunc)(void);
typedef int (*MotionStopFunc)(void);
typedef int (*MotionGetStateFunc)(SceMotionState* state);

static MotionStartFunc motionStart = NULL;
static MotionStopFunc motionStop = NULL;
static MotionGetStateFunc motionGetState = NULL;
static int motionResolved = 0;      // 0: not tried yet, 1: available, -1: unavailable
static int motionSampling = 0;      // sampling is currently running
static int motionStartedByUs = 0;   // this plugin started it, so it stops it on unload

static int ResolveMotion(void)
{
    if (0 != motionResolved)
        return motionResolved > 0;

    uintptr_t fStart = 0, fStop = 0, fGetState = 0;
    if (taiGetModuleExportFunc(SCEMOTION_MODULE, SCEMOTION_LIBRARY_NID, SCEMOTION_START_NID, &fStart) >= 0
     && taiGetModuleExportFunc(SCEMOTION_MODULE, SCEMOTION_LIBRARY_NID, SCEMOTION_STOP_NID, &fStop) >= 0
     && taiGetModuleExportFunc(SCEMOTION_MODULE, SCEMOTION_LIBRARY_NID, SCEMOTION_GETSTATE_NID, &fGetState) >= 0)
    {
        motionStart = (MotionStartFunc)fStart;
        motionStop = (MotionStopFunc)fStop;
        motionGetState = (MotionGetStateFunc)fGetState;
        motionResolved = 1;
    }
    else
    {
        motionResolved = -1;
    }
    Log("SceMotion %s\n", (motionResolved > 0) ? "found" : "not found: no image scrolling");
    return motionResolved > 0;
}

static void MotionStartSampling(void)
{
    if (!config.motion || motionSampling || !ResolveMotion())
        return;

    int res = motionStart();
    if (res >= 0)
    {
        motionSampling = 1;
        motionStartedByUs = 1;
    }
    else if (SCE_MOTION_ERROR_ALREADY_SAMPLING == (unsigned int)res)
    {
        motionSampling = 1;
    }
    Log("sceMotionStartSampling: 0x%08X\n", res);
}

static void MotionStopSampling(void)
{
    if (motionSampling && motionStartedByUs && NULL != motionStop)
        motionStop();
    motionSampling = 0;
    motionStartedByUs = 0;
}

// Fills the scrolling rates (from -1 to 1) and returns 1 when tilt data is
// available. Without any sensor the image simply stays centered.
static int MotionGetTilt(float* oWidthRate, float* oHeightRate)
{
    *oWidthRate = 0.f;
    *oHeightRate = 0.f;
    if (!motionSampling)
        return 0;

    SceMotionState state;
    memset(&state, 0, sizeof(state));
    int res = motionGetState(&state);
    if (res < 0)
    {
        // The title may have stopped the sampling itself: retry at the next camera open
        if (SCE_MOTION_ERROR_NOT_SAMPLING == (unsigned int)res)
            motionSampling = 0;
        return 0;
    }

    // Gravity as seen by the device: X to the right, Y to the top, Z through the screen
    float x = state.acceleration.x;
    float y = state.acceleration.y;
    float z = state.acceleration.z;
    if (x*x + y*y + z*z < 0.25f)
        return 0;   // no sensor data (PS TV without a motion emulator)

    float scale = (float)config.sensitivity / 100.f;
    float roll = atan2_approx(x, absf(z)) * scale;   // side tilt  -> horizontal scrolling
    float pitch = atan2_approx(y, absf(z)) * scale;  // front tilt -> vertical scrolling
    *oWidthRate = clampf(config.invertX ? -roll : roll, -1.f, 1.f);
    *oHeightRate = clampf(config.invertY ? -pitch : pitch, -1.f, 1.f);
    return 1;
}

// ---------------------------------------------------------------------------
// Camera buffers
// ---------------------------------------------------------------------------

// Alternative layout of SceCameraRead used by some titles
typedef struct SceCameraRead2 {
    SceSize size;   //!< sizeof(SceCameraRead2)
    int mode;
    int pad;
    int status;
    uint64_t frame;
    uint64_t timestamp;
    int unknown0;
    int unknown1;
    int unknown2;
    void* unknownNullCheck;
    SceSize sizeIBase;
    SceSize sizeUBase;
    SceSize sizeVBase;
    void *pIBase;
    void *pUBase;
    void *pVBase;
} SceCameraRead2;

typedef struct {
    SceUID blockIDs[3];
    void* blocksData[3];
    uint16_t texelBits[3];
    uint16_t rowStride[3];
    uint16_t rowDepend[3];
    uint16_t widthAlign;
    uint16_t heightAlign;
    uint16_t imageWidth;
    uint16_t imageHeight;
    int ready;              // -1: no image (or failed to load), 0: loading, 1: ready
} ImageBuffers;

#define IMAGE_BUFFERS_INIT { {-1, -1, -1}, {NULL, NULL, NULL}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 0, 0, 0, 0, -1 }

static unsigned int alignSizeForMemBlock(unsigned int size)
{
    if (size & 0xFFF)
        size += ((~size) & 0xFFF) + 1;  // 4kB pages
    return size;
}

static unsigned int bitSize(unsigned int size, unsigned int bits)
{
    return (size * bits) / 8;
}

static void FreeImageBuffers(ImageBuffers* ioBuffers)
{
    for (int i = 0; i < 3; i++)
    {
        if (ioBuffers->blockIDs[i] >= 0)
            sceKernelFreeMemBlock(ioBuffers->blockIDs[i]);
        ioBuffers->blockIDs[i] = -1;
        ioBuffers->blocksData[i] = NULL;
    }
    ioBuffers->ready = -1;
}

// ---------------------------------------------------------------------------
// Camera formats: conversion of one ABGR pixel into the camera buffers
// ---------------------------------------------------------------------------

typedef void (*BufferWriteFunc)(void* iFuncData, ImageBuffers* oBuffers, unsigned int iGlobalPos, uint16_t iWidthPos, uint16_t iHeightPos, unsigned int iColor);

static void ABGRWrite(void* iFuncData, ImageBuffers* oBuffers, unsigned int iGlobalPos, uint16_t iWidthPos, uint16_t iHeightPos, unsigned int iColor)
{
    ((unsigned int*)oBuffers->blocksData[0])[iGlobalPos] = iColor;
}

static void ARGBWrite(void* iFuncData, ImageBuffers* oBuffers, unsigned int iGlobalPos, uint16_t iWidthPos, uint16_t iHeightPos, unsigned int iColor)
{
    ((unsigned int*)oBuffers->blocksData[0])[iGlobalPos] = (iColor&0xFF00FF00) | (iColor&0xFF)<<16 | (iColor&0xFF0000)>>16;
}

static const float convMat[3][3] = { {0.299f, 0.587f, 0.114f}, {-0.14317f, -0.28886f, 0.436f}, {0.615f, -0.51499f, -0.10001f} };

static void RGBToYUV(const unsigned int* iColors, int iCount, float* Y, float* Cb, float* Cr)
{
    for (int i = 0; i < iCount; i++)
    {
        unsigned int color = iColors[i];
        float nR = (float)(color&0xFF);
        float nG = (float)((color&0xFF00)>>8);
        float nB = (float)((color&0xFF0000)>>16);

        Y[i] = convMat[0][0] * nR + convMat[0][1] * nG + convMat[0][2] * nB;
        Cb[i] = convMat[1][0] * nR + convMat[1][1] * nG + convMat[1][2] * nB + 128.f;
        Cr[i] = convMat[2][0] * nR + convMat[2][1] * nG + convMat[2][2] * nB + 128.f;
    }
}

typedef struct {
    unsigned int globalPos;
    unsigned int colors[2];
} YUV422Data;

static void YUV422PackedWrite(void* iFuncData, ImageBuffers* oBuffers, unsigned int iGlobalPos, uint16_t iWidthPos, uint16_t iHeightPos, unsigned int iColor)
{
    YUV422Data* data = (YUV422Data*)iFuncData;
    if (0 == iWidthPos%2)
    {
        data->globalPos = iGlobalPos;
        data->colors[0] = iColor;
        return;
    }
    data->colors[1] = iColor;

    float Y[2], Cb[2], Cr[2];
    RGBToYUV(data->colors, 2, Y, Cb, Cr);

    ((unsigned short*)oBuffers->blocksData[0])[data->globalPos] = (((unsigned char)Y[0])<<8) | (unsigned char)((Cb[0]+Cb[1])/2.f);
    ((unsigned short*)oBuffers->blocksData[0])[iGlobalPos] = (((unsigned char)Y[1])<<8) | (unsigned char)((Cr[0]+Cr[1])/2.f);
}

static void YUV422PlaneWrite(void* iFuncData, ImageBuffers* oBuffers, unsigned int iGlobalPos, uint16_t iWidthPos, uint16_t iHeightPos, unsigned int iColor)
{
    YUV422Data* data = (YUV422Data*)iFuncData;
    if (0 == iWidthPos%2)
    {
        data->globalPos = iGlobalPos;
        data->colors[0] = iColor;
        return;
    }
    data->colors[1] = iColor;

    float Y[2], Cb[2], Cr[2];
    RGBToYUV(data->colors, 2, Y, Cb, Cr);

    ((unsigned char*)oBuffers->blocksData[0])[data->globalPos] = (unsigned char)Y[0];
    ((unsigned char*)oBuffers->blocksData[0])[iGlobalPos] = (unsigned char)Y[1];
    ((unsigned char*)oBuffers->blocksData[1])[data->globalPos/2] = (unsigned char)((Cb[0]+Cb[1])/2.f);
    ((unsigned char*)oBuffers->blocksData[2])[data->globalPos/2] = (unsigned char)((Cr[0]+Cr[1])/2.f);
}

typedef struct {
    unsigned int globalPos[2];
    unsigned int colors[4];
} YUV420Data;

static void YUV420PlaneWrite(void* iFuncData, ImageBuffers* oBuffers, unsigned int iGlobalPos, uint16_t iWidthPos, uint16_t iHeightPos, unsigned int iColor)
{
    YUV420Data* data = (YUV420Data*)iFuncData;
    unsigned int pixelPosInBlock = iWidthPos%2+((iHeightPos%2)<<1);
    if (0 == iWidthPos%2)
        data->globalPos[iHeightPos%2] = iGlobalPos;
    data->colors[pixelPosInBlock] = iColor;
    if (pixelPosInBlock < 3)
        return;

    float Y[4], Cb[4], Cr[4];
    RGBToYUV(data->colors, 4, Y, Cb, Cr);

    ((unsigned short*)oBuffers->blocksData[0])[data->globalPos[0]/2] = (unsigned char)Y[0] | (((unsigned char)Y[1])<<8);
    ((unsigned short*)oBuffers->blocksData[0])[data->globalPos[1]/2] = (unsigned char)Y[2] | (((unsigned char)Y[3])<<8);
    ((unsigned char*)oBuffers->blocksData[1])[(data->globalPos[1]+(iWidthPos-1))/4] = (unsigned char)((Cb[0]+Cb[1]+Cb[2]+Cb[3])/4.f);
    ((unsigned char*)oBuffers->blocksData[2])[(data->globalPos[1]+(iWidthPos-1))/4] = (unsigned char)((Cr[0]+Cr[1]+Cr[2]+Cr[3])/4.f);
}

// ---------------------------------------------------------------------------
// BMP reading (inspired by libvita2d)
// https://github.com/xerpi/libvita2d/blob/master/libvita2d/source/vita2d_image_bmp.c
// ---------------------------------------------------------------------------

#define BMP_SIGNATURE   (0x4D42)
#define BI_RGB          0
#define BI_BITFIELDS    3

typedef struct {
    unsigned short  bfType;
    unsigned int    bfSize;
    unsigned short  bfReserved1;
    unsigned short  bfReserved2;
    unsigned int    bfOffBits;
} __attribute__((packed)) BITMAPFILEHEADER;

typedef struct {
    unsigned int    biSize;
    int             biWidth;
    int             biHeight;
    unsigned short  biPlanes;
    unsigned short  biBitCount;
    unsigned int    biCompression;
    unsigned int    biSizeImage;
    int             biXPelsPerMeter;
    int             biYPelsPerMeter;
    unsigned int    biClrUsed;
    unsigned int    biClrImportant;
} __attribute__((packed)) BITMAPINFOHEADER;

// Position of each channel in a 16 or 32 bits pixel
typedef struct {
    unsigned int mask;
    int shift;
    int bits;
} ChannelMask;

typedef struct {
    unsigned short bitCount;
    ChannelMask r, g, b, a;
} PixelLayout;

static ChannelMask MakeChannelMask(unsigned int mask)
{
    ChannelMask c = { mask, 0, 0 };
    if (0 == mask)
        return c;
    while (0 == ((mask >> c.shift) & 1))
        c.shift++;
    for (unsigned int m = mask >> c.shift; 0 != (m & 1); m >>= 1)
        c.bits++;
    return c;
}

// Extracts one channel and scales it to 8 bits
static inline unsigned int ChannelTo8(unsigned int color, const ChannelMask* c)
{
    if (0 == c->bits)
        return 0;
    unsigned int v = (color & c->mask) >> c->shift;
    if (c->bits >= 8)
        return v >> (c->bits - 8);
    return (v * 255) / ((1u << c->bits) - 1);
}

// Returns the pixel as ABGR (R in the low byte)
static unsigned int ReadColor(const void* buffer, const PixelLayout* layout, unsigned int row_stride, int col, int row)
{
    const unsigned char* address = (const unsigned char*)buffer + row*row_stride;
    if (24 == layout->bitCount)     // BGR888
    {
        address += col*3;
        return address[2] | (address[1]<<8) | (address[0]<<16) | (0xFFu<<24);
    }

    unsigned int color;
    if (32 == layout->bitCount)
        color = *(const unsigned int *)(address + col*4);
    else                            // 16 bits
        color = *(const unsigned short *)(address + col*2);

    unsigned int r = ChannelTo8(color, &layout->r);
    unsigned int g = ChannelTo8(color, &layout->g);
    unsigned int b = ChannelTo8(color, &layout->b);
    unsigned int a = (0 != layout->a.bits) ? ChannelTo8(color, &layout->a) : 0xFF;
    return r | (g<<8) | (b<<16) | (a<<24);
}

// Reads the pixel rows and converts them into the camera buffers. BMP rows are
// stored bottom-up unless the height is negative (top-down): a top-down file is
// read backwards so that the converters always see bottom-up rows.
static int LoadBMPGeneric(const BITMAPFILEHEADER *bmp_fh, const BITMAPINFOHEADER *bmp_ih, const PixelLayout* iLayout,
                          SceUID iFile, ImageBuffers* oBuffers, BufferWriteFunc iWriteFunc, void* iFuncData)
{
    int topDown = (bmp_ih->biHeight < 0);
    unsigned int row_stride = bmp_ih->biWidth * (iLayout->bitCount/8);
    if (row_stride%4 != 0) {
        row_stride += 4-(row_stride%4);
    }
    unsigned int block_stride = row_stride * oBuffers->heightAlign;

    unsigned int size = alignSizeForMemBlock(block_stride);
    SceUID bufferID = sceKernelAllocMemBlock("bitmap_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, NULL);
    void *buffer = NULL;
    if (bufferID < 0 || sceKernelGetMemBlockBase(bufferID, (void **)&buffer) < 0 || NULL == buffer) {
        if (bufferID >= 0)
            sceKernelFreeMemBlock(bufferID);
        return -1;
    }

    unsigned int alignedWidth = oBuffers->imageWidth;
    unsigned int alignedHeight = oBuffers->imageHeight;
    unsigned int blocksCount = alignedHeight / oBuffers->heightAlign;

    sceIoLseek(iFile, bmp_fh->bfOffBits, SCE_SEEK_SET);

    unsigned int b, i, j, x, y;
    for (b = 0; b < blocksCount; b++)
    {
        if (topDown)
        {
            unsigned int fileBlock = blocksCount - 1 - b;
            sceIoLseek(iFile, (SceOff)bmp_fh->bfOffBits + (SceOff)fileBlock * block_stride, SCE_SEEK_SET);
        }
        if (sceIoRead(iFile, buffer, block_stride) != (int)block_stride)
        {
            sceKernelFreeMemBlock(bufferID);
            return -1;
        }

        if (oBuffers->heightAlign > 1)
        {
            for (i = 0; i < alignedWidth; i+=oBuffers->widthAlign)
            {
                for (j = 0; j < oBuffers->heightAlign ; j++)
                {
                    y = b*oBuffers->heightAlign + j;
                    unsigned int fileRow = topDown ? (oBuffers->heightAlign - 1 - j) : j;

                    for (x = i; x < i+oBuffers->widthAlign; x++)
                    {
                        unsigned int globalPos = (alignedHeight - 1 - y)*alignedWidth + x;
                        unsigned int imgColor = ReadColor(buffer, iLayout, row_stride, x, fileRow);
                        iWriteFunc(iFuncData, oBuffers, globalPos, x, y, imgColor);
                    }
                }
            }
        }
        else
        {
            y = b;
            unsigned int globalPos = (alignedHeight - 1 - y)*alignedWidth;

            for (x = 0; x < alignedWidth; x++)
            {
                unsigned int imgColor = ReadColor(buffer, iLayout, row_stride, x, 0);
                iWriteFunc(iFuncData, oBuffers, globalPos, x, y, imgColor);
                globalPos++;
            }
        }
    }

    sceKernelFreeMemBlock(bufferID);
    return 1;
}

static int LoadBMPFile(SceUID iFile, SceCameraFormat iFormat, const char* iMemName, ImageBuffers* oBuffers)
{
    BITMAPFILEHEADER bmp_fh;
    BITMAPINFOHEADER bmp_ih;
    if (sceIoRead(iFile, (void *)&bmp_fh, sizeof(BITMAPFILEHEADER)) != sizeof(BITMAPFILEHEADER)
     || sceIoRead(iFile, (void *)&bmp_ih, sizeof(BITMAPINFOHEADER)) != sizeof(BITMAPINFOHEADER))
    {
        Log("BMP: file too short\n");
        return -1;
    }

    int width = bmp_ih.biWidth;
    int height = absi(bmp_ih.biHeight);
    if (bmp_fh.bfType != BMP_SIGNATURE || bmp_ih.biSize < sizeof(BITMAPINFOHEADER)
     || bmp_fh.bfOffBits < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)
     || width <= 0 || height <= 0 || width > MAX_IMAGE_SIZE || height > MAX_IMAGE_SIZE)
    {
        Log("BMP: not a supported bitmap (%dx%d)\n", width, bmp_ih.biHeight);
        return -1;
    }
    if ((bmp_ih.biCompression != BI_RGB && bmp_ih.biCompression != BI_BITFIELDS)
     || (bmp_ih.biBitCount != 16 && bmp_ih.biBitCount != 24 && bmp_ih.biBitCount != 32)
     || (bmp_ih.biCompression == BI_BITFIELDS && bmp_ih.biBitCount == 24))
    {
        Log("BMP: unsupported %d bits compression %u (use an uncompressed 16, 24 or 32 bits BMP)\n", bmp_ih.biBitCount, bmp_ih.biCompression);
        return -1;
    }

    // Channel layout: fixed for BI_RGB, given by masks (following the 40 bytes
    // header, or embedded in the larger V3/V4/V5 headers) for BI_BITFIELDS
    PixelLayout layout;
    unsigned int masks[4] = { 0, 0, 0, 0 };
    if (BI_BITFIELDS == bmp_ih.biCompression)
    {
        int maskBytes = (bmp_ih.biSize >= 56) ? 16 : 12;
        if (sceIoRead(iFile, masks, maskBytes) != maskBytes)
        {
            Log("BMP: file too short\n");
            return -1;
        }
    }
    else if (16 == bmp_ih.biBitCount)
    {
        masks[0] = 0x7C00; masks[1] = 0x03E0; masks[2] = 0x001F;     // X1R5G5B5
    }
    else if (32 == bmp_ih.biBitCount)
    {
        masks[0] = 0x00FF0000; masks[1] = 0x0000FF00; masks[2] = 0x000000FF; // X8R8G8B8
    }
    layout.bitCount = bmp_ih.biBitCount;
    layout.r = MakeChannelMask(masks[0]);
    layout.g = MakeChannelMask(masks[1]);
    layout.b = MakeChannelMask(masks[2]);
    layout.a = MakeChannelMask(masks[3]);
    if (24 != layout.bitCount)
    {
        unsigned int allowed = (16 == layout.bitCount) ? 0xFFFFu : 0xFFFFFFFFu;
        if (0 == layout.r.bits || 0 == layout.g.bits || 0 == layout.b.bits
         || 0 != ((masks[0] | masks[1] | masks[2] | masks[3]) & ~allowed))
        {
            Log("BMP: unsupported channel masks %08X %08X %08X %08X\n", masks[0], masks[1], masks[2], masks[3]);
            return -1;
        }
    }

    BufferWriteFunc writeFunc = NULL;
    char funcData[24];

    oBuffers->rowStride[0] = 0;
    oBuffers->rowStride[1] = 0;
    oBuffers->rowStride[2] = 0;
    oBuffers->rowDepend[0] = 1;
    oBuffers->rowDepend[1] = 1;
    oBuffers->rowDepend[2] = 1;
    oBuffers->widthAlign = 1;
    oBuffers->heightAlign = 1;

    switch (iFormat)
    {
    case SCE_CAMERA_FORMAT_ARGB:
        oBuffers->texelBits[0] = 32;
        writeFunc = &ARGBWrite;
        break;
    case SCE_CAMERA_FORMAT_ABGR:
        oBuffers->texelBits[0] = 32;
        writeFunc = &ABGRWrite;
        break;
    case SCE_CAMERA_FORMAT_YUV422_PACKED:
        oBuffers->texelBits[0] = 16;
        oBuffers->widthAlign = 2;
        writeFunc = &YUV422PackedWrite;
        break;
    case SCE_CAMERA_FORMAT_YUV422_PLANE:
        oBuffers->texelBits[0] = 8;
        oBuffers->texelBits[1] = 4;
        oBuffers->texelBits[2] = 4;
        oBuffers->widthAlign = 2;
        writeFunc = &YUV422PlaneWrite;
        break;
    case SCE_CAMERA_FORMAT_YUV420_PLANE:
        oBuffers->texelBits[0] = 8;
        oBuffers->texelBits[1] = 2;
        oBuffers->rowDepend[1] = 2;
        oBuffers->texelBits[2] = 2;
        oBuffers->rowDepend[2] = 2;
        oBuffers->widthAlign = 2;
        oBuffers->heightAlign = 2;
        writeFunc = &YUV420PlaneWrite;
        break;
    default:
        Log("BMP: camera format %d is not supported\n", iFormat);
        return -1;
    }

    oBuffers->imageWidth = (width/oBuffers->widthAlign)*oBuffers->widthAlign;
    oBuffers->imageHeight = (height/oBuffers->heightAlign)*oBuffers->heightAlign;
    if (0 == oBuffers->imageWidth || 0 == oBuffers->imageHeight)
        return -1;

    char memname[48];
    for (int i = 0; i < 3; i++)
    {
        oBuffers->rowStride[i] = (oBuffers->imageWidth*oBuffers->texelBits[i]*oBuffers->rowDepend[i])/8;
        if (oBuffers->rowStride[i] > 0)
        {
            snprintf(memname, sizeof(memname), "%s_%d", iMemName, i);
            unsigned int size = alignSizeForMemBlock(oBuffers->rowStride[i]*oBuffers->imageHeight/oBuffers->rowDepend[i]);
            oBuffers->blockIDs[i] = sceKernelAllocMemBlock(memname, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, NULL);
            oBuffers->blocksData[i] = NULL;
            if (oBuffers->blockIDs[i] >= 0)
                sceKernelGetMemBlockBase(oBuffers->blockIDs[i], (void **)&oBuffers->blocksData[i]);

            if (NULL == oBuffers->blocksData[i])
            {
                Log("BMP: cannot allocate %u bytes for plane %d\n", size, i);
                FreeImageBuffers(oBuffers);
                return -1;
            }
        }
    }

    memset(funcData, 0, sizeof(funcData));
    int res = LoadBMPGeneric(&bmp_fh, &bmp_ih, &layout, iFile, oBuffers, writeFunc, funcData);
    if (res < 0)
        FreeImageBuffers(oBuffers);
    return res;
}

// ---------------------------------------------------------------------------
// Camera state
// ---------------------------------------------------------------------------

static const char* const camName[NB_CAM] = { "Front", "Back" };

static uint16_t width[NB_CAM] = {0, 0};
static uint16_t height[NB_CAM] = {0, 0};

static void* IBufferOnOpen[NB_CAM] = {NULL, NULL};
static void* UBufferOnOpen[NB_CAM] = {NULL, NULL};
static void* VBufferOnOpen[NB_CAM] = {NULL, NULL};

static ImageBuffers imageBuffers[NB_CAM] = { IMAGE_BUFFERS_INIT, IMAGE_BUFFERS_INIT };
static SceCameraFormat imageFormat[NB_CAM] = {0, 0};
static SceCameraFormat openFormat[NB_CAM] = {0, 0};
static char imageName[NB_CAM][IMAGE_NAME_MAX] = { "", "" };  // configured name currently loaded
static int imageIndex[NB_CAM] = {0, 0};                      // position in the configured list
static int imageBuf_opened[NB_CAM] = {0, 0};                 // camera openings so far
static uint64_t imageSwitchTime[NB_CAM] = {0, 0};

static int prevWidthOffset[NB_CAM] = {-1, -1};
static int prevHeightOffset[NB_CAM] = {-1, -1};
static void* prevBuffers[NB_CAM][3] = { {NULL, NULL, NULL}, {NULL, NULL, NULL} };

static int cameraOpened[NB_CAM] = {0, 0};
static int cameraActive[NB_CAM] = {0, 0};
static uint16_t framerate[NB_CAM] = {0, 0};
static uint64_t prevFrame[NB_CAM] = {0, 0};
static uint64_t initTimeStamp[NB_CAM] = {0, 0};
static uint64_t prevTimeStamp[NB_CAM] = {0, 0};

static void BuildImagePath(char* oPath, size_t iPathSize, const char* iName)
{
    if (NULL != strchr(iName, ':'))
        snprintf(oPath, iPathSize, "%s", iName);           // full path
    else
        snprintf(oPath, iPathSize, FAKECAMERA_DIR "/%s", iName);
}

// Looks for an image in this order: the "front"/"back" then "image" names of
// the configuration, then TITLEID_Front.bmp (or _Back), TITLEID.bmp,
// ALL_Front.bmp (or _Back) and ALL.bmp
static const char* ImageList(int devnum)
{
    return (1 == devnum) ? config.back : config.front;
}

static SceUID OpenImageFile(int devnum, char* oPath, size_t iPathSize)
{
    SceUID fd = -1;
    char name[IMAGE_NAME_MAX];
    if (ImageListGet(ImageList(devnum), imageIndex[devnum], name, sizeof(name)))
    {
        BuildImagePath(oPath, iPathSize, name);
        fd = sceIoOpen(oPath, SCE_O_RDONLY, 0);
        if (fd >= 0)
            return fd;
        Log("Configured image %s cannot be opened (0x%08X)\n", oPath, fd);
    }

    for (int i = 0; i < 4 && fd < 0; i++)
    {
        switch (i)
        {
        case 0: snprintf(oPath, iPathSize, FAKECAMERA_DIR "/%s_%s.bmp", titleid, camName[devnum]); break;
        case 1: snprintf(oPath, iPathSize, FAKECAMERA_DIR "/%s.bmp", titleid); break;
        case 2: snprintf(oPath, iPathSize, FAKECAMERA_DIR "/ALL_%s.bmp", camName[devnum]); break;
        default: snprintf(oPath, iPathSize, FAKECAMERA_DIR "/ALL.bmp"); break;
        }
        fd = sceIoOpen(oPath, SCE_O_RDONLY, 0);
    }
    return fd;
}

static void LoadCameraImage(int devnum, SceCameraFormat iFormat)
{
    ImageBuffers* imageBuf = &imageBuffers[devnum];
    char name[IMAGE_NAME_MAX] = "";
    ImageListGet(ImageList(devnum), imageIndex[devnum], name, sizeof(name));
    if (imageBuf->ready > 0 && imageFormat[devnum] == iFormat && 0 == strcmp(name, imageName[devnum]))
        return; // already loaded for this format and name

    FreeImageBuffers(imageBuf);
    imageBuf->ready = 0;
    imageFormat[devnum] = SCE_CAMERA_FORMAT_INVALID;
    snprintf(imageName[devnum], IMAGE_NAME_MAX, "%s", name);
    prevWidthOffset[devnum] = -1;   // the camera buffers must be filled again
    prevHeightOffset[devnum] = -1;

    char pathname[256];
    SceUID fd = OpenImageFile(devnum, pathname, sizeof(pathname));
    if (fd < 0)
    {
        Log("No image for %s camera in " FAKECAMERA_DIR " (0x%08X)\n", camName[devnum], fd);
        imageBuf->ready = -1;
        return;
    }

    char memname[32];
    snprintf(memname, sizeof(memname), "%s_%s", titleid, camName[devnum]);
    if (LoadBMPFile(fd, iFormat, memname, imageBuf) >= 0)
    {
        imageFormat[devnum] = iFormat;
        imageBuf->ready = 1;
        Log("Loaded %s (%ux%u) for the %s camera, format %d\n", pathname, imageBuf->imageWidth, imageBuf->imageHeight, camName[devnum], iFormat);
    }
    else
    {
        imageBuf->ready = -1;
        Log("Failed to load %s\n", pathname);
    }
    sceIoClose(fd);
}

// ---------------------------------------------------------------------------
// SceCamera hooks: the real function is always called first, the fake answer
// is only used when it fails (which means there is no camera)
// ---------------------------------------------------------------------------

static tai_hook_ref_t ref_sceCameraOpen;
static int hook_sceCameraOpen(int devnum, SceCameraInfo *pInfo)
{
    int res = TAI_CONTINUE(int, ref_sceCameraOpen, devnum, pInfo);

    if ((unsigned int)devnum < NB_CAM && NULL != pInfo && !cameraOpened[devnum])
    {
        cameraOpened[devnum] = 1;
        framerate[devnum] = pInfo->framerate;

        if (res < 0 && pInfo->resolution > SCE_CAMERA_RESOLUTION_0_0 && pInfo->resolution <= SCE_CAMERA_RESOLUTION_640_360)
        {
            if (pInfo->resolution < SCE_CAMERA_RESOLUTION_352_288)
            {
                pInfo->width = (640 >> (pInfo->resolution-1));
                pInfo->height = (480 >> (pInfo->resolution-1));
            }
            else if (pInfo->resolution < SCE_CAMERA_RESOLUTION_480_272)
            {
                pInfo->width = (352 >> (pInfo->resolution-4));
                pInfo->height = (288 >> (pInfo->resolution-4));
            }
            else if (SCE_CAMERA_RESOLUTION_480_272 == pInfo->resolution)
            {
                pInfo->width = 480;
                pInfo->height = 272;
            }
            else if (SCE_CAMERA_RESOLUTION_640_360 == pInfo->resolution)
            {
                pInfo->width = 640;
                pInfo->height = 360;
            }

            width[devnum] = pInfo->width;
            height[devnum] = pInfo->height;
            if (0 == pInfo->buffer)
            {
                IBufferOnOpen[devnum] = pInfo->pIBase;
                UBufferOnOpen[devnum] = pInfo->pUBase;
                VBufferOnOpen[devnum] = pInfo->pVBase;
            }

            Log("Camera %s opened: %ux%u, format %d, framerate %u\n", camName[devnum], pInfo->width, pInfo->height, pInfo->format, pInfo->framerate);
            openFormat[devnum] = pInfo->format;
            if (0 == config.cycle && imageBuf_opened[devnum]++ > 0)
                imageIndex[devnum]++;   // next picture of the list at each opening
            imageSwitchTime[devnum] = sceKernelGetProcessTimeWide();
            LoadCameraImage(devnum, pInfo->format);
            if (imageBuffers[devnum].ready > 0)
                MotionStartSampling();  // (re)start the tilt sampling for each opening

            res = 0;
        }
    }

    return res;
}

static tai_hook_ref_t ref_sceCameraClose;
static int hook_sceCameraClose(int devnum)
{
    int res = TAI_CONTINUE(int, ref_sceCameraClose, devnum);

    if ((unsigned int)devnum < NB_CAM)
    {
        cameraOpened[devnum] = 0;
        width[devnum] = 0;
        height[devnum] = 0;
        IBufferOnOpen[devnum] = NULL;
        UBufferOnOpen[devnum] = NULL;
        VBufferOnOpen[devnum] = NULL;

        if (res < 0) res = 0;
    }

    return res;
}

static tai_hook_ref_t ref_sceCameraStart;
static int hook_sceCameraStart(int devnum)
{
    int res = TAI_CONTINUE(int, ref_sceCameraStart, devnum);

    if ((unsigned int)devnum < NB_CAM && cameraOpened[devnum])
    {
        cameraActive[devnum] = 1;
        initTimeStamp[devnum] = sceKernelGetProcessTimeWide();
        prevTimeStamp[devnum] = initTimeStamp[devnum];
        if (res < 0) res = 0;
    }

    return res;
}

static tai_hook_ref_t ref_sceCameraStop;
static int hook_sceCameraStop(int devnum)
{
    int res = TAI_CONTINUE(int, ref_sceCameraStop, devnum);

    if ((unsigned int)devnum < NB_CAM)
    {
        prevFrame[devnum] = 0;
        prevWidthOffset[devnum] = -1;
        prevHeightOffset[devnum] = -1;
        prevBuffers[devnum][0] = NULL;
        prevBuffers[devnum][1] = NULL;
        prevBuffers[devnum][2] = NULL;

        cameraActive[devnum] = 0;
        if (res < 0) res = 0;
    }

    return res;
}

// Copies the image into the camera buffers, scrolled by the tilt when the
// image is larger than the camera resolution (centered otherwise)
static void FillCameraBuffers(int devnum, char* buffers[3], int buffersChanged)
{
    ImageBuffers* imageBuf = &imageBuffers[devnum];

    float widthOffsetRate = 0.f;
    float heightOffsetRate = 0.f;
    MotionGetTilt(&widthOffsetRate, &heightOffsetRate);

    unsigned int imgRowTexels = imageBuf->imageWidth;
    unsigned int imgRowCount = imageBuf->imageHeight;
    unsigned int bufRowTexels = width[devnum];
    unsigned int bufRowCount = height[devnum];

    unsigned int minRowTexels = minu(imgRowTexels, bufRowTexels);
    unsigned int minRowCount = minu(imgRowCount, bufRowCount);

    int widthLeft = imgRowTexels - bufRowTexels;
    int heightLeft = imgRowCount - bufRowCount;

    unsigned int widthOffset = (unsigned int)((1.f + widthOffsetRate) * (float)absi(widthLeft) / 2.f);
    unsigned int heightOffset = (unsigned int)((1.f + heightOffsetRate) * (float)absi(heightLeft) / 2.f);
    widthOffset = (widthOffset/imageBuf->widthAlign)*imageBuf->widthAlign;
    heightOffset = (heightOffset/imageBuf->heightAlign)*imageBuf->heightAlign;

    unsigned int bufWidthOffset = 0;
    unsigned int imgWidthOffset = 0;
    if (widthLeft > 0)
        imgWidthOffset = widthOffset;
    else
        bufWidthOffset = widthOffset;

    unsigned int bufHeightOffset = 0;
    unsigned int imgHeightOffset = 0;
    if (heightLeft > 0)
        imgHeightOffset = heightOffset;
    else
        bufHeightOffset = heightOffset;

    if (prevWidthOffset[devnum] == (int)widthOffset && prevHeightOffset[devnum] == (int)heightOffset && !buffersChanged)
        return; // the buffers already hold this view

    prevWidthOffset[devnum] = widthOffset;
    prevHeightOffset[devnum] = heightOffset;
    prevBuffers[devnum][0] = buffers[0];
    prevBuffers[devnum][1] = buffers[1];
    prevBuffers[devnum][2] = buffers[2];

    for (int i = 0; i < 3; i++)
    {
        char* image = (imageBuf->blockIDs[i] >= 0) ? imageBuf->blocksData[i] : NULL;
        if (NULL != buffers[i] && NULL != image)
        {
            unsigned int rowDepend = imageBuf->rowDepend[i];
            unsigned int bufOffset = bufWidthOffset + bufHeightOffset*bufRowTexels/rowDepend;
            unsigned int imgOffset = imgWidthOffset + imgHeightOffset*imgRowTexels/rowDepend;
            unsigned int texelDependBits = imageBuf->texelBits[i]*rowDepend;

            for (unsigned int row = 0; row < bufHeightOffset/rowDepend; row++)
                memset(buffers[i]+bitSize(row*bufRowTexels,texelDependBits), 0, bitSize(bufRowTexels,texelDependBits));

            for (unsigned int row = 0 ; row < minRowCount/rowDepend ; row++)
                memcpy(buffers[i]+bitSize(row*bufRowTexels+bufOffset,texelDependBits), image+bitSize(row*imgRowTexels+imgOffset,texelDependBits), bitSize(minRowTexels,texelDependBits));

            if (imgRowTexels < bufRowTexels)
            {
                for (unsigned int row = bufHeightOffset; row < (bufHeightOffset+minRowCount)/rowDepend ; row++)
                {
                    memset(buffers[i]+bitSize(row*bufRowTexels,texelDependBits), 0, bitSize(bufWidthOffset,texelDependBits));
                    memset(buffers[i]+bitSize(row*bufRowTexels+bufWidthOffset+imgRowTexels,texelDependBits), 0, bitSize(bufRowTexels-bufWidthOffset-imgRowTexels,texelDependBits));
                }
            }

            for (unsigned int row = bufHeightOffset+minRowCount; row < bufRowCount/rowDepend; row++)
                memset(buffers[i]+bitSize(row*bufRowTexels,texelDependBits), 0, bitSize(bufRowTexels,texelDependBits));
        }
    }
}

static tai_hook_ref_t ref_sceCameraRead;
static int hook_sceCameraRead(int devnum, SceCameraRead *pRead)
{
    int res = TAI_CONTINUE(int, ref_sceCameraRead, devnum, pRead);

    if ((unsigned int)devnum < NB_CAM && NULL != pRead && cameraActive[devnum])
    {
        uint64_t newTimeStamp = sceKernelGetProcessTimeWide();

        if (res < 0)
        {
            sceKernelDelayThread(1); // Release the thread time quantum to avoid a freeze in some titles (Frobisher Says)

            uint64_t fakeTimeStamp = (newTimeStamp+prevTimeStamp[devnum])>>1;
            uint64_t fakeFrame = (((fakeTimeStamp-initTimeStamp[devnum])*framerate[devnum])>>21) + 1;

            pRead->status = 0;
            if (0 == pRead->mode)
            {
                // Simulate the "wait next frame" time
                while (prevFrame[devnum] >= fakeFrame)
                {
                    sceKernelDelayThread(1000);
                    newTimeStamp = sceKernelGetProcessTimeWide();
                    fakeTimeStamp = (newTimeStamp+prevTimeStamp[devnum])>>1;
                    fakeFrame = (((fakeTimeStamp-initTimeStamp[devnum])*framerate[devnum])>>21) + 1;
                }
            }
            else if (prevFrame[devnum] >= fakeFrame)
                pRead->status = 2;

            ImageBuffers* imageBuf = &imageBuffers[devnum];

            char* buffers[3] = {NULL, NULL, NULL};
            if (NULL == ((SceCameraRead2*)pRead)->unknownNullCheck && sizeof(SceCameraRead2) == pRead->size)
            {
                SceCameraRead2* pRead2 = (SceCameraRead2*)pRead;
                buffers[0] = (NULL != IBufferOnOpen[devnum]) ? IBufferOnOpen[devnum] : pRead2->pIBase;
                buffers[1] = (NULL != UBufferOnOpen[devnum]) ? UBufferOnOpen[devnum] : pRead2->pUBase;
                buffers[2] = (NULL != VBufferOnOpen[devnum]) ? VBufferOnOpen[devnum] : pRead2->pVBase;
            }
            else
            {
                buffers[0] = (NULL != IBufferOnOpen[devnum]) ? IBufferOnOpen[devnum] : pRead->pIBase;
                buffers[1] = (NULL != UBufferOnOpen[devnum]) ? UBufferOnOpen[devnum] : pRead->pUBase;
                buffers[2] = (NULL != VBufferOnOpen[devnum]) ? VBufferOnOpen[devnum] : pRead->pVBase;
            }

            if (config.cycle > 0 && newTimeStamp - imageSwitchTime[devnum] >= (uint64_t)config.cycle * 1000000ull
             && ImageListCount(ImageList(devnum)) > 1)
            {
                imageSwitchTime[devnum] = newTimeStamp;
                imageIndex[devnum]++;
                LoadCameraImage(devnum, openFormat[devnum]);
            }

            int buffersChanged = (prevBuffers[devnum][0] != buffers[0] || prevBuffers[devnum][1] != buffers[1] || prevBuffers[devnum][2] != buffers[2]);
            if (imageBuf->ready > 0 && width[devnum] > 0 && height[devnum] > 0 && (prevFrame[devnum] < fakeFrame || buffersChanged))
            {
                prevFrame[devnum] = fakeFrame;
                FillCameraBuffers(devnum, buffers, buffersChanged);
            }

            pRead->frame = fakeFrame;
            pRead->timestamp = fakeTimeStamp;
            res = 0;
        }

        prevTimeStamp[devnum] = newTimeStamp;
    }

    return res;
}

static tai_hook_ref_t ref_sceCameraIsActive;
static int hook_sceCameraIsActive(int devnum)
{
    int res = TAI_CONTINUE(int, ref_sceCameraIsActive, devnum);
    if ((unsigned int)devnum < NB_CAM && res <= 0) res = cameraActive[devnum];
    return res;
}

static tai_hook_ref_t ref_sceCameraGetDeviceLocation;
static int hook_sceCameraGetDeviceLocation(int devnum, SceFVector3 *pLocation)
{
    int res = TAI_CONTINUE(int, ref_sceCameraGetDeviceLocation, devnum, pLocation);
    if ((unsigned int)devnum < NB_CAM && NULL != pLocation && res < 0) res = 0;
    return res;
}

// Every remaining camera setting is a pair of "get"/"set" functions working on
// an integer: the value set by the title is remembered and given back to it.
#define CAMERA_INT_SETTING(Name, defaultValue)                                     \
static int setting##Name[NB_CAM] = {defaultValue, defaultValue};                    \
static tai_hook_ref_t ref_sceCameraGet##Name;                                       \
static int hook_sceCameraGet##Name(int devnum, int *pValue)                         \
{                                                                                   \
    int res = TAI_CONTINUE(int, ref_sceCameraGet##Name, devnum, pValue);            \
    if ((unsigned int)devnum < NB_CAM && NULL != pValue && res < 0)                 \
    {                                                                               \
        *pValue = setting##Name[devnum];                                            \
        res = 0;                                                                    \
    }                                                                               \
    return res;                                                                     \
}                                                                                   \
static tai_hook_ref_t ref_sceCameraSet##Name;                                       \
static int hook_sceCameraSet##Name(int devnum, int value)                           \
{                                                                                   \
    int res = TAI_CONTINUE(int, ref_sceCameraSet##Name, devnum, value);             \
    if ((unsigned int)devnum < NB_CAM && res < 0)                                   \
    {                                                                               \
        setting##Name[devnum] = value;                                              \
        res = 0;                                                                    \
    }                                                                               \
    return res;                                                                     \
}

CAMERA_INT_SETTING(Saturation, SCE_CAMERA_SATURATION_0)
CAMERA_INT_SETTING(Brightness, 127)
CAMERA_INT_SETTING(Contrast, 127)
CAMERA_INT_SETTING(Sharpness, SCE_CAMERA_SHARPNESS_100)
CAMERA_INT_SETTING(Reverse, SCE_CAMERA_REVERSE_OFF)
CAMERA_INT_SETTING(Effect, SCE_CAMERA_EFFECT_NORMAL)
CAMERA_INT_SETTING(EV, SCE_CAMERA_EV_POSITIVE_0)
CAMERA_INT_SETTING(Zoom, 10)
CAMERA_INT_SETTING(AntiFlicker, SCE_CAMERA_ANTIFLICKER_AUTO)
CAMERA_INT_SETTING(ISO, SCE_CAMERA_ISO_AUTO)
CAMERA_INT_SETTING(Gain, SCE_CAMERA_GAIN_AUTO)
CAMERA_INT_SETTING(WhiteBalance, SCE_CAMERA_WB_AUTO)
CAMERA_INT_SETTING(Backlight, SCE_CAMERA_BACKLIGHT_OFF)
CAMERA_INT_SETTING(Nightmode, SCE_CAMERA_NIGHTMODE_OFF)
CAMERA_INT_SETTING(ExposureCeiling, 0)
CAMERA_INT_SETTING(AutoControlHold, 0)
CAMERA_INT_SETTING(ImageQuality, 0)
CAMERA_INT_SETTING(NoiseReduction, 0)
CAMERA_INT_SETTING(SharpnessOff, 0)

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

#define SCECAMERA_LIBRARY_NID 0xDA91B3ED

typedef struct {
    uint32_t nid;
    const void* func;
    tai_hook_ref_t* ref;
} HookEntry;

#define HOOK(nid, name) { nid, hook_##name, &ref_##name }

static const HookEntry hookTable[] = {
    HOOK(0xA462F801, sceCameraOpen),
    HOOK(0xCD6E1CFC, sceCameraClose),
    HOOK(0xA8FEAE35, sceCameraStart),
    HOOK(0x1DD9C9CE, sceCameraStop),
    HOOK(0x79B5C2DE, sceCameraRead),
    HOOK(0x103A75B8, sceCameraIsActive),
    HOOK(0x274EF751, sceCameraGetDeviceLocation),
    HOOK(0x624F7653, sceCameraGetSaturation),
    HOOK(0xF9F7CA3D, sceCameraSetSaturation),
    HOOK(0x85D5951D, sceCameraGetBrightness),
    HOOK(0x98D71588, sceCameraSetBrightness),
    HOOK(0x8FBE84BE, sceCameraGetContrast),
    HOOK(0x06FB2900, sceCameraSetContrast),
    HOOK(0xAA72C3DC, sceCameraGetSharpness),
    HOOK(0xD1A5BB0B, sceCameraSetSharpness),
    HOOK(0x44F6043F, sceCameraGetReverse),
    HOOK(0x1175F477, sceCameraSetReverse),
    HOOK(0x7E8EF3B2, sceCameraGetEffect),
    HOOK(0xE9D2CFB1, sceCameraSetEffect),
    HOOK(0x8B5E6147, sceCameraGetEV),
    HOOK(0x62AFF0B8, sceCameraSetEV),
    HOOK(0x06D3816C, sceCameraGetZoom),
    HOOK(0xF7464216, sceCameraSetZoom),
    HOOK(0x9FDACB99, sceCameraGetAntiFlicker),
    HOOK(0xE312958A, sceCameraSetAntiFlicker),
    HOOK(0x4EBD5C68, sceCameraGetISO),
    HOOK(0x3CF630A1, sceCameraSetISO),
    HOOK(0x2C36D6F3, sceCameraGetGain),
    HOOK(0xE65CFE86, sceCameraSetGain),
    HOOK(0xDBFFA1DA, sceCameraGetWhiteBalance),
    HOOK(0x4D4514AC, sceCameraSetWhiteBalance),
    HOOK(0x8DD1292B, sceCameraGetBacklight),
    HOOK(0xAE071044, sceCameraSetBacklight),
    HOOK(0x12B6FF26, sceCameraGetNightmode),
    HOOK(0x3F26233E, sceCameraSetNightmode),
    HOOK(0x5FA5B1BB, sceCameraGetExposureCeiling),
    HOOK(0x04F34BEE, sceCameraSetExposureCeiling),
    HOOK(0x06A21BBB, sceCameraGetAutoControlHold),
    HOOK(0x3A0DABBD, sceCameraSetAutoControlHold),
    HOOK(0xE2AC7BCE, sceCameraGetImageQuality),
    HOOK(0x75C4300B, sceCameraSetImageQuality),
    HOOK(0xFEB99ACC, sceCameraGetNoiseReduction),
    HOOK(0xF9B79556, sceCameraSetNoiseReduction),
    HOOK(0x34CCAF85, sceCameraGetSharpnessOff),
    HOOK(0x4B5405C8, sceCameraSetSharpnessOff),
};

#define NB_HOOKS (sizeof(hookTable) / sizeof(hookTable[0]))

static SceUID g_hooks[NB_HOOKS];

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args)
{
    (void)argc;
    (void)args;

    sceAppMgrAppParamGetString(0, 12, titleid, sizeof(titleid));
    titleid[sizeof(titleid) - 1] = '\0';
    LoadConfig();
    Log("FakeCamera " FAKECAMERA_VERSION " started in %s (motion %s, invert_x %s, invert_y %s, sensitivity %d, cycle %d, front \"%s\", back \"%s\")\n",
        titleid, config.motion ? "on" : "off", config.invertX ? "on" : "off", config.invertY ? "on" : "off", config.sensitivity, config.cycle,
        config.front, config.back);

    for (unsigned int i = 0; i < NB_HOOKS; i++)
    {
        g_hooks[i] = taiHookFunctionImport(hookTable[i].ref, TAI_MAIN_MODULE, SCECAMERA_LIBRARY_NID, hookTable[i].nid, hookTable[i].func);
    }

    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args)
{
    (void)argc;
    (void)args;

    for (unsigned int i = 0; i < NB_HOOKS; i++)
    {
        if (g_hooks[i] >= 0)
            taiHookRelease(g_hooks[i], *hookTable[i].ref);
    }

    MotionStopSampling();
    for (int i = 0; i < NB_CAM; i++)
        FreeImageBuffers(&imageBuffers[i]);

    return SCE_KERNEL_STOP_SUCCESS;
}
