#define _CRT_SECURE_NO_DEPRECATE // yeah, shut up.

#include <windows.h>
#include <vfw.h>
#include <stdio.h>
#include <string.h>
#include "aviwriter.h"

#pragma comment(lib, "vfw32.lib")

#ifdef _WIN64
#define USEMMX 0  // no MMX on Win64
#else
#define USEMMX 1
#endif

// internal data
struct AVIWriter::Internal
{
  CRITICAL_SECTION lock;
  PAVIFILE file;
  PAVISTREAM vid,vidC;
  PAVISTREAM aud;
  WAVEFORMATEX wfx;

  char basename[_MAX_PATH];
  int segment;
  unsigned long codec;
  unsigned int quality;
  LONG overflowCounter;
  
  unsigned char *conversionBuffer;

  bool initialized;
  bool formatSet;
};

bool AVIWriter::InternalInit()
{
  AVISTREAMINFOA asi;
  AVICOMPRESSOPTIONS aco;
  bool error = true;

  AVIFileInit();
  d->overflowCounter = 0;

  // create avi file
  char name[_MAX_PATH];
  _snprintf(name,_MAX_PATH,"%s.%02d.avi",d->basename,d->segment);
  name[_MAX_PATH-1] = 0;

  if(AVIFileOpenA(&d->file,name,OF_CREATE|OF_WRITE,NULL) != AVIERR_OK)
    goto cleanup;

  // initialize video stream header
  ZeroMemory(&asi,sizeof(asi));
  asi.fccType               = streamtypeVIDEO;
  asi.dwScale               = 10000;
  asi.dwRate                = (DWORD) (10000*fps + 0.5f);
  asi.dwSuggestedBufferSize = xRes * yRes * 3;
  asi.rcFrame.right         = xRes;
  asi.rcFrame.bottom        = yRes;
  memcpy(asi.szName,"Video\0",6);

  // create video stream
  if(AVIFileCreateStreamA(d->file,&d->vid,&asi) != AVIERR_OK)
    goto cleanup;

  // create compressed stream
  unsigned long codec = d->codec;
  if(!codec
#ifdef _WIN64
      || true // force on win64
#endif
      )
    codec = mmioFOURCC('D','I','B',' '); // uncompressed frames
  
  ZeroMemory(&aco,sizeof(aco));
  aco.fccType = streamtypeVIDEO;
  aco.fccHandler = codec;
  aco.dwQuality = d->quality;

  // display codec selection dialog if requested.
  if(codec == ~0U)
  {
    AVICOMPRESSOPTIONS *optlist[1] = { &aco };

    if(AVISaveOptions(0,0,1,&d->vid,optlist) == TRUE)
    {
      d->codec = codec = aco.fccHandler;
      d->quality = aco.dwQuality;
    }
    else
      goto cleanup;
#endif
  }

  if(AVIMakeCompressedStream(&d->vidC,d->vid,&aco,0) != AVIERR_OK)
    goto cleanup;

  error = false;
  d->initialized = true;

cleanup:
  if(error)
    InternalCleanup();

  return !error;
}

void AVIWriter::InternalCleanup()
{
  EnterCriticalSection(&d->lock);
  
  if(d->initialized)
  {
    // Some broken codecs barf on exit, make sure this doesn't hit our app.
    __try
    {
      if(d->aud)
      {
        AVIStreamRelease(d->aud);
        d->aud = 0;
      }

      if(d->vidC)
      {
        AVIStreamRelease(d->vidC);
        d->vidC = 0;
      }

      if(d->vid)
      {
        AVIStreamRelease(d->vid);
        d->vid = 0;
      }

      if(d->file)
      {
        AVIFileRelease(d->file);
        d->file = 0;
      }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }

    AVIFileExit();
    d->initialized = false;
  }
  
  LeaveCriticalSection(&d->lock);
}

bool AVIWriter::StartEncode()
{
  BITMAPINFOHEADER bmi;
  bool error = true;

  if(!d->file)
    return false;

  EnterCriticalSection(&d->lock);

  // set stream format
  ZeroMemory(&bmi,sizeof(bmi));
  bmi.biSize        = sizeof(bmi);
  bmi.biWidth       = xRes;
  bmi.biHeight      = yRes;
  bmi.biPlanes      = 1;
  bmi.biBitCount    = 24;
  bmi.biCompression = BI_RGB;
  bmi.biSizeImage   = xRes * yRes * 3;
  if(AVIStreamSetFormat(d->vidC,0,&bmi,sizeof(bmi)) != AVIERR_OK)
    goto cleanup;

  error = false;
  frame = 0;
  d->formatSet = true;

cleanup:
  LeaveCriticalSection(&d->lock);

  if(error)
    InternalCleanup();

  return !error;
}

