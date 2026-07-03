#ifndef MIXER_H
#define MIXER_H

#include "Arduino.h"
#include "Nodes/AudioNode.h"
#include "Interpolator.h"

class MixerNode : public AudioNode {
public:
	MixerNode();
	MixerNode(int fs, int channelCount);
	~MixerNode();
	void begin(int fs, int channelCount);
	float processSample(float sample, int channel);
	float getVolume() { return volume[kCurrent]; }
	void setVolume(float volume, bool fade = true);
	float getVolumedB() { return 20 * log10(volume[kCurrent]); }
	void setVolumedB(float dB, bool fade = true);
protected:
	int fs;
	Interpolator *interpolator;
	float volume[kSmoothCount];
};

#endif
