"""Extract real small production paths for host regression tests; no device IO."""
from pathlib import Path
import re, sys
root=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1])
def function(text,name):
    match=re.search(r"(?:static )?(?:void|int) "+name+r"\([^)]*\)\s*\{",text)
    if not match: raise ValueError(name)
    return block(text,match.start())
def block(text,start):
    at=text.index('{',start);depth=1;i=at+1
    while depth:
        if text[i]=='{':depth+=1
        elif text[i]=='}':depth-=1
        i+=1
    return text[start:i]
process=(root/'WildcatV6_6/Process.c').read_text(encoding='latin1')
common=(root/'WildcatV6_6/UiCommon.c').read_text(encoding='latin1')
main=(root/'WildcatV6_6/Main.c').read_text(encoding='latin1')
headers='\n'.join('#include "'+h+'"' for h in ['BIOS.h','Menu.h','Draw.h','Process.h','Function.h','Interrupt.h','UiCommon.h','Scope144.h'])+'\n#include <string.h>\n'
init='\n'.join(function(process,n) for n in ['BackGround_Reset','App_HardwareInit','App_init','App_ModeTransition','Process144_ClearFrame'])
analog=common[common.index('static u16 analog_values'):common.index('/* Pure repeat policy.')]
(out/'ui_transition_engine.c').write_text(headers+'extern u8 SelectiveUpdate;\n'+analog+init+'\n'+function(common,'UiCommon_ModeTransition'),encoding='ascii')
start=main.index('BypassUpdate:')
finish=main[start:main.index('if((OSBufferLogic()!=OSStatus))',start)]
handlers=[]
for prefix in ['if(Key_Buffer!=KEY4 && Key_Buffer!=K_INDEX_S && Generator_Key', 'if(Generator_Key(KEY4,0))', 'if(Generator_Key(K_INDEX_S,0))']:
    handlers.append(block(main,main.index(prefix)))
body="""
extern u8 SelectiveUpdate,Update;
int Test_NormalGenerator(unsigned key) {
    Key_Buffer=(u8)key;
    if(key==KEY4){%s return 0;}
    if(key==K_INDEX_S){%s return 0;}
    %s
    return 0;
    %s
    return 1;
}
void Test_LegacyGeneratorFinish(void) {%s}
"""%(handlers[1],handlers[2],handlers[0],finish,finish.replace('BypassUpdate:',''))
(out/'normal_generator_dispatch.c').write_text(headers+body,encoding='ascii')

# Exercise the real Normal title ownership guard and the shared draw dispatch.
# The generic fallback deliberately paints a stale value if ownership regresses.
menu=(root/'WildcatV6_6/Menu.c').read_text(encoding='latin1')
guard=block(menu,menu.index('if(i==OUTPUT && (j==KIND'))
start=menu.index('unsigned gen_dirty=0,file_dirty=0,k;',menu.index('void Display_Title'))
prefix=menu[start:menu.index('u16  i, j, TmpColor;',start)]
finish=block(menu,menu.index('if(gen_dirty){Generator_KindTitle();Generator_FrequencyTitle();}'))
normal_title=headers+'void Test_NormalGeneratorTitle(void) {\n'+prefix
normal_title+='unsigned i=OUTPUT,j;\n(void)file_dirty;\nfor(j=KIND;j<=FRQN;++j) {\n'+guard
normal_title+='\nif(Title[i][j].Flag&UPDAT || (Current==i && Detail[i]==j && Blink)) {\n'
normal_title+='Print_Str(Title[i][j].XPOS,Title[i][j].YPOS,Title[i][j].Color[0],PRN,Title[i][j].Str+10*Title[i][j].Value);\n}\n}\n'
normal_title+=finish+'\n'+re.search(r'if\(gen_dirty && UiCommon_HeaderOwned\(\) && Current!=FILE\)Generator_Draw\(\);',menu).group(0)+'\n}\n'
(out/'normal_generator_title.c').write_text(normal_title,encoding='ascii')

