#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Capture144.h"
#define COUNT 168u
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
typedef struct {
    unsigned configured, started, reads, polls, fail_read, cancel_at;
    int config_ok, start_ok, full;
    uint32_t ticks;
} Mock;
static int Configure(void *user) { Mock *m=user; m->configured++; return m->config_ok; }
static int Start(void *user) { Mock *m=user; m->started++; return m->start_ok; }
static uint32_t Status(void *user) { Mock *m=user; m->polls++; return m->full ? 0x28u : 0u; }
static int Read(void *user, uint16_t *word) {
    Mock *m=user;
    unsigned index=m->reads++;
    if (m->fail_read && m->reads == m->fail_read) return 0;
    *word = index == 0 ? 0xdead : (uint16_t)(index-1);
    return 1;
}
static uint32_t Now(void *user) { Mock *m=user; return m->ticks++; }
static int Cancel(void *user) { Mock *m=user; return m->cancel_at && m->ticks >= m->cancel_at; }
static void Reset(Mock *m) { memset(m,0,sizeof *m); m->config_ok=1; m->start_ok=1; m->full=1; }
int main(void) {
    Mock mock;
    Capture144Ops ops={&mock, Configure, Start, Status, Read, Now, Cancel};
    Capture144Ops bad;
    FpgaProfile w11=FpgaProfile_Decode(0x5731,0x0101,0);
    FpgaProfile w12=FpgaProfile_Decode(0x5731,0x0101,0x0102);
    FpgaProfile unknown=FpgaProfile_Decode(0x5731,0x0101,0x0103);
    uint16_t data[COUNT+1];
    unsigned i;
    Reset(&mock); data[COUNT]=0xbeef;
    CHECK(Capture144_Record(&w11,&ops,data,COUNT,1000)==CAPTURE144_OK);
    CHECK(mock.configured==1 && mock.started==1 && mock.reads==COUNT+1);
    for(i=0;i<COUNT;++i) CHECK(data[i]==i);
    CHECK(data[COUNT]==0xbeef);
    Reset(&mock); mock.ticks=0xffffff80u;
    CHECK(Capture144_Record(&w12,&ops,data,COUNT,1000)==CAPTURE144_OK);
    CHECK(mock.ticks<0xffffff80u); /* Wrapped clock still succeeds. */
    Reset(&mock); mock.full=0;
    CHECK(Capture144_Record(&w11,&ops,data,COUNT,20)==CAPTURE144_TIMEOUT);
    CHECK(mock.reads==0 && mock.polls>0);
    Reset(&mock);
    CHECK(Capture144_Record(&w11,&ops,data,COUNT,20)==CAPTURE144_TIMEOUT);
    CHECK(mock.reads>1 && mock.reads<COUNT+1); /* Timeout during transfer. */
    Reset(&mock); mock.cancel_at=1;
    CHECK(Capture144_Record(&w11,&ops,data,COUNT,1000)==CAPTURE144_CANCELLED);
    CHECK(mock.configured==0 && mock.started==0 && mock.reads==0);
    Reset(&mock); mock.cancel_at=20;
    CHECK(Capture144_Record(&w11,&ops,data,COUNT,1000)==CAPTURE144_CANCELLED);
    CHECK(mock.reads>0 && mock.reads<COUNT+1);
    Reset(&mock); mock.config_ok=0;
    CHECK(Capture144_Record(&w11,&ops,data,COUNT,1000)==CAPTURE144_IO);
    CHECK(mock.started==0 && mock.reads==0);
    Reset(&mock); mock.start_ok=0;
    CHECK(Capture144_Record(&w11,&ops,data,COUNT,1000)==CAPTURE144_IO);
    CHECK(mock.reads==0);
    Reset(&mock); mock.fail_read=1;
    CHECK(Capture144_Record(&w11,&ops,data,COUNT,1000)==CAPTURE144_IO);
    CHECK(mock.reads==1);
    Reset(&mock); mock.fail_read=25;
    CHECK(Capture144_Record(&w11,&ops,data,COUNT,1000)==CAPTURE144_IO);
    CHECK(mock.reads==25);
    Reset(&mock);
    CHECK(Capture144_Record(&unknown,&ops,data,COUNT,1000)==CAPTURE144_UNSUPPORTED);
    CHECK(Capture144_Record(NULL,&ops,data,COUNT,1000)==CAPTURE144_ARGUMENT);
    CHECK(Capture144_Record(&w11,NULL,data,COUNT,1000)==CAPTURE144_ARGUMENT);
    CHECK(Capture144_Record(&w11,&ops,NULL,COUNT,1000)==CAPTURE144_ARGUMENT);
    CHECK(Capture144_Record(&w11,&ops,data,COUNT,0)==CAPTURE144_ARGUMENT);
    CHECK(Capture144_Record(&w11,&ops,data,COUNT,0x80000000u)==CAPTURE144_ARGUMENT);
    CHECK(Capture144_Record(&w11,&ops,data,167,1000)==CAPTURE144_ARGUMENT);
    CHECK(Capture144_Record(&w11,&ops,data,4097,1000)==CAPTURE144_ARGUMENT);
    bad=ops; bad.configure=NULL;
    CHECK(Capture144_Record(&w11,&bad,data,COUNT,1000)==CAPTURE144_ARGUMENT);
    bad=ops; bad.start=NULL;
    CHECK(Capture144_Record(&w11,&bad,data,COUNT,1000)==CAPTURE144_ARGUMENT);
    bad=ops; bad.status=NULL;
    CHECK(Capture144_Record(&w11,&bad,data,COUNT,1000)==CAPTURE144_ARGUMENT);
    bad=ops; bad.read_pair=NULL;
    CHECK(Capture144_Record(&w11,&bad,data,COUNT,1000)==CAPTURE144_ARGUMENT);
    bad=ops; bad.now=NULL;
    CHECK(Capture144_Record(&w11,&bad,data,COUNT,1000)==CAPTURE144_ARGUMENT);
    bad=ops; bad.cancelled=NULL;
    CHECK(Capture144_Record(&w11,&bad,data,COUNT,1000)==CAPTURE144_ARGUMENT);
    CHECK(mock.configured==0 && mock.started==0 && mock.reads==0);
    { FpgaProfile legacy=FpgaProfile_Decode(0x5731,0,0);
      Reset(&mock);
      CHECK(Capture144_RecordFifo(&legacy,&ops,data,COUNT,1000)==CAPTURE144_OK);
      Reset(&mock);
      CHECK(Capture144_Record(&legacy,&ops,data,COUNT,1000)==CAPTURE144_UNSUPPORTED);
      CHECK(mock.configured==0);
    }
    printf("PASS Wildcat 144 acquisition: %u checks (mock bus/clock, no hardware)\n", checks);
    return 0;
}
