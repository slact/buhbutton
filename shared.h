#define ASSERT_CONCAT_(a, b) a##b
#define ASSERT_CONCAT(a, b) ASSERT_CONCAT_(a, b)
#ifdef __COUNTER__
  #define STATIC_ASSERT(e,m) \
    enum { ASSERT_CONCAT(static_assert_, __COUNTER__) = 1/(!!(e)) }
#else
  #define STATIC_ASSERT(e,m) \
    enum { ASSERT_CONCAT(assert_line_, __LINE__) = 1/(!!(e)) }
#endif

typedef struct {
  char header[5];
  uint8_t button;
  uint8_t led[2];
  uint8_t vibrate;
  uint8_t pattern;
  uint8_t pinf;
  char footer[5];
} state_t;

STATIC_ASSERT(sizeof(state_t) < 64, "Packet cannot exceed 64 bytes.");