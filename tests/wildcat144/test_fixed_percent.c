/* Same LCD across Normal/144, actual native Int2Str for percentages. */
#define REAL_UI_PERCENT
#define main UI144_ExistingTests
#include "test_ui144.c"
#undef main
void Test_NormalFixed(unsigned,unsigned,unsigned);
void Test_NormalPercent(unsigned,unsigned);
u8 ChartLogic(void){return 0;}
static void Compare(unsigned group,unsigned detail,unsigned selected,unsigned percent)
{
    unsigned a,b,n;char expected[64];u16 color,mode;menu *e=&Title[group][detail];
    s16 values[13][4];unsigned g,d;
    for(g=0;g<13;++g)for(d=0;d<4;++d)values[g][d]=Title[g][d].Value;
    Current=(u8)(selected?group:OUTPUT);Detail[group]=(u8)detail;
    draws=formatted=0;
    if(percent)Test_NormalPercent(group,selected);else Test_NormalFixed(group,detail,selected);
    CHECK(draws==1);a=0;strcpy(expected,painted[a].text);
    color=(u16)painted[a].color;mode=(u16)painted[a].mode;
    Draw(Current,Detail[Current],1);b=Find(e->XPOS,e->YPOS,expected);
    CHECK(painted[b].color==color && painted[b].mode==mode);
    n=0;for(a=0;a<draws;++a)if(painted[a].x==e->XPOS && painted[a].y==e->YPOS)++n;
    CHECK(n==1);
    draws=formatted=0;
    if(percent)Test_NormalPercent(group,selected);else Test_NormalFixed(group,detail,selected);
    CHECK(draws==1 && !strcmp(painted[0].text,expected));
    CHECK(painted[0].color==color && painted[0].mode==mode);
    for(g=0;g<13;++g)for(d=0;d<4;++d)CHECK(Title[g][d].Value==values[g][d]);
}
int main(void)
{
    static const unsigned groups[]={V_VERNIE,V_VERNIE,T_VERNIE,T_VERNIE,T_BASE,BK_LIGHT,VOLUME};
    static const unsigned details[]={V1,V2,T1,T2,XPOSI,0,0};
    unsigned p,page,k,selected,blink,value,g,x,y,n;Scope144Measurements m={0};
    for(p=0;p<3;++p)for(page=0;page<2;++page)for(k=0;k<7;++k)
    for(selected=0;selected<2;++selected)for(blink=0;blink<2;++blink) {
        FlagMeter=(u8)p;UpdateMeter=page?4:0;Twink=(u8)blink;
        if(k>=5 && (!p || (p==2 && !page)))continue;
        Compare(groups[k],details[k],selected,0);
    }
    /* Every supply-independent setting, in both visible panel layouts. */
    for(g=BK_LIGHT;g<=VOLUME;++g)for(value=0;value<10;++value)
    for(p=1;p<=2;++p)for(selected=0;selected<2;++selected)for(blink=0;blink<2;++blink) {
        Title[g][1].Value=(s16)value;FlagMeter=(u8)p;UpdateMeter=4;Twink=(u8)blink;
        Compare(g,1,selected,1);
    }
    /* 100 -> 90 -> 10: erase the former digits and unit tail in one pass.
     * Check exact native text and that the neighboring area stays untouched. */
    for(g=BK_LIGHT;g<=VOLUME;++g) {
        unsigned y0=Title[g][1].YPOS;
        FlagMeter=1;UpdateMeter=4;Twink=PRN;Current=(u8)g;Detail[g]=1;
        Title[g][1].Value=9;draws=formatted=0;Test_NormalPercent(g,0);
        Find(342,y0," 100% ");
        for(x=391;x<400;++x)for(y=y0;y<y0+11;++y)screen[x][y]=0x1234;
        Title[g][1].Value=8;draws=formatted=0;Test_NormalPercent(g,0);
        Find(342,y0," 90%  ");
        for(x=383;x<391;++x)for(y=y0;y<y0+11;++y)CHECK(!screen[x][y]);
        for(x=391;x<400;++x)for(y=y0;y<y0+11;++y)CHECK(screen[x][y]==0x1234);
        Draw(g,1,1);Find(342,y0," 90%  ");
        Title[g][1].Value=0;Draw(g,1,1);Find(342,y0," 10%  ");
    }
    /* No light/volume cells on the hidden panel or the other large page. */
    for(p=0;p<=2;p+=2) {
        FlagMeter=(u8)p;UpdateMeter=0;Draw(TRACK1,SOURCE,1);
        for(n=0;n<draws;++n)CHECK(!(painted[n].x>=314 &&
            (painted[n].y==167 || painted[n].y==152)));
    }
    /* Blink changes the selected label without repainting the overview. */
    FlagMeter=1;UpdateMeter=4;Twink=PRN;Draw(V_VERNIE,V1,1);
    {unsigned graphs=graph_calls;
     Twink=INV;draws=formatted=0;Menu144_View(&m,0,0,1,0);
     n=Find(35,0,"!V1!");CHECK(painted[n].mode==INV && graph_calls==graphs);
    }
    Draw(TRACK2,SOURCE,1);CHECK(painted[Find(86,228,"CH(A)")].color==0x0905);
    printf("PASS fixed labels/percent: %u checks; actual native percent formatting, Normal/144 labels/colors/blink, all settings, 100->90, panel pages, no value mutation\n",checks);
    return 0;
}
