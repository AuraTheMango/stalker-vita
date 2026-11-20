#pragma once
#define SCE_AUDIO_OUT_PORT_TYPE_MAIN 0
#define SCE_AUDIO_OUT_MODE_STEREO 0
int sceAudioOutOpenPort(int,unsigned,int,int);
int sceAudioOutOutput(int,const void*);
int sceAudioOutReleasePort(int);
