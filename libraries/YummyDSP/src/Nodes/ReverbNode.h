#ifndef REVERBNODE_H
#define REVERBNODE_H

#include "Arduino.h"
#include "Nodes/AudioNode.h"
#include "Nodes/FilterNode.h"

#define STATIC_REV_BUFFER

#ifdef BOARD_HAS_PSRAM 
#define REV_MULTIPLIER 1.2f
#else
#define REV_MULTIPLIER 0.35f
#endif

#define l_CB0 (int)( 3604 * REV_MULTIPLIER)
#define l_CB1 (int)( 3112 * REV_MULTIPLIER)
#define l_CB2 (int)( 4044 * REV_MULTIPLIER)
#define l_CB3 (int)( 4492 * REV_MULTIPLIER)
#define l_AP0 (int)( 500 * REV_MULTIPLIER)
#define l_AP1 (int)( 168 * REV_MULTIPLIER)
#define l_AP2 (int)( 48 * REV_MULTIPLIER)

class ReverbNode : public AudioNode {
	public:
	ReverbNode() {};
	ReverbNode(int sampleRate, int channelCount) {};
	~ReverbNode() {};
	
  	inline float processSample( float inSample, int channel  ){
   		float newsample = (Do_Comb0(inSample) + Do_Comb1(inSample) + Do_Comb2(inSample) + Do_Comb3(inSample)) * 0.125f;
  		newsample = Do_Allpass0(newsample);
  		newsample = Do_Allpass1(newsample);
  		newsample = Do_Allpass2(newsample);
  		newsample *= rev_level;
  		inSample += newsample; 
		return inSample;
  	};
  
  	inline void begin(int sampleRate, int channelCount){ 
  		setMix( 1.0f );
  		setTime( 0.75f );
  	};
		
    inline void setTime( float value ){
      rev_time = 0.92f * value + 0.02f;
      cf0_lim = (int)(rev_time * (l_CB0));
      cf1_lim = (int)(rev_time * (l_CB1));
      cf2_lim = (int)(rev_time * (l_CB2));
      cf3_lim = (int)(rev_time * (l_CB3));
      ap0_lim = (int)(rev_time * (l_AP0));
      ap1_lim = (int)(rev_time * (l_AP1));
      ap2_lim = (int)(rev_time * (l_AP2));
    };
    
    inline void setMix( float value ){
      rev_level = value;
    };
		
	private:
		float rev_time = 0.5f;
		float rev_level = 0.5f;
#ifdef STATIC_REV_BUFFER
    float cfbuf0[l_CB0];
    float cfbuf1[l_CB1];
    float cfbuf2[l_CB2];
    float cfbuf3[l_CB3];
    float apbuf0[l_AP0];
    float apbuf1[l_AP1];
    float apbuf2[l_AP2];
#else
    float * cfbuf0 = NULL;
    float * cfbuf1 = NULL;
    float * cfbuf2 = NULL;
    float * cfbuf3 = NULL;
    float * apbuf0 = NULL;
    float * apbuf1 = NULL;
    float * apbuf2 = NULL;    
#endif	
		int cf0_lim, cf1_lim, cf2_lim, cf3_lim, ap0_lim, ap1_lim, ap2_lim;
    
    inline float Do_Comb0( float inSample ){
      static int cf0_p = 0;
      static float cf0_g = 0.805f;
      float readback = cfbuf0[cf0_p];
      float newV = readback * cf0_g + inSample;
      cfbuf0[cf0_p] = newV;
      cf0_p++;
      if( cf0_p >= cf0_lim ){ cf0_p = 0; }
      return readback;
    };

    inline float Do_Comb1( float inSample ){
      static int cf1_p = 0;
      static float cf1_g = 0.827f;
      float readback = cfbuf1[cf1_p];
      float newV = readback * cf1_g + inSample;
      cfbuf1[cf1_p] = newV;
      cf1_p++;
      if( cf1_p >= cf1_lim ){ cf1_p = 0; }
      return readback;
    };

    inline float Do_Comb2( float inSample ){
      static int cf2_p = 0;
      static float cf2_g = 0.783f;
      float readback = cfbuf2[cf2_p];
      float newV = readback * cf2_g + inSample;
      cfbuf2[cf2_p] = newV;
      cf2_p++;
      if( cf2_p >= cf2_lim ){ cf2_p = 0; }
      return readback;
    };

    inline float Do_Comb3( float inSample ){
      static int cf3_p = 0;
      static float cf3_g = 0.764f;
      float readback = cfbuf3[cf3_p];
      float newV = readback * cf3_g + inSample;
      cfbuf3[cf3_p] = newV;
      cf3_p++;
      if( cf3_p >= cf3_lim ){ cf3_p = 0; }
      return readback;
    };

    inline float Do_Allpass0( float inSample ){
      static int ap0_p = 0;
      static float ap0_g = 0.7f;
      float readback = apbuf0[ap0_p];
      readback += (-ap0_g) * inSample;
      float newV = readback * ap0_g + inSample;
      apbuf0[ap0_p] = newV;
      ap0_p++;
      if( ap0_p >= ap0_lim ){ ap0_p = 0; }
      return readback;
    };

    inline float Do_Allpass1( float inSample ){
      static int ap1_p = 0;
      static float ap1_g = 0.7f;
      float readback = apbuf1[ap1_p];
      readback += (-ap1_g) * inSample;
      float newV = readback * ap1_g + inSample;
      apbuf1[ap1_p] = newV;
      ap1_p++;
      if( ap1_p >= ap1_lim ){ ap1_p = 0; }
      return readback;
    };

    inline float Do_Allpass2( float inSample ){
      static int ap2_p = 0;
      static float ap2_g = 0.7f;
      float readback = apbuf2[ap2_p];
      readback += (-ap2_g) * inSample;
      float newV = readback * ap2_g + inSample;
      apbuf2[ap2_p] = newV;
      ap2_p++;
      if( ap2_p >= ap2_lim ){ ap2_p = 0; }
      return readback;
    };
};

#endif
