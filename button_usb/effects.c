typedef struct wf_state_s {
  int8_t t[2];
  int8_t threshold;
  uint8_t invert:1;
  int8_t (*waveform)(struct wf_state_s *, uint8_t);
  int8_t offset;
  uint8_t downscale; // waveform / scale
  uint8_t step_multiplier; // step multiplier
  struct wf_state_s *subwave;
} wf_state_t;

uint8_t clip_overflow(uint16_t n) {
  if (n > INT8_MAX) //overflow
    return INT8_MAX;
  else if(n < INT8_MIN) //underflow
    return INT8_MIN;
  else
    return (int8_t) n;
}

int8_t waveform(wf_state_t *wfs, uint8_t step) {
  int8_t res, pres;
  int16_t precision_res, truestep;
  truestep= step * wfs->step_multiplier;
  if(truestep > 255)
    truestep=255;
  if(wfs->step_multiplier==0)
    wfs->step_multiplier=1;
  
  if(wfs->waveform == NULL)
    return 0; //bad!

  res=wfs->waveform(wfs, (uint8_t) truestep);
  if (wfs->downscale > 1) {
    res=clip_overflow(res/wfs->downscale);
  }
  if(wfs->offset > 0) {
    pres = res; //overflow detection
    res += wfs->offset;
    if(wfs->offset > 0 && pres > res) //overflow!
      res=INT8_MAX;
    else if(wfs->offset < 0 && pres < res) //underflow!
      res=INT8_MIN;
  }
  if (wfs->subwave != NULL) {
    if (wfs->downscale > 1)
      res=clip_overflow((int16_t)res + waveform(wfs->subwave, (int8_t) truestep));
  }
  return res;
}

int8_t wf_triangle(wf_state_t *wfs, uint8_t step) {
  int8_t *t=wfs->t;
  if (t[0] >= INT8_MAX+1-step)
    t[1]=-1;
  else if (t[0] < INT8_MIN + step)
    t[1]=1;
  if(wfs->invert==1)
    t[0]-=t[1] * step;
  else 
    t[0]+=t[1] * step;
  return(t[0]);
}

int8_t wf_sawtooth(wf_state_t *wfs, uint8_t step) {
  int8_t *t=wfs->t;
  if(t[1]++ % 2 ==0) {
    if(wfs->invert==1)
      t[0]-= step;
    else
      t[0]+= step;
  }
  return(t[0]);
}

int8_t wf_pulse(wf_state_t *wfs, uint8_t step) {
  return((wf_triangle(wfs, step) > wfs->threshold) ? INT8_MAX : INT8_MIN);
}

int8_t wf_square(wf_state_t *wfs, uint8_t step) {
  wfs->threshold=0;
  return wf_pulse(wfs, step);
}
int8_t wf_comb(wf_state_t *wfs, uint8_t step) {
  wfs->threshold=INT8_MAX-4;
  return wf_pulse(wfs, step);
}

const int8_t cos_LUT[256]={
  -127, -127, -127, -127, -127, -127, -127, -127, -126, -126, -126, -126, -126, -125, -125, -125,
  -125, -124, -124, -124, -123, -123, -122, -122, -121, -121, -121, -120, -119, -119, -118, -118,
  -117, -117, -116, -115, -115, -114, -113, -113, -112, -111, -110, -110, -109, -108, -107, -106,
  -105, -104, -104, -103, -102, -101, -100,  -99,  -98,  -97,  -96,  -95,  -94,  -93,  -92,  -90,
   -89,  -88,  -87,  -86,  -85,  -84,  -82,  -81,  -80,  -79,  -78,  -76,  -75,  -74,  -73,  -71,
   -70,  -69,  -67,  -66,  -65,  -63,  -62,  -61,  -59,  -58,  -56,  -55,  -54,  -52,  -51,  -49,
   -48,  -46,  -45,  -43,  -42,  -40,  -39,  -37,  -36,  -34,  -33,  -31,  -30,  -28,  -27,  -25,
   -24,  -22,  -21,  -19,  -18,  -16,  -14,  -13,  -11,  -10,   -8,   -7,   -5,   -3,   -2,    0,
     1,    3,    4,    6,    8,    9,   11,   12,   14,   15,   17,   19,   20,   22,   23,   25,
    26,   28,   29,   31,   32,   34,   35,   37,   38,   40,   41,   43,   44,   46,   47,   49,
    50,   52,   53,   55,   56,   57,   59,   60,   62,   63,   64,   66,   67,   68,   70,   71,
    72,   74,   75,   76,   77,   79,   80,   81,   82,   83,   85,   86,   87,   88,   89,   90,
    91,   93,   94,   95,   96,   97,   98,   99,  100,  101,  102,  103,  104,  105,  105,  106,
   107,  108,  109,  110,  111,  111,  112,  113,  114,  114,  115,  116,  116,  117,  118,  118,
   119,  119,  120,  120,  121,  122,  122,  122,  123,  123,  124,  124,  125,  125,  125,  126,
   126,  126,  126,  127,  127,  127,  127,  127,  128,  128,  128,  128,  128,  128,  128,  128
};

int8_t wf_cos(wf_state_t *wfs, uint8_t step) {
  return(cos_LUT[wf_triangle(wfs, step)]);
}

//X ABC Algorithm Random Number Generator for 8-Bit Devices:
// posted by EternityForest on http://www.electro-tech-online.com/threads/ultra-fast-pseudorandom-number-generator-for-8-bit.124249/
uint8_t rng_a, rng_b, rng_c;
uint8_t badrand() {
  static uint8_t rng_x;
  rng_x++; //x is incremented every round and is not affected by any other variable
  rng_a = (rng_a^rng_c^rng_x);       //note the mix of addition and XOR
  rng_b = (rng_b+rng_a);         //And the use of very few instructions
  rng_c = (rng_c+((rng_b>>1)^rng_a));  //the right shift is to ensure that high-order bits from b can affect  
  return(rng_c);          //low order bits of other variables
}
int8_t wf_random(wf_state_t *wfs, uint8_t step) {
  return((int8_t) badrand());
}