# Native routing now uses the same core after its special pre-navigation.
# The removed original branch is frozen only in a small regression fixture.
reference=(root/'tests/wildcat144/fixtures/k4_detail_before.c').read_text(encoding='latin1')
call=re.search(r'UiCommon_NextDetail\(0\);',main).group(0)
channel=headers+reference+'\nint Test_NormalChannelDetail(void) { return '+call+' }\n'
channel+='void Test_LegacyChannelDetail(void) { Test_LegacyDetailTail(); }\n'
start=main.index('if(Current < METER_0) {',main.index('if(Key_Buffer== KEY4)'))
tail=block(main,start);end=start+len(tail)
assert re.match(r'\s*else\s*\{',main[end:])
tail+=block(main,end)
channel+='void Test_NormalDetailTail(void) {\n'+tail+'\n}\n'
pstart=main.index('if ((Current == TRACK3)&&(_Det == 0))',main.index('if(Key_Buffer== KEY4)'))
prelude=main[pstart:start]
channel+='int Test_NormalDetailPrelude(void) {\n'+prelude+'\nreturn 0;\nbypasslongpress: return 1;\n}\n'
(out/'normal_channel_detail.c').write_text(channel,encoding='ascii')

# Native production call sites, plus the real native numeric formatter. This
# test does not substitute a new implementation for Int2Str's percent rules.
fixed=re.findall(r'UiCommon_FixedTitle\(i,j,(PRN|Twink)\);',menu)
percent=re.findall(r'UiCommon_PercentTitle\(i,(PRN|Twink)\);',menu)
assert fixed==['PRN','Twink'] and percent==['PRN','Twink']
labels=headers+'void Test_NormalFixed(unsigned i,unsigned j,unsigned selected) {\n'
labels+='if(selected) '+re.search(r'UiCommon_FixedTitle\(i,j,Twink\);',menu).group(0)
labels+='\nelse '+re.search(r'UiCommon_FixedTitle\(i,j,PRN\);',menu).group(0)+'\n}\n'
labels+='void Test_NormalPercent(unsigned i,unsigned selected) {\n'
labels+='if(selected) '+re.search(r'UiCommon_PercentTitle\(i,Twink\);',menu).group(0)
labels+='\nelse '+re.search(r'UiCommon_PercentTitle\(i,PRN\);',menu).group(0)+'\n}\n'
f=(root/'WildcatV6_6/Function.c').read_text(encoding='latin1')
labels+=block(f,f.index('u32 Power('))+'\n'
# Preserve the production function byte-for-byte apart from its symbol name.
# Its existing mixed-sign comparisons are accepted by the firmware's -Wall;
# the host suite additionally uses -Wextra. Scope the waiver to this legacy code.
labels+='#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wsign-compare"\n'
labels+=function(f,'Int2Str').replace('void Int2Str(', 'void Native_Int2Str(')+'\n'
labels+='#pragma GCC diagnostic pop\n'
(out/'normal_fixed_percent.c').write_text(labels,encoding='latin1')

# Cursor tests share the same frozen original tail instead of retaining a
# second copy in production or depending on the newly deduplicated source.
(out/'normal_cursor_detail.c').write_text(headers+'void Test_LegacyDetailTail(void);\nvoid Test_LegacyCursorDetail(void) { Test_LegacyDetailTail(); }\n',encoding='ascii')

# Extract the real statements preceding short/long K4 dispatch. Instrument
# both macro operands so a meter selection fails before an out-of-bounds read.
release='if((__Get(KEY_STATUS)& KEY4_STATUS)!=0){'
kstart=main.index('if(Key_Buffer== KEY4)')
short=main.index(release,kstart)+len(release)
long=main.index(release,short)+len(release)
prefixes=[main[short:main.index('if(NativeTriggerMenu(KEY4))',short)].strip(),
          main[long:main.index('if((_4_source>11)',long)].strip()]
assert all(p.count('_Curr[_Det].Flag')==1 for p in prefixes)
key4=headers+'menu *Test_CheckedTitle(void);\nu8 Test_CheckedDetail(void);\n'
key4+='#undef _Curr\n#define _Curr Test_CheckedTitle()\n#undef _Det\n#define _Det Test_CheckedDetail()\n'
for name,prefix in zip(['Short','Long'],prefixes):
    key4+='void Test_K4'+name+'Start(void) {\n'+prefix+'\n}\n'
(out/'k4_release_prefix.c').write_text(key4,encoding='ascii')

