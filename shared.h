#define ASSERT_CONCAT_(a, b) a##b
#define ASSERT_CONCAT(a, b) ASSERT_CONCAT_(a, b)
#ifdef __COUNTER__
  #define STATIC_ASSERT(e,m) \
    enum { ASSERT_CONCAT(static_assert_, __COUNTER__) = 1/(!!(e)) }
#else
  #define STATIC_ASSERT(e,m) \
    enum { ASSERT_CONCAT(assert_line_, __LINE__) = 1/(!!(e)) }
#endif

// These 4 numbers identify your device.  Set these to
// something that is (hopefully) not used by any others!
#define VENDOR_ID     0xFAD9
#define PRODUCT_ID    0x2C5D
#define RAWHID_USAGE_PAGE 0xFFBC
#define RAWHID_USAGE  0xFFF0


typedef struct {
  char header[5];
  uint8_t button;
  uint8_t led[2];
  uint8_t vibrate;
  uint8_t pattern;
  char footer[5];
} state_t;

STATIC_ASSERT(sizeof(state_t) < 64, "Packet cannot exceed 64 bytes.");