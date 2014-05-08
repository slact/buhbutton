char dbg_buf[1024];
void dbg_print(char *str);
#define dbg_printf(format, ...) \
snprintf(&dbg_buf, 1024, format, __VA_ARGS__);     \
dbg_print(&dbg_buf);