# Real IRQ publication and the common holdoff predicate/view ownership.
interrupt=(root/'WildcatV6_6/Interrupt.c').read_text(encoding='latin1')
publish=block(interrupt,interrupt.index('if(KeyCode !=0)'))
(out/'key1_publish.c').write_text(headers+'void Test_KeyPublish(u8 KeyCode) {\n'+publish+'\n}\n',encoding='ascii')
draw=(root/'WildcatV6_6/Draw.c').read_text(encoding='latin1')
overview=draw[draw.index('static unsigned overview_known'):draw.index('void Update_View_Area(void)')]+function(draw,'Update_View_Area').replace('void Update_View_Area(', 'void Test_PaintOverview(')
guard=block(menu,menu.index('if(i==TRIGG && j==3 && !NativeHoldoffAvailable())'))
printer=block(menu,menu.index('if (!((i==TRIGG)&&(j==3)&&((NotificationTimer>0)||(Current==FILE))))'))
holdoff=headers+'#include \"NativeWindow.h\"\nextern const u16 RULE_BASE[8];\nvoid DrawPixel(u16,u8,u16);\n'+function(menu,'NativeHoldoffAvailable')+'\n'+overview+'\n'
holdoff+='void Update_View_Area(void) { Test_PaintOverview(); }\n'
holdoff+='void Test_HoldoffTitle(void) {unsigned i=TRIGG,j=3;do {\n'+guard+'\n'+printer+'\n}while(0); }\n'
# Exercise the actual shared trace-boundary helper in the existing painter test.
trace_start=draw.rfind('\nint ',0,draw.index('Draw_TraceSegment('))+1
holdoff+='\n'+block(draw,trace_start)+'\n'
(out/'holdoff_view.c').write_text(holdoff,encoding='ascii')

# Extract actual native numeric stepping and the CFG meter decoding loop.
# The retired measurement slot must not change any other saved item ID.
parts=[]
for marker,end_marker in [
    ('if(Meter[Current-METER_0].Item >VBT)', 'if(Meter[Current-METER_0].Item == TL)'),
    ('if(Meter[Current-METER_0].Item < TL)', 'if((Meter[Current-METER_0].Track>1)&&(Meter[Current-METER_0].Track<4))')]:
    at=main.index(marker)
    begin=main.rfind('Meter[Current-METER_0].Flag |= UPDAT;',0,at)
    end=main.index(end_marker,at)
    end+=len(block(main,end))
    parts.append(main[begin:end])
files=(root/'WildcatV6_6/Files.c').read_text(encoding='latin1')
at=files.index('    Meter[i].Item     = *p;')
begin=files.rfind('for(i=0; i<9; i++){',0,at)
decode=block(files,begin)
selection=headers+'#ifndef NORMAL_EDGE_ONLY\nextern u8 SaveCurrent[2],UpdateMeter;\n'
selection+='void Test_NormalMeterStep(int delta) {if(delta<0) {\n'+parts[0]+'\n}else{\n'+parts[1]+'\n}}\n'
selection+='void Test_LoadMeterItems(u16 *p) {unsigned i;const u8 reserved_cfg_item=1;\n'+decode+'\n}\n'
(out/'meter_selection.c').write_text(selection+'\n#endif\n',encoding='ascii')

# Preserve the real main-loop paint gate; an overlay must not consume an
# overview change that has not yet reached the LCD.
at=main.index('if(Current!=FILE && !NotificationTimer && Process_NativeOverviewChanged())')
gate=main[at:main.index('Update_Buffer_Overview();',at)+len('Update_Buffer_Overview();')]
(out/'overview_refresh_gate.c').write_text(headers+'int Process_NativeOverviewChanged(void);\nvoid Test_NormalOverviewTick(void) {\n'+gate+'\n}\n',encoding='ascii')

# Real native label consumers, including the unchanged large-meter renderer.
labels=headers+'extern uc16 Y_INV[5];\n'
labels+=block(menu,menu.index('void Display_Meter(')).replace('void Display_Meter(', 'void Test_NormalMeterLabels(')+'\n'
labels+=function(menu,'DisplayBigMeter').replace('void DisplayBigMeter(', 'void Test_NormalBigMeter(')+'\n'
labels+='extern u8 BTwinkState;\nvoid Test_MeterValue(u8);\n'
labels+=function(main,'UpdateBigMeters').replace('void UpdateBigMeters(', 'void Test_NormalBigRefresh(').replace('Display_Value(', 'Test_MeterValue(')+'\n'
(out/'meter_label_renderers.c').write_text(labels,encoding='latin1')
# Serializing measurement IDs is identical in the boot and numbered settings.
needle='*p++ =(Meter[i].Track<<8)+ Meter[i].Item;'
stores=[]
for match in re.finditer(re.escape(needle),files):
    begin=files.rfind('for(i=0; i<9; i++){',0,match.start())
    stores.append(block(files,begin))
