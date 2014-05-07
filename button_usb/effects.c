typedef struct wf_state_s {
  uint8_t t[2];
  uint8_t threshold;
  uint8_t invert:1;
  uint8_t (*waveform)(struct wf_state_s *, uint8_t);
  uint8_t offset;
  uint8_t downscale; // waveform / scale
  uint8_t step_multiplier; // step multiplier
  struct wf_state_s *subwave;
} wf_state_t;

uint8_t clip_overflow(uint16_t n) {
  if (n > 255) //overflow
    return 255;
  else if(n < 0) //underflow
    return 0;
  else
    return (uint8_t) n;
}

uint8_t waveform(wf_state_t *wfs, uint8_t step) {
  uint8_t res, pres;
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
      res=255;
    else if(wfs->offset < 0 && pres < res) //underflow!
      res=0;
  }
  if (wfs->subwave != NULL) {
    if (wfs->downscale > 1)
      res=clip_overflow((int16_t)res + waveform(wfs->subwave, (int8_t) truestep));
    else
      res=clip_overflow((int16_t)res + waveform(wfs->subwave, (int8_t) truestep));
  }
  return res;
}

uint8_t wf_triangle(wf_state_t *wfs, uint8_t step) {
  uint8_t *t=wfs->t;
  if (t[0] >= 256-step)
    t[1]=-1;
  else if (t[0] < step)
    t[1]=1;
  if(wfs->invert==1)
    t[0]-=t[1] * step;
  else 
    t[0]+=t[1] * step;
  return(t[0]);
}

uint8_t wf_sawtooth(wf_state_t *wfs, uint8_t step) {
  uint8_t *t=wfs->t;
  if(t[1]++ % 2 ==0) {
    if(wfs->invert==1)
      t[0]-= step;
    else
      t[0]+= step;
  }
  return(t[0]);
}

uint8_t wf_pulse(wf_state_t *wfs, uint8_t step) {
  return((wf_triangle(wfs, step) > wfs->threshold) ? 255 : 0);
}

uint8_t wf_square(wf_state_t *wfs, uint8_t step) {
  wfs->threshold=128;
  return wf_pulse(wfs, step);
}
uint8_t wf_comb(wf_state_t *wfs, uint8_t step) {
  wfs->threshold=253;
  return wf_pulse(wfs, step);
}

const uint8_t cos_LUT[256]={
    0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   2,   2,   2,   2,   3,   3,   3,   4,   4,   5,   5,   6,   6,   6,   7,   8,   8,   9,   9,
   10,  10,  11,  12,  12,  13,  14,  14,  15,  16,  17,  17,  18,  19,  20,  21,  22,  23,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  37,
   38,  39,  40,  41,  42,  43,  45,  46,  47,  48,  49,  51,  52,  53,  54,  56,  57,  58,  60,  61,  62,  64,  65,  66,  68,  69,  71,  72,  73,  75,  76,  78,
   79,  81,  82,  84,  85,  87,  88,  90,  91,  93,  94,  96,  97,  99, 100, 102, 103, 105, 106, 108, 109, 111, 113, 114, 116, 117, 119, 120, 122, 124, 125, 127,
  128, 130, 131, 133, 135, 136, 138, 139, 141, 142, 144, 146, 147, 149, 150, 152, 153, 155, 156, 158, 159, 161, 162, 164, 165, 167, 168, 170, 171, 173, 174, 176,
  177, 179, 180, 182, 183, 184, 186, 187, 189, 190, 191, 193, 194, 195, 197, 198, 199, 201, 202, 203, 204, 206, 207, 208, 209, 210, 212, 213, 214, 215, 216, 217,
  218, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 232, 233, 234, 235, 236, 237, 238, 238, 239, 240, 241, 241, 242, 243, 243, 244, 245, 245,
  246, 246, 247, 247, 248, 249, 249, 249, 250, 250, 251, 251, 252, 252, 252, 253, 253, 253, 253, 254, 254, 254, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255
};

uint8_t wf_cos(wf_state_t *wfs, uint8_t step) {
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
uint8_t wf_random(wf_state_t *wfs, uint8_t step) {
  return(badrand());
}