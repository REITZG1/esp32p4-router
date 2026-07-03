/*
 * 	Interpolator
 *
 *  Author: Gary Grutzek
 * 	gary@ib-gru.de
 */

#include "Interpolator.h"

Interpolator::Interpolator() {
	;
}

Interpolator::Interpolator(int fs, int ms) {
	this->begin(fs, ms);
}

Interpolator::~Interpolator() {
	values.clear();
}

// Junon
void Interpolator::clear() {
	values.clear();
}


void Interpolator::begin(int fs, int ms) {
	samplerate = fs;
	invFadeSteps = 1.f / ((float)fs / 1000.0f * (float)constrain(ms, 1, 1000)); // e.g. 1440 samples for 30 ms @ 48kHz
}

void Interpolator::add(float *current, float target) {

	// check if exists
	std::list<rampValue>::iterator it = values.begin();
	while (it != values.end()) {
		if ((*it).current == current) {
			(*it).target = target;
			(*it).increment = (target - *current) * invFadeSteps;
			return;
		}
		it++;
	}

	rampValue v;
	v.target = target;
	v.current = current;
	v.increment = (target - *current) * invFadeSteps;
	values.push_back(v);
}

void Interpolator::process() {

	std::list<rampValue>::iterator it = values.begin();
	while (it != values.end()) {
		rampValue v = *it;
		// ramping
		*v.current += v.increment;
		// remove when ramped to target
		if ( (*(v.current) >= v.target && v.increment > 0) || (*(v.current) <= v.target && v.increment < 0) ) {
			*(v.current) = v.target;
			it = values.erase(it);
		}
		else {
			it++;
		}
	}
}