assert len(stores)==2
(out/'meter_store.c').write_text(headers+'void Test_StoreMeterItems(u16 *p) {unsigned i;\n'+stores[0]+'\n}\n'+'void Test_StoreBootMeterItems(u16 *p) {unsigned i;\n'+stores[1]+'\n}\n',encoding='ascii')

# Real Normal XPOS call sites and unchanged interpolation conversions.
xpos=[]
for direction in [-1,1]:
    marker='int step=UiCommon_XposStep(XposRef,%d,'%direction
    at=main.index(marker)
    begin=main.rfind('{ unsigned reference_units;',0,at)
    finish=main.index('XposRef=GetXposRef(_Curr[_Det].Value);',at)+len('XposRef=GetXposRef(_Curr[_Det].Value);')
    xpos.append(main[begin:finish])
conversions=''
for name in ['GetXposRef','GetXpos']:
    conversions+=block(process,process.index('u16 '+name+'('))+'\n'
body=headers+'extern u16 XposRef;\n'+conversions
body+='int Test_NormalXpos(int direction) {if(direction<0){\n'+xpos[0]+'\n}else{\n'+xpos[1]+'\n}return 0;\nbypasslongpress:return 1;\n}\n'
(out/'normal_xpos.c').write_text(body,encoding='ascii')

# Real Native FIFO transfer + Process selection/interpolator/publication paths.
# Values are irrelevant to this metadata test; the FIFO and control flow are
# real extracted production functions, without running the display/meters.
flow=headers+'#include "NativeWindow.h"\nu8 ADCoffset=40;\nextern u8 native_buffer_pending,BufferRestore,CalibrateMode,OsTransferCompleteFlag,SerialStatus;\n'
flow+=process[process.index('static s16 native_command_y'):process.index('void Process144_ReleaseBuffer(')].replace(',native_delta_y[2]','')+'\n'
flow+=function(process,'Process144_NativePending')+'\n'
start=process.index('static unsigned NativeViewFormat(void)')
flow+=block(process,start)+'\n'+function(process,'NativeViewUnsupported')+'\n'
flow+=function(process,'Process_NativeOverview')+'\n'+function(process,'Process_NativeOverviewChanged')+'\n'
flow+='extern u32 ReverseBitMask,BitMask;\n'
flow+=block(process,process.index('void TransferFIFO(s16 i,u8 service)'))+'\n'
start=process.index('  for(i=0; i <bag_max_buf; i++){',process.index('void Process(void)'))
transfer=process[start:process.index('      V[A]=(DataBuf[i]&0xFF)-',start)]
interp=block(process,process.index('while(j > 0 )',start))
start=process.index('NativeWindowInput window;',start)
publish=process[start:process.index('NativeClipPublish(native_clip_good);',start)+len('NativeClipPublish(native_clip_good);')]
start=process.index('   if (_T_base < 17){')
blank=process[start:process.index('   for(jj=0; jj<discard; jj++){',start)]
flow+='void Test_NativeProcess(int Shift) {\n'
flow+='s16 i,j=0,k,n=0,V[8]={0},OsA=0,OsB=0,OsA_=0,OsB_=0;u8 C_D=0;\n'
flow+='s16 view_origin; s32 Tmp=0;\n'
flow+='k=((1024-TempKp1)*150+512)/1024+_X_posi.Value;view_origin=k;\n'
flow+=transfer+'\nif(i>=k){\n'+interp+'\nj+=1024;V[A_]=V[A];V[B_]=V[B];OsA_=OsA;OsB_=OsB;}\n}\n'
flow+=blank+'\n{'+publish+'}\n}\n'
(out/'native_overview_flow.c').write_text(flow,encoding='ascii')

# The real post-key refresh must not turn a small XPOS move into a full bar.
finish=block(main,main.index('if((Current != FILE)&&(NotificationTimer<43))'))
(out/'overview_key_finish.c').write_text(headers+'void Test_NormalOverviewFinish(void) {\n'+finish+'\n}\n',encoding='ascii')

