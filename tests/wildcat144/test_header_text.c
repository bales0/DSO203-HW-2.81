/* Compare the shared FILE/144 cell against its original eight-cell layout. */
#define NORMAL_TRIGGER_FIXTURE
#define main UI144_ExistingTests
#include "test_ui144.c"
#undef main
#include "File.h"
void Test_NormalFileMessage(u8);
static unsigned message_delay;
void Delayms(u16 ms){message_delay=ms;}
const char ErrorCode[5][6]={" OK  "," FULL"," NONE"," ERR"," CNCL"};
static int active;
int Wildcat144_Active(void){return active;}
int main(void)
{
    unsigned length,selected,blink,color,x,y,i,n;char text[40],expected[9];
    static const unsigned statuses[]={OK,OVER,NEW,RD_ERR,WR_ERR,UI_FILE_BUSY,200};
    draws=0;UiFile_Begin();CHECK(draws==1);
    CHECK(painted[0].x==230 && painted[0].y==0 && painted[0].mode==PRN);
    CHECK(painted[0].color==((SCRN<<8)+TEXT1) && !strcmp(painted[0].text," >   "));
    _Kind=6;Current=FILE;Title[FILE][0].Value=SAVE;Title[FILE][2].Value=CSV;
    for(length=0;length<sizeof text;++length)for(selected=0;selected<2;++selected)
    for(blink=0;blink<2;++blink)for(color=0;color<3;++color) {
        memset(text,'x',length);text[length]=0;
        n=length<8?length:8;memset(expected,' ',8);memcpy(expected,text,n);expected[8]=0;
        for(x=173;x<237;++x)for(y=216;y<227;++y)screen[x][y]=0x1234;
        draws=0;Twink=(u8)blink;
        UiCommon_HeaderText((u16)(0x0405+color*256u),selected,text);
        CHECK(draws==1);i=Find(172,216,expected);
        CHECK(painted[i].color==0x0405+color*256u);
        CHECK(painted[i].mode==(selected?blink:PRN));
        for(x=173+n*8;x<237;++x)for(y=216;y<227;++y)CHECK(!screen[x][y]);
    }
    /* The 144 service calls this same legacy FileMessage after its save.
     * No result may replace the generator field at y=216. */
    Current=FILE;Title[FILE][0].Value=SAVE;Title[FILE][2].Value=CFG;Title[FILE][1].Value=7;
    for(i=0;i<sizeof statuses/sizeof statuses[0];++i) {
        active=1;UiFile_Result(statuses[i]);Draw(FILE,1,1);
        Find(126,0,"Save  File");Find(206,0,"007");Find(230,0,".CFG ");
        for(n=0;n<draws;++n)CHECK(painted[n].x!=172 || painted[n].y!=216 ||
            !strstr(painted[n].text,UiCommon_Message()));
        draws=0;message_delay=0;Test_NormalFileMessage((u8)statuses[i]);
        CHECK(draws==1 && painted[0].x==230 && painted[0].y==0 && painted[0].color==0x050a && painted[0].mode==PRN);
        CHECK(!strcmp(painted[0].text,ErrorCode[statuses[i]==0?0:statuses[i]==OVER?1:statuses[i]==NEW?2:statuses[i]==200?4:3]));
        CHECK(message_delay==900);
        Draw(FILE,1,1);Find(230,0,".CFG "); /* original title replaces the transient result */
    }
    /* Punctuation, native prefix, selection and filename changes use the
     * same Title strings in both modes. No stale status/number tail remains. */
    for(length=0;length<2;++length)for(selected=0;selected<3;++selected)for(blink=0;blink<2;++blink) {
        active=(int)length;Current=FILE;Detail[FILE]=(u8)selected;Twink=(u8)blink;
        Title[FILE][0].Value=SAVE;Title[FILE][2].Value=CFG;
        for(i=0;i<256;i+=17) {
            Title[FILE][1].Value=(s16)i;draws=0;UiFile_Draw();
            n=Find(126,0,"Save  File");CHECK(painted[n].mode==(selected==0?blink:PRN));
            expected[0]=(char)('0'+i/100);expected[1]=(char)('0'+i/10%10);expected[2]=(char)('0'+i%10);expected[3]=0;
            n=Find(206,0,expected);CHECK(painted[n].mode==(selected==1?blink:PRN));
            n=Find(230,0,".CFG ");CHECK(painted[n].mode==(selected==2?blink:PRN));
        }
    }
    printf("PASS shared header: %u checks; original padding/truncation/colors/blink, native Save File/extension, original bottom-row result and 900ms delay, persistent LCD; no device IO\n",checks);
    return 0;
}
