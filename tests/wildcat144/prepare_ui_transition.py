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
headers='\n'.join('#include "'+h+'"' for h in ['BIOS.h','Menu.h','Draw.h','Process.h','Function.h','Interrupt.h','UiCommon.h'])+'\n#include <string.h>\n'
init='\n'.join(function(process,n) for n in ['BackGround_Reset','App_HardwareInit','App_init','App_ModeTransition','Process144_ClearFrame'])
(out/'ui_transition_engine.c').write_text(headers+'extern u8 SelectiveUpdate;\n'+init+'\n'+function(common,'UiCommon_ModeTransition'),encoding='ascii')
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
overview=function(draw,'Update_View_Area').replace('void Update_View_Area(', 'void Test_PaintOverview(')
guard=block(menu,menu.index('if(i==TRIGG && j==3 && !NativeHoldoffAvailable())'))
printer=block(menu,menu.index('if (!((i==TRIGG)&&(j==3)&&((NotificationTimer>0)||(Current==FILE))))'))
holdoff=headers+'#include \"NativeWindow.h\"\nextern const u16 RULE_BASE[8];\nvoid DrawPixel(u16,u8,u16);\n'+function(menu,'NativeHoldoffAvailable')+'\n'+overview+'\n'
holdoff+='void Update_View_Area(void) { Test_PaintOverview(); }\n'
holdoff+='void Test_HoldoffTitle(void) {unsigned i=TRIGG,j=3;do {\n'+guard+'\n'+printer+'\n}while(0); }\n'
(out/'holdoff_view.c').write_text(holdoff,encoding='ascii')