# Only the new metadata guard is extracted here; hardware configuration order
# and all original Update_Base register operations remain untouched.
guard='if(_Status == RUN)NativeWindow_Reset();'
assert process.count(guard)==1
with (out/'native_overview_flow.c').open('a',encoding='ascii') as f:
    f.write('void Test_NativeBaseMetadata(void){if(_Mode!=SCAN){'+guard+'}}\n')

# Shared demand and the real native edge/average paths; same existing output.
edge_begin=process.index('    if((CurDefTime==0)||((i>start-1)&&(i<(finish+1))))')
edge_end=process.index('    if((JComp)&&(!OsBufferLogicFlag))',edge_begin)
normal_edges='void Test_NormalEdges(const uint8_t *values,unsigned count,unsigned native_time_demand,unsigned CurDefTime,unsigned begin,unsigned end,unsigned *out)\n{\n    int i,bag_max_buf=(int)count,finish=(int)end,start=(int)begin;\n    int8_t Sa=2,Sb=2,Sc=2,Sd=2;\n    uint16_t Ta=0,Tb=0,Tc=0,Td=0,Pa=0,Pb=0,Pc=0,Pd=0;\n    uint16_t TaS=0,TbS=0,TcS=0,TdS=0,PaS=0,PbS=0,PcS=0,PdS=0,TaN=0,TbN=0,TcN=0,TdN=0,QStart[2]={0};\n    uint8_t Ch[2],C_D,FirstA_Mid=0,LastA_Mid=0,FirstB_Mid=0,LastB_Mid=0;\n    int a_Mid_H=130,a_Mid_L=126,b_Mid_H=140,b_Mid_L=136;\n    (void)native_time_demand;\n    for(i=0;i<(int)count;++i){Ch[0]=values[3*i];Ch[1]=values[3*i+1];C_D=values[3*i+2];\n'+process[edge_begin:edge_end].replace("Ch[A]","Ch[0]").replace("Ch[B]","Ch[1]")+'\n    }\n    out[0]=TaS-Ta;out[1]=PaS;out[2]=TaN;out[3]=QStart[0];out[4]=TaS;out[5]=FirstA_Mid;out[6]=LastA_Mid;\n    out[7]=TbS-Tb;out[8]=PbS;out[9]=TbN;out[10]=QStart[1];out[11]=TbS;out[12]=FirstB_Mid;out[13]=LastB_Mid;\n    out[14]=TcS-Tc;out[15]=PcS;out[16]=TcN;\n    out[17]=TdS-Td;out[18]=PdS;out[19]=TdN;\n}\n'
native_demand=block(process,process.index("static unsigned NativeTimeDemand("))
with (out/"meter_selection.c").open("a",encoding="ascii") as f:
    f.write(normal_edges+"\n#ifndef NORMAL_EDGE_ONLY\nextern u8 SerialStatus,CalibrateMode;\n"+native_demand.replace("static unsigned NativeTimeDemand(","unsigned Test_NormalTimeDemand(") + "\n#endif\n")

# Verify the unchanged voltage averaging separately from time-demand gating.
avg=block(process,process.index('void Average(u8 Ch){'))
avg=avg[avg.index('{')+1:avg.rindex('}')]
vr=block(process,process.index('s32 VRunAvg(u8 Ch, s32 Value, s32 Sum){'))
vr=vr[vr.index('{')+1:vr.rindex('}')]
rr=block(process,process.index('void RunAvg(u8 Ch){'))
rr=rr[rr.index('{')+1:rr.rindex('}')]
helper='static int32_t AvgVolt(unsigned Ch,int32_t Value,int32_t Sum,uint16_t *VNAvg){'+vr+'}\n'
helper+='static void AvgRun(unsigned Ch,uint32_t UTmp,uint32_t *Sum,uint16_t *NAvg){'+rr+'}\n'
prefix='\n unsigned i;uint32_t Sum[4],PxS[4],TxS[4],TxN[4],UTmp=1000003;\n uint16_t NAvg[4],VNAvg[2];int32_t VxAvg[2],VxSsq[2];\n int32_t a_Avg=-10000,b_Avg=50000,a_Ssq=700000,b_Ssq=800000;\n unsigned base=mode==1?21:mode?8:0,JumpCnt=mode==2?4096:0,FrameMode=mode>1;\n unsigned PaS=11,TaS=33,TaN=2,PbS=17,TbS=47,TbN=3,PcS=19,TcS=53,TcN=5,PdS=23,TdS=71,TdN=7;\n for(i=0;i<4;++i){Sum[i]=out[i];NAvg[i]=(uint16_t)out[4+i];PxS[i]=out[8+i];TxS[i]=out[12+i];TxN[i]=out[16+i];}\n for(i=0;i<2;++i){VNAvg[i]=(uint16_t)out[20+i];VxAvg[i]=(int32_t)out[22+i];VxSsq[i]=(int32_t)out[24+i];}\n'
suffix='\n for(i=0;i<4;++i){out[i]=Sum[i];out[4+i]=NAvg[i];out[8+i]=PxS[i];out[12+i]=TxS[i];out[16+i]=TxN[i];}\n for(i=0;i<2;++i){out[20+i]=VNAvg[i];out[22+i]=(uint32_t)VxAvg[i];out[24+i]=(uint32_t)VxSsq[i];}\n}\n'
avg=avg.replace('_T_base','base').replace('get_bag_max_buf()','4096')
avg=avg.replace('RunAvg(Ch)','AvgRun(Ch,UTmp,Sum,NAvg)')
for ch,val,dest in [(0,'a_Avg','VxAvg[0]'),(0,'a_Ssq','VxSsq[0]'),(1,'b_Avg','VxAvg[1]'),(1,'b_Ssq','VxSsq[1]')]:
    avg=avg.replace('VRunAvg(%u,%s,%s)'%(ch,val,dest),'AvgVolt(%u,%s,%s,VNAvg)'%(ch,val,dest))
