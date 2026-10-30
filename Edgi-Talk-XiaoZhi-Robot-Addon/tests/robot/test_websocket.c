/* Test the production C transport with mock TCP and pbuf functions. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int err_t;
typedef uint16_t u16_t;
enum { ERR_OK=0, ERR_VAL=-1, ERR_CONN=-2, ERR_MEM=-3,
       WS_TCP_CONNECTED=1, PWS_STATE_INITD=1, PBUF_RAW=0, PBUF_RAM=0,
       TCP_WRITE_FLAG_COPY=1, WSOCK_RESULT_ERR_MEM=2, WSOCK_RESULT_ERR_UNKNOWN=3 };
#define LWIP_ASSERT(message, condition) assert(condition)
#define LOCK_TCPIP_CORE() ((void)0)
#define UNLOCK_TCPIP_CORE() ((void)0)
#define FNTRACE() ((void)0)
static int wsverbose = 0;
typedef struct pbuf { void *payload; struct pbuf *next; uint16_t len; } pbuf;
typedef struct wsock_state_t {
    void *pcb;
    int state0, state1, tcp_state;
    pbuf *request;
} wsock_state_t;
static unsigned char sent[33000];
static size_t sent_size;
static uint16_t htons(uint16_t n) { return (n >> 8) | (n << 8); }
static pbuf *pbuf_alloc(int kind, uint16_t size, int type) {
    (void)kind; (void)type;
    pbuf *p = malloc(sizeof(*p));
    p->payload = malloc(size); p->next = NULL; p->len = size; return p;
}
static void pbuf_free(pbuf *p) { free(p->payload); free(p); }
static err_t wsock_close(wsock_state_t *ws, int result, err_t err) {
    (void)ws; (void)result; assert(0); return err;
}
static err_t altcp_write(void *pcb, const void *bytes, uint16_t size, int flags) {
    (void)pcb; (void)flags; assert(size <= sizeof(sent));
    memcpy(sent, bytes, size); sent_size = size; return ERR_OK;
}
static void altcp_output(void *pcb) { (void)pcb; }
static void wsock_hexdump(void *bytes, uint16_t length) { (void)bytes; (void)length; }
#include "wsock_write_under_test.inc"

int main(void)
{
    const size_t lengths[] = {0,20,125,126,1419,1420,1421,4096,8000};
    for (size_t n = 0; n < sizeof(lengths)/sizeof(lengths[0]); ++n) {
        size_t length = lengths[n];
        wsock_state_t ws = {(void *)1,1,1,WS_TCP_CONNECTED,NULL};
        char text[8000]; memset(text, 'x', sizeof(text));
        assert(wsock_write(&ws, text, (uint16_t)length, OPCODE_TEXT) == ERR_OK);
        assert(sent[0] == (WSHDRBITS_FIN | OPCODE_TEXT));
        assert(sent[1] & WSHDRBITS_MASKED);
        size_t mask = length <= 125 ? 2 : 4;
        size_t declared = length <= 125 ? (sent[1] & 127) : ((sent[2] << 8) | sent[3]);
        assert(declared == length && sent_size == length + mask + 4);
        for (size_t i = 0; i < length; ++i)
            assert((sent[mask+4+i] ^ sent[mask+(i%4)]) == 'x');
        assert(ws.request == NULL);
    }
    puts("PASS: actual wsock_write sets FIN at 1420-byte boundary and for complete 8KB messages");
    return 0;
}
