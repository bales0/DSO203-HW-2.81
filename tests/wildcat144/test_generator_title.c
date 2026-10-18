/* Persistent LCD regression: actual shared painter, actual 144 title path,
 * and Normal's extracted production ownership/dispatch (not the whole menu). */
#define main UI144_ExistingTests
#include "test_ui144.c"
#undef main
void Test_NormalGeneratorTitle(void);
vu8 Blink;
static void NormalTitle(void)
{
    unsigned d;draws=formatted=0;
    for(d=0;d<4;++d)Title[OUTPUT][d].Flag|=UPDAT;
    Test_NormalGeneratorTitle();
}
static unsigned Frequency(void)
{
    unsigned i,n=0,found=0;
    for(i=0;i<draws;++i)if(painted[i].x==239 && painted[i].y==216){found=i;++n;}
    CHECK(n==1);return found;
}
static void BlankFrequency(void)
{
    unsigned x,y,n=Frequency();
    CHECK(strspn(painted[n].text," ")==strlen(painted[n].text));
    CHECK(painted[n].mode==PRN);
    for(x=239;x<288;++x)for(y=216;y<227;++y)CHECK(screen[x][y]==0);
}
int main(void)
{
    unsigned kind,rate,focus,blink,n,m,x,y;char text[64];u16 color,mode;
    static const unsigned steps[]={0,9,18,22};
    strcpy(rates[0],"! 1Hz !");strcpy(rates[9]," 1KHz ");
    strcpy(rates[18]," 1MHz ");strcpy(rates[22]," 8MHz ");
    FlagMeter=0;Sweep=0;
    for(kind=0;kind<9;++kind)for(rate=0;rate<4;++rate)
    for(focus=0;focus<3;++focus)for(blink=0;blink<2;++blink) {
        _Kind=(s16)kind;_Frqn=(s16)steps[rate];
        Current=focus?OUTPUT:TRACK1;Detail[OUTPUT]=focus==2?FRQN:KIND;Twink=(u8)blink;
        /* Stale text must be erased using the common field width, even OFF. */
        for(x=239;x<288;++x)for(y=216;y<227;++y)screen[x][y]=0x1234;
        NormalTitle();n=Frequency();strcpy(text,painted[n].text);
        color=(u16)painted[n].color;mode=(u16)painted[n].mode;
        if(kind==6)BlankFrequency();
        else if(kind==8)CHECK(!strncmp(text,"UART",4));
        else CHECK(!strcmp(text,rates[steps[rate]]));
        CHECK(mode==(kind!=6 && focus==2?blink:PRN));
        CHECK(_Frqn==(s16)steps[rate] && _Kind==(s16)kind);
        /* Enter/leave 144 on the same LCD; exactly one frequency writer. */
        Draw(Current,Detail[Current],1);m=Frequency();
        CHECK(!strcmp(text,painted[m].text));
        CHECK(color==painted[m].color && mode==painted[m].mode);
        if(kind==6)BlankFrequency();
        NormalTitle();n=Frequency();
        CHECK(!strcmp(text,painted[n].text));
        CHECK(color==painted[n].color && mode==painted[n].mode);
        CHECK(_Frqn==(s16)steps[rate] && _Kind==(s16)kind);
    }
    /* Active -> OFF -> active: retain the rate, erase all old digits, and
     * return the same frequency. Blink on OFF must not fill a selection box. */
    for(rate=0;rate<4;++rate) {
        _Kind=3;_Frqn=(s16)steps[rate];Current=OUTPUT;Detail[OUTPUT]=FRQN;Twink=INV;
        NormalTitle();n=Frequency();strcpy(text,painted[n].text);
        _Kind=6;NormalTitle();BlankFrequency();CHECK(_Frqn==(s16)steps[rate]);
        Draw(OUTPUT,FRQN,1);BlankFrequency();CHECK(_Frqn==(s16)steps[rate]);
        _Kind=3;Draw(OUTPUT,FRQN,1);n=Frequency();CHECK(!strcmp(text,painted[n].text));
        NormalTitle();n=Frequency();CHECK(!strcmp(text,painted[n].text));
    }
    /* Blink without any value change must use the same owner and must not
     * reveal an OFF rate or invert its blank field. Other menus leave GEN alone. */
    for(kind=0;kind<9;++kind) {
        _Kind=(s16)kind;_Frqn=18;Current=OUTPUT;Detail[OUTPUT]=FRQN;
        for(blink=0;blink<2;++blink) {
            unsigned d;
            for(d=0;d<4;++d)Title[OUTPUT][d].Flag=0;
            Twink=(u8)blink;Blink=1;draws=formatted=0;
            Test_NormalGeneratorTitle();Frequency();CHECK(!Blink);
            if(kind==6)BlankFrequency();
        }
        Current=TRACK1;Blink=1;draws=formatted=0;
        Test_NormalGeneratorTitle();CHECK(!draws);
    }
    /* Normal and 144 share one auxiliary cell painter: waveform amplitude,
     * PWM duty, ARB points, UART parameters, disabled details and OFF blanks. */
    for(kind=0;kind<9;++kind)for(focus=0;focus<4;++focus)for(blink=0;blink<2;++blink) {
        _Kind=(s16)kind;Current=OUTPUT;Detail[OUTPUT]=(u8)focus;
        _Duty=15000;Title[OUTPUT][OUTATT].Value=50;Twink=(u8)blink;GenAdjustMode=PWAdjustMode=0;
        for(x=172;x<237;++x)for(y=216;y<227;++y)screen[x][y]=0x1234;
        NormalTitle();n=Find(172,216,painted[draws-1].text);
        strcpy(text,painted[n].text);color=(u16)painted[n].color;mode=(u16)painted[n].mode;
        CHECK(strlen(text)==8);
        Draw(OUTPUT,focus,1);m=Find(172,216,text);
        CHECK(color==painted[m].color && mode==painted[m].mode);
        NormalTitle();n=Find(172,216,text);
        CHECK(color==painted[n].color && mode==painted[n].mode);
    }
    /* Replacing a long UART label with OFF erases all eight cells. */
    _Kind=8;Current=OUTPUT;Detail[OUTPUT]=FRQN;NormalTitle();
    _Kind=6;Detail[OUTPUT]=KIND;NormalTitle();Find(172,216,"");
    Draw(OUTPUT,KIND,1);Find(172,216,"");
    for(x=172;x<237;++x)for(y=216;y<227;++y)CHECK(!screen[x][y]);
    /* Shorter active strings must also erase the tail of a previous label. */
    strcpy(rates[0],"1Hz");_Kind=8;NormalTitle();
    _Kind=0;_Frqn=0;NormalTitle();n=Frequency();CHECK(!strcmp(painted[n].text,"1Hz   "));
    for(x=264;x<288;++x)for(y=216;y<227;++y)CHECK(!screen[x][y]);
    printf("PASS generator title: %u checks; Normal/144 colors and selection, OFF clearing, retained rate, persistent LCD, one writer\n",checks);
    return 0;
}
