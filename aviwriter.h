#pragma once

#ifndef __AVIWRITER_H__
#define __AVIWRITER_H__

struct tWAVEFORMATEX; // structure tag for Windows WAVEFORMATEX structure.

class AVIWriter
{
  struct Internal;
  
  int xRes,yRes;
  int frame;
  int audioSample,audioBytesSample;
  float fps;
  Internal *d;

  bool InternalInit();
  void InternalCleanup();
  bool StartEncode();
  bool StartAudioEncode(const tWAVEFORMATEX *fmt);

public:
  AVIWriter();
  ~AVIWriter();

  // Opens an AVI file for writing.
  // name:    File name for the target file.
  // fps:     Target FPS rate.
  // codec:   FOURCC of codec to use (0=uncompressed, ~0=display codec selection dialog).
  // quality: Quality to use. Value is codec-specific. Most modern codecs ignore this anyway.
  // Return true if successful, false if not.
  bool Init(const char *name,float fps,unsigned long codec=~0U,unsigned int quality=100);

  // Cleans up everything. Never fails.
  void Exit();

  // Set size of image. Need to call this before the first call to WriteFrame.
  // Ignored after the first frame has been written.
  void SetSize(int xRes,int yRes);
  
  // Write one frame's worth of bottom-up, BGR data (this is the format that video for windows expects).
  // Automatically starts a new .AVI file when the current one is >=1.8GB (to avoid >2GB issues).
  // Segments are numbered.
  // Returns true if successful.
  bool WriteFrameBottomUpBGR(const unsigned char *buffer);

  // Write one frame's worth of top-down, BGRA data (convenience function, D3D and OGL both support this directly).
  // Except for the input format, this is the same as WriteFrameBottomUpBGR.
  // linePitch is distance between lines in bytes.
  // Returns true if successful.
  bool WriteFrame(const unsigned char *buffer,unsigned int linePitch);

  // Sets the audio format to use (standard WAVEFORMATEX structure). Need to call this before first WriteAudioFrame.
  void SetAudioFormat(const tWAVEFORMATEX *fmt);

  // Writes a bunch of audio data. You should save a proper amount of samples per frame or the target file's
  // interleaving will be screwed (which means crappy playback). Size of buffer is in samples, not bytes.
  // If you never call this, no audio stream will be created. You are expected to call first WriteFrame, then
  // WriteAudioFrame (doing this the other way round won't be fatal but may result in crappy interleaving, again).
  // Returns true if successful.
  bool WriteAudioFrame(const void *buffer,int samples);
};

#endif