with (out/'meter_selection.c').open('a',encoding='ascii') as f:
    f.write(helper)
    for name,body in [('Test_NormalAverage',avg),('Legacy_NormalAverage',avg.replace('if(native_time_demand&(1u<<Ch))',''))]:
        f.write('void '+name+'(unsigned native_time_demand,unsigned Ch,unsigned mode,uint32_t *out){'+prefix+'(void)native_time_demand;'+body+suffix)

# Actual ADC default and BootLoad decoding, all marker bytes exercised in C.
files=(root/'WildcatV6_6/Files.c').read_text(encoding='latin1')
start=files.index('    if(BootLoad){')
adc_load=block(files,start)
default=re.search(r'u8  ADCoffset=(\d+);',process).group(1)
with (out/'ui_transition_engine.c').open('a',encoding='ascii') as stream:
    stream.write('\nu8 ADCoffset='+default+';\n')
    stream.write('void Test_AdcLoad(u16 packed,unsigned BootLoad) {u16 *p=&packed;u8 Sum;'+adc_load+'}\n')

# Actual early Normal service dispatch, before generator/value handlers.
service_key=block(common,common.index('int __attribute__((optimize("Os"))) UiCommon_ServiceKey'))
service_route=block(main,main.index('if(OffsetSelect && !((Current==T_BASE)'))
with (out/'ui_transition_engine.c').open('a',encoding='ascii') as stream:
    stream.write('\ns16 PPM_Comp;\n'+service_key+'\n')
    stream.write('int Test_NormalServiceKey(unsigned key) {Key_Buffer=(u8)key;'+service_route.replace('goto bypassOSselect;','return 1;')+'return 0;}\n')

# Real native CFG save and full serializer for the existing file-IO test.
cfg_headers='\n'.join('#include "'+h+'"' for h in
    ['BIOS.h','File.h','Menu.h','Process.h','Calibrat.h','Scope144.h','UiCommon.h','Interrupt.h','Function.h'])+'\n'
store_body=block(files,files.index('void storeParameter(void)\n{'))
used=set(re.findall(r'\b[A-Za-z_]\w*\b',store_body))
header_text='\n'.join(p.read_text(encoding='latin1') for p in (root/'WildcatV6_6/inc').glob('*.h'))
header_text=re.sub(r'/\*.*?\*/|//[^\n]*','',header_text,flags=re.S)
definitions=[];defined=set()
for declaration in re.findall(r'\bextern\s+([^;]+);',header_text):
    if '(' in declaration or '{' in declaration:continue
    names={n for n in used if re.search(r'\b'+re.escape(n)+r'\s*(?:\[|,|$)',declaration.strip())}
    if not names or names&defined or names&{'SecBuff','Versions'}:continue
    definitions.append(declaration+';');defined|=names