bool AVIWriter::StartAudioEncode(const tWAVEFORMATEX *fmt)
{
  AVISTREAMINFOA asi;
  bool error = true;

  if(!d->file)
    return false;

  EnterCriticalSection(&d->lock);

  // initialize stream info
  ZeroMemory(&asi,sizeof(asi));
  asi.fccType               = streamtypeAUDIO;
  asi.dwScale               = 1;
  asi.dwRate                = fmt->nSamplesPerSec;
  asi.dwSuggestedBufferSize = fmt->nAvgBytesPerSec;
  asi.dwSampleSize          = fmt->nBlockAlign;
  memcpy(asi.szName,"Audio\0",6);

  // create the stream
  if(AVIFileCreateStreamA(d->file,&d->aud,&asi) != AVIERR_OK)
    goto cleanup;

  // set format
  if(AVIStreamSetFormat(d->aud,0,(LPVOID) fmt,sizeof(WAVEFORMATEX)) != AVIERR_OK)
    goto cleanup;

  error = false;
  audioSample = 0;
  audioBytesSample = fmt->nBlockAlign;

cleanup:
  LeaveCriticalSection(&d->lock);

  if(error)
    InternalCleanup();

  return !error;
}

AVIWriter::AVIWriter()
{
  d = new Internal;
  d->file = 0;
  d->vid = 0;
  d->vidC = 0;
  d->aud = 0;
  d->conversionBuffer = 0;
  d->initialized = false;
  InitializeCriticalSection(&d->lock);
}

AVIWriter::~AVIWriter()
{
  Exit();
  DeleteCriticalSection(&d->lock);
}

bool AVIWriter::Init(const char *name,float fps,unsigned long codec,unsigned int quality)
{
  Exit();

  xRes = yRes = 0;
  frame = 0;
  audioSample = 0;
  this->fps = fps;

  // determine file base name
  int i = 0;
  while(name[i] && i<sizeof(d->basename)-1)
  {
    d->basename[i] = name[i];
    i++;
  }

  d->basename[i] = 0;
  while(i-- >= 0)
  {
    if(d->basename[i] == '/' || d->basename[i] == '\\')
      break;
    else if(d->basename[i] == '.')
    {
      d->basename[i] = 0;
      break;
    }
  }

  d->segment = 1;
  d->codec = codec;
  d->quality = quality;

  d->initialized = false;
  d->formatSet = false;

  return InternalInit();
}

void AVIWriter::Exit()
{
  InternalCleanup();

  if(d->conversionBuffer)
  {
    delete[] d->conversionBuffer;
    d->conversionBuffer = 0;
  }
}

void AVIWriter::SetSize(int _xRes,int _yRes)
{
  if(!d->formatSet)
  {
    xRes = _xRes;
    yRes = _yRes;
  }
}

bool AVIWriter::WriteFrameBottomUpBGR(const unsigned char *buffer)
{
  bool error = true;

  // encode the frame
  EnterCriticalSection(&d->lock);

  if(!frame && !d->formatSet) 
  {
    if(!xRes || !yRes || !StartEncode())
      goto end;
  }

  if(d->formatSet && d->vidC)
  {
    // once we are >1.8GB...
    if(d->overflowCounter >= 1024*1024*1800)
    {
      bool gotAudio = d->aud != 0;

      // end this segment and start a new one.
      InternalCleanup();
      d->segment++;
      InternalInit();

      StartEncode();
      if(gotAudio)
        StartAudioEncode(&d->wfx);
    }

    LONG written = 0;
    AVIStreamWrite(d->vidC,frame,1,(void *)buffer,3*xRes*yRes,0,0,&written);
    frame++;
    d->overflowCounter += written;

    error = written == 0;
  }
  
end:
  LeaveCriticalSection(&d->lock);
  return !error;
}

