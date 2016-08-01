#include <stdio.h>
#include <string.h>
#include <windows.h> // need this only for WAVEFORMATEX, not for aviwriter.h.
#include "aviwriter.h"

int main()
{
  AVIWriter writer;

  static const int xRes = 400;
  static const int yRes = 300;
  static const int frameRate = 60;
  static const int nFrames = 256;

  static const bool audio = true;
  static const int sampleRate = 44100;
  static const int samplesPerFrame = 44100 / frameRate;

  // for the presets, samplesPerFrame works out to an integer, if it doesn't
  // you should splice in 1-sample-longer frames regularly or you risk
  // crappy interleaving.

  if(!writer.Init("checker.avi",frameRate,~0))
  {
    printf("Couldn't open video file!\n");
    return 0;
  }

  unsigned long *buffer = new unsigned long[xRes*yRes];
  writer.SetSize(xRes,yRes);

  // set up wave format
  if(audio)
  {
    WAVEFORMATEX wfx;
    memset(&wfx,0,sizeof(wfx));

    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1; // mono is enough
    wfx.nSamplesPerSec = sampleRate;
    wfx.nAvgBytesPerSec = sampleRate * 2;
    wfx.nBlockAlign = 2;
    wfx.wBitsPerSample = 16;
    writer.SetAudioFormat(&wfx);
  }

  // audio state
  float c1=24000.0f, s1=0.0f, f1 = 0.03f;
  float c2=1.0f, s2=0.0f, f2 = 0.0013f;

  // render nFrames frames of the magic scrolling checkerboard(tm)
  for(int frame=0;frame<nFrames;frame++)
  {
    printf("\rframe %3d",frame);

    unsigned long *ptr = buffer;

    for(int y=0;y<yRes;y++)
    {
      for(int x=0;x<xRes;x++)
      {
        int on = ((x + frame) & 32) ^ ((y+frame) & 32);
        *ptr++ = on ? 0xffffffffu : 0xff000000u;
      }
    }

    writer.WriteFrame((unsigned char *) buffer,xRes*4);

    if(audio)
    {
      // *everything* is trippier with hypnotic sinewaves as audio backdrop!
      short samples[samplesPerFrame];

      for(int i=0;i<samplesPerFrame;i++)
      {
        c1 -= f1*s1;
        s1 += f1*c1;

        c2 += f2*s2;
        s2 -= f2*c2;

        float val = s1 * (0.75f + 0.25f * c2);
        if(frame == nFrames - 1) // fade audio out on last frame
          val *= 1.0f * (samplesPerFrame - 1 - i) / samplesPerFrame;

        samples[i] = (int) val;
      }

      writer.WriteAudioFrame(samples,samplesPerFrame);
    }
  }

  printf("\rdone.              \n");

  delete[] buffer;
  writer.Exit();

  return 0;
}