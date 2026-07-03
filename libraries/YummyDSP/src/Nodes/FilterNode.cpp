#include <Nodes/FilterNode.h>

FilterNode::FilterNode() { ; }

FilterNode::FilterNode(int fs, int channelCount) { this->begin(fs, channelCount); }

FilterNode::~FilterNode() { if(interpolator) { delete interpolator; } }

void FilterNode::begin(int sampleRate, int channelCount) {
	fs = sampleRate;
	interpolator = new Interpolator(fs, 50);
	memset(filterStates, 0, sizeof(filterStates));
	for (int i = 0; i < 5; i++) {
		filterCoeffs[i][kCurrent] = 0;
		filterCoeffs[i][kTarget] = 0;
	}
}

void FilterNode::setupFilter(int type, float f0, float q, bool smooth, bool resetStates) {
	this->f0 = f0;
	this->type = type;
	this->q = q; 
    float frequency = f0;
    float AUDIO_SAMPLE_RATE_EXACT = fs; 
	float w0 = 0; 
	float cosW0 = 0;
	float sinW0 = 0;
	float alpha = 0;
    double scale = 0;
    float coeff[5];

	switch (type) {
		case LPF:
		    w0 = frequency * (2 * 3.141592654 / AUDIO_SAMPLE_RATE_EXACT);
		    sinW0 = sin(w0);
		    alpha = sinW0 / ((double)q * 2.0);
		    cosW0 = cos(w0);
		    scale = 1.0 / (1.0+alpha);
		    coeff[0] = ((1.0 - cosW0) / 2.0) * scale;
		    coeff[1] = (1.0 - cosW0) * scale;
		    coeff[2] = coeff[0];
		    coeff[3] = (-2.0 * cosW0) * scale;
		    coeff[4] = (1.0 - alpha) * scale;
			break;
		case HPF:
		    w0 = frequency * (2 * 3.141592654 / AUDIO_SAMPLE_RATE_EXACT);
		    sinW0 = sin(w0);
		    alpha = sinW0 / ((double)q * 2.0);
		    cosW0 = cos(w0);
		    scale = 1.0 / (1.0+alpha);
		    coeff[0] = ((1.0 + cosW0) / 2.0) * scale;
		    coeff[1] = -(1.0 + cosW0) * scale;
		    coeff[2] = coeff[0];
		    coeff[3] = (-2.0 * cosW0) * scale;
		    coeff[4] = (1.0 - alpha) * scale;
			break;
		case BPF:
		     w0 = frequency * (2 * 3.141592654 / AUDIO_SAMPLE_RATE_EXACT);
		     sinW0 = sin(w0);
		     alpha = sinW0 / ((double)q * 2.0);
		     cosW0 = cos(w0);
		     scale = 1.0 / (1.0+alpha);
		     coeff[0] = alpha * scale;
		     coeff[1] = 0;
		     coeff[2] = (-alpha) * scale;
		     coeff[3] = (-2.0 * cosW0) * scale;
		     coeff[4] = (1.0 - alpha) * scale;
			break;
		default:
            for (int i = 0; i < 5; i++) coeff[i] = 0;
			break;
	}
	
	filterCoeffs[cB0][kTarget] = coeff[0];
	filterCoeffs[cB1][kTarget] = coeff[1];
	filterCoeffs[cB2][kTarget] = coeff[2];
	filterCoeffs[cA1][kTarget] = coeff[3];
	filterCoeffs[cA2][kTarget] = coeff[4];
	
	if (resetStates) {
		memset(filterStates, 0, sizeof(filterStates));
	}
	
	for (int i = 0; i < 5; i++) {
		interpolator->add(&filterCoeffs[i][kCurrent], filterCoeffs[i][kTarget]);
	}
}

void FilterNode::resetFilter(int type, float f0, float q, bool smooth, bool resetStates) {
  if(interpolator) { delete interpolator; }
  interpolator = new Interpolator(fs, 50);
  memset(filterStates, 0, sizeof(filterStates));
  for (int i = 0; i < 5; i++) {
	filterCoeffs[i][kCurrent] = 0;
	filterCoeffs[i][kTarget] = 0;
  }
  setupFilter(type, f0, q, smooth, resetStates);
}

void FilterNode::updateFilter(float f0) { ; }

float FilterNode::processSample(float sample, int channel) {
	float x = sample;
	float y = filterCoeffs[cB0][kCurrent] * x + filterStates[0][channel];
	filterStates[0][channel] = filterCoeffs[cB1][kCurrent] * x - filterCoeffs[cA1][kCurrent] * y + filterStates[1][channel];
	filterStates[1][channel] = filterCoeffs[cB2][kCurrent] * x - filterCoeffs[cA2][kCurrent] * y;
	interpolator->process();
	return y;
}