static void blit32to24loop(unsigned char *dest,const unsigned char *src,unsigned int count)
{
#if USEMMX
  // BGRA->BGR conversion using MMX, slightly faster than doing it with C.
  // (enough to make kkapture run a few % faster, not enough to actually make
  // a fuss about it, but well, I had the time so...)
  static const unsigned __int64 mask = 0x00ffffff00ffffff;

  __asm
  {
    pxor      mm7, mm7;
    mov       esi, [src];
    mov       edi, [dest];
    mov       ecx, [count];
    shr       ecx, 3;
    jz        tail;

blitloop:
    movq      mm0, [esi];     // mm0=b0g0r0a0b1g1r1a1
    movq      mm1, [esi+8];   // mm1=b2g2r2a2b3g3r3a3
    movq      mm2, [esi+16];  // mm2=b4g4r4a4b5g5r5a5
    movq      mm3, [esi+24];  // mm3=b6g6r6a6b7g7r7a7

    pand      mm0, [mask];    // mm0=b0g0r0__b1g1r1__
    pand      mm1, [mask];    // mm1=b2g2r2__b3g3r3__
    pand      mm2, [mask];    // mm2=b4g4r4__b5g5r5__
    pand      mm3, [mask];    // mm3=b6g6r6__b7g7r7__

    movq      mm4, mm0;       // mm4=b0g0r0__b1g1r1__
    movq      mm5, mm1;       // mm5=b2g2r2__b3g3r3__
    movq      mm6, mm1;       // mm6=b2g2r2__b3g3r3__
    punpckhdq mm0, mm7;       // mm0=b1g1r1__________
    punpckldq mm4, mm7;       // mm4=b0g0r0__________
    psllq     mm5, 48;        // mm5=____________b2g2
    punpckldq mm1, mm7;       // mm1=b2g2r2__________
    psllq     mm0, 24;        // mm0=______b1g1r1____
    por       mm4, mm5;       // mm4=b0g0r0______b2g2
    punpckhdq mm6, mm7;       // mm6=b3g3r3__________
    psrlq     mm1, 16;        // mm1=r2______________
    por       mm0, mm4;       // mm0=b0g0r0b1g1r1b2g2
    movq      mm4, mm2;       // mm4=b4g4r4__b5g5r5__
    pxor      mm5, mm5;       // mm5=________________
    psllq     mm6, 8;         // mm6=__b3g3r3________
    punpckhdq mm2, mm7;       // mm2=b5g5r5__________
    movq      [edi], mm0;
    por       mm1, mm6;       // mm1=r2b3g3r3________
    psllq     mm2, 56;        // mm2=______________b5
    punpckhdq mm5, mm3;       // mm5=________b7g7r7__
    punpckldq mm1, mm4;       // mm1=r2b3g3r3b4g4r4__
    punpckldq mm3, mm7;       // mm3=b6g6r6__________
    psrlq     mm4, 40;        // mm4=g5r5____________
    psllq     mm5, 8;         // mm5=__________b7g7r7
    por       mm1, mm2;       // mm1=r2b3g3r3b4g4r4b5
    psllq     mm3, 16;        // mm3=____b6g6r6______
    por       mm4, mm5;       // mm4=g5r5______b7g7r7
    movq      [edi+8], mm1;
    add       esi, 32;
    por       mm3, mm4;       // mm3=g5r5b6g6r6b7g7r7
    movq      [edi+16], mm3;
    add       edi, 24;
    dec       ecx;
    jnz       blitloop;

tail:
    mov       ecx, [count];
    and       ecx, 7;
    jz        end;

tailloop:
    movsb;
    movsb;
    movsb;
    inc       esi;
    dec       ecx;
    jnz       tailloop;

end:
    emms;
  }
#else
  const unsigned char *srcp = src;
  unsigned char *dstp = dest;

  for(int x=0;x<count;x++)
  {
    *dstp++ = *srcp++;
    *dstp++ = *srcp++;
    *dstp++ = *srcp++;
    srcp++;
  }
#endif
}

bool AVIWriter::WriteFrame(const unsigned char *buffer,unsigned int linePitch)
{
  if(!d->conversionBuffer)
    d->conversionBuffer = new unsigned char[xRes*yRes*3];

  unsigned int dstPitch = xRes*3;

  const unsigned char *src = buffer + (yRes-1) * linePitch;
  unsigned char *dst = d->conversionBuffer;

  for(int y=0;y<yRes;y++)
  {
    blit32to24loop(dst,src,xRes);

    dst += dstPitch;
    src -= linePitch;
  }

  return WriteFrameBottomUpBGR(d->conversionBuffer);
}

void AVIWriter::SetAudioFormat(const tWAVEFORMATEX *fmt)
{
  d->wfx = *fmt;
}

bool AVIWriter::WriteAudioFrame(const void *buffer,int samples)
{
  bool error = true;

  EnterCriticalSection(&d->lock);

  if(!d->aud && !StartAudioEncode(&d->wfx))
    goto end;

  if(d->aud && samples)
  {
    // Write this frame's audio.
    LONG written = 0;
    AVIStreamWrite(d->aud,audioSample,samples,(LPVOID) buffer,
      samples*audioBytesSample,0,0,&written);
    audioSample += samples;
    d->overflowCounter += written;

    error = written == 0;
  }

end:
  LeaveCriticalSection(&d->lock);
  return !error;
}