cfg=cfg_headers+'#include <string.h>\nu8 InitFileSystem(void);\nvoid storeParameter(void);\nu8 WriteFileSec(u8 *,u16 *,u32 *);\n'+'\n'.join(definitions)+'\n'
cfg+=store_body.replace('void storeParameter(void)','void Test_RealStore(void)')+'\n'
cfg+=block(files,files.index('u8 Make_Filename('))+'\n'
cfg+=block(files,files.index('u8 Save_Param(u8 FileNum)'))+'\n'
(out/'cfg_save.c').write_text(cfg,encoding='latin1')

# Confirm the actual legacy confirmation text, color, coordinate and delay.
normal_message=headers+function(files,'FileMessage').replace('void FileMessage(', 'void Test_NormalFileMessage(')
normal_message=normal_message.replace('void Test_NormalFileMessage', 'extern const char ErrorCode[5][6];\nvoid Test_NormalFileMessage',1)
(out/'normal_file_message.c').write_text(normal_message,encoding='ascii')

# Same production Normal hardware path as ARM, with BIOS mocked by the UI test.
vertical=headers+'#include "Calibrat.h"\nstatic s16 native_command_y[2];\ns8 InterpolateS8(u8,s8,s8);\nu16 InterpolateU16(u8,u16,u16);\n'
vertical+=function(process,'SetOffset').replace('void SetOffset(', 'void Test_NormalSetOffset(')+'\n'
vertical+=function(process,'BatLevelCompensation').replace('void BatLevelCompensation(', 'void Test_NormalBattery(')+'\n'
for signature in ['s8 InterpolateS8(', 'u16 InterpolateU16(']:
    vertical+=block(process,process.index(signature,process.index('void BatLevelCompensation(void){')))+'\n'
(out/'normal_vertical.c').write_text(vertical,encoding='ascii')

vertical_offset=headers+'#include "Calibrat.h"\nextern u8 ADCoffset,LastA_Mid,LastB_Mid,FirstA_Mid,FirstB_Mid;\nextern s16 A_Posi,B_Posi;\n'
# CSVdata has an s32 signature, so use the same brace extractor.
vertical_offset+=block(files,files.index('s32 CSVdata(u8 Ch,u8 Inv,u16 i,u8 data){'))+'\n'
vertical_offset+=block(process,process.index('s16 QParam(u8 Ch, u16 Position,u8 service){'))+'\n'
(out/'normal_offset_export.c').write_text(vertical_offset,encoding='ascii')

# Test the actual shared Normal marker glyphs/painter on the persistent LCD.
draw=(root/'WildcatV6_6/Draw.c').read_text(encoding='latin1')
glyph=re.search(r'uc8\s+Mark_TAB\[5\]\[7\]\s*=.*?;',draw,re.S).group(0)
marks=headers+'extern s32 Tmp2;\nextern vu16 ConfNameTimer;\ns32 ScaleXposi(void);\nvoid DrawBase(u16,u16);\nvoid PrintConfigName(void);\n'+glyph+'\n'
marks+=block(draw,draw.index('void Draw_Mark(u16')).replace('Draw_Mark(', 'Test_NormalMark(')+'\n'
marks+=block(draw,draw.index('void Update_Mark(void)')).replace('Update_Mark(', 'Test_NormalMarks(').replace('Draw_Mark(', 'Test_NormalMark(')+'\n'
(out/'normal_marks.c').write_text(marks,encoding='latin1')

# Real completed-frame projection and pending-frame YPOS preview; generated only.
preview=headers+'#include "Frame144.h"\n'
preview+=block(draw,draw.index('int __attribute__((noinline,section(".text.trace_segment")))'))+'\n'
start=process.index('int Process144_Preview(')
end=process.index('/* Shared Normal/interlace transform',start)
preview+=process[start:end]
(out/'preview_renderer.c').write_text(preview,encoding='latin1')

(out/'hold_p0_entry.c').write_text("""#define main NativeService_ExistingTests
#include "test_native.c"
#undef main
int main(void){unsigned i;
 CHECK(VirtualAlloc((void *)0x40000000,0x30000,0x3000,4)==(void *)0x40000000);
 StartupImageCheck();xattributes[_100uS].PSC=0;xattributes[_100uS].ARR=239;
 for(i=0;i<8;++i){strcpy(attributes[i].STR,"1V");attributes[i].SCALE=40000;}
 WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0x0102);Wildcat144_CheckImage();HoldP0();return 0;}
""",encoding='ascii')
