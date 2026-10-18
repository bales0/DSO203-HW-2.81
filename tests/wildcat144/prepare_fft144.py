"""Extract the actual native FFT/tables, not a Python substitute for them."""
from pathlib import Path
import re,sys
root=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
f=(root/'WildcatV6_6/Function.c').read_text(encoding='latin1')
p=(root/'WildcatV6_6/Process.c').read_text(encoding='latin1')
def function(text,name):
 text=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
 m=re.search(r'(?:void|u32|s32) '+name+r'\([^)]*\)\s*\{',text)
 assert m,name
 a=text.index('{',m.start());depth=1;i=a+1
 while depth:
  depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[m.start():i]
def table(text,name):
 m=re.search(r'(?:sc16|uc8) '+name+r'\[[^]]*\][^=]*=\s*(?://[^\n]*\n)?\s*\{.*?\};',text,re.S)
 assert m,name
 return m.group(0)
s='#include "Function.h"\n#include "Menu.h"\n#include "Process.h"\n#include "Fft144.h"\n'
s+='s32 fr[FFTSize],fi[FFTSize];u8 AutoFFT,FFTGain;u32 NFreq;int imax;short PeakFreq;\n'
s+='char NFreqStr[12],BaseFreqStr[12],PeakFreqStr[12],FreqDivStr[12],FreqT1Str[12];\n'
s+='s16 Sinewave[N_WAVE-N_WAVE/4];\n'+table(f,'SineQuad')+'\n'
s+=table(p,'Log10Mant')+'\n'+table(p,'Window')+'\n'
for name in ['LoadNwave','Int_sqrt','fix_fft']:s+=function(f,name)+'\n'
for name in ['Log10Process','FftMagnitude','Process_Fft']:s+=function(p,name)+'\n'
s+=function(f,'Power')+'\n'
s+='#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored \"-Wsign-compare\"\n'
s+=function(f,'Int2Str').replace('void Int2Str(', 'void Native_FftInt2Str(')+'\n'
s+='#pragma GCC diagnostic pop\n'
s+=(root/'tests/wildcat144/fixtures/fft_before.c').read_text(encoding='latin1')
(out/'fft_core.c').write_text(s,encoding='latin